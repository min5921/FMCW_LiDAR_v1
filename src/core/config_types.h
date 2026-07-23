#pragma once

#include "core/frame_types.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace fmcw {

inline constexpr std::uint32_t kConfigSchemaVersion = 5;

enum class Coupling {
  Ac,
  Dc,
};

enum class TriggerSource {
  External,
  Internal,
};

enum class TriggerSlope {
  Rising,
  Falling,
};

enum class AcquisitionMode {
  Single,
  Continuous,
  Finite,
};

enum class AcquisitionSource {
  Simulator,
  Alazar,
  Replay,
};

enum class ChirpTriggerMode {
  UpChirpOnly,
};

enum class WindowFunction {
  Hann,
  Hamming,
  Blackman,
  Rectangular,
};

enum class SegmentPolarity {
  Normal,
  InvertDown,
};

enum class FftBackendKind {
  Cuda,
  Fftw,
};

enum class EdfaMode {
  None,
  Manual,
  Controlled,
};

enum class EdfaControlMode {
  Acc,
  Apc,
  Agc,
};

enum class OpticalPowerUnit {
  Milliwatt,
  Dbm,
};

enum class QueueOverflowPolicy {
  StopAcquisition,
};

enum class UdpBackpressurePolicy {
  LatestFrame,
  PreserveFrames,
  StopSending,
};

enum class SerialParity {
  None,
  Even,
  Odd,
};

struct OpticalPowerSetpoint {
  double value = 10.0;
  OpticalPowerUnit unit = OpticalPowerUnit::Dbm;
};

struct ProfileMetadata {
  std::uint32_t schema_version = kConfigSchemaVersion;
  std::string id = "default";
  std::string name = "Default FMCW LiDAR";
  std::string description = "Self-consistent development full-period acquisition profile";
  std::string author = "FMCW LiDAR Team";
  std::string created_utc = "2026-07-10T00:00:00Z";
  std::string modified_utc = "2026-07-10T00:00:00Z";
};

struct RuntimeConfig {
  AcquisitionSource acquisition_source = AcquisitionSource::Simulator;
  std::string replay_file;
  bool replay_loop = false;
  bool simulator_realtime_dma = false;
};

struct DigitizerConfig {
  std::uint32_t system_id = 1;
  std::uint32_t board_id = 1;
  std::string board_profile = "ats9371";
  DigitizerChannel channel = DigitizerChannel::A;
  double sample_rate_hz = 1.0e9;
  std::uint32_t sample_point = 4096;
  std::uint32_t records_per_buffer = 64;
  std::uint32_t a_scan_count = 64;
  std::uint32_t b_scan_count = 25;
  double input_range_volts = 0.4;
  Coupling coupling = Coupling::Dc;
  std::uint32_t impedance_ohms = 50;
  TriggerSource trigger_source = TriggerSource::External;
  TriggerSlope trigger_slope = TriggerSlope::Rising;
  std::uint32_t trigger_delay_samples = 0;
  std::uint32_t pre_trigger_samples = 0;
  std::uint32_t post_trigger_samples = 4096;
  std::uint32_t timeout_ms = 1000;
  std::uint32_t dma_buffer_count = 8;
  bool fifo_only_streaming = true;
  AcquisitionMode acquisition_mode = AcquisitionMode::Continuous;
  std::uint32_t finite_frame_count = 1;
};

struct LaserConfig {
  double sweep_bandwidth_hz = 2.0e9;
  double sweep_rate_hz = 200.0e3;
};

struct EdfaConfig {
  EdfaMode mode = EdfaMode::None;
  bool required_before_start = false;
  std::string port;
  std::uint32_t baud_rate = 9600;
  SerialParity parity = SerialParity::None;
  std::uint32_t stop_bits = 1;
  std::uint32_t timeout_ms = 500;
  EdfaControlMode control_mode = EdfaControlMode::Apc;
  OpticalPowerSetpoint output_setpoint;
  double output_min_dbm = 0.0;
  double output_max_dbm = 23.0;
  std::uint32_t warmup_delay_ms = 3000;
  bool stop_acquisition_on_disconnect = true;
};

struct ScanConfig {
  double x_start_deg = -9.0;
  double x_end_deg = 9.0;
  double y_start_deg = -9.0;
  double y_end_deg = 9.0;
  bool bidirectional = false;
  std::uint32_t x_pixel_count = 64;
  std::uint32_t y_line_count = 25;
  std::int32_t trigger_shift_samples = 0;
  double scanner_sample_rate_hz = 100000.0;
};

struct ChirpSegmentationConfig {
  ChirpTriggerMode mode = ChirpTriggerMode::UpChirpOnly;
  std::int32_t trigger_to_period_offset = 128;
  SegmentRange up_segment{192, 1920};
  SegmentRange down_segment{2112, 3840};
  std::uint32_t guard_samples = 64;
  std::uint32_t segment_fft_length = 2048;
  WindowFunction window = WindowFunction::Hann;
  SegmentPolarity polarity = SegmentPolarity::InvertDown;
};

struct ProcessingConfig {
  FftBackendKind fft_backend = FftBackendKind::Fftw;
  bool dc_removal = true;
  double peak_threshold_db = -45.0;
  std::uint32_t peak_search_start_bin = 2;
  std::uint32_t peak_search_end_bin = 900;
  std::uint32_t queue_capacity = 32;
  QueueOverflowPolicy overflow_policy = QueueOverflowPolicy::StopAcquisition;
};

struct UdpConfig {
  bool enabled = false;
  std::string target_ip = "192.168.0.10";
  std::uint16_t target_port = 9000;
  std::uint32_t packet_point_count = 256;
  std::uint32_t packet_format_version = 1;
  std::uint32_t queue_capacity = 8;
  UdpBackpressurePolicy backpressure_policy = UdpBackpressurePolicy::LatestFrame;
};

struct StorageConfig {
  bool raw_enabled = false;
  bool processed_enabled = false;
  std::string output_directory = "data/raw";
  std::uint32_t queue_capacity = 64;
  QueueOverflowPolicy overflow_policy = QueueOverflowPolicy::StopAcquisition;
  double split_file_size_gb = 4.0;
  std::uint32_t flush_interval_frames = 128;
};

struct UiConfig {
  double plot_update_hz = 60.0;
  double point_cloud_update_hz = 10.0;
  bool segment_overlay = true;
  std::string color_map = "viridis";
  std::string last_profile = "config/profiles/lab_simulator.yaml";
};

struct CalibrationConfig {
  std::string version = "default-v1";
  double distance_offset_m = 0.0;
  double distance_scale = 1.0;
  double velocity_wavelength_nm = 1550.0;
  double velocity_offset_mps = 0.0;
  double velocity_scale = 1.0;
  double x_angle_offset_deg = 0.0;
  double y_angle_offset_deg = 0.0;
};

struct McuConfig {
  bool enabled = false;
  std::string port;
  std::uint32_t baud_rate = 115200;
  SerialParity parity = SerialParity::None;
  std::uint32_t stop_bits = 1;
  std::uint32_t timeout_ms = 500;
  std::uint32_t retry_count = 2;
  bool require_ack = true;
};

struct SystemConfig {
  ProfileMetadata profile;
  RuntimeConfig runtime;
  DigitizerConfig digitizer;
  LaserConfig laser;
  EdfaConfig edfa;
  ScanConfig scan;
  ChirpSegmentationConfig chirp_segmentation;
  ProcessingConfig processing;
  UdpConfig udp;
  StorageConfig storage;
  UiConfig ui;
  CalibrationConfig calibration;
  McuConfig mcu;
};

SystemConfig makeAts9371QualificationSimulatorConfig();

std::uint32_t derivedAScanCount(const SystemConfig& config);
std::uint64_t derivedFramePointCount(const SystemConfig& config);
double derivedMcuFrameTimeMs(const SystemConfig& config);
double derivedRecordPeriodSeconds(const SystemConfig& config);

std::string toString(DigitizerChannel value);
std::string toString(Coupling value);
std::string toString(TriggerSource value);
std::string toString(TriggerSlope value);
std::string toString(AcquisitionMode value);
std::string toString(AcquisitionSource value);
std::string toString(ChirpTriggerMode value);
std::string toString(WindowFunction value);
std::string toString(SegmentPolarity value);
std::string toString(FftBackendKind value);
std::string toString(EdfaMode value);
std::string toString(EdfaControlMode value);
std::string toString(OpticalPowerUnit value);
std::string toString(QueueOverflowPolicy value);
std::string toString(UdpBackpressurePolicy value);
std::string toString(SerialParity value);

bool fromString(std::string_view text, DigitizerChannel& value);
bool fromString(std::string_view text, Coupling& value);
bool fromString(std::string_view text, TriggerSource& value);
bool fromString(std::string_view text, TriggerSlope& value);
bool fromString(std::string_view text, AcquisitionMode& value);
bool fromString(std::string_view text, AcquisitionSource& value);
bool fromString(std::string_view text, ChirpTriggerMode& value);
bool fromString(std::string_view text, WindowFunction& value);
bool fromString(std::string_view text, SegmentPolarity& value);
bool fromString(std::string_view text, FftBackendKind& value);
bool fromString(std::string_view text, EdfaMode& value);
bool fromString(std::string_view text, EdfaControlMode& value);
bool fromString(std::string_view text, OpticalPowerUnit& value);
bool fromString(std::string_view text, QueueOverflowPolicy& value);
bool fromString(std::string_view text, UdpBackpressurePolicy& value);
bool fromString(std::string_view text, SerialParity& value);

}  // namespace fmcw
