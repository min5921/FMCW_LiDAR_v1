#include "core/config_profile.h"
#include "drivers/simulator/fake_digitizer.h"
#include "storage/async_storage_service.h"
#include "storage/binary_storage.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>

#ifndef FMCW_STORAGE_BLOCK_COUNT
#define FMCW_STORAGE_BLOCK_COUNT 32
#endif

#ifndef FMCW_STORAGE_REALTIME_PACING
#define FMCW_STORAGE_REALTIME_PACING 0
#endif

#ifndef FMCW_STORAGE_ENFORCE_ACCEPTANCE
#define FMCW_STORAGE_ENFORCE_ACCEPTANCE 0
#endif

namespace {

constexpr double kDmaPeriodSeconds = 998.0 / 200000.0;
constexpr double kTargetBytesPerSecond = 4992.0 * 998.0 * sizeof(std::int16_t) / kDmaPeriodSeconds;
constexpr double kBenchmarkBytesPerSecond = kTargetBytesPerSecond * 1.3;

std::filesystem::path outputDirectory() {
  const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
  return std::filesystem::temp_directory_path() /
      ("fmcw_phase7_storage_performance_" + std::to_string(suffix));
}

}  // namespace

int main() {
  auto config = fmcw::makeAts9371QualificationSimulatorConfig();
  fmcw::FakeDigitizer digitizer;
  std::string error;
  if (!digitizer.configure(config, error) || !digitizer.connect(error) || !digitizer.start(error)) {
    std::cerr << "Storage qualification simulator setup failed: " << error << '\n';
    return 1;
  }
  fmcw::MutableRawFrameBatchPtr mutable_batch;
  if (digitizer.waitForBatch(mutable_batch, std::chrono::milliseconds(100), error) !=
          fmcw::FrameWaitResult::FrameReady || !mutable_batch ||
      !mutable_batch->hasContiguousSamples()) {
    std::cerr << "Storage qualification DMA block failed: " << error << '\n';
    return 1;
  }
  digitizer.stop(error);
  fmcw::RawFrameBatchPtr batch = mutable_batch;

  const auto directory = outputDirectory();
  const auto payload_bytes = static_cast<std::uint64_t>(batch->contiguous_samples.size()) *
      sizeof(std::int16_t);
  const auto expected_payload_bytes = payload_bytes * FMCW_STORAGE_BLOCK_COUNT;
  fmcw::WriterOpenOptions options;
  options.session_directory = directory;
  options.file_stem = "qualification";
  options.session.session_id = "phase7-storage-performance";
  options.session.profile_id = config.profile.id;
  options.session.platform = "Windows";
  options.session.application_version = "test";
  options.session.start_timestamp_utc_ns = 1U;
  options.session.config_snapshot_json = fmcw::ConfigProfileCodec::toJsonSnapshot(config);
  options.session.config_snapshot_yaml = fmcw::ConfigProfileCodec::toYaml(config);
  options.raw_stream.format_version = fmcw::kRawFrameBatchFormatVersion;
  options.raw_stream.channel = config.digitizer.channel;
  options.raw_stream.sample_rate_hz = config.digitizer.sample_rate_hz;
  options.raw_stream.record_length = config.digitizer.sample_point;
  options.raw_stream.records_per_buffer = config.digitizer.records_per_buffer;
  options.raw_enabled = true;
  options.processed_enabled = false;
  options.queue_capacity = 64U;
  options.flush_interval_ms = 60000U;
  options.split_file_size_gb = FMCW_STORAGE_REALTIME_PACING ? 4.0 : 1.0;
  options.preallocate_raw_parts = true;
  options.minimum_free_space_bytes = expected_payload_bytes + 64U * 1024U * 1024U;

  fmcw::AsyncStorageService storage;
  if (!storage.start(options, error)) {
    std::cerr << "Storage performance preflight failed: " << error
              << " required_payload_bytes=" << expected_payload_bytes << '\n';
    return 1;
  }

  const auto started = std::chrono::steady_clock::now();
  auto next_due = started;
  std::uint64_t accepted_blocks = 0U;
  bool enqueue_ok = true;
  for (std::uint64_t block_index = 0U; block_index < FMCW_STORAGE_BLOCK_COUNT; ++block_index) {
#if FMCW_STORAGE_REALTIME_PACING
    next_due += std::chrono::duration_cast<std::chrono::steady_clock::duration>(
        std::chrono::duration<double>(kDmaPeriodSeconds));
    std::this_thread::sleep_until(next_due);
#endif
    if (storage.enqueueRawBatch(batch, error) != fmcw::EnqueueResult::Accepted) {
      enqueue_ok = false;
      break;
    }
    ++accepted_blocks;
  }
  storage.requestStop(enqueue_ok ? "Storage performance run complete" : error);
  const bool stopped = storage.waitUntilStopped(error);
  const auto elapsed_seconds = std::chrono::duration<double>(
      std::chrono::steady_clock::now() - started).count();
  const auto status = storage.status();
  const auto bytes_per_second = elapsed_seconds > 0.0
      ? static_cast<double>(status.raw_writer.bytes_written) / elapsed_seconds
      : 0.0;
  const auto target_ratio = bytes_per_second / kTargetBytesPerSecond;
  const bool exact = stopped && enqueue_ok && accepted_blocks == FMCW_STORAGE_BLOCK_COUNT &&
      status.raw_writer.blocks_written == FMCW_STORAGE_BLOCK_COUNT &&
      status.raw_writer.frames_written ==
          static_cast<std::uint64_t>(FMCW_STORAGE_BLOCK_COUNT) * 998U &&
      status.raw_queue_size == 0U && !status.stop_reason.empty();
  const bool throughput_pass = bytes_per_second >= kBenchmarkBytesPerSecond;
#if FMCW_STORAGE_REALTIME_PACING
  const bool hard_pass = exact && elapsed_seconds >= 599.0 && elapsed_seconds <= 620.0;
#else
  const bool hard_pass = exact && throughput_pass;
#endif

  std::cout << "phase7_4 blocks=" << status.raw_writer.blocks_written
            << " records=" << status.raw_writer.frames_written
            << " payload_gb=" << static_cast<double>(expected_payload_bytes) / 1.0e9
            << " elapsed_s=" << elapsed_seconds
            << " write_gbps=" << bytes_per_second / 1.0e9
            << " target_ratio=" << target_ratio
            << " queue_high_water=" << status.raw_queue_high_water_mark
            << "/" << status.raw_queue_capacity
            << " result=" << (hard_pass ? "HARD_PASS" : "HARD_FAIL")
            << " functional=" << (exact ? "PASS" : "FAIL") << '\n';

  fmcw::RawReplayReader reader;
  fmcw::RawFrameBatch replayed;
  const auto first_part = directory / "qualification.raw.0000.bin";
  const bool replay_ok = reader.open(first_part, error) &&
      reader.readNextBatch(replayed, error) == fmcw::ReplayReadResult::FrameReady &&
      replayed.contiguous_samples == batch->contiguous_samples;
  reader.close();
  std::error_code remove_error;
  std::filesystem::remove_all(directory, remove_error);

  const bool functional = exact && replay_ok && !remove_error;
#if FMCW_STORAGE_ENFORCE_ACCEPTANCE
  return functional && hard_pass ? 0 : 1;
#else
  return functional ? 0 : 1;
#endif
}
