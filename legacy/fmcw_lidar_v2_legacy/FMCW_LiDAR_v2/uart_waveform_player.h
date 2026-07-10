#pragma once

#include <cstdint>
#include <string>
#include <vector>

// ----------------------------
// Data structures
// ----------------------------
struct XYMRow
{
    double x = 0.0;
    double y = 0.0;
    int m = 0;
};

struct UartFrame
{
    uint16_t a = 0;
    uint16_t b = 0;
    uint16_t c = 0;
    uint16_t d = 0;
    int m = 0;
};

// ----------------------------
// UART Waveform Player
// ----------------------------
class UartWaveformPlayer
{
public:
    UartWaveformPlayer();
    ~UartWaveformPlayer();

    bool Init(const std::string& portName, uint32_t baudRate);
    void Shutdown();

    bool IsOpen() const;
    bool IsReady() const;
    bool IsRunning() const;

    bool LoadWaveformFile(const std::string& path);
    bool UploadToMCU();
    bool Start();
    bool Stop();

    const std::vector<UartFrame>& GetFrames() const { return frames_; }
    const std::string& GetLastError() const { return lastError_; }
    double GetSps() const { return sps_; }

private:
    // ------------------------
    // Serial control
    // ------------------------
    bool OpenSerial(const std::string& portName, uint32_t baudRate);
    void CloseSerial();
    void Purge();

    bool WriteBytes(const char* data, size_t len);
    bool WriteString(const std::string& s);
    bool Flush();

    bool ReadLine(std::string& out, double timeoutSec);
    bool ReadResponse(std::string& out, double timeoutSec);
    bool SendLine(const std::string& line,
        const char* expectPrefix,
        double timeoutSec,
        bool verbose);

    // ------------------------
    // Waveform processing
    // ------------------------
    bool LoadXYMFile(const std::string& path,
        double& spsOut,
        std::vector<XYMRow>& rowsOut);

    void BuildFramesFromRows(const std::vector<XYMRow>& rows);

    static double Clamp(double x, double lo, double hi);
    static uint16_t VoltageToCode(double v);
    static void XYtoABCD(double x, double y,
        uint16_t& a, uint16_t& b, uint16_t& c, uint16_t& d);

private:
    // ------------------------
    // Linux UART handle
    // ------------------------
    int fdSerial_ = -1;

    std::string portName_;
    uint32_t baudRate_ = 115200;

    std::vector<UartFrame> frames_;
    double sps_ = 0.0;

    bool ready_ = false;
    bool running_ = false;

    std::string lastError_;

private:
    // ------------------------
    // Device constants
    // ------------------------
    static constexpr double VBIAS = 90.0;
    static constexpr double VDIFF_MAX = 120.0;
    static constexpr double V_FULL = 200.0;
    static constexpr double ACK_TIMEOUT = 5.0;
    static constexpr int REPEAT_COUNT = 1;
};