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
  expect(!validation.hasWarnings(), "code defaults do not produce timing or performance warnings");

  const auto yaml = fmcw::ConfigProfileCodec::toYaml(defaults);
  const auto decoded = fmcw::ConfigProfileCodec::decodeYaml(yaml, "round-trip");
  expect(decoded.ok(), "serialized default YAML decodes");
  expect(decoded.config.digitizer.channel == fmcw::DigitizerChannel::A, "round trip preserves single channel A");
  expect(decoded.config.chirp_segmentation.mode == fmcw::ChirpTriggerMode::UpChirpOnly,
         "round trip preserves up-chirp-only trigger mode");
  expect(decoded.config.profile.schema_version == 4, "round trip uses the full-frame scan schema version");
  expect(decoded.config.digitizer.board_profile == "ats9371",
         "round trip preserves the digitizer capability profile");
  expect(decoded.config.runtime.acquisition_source == fmcw::AcquisitionSource::Simulator &&
             decoded.config.runtime.replay_file.empty() && !decoded.config.runtime.replay_loop,
         "round trip preserves the simulator runtime source defaults");
  expect(fmcw::alazarTriggerLevelCode(defaults.digitizer.trigger_level_percent) == 150,
         "legacy +17.3 percent trigger threshold maps to ATS code 150");
  expect(fmcw::derivedAScanCount(defaults) == defaults.digitizer.records_per_buffer,
         "A-scan count is derived from records per buffer");
  expect(fmcw::derivedFramePointCount(defaults) == 1600U,
         "one frame contains A-scans per DMA buffer times B-scans per frame");
  expect(std::abs(fmcw::derivedMcuFrameTimeMs(defaults) - 16.0) < 1.0e-9,
         "MCU cycle time is derived from the full-frame waveform point count");
  const auto& board = fmcw::digitizerBoardCapabilities().front();
  expect(board.display_name.find("ATS9371") != std::string::npos && board.sample_rates_hz.size() == 20,
         "ATS9371 capability exposes the SDK discrete sample-rate list");
  expect(fmcw::supportsSampleRate(board, 800.0e6), "ATS9371 supports the SDK 800 MS/s setting");
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
  expect(!layered_validation.hasWarnings(), "layered profile is warning-free");

  const auto jetson = fmcw::ConfigProfileCodec::loadLayered({
      source_root / "config" / "default.yaml",
      source_root / "config" / "jetson.yaml",
      source_root / "config" / "calibration" / "default.yaml",
  });
  expect(jetson.ok(), "Jetson platform layer loads");
  expect(jetson.config.ui.plot_update_hz == 20.0, "Jetson UI rate override is applied");
  const auto jetson_validation = fmcw::ConfigValidator::validate(jetson.config);
  expect(!jetson_validation.hasErrors(), "Jetson platform profile passes validation");
  expect(!jetson_validation.hasWarnings(), "Jetson platform profile is warning-free");
}

void testLaserTimingValidation() {
  fmcw::SystemConfig period_mismatch;
  period_mismatch.laser.chirp_period_us = 20.0;
  const auto period_validation = fmcw::ConfigValidator::validate(period_mismatch);
  expect(hasIssue(period_validation, fmcw::ValidationSeverity::Warning, "laser.chirp_period_us"),
         "laser period mismatch identifies the chirp-period field");

  fmcw::SystemConfig slope_mismatch;
  slope_mismatch.laser.sweep_rate_hz_per_s *= 0.5;
  const auto slope_validation = fmcw::ConfigValidator::validate(slope_mismatch);
  expect(hasIssue(slope_validation, fmcw::ValidationSeverity::Warning, "laser.sweep_rate_hz_per_s"),
         "laser bandwidth, period, and slope mismatch identifies the sweep-rate field");

  fmcw::SystemConfig invalid_slope;
  invalid_slope.laser.sweep_rate_hz_per_s = 0.0;
  expect(hasIssue(fmcw::ConfigValidator::validate(invalid_slope),
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
  legacy_schema.profile.schema_version = 3;
  expect(fmcw::ConfigValidator::validate(legacy_schema).hasErrors(),
         "schema version 3 requires migration before Start");

  fmcw::SystemConfig oversized_mcu_frame;
  oversized_mcu_frame.mcu.enabled = true;
  oversized_mcu_frame.mcu.port = "COM4";
  oversized_mcu_frame.scan.y_line_count = 300;
  oversized_mcu_frame.digitizer.b_scan_count = 300;
  expect(fmcw::ConfigValidator::validate(oversized_mcu_frame).hasErrors(),
         "MCU full-frame waveform cannot exceed the firmware point buffer");

  fmcw::SystemConfig invalid;
  invalid.digitizer.sample_rate_hz = 750.0e6;
  const auto validation = fmcw::ConfigValidator::validate(invalid);
  expect(validation.hasErrors(), "unsupported board sampling rate is rejected");

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

  fmcw::SystemConfig oversized_udp_packet;
  oversized_udp_packet.udp.enabled = true;
  oversized_udp_packet.udp.packet_point_count = 3274;
  expect(fmcw::ConfigValidator::validate(oversized_udp_packet).hasErrors(),
         "UDP v1 point count cannot exceed one maximum-size datagram");
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
  testStrictYamlAndLayering();
  testLaserTimingValidation();
  testPresentationAndChangePolicy();
  testSchemaAndBoardCapabilityValidation();
  testActiveAndPendingConfiguration();
  testStartGateSnapshotAndOverflowStop();

  if (failures == 0) {
    std::cout << "All configuration and operation tests passed.\n";
  }
  return failures == 0 ? 0 : 1;
}
