#include "core/config_profile.h"

#include <cmath>
#include <exception>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace fmcw {
namespace {

void addIssue(std::vector<ConfigProfileIssue>& issues, const std::string& source,
              std::string path, std::string message, std::size_t line = 0) {
  issues.push_back({source, line, std::move(path), std::move(message)});
}

const ConfigScalar* scalarAt(const ConfigDocument& document, const std::string& path,
                             std::vector<ConfigProfileIssue>& issues, const std::string& source) {
  const auto* scalar = document.find(path);
  if (scalar == nullptr) {
    addIssue(issues, source, path, "Required configuration key is missing");
  }
  return scalar;
}

bool readString(const ConfigDocument& document, const std::string& path, std::string& value,
                std::vector<ConfigProfileIssue>& issues, const std::string& source) {
  const auto* scalar = scalarAt(document, path, issues, source);
  if (scalar == nullptr) {
    return false;
  }
  if (scalar->kind != ConfigScalarKind::String) {
    addIssue(issues, source, path, "Expected a string scalar");
    return false;
  }
  value = scalar->text;
  return true;
}

bool readBool(const ConfigDocument& document, const std::string& path, bool& value,
              std::vector<ConfigProfileIssue>& issues, const std::string& source) {
  const auto* scalar = scalarAt(document, path, issues, source);
  if (scalar == nullptr) {
    return false;
  }
  if (scalar->kind != ConfigScalarKind::Boolean) {
    addIssue(issues, source, path, "Expected true or false");
    return false;
  }
  value = scalar->text == "true";
  return true;
}

template <typename Integer>
bool readInteger(const ConfigDocument& document, const std::string& path, Integer& value,
                 std::vector<ConfigProfileIssue>& issues, const std::string& source) {
  static_assert(std::is_integral<Integer>::value, "Integer type required");
  const auto* scalar = scalarAt(document, path, issues, source);
  if (scalar == nullptr) {
    return false;
  }
  if (scalar->kind != ConfigScalarKind::Integer) {
    addIssue(issues, source, path, "Expected an integer scalar");
    return false;
  }

  try {
    std::size_t consumed = 0;
    if constexpr (std::is_signed<Integer>::value) {
      const long long parsed = std::stoll(scalar->text, &consumed, 10);
      if (consumed != scalar->text.size() || parsed < static_cast<long long>(std::numeric_limits<Integer>::min()) ||
          parsed > static_cast<long long>(std::numeric_limits<Integer>::max())) {
        throw std::out_of_range("integer range");
      }
      value = static_cast<Integer>(parsed);
    } else {
      if (!scalar->text.empty() && scalar->text.front() == '-') {
        throw std::out_of_range("unsigned range");
      }
      const unsigned long long parsed = std::stoull(scalar->text, &consumed, 10);
      if (consumed != scalar->text.size() || parsed > static_cast<unsigned long long>(std::numeric_limits<Integer>::max())) {
        throw std::out_of_range("integer range");
      }
      value = static_cast<Integer>(parsed);
    }
    return true;
  } catch (const std::exception&) {
    addIssue(issues, source, path, "Integer is outside the supported range");
    return false;
  }
}

bool readNumber(const ConfigDocument& document, const std::string& path, double& value,
                std::vector<ConfigProfileIssue>& issues, const std::string& source) {
  const auto* scalar = scalarAt(document, path, issues, source);
  if (scalar == nullptr) {
    return false;
  }
  if (scalar->kind != ConfigScalarKind::Number && scalar->kind != ConfigScalarKind::Integer) {
    addIssue(issues, source, path, "Expected a numeric scalar");
    return false;
  }
  try {
    std::size_t consumed = 0;
    const double parsed = std::stod(scalar->text, &consumed);
    if (consumed != scalar->text.size() || !std::isfinite(parsed)) {
      throw std::out_of_range("number range");
    }
    value = parsed;
    return true;
  } catch (const std::exception&) {
    addIssue(issues, source, path, "Number is invalid or outside the supported range");
    return false;
  }
}

template <typename Enum>
bool readEnum(const ConfigDocument& document, const std::string& path, Enum& value,
              std::vector<ConfigProfileIssue>& issues, const std::string& source) {
  std::string text;
  if (!readString(document, path, text, issues, source)) {
    return false;
  }
  if (!fromString(text, value)) {
    addIssue(issues, source, path, "Unsupported option: " + text);
    return false;
  }
  return true;
}

void setUnsigned(ConfigDocument& document, std::string path, std::uint64_t value) {
  document.setInteger(std::move(path), static_cast<std::int64_t>(value));
}

bool readFile(const std::filesystem::path& path, std::string& text, std::string& error) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    error = "Unable to open profile: " + path.string();
    return false;
  }
  std::ostringstream buffer;
  buffer << stream.rdbuf();
  if (!stream.good() && !stream.eof()) {
    error = "Unable to read profile: " + path.string();
    return false;
  }
  text = buffer.str();
  return true;
}

}  // namespace

ConfigDocument ConfigProfileCodec::encode(const SystemConfig& config) {
  ConfigDocument document;

  setUnsigned(document, "profile.schema_version", config.profile.schema_version);
  document.setString("profile.id", config.profile.id);
  document.setString("profile.name", config.profile.name);
  document.setString("profile.description", config.profile.description);
  document.setString("profile.author", config.profile.author);
  document.setString("profile.created_utc", config.profile.created_utc);
  document.setString("profile.modified_utc", config.profile.modified_utc);

  document.setString("runtime.acquisition_source", toString(config.runtime.acquisition_source));
  document.setString("runtime.replay_file", config.runtime.replay_file);
  document.setBoolean("runtime.replay_loop", config.runtime.replay_loop);
  document.setBoolean("runtime.simulator_realtime_dma", config.runtime.simulator_realtime_dma);

  setUnsigned(document, "digitizer.system_id", config.digitizer.system_id);
  setUnsigned(document, "digitizer.board_id", config.digitizer.board_id);
  document.setString("digitizer.board_profile", config.digitizer.board_profile);
  document.setString("digitizer.channel", toString(config.digitizer.channel));
  document.setNumber("digitizer.sample_rate_hz", config.digitizer.sample_rate_hz);
  setUnsigned(document, "digitizer.sample_point", config.digitizer.sample_point);
  setUnsigned(document, "digitizer.records_per_buffer", config.digitizer.records_per_buffer);
  setUnsigned(document, "digitizer.a_scan_count", config.digitizer.a_scan_count);
  setUnsigned(document, "digitizer.b_scan_count", config.digitizer.b_scan_count);
  document.setNumber("digitizer.input_range_volts", config.digitizer.input_range_volts);
  document.setString("digitizer.coupling", toString(config.digitizer.coupling));
  setUnsigned(document, "digitizer.impedance_ohms", config.digitizer.impedance_ohms);
  document.setString("digitizer.trigger_source", toString(config.digitizer.trigger_source));
  document.setString("digitizer.trigger_slope", toString(config.digitizer.trigger_slope));
  setUnsigned(document, "digitizer.trigger_delay_samples", config.digitizer.trigger_delay_samples);
  setUnsigned(document, "digitizer.pre_trigger_samples", config.digitizer.pre_trigger_samples);
  setUnsigned(document, "digitizer.post_trigger_samples", config.digitizer.post_trigger_samples);
  setUnsigned(document, "digitizer.timeout_ms", config.digitizer.timeout_ms);
  setUnsigned(document, "digitizer.dma_buffer_count", config.digitizer.dma_buffer_count);
  document.setBoolean("digitizer.fifo_only_streaming", config.digitizer.fifo_only_streaming);
  document.setString("digitizer.acquisition_mode", toString(config.digitizer.acquisition_mode));
  setUnsigned(document, "digitizer.finite_frame_count", config.digitizer.finite_frame_count);

  document.setNumber("laser.sweep_bandwidth_hz", config.laser.sweep_bandwidth_hz);
  document.setNumber("laser.sweep_rate_hz", config.laser.sweep_rate_hz);

  document.setString("edfa.mode", toString(config.edfa.mode));
  document.setBoolean("edfa.required_before_start", config.edfa.required_before_start);
  document.setString("edfa.port", config.edfa.port);
  setUnsigned(document, "edfa.baud_rate", config.edfa.baud_rate);
  document.setString("edfa.parity", toString(config.edfa.parity));
  setUnsigned(document, "edfa.stop_bits", config.edfa.stop_bits);
  setUnsigned(document, "edfa.timeout_ms", config.edfa.timeout_ms);
  document.setString("edfa.control_mode", toString(config.edfa.control_mode));
  document.setNumber("edfa.output_setpoint.value", config.edfa.output_setpoint.value);
  document.setString("edfa.output_setpoint.unit", toString(config.edfa.output_setpoint.unit));
  document.setNumber("edfa.output_min_dbm", config.edfa.output_min_dbm);
  document.setNumber("edfa.output_max_dbm", config.edfa.output_max_dbm);
  setUnsigned(document, "edfa.warmup_delay_ms", config.edfa.warmup_delay_ms);
  document.setBoolean("edfa.stop_acquisition_on_disconnect", config.edfa.stop_acquisition_on_disconnect);

  document.setNumber("scan.x_start_deg", config.scan.x_start_deg);
  document.setNumber("scan.x_end_deg", config.scan.x_end_deg);
  document.setNumber("scan.y_start_deg", config.scan.y_start_deg);
  document.setNumber("scan.y_end_deg", config.scan.y_end_deg);
  document.setBoolean("scan.bidirectional", config.scan.bidirectional);
  setUnsigned(document, "scan.x_pixel_count", config.scan.x_pixel_count);
  setUnsigned(document, "scan.y_line_count", config.scan.y_line_count);
  document.setInteger("scan.trigger_shift_samples", config.scan.trigger_shift_samples);
  document.setNumber("scan.scanner_sample_rate_hz", config.scan.scanner_sample_rate_hz);

  document.setString("chirp_segmentation.mode", toString(config.chirp_segmentation.mode));
  document.setInteger("chirp_segmentation.trigger_to_period_offset", config.chirp_segmentation.trigger_to_period_offset);
  setUnsigned(document, "chirp_segmentation.chirp_period_samples", config.digitizer.sample_point);
  setUnsigned(document, "chirp_segmentation.up_segment.start_sample", config.chirp_segmentation.up_segment.start_sample);
  setUnsigned(document, "chirp_segmentation.up_segment.end_sample_exclusive",
              config.chirp_segmentation.up_segment.end_sample_exclusive);
  setUnsigned(document, "chirp_segmentation.down_segment.start_sample", config.chirp_segmentation.down_segment.start_sample);
  setUnsigned(document, "chirp_segmentation.down_segment.end_sample_exclusive",
              config.chirp_segmentation.down_segment.end_sample_exclusive);
  setUnsigned(document, "chirp_segmentation.guard_samples", config.chirp_segmentation.guard_samples);
  setUnsigned(document, "chirp_segmentation.segment_fft_length", config.chirp_segmentation.segment_fft_length);
  document.setString("chirp_segmentation.window", toString(config.chirp_segmentation.window));
  document.setString("chirp_segmentation.polarity", toString(config.chirp_segmentation.polarity));

  document.setString("processing.fft_backend", toString(config.processing.fft_backend));
  document.setBoolean("processing.dc_removal", config.processing.dc_removal);
  document.setNumber("processing.peak_threshold_db", config.processing.peak_threshold_db);
  setUnsigned(document, "processing.peak_search_start_bin", config.processing.peak_search_start_bin);
  setUnsigned(document, "processing.peak_search_end_bin", config.processing.peak_search_end_bin);
  setUnsigned(document, "processing.queue_capacity", config.processing.queue_capacity);
  document.setString("processing.overflow_policy", toString(config.processing.overflow_policy));

  document.setBoolean("udp.enabled", config.udp.enabled);
  document.setString("udp.target_ip", config.udp.target_ip);
  setUnsigned(document, "udp.target_port", config.udp.target_port);
  setUnsigned(document, "udp.packet_point_count", config.udp.packet_point_count);
  setUnsigned(document, "udp.packet_format_version", config.udp.packet_format_version);
  setUnsigned(document, "udp.queue_capacity", config.udp.queue_capacity);
  document.setString("udp.backpressure_policy", toString(config.udp.backpressure_policy));

  document.setBoolean("storage.raw_enabled", config.storage.raw_enabled);
  document.setBoolean("storage.processed_enabled", config.storage.processed_enabled);
  document.setString("storage.output_directory", config.storage.output_directory);
  setUnsigned(document, "storage.queue_capacity", config.storage.queue_capacity);
  document.setString("storage.overflow_policy", toString(config.storage.overflow_policy));
  document.setNumber("storage.split_file_size_gb", config.storage.split_file_size_gb);
  setUnsigned(document, "storage.flush_interval_ms", config.storage.flush_interval_ms);

  document.setNumber("ui.plot_update_hz", config.ui.plot_update_hz);
  document.setNumber("ui.point_cloud_update_hz", config.ui.point_cloud_update_hz);
  document.setBoolean("ui.segment_overlay", config.ui.segment_overlay);
  document.setString("ui.color_map", config.ui.color_map);
  document.setString("ui.last_profile", config.ui.last_profile);

  document.setString("calibration.version", config.calibration.version);
  document.setNumber("calibration.distance_offset_m", config.calibration.distance_offset_m);
  document.setNumber("calibration.distance_scale", config.calibration.distance_scale);
  document.setNumber("calibration.velocity_wavelength_nm", config.calibration.velocity_wavelength_nm);
  document.setNumber("calibration.velocity_offset_mps", config.calibration.velocity_offset_mps);
  document.setNumber("calibration.velocity_scale", config.calibration.velocity_scale);
  document.setNumber("calibration.x_angle_offset_deg", config.calibration.x_angle_offset_deg);
  document.setNumber("calibration.y_angle_offset_deg", config.calibration.y_angle_offset_deg);

  document.setBoolean("mcu.enabled", config.mcu.enabled);
  document.setString("mcu.port", config.mcu.port);
  document.setString("mcu.waveform_source", toString(config.mcu.waveform_source));
  document.setString("mcu.waveform_file", config.mcu.waveform_file);
  setUnsigned(document, "mcu.baud_rate", config.mcu.baud_rate);
  document.setString("mcu.parity", toString(config.mcu.parity));
  setUnsigned(document, "mcu.stop_bits", config.mcu.stop_bits);
  setUnsigned(document, "mcu.timeout_ms", config.mcu.timeout_ms);
  setUnsigned(document, "mcu.retry_count", config.mcu.retry_count);
  document.setBoolean("mcu.require_ack", config.mcu.require_ack);

  return document;
}

bool ConfigProfileCodec::decode(const ConfigDocument& document, SystemConfig& config,
                                std::vector<ConfigProfileIssue>& issues, std::string source) {
  const auto initial_issue_count = issues.size();

  readInteger(document, "profile.schema_version", config.profile.schema_version, issues, source);
  readString(document, "profile.id", config.profile.id, issues, source);
  readString(document, "profile.name", config.profile.name, issues, source);
  readString(document, "profile.description", config.profile.description, issues, source);
  readString(document, "profile.author", config.profile.author, issues, source);
  readString(document, "profile.created_utc", config.profile.created_utc, issues, source);
  readString(document, "profile.modified_utc", config.profile.modified_utc, issues, source);

  readEnum(document, "runtime.acquisition_source", config.runtime.acquisition_source, issues, source);
  readString(document, "runtime.replay_file", config.runtime.replay_file, issues, source);
  readBool(document, "runtime.replay_loop", config.runtime.replay_loop, issues, source);
  readBool(document, "runtime.simulator_realtime_dma", config.runtime.simulator_realtime_dma, issues, source);

  readInteger(document, "digitizer.system_id", config.digitizer.system_id, issues, source);
  readInteger(document, "digitizer.board_id", config.digitizer.board_id, issues, source);
  readString(document, "digitizer.board_profile", config.digitizer.board_profile, issues, source);
  readEnum(document, "digitizer.channel", config.digitizer.channel, issues, source);
  readNumber(document, "digitizer.sample_rate_hz", config.digitizer.sample_rate_hz, issues, source);
  readInteger(document, "digitizer.sample_point", config.digitizer.sample_point, issues, source);
  readInteger(document, "digitizer.records_per_buffer", config.digitizer.records_per_buffer, issues, source);
  readInteger(document, "digitizer.a_scan_count", config.digitizer.a_scan_count, issues, source);
  readInteger(document, "digitizer.b_scan_count", config.digitizer.b_scan_count, issues, source);
  readNumber(document, "digitizer.input_range_volts", config.digitizer.input_range_volts, issues, source);
  readEnum(document, "digitizer.coupling", config.digitizer.coupling, issues, source);
  readInteger(document, "digitizer.impedance_ohms", config.digitizer.impedance_ohms, issues, source);
  readEnum(document, "digitizer.trigger_source", config.digitizer.trigger_source, issues, source);
  readEnum(document, "digitizer.trigger_slope", config.digitizer.trigger_slope, issues, source);
  readInteger(document, "digitizer.trigger_delay_samples", config.digitizer.trigger_delay_samples, issues, source);
  readInteger(document, "digitizer.pre_trigger_samples", config.digitizer.pre_trigger_samples, issues, source);
  readInteger(document, "digitizer.post_trigger_samples", config.digitizer.post_trigger_samples, issues, source);
  readInteger(document, "digitizer.timeout_ms", config.digitizer.timeout_ms, issues, source);
  readInteger(document, "digitizer.dma_buffer_count", config.digitizer.dma_buffer_count, issues, source);
  readBool(document, "digitizer.fifo_only_streaming", config.digitizer.fifo_only_streaming, issues, source);
  readEnum(document, "digitizer.acquisition_mode", config.digitizer.acquisition_mode, issues, source);
  readInteger(document, "digitizer.finite_frame_count", config.digitizer.finite_frame_count, issues, source);

  readNumber(document, "laser.sweep_bandwidth_hz", config.laser.sweep_bandwidth_hz, issues, source);
  readNumber(document, "laser.sweep_rate_hz", config.laser.sweep_rate_hz, issues, source);

  readEnum(document, "edfa.mode", config.edfa.mode, issues, source);
  readBool(document, "edfa.required_before_start", config.edfa.required_before_start, issues, source);
  readString(document, "edfa.port", config.edfa.port, issues, source);
  readInteger(document, "edfa.baud_rate", config.edfa.baud_rate, issues, source);
  readEnum(document, "edfa.parity", config.edfa.parity, issues, source);
  readInteger(document, "edfa.stop_bits", config.edfa.stop_bits, issues, source);
  readInteger(document, "edfa.timeout_ms", config.edfa.timeout_ms, issues, source);
  readEnum(document, "edfa.control_mode", config.edfa.control_mode, issues, source);
  readNumber(document, "edfa.output_setpoint.value", config.edfa.output_setpoint.value, issues, source);
  readEnum(document, "edfa.output_setpoint.unit", config.edfa.output_setpoint.unit, issues, source);
  readNumber(document, "edfa.output_min_dbm", config.edfa.output_min_dbm, issues, source);
  readNumber(document, "edfa.output_max_dbm", config.edfa.output_max_dbm, issues, source);
  readInteger(document, "edfa.warmup_delay_ms", config.edfa.warmup_delay_ms, issues, source);
  readBool(document, "edfa.stop_acquisition_on_disconnect", config.edfa.stop_acquisition_on_disconnect, issues, source);

  readNumber(document, "scan.x_start_deg", config.scan.x_start_deg, issues, source);
  readNumber(document, "scan.x_end_deg", config.scan.x_end_deg, issues, source);
  readNumber(document, "scan.y_start_deg", config.scan.y_start_deg, issues, source);
  readNumber(document, "scan.y_end_deg", config.scan.y_end_deg, issues, source);
  readBool(document, "scan.bidirectional", config.scan.bidirectional, issues, source);
  readInteger(document, "scan.x_pixel_count", config.scan.x_pixel_count, issues, source);
  readInteger(document, "scan.y_line_count", config.scan.y_line_count, issues, source);
  readInteger(document, "scan.trigger_shift_samples", config.scan.trigger_shift_samples, issues, source);
  readNumber(document, "scan.scanner_sample_rate_hz", config.scan.scanner_sample_rate_hz, issues, source);

  readEnum(document, "chirp_segmentation.mode", config.chirp_segmentation.mode, issues, source);
  readInteger(document, "chirp_segmentation.trigger_to_period_offset",
              config.chirp_segmentation.trigger_to_period_offset, issues, source);
  std::uint32_t legacy_chirp_period_samples = config.digitizer.sample_point;
  readInteger(document, "chirp_segmentation.chirp_period_samples",
              legacy_chirp_period_samples, issues, source);
  readInteger(document, "chirp_segmentation.up_segment.start_sample",
              config.chirp_segmentation.up_segment.start_sample, issues, source);
  readInteger(document, "chirp_segmentation.up_segment.end_sample_exclusive",
              config.chirp_segmentation.up_segment.end_sample_exclusive, issues, source);
  readInteger(document, "chirp_segmentation.down_segment.start_sample",
              config.chirp_segmentation.down_segment.start_sample, issues, source);
  readInteger(document, "chirp_segmentation.down_segment.end_sample_exclusive",
              config.chirp_segmentation.down_segment.end_sample_exclusive, issues, source);
  readInteger(document, "chirp_segmentation.guard_samples", config.chirp_segmentation.guard_samples, issues, source);
  readInteger(document, "chirp_segmentation.segment_fft_length",
              config.chirp_segmentation.segment_fft_length, issues, source);
  readEnum(document, "chirp_segmentation.window", config.chirp_segmentation.window, issues, source);
  readEnum(document, "chirp_segmentation.polarity", config.chirp_segmentation.polarity, issues, source);

  readEnum(document, "processing.fft_backend", config.processing.fft_backend, issues, source);
  readBool(document, "processing.dc_removal", config.processing.dc_removal, issues, source);
  readNumber(document, "processing.peak_threshold_db", config.processing.peak_threshold_db, issues, source);
  readInteger(document, "processing.peak_search_start_bin", config.processing.peak_search_start_bin, issues, source);
  readInteger(document, "processing.peak_search_end_bin", config.processing.peak_search_end_bin, issues, source);
  readInteger(document, "processing.queue_capacity", config.processing.queue_capacity, issues, source);
  readEnum(document, "processing.overflow_policy", config.processing.overflow_policy, issues, source);

  readBool(document, "udp.enabled", config.udp.enabled, issues, source);
  readString(document, "udp.target_ip", config.udp.target_ip, issues, source);
  readInteger(document, "udp.target_port", config.udp.target_port, issues, source);
  readInteger(document, "udp.packet_point_count", config.udp.packet_point_count, issues, source);
  readInteger(document, "udp.packet_format_version", config.udp.packet_format_version, issues, source);
  readInteger(document, "udp.queue_capacity", config.udp.queue_capacity, issues, source);
  readEnum(document, "udp.backpressure_policy", config.udp.backpressure_policy, issues, source);

  readBool(document, "storage.raw_enabled", config.storage.raw_enabled, issues, source);
  readBool(document, "storage.processed_enabled", config.storage.processed_enabled, issues, source);
  readString(document, "storage.output_directory", config.storage.output_directory, issues, source);
  readInteger(document, "storage.queue_capacity", config.storage.queue_capacity, issues, source);
  readEnum(document, "storage.overflow_policy", config.storage.overflow_policy, issues, source);
  readNumber(document, "storage.split_file_size_gb", config.storage.split_file_size_gb, issues, source);
  if (document.contains("storage.flush_interval_ms")) {
    readInteger(document, "storage.flush_interval_ms", config.storage.flush_interval_ms, issues, source);
  } else if (document.contains("storage.flush_interval_frames")) {
    std::uint32_t legacy_flush_interval = 0U;
    readInteger(document, "storage.flush_interval_frames", legacy_flush_interval, issues, source);
    config.storage.flush_interval_ms = 250U;
  } else {
    addIssue(issues, source, "storage.flush_interval_ms",
             "Required configuration key is missing");
  }

  readNumber(document, "ui.plot_update_hz", config.ui.plot_update_hz, issues, source);
  readNumber(document, "ui.point_cloud_update_hz", config.ui.point_cloud_update_hz, issues, source);
  readBool(document, "ui.segment_overlay", config.ui.segment_overlay, issues, source);
  readString(document, "ui.color_map", config.ui.color_map, issues, source);
  readString(document, "ui.last_profile", config.ui.last_profile, issues, source);

  readString(document, "calibration.version", config.calibration.version, issues, source);
  readNumber(document, "calibration.distance_offset_m", config.calibration.distance_offset_m, issues, source);
  readNumber(document, "calibration.distance_scale", config.calibration.distance_scale, issues, source);
  readNumber(document, "calibration.velocity_wavelength_nm", config.calibration.velocity_wavelength_nm, issues, source);
  readNumber(document, "calibration.velocity_offset_mps", config.calibration.velocity_offset_mps, issues, source);
  readNumber(document, "calibration.velocity_scale", config.calibration.velocity_scale, issues, source);
  readNumber(document, "calibration.x_angle_offset_deg", config.calibration.x_angle_offset_deg, issues, source);
  readNumber(document, "calibration.y_angle_offset_deg", config.calibration.y_angle_offset_deg, issues, source);

  readBool(document, "mcu.enabled", config.mcu.enabled, issues, source);
  readString(document, "mcu.port", config.mcu.port, issues, source);
  readEnum(document, "mcu.waveform_source", config.mcu.waveform_source, issues, source);
  readString(document, "mcu.waveform_file", config.mcu.waveform_file, issues, source);
  readInteger(document, "mcu.baud_rate", config.mcu.baud_rate, issues, source);
  readEnum(document, "mcu.parity", config.mcu.parity, issues, source);
  readInteger(document, "mcu.stop_bits", config.mcu.stop_bits, issues, source);
  readInteger(document, "mcu.timeout_ms", config.mcu.timeout_ms, issues, source);
  readInteger(document, "mcu.retry_count", config.mcu.retry_count, issues, source);
  readBool(document, "mcu.require_ack", config.mcu.require_ack, issues, source);

  return issues.size() == initial_issue_count;
}

ConfigLoadResult ConfigProfileCodec::decodeYaml(std::string_view yaml, std::string source) {
  ConfigLoadResult result;
  result.merged_document = encode(SystemConfig{});

  ConfigDocument overrides;
  ConfigParseError parse_error;
  if (!ConfigDocument::parseYaml(yaml, overrides, parse_error)) {
    addIssue(result.issues, source, {}, parse_error.message, parse_error.line);
    return result;
  }

  for (const auto& entry : overrides.values()) {
    if (!result.merged_document.contains(entry.first)) {
      addIssue(result.issues, source, entry.first, "Unknown configuration key");
    }
  }
  if (!result.issues.empty()) {
    return result;
  }

  result.merged_document.merge(overrides);
  decode(result.merged_document, result.config, result.issues, source);
  return result;
}

ConfigLoadResult ConfigProfileCodec::loadLayered(const std::vector<std::filesystem::path>& paths) {
  ConfigLoadResult result;
  result.merged_document = encode(SystemConfig{});

  for (const auto& path : paths) {
    std::string text;
    std::string file_error;
    if (!readFile(path, text, file_error)) {
      addIssue(result.issues, path.string(), {}, std::move(file_error));
      continue;
    }

    ConfigDocument layer;
    ConfigParseError parse_error;
    if (!ConfigDocument::parseYaml(text, layer, parse_error)) {
      addIssue(result.issues, path.string(), {}, parse_error.message, parse_error.line);
      continue;
    }
    for (const auto& entry : layer.values()) {
      if (!result.merged_document.contains(entry.first)) {
        addIssue(result.issues, path.string(), entry.first, "Unknown configuration key");
      }
    }
    if (result.issues.empty()) {
      result.merged_document.merge(layer);
    }
  }

  if (result.issues.empty()) {
    decode(result.merged_document, result.config, result.issues, "<merged profile>");
  }
  return result;
}

bool ConfigProfileCodec::save(const std::filesystem::path& path, const SystemConfig& config, std::string& error) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  if (!stream) {
    error = "Unable to open profile for writing: " + path.string();
    return false;
  }
  stream << toYaml(config);
  if (!stream) {
    error = "Unable to write profile: " + path.string();
    return false;
  }
  return true;
}

std::string ConfigProfileCodec::toYaml(const SystemConfig& config) { return encode(config).toYaml(); }

std::string ConfigProfileCodec::toJsonSnapshot(const SystemConfig& config) {
  return encode(config).toFlatJson();
}

}  // namespace fmcw
