import time
import numpy as np
import serial
import matplotlib.pyplot as plt

# =========================================================
# 사용자 설정
# =========================================================
INPUT_FILE = r"waveform.txt"
OUTPUT_FILE = r"waveform_resample.txt"

PORT = "COM4"
BAUD = 115200
SER_TIMEOUT = 1.0

DST_SPS = 100000.0

# UART 전송 옵션
SEND_TO_MCU = False         # False면 plot/저장만, True면 UART까지 전송
AUTO_START = False
CHUNK_SIZE = 20
CHUNK_SLEEP = 0.01

# Plot 옵션
SHOW_FULL_PLOT = True
PLOT_PREVIEW_SAMPLES = 3000   # SHOW_FULL_PLOT=False일 때만 사용

# x,y -> DAC code 변환 범위
XY_INPUT_MIN = -1.0
XY_INPUT_MAX = 1.0

DAC_MIN = 0
DAC_MAX = 65535

# M 처리
HIGH_M_VALUE = 200
LOW_M_VALUE = 0


# =========================================================
# 파일 로드
# =========================================================
def load_xym_file(path):
    """
    입력 형식:
        sps 96000
        x y M
        x y M
        ...
    """
    with open(path, "r", encoding="utf-8") as f:
        lines = [line.strip() for line in f if line.strip()]

    if not lines:
        raise RuntimeError("입력 파일이 비어 있음")

    header = lines[0].split()
    if len(header) < 2 or header[0].lower() != "sps":
        raise RuntimeError("첫 줄은 반드시 'sps 96000' 형식이어야 함")

    src_sps = float(header[1])

    data = []
    for line_no, line in enumerate(lines[1:], start=2):
        parts = line.split()
        if len(parts) < 3:
            print(f"[WARN] skip line {line_no}: {line}")
            continue

        x = float(parts[0])
        y = float(parts[1])
        m = float(parts[2])
        data.append([x, y, m])

    if len(data) < 2:
        raise RuntimeError("보간하려면 최소 2개 이상의 샘플이 필요함")

    return src_sps, np.array(data, dtype=np.float64)


# =========================================================
# 같은 형식으로 저장
# =========================================================
def save_xym_file(path, sps, data):
    """
    저장 형식:
        sps 100000
        x y M
        ...
    """
    with open(path, "w", encoding="utf-8") as f:
        f.write(f"sps {int(round(sps))}\n")
        for x, y, m in data:
            f.write(f"{x:.7f} {y:.7f} {int(round(m))}\n")


# =========================================================
# 보간
# =========================================================
def resample_xym(data, src_sps, dst_sps):
    """
    data shape: (N,3) => x,y,m
    x,y : linear interpolation
    m   : nearest-neighbor
    """
    n_src = len(data)
    n_dst = int(round(n_src * dst_sps / src_sps))

    t_src = np.arange(n_src) / src_sps
    t_dst = np.arange(n_dst) / dst_sps

    out = np.zeros((n_dst, 3), dtype=np.float64)

    # x, y 선형 보간
    out[:, 0] = np.interp(t_dst, t_src, data[:, 0])
    out[:, 1] = np.interp(t_dst, t_src, data[:, 1])

    # m nearest neighbor
    idx = np.round(t_dst * src_sps).astype(int)
    idx = np.clip(idx, 0, n_src - 1)
    out[:, 2] = data[idx, 2]

    return out


# =========================================================
# x,y -> DAC code
# =========================================================
def float_to_dac_code(v, in_min=XY_INPUT_MIN, in_max=XY_INPUT_MAX,
                      dac_min=DAC_MIN, dac_max=DAC_MAX):
    if in_max <= in_min:
        raise ValueError("XY_INPUT_MAX must be > XY_INPUT_MIN")

    norm = (v - in_min) / (in_max - in_min)
    code = np.round(norm * (dac_max - dac_min) + dac_min)
    code = np.clip(code, dac_min, dac_max)
    return code.astype(np.uint16)


def convert_xym_to_abcdm(data_xym):
    """
    현재는 가장 단순하게:
      A = X
      B = X
      C = Y
      D = Y
    """
    x = data_xym[:, 0]
    y = data_xym[:, 1]
    m = data_xym[:, 2]

    x_code = float_to_dac_code(x)
    y_code = float_to_dac_code(y)
    m_code = np.where(m >= 0.5, HIGH_M_VALUE, LOW_M_VALUE).astype(np.uint8)

    a = x_code
    b = x_code
    c = y_code
    d = y_code

    return list(zip(a, b, c, d, m_code))


# =========================================================
# Plot
# =========================================================
def plot_preview(src_data, src_sps, dst_data, dst_sps, show_full=True, preview_src_samples=1000):
    if show_full:
        preview_src_samples = len(src_data)
    else:
        preview_src_samples = min(preview_src_samples, len(src_data))

    t_src = np.arange(len(src_data)) / src_sps
    t_dst = np.arange(len(dst_data)) / dst_sps

    t_src_part = t_src[:preview_src_samples]
    x_src_part = src_data[:preview_src_samples, 0]
    y_src_part = src_data[:preview_src_samples, 1]
    m_src_part = src_data[:preview_src_samples, 2]

    max_t = t_src_part[-1]
    mask_dst = t_dst <= max_t
    t_dst_part = t_dst[mask_dst]
    x_dst_part = dst_data[mask_dst, 0]
    y_dst_part = dst_data[mask_dst, 1]
    m_dst_part = dst_data[mask_dst, 2]

    plt.figure(figsize=(12, 5))
    plt.plot(t_src_part, x_src_part, label=f"X original @ {src_sps:.0f} sps")
    plt.plot(t_dst_part, x_dst_part, label=f"X resampled @ {dst_sps:.0f} sps")
    plt.xlabel("Time (s)")
    plt.ylabel("X")
    plt.title("X comparison")
    plt.grid(True)
    plt.legend()
    plt.tight_layout()

    plt.figure(figsize=(12, 5))
    plt.plot(t_src_part, y_src_part, label=f"Y original @ {src_sps:.0f} sps")
    plt.plot(t_dst_part, y_dst_part, label=f"Y resampled @ {dst_sps:.0f} sps")
    plt.xlabel("Time (s)")
    plt.ylabel("Y")
    plt.title("Y comparison")
    plt.grid(True)
    plt.legend()
    plt.tight_layout()

    plt.figure(figsize=(12, 4))
    plt.step(t_src_part, m_src_part, where="post", label=f"M original @ {src_sps:.0f} sps")
    plt.step(t_dst_part, m_dst_part, where="post", label=f"M resampled @ {dst_sps:.0f} sps")
    plt.xlabel("Time (s)")
    plt.ylabel("M")
    plt.title("M comparison")
    plt.grid(True)
    plt.legend()
    plt.tight_layout()

    plt.show()


# =========================================================
# UART helper
# =========================================================
def read_response_line(ser, timeout=1.0):
    t0 = time.time()
    buf = b""

    while time.time() - t0 < timeout:
        chunk = ser.read(1)
        if chunk:
            buf += chunk
            if buf.endswith(b"\n"):
                break

    if not buf:
        return None

    return buf.decode("ascii", errors="replace").strip()


def send_line(ser, line, expect_prefix=None, timeout=1.0, verbose=True):
    tx = line.rstrip("\n") + "\n"

    if verbose:
        print(f"TX: {tx.strip()}")

    ser.write(tx.encode("ascii"))
    ser.flush()

    rsp = read_response_line(ser, timeout=timeout)
    if rsp is None:
        raise RuntimeError(f"No response for command: {line}")

    if verbose:
        print(f"RX: {rsp}")

    if rsp.startswith("ERR:"):
        raise RuntimeError(f"MCU error response: {rsp}")

    if expect_prefix is not None and not rsp.startswith(expect_prefix):
        raise RuntimeError(f"Unexpected response. expected={expect_prefix}, got={rsp}")

    return rsp


def send_frames_to_mcu(port, baud, frames, auto_start=False):
    ser = None
    try:
        ser = serial.Serial(port, baud, timeout=SER_TIMEOUT)
        time.sleep(2.0)

        print("Clearing MCU buffer...")
        send_line(ser, "CLR", expect_prefix="ACK:CLR", timeout=2.0, verbose=True)

        total = 0
        for i, (a, b, c, d, m) in enumerate(frames, start=1):
            line = f"DATA,{int(a)},{int(b)},{int(c)},{int(d)},{int(m)}\n"
            ser.write(line.encode("ascii"))
            total += 1

            if (i % CHUNK_SIZE) == 0:
                ser.flush()
                time.sleep(CHUNK_SLEEP)

        ser.flush()
        time.sleep(0.5)

        print(f"Sent total {total} DATA frames.")

        rsp = send_line(ser, "LOAD_DONE", expect_prefix="ACK:LOAD_DONE", timeout=5.0, verbose=True)
        print(f"LOAD_DONE response: {rsp}")

        if auto_start:
            send_line(ser, "START", expect_prefix="ACK:START", timeout=2.0, verbose=True)

        print("UART transmission done.")

    finally:
        if ser is not None:
            ser.close()
            print("Serial port closed.")


# =========================================================
# main
# =========================================================
def main():
    src_sps, src_data = load_xym_file(INPUT_FILE)

    n_src = len(src_data)
    duration_src = n_src / src_sps

    print(f"Loaded {n_src} source samples")
    print(f"Source SPS      : {src_sps}")
    print(f"Source duration : {duration_src:.9f} s")

    dst_data = resample_xym(src_data, src_sps, DST_SPS)

    n_dst = len(dst_data)
    duration_dst = n_dst / DST_SPS

    print(f"Destination SPS : {DST_SPS}")
    print(f"Resampled count : {n_dst}")
    print(f"Resampled dur.  : {duration_dst:.9f} s")
    print(f"Duration error  : {abs(duration_dst - duration_src):.12f} s")

    # 같은 형식으로 저장
    save_xym_file(OUTPUT_FILE, DST_SPS, dst_data)
    print(f"Saved resampled file: {OUTPUT_FILE}")

    # Plot
    plot_preview(
        src_data,
        src_sps,
        dst_data,
        DST_SPS,
        show_full=SHOW_FULL_PLOT,
        preview_src_samples=PLOT_PREVIEW_SAMPLES
    )

    # UART 전송용 frame 변환
    frames = convert_xym_to_abcdm(dst_data)
    print(f"Prepared {len(frames)} UART DATA frames")

    if SEND_TO_MCU:
        send_frames_to_mcu(PORT, BAUD, frames, auto_start=AUTO_START)


if __name__ == "__main__":
    main()
