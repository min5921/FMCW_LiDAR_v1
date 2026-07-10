#include "core/config_manager.h"
#include "core/config_policy.h"
#include "core/config_profile.h"
#include "core/config_validation.h"
#include "core/operation_controller.h"

#include <filesystem>
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

void testDefaultsAndRoundTrip() {
  const fmcw::SystemConfig defaults;
  const auto validation = fmcw::ConfigValidator::validate(defaults);
  expect(!validation.hasErrors(), "code defaults pass validation");

  const auto yaml = fmcw::ConfigProfileCodec::toYaml(defaults);
  const auto decoded = fmcw::ConfigProfileCodec::decodeYaml(yaml, "round-trip");
  expect(decoded.ok(), "serialized default YAML decodes");
  expect(decoded.config.digitizer.channel == fmcw::DigitizerChannel::A, "round trip preserves single channel A");
  expect(decoded.config.chirp_segmentation.mode == fmcw::ChirpTriggerMode::UpChirpOnly,
         "round trip preserves up-chirp-only trigger mode");
}

void testStrictYamlAndLayering() {
  const auto unknown = fmcw::ConfigProfileCodec::decodeYaml("digitizer:\n  unsupported: 1\n", "unknown");
  expect(!unknown.ok(), "unknown keys are rejected");

  const auto malformed = fmcw::ConfigProfileCodec::decodeYaml("digitizer:\n   sample_point: 100\n", "indent");
  expect(!malformed.ok(), "non-two-space indentation is rejected");

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
  expect(!fmcw::ConfigValidator::validate(layered.config).hasErrors(), "layered profile passes validation");

  const auto jetson = fmcw::ConfigProfileCodec::loadLayered({
      source_root / "config" / "default.yaml",
      source_root / "config" / "jetson.yaml",
      source_root / "config" / "calibration" / "default.yaml",
  });
  expect(jetson.ok(), "Jetson platform layer loads");
  expect(jetson.config.ui.plot_update_hz == 20.0, "Jetson UI rate override is applied");
  expect(!fmcw::ConfigValidator::validate(jetson.config).hasErrors(), "Jetson platform profile passes validation");
}

void testExposureAndChangePolicy() {
  expect(fmcw::policyFor("storage.raw_enabled").exposure == fmcw::UiExposure::Basic,
         "raw save toggle is exposed in Basic mode");
  expect(fmcw::policyFor("digitizer.sample_rate_hz").exposure == fmcw::UiExposure::Advanced,
         "sampling rate is Advanced-only");
  expect(fmcw::policyFor("digitizer.sample_rate_hz").change_policy == fmcw::ChangePolicy::RestartRequired,
         "sampling rate requires restart");
  expect(fmcw::policyFor("processing.peak_threshold_db").change_policy == fmcw::ChangePolicy::Runtime,
         "peak threshold applies at runtime");
  expect(fmcw::policyFor("chirp_segmentation.up_segment.start_sample").change_policy ==
             fmcw::ChangePolicy::PreviewOnly,
         "segment boundary applies only during preview");
}

void testActiveAndPendingConfiguration() {
  fmcw::ConfigManager manager;
  auto requested = manager.activeConfig();
  requested.processing.peak_threshold_db = -38.0;
  requested.digitizer.sample_rate_hz = 800.0e6;

  const auto update = manager.requestUpdate(requested, fmcw::OperationState::Acquiring);
  expect(update.accepted, "valid running update is accepted");
  expect(update.applied_changes.size() == 1, "runtime field applies immediately");
  expect(update.pending_changes.size() == 1, "digitizer field remains pending");
  expect(manager.activeConfig().processing.peak_threshold_db == -38.0, "active threshold is updated");
  expect(manager.activeConfig().digitizer.sample_rate_hz == 1.0e9, "active sample rate remains unchanged");
  expect(manager.revision() == 2, "immediate update increments config revision");

  std::string error;
  expect(!manager.applyPending(fmcw::OperationState::Acquiring, error), "pending settings cannot apply while running");
  expect(manager.applyPending(fmcw::OperationState::Ready, error), "pending settings apply in Ready state");
  expect(manager.activeConfig().digitizer.sample_rate_hz == 800.0e6, "pending sample rate becomes active");
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
  testExposureAndChangePolicy();
  testActiveAndPendingConfiguration();
  testStartGateSnapshotAndOverflowStop();

  if (failures == 0) {
    std::cout << "All configuration and operation tests passed.\n";
  }
  return failures == 0 ? 0 : 1;
}
