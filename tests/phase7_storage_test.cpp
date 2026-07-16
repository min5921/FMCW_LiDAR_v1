#include "core/acquisition_session.h"
#include "drivers/replay/replay_digitizer.h"
#include "drivers/simulator/fake_digitizer.h"
#include "drivers/simulator/fake_edfa.h"
#include "drivers/simulator/fake_mcu.h"
#include "processing/fft_backends.h"
#include "processing/signal_processor.h"
#include "storage/async_storage_service.h"
#include "storage/binary_storage.h"

#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
  if (!condition) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}

std::filesystem::path uniqueTestDirectory(const char* label) {
  const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
  return std::filesystem::temp_directory_path() /
      (std::string("fmcw_phase7_storage_") + label + "_" + std::to_string(suffix));
}

fmcw::RawFrameBatchPtr qualificationBatch(fmcw::SystemConfig& config) {
  config = fmcw::makeAts9371QualificationSimulatorConfig();
  fmcw::FakeDigitizer digitizer;
  std::string error;
  if (!digitizer.configure(config, error) || !digitizer.connect(error) || !digitizer.start(error)) {
    std::cerr << "Strict simulator setup failed: " << error << '\n';
    return {};
  }
  fmcw::MutableRawFrameBatchPtr batch;
  const auto result = digitizer.waitForBatch(batch, std::chrono::milliseconds(100), error);
  digitizer.stop(error);
  if (result != fmcw::FrameWaitResult::FrameReady || !batch) {
    std::cerr << "Strict simulator batch failed: " << error << '\n';
    return {};
  }
  return batch;
}

fmcw::WriterOpenOptions writerOptions(const fmcw::SystemConfig& config,
                                      const std::filesystem::path& directory) {
  fmcw::WriterOpenOptions options;
  options.session_directory = directory;
  options.file_stem = "qualification";
  options.session.session_id = "phase7-storage-test";
  options.session.profile_id = config.profile.id;
  options.session.platform = "Windows";
  options.session.application_version = "test";
  options.session.start_timestamp_utc_ns = 1U;
  options.session.config_snapshot_json = "{}";
  options.raw_stream.format_version = fmcw::kRawFrameBatchFormatVersion;
  options.raw_stream.channel = config.digitizer.channel;
  options.raw_stream.sample_rate_hz = config.digitizer.sample_rate_hz;
  options.raw_stream.record_length = config.digitizer.sample_point;
  options.raw_stream.records_per_buffer = config.digitizer.records_per_buffer;
  options.raw_enabled = true;
  options.processed_enabled = false;
  options.queue_capacity = 4U;
  options.flush_interval_frames = config.digitizer.records_per_buffer;
  options.split_file_size_gb = 0.015;
  options.preallocate_raw_parts = true;
  options.minimum_free_space_bytes = 1U * 1024U * 1024U;
  return options;
}

bool processingParity(const fmcw::SystemConfig& config, const fmcw::RawFrameBatch& live,
                      const fmcw::RawFrameBatch& replayed) {
  fmcw::SignalProcessor live_processor(std::make_unique<fmcw::FftwBackend>());
  fmcw::SignalProcessor replay_processor(std::make_unique<fmcw::FftwBackend>());
  std::string error;
  if (!live_processor.configure(config, 1U, error) ||
      !replay_processor.configure(config, 1U, error)) {
    std::cerr << "FFTW replay parity setup failed: " << error << '\n';
    return false;
  }
  std::vector<fmcw::ProcessedFrame> live_results;
  std::vector<fmcw::ProcessedFrame> replay_results;
  if (!live_processor.processBatch(live, 997U, live_results, error) ||
      !replay_processor.processBatch(replayed, 997U, replay_results, error) ||
      live_results.size() != replay_results.size()) {
    std::cerr << "FFTW replay parity processing failed: " << error << '\n';
    return false;
  }
  for (std::size_t index = 0; index < live_results.size(); ++index) {
    const auto& lhs = live_results[index];
    const auto& rhs = replay_results[index];
    if (lhs.measurement_valid != rhs.measurement_valid ||
        lhs.up_peak.discrete_bin != rhs.up_peak.discrete_bin ||
        lhs.down_peak.discrete_bin != rhs.down_peak.discrete_bin ||
        std::abs(lhs.distance_m - rhs.distance_m) > 1.0e-6F ||
        std::abs(lhs.velocity_mps - rhs.velocity_mps) > 1.0e-6F ||
        std::abs(lhs.point.x - rhs.point.x) > 1.0e-6F ||
        std::abs(lhs.point.y - rhs.point.y) > 1.0e-6F ||
        std::abs(lhs.point.z - rhs.point.z) > 1.0e-6F) {
      return false;
    }
  }
  return true;
}

void testDmaBlockStorageAndReplay() {
  fmcw::SystemConfig config;
  const auto batch = qualificationBatch(config);
  expect(batch && batch->hasContiguousSamples() && batch->records.size() == 998U,
         "strict simulator owns one contiguous 4992 x 998 payload");
  if (!batch) {
    return;
  }

  const auto directory = uniqueTestDirectory("v2");
  const auto options = writerOptions(config, directory);
  fmcw::AsyncStorageService storage;
  std::string error;
  expect(storage.start(options, error), "raw v2 asynchronous storage starts");
  expect(storage.enqueueRawBatch(batch, error) == fmcw::EnqueueResult::Accepted,
         "first complete DMA block enters the raw queue");
  expect(storage.enqueueRawBatch(batch, error) == fmcw::EnqueueResult::Accepted,
         "second complete DMA block enters the raw queue");
  storage.requestStop("phase7 v2 test complete");
  expect(storage.waitUntilStopped(error), "raw v2 storage drains and finalizes");
  const auto status = storage.status();
  expect(status.raw_writer.blocks_written == 2U && status.raw_writer.frames_written == 1996U &&
             status.raw_writer.bytes_written > batch->contiguous_samples.size() * sizeof(std::int16_t) * 2U,
         "raw writer reports DMA blocks, records, and physical bytes separately");

  const auto first_part = directory / "qualification.raw.0000.bin";
  const auto second_part = directory / "qualification.raw.0001.bin";
  expect(std::filesystem::exists(first_part) && std::filesystem::exists(second_part),
         "v2 split keeps each complete DMA block in one numbered part");
  fmcw::RawReplayReader reader;
  expect(reader.open(first_part, error) &&
             reader.streamDescriptor().format_version == fmcw::kRawFrameBatchFormatVersion,
         "raw replay detects the v2 DMA-block stream");
  fmcw::RawFrameBatch replayed;
  expect(reader.readNextBatch(replayed, error) == fmcw::ReplayReadResult::FrameReady &&
             replayed.hasContiguousSamples() && replayed.records.size() == 998U,
         "v2 replay restores one complete contiguous DMA block");
  expect(replayed.contiguous_samples == batch->contiguous_samples &&
             replayed.records.front().metadata.scan_position.x_index ==
                 batch->records.front().metadata.scan_position.x_index &&
             replayed.records.back().metadata.scan_position.x_index ==
                 batch->records.back().metadata.scan_position.x_index,
         "v2 replay preserves samples and first/last raster positions");
  expect(processingParity(config, *batch, replayed),
         "v2 replay produces the same FFTW peak, distance, velocity, and XYZ results");
  fmcw::RawFrameBatch second_replayed;
  expect(reader.readNextBatch(second_replayed, error) == fmcw::ReplayReadResult::FrameReady &&
             reader.readNextBatch(second_replayed, error) == fmcw::ReplayReadResult::EndOfStream,
         "v2 replay advances across split parts and stops at the exact block boundary");
  reader.close();

  auto replay_config = config;
  replay_config.runtime.acquisition_source = fmcw::AcquisitionSource::Replay;
  replay_config.runtime.replay_file = first_part.string();
  replay_config.runtime.replay_loop = false;
  fmcw::ReplayDigitizer replay_digitizer;
  fmcw::FakeEdfaController replay_edfa;
  fmcw::FakeMcuController replay_mcu;
  fmcw::AcquisitionSession replay_session(replay_digitizer, replay_edfa, replay_mcu);
  fmcw::RawFrameBatchPtr runtime_replay;
  expect(replay_session.configure(replay_config, 7U, error) && replay_session.connect(error) &&
             replay_session.start(error) &&
             replay_session.waitForBatch(runtime_replay, std::chrono::milliseconds(100), error) ==
                 fmcw::FrameWaitResult::FrameReady &&
             runtime_replay && runtime_replay->hasContiguousSamples(),
         "runtime replay keeps the v2 DMA block contiguous through AcquisitionSession");
  expect(runtime_replay &&
             runtime_replay->records.back().metadata.scan_position.x_index ==
                 batch->records.back().metadata.scan_position.x_index &&
             runtime_replay->records.back().metadata.scan_position.x_angle_deg ==
                 batch->records.back().metadata.scan_position.x_angle_deg,
         "AcquisitionSession preserves valid scan positions restored from raw v2 metadata");
  expect(replay_session.stop(error), "runtime v2 replay session stops cleanly");
  replay_session.disconnect();

  auto insufficient = options;
  insufficient.session_directory = uniqueTestDirectory("space");
  insufficient.minimum_free_space_bytes = std::numeric_limits<std::uint64_t>::max();
  fmcw::BinaryRawFrameWriter writer;
  expect(!writer.open(insufficient, error) && error.find("free space") != std::string::npos,
         "raw writer rejects a session that cannot satisfy its free-space requirement");

  std::error_code remove_error;
  std::filesystem::remove_all(directory, remove_error);
  std::filesystem::remove_all(insufficient.session_directory, remove_error);
}

void testNativeAtsSampleFormatRoundTrip() {
  constexpr std::uint32_t record_count = 2U;
  constexpr std::uint32_t record_length = 8U;
  fmcw::SystemConfig config;
  const auto directory = uniqueTestDirectory("native-ats12");
  auto options = writerOptions(config, directory);
  options.file_stem = "native";
  options.raw_stream.sample_format =
      fmcw::SampleFormat::UnsignedOffsetBinary12LeftAligned;
  options.raw_stream.sample_rate_hz = 1.0e9;
  options.raw_stream.record_length = record_length;
  options.raw_stream.records_per_buffer = record_count;
  options.preallocate_raw_parts = false;
  options.split_file_size_gb = 1.0;

  fmcw::RawFrameBatch batch;
  batch.metadata.sequence = 11U;
  batch.metadata.completion_timestamp_ns = 12U;
  batch.metadata.ownership_ready_timestamp_ns = 13U;
  batch.metadata.record_count = record_count;
  batch.metadata.record_length = record_length;
  const std::vector<std::int16_t> native_codes = {
      static_cast<std::int16_t>(0x0000U), static_cast<std::int16_t>(0x1000U),
      static_cast<std::int16_t>(0x7000U), static_cast<std::int16_t>(0x8000U),
      static_cast<std::int16_t>(0x9000U), static_cast<std::int16_t>(0xF000U),
      static_cast<std::int16_t>(0xFFF0U), static_cast<std::int16_t>(0xFFFFU),
      static_cast<std::int16_t>(0x800FU), static_cast<std::int16_t>(0x8010U),
      static_cast<std::int16_t>(0x4000U), static_cast<std::int16_t>(0xC000U),
      static_cast<std::int16_t>(0x0100U), static_cast<std::int16_t>(0xFE00U),
      static_cast<std::int16_t>(0x7FF0U), static_cast<std::int16_t>(0xA000U),
  };
  batch.contiguous_samples = native_codes;
  batch.records.resize(record_count);
  for (std::uint32_t index = 0U; index < record_count; ++index) {
    auto& record = batch.records[index];
    record.metadata.frame_id = index + 1U;
    record.metadata.dma_buffer_sequence = batch.metadata.sequence;
    record.metadata.record_index_in_buffer = index;
    record.metadata.records_in_buffer = record_count;
    record.metadata.channel = fmcw::DigitizerChannel::A;
    record.metadata.sample_format =
        fmcw::SampleFormat::UnsignedOffsetBinary12LeftAligned;
    record.metadata.sample_rate_hz = options.raw_stream.sample_rate_hz;
    record.metadata.record_length = record_length;
    record.metadata.post_trigger_samples = record_length;
    record.metadata.up_segment = {0U, 4U};
    record.metadata.down_segment = {4U, 8U};
    record.samples.setView(batch.contiguous_samples.data() + index * record_length,
                           record_length);
  }

  fmcw::BinaryRawFrameWriter writer;
  std::string error;
  expect(writer.open(options, error) && writer.writeBatch(batch, error) &&
             writer.finalize({14U, "native ATS12 round trip", true}, error),
         "raw v2 writes native ATS12 codes without conversion");

  fmcw::RawReplayReader reader;
  fmcw::RawFrameBatch replayed;
  const auto raw_path = directory / "native.raw.0000.bin";
  expect(reader.open(raw_path, error) &&
             reader.streamDescriptor().sample_format ==
                 fmcw::SampleFormat::UnsignedOffsetBinary12LeftAligned &&
             reader.readNextBatch(replayed, error) == fmcw::ReplayReadResult::FrameReady &&
             replayed.contiguous_samples == native_codes &&
             replayed.records.front().metadata.sample_format ==
                 fmcw::SampleFormat::UnsignedOffsetBinary12LeftAligned,
         "raw v2 replay restores the exact native ATS12 descriptor and payload");
  reader.close();
  std::error_code remove_error;
  std::filesystem::remove_all(directory, remove_error);
}

struct ParallelWriterState {
  std::mutex mutex;
  std::condition_variable condition;
  bool raw_started = false;
  bool raw_release = false;
  bool processed_written = false;
};

class BlockingBatchWriter final : public fmcw::IRawFrameWriter {
 public:
  explicit BlockingBatchWriter(std::shared_ptr<ParallelWriterState> state) : state_(std::move(state)) {}
  bool open(const fmcw::WriterOpenOptions&, std::string& error) override {
    status_.open = true;
    status_.recording = true;
    error.clear();
    return true;
  }
  bool write(const fmcw::RawFrame&, std::string& error) override {
    error.clear();
    return true;
  }
  bool writeBatch(const fmcw::RawFrameBatch& batch, std::string& error) override {
    std::unique_lock<std::mutex> lock(state_->mutex);
    state_->raw_started = true;
    state_->condition.notify_all();
    state_->condition.wait(lock, [&] { return state_->raw_release; });
    ++status_.blocks_written;
    status_.frames_written += batch.records.size();
    error.clear();
    return true;
  }
  bool flush(std::string& error) override { error.clear(); return true; }
  bool finalize(const fmcw::WriterFinalizeOptions&, std::string& error) override {
    status_.open = false;
    status_.recording = false;
    error.clear();
    return true;
  }
  fmcw::WriterStatus status() const override { return status_; }

 private:
  std::shared_ptr<ParallelWriterState> state_;
  fmcw::WriterStatus status_;
};

class ImmediateProcessedWriter final : public fmcw::IProcessedFrameWriter {
 public:
  explicit ImmediateProcessedWriter(std::shared_ptr<ParallelWriterState> state) : state_(std::move(state)) {}
  bool open(const fmcw::WriterOpenOptions&, std::string& error) override {
    status_.open = true;
    status_.recording = true;
    error.clear();
    return true;
  }
  bool write(const fmcw::ProcessedFrame&, std::string& error) override {
    std::lock_guard<std::mutex> lock(state_->mutex);
    state_->processed_written = true;
    ++status_.frames_written;
    state_->condition.notify_all();
    error.clear();
    return true;
  }
  bool flush(std::string& error) override { error.clear(); return true; }
  bool finalize(const fmcw::WriterFinalizeOptions&, std::string& error) override {
    status_.open = false;
    status_.recording = false;
    error.clear();
    return true;
  }
  fmcw::WriterStatus status() const override { return status_; }

 private:
  std::shared_ptr<ParallelWriterState> state_;
  fmcw::WriterStatus status_;
};

void testIndependentRawAndProcessedWorkers() {
  fmcw::SystemConfig config;
  const auto batch = qualificationBatch(config);
  if (!batch) {
    expect(false, "strict batch exists for independent writer test");
    return;
  }
  auto state = std::make_shared<ParallelWriterState>();
  fmcw::AsyncStorageService storage(std::make_unique<BlockingBatchWriter>(state),
                                    std::make_unique<ImmediateProcessedWriter>(state));
  auto options = writerOptions(config, uniqueTestDirectory("parallel"));
  options.processed_enabled = true;
  options.queue_capacity = 1U;
  std::string error;
  expect(storage.start(options, error), "independent raw and processed writer workers start");
  expect(storage.enqueueRawBatch(batch, error) == fmcw::EnqueueResult::Accepted,
         "blocking raw DMA block reaches its worker");
  {
    std::unique_lock<std::mutex> lock(state->mutex);
    expect(state->condition.wait_for(lock, std::chrono::seconds(2), [&] { return state->raw_started; }),
           "raw writer is blocked independently");
  }
  auto processed = std::make_shared<fmcw::ProcessedFrame>();
  processed->frame_id = 1U;
  expect(storage.enqueueProcessed(processed, error) == fmcw::EnqueueResult::Accepted,
         "processed result enters its separate queue");
  {
    std::unique_lock<std::mutex> lock(state->mutex);
    expect(state->condition.wait_for(lock, std::chrono::seconds(2), [&] { return state->processed_written; }),
           "processed writer advances while raw disk writer is blocked");
  }
  expect(storage.enqueueRawBatch(batch, error) == fmcw::EnqueueResult::Accepted,
         "second raw block occupies only the raw queue");
  expect(storage.enqueueRawBatch(batch, error) == fmcw::EnqueueResult::Overflow &&
             error.find("Raw storage queue") != std::string::npos,
         "raw queue overflow requests STOP without mislabeling the processed queue");
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    state->raw_release = true;
    state->condition.notify_all();
  }
  expect(storage.waitUntilStopped(error), "independent writer workers drain after STOP");
  std::error_code remove_error;
  std::filesystem::remove_all(options.session_directory, remove_error);
}

}  // namespace

int main() {
  testDmaBlockStorageAndReplay();
  testNativeAtsSampleFormatRoundTrip();
  testIndependentRawAndProcessedWorkers();
  if (failures == 0) {
    std::cout << "All Phase 7.4 DMA-block storage tests passed.\n";
  }
  return failures == 0 ? 0 : 1;
}
