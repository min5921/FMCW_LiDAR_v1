#include "core/config_manager.h"
#include "core/config_policy.h"
#include "core/config_profile.h"
#include "core/config_validation.h"
#include "core/digitizer_capabilities.h"
#include "core/operation_controller.h"

#include <algorithm>
#include <filesystem>
#include <cmath>
#include <iostream>
#include <limits>
#include <set>
#include <string>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

bool hasIssue(const fmcw::ValidationResult& result, fmcw::ValidationSeverity severity,
              const std::string& path) {
  return std::any_of(result.issues.begin(), result.issues.end(),
                     [&](const fmcw::ValidationIssue& issue) {
                       return issue.severity == severity && issue.path == path;
                     });
}

void testDefaultsAndRoundTrip() {
  const fmcw::SystemConfig defaults;
  const auto validation = fmcw::ConfigValidator::validate(defaults);
  expect(!validation.hasErrors(), "code defaults pass validation");
  expect(!validation.hasWarnings(),
         "digitizer record length is the single full-period length and needs no duplicate warning");

  const auto yaml = fmcw::ConfigProfileCodec::toYaml(defaults);
  expect(yaml.find("chirp_period_samples: 4096") != std::string::npos,
         "serialized compatibility field mirrors the digitizer record length");
  const auto decoded = fmcw::ConfigProfileCodec::decodeYaml(yaml, "round-trip");
  expect(decoded.ok(), "serialized default YAML decodes");
  expect(decoded.config.digitizer.channel == fmcw::DigitizerChannel::A, "round trip preserves single channel A");
  expect(decoded.config.digitizer.trigger_delay_samples == 0U,
         "external trigger delay defaults to zero samples");
  expect(decoded.config.chirp_segmentation.mode == fmcw::ChirpTriggerMode::UpChirpOnly,
         "round trip preserves up-chirp-only trigger mode");
  expect(decoded.config.profile.schema_version == 5, "round trip uses the fixed-TTL laser-distance schema version");
  expect(decoded.config.digitizer.board_profile == "ats9371",
         "round trip preserves the digitizer capability profile");
  expect(decoded.config.runtime.acquisition_source == fmcw::AcquisitionSource::Simulator &&
             decoded.config.runtime.replay_file.empty() && !decoded.config.runtime.replay_loop,
         "round trip preserves the simulator runtime source defaults");
  expect(decoded.config.mcu.waveform_source == fmcw::McuWaveformSource::LegacyXymFile &&
             decoded.config.mcu.waveform_file == "config/waveforms/mems_xym_100ksps.txt",
         "round trip preserves the packaged legacy X/Y/M waveform selection");
  auto shifted_trigger = defaults;
  shifted_trigger.scan.trigger_shift_samples = -7;
  const auto shifted_trigger_decoded = fmcw::ConfigProfileCodec::decodeYaml(
      fmcw::ConfigProfileCodec::toYaml(shifted_trigger), "trigger-shift-round-trip");
  expect(shifted_trigger_decoded.ok() &&
             shifted_trigger_decoded.config.scan.trigger_shift_samples == -7,
         "round trip preserves a signed MCU B-trigger offset");
  expect(fmcw::kAlazarExternalTriggerLevelCode == 150U,
         "external trigger retains the fixed SDK level argument");
  expect(decoded.config.laser.sweep_bandwidth_hz == 2.0e9 &&
             decoded.config.laser.sweep_rate_hz == 200.0e3,
         "round trip preserves the two distance-conversion laser inputs");
  expect(decoded.config.edfa.output_max_dbm == fmcw::kEdfaMaximumOutputDbm,
         "EDFA output limit supports the hardware 30 dBm maximum");
  expect(fmcw::derivedAScanCount(defaults) == defaults.digitizer.records_per_buffer,
         "A-scan count is derived from records per buffer");
  expect(fmcw::derivedFramePointCount(defaults) == 1600U,
         "one frame contains A-scans per DMA buffer times B-scans per frame");
  expect(std::abs(fmcw::derivedMcuFrameTimeMs(defaults) - 16.0) < 1.0e-9,
         "MCU cycle time is derived from the full-frame waveform point count");
  fmcw::SystemConfig measured_scan = defaults;
  measured_scan.scan.y_line_count = 100U;
  expect(std::abs(fmcw::derivedMeasuredFrameRateHz(measured_scan, 67.97) - 0.6797) < 1.0e-9 &&
             std::abs(fmcw::derivedMeasuredFrameTimeMs(measured_scan, 67.97) -
                      (1000.0 / 0.6797)) < 1.0e-9,
         "raster FPS and frame time derive continuously from measured DMA B-scan rate");
  expect(fmcw::derivedMeasuredFrameRateHz(measured_scan, 0.0) == 0.0 &&
             fmcw::derivedMeasuredFrameTimeMs(measured_scan, 0.0) == 0.0,
         "missing DMA timing produces an unavailable measured frame rate");
  const auto& board = fmcw::digitizerBoardCapabilities().front();
  expect(board.display_name.find("ATS9371") != std::string::npos && board.sample_rates_hz.size() == 20 &&
             board.minimum_record_samples == 256U && board.record_resolution_samples == 128U &&
             board.pretrigger_alignment_samples == 128U &&
             board.maximum_npt_pretrigger_samples == 8176U &&
             board.single_channel_trigger_delay_alignment_samples == 16U,
         "ATS9371 capability exposes the SDK sample-rate and record-size constraints");
  expect(fmcw::supportsSampleRate(board, 800.0e6), "ATS9371 supports the SDK 800 MS/s setting");
  expect(fmcw::supportsRecordLength(board, 4096U) &&
             fmcw::supportsRecordLength(board, 4992U) &&
             !fmcw::supportsRecordLength(board, 128U) &&
             !fmcw::supportsRecordLength(board, 4000U) &&
             !fmcw::supportsRecordLength(board, 5000U),
         "ATS9371 accepts records of at least 256 samples in multiples of 128");
  expect(fmcw::nearestSupportedRecordLength(board, 128U) == 256U &&
             fmcw::nearestSupportedRecordLength(board, 5000U) == 4992U &&
             fmcw::nearestSupportedRecordLength(board, 5056U) == 5120U,
         "unsupported record lengths normalize to the nearest ATS9371 setting");
}

void testSupportedAlazarBoardCatalog() {
  const auto& boards = fmcw::digitizerBoardCapabilities();
  expect(boards.size() == 11U,
         "catalog contains the 11 SDK 25.1.0 models with 12-bit data and AUX trigger-enable");

  std::set<std::string> profile_ids;
  std::set<std::uint32_t> board_kinds;
  bool common_contract_valid = true;
  for (const auto& board : boards) {
    profile_ids.insert(board.profile_id);
    board_kinds.insert(board.sdk_board_kind);
    common_contract_valid = common_contract_valid &&
        board.bits_per_sample == 12U &&
        board.aux_trigger_enable_supported &&
        board.impedances_ohms == std::vector<std::uint32_t>{50U} &&
        !board.sample_rates_hz.empty() &&
        !board.input_ranges_volts.empty();
    expect(fmcw::findDigitizerBoardCapabilitiesBySdkBoardKind(board.sdk_board_kind) ==
               &board,
           board.display_name + " is addressable by its SDK board kind");
  }
  expect(profile_ids.size() == boards.size() && board_kinds.size() == boards.size(),
         "supported Alazar profiles and SDK board kinds are unique");
  expect(common_contract_valid,
         "every supported board satisfies the 12-bit AUX trigger-enable acquisition contract");

  const auto* ats9120 = fmcw::findDigitizerBoardCapabilities("ats9120");
  const auto* ats9350 = fmcw::findDigitizerBoardCapabilities("ats9350");
  const auto* ats9360 = fmcw::findDigitizerBoardCapabilities("ats9360");
  const auto* ats9362 = fmcw::findDigitizerBoardCapabilities("ats9362");
  const auto* ats9373 = fmcw::findDigitizerBoardCapabilities("ats9373");
  expect(ats9120 != nullptr && ats9120->sdk_board_kind == 32U &&
             fmcw::supportsSampleRate(*ats9120, 20.0e6) &&
             !fmcw::supportsSampleRate(*ats9120, 25.0e6) &&
             fmcw::supportsInputRange(*ats9120, 4.0) &&
             ats9120->record_resolution_samples == 32U &&
             ats9120->fifo_only_streaming_supported,
         "ATS9120 exposes its low-speed rate, range, record, and FIFO constraints");
  expect(ats9350 != nullptr &&
             ats9350->external_trigger_range ==
                 fmcw::AlazarExternalTriggerRange::FiveVolts &&
             !ats9350->fifo_only_streaming_supported &&
             fmcw::supportsSampleRate(*ats9350, 125.0e6),
         "ATS9350 uses its SDK 5 V external-trigger and non-FIFO NPT setup");
  expect(ats9360 != nullptr && fmcw::supportsSampleRate(*ats9360, 1.8e9) &&
             ats9360->fifo_only_streaming_supported,
         "ATS9360 exposes its 1.8 GS/s FIFO-only setup");
  expect(ats9362 != nullptr && fmcw::supportsSampleRate(*ats9362, 750.0e6) &&
             !ats9362->fifo_only_streaming_supported,
         "ATS9362 exposes its 750 MS/s non-FIFO setup");
  expect(ats9373 != nullptr && ats9373->sdk_board_kind == 29U &&
             fmcw::supportsSampleRate(*ats9373, 4.0e9) &&
             ats9373->fifo_only_streaming_supported,
         "ATS9373 exposes its 4 GS/s FIFO-only setup");
  expect(fmcw::findDigitizerBoardCapabilitiesBySdkBoardKind(9999U) == nullptr,
         "unknown SDK board kinds are rejected");
}

void testStrictYamlAndLayering() {
  const auto unknown = fmcw::ConfigProfileCodec::decodeYaml("digitizer:\n  unsupported: 1\n", "unknown");
  expect(!unknown.ok(), "unknown keys are rejected");

  const auto malformed = fmcw::ConfigProfileCodec::decodeYaml("digitizer:\n   sample_point: 100\n", "indent");
  expect(!malformed.ok(), "non-two-space indentation is rejected");

  const auto removed_ui_mode = fmcw::ConfigProfileCodec::decodeYaml("ui:\n  mode: basic\n", "legacy-ui-mode");
  expect(!removed_ui_mode.ok(), "removed global Basic/Advanced mode is rejected");

  const auto removed_tracking = fmcw::ConfigProfileCodec::decodeYaml(
      "processing:\n  peak_tracking_max_delta_bins: 12\n", "legacy-peak-tracking");
  expect(!removed_tracking.ok(), "removed peak tracking controls are rejected");

  const auto removed_normalize = fmcw::ConfigProfileCodec::decodeYaml(
      "processing:\n  normalize: true\n", "legacy-normalize");
  expect(!removed_normalize.ok(), "removed dynamic segment normalization is rejected");

  const auto removed_line_time = fmcw::ConfigProfileCodec::decodeYaml(
      "scan:\n  line_time_ms: 0.64\n", "legacy-line-time");
  expect(!removed_line_time.ok(), "removed MCU-derived B-scan line time is rejected");

  const auto removed_trigger_threshold = fmcw::ConfigProfileCodec::decodeYaml(
      "digitizer:\n  trigger_level_percent: 17.3\n", "legacy-trigger-threshold");
  expect(!removed_trigger_threshold.ok(), "removed analog trigger threshold is rejected");

  const auto removed_laser_slope = fmcw::ConfigProfileCodec::decodeYaml(
      "laser:\n  sweep_rate_hz_per_s: 1000000000000000\n", "legacy-laser-slope");
  expect(!removed_laser_slope.ok(), "removed laser slope field is rejected");

  const std::filesystem::path source_root = FMCW_TEST_SOURCE_DIR;
  const auto layered = fmcw::ConfigProfileCodec::loadLayered({
      source_root / "config" / "default.yaml",
      source_root / "config" / "windows.yaml",
      source_root / "config" / "profiles" / "lab_simulator.yaml",
      source_root / "config" / "calibration" / "default.yaml",
  });
  expect(layered.ok(), "default, platform, profile, and calibration layers load");
  expect(layered.config.profile.id == "lab-simulator", "user profile overrides the built-in profile id");
  expect(layered.config.processing.fft_backend == fmcw::FftBackendKind::Fftw,
         "user profile overrides the platform FFT backend");
  const auto layered_validation = fmcw::ConfigValidator::validate(layered.config);
  expect(!layered_validation.hasErrors(), "layered profile passes validation");
  expect(!layered_validation.hasWarnings(),
         "layered profile has no duplicate chirp-period or laser consistency warnings");

  const auto qualification = fmcw::ConfigProfileCodec::loadLayered({
      source_root / "config" / "default.yaml",
      source_root / "config" / "windows.yaml",
      source_root / "config" / "profiles" / "ats9371_200hz_simulator.yaml",
      source_root / "config" / "calibration" / "default.yaml",
  });
  expect(qualification.ok() && qualification.config.runtime.simulator_realtime_dma &&
             qualification.config.digitizer.sample_point == 4992U &&
             qualification.config.digitizer.records_per_buffer == 998U &&
             qualification.config.chirp_segmentation.up_segment.length() == 2048U &&
             qualification.config.chirp_segmentation.down_segment.length() == 2048U,
         "ATS9371 qualification profile loads the strict 4992 x 998 DMA workload");
  expect(!fmcw::ConfigValidator::validate(qualification.config).hasErrors(),
         "ATS9371 qualification profile passes full validation");

  const auto jetson = fmcw::ConfigProfileCodec::loadLayered({
      source_root / "config" / "default.yaml",
      source_root / "config" / "jetson.yaml",
      source_root / "config" / "calibration" / "default.yaml",
  });
  expect(jetson.ok(), "Jetson platform layer loads");
  expect(jetson.config.ui.plot_update_hz == 30.0, "Jetson UI rate override is applied");
  const auto jetson_validation = fmcw::ConfigValidator::validate(jetson.config);
  expect(!jetson_validation.hasErrors(), "Jetson platform profile passes validation");
  expect(!jetson_validation.hasWarnings(),
         "Jetson profile uses the digitizer record as its full-period length");
}

void testLaserDistanceInputsAndRecordLength() {
  fmcw::SystemConfig exact_period_record;
  exact_period_record.digitizer.sample_point = 3840;
  exact_period_record.digitizer.post_trigger_samples = 3840;
  exact_period_record.chirp_segmentation.trigger_to_period_offset = 0;
  exact_period_record.chirp_segmentation.up_segment = {192, 1920};
  exact_period_record.chirp_segmentation.down_segment = {2112, 3840};
  exact_period_record.chirp_segmentation.guard_samples = 64;
  const auto exact_period_validation = fmcw::ConfigValidator::validate(exact_period_record);
  expect(!exact_period_validation.hasWarnings(),
         "an ATS-aligned digitizer record defines the full-period length without a duplicate warning");

  fmcw::SystemConfig over_period_record = exact_period_record;
  over_period_record.digitizer.sample_point = 3968;
  over_period_record.digitizer.post_trigger_samples = 3968;
  const auto over_period_validation = fmcw::ConfigValidator::validate(over_period_record);
  expect(!over_period_validation.hasErrors() && !over_period_validation.hasWarnings() &&
             std::abs(fmcw::derivedRecordPeriodSeconds(over_period_record) - 3.968e-6) < 1.0e-12,
         "changing the ATS record length directly changes the derived full-period duration");

  fmcw::SystemConfig independent_laser_inputs = exact_period_record;
  independent_laser_inputs.laser.sweep_bandwidth_hz = 1.5e9;
  independent_laser_inputs.laser.sweep_rate_hz = 175.0e3;
  const auto independent_validation = fmcw::ConfigValidator::validate(independent_laser_inputs);
  expect(!independent_validation.hasErrors() && !independent_validation.hasWarnings(),
         "positive bandwidth and sweep rate are accepted without timing consistency warnings");

  fmcw::SystemConfig invalid_sweep_rate;
  invalid_sweep_rate.laser.sweep_rate_hz = 0.0;
  expect(hasIssue(fmcw::ConfigValidator::validate(invalid_sweep_rate),
                  fmcw::ValidationSeverity::Error, "laser"),
         "non-positive sweep rate is rejected before distance processing");
}

void testPresentationAndChangePolicy() {
  expect(fmcw::policyFor("storage.raw_enabled").presentation == fmcw::FieldPresentation::Primary,
         "raw save toggle is a primary page control");
  expect(fmcw::policyFor("digitizer.sample_rate_hz").presentation == fmcw::FieldPresentation::Detailed,
         "sampling rate is shown in the Digitizer page details");
  expect(fmcw::policyFor("digitizer.sample_rate_hz").change_policy == fmcw::ChangePolicy::RestartRequired,
         "sampling rate requires restart");
  expect(fmcw::policyFor("runtime.acquisition_source").change_policy ==
             fmcw::ChangePolicy::RestartRequired,
         "runtime acquisition source changes require restart");
  expect(fmcw::policyFor("processing.peak_threshold_db").change_policy == fmcw::ChangePolicy::Runtime,
         "peak threshold applies at runtime");
  expect(fmcw::policyFor("processing.peak_search_end_bin").change_policy == fmcw::ChangePolicy::Runtime,
         "peak search range applies at runtime");
  expect(fmcw::policyFor("chirp_segmentation.up_segment.start_sample").change_policy ==
             fmcw::ChangePolicy::PreviewOnly,
         "segment boundary applies only during preview");
}

void testSchemaAndBoardCapabilityValidation() {
  fmcw::SystemConfig legacy_schema;
  legacy_schema.profile.schema_version = 4;
  expect(fmcw::ConfigValidator::validate(legacy_schema).hasErrors(),
         "schema version 4 requires migration before Start");

  fmcw::SystemConfig oversized_mcu_frame;
  oversized_mcu_frame.mcu.enabled = true;
  oversized_mcu_frame.mcu.port = "COM4";
  oversized_mcu_frame.mcu.waveform_source = fmcw::McuWaveformSource::GeneratedRaster;
  oversized_mcu_frame.scan.y_line_count = 300;
  oversized_mcu_frame.digitizer.b_scan_count = 300;
  expect(fmcw::ConfigValidator::validate(oversized_mcu_frame).hasErrors(),
         "MCU full-frame waveform cannot exceed the firmware point buffer");

  fmcw::SystemConfig excessive_trigger_shift;
  excessive_trigger_shift.mcu.enabled = true;
  excessive_trigger_shift.scan.trigger_shift_samples = 15000;
  expect(hasIssue(fmcw::ConfigValidator::validate(excessive_trigger_shift),
                  fmcw::ValidationSeverity::Error, "scan.trigger_shift_samples"),
         "MCU B-trigger offset cannot exceed the firmware waveform buffer");

  fmcw::SystemConfig missing_mcu_waveform;
  missing_mcu_waveform.mcu.enabled = true;
  missing_mcu_waveform.mcu.port = "COM4";
  missing_mcu_waveform.mcu.waveform_file.clear();
  expect(hasIssue(fmcw::ConfigValidator::validate(missing_mcu_waveform),
                  fmcw::ValidationSeverity::Error, "mcu.waveform_file"),
         "legacy MCU mode requires an X/Y/M waveform path");

  fmcw::SystemConfig invalid;
  invalid.digitizer.sample_rate_hz = 750.0e6;
  const auto validation = fmcw::ConfigValidator::validate(invalid);
  expect(validation.hasErrors(), "unsupported board sampling rate is rejected");

  fmcw::SystemConfig below_minimum_record;
  below_minimum_record.digitizer.sample_point = 128;
  below_minimum_record.digitizer.post_trigger_samples = 128;
  expect(hasIssue(fmcw::ConfigValidator::validate(below_minimum_record),
                  fmcw::ValidationSeverity::Error, "digitizer.sample_point"),
         "ATS9371 records shorter than 256 samples are rejected");

  fmcw::SystemConfig unaligned_record;
  unaligned_record.digitizer.sample_point = 4000;
  unaligned_record.digitizer.post_trigger_samples = 4000;
  expect(hasIssue(fmcw::ConfigValidator::validate(unaligned_record),
                  fmcw::ValidationSeverity::Error, "digitizer.sample_point"),
         "ATS9371 records that are not multiples of 128 are rejected");

  fmcw::SystemConfig insufficient_post_trigger;
  insufficient_post_trigger.digitizer.pre_trigger_samples = 4096;
  insufficient_post_trigger.digitizer.post_trigger_samples = 0;
  expect(hasIssue(fmcw::ConfigValidator::validate(insufficient_post_trigger),
                  fmcw::ValidationSeverity::Error, "digitizer.pre_trigger_samples"),
         "Alazar records retain at least 64 post-trigger samples");

  fmcw::SystemConfig missing_replay_file;
  missing_replay_file.runtime.acquisition_source = fmcw::AcquisitionSource::Replay;
  expect(hasIssue(fmcw::ConfigValidator::validate(missing_replay_file),
                  fmcw::ValidationSeverity::Error, "runtime.replay_file"),
         "replay source requires a raw v1 input file");

  fmcw::SystemConfig wrong_address;
  wrong_address.digitizer.board_id = 2;
  expect(fmcw::ConfigValidator::validate(wrong_address).hasErrors(),
         "System 1 / Board 1 is enforced");

  fmcw::SystemConfig inconsistent_scan;
  inconsistent_scan.scan.x_pixel_count += 1;
  expect(fmcw::ConfigValidator::validate(inconsistent_scan).hasErrors(),
         "A-scan count cannot diverge from records per buffer");

  fmcw::SystemConfig misaligned_trigger;
  misaligned_trigger.digitizer.trigger_delay_samples = 401;
  expect(fmcw::ConfigValidator::validate(misaligned_trigger).hasErrors(),
         "ATS9371 trigger delay alignment is enforced");

  fmcw::SystemConfig nonfinite_scan;
  nonfinite_scan.scan.x_start_deg = std::numeric_limits<double>::quiet_NaN();
  expect(hasIssue(fmcw::ConfigValidator::validate(nonfinite_scan),
                  fmcw::ValidationSeverity::Error, "scan"),
         "non-finite scanner angles are rejected before Cartesian conversion");

  fmcw::SystemConfig nonfinite_calibration;
  nonfinite_calibration.calibration.y_angle_offset_deg =
      std::numeric_limits<double>::infinity();
  expect(hasIssue(fmcw::ConfigValidator::validate(nonfinite_calibration),
                  fmcw::ValidationSeverity::Error, "calibration"),
         "non-finite Cartesian calibration values are rejected");

  fmcw::SystemConfig last_non_nyquist_bin;
  last_non_nyquist_bin.processing.peak_search_end_bin = 1023;
  expect(!fmcw::ConfigValidator::validate(last_non_nyquist_bin).hasErrors(),
         "FFT length 2048 accepts bin 1023 as the inclusive peak-search end");

  fmcw::SystemConfig nyquist_peak_bin = last_non_nyquist_bin;
  nyquist_peak_bin.processing.peak_search_end_bin = 1024;
  expect(hasIssue(fmcw::ConfigValidator::validate(nyquist_peak_bin),
                  fmcw::ValidationSeverity::Error, "processing.peak_search_end_bin"),
         "FFT length 2048 excludes Nyquist bin 1024 from peak search");

  fmcw::SystemConfig oversized_udp_packet;
  oversized_udp_packet.udp.enabled = true;
  oversized_udp_packet.udp.packet_point_count = 3274;
  expect(fmcw::ConfigValidator::validate(oversized_udp_packet).hasErrors(),
         "UDP v2 point count cannot exceed one maximum-size datagram");

  fmcw::SystemConfig controlled_edfa;
  controlled_edfa.edfa.mode = fmcw::EdfaMode::Controlled;
  controlled_edfa.edfa.port = "COM6";
  controlled_edfa.edfa.output_setpoint = {30.0, fmcw::OpticalPowerUnit::Dbm};
  expect(!fmcw::ConfigValidator::validate(controlled_edfa).hasErrors(),
         "controlled APC EDFA accepts the hardware 30 dBm maximum");
  controlled_edfa.edfa.control_mode = fmcw::EdfaControlMode::Agc;
  expect(hasIssue(fmcw::ConfigValidator::validate(controlled_edfa),
                  fmcw::ValidationSeverity::Error, "edfa.control_mode"),
         "UI-controlled EDFA rejects AGC until a gain setpoint control exists");

  fmcw::SystemConfig shared_serial_port;
  shared_serial_port.mcu.enabled = true;
  shared_serial_port.mcu.port = "/dev/ttyTHS0";
  shared_serial_port.edfa.mode = fmcw::EdfaMode::Controlled;
  shared_serial_port.edfa.port = shared_serial_port.mcu.port;
  expect(hasIssue(fmcw::ConfigValidator::validate(shared_serial_port),
                  fmcw::ValidationSeverity::Error, "serial.port_conflict"),
         "MCU and controlled EDFA cannot select the same serial port");
}

void testActiveAndPendingConfiguration() {
  fmcw::ConfigManager manager;
  auto requested = manager.activeConfig();
  requested.processing.peak_threshold_db = -38.0;
  requested.digitizer.dma_buffer_count = 16;

  const auto update = manager.requestUpdate(requested, fmcw::OperationState::Acquiring);
  expect(update.accepted, "valid running update is accepted");
  expect(update.applied_changes.size() == 1, "runtime field applies immediately");
  expect(update.pending_changes.size() == 1, "digitizer field remains pending");
  expect(manager.activeConfig().processing.peak_threshold_db == -38.0, "active threshold is updated");
  expect(manager.activeConfig().digitizer.dma_buffer_count == 8, "active DMA setting remains unchanged");
  expect(manager.revision() == 2, "immediate update increments config revision");

  std::string error;
  expect(!manager.applyPending(fmcw::OperationState::Acquiring, error), "pending settings cannot apply while running");
  expect(manager.applyPending(fmcw::OperationState::Ready, error), "pending settings apply in Ready state");
  expect(manager.activeConfig().digitizer.dma_buffer_count == 16, "pending DMA setting becomes active");
  expect(manager.revision() == 3, "pending application increments config revision");

  fmcw::ConfigManager coupled_manager;
  auto coupled = coupled_manager.activeConfig();
  coupled.chirp_segmentation.segment_fft_length = 4096;
  coupled.processing.peak_search_end_bin = 1500;
  const auto coupled_update = coupled_manager.requestUpdate(coupled, fmcw::OperationState::Acquiring);
  expect(coupled_update.accepted, "valid coupled update is accepted");
  expect(coupled_update.applied_changes.empty(), "invalid intermediate active settings are not applied");
  expect(coupled_update.pending_changes.size() == 2, "coupled runtime and restart changes remain pending together");
  expect(coupled_manager.activeConfig().processing.peak_search_end_bin == 900,
         "active config remains valid while coupled settings are pending");
}

void advanceToReady(fmcw::OperationController& controller) {
  expect(controller.markConnected(), "operation connects");
  expect(controller.markConfigured(), "operation configures");
  expect(controller.markReady(), "operation becomes ready");
}

void testStartGateSnapshotAndOverflowStop() {
  fmcw::OperationController controller;
  advanceToReady(controller);

  fmcw::SystemConfig invalid;
  invalid.digitizer.post_trigger_samples = 1;
  const auto rejected = controller.requestStart(invalid, 1, "{}");
  expect(!rejected.accepted, "invalid configuration blocks Start");
  expect(controller.state() == fmcw::OperationState::Ready, "blocked Start preserves Ready state");

  fmcw::ConfigManager manager;
  const auto started = controller.requestStart(manager.activeConfig(), manager.revision(), manager.activeSnapshotJson());
  expect(started.accepted, "valid configuration starts acquisition");
  expect(started.config_revision == manager.revision(), "Start captures the active config revision");
  expect(!started.config_snapshot_json.empty(), "Start captures a configuration snapshot");

  expect(controller.handleQueueOverflow("raw_writer", 65, 64, 120), "queue overflow requests Stop");
  expect(controller.state() == fmcw::OperationState::Stopping, "overflow enters Stopping");
  expect(controller.lastStop().cause == fmcw::StopCause::QueueOverflow, "overflow cause is retained");
  expect(controller.completeStop(), "overflow stop completes");
  expect(controller.state() == fmcw::OperationState::Error, "overflow completion enters Error for operator acknowledgement");
}

}  // namespace

int main() {
  testDefaultsAndRoundTrip();
  testSupportedAlazarBoardCatalog();
  testStrictYamlAndLayering();
  testLaserDistanceInputsAndRecordLength();
  testPresentationAndChangePolicy();
  testSchemaAndBoardCapabilityValidation();
  testActiveAndPendingConfiguration();
  testStartGateSnapshotAndOverflowStop();

  if (failures == 0) {
    std::cout << "All configuration and operation tests passed.\n";
  }
  return failures == 0 ? 0 : 1;
}
