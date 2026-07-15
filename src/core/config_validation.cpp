#include "core/config_validation.h"

#include "core/digitizer_capabilities.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <sstream>
#include <string>
#include <utility>

namespace fmcw {
namespace {

void add(ValidationResult& result, ValidationSeverity severity, std::string path,
         std::string message, std::string action) {
  result.issues.push_back({severity, std::move(path), std::move(message), std::move(action)});
}

bool isPowerOfTwo(std::uint32_t value) {
  return value != 0 && (value & (value - 1)) == 0;
}

bool isValidIpv4(const std::string& address) {
  std::istringstream stream(address);
  std::string part;
  int count = 0;
  while (std::getline(stream, part, '.')) {
    if (part.empty() || part.size() > 3 ||
        !std::all_of(part.begin(), part.end(), [](unsigned char ch) { return ch >= '0' && ch <= '9'; })) {
      return false;
    }
    if (part.size() > 1 && part.front() == '0') {
      return false;
    }
    try {
      const int value = std::stoi(part);
      if (value < 0 || value > 255) {
        return false;
      }
    } catch (...) {
      return false;
    }
    ++count;
  }
  return count == 4 && !address.empty() && address.back() != '.';
}

bool validSerialStopBits(std::uint32_t stop_bits) {
  return stop_bits == 1 || stop_bits == 2;
}

double setpointDbm(const OpticalPowerSetpoint& setpoint) {
  if (setpoint.unit == OpticalPowerUnit::Dbm) {
    return setpoint.value;
  }
  return setpoint.value > 0.0 ? 10.0 * std::log10(setpoint.value) : -INFINITY;
}

}  // namespace

bool ValidationResult::hasErrors() const {
  return std::any_of(issues.begin(), issues.end(), [](const ValidationIssue& issue) {
    return issue.severity == ValidationSeverity::Error;
  });
}

bool ValidationResult::hasWarnings() const {
  return std::any_of(issues.begin(), issues.end(), [](const ValidationIssue& issue) {
    return issue.severity == ValidationSeverity::Warning;
  });
}

ValidationResult ConfigValidator::validate(const SystemConfig& config) {
  ValidationResult result;
  const auto& digitizer = config.digitizer;
  const auto& chirp = config.chirp_segmentation;

  if (config.profile.schema_version != kConfigSchemaVersion) {
    add(result, ValidationSeverity::Error, "profile.schema_version", "Unsupported configuration schema version",
        "Migrate the profile to schema version " + std::to_string(kConfigSchemaVersion));
  }
  if (config.profile.id.empty() || config.profile.name.empty()) {
    add(result, ValidationSeverity::Error, "profile", "Profile id and name are required",
        "Set both profile.id and profile.name");
  }

  if (config.runtime.acquisition_source == AcquisitionSource::Replay &&
      config.runtime.replay_file.empty()) {
    add(result, ValidationSeverity::Error, "runtime.replay_file",
        "Replay source requires a raw recording file",
        "Select a *.raw.0000.bin file in the Digitizer page");
  }

  if (digitizer.system_id != kAlazarSystemId || digitizer.board_id != kAlazarBoardId) {
    add(result, ValidationSeverity::Error, "digitizer", "This application uses Alazar System 1 / Board 1",
        "Set system_id and board_id to 1");
  }
  if (!(digitizer.sample_rate_hz > 0.0)) {
    add(result, ValidationSeverity::Error, "digitizer.sample_rate_hz", "Sample rate must be positive",
        "Select a board-supported Alazar sampling rate");
  }
  if (digitizer.sample_point == 0U) {
    add(result, ValidationSeverity::Error, "digitizer.sample_point", "Record length must be positive",
        "Enter a board-supported record sample count");
  }
  const auto* board_capabilities = findDigitizerBoardCapabilities(digitizer.board_profile);
  if (board_capabilities == nullptr) {
    add(result, ValidationSeverity::Error, "digitizer.board_profile", "Unknown digitizer board capability profile",
        "Select a board profile supported by this application build");
  } else {
    if (!supportsSampleRate(*board_capabilities, digitizer.sample_rate_hz)) {
      add(result, ValidationSeverity::Error, "digitizer.sample_rate_hz",
          "Sampling rate is not supported by the selected board profile",
          "Choose one of the board-specific sampling rates");
    }
    if (!supportsRecordLength(*board_capabilities, digitizer.sample_point)) {
      add(result, ValidationSeverity::Error, "digitizer.sample_point",
          "Record length is not accepted by the selected Alazar board",
          "Use at least " + std::to_string(board_capabilities->minimum_record_samples) +
              " samples and a multiple of " +
              std::to_string(board_capabilities->record_resolution_samples));
    }
    if (!supportsInputRange(*board_capabilities, digitizer.input_range_volts) ||
        !supportsImpedance(*board_capabilities, digitizer.impedance_ohms)) {
      add(result, ValidationSeverity::Error, "digitizer.input_range_volts",
          "Input range or impedance is not supported by the selected board profile",
          "Choose a board-specific input range and impedance");
    }
  }
  if (digitizer.pre_trigger_samples + digitizer.post_trigger_samples != digitizer.sample_point) {
    add(result, ValidationSeverity::Error, "digitizer.sample_point",
        "pre_trigger_samples + post_trigger_samples must equal sample_point",
        "Adjust the pre/post trigger split to cover exactly one record");
  }
  if (board_capabilities != nullptr) {
    if ((digitizer.pre_trigger_samples % board_capabilities->pretrigger_alignment_samples) != 0U ||
        digitizer.pre_trigger_samples > board_capabilities->maximum_npt_pretrigger_samples ||
        digitizer.post_trigger_samples < kAlazarMinimumPostTriggerSamples) {
      add(result, ValidationSeverity::Error, "digitizer.pre_trigger_samples",
          "Pre/post-trigger split is not accepted by the selected Alazar board",
          "Align pre-trigger to " +
              std::to_string(board_capabilities->pretrigger_alignment_samples) +
              " samples, keep it at or below " +
              std::to_string(board_capabilities->maximum_npt_pretrigger_samples) +
              ", and leave at least " + std::to_string(kAlazarMinimumPostTriggerSamples) +
              " post-trigger samples");
    }
    if ((digitizer.trigger_delay_samples %
         board_capabilities->single_channel_trigger_delay_alignment_samples) != 0U) {
      add(result, ValidationSeverity::Error, "digitizer.trigger_delay_samples",
          "Trigger delay is not aligned for single-channel acquisition",
          "Use a trigger delay divisible by " +
              std::to_string(board_capabilities->single_channel_trigger_delay_alignment_samples));
    }
  }
  if (digitizer.records_per_buffer == 0 || digitizer.dma_buffer_count == 0 || digitizer.timeout_ms == 0) {
    add(result, ValidationSeverity::Error, "digitizer.records_per_buffer", "DMA sizes and timeout must be non-zero",
        "Set records_per_buffer, dma_buffer_count, and timeout_ms above zero");
  }
  if (!(digitizer.input_range_volts > 0.0) ||
      (digitizer.impedance_ohms != 50 && digitizer.impedance_ohms != 1000000)) {
    add(result, ValidationSeverity::Error, "digitizer.input_range_volts",
        "Input range must be positive and impedance must be 50 or 1000000 ohms",
        "Choose a value supported by the installed digitizer");
  }
  if (digitizer.trigger_source != TriggerSource::External) {
    add(result, ValidationSeverity::Error, "digitizer.trigger_source", "UP chirp acquisition requires TRIG IN",
        "Use the external TTL trigger source");
  }
  if (digitizer.coupling != Coupling::Dc) {
    add(result, ValidationSeverity::Error, "digitizer.coupling", "ATS9371 analog inputs use DC coupling",
        "Select DC coupling");
  }
  if (digitizer.trigger_level_percent < -100.0 || digitizer.trigger_level_percent > 100.0) {
    add(result, ValidationSeverity::Error, "digitizer.trigger_level_percent",
        "Trigger threshold must be between -100 and +100 percent of full scale",
        "Adjust the external trigger threshold");
  }
  if (digitizer.acquisition_mode == AcquisitionMode::Finite && digitizer.finite_frame_count == 0) {
    add(result, ValidationSeverity::Error, "digitizer.finite_frame_count", "Finite mode requires at least one frame",
        "Set finite_frame_count above zero");
  }

  if (chirp.mode != ChirpTriggerMode::UpChirpOnly) {
    add(result, ValidationSeverity::Error, "chirp_segmentation.mode", "Only up-chirp trigger mode is supported",
        "Use up_chirp_only and acquire the complete up/down period");
  }
  if (chirp.trigger_to_period_offset < 0 ||
      static_cast<std::uint64_t>(chirp.trigger_to_period_offset) + chirp.chirp_period_samples > digitizer.sample_point) {
    add(result, ValidationSeverity::Error, "chirp_segmentation.chirp_period_samples",
        "The full chirp period falls outside the digitizer record",
        "Adjust trigger_to_period_offset, chirp_period_samples, or sample_point");
  }
  if (!chirp.up_segment.validFor(digitizer.sample_point) || !chirp.down_segment.validFor(digitizer.sample_point)) {
    add(result, ValidationSeverity::Error, "chirp_segmentation", "Up and down segments must be valid half-open record ranges",
        "Keep each segment inside [0, sample_point)");
  } else {
    const auto period_start = static_cast<std::uint32_t>(std::max(chirp.trigger_to_period_offset, 0));
    const auto period_end = period_start + chirp.chirp_period_samples;
    if (chirp.up_segment.start_sample < period_start || chirp.up_segment.end_sample_exclusive > period_end ||
        chirp.down_segment.start_sample < period_start || chirp.down_segment.end_sample_exclusive > period_end) {
      add(result, ValidationSeverity::Error, "chirp_segmentation", "Segments must stay inside the captured chirp period",
          "Move both segment ranges between the period start and end");
    }
    if (chirp.up_segment.end_sample_exclusive > chirp.down_segment.start_sample) {
      add(result, ValidationSeverity::Error, "chirp_segmentation", "Up and down segments overlap",
          "Leave a transition region between the two segments");
    } else if (chirp.down_segment.start_sample - chirp.up_segment.end_sample_exclusive < chirp.guard_samples * 2U) {
      add(result, ValidationSeverity::Error, "chirp_segmentation.guard_samples",
          "The up/down transition is too short for the requested guard on both sides",
          "Reduce guard_samples or widen the transition region");
    }
  }
  const auto longest_segment = std::max(chirp.up_segment.length(), chirp.down_segment.length());
  if (chirp.segment_fft_length < longest_segment) {
    add(result, ValidationSeverity::Error, "chirp_segmentation.segment_fft_length", "FFT length is shorter than a chirp segment",
        "Increase segment_fft_length or shorten the segment");
  } else if (!isPowerOfTwo(chirp.segment_fft_length)) {
    add(result, ValidationSeverity::Warning, "chirp_segmentation.segment_fft_length",
        "A non-power-of-two FFT length can reduce real-time throughput",
        "Use a power-of-two length unless measurement constraints require otherwise");
  }
  if (config.processing.peak_search_start_bin >= config.processing.peak_search_end_bin ||
      config.processing.peak_search_end_bin >= chirp.segment_fft_length / 2U) {
    add(result, ValidationSeverity::Error, "processing.peak_search_end_bin", "Peak search range is outside the usable FFT bins",
        "Set start < end < segment_fft_length / 2");
  }
  if (config.processing.queue_capacity == 0 || config.storage.queue_capacity == 0 || config.udp.queue_capacity == 0) {
    add(result, ValidationSeverity::Error, "processing.queue_capacity", "All real-time queue capacities must be non-zero",
        "Set processing, storage, and UDP queue capacities above zero");
  }

  if (!(config.laser.wavelength_nm > 0.0) || !(config.laser.sweep_bandwidth_hz > 0.0) ||
      !(config.laser.sweep_rate_hz_per_s > 0.0) || !(config.laser.chirp_period_us > 0.0) ||
      !(config.laser.optical_path_factor > 0.0)) {
    add(result, ValidationSeverity::Error, "laser",
        "Laser wavelength, bandwidth, sweep rate, chirp period, and path factor must be positive",
        "Enter the measured laser specifications");
  } else {
    if (digitizer.sample_rate_hz > 0.0) {
      const double expected_samples = digitizer.sample_rate_hz * config.laser.chirp_period_us * 1.0e-6;
      if (static_cast<double>(digitizer.sample_point) > expected_samples) {
        const double record_duration_us =
            static_cast<double>(digitizer.sample_point) * 1.0e6 / digitizer.sample_rate_hz;
        std::ostringstream message;
        message << "Record length captures " << digitizer.sample_point << " samples ("
                << record_duration_us << " us), longer than one laser period ("
                << config.laser.chirp_period_us << " us)";
        add(result, ValidationSeverity::Warning, "digitizer.sample_point", message.str(),
            "The ATS record length is valid; keep the extra capture margin only if intentional");
      }
      const double difference = std::abs(expected_samples - chirp.chirp_period_samples);
      if (difference > std::max(4.0, expected_samples * 0.01)) {
        std::ostringstream message;
        message << "Laser chirp period implies " << std::llround(expected_samples)
                << " samples, but chirp_period_samples is " << chirp.chirp_period_samples;
        add(result, ValidationSeverity::Warning, "chirp_segmentation.chirp_period_samples", message.str(),
            "Verify the full-period trigger timing and update the measured period or sample count");
      }
    }

    const double full_period_seconds = config.laser.chirp_period_us * 1.0e-6;
    const double symmetric_triangular_slope =
        2.0 * config.laser.sweep_bandwidth_hz / full_period_seconds;
    const double slope_difference =
        std::abs(config.laser.sweep_rate_hz_per_s - symmetric_triangular_slope);
    if (slope_difference > symmetric_triangular_slope * 0.01) {
      add(result, ValidationSeverity::Warning, "laser.sweep_rate_hz_per_s",
          "Sweep rate differs by more than 1 percent from bandwidth divided by a half chirp period",
          "Enter the measured sweep slope, or verify bandwidth and full chirp period for the laser waveform");
    }
  }

  if (config.scan.x_start_deg >= config.scan.x_end_deg || config.scan.y_start_deg >= config.scan.y_end_deg ||
      config.scan.x_pixel_count < 2 || config.scan.y_line_count < 2 ||
      !(config.scan.scanner_sample_rate_hz > 0.0)) {
    add(result, ValidationSeverity::Error, "scan", "Scan ranges, dimensions, or MCU point rate are invalid",
        "Set increasing angle ranges, at least two points on each axis, and a positive MCU point rate");
  }
  if (config.scan.x_pixel_count != derivedAScanCount(config) || digitizer.a_scan_count != derivedAScanCount(config) ||
      digitizer.b_scan_count != config.scan.y_line_count) {
    add(result, ValidationSeverity::Error, "digitizer.a_scan_count",
        "A-scan count must equal records_per_buffer and B-scan count must equal the configured line count",
        "Derive A-scans from the DMA record count and keep B-scans aligned with scan.y_line_count");
  }
  if (config.mcu.enabled && derivedFramePointCount(config) > 15000U) {
    add(result, ValidationSeverity::Error, "scan.y_line_count",
        "The full-frame MCU waveform exceeds the firmware 15000-point buffer",
        "Reduce records_per_buffer or B-scans per frame");
  }

  if (config.edfa.mode == EdfaMode::None && config.edfa.required_before_start) {
    add(result, ValidationSeverity::Error, "edfa.required_before_start", "EDFA cannot be required when EDFA mode is none",
        "Disable required_before_start or select manual/controlled mode");
  }
  if (config.edfa.mode == EdfaMode::Controlled) {
    if (config.edfa.port.empty() || config.edfa.baud_rate == 0 || config.edfa.timeout_ms == 0 ||
        !validSerialStopBits(config.edfa.stop_bits)) {
      add(result, ValidationSeverity::Error, "edfa.port", "Controlled EDFA requires a complete serial connection",
          "Set port, baud rate, timeout, and one or two stop bits");
    }
    if (config.edfa.output_min_dbm >= config.edfa.output_max_dbm) {
      add(result, ValidationSeverity::Error, "edfa.output_max_dbm", "EDFA output limits are reversed or equal",
          "Set output_min_dbm below output_max_dbm");
    }
    const double output_dbm = setpointDbm(config.edfa.output_setpoint);
    if (!std::isfinite(output_dbm) || output_dbm < config.edfa.output_min_dbm || output_dbm > config.edfa.output_max_dbm) {
      add(result, ValidationSeverity::Error, "edfa.output_setpoint", "EDFA setpoint is outside the configured safe range",
          "Choose an output setpoint between the minimum and maximum limits");
    }
  }

  if (config.udp.enabled && (!isValidIpv4(config.udp.target_ip) || config.udp.target_port == 0 ||
                             config.udp.packet_point_count == 0 || config.udp.packet_point_count > 3273U ||
                             config.udp.packet_format_version != 1U)) {
    add(result, ValidationSeverity::Error, "udp", "Enabled UDP output requires a valid IPv4 endpoint and packet format",
        "Use packet format v1 and 1..3273 points per datagram");
  }
  if ((config.storage.raw_enabled || config.storage.processed_enabled) && config.storage.output_directory.empty()) {
    add(result, ValidationSeverity::Error, "storage.output_directory", "Enabled storage requires an output directory",
        "Select a writable session data directory");
  }
  if (!(config.storage.split_file_size_gb > 0.0) || config.storage.flush_interval_frames == 0) {
    add(result, ValidationSeverity::Error, "storage.split_file_size_gb", "Storage split size and flush interval must be positive",
        "Set a positive split_file_size_gb and flush_interval_frames");
  }

  if (config.ui.plot_update_hz <= 0.0 || config.ui.plot_update_hz > 60.0 ||
      config.ui.point_cloud_update_hz <= 0.0 || config.ui.point_cloud_update_hz > 30.0) {
    add(result, ValidationSeverity::Error, "ui.plot_update_hz", "UI update rates exceed the supported real-time range",
        "Use plot_update_hz in (0, 60] and point_cloud_update_hz in (0, 30]");
  }
  if (config.calibration.version.empty() || !(config.calibration.distance_scale > 0.0) ||
      !(config.calibration.velocity_scale > 0.0)) {
    add(result, ValidationSeverity::Error, "calibration", "Calibration version and positive scales are required",
        "Load a valid calibration layer");
  }
  if (config.mcu.enabled && (config.mcu.port.empty() || config.mcu.baud_rate == 0 || config.mcu.timeout_ms == 0 ||
                             !validSerialStopBits(config.mcu.stop_bits))) {
    add(result, ValidationSeverity::Error, "mcu.port", "Enabled MCU requires a complete serial connection",
        "Set port, baud rate, timeout, and one or two stop bits");
  }

  if (config.storage.raw_enabled && config.laser.chirp_period_us > 0.0) {
    const double raw_megabytes_per_second =
        static_cast<double>(digitizer.sample_point) * sizeof(std::int16_t) / config.laser.chirp_period_us;
    if (raw_megabytes_per_second > 1000.0) {
      add(result, ValidationSeverity::Warning, "storage.raw_enabled",
          "Estimated raw data rate exceeds 1000 MB/s",
          "Benchmark the target NVMe path before acquisition");
    }
  }

  return result;
}

std::string toString(ValidationSeverity severity) {
  switch (severity) {
    case ValidationSeverity::Info: return "info";
    case ValidationSeverity::Warning: return "warning";
    case ValidationSeverity::Error: return "error";
  }
  return "unknown";
}

}  // namespace fmcw
