#include "uart_waveform_player.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <sstream>
#include <thread>
#include <cstring>
#include <cerrno>

#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/ioctl.h>

// ttyTHS0 기준으로 쓸 수 있게 Linux/Jetson용으로 변경

namespace {
    speed_t ToBaudConstant(uint32_t baudRate)
    {
        switch (baudRate) {
        case 9600:   return B9600;
        case 19200:  return B19200;
        case 38400:  return B38400;
        case 57600:  return B57600;
        case 115200: return B115200;
        case 230400: return B230400;
        case 460800: return B460800;
        case 921600: return B921600;
#ifdef B1000000
        case 1000000: return B1000000;
#endif
#ifdef B1500000
        case 1500000: return B1500000;
#endif
#ifdef B2000000
        case 2000000: return B2000000;
#endif
#ifdef B3000000
        case 3000000: return B3000000;
#endif
        default:
            return 0;
        }
    }
}

UartWaveformPlayer::UartWaveformPlayer()
{
}

UartWaveformPlayer::~UartWaveformPlayer()
{
    Shutdown();
}

bool UartWaveformPlayer::Init(const std::string& portName, uint32_t baudRate)
{
    lastError_.clear();
    ready_ = false;
    running_ = false;
    portName_ = portName;
    baudRate_ = baudRate;

    if (!OpenSerial(portName, baudRate)) {
        return false;
    }

    // MCU reset / boot 대기
    std::this_thread::sleep_for(std::chrono::seconds(2));

    Purge();
    return true;
}

void UartWaveformPlayer::Shutdown()
{
    if (running_) {
        Stop();
    }

    ready_ = false;
    running_ = false;
    frames_.clear();
    sps_ = 0.0;
    CloseSerial();
}

bool UartWaveformPlayer::IsOpen() const
{
    return fdSerial_ >= 0;
}

bool UartWaveformPlayer::IsReady() const
{
    return ready_;
}

bool UartWaveformPlayer::IsRunning() const
{
    return running_;
}

bool UartWaveformPlayer::LoadWaveformFile(const std::string& path)
{
    lastError_.clear();
    ready_ = false;
    running_ = false;

    double sps = 0.0;
    std::vector<XYMRow> rows;

    if (!LoadXYMFile(path, sps, rows)) {
        return false;
    }

    sps_ = sps;
    BuildFramesFromRows(rows);
    return true;
}

bool UartWaveformPlayer::UploadToMCU()
{
    lastError_.clear();

    if (!IsOpen()) {
        lastError_ = "Serial port is not open.";
        return false;
    }

    if (frames_.empty()) {
        lastError_ = "No waveform frames loaded.";
        return false;
    }

    if (!SendLine("CLR", "ACK:CLR", 2.0, true)) {
        return false;
    }

    int total = 0;
    const int chunk = 100;

    for (int rep = 0; rep < REPEAT_COUNT; ++rep) {
        for (size_t i = 0; i < frames_.size(); ++i) {
            const UartFrame& f = frames_[i];

            std::ostringstream oss;
            oss << "DATA,"
                << f.a << ","
                << f.b << ","
                << f.c << ","
                << f.d << ","
                << f.m
                << "\n";

            if (!WriteString(oss.str())) {
                lastError_ = "Failed to write DATA line.";
                ready_ = false;
                return false;
            }

            ++total;

            if (((int)i + 1) % chunk == 0) {
                if (!Flush()) {
                    lastError_ = "Flush failed during DATA upload.";
                    ready_ = false;
                    return false;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        }
    }

    if (!Flush()) {
        lastError_ = "Final flush failed after DATA upload.";
        ready_ = false;
        return false;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    if (!SendLine("LOAD_DONE", "ACK:LOAD_DONE", 5.0, true)) {
        ready_ = false;
        return false;
    }

    ready_ = true;
    running_ = false;
    return true;
}

bool UartWaveformPlayer::Start()
{
    lastError_.clear();

    if (!IsOpen()) {
        lastError_ = "Serial port is not open.";
        return false;
    }

    if (!ready_) {
        lastError_ = "Waveform is not ready.";
        return false;
    }

    if (!SendLine("START", "ACK:START", 2.0, true)) {
        running_ = false;
        return false;
    }

    running_ = true;
    return true;
}

bool UartWaveformPlayer::Stop()
{
    lastError_.clear();

    if (!IsOpen()) {
        lastError_ = "Serial port is not open.";
        running_ = false;
        return false;
    }

    if (!SendLine("STOP", "ACK:STOP", 2.0, true)) {
        running_ = false;
        return false;
    }

    running_ = false;
    return true;
}

bool UartWaveformPlayer::OpenSerial(const std::string& portName, uint32_t baudRate)
{
    CloseSerial();

    const speed_t baudConst = ToBaudConstant(baudRate);
    if (baudConst == 0) {
        lastError_ = "Unsupported baud rate: " + std::to_string(baudRate);
        return false;
    }

    fdSerial_ = open(portName.c_str(), O_RDWR | O_NOCTTY | O_SYNC);
    if (fdSerial_ < 0) {
        lastError_ = "Failed to open serial port: " + portName +
            " (" + std::strerror(errno) + ")";
        return false;
    }

    struct termios tty {};
    if (tcgetattr(fdSerial_, &tty) != 0) {
        lastError_ = "tcgetattr failed: " + std::string(std::strerror(errno));
        CloseSerial();
        return false;
    }

    cfmakeraw(&tty);

    if (cfsetispeed(&tty, baudConst) != 0 || cfsetospeed(&tty, baudConst) != 0) {
        lastError_ = "Failed to set baudrate.";
        CloseSerial();
        return false;
    }

    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;
    tty.c_cflag &= ~CRTSCTS;
    tty.c_cflag |= CREAD | CLOCAL;

    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    tty.c_oflag &= ~OPOST;

    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 1; // 0.1 sec

    if (tcsetattr(fdSerial_, TCSANOW, &tty) != 0) {
        lastError_ = "tcsetattr failed: " + std::string(std::strerror(errno));
        CloseSerial();
        return false;
    }

    Purge();
    return true;
}

void UartWaveformPlayer::CloseSerial()
{
    if (fdSerial_ >= 0) {
        close(fdSerial_);
        fdSerial_ = -1;
    }
}

void UartWaveformPlayer::Purge()
{
    if (IsOpen()) {
        tcflush(fdSerial_, TCIOFLUSH);
    }
}

bool UartWaveformPlayer::WriteBytes(const char* data, size_t len)
{
    if (!IsOpen()) {
        lastError_ = "Serial port not open.";
        return false;
    }

    size_t totalWritten = 0;
    while (totalWritten < len) {
        ssize_t n = write(fdSerial_, data + totalWritten, len - totalWritten);
        if (n < 0) {
            if (errno == EINTR)
                continue;

            lastError_ = "write failed: " + std::string(std::strerror(errno));
            return false;
        }

        if (n == 0) {
            lastError_ = "write returned 0.";
            return false;
        }

        totalWritten += static_cast<size_t>(n);
    }

    return true;
}

bool UartWaveformPlayer::WriteString(const std::string& s)
{
    return WriteBytes(s.data(), s.size());
}

bool UartWaveformPlayer::Flush()
{
    if (!IsOpen()) {
        lastError_ = "Serial port not open.";
        return false;
    }

    if (tcdrain(fdSerial_) != 0) {
        lastError_ = "tcdrain failed: " + std::string(std::strerror(errno));
        return false;
    }

    return true;
}

bool UartWaveformPlayer::ReadLine(std::string& out, double timeoutSec)
{
    out.clear();

    auto t0 = std::chrono::steady_clock::now();
    std::string buf;

    while (true) {
        char ch = 0;
        ssize_t nread = read(fdSerial_, &ch, 1);

        if (nread < 0) {
            if (errno != EINTR && errno != EAGAIN) {
                lastError_ = "read failed: " + std::string(std::strerror(errno));
                return false;
            }
        }
        else if (nread == 1) {
            if (ch == '\n') {
                while (!buf.empty() && (buf.back() == '\r' || buf.back() == '\n')) {
                    buf.pop_back();
                }

                if (!buf.empty()) {
                    out = buf;
                    return true;
                }
            }
            else {
                buf.push_back(ch);
            }
        }

        auto now = std::chrono::steady_clock::now();
        double dt = std::chrono::duration<double>(now - t0).count();
        if (dt >= timeoutSec) {
            while (!buf.empty() && (buf.back() == '\r' || buf.back() == '\n')) {
                buf.pop_back();
            }

            if (!buf.empty()) {
                out = buf;
                return true;
            }

            lastError_ = "Read timeout.";
            return false;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

bool UartWaveformPlayer::ReadResponse(std::string& out, double timeoutSec)
{
    if (!ReadLine(out, timeoutSec)) {
        return false;
    }

    if (out.rfind("ERR:", 0) == 0) {
        lastError_ = "MCU error response: " + out;
        return false;
    }

    return true;
}

bool UartWaveformPlayer::SendLine(const std::string& line,
    const char* expectPrefix,
    double timeoutSec,
    bool verbose)
{
    std::string payload = line + "\n";

    if (!WriteString(payload)) {
        return false;
    }

    if (!Flush()) {
        return false;
    }

    if (expectPrefix == nullptr || expectPrefix[0] == '\0') {
        return true;
    }

    std::string rsp;
    if (!ReadResponse(rsp, timeoutSec)) {
        if (lastError_.empty()) {
            lastError_ = "No response for command: " + line;
        }
        return false;
    }

    if (rsp.rfind(expectPrefix, 0) != 0) {
        lastError_ = "Unexpected response for '" + line +
            "'. Expected prefix '" + expectPrefix +
            "', got '" + rsp + "'";
        return false;
    }

    return true;
}

bool UartWaveformPlayer::LoadXYMFile(const std::string& path,
    double& spsOut,
    std::vector<XYMRow>& rowsOut)
{
    std::ifstream fin(path);
    if (!fin.is_open()) {
        lastError_ = "Failed to open waveform file: " + path;
        return false;
    }

    spsOut = 0.0;
    rowsOut.clear();

    std::string line;
    while (std::getline(fin, line)) {
        if (line.empty()) {
            continue;
        }

        std::istringstream iss(line);

        if (line.rfind("sps", 0) == 0) {
            std::string key;
            double val = 0.0;
            if (iss >> key >> val) {
                spsOut = val;
            }
            continue;
        }

        double x = 0.0;
        double y = 0.0;
        double mAsDouble = 0.0;

        if (!(iss >> x >> y >> mAsDouble)) {
            continue;
        }

        XYMRow row;
        row.x = x;
        row.y = y;
        row.m = (int)std::lround(mAsDouble);

        rowsOut.push_back(row);
    }

    return true;
}

void UartWaveformPlayer::BuildFramesFromRows(const std::vector<XYMRow>& rows)
{
    frames_.clear();
    frames_.reserve(rows.size());

    for (const auto& row : rows) {
        UartFrame f;
        XYtoABCD(row.x, row.y, f.a, f.b, f.c, f.d);
        f.m = row.m;
        frames_.push_back(f);
    }
}

double UartWaveformPlayer::Clamp(double x, double lo, double hi)
{
    return std::max(lo, std::min(hi, x));
}

uint16_t UartWaveformPlayer::VoltageToCode(double v)
{
    v = Clamp(v, 0.0, V_FULL);
    int code = (int)std::llround((v / V_FULL) * 65535.0);
    code = std::max(0, std::min(65535, code));
    return (uint16_t)code;
}

void UartWaveformPlayer::XYtoABCD(double x, double y,
    uint16_t& a, uint16_t& b, uint16_t& c, uint16_t& d)
{
    double xNorm = Clamp(x, -1.0, 1.0);
    double yNorm = Clamp(y, -1.0, 1.0);

    double vx = xNorm * VDIFF_MAX;
    double vy = yNorm * VDIFF_MAX;

    double A = VBIAS + vx / 2.0;
    double B = VBIAS - vx / 2.0;
    double C = VBIAS + vy / 2.0;
    double D = VBIAS - vy / 2.0;

    a = VoltageToCode(A);
    b = VoltageToCode(B);
    c = VoltageToCode(C);
    d = VoltageToCode(D);
}