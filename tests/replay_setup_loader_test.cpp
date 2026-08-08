#include "apps/common/replay_setup_loader.h"

#include "core/config_profile.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

std::filesystem::path uniqueTestDirectory(const char* name) {
  static std::uint32_t sequence = 0U;
  return std::filesystem::temp_directory_path() /
      (std::string("fmcw_replay_setup_") + name + "_" + std::to_string(++sequence));
}

void writeText(const std::filesystem::path& path, const std::string& text) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  stream << text;
}

QString qPath(const std::filesystem::path& path) {
#ifdef _WIN32
  return QString::fromStdWString(path.wstring());
#else
  return QString::fromStdString(path.string());
#endif
}

void testCorruptYamlFallsBackToJsonSnapshot() {
  const auto directory = uniqueTestDirectory("json_fallback");
  std::filesystem::create_directories(directory);
  const auto raw_path = directory / "capture.raw.0000.bin";
  const auto setup_path = directory / "capture.setup.yaml";
  const auto sidecar_path = directory / "capture.raw.json";
  writeText(raw_path, "raw-test-placeholder");
  writeText(setup_path, "digitizer:\n  board_profile: \"unterminated\n");

  fmcw::SystemConfig recorded;
  recorded.profile.name = "Replay regression";
  recorded.digitizer.board_profile = "ats9360";
  recorded.digitizer.sample_rate_hz = 500.0e6;
  recorded.digitizer.sample_point = 4992U;
  recorded.digitizer.records_per_buffer = 998U;
  recorded.scan.x_start_deg = -7.5;
  recorded.scan.x_end_deg = 8.25;
  recorded.scan.y_start_deg = -3.0;
  recorded.scan.y_end_deg = 4.0;
  recorded.scan.y_line_count = 13U;
  recorded.mcu.waveform_source = fmcw::McuWaveformSource::GeneratedRaster;
  recorded.processing.peak_threshold_db = -51.25;
  const auto snapshot = fmcw::ConfigProfileCodec::toJsonSnapshot(recorded);
  writeText(sidecar_path,
            "{\n  \"setup_file\": \"capture.setup.yaml\",\n"
            "  \"config_snapshot\": " + snapshot + "\n}\n");

  const auto result = fmcw::loadRecordedSetup(qPath(raw_path));
  expect(result.ok(), "corrupt YAML falls back to the JSON setup snapshot");
  expect(result.used_json_fallback, "loader identifies that JSON fallback supplied the setup");
  expect(!result.warnings.isEmpty(), "YAML fallback records an operator-visible warning");
  expect(std::filesystem::path(result.setup_source.toStdString()).filename() == "capture.raw.json",
         "fallback reports the JSON sidecar as the actual setup source");
  expect(fmcw::ConfigProfileCodec::toJsonSnapshot(result.config) == snapshot,
         "JSON fallback restores every recorded setup field exactly");

  std::error_code remove_error;
  std::filesystem::remove_all(directory, remove_error);
}

void testUnsupportedRecordedBoardIsRejected() {
  const auto directory = uniqueTestDirectory("unsupported_board");
  std::filesystem::create_directories(directory);
  const auto raw_path = directory / "capture.raw.0000.bin";
  const auto setup_path = directory / "capture.setup.yaml";
  writeText(raw_path, "raw-test-placeholder");

  fmcw::SystemConfig recorded;
  recorded.digitizer.board_profile = "unsupported-recorded-board";
  recorded.mcu.waveform_source = fmcw::McuWaveformSource::GeneratedRaster;
  writeText(setup_path, fmcw::ConfigProfileCodec::toYaml(recorded));

  const auto result = fmcw::loadRecordedSetup(qPath(raw_path));
  expect(!result.ok() && result.error.contains("unsupported-recorded-board"),
         "an unsupported recorded board is rejected instead of silently becoming ATS9371");

  std::error_code remove_error;
  std::filesystem::remove_all(directory, remove_error);
}

void testEquivalentReplayPathDoesNotCountAsAChange() {
  const auto directory = uniqueTestDirectory("path_guard");
  std::filesystem::create_directories(directory);
  const auto raw_path = directory / "capture.raw.0000.bin";
  const auto other_path = directory / "other.raw.0000.bin";
  writeText(raw_path, "raw-test-placeholder");
  writeText(other_path, "raw-test-placeholder");

  const auto loaded = qPath(raw_path);
  const auto equivalent = qPath(directory / "." / raw_path.filename());
  expect(!fmcw::replayPathChanged(equivalent, loaded),
         "equivalent replay paths do not retrigger setup restoration");
  expect(fmcw::replayPathChanged(qPath(other_path), loaded),
         "a genuinely different replay file triggers setup restoration");

  std::error_code remove_error;
  std::filesystem::remove_all(directory, remove_error);
}

}  // namespace

int main() {
  testCorruptYamlFallsBackToJsonSnapshot();
  testUnsupportedRecordedBoardIsRejected();
  testEquivalentReplayPathDoesNotCountAsAChange();

  if (failures != 0) {
    std::cerr << failures << " replay setup test(s) failed\n";
    return 1;
  }
  std::cout << "Replay setup restoration tests passed\n";
  return 0;
}
