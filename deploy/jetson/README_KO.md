# FMCW LiDAR Jetson Source Bundle

이 폴더는 Windows 실행 파일을 변환하는 도구가 아니다. 현재 FMCW LiDAR 공통
core, Qt UI, CUDA/cuFFT, POSIX serial, UDP, storage 소스를 Jetson ARM64에서
native Release로 빌드한다. Jetson 버전의 신호 처리는 CUDA/cuFFT 전용이다.

## 1. 지원 범위

- Qt 6.2 이상 기반 Jetson 로컬 UI
- CUDA/cuFFT batch 처리
- AlazarTech Linux ARM64 SDK가 설치된 경우 지원 ATS 12-bit adapter
- `/dev/tty*` 기반 MCU/EDFA serial
- UDP와 raw/processed storage
- Simulator와 Raw Replay

현재 단계는 빌드 가능한 Jetson 소스 전달이다. 정확한 JetPack/kernel용 AlazarTech
driver, 실제 DMA, CUDA 성능, NVMe, 온도 및 장시간 동작은 Jetson에서 검증해야 한다.

## 2. 소스 복사

Windows에서 생성된 다음 폴더 또는 ZIP 전체를 Jetson으로 복사한다.

```text
FMCW_LiDAR_Jetson_Source
FMCW_LiDAR_Jetson_Source.zip
```

경로에는 공백이 없어도 되고 있어도 된다. 빌드 결과가 소스 폴더 아래 `build`에
생성되므로 최소 10 GB 이상의 여유 공간을 권장한다.

## 3. 기본 개발 패키지

JetPack에 맞는 CUDA는 NVIDIA JetPack 방식으로 설치한다. 일반 Ubuntu 패키지는
다음과 같이 준비할 수 있다.

```bash
sudo apt update
sudo apt install -y \
  build-essential ninja-build pkg-config \
  qt6-base-dev libqt6opengl6-dev libgl1-mesa-dev \
  libglvnd-dev
```

프로젝트는 CMake 3.18 이상을 요구한다. Jetson의 기본 CMake가 더 오래됐다면
JetPack/Ubuntu 버전에 맞는 새 CMake를 먼저 설치한다.

CUDA와 cuFFT는 반드시 JetPack에 포함된 버전을 사용한다. `nvcc` 또는
`libcufft.so`가 없으면 빌드 스크립트가 즉시 중단된다.

cuFFT 검사는 Jetson 표준 위치인
`/usr/local/cuda/targets/aarch64-linux/lib/libcufft.so`와
`/usr/local/cuda/lib64/libcufft.so`를 먼저 확인하고, 이후 심볼릭 링크를 따라가는
`find -L /usr/local/cuda` 검색을 사용한다. 성공 시 감지된 실제 경로를 출력한다.
`set -Eeuo pipefail`에서 SIGPIPE로 결과가 흔들리지 않도록 의존성 판정에는
`producer | grep -q` 형식을 사용하지 않는다.

## 4. AlazarTech 준비

실제 ATS 보드를 사용할 경우 현재 JetPack kernel과 맞는 AlazarTech ARM64 driver와
Linux SDK를 설치한다. 기본 탐색 위치는 다음과 같다.

```text
/usr/local/AlazarTech/include/AlazarApi.h
/usr/local/AlazarTech/lib/libATSApi.so
```

SDK 위치가 다르면 `deploy/jetson/jetson.env`의
`FMCW_JETSON_ALAZAR_SDK_ROOT`를 수정한다. SDK가 아직 없다면 최초 UI/Simulator
빌드에서 `FMCW_JETSON_WITH_ALAZAR=OFF`로 설정할 수 있다.

## 5. 빌드 설정

`deploy/jetson/jetson.env` 한 파일만 수정한다.

- `FMCW_JETSON_WITH_ALAZAR`: 실제 지원 ATS adapter
- `FMCW_JETSON_ALAZAR_SDK_ROOT`: ARM64 SDK 위치
- `FMCW_JETSON_CUDA_ARCHITECTURES`: 기본값 `auto`; Jetson 모델에서 숫자 architecture 자동 결정
- `FMCW_JETSON_QT_ROOT`: system Qt 6.2 이상이 아닐 때 Qt CMake prefix

Jetson 빌드는 `FMCW_WITH_FFTW=OFF`, `FMCW_WITH_CUDA=ON`,
`FMCW_REQUIRE_CUDA=ON`으로 고정된다. UI에도 `CUDA cuFFT`만 표시된다.

## 6. 빌드

소스 폴더에서 다음 한 줄을 실행한다.

```bash
bash deploy/jetson/build.sh
```

의존성 검사를 반복 확인하고 clean build를 수행하려면 다음 순서로 실행한다.

```bash
for i in {1..20}; do
  bash deploy/jetson/check_dependencies.sh || exit 1
done

rm -rf build/jetson-release
bash deploy/jetson/build.sh
```

Alazar SDK header와 `libATSApi.so`가 감지되었지만 device node가 없을 때 표시되는
경고는 CUDA/cuFFT 오류가 아니다. 이 항목은 warning으로 유지되며 빌드를 중단하지 않는다.

스크립트는 다음을 순서대로 수행한다.

1. ARM64, CMake, Ninja, Qt 6.2 이상, CUDA/cuFFT, Alazar SDK 점검
2. `Release` 구성
3. Jetson 실행 파일 빌드
4. CTest 실행
5. Qt smoke test
6. 실행 패키지 생성

## 7. 실행

빌드가 끝나면 Jetson 데스크톱 터미널에서 실행한다.

```bash
bash build/package/FMCW_LiDAR_Jetson/run.sh
```

실행 패키지는 다음 위치에 생성된다.

```text
build/package/FMCW_LiDAR_Jetson
```

`runtime_dependencies.txt`에서 `not found`가 없어야 한다. MCU와 EDFA serial port를
사용하려면 로그인 사용자를 해당 장치 권한 그룹에 추가해야 할 수 있다.

```bash
sudo usermod -aG dialout "$USER"
```

그룹 변경은 로그아웃 후 다시 로그인해야 반영된다.

AGX Orin에서는 MCU를 J30 40핀 헤더의 UART1에 직접 연결한다. 물리 핀 8은
Jetson TX이므로 MCU RX에, 핀 10은 Jetson RX이므로 MCU TX에 연결하고 GND를
공통으로 연결한다. GUI에는 이 포트가
`Jetson 40-pin UART (pins 8/10) | /dev/ttyTHS0`으로 표시된다. EDFA는 별도의
USB-FTDI 장치(`/dev/ttyUSB*`)를 선택해야 하며 MCU와 같은 포트를 선택하면
설정 검증이 Apply와 Connect를 차단한다. UART 장치가 보이지 않으면 Jetson-IO로
J30 UART1 기능을 활성화한 뒤 재부팅한다. `check_dependencies.sh`는
`FMCW_JETSON_MCU_UART`의 존재 여부와 현재 사용자의 읽기/쓰기 권한을 경고로
진단하며, 선택 장치가 없어도 빌드는 중단하지 않는다.

## 8. 최초 검증 순서

1. `Simulator + CUDA`로 Qt UI와 CUDA backend 확인
2. System 1 / Board 1에서 자동 감지된 ATS 모델과 driver/device node 확인
3. `AlazarTech ATS + CUDA`로 짧은 DMA 확인
4. 4992 sample x 998 record, 200 Hz 조건의 batch 처리 확인
5. MCU/EDFA를 하나씩 활성화
6. NVMe raw 저장과 장시간 성능 검증

Jetson에서 생성된 `BUILD_INFO.txt`, `runtime_dependencies.txt`, `SHA256SUMS`, CTest
출력을 Windows 프로젝트로 다시 가져오면 다음 하드웨어 검증에 사용할 수 있다.
