#include "storage/async_storage_service.h"
#include "storage/binary_storage.h"
#include "core/config_profile.h"

#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>

namespace {
int failures = 0;
void expect(bool value, const std::string& message) {
  if (!value) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}

struct WriterState {
  std::mutex mutex;
  std::condition_variable cv;
  bool block = false;
  bool entered = false;
  bool release = false;
  bool fail_write = false;
  bool fail_finalize = false;
  bool throw_write = false;
  bool throw_finalize = false;
  unsigned finalizations = 0;
  fmcw::WriterFinalizeOptions final;
  fmcw::WriterStatus status;
};

class FaultWriter final : public fmcw::IRawFrameWriter, public fmcw::IPointCloudFrameWriter {
 public:
  explicit FaultWriter(std::shared_ptr<WriterState> state) : state_(std::move(state)) {}
  bool open(const fmcw::WriterOpenOptions&, std::string& error) override {
    std::lock_guard<std::mutex> lock(state_->mutex);
    state_->status.open = state_->status.recording = true;
    error.clear(); return true;
  }
  bool write(const fmcw::RawFrame&, std::string& error) override { return writeOne(error); }
  bool write(const fmcw::PointCloudSnapshot&, std::string& error) override { return writeOne(error); }
  bool flush(std::string& error) override { error.clear(); return true; }
  bool finalize(const fmcw::WriterFinalizeOptions& options, std::string& error) override {
    std::lock_guard<std::mutex> lock(state_->mutex);
    state_->final = options;
    ++state_->finalizations;
    state_->status.open = state_->status.recording = false;
    if (state_->throw_finalize) { throw std::runtime_error("injected finalize exception"); }
    error = state_->fail_finalize ? "injected finalize failure" : "";
    return !state_->fail_finalize;
  }
  fmcw::WriterStatus status() const override {
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->status;
  }
 private:
  bool writeOne(std::string& error) {
    std::unique_lock<std::mutex> lock(state_->mutex);
    state_->entered = true;
    state_->cv.notify_all();
    state_->cv.wait(lock, [this] { return !state_->block || state_->release; });
    if (state_->throw_write) { throw std::runtime_error("injected write exception"); }
    error = state_->fail_write ? "injected write failure" : "";
    if (!state_->fail_write) { ++state_->status.frames_written; }
    return !state_->fail_write;
  }
  std::shared_ptr<WriterState> state_;
};

fmcw::WriterOpenOptions options() {
  fmcw::WriterOpenOptions value;
  value.raw_enabled = value.processed_enabled = true;
  value.queue_capacity = 1;
  return value;
}

void testFailure(bool point_cloud, const std::string& kind) {
  auto raw = std::make_shared<WriterState>();
  auto cloud = std::make_shared<WriterState>();
  const auto fault = point_cloud ? cloud : raw;
  fault->block = kind == "overflow";
  fault->fail_write = kind == "write";
  fault->fail_finalize = kind == "finalize";
  fault->throw_write = kind == "write exception";
  fault->throw_finalize = kind == "finalize exception";
  fmcw::AsyncStorageService storage(std::make_unique<FaultWriter>(raw),
                                   std::make_unique<FaultWriter>(cloud));
  std::string error;
  expect(storage.start(options(), error), "fault-injection session starts");
  auto raw_frame = std::make_shared<fmcw::RawFrame>();
  auto cloud_frame = std::make_shared<fmcw::PointCloudSnapshot>();
  cloud_frame->complete = true;
  const auto enqueue = [&] {
    return point_cloud ? storage.enqueuePointCloud(cloud_frame, error)
                       : storage.enqueueRaw(raw_frame, error);
  };
  expect(enqueue() == fmcw::EnqueueResult::Accepted, "first input accepted");
  if (kind == "overflow") {
    {
      std::unique_lock<std::mutex> lock(fault->mutex);
      expect(fault->cv.wait_for(lock, std::chrono::seconds(2), [&] { return fault->entered; }),
             "writer is blocked while queue fills");
    }
    expect(enqueue() == fmcw::EnqueueResult::Accepted, "second input fills queue");
    expect(enqueue() == fmcw::EnqueueResult::Overflow, "third input overflows");
    {
      std::lock_guard<std::mutex> lock(fault->mutex);
      fault->release = true;
      fault->cv.notify_all();
    }
  } else {
    storage.requestStop("operator stop");
  }
  expect(!storage.waitUntilStopped(error) && !error.empty(), kind + " reports failure after join");
  expect(storage.status().failed && !raw->final.completed && !cloud->final.completed,
         kind + " invalidates both stream results");
  expect(!raw->final.stop_reason.empty() && raw->final.stop_reason == cloud->final.stop_reason,
         kind + " has a consistent recorded reason");
  const auto finalized = raw->finalizations + cloud->finalizations;
  expect(!storage.waitUntilStopped(error) && finalized == raw->finalizations + cloud->finalizations,
         "repeated join preserves failure without refinalizing");
}

void testStopOutcome(bool failure) {
  auto raw = std::make_shared<WriterState>();
  auto cloud = std::make_shared<WriterState>();
  fmcw::AsyncStorageService storage(std::make_unique<FaultWriter>(raw),
                                   std::make_unique<FaultWriter>(cloud));
  std::string error;
  expect(storage.start(options(), error), "stop-outcome session starts");
  storage.requestStop(failure ? "digitizer failure" : "operator stop", failure);
  expect(storage.waitUntilStopped(error) == !failure, "stop outcome propagates to caller");
  expect(raw->final.completed == !failure && cloud->final.completed == !failure,
         "external failure and normal stop have different completion results");
}

void testClosedPeerMetadataDowngrade() {
  auto cloud = std::make_shared<WriterState>();
  cloud->fail_finalize = true;
  fmcw::AsyncStorageService storage(std::make_unique<fmcw::BinaryRawFrameWriter>(),
                                   std::make_unique<FaultWriter>(cloud));
  auto value = options();
  value.session_directory = std::filesystem::temp_directory_path() /
      ("fmcw_storage_failure_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  value.file_stem = "failure";
  value.raw_stream.record_length = 4096;
  value.raw_stream.sample_rate_hz = 1.0e9;
  value.preallocate_raw_parts = false;
  value.session.config_snapshot_yaml = fmcw::ConfigProfileCodec::toYaml(fmcw::SystemConfig{});
  std::string error;
  const bool opened = storage.start(value, error);
  expect(opened, "binary peer opens: " + error);
  storage.requestStop("operator stop");
  expect(!storage.waitUntilStopped(error), "later peer finalizer reports failure");
  {
    std::ifstream file(value.session_directory / "failure.raw.json");
    const std::string text{std::istreambuf_iterator<char>(file), {}};
    expect(text.find("\"completed\": false") != std::string::npos &&
               text.find("injected finalize failure") != std::string::npos,
           "previously closed raw sidecar is downgraded when the other finalizer fails");
  }
  std::error_code ec;
  std::filesystem::remove_all(value.session_directory, ec);
  expect(!ec, "temporary recording cleaned up");
}
}  // namespace

int main() {
  for (const bool cloud : {false, true}) {
    for (const auto* failure : {"overflow", "write", "finalize", "write exception", "finalize exception"}) {
      testFailure(cloud, failure);
    }
  }
  testStopOutcome(false);
  testStopOutcome(true);
  testClosedPeerMetadataDowngrade();
  if (!failures) { std::cout << "All storage failure tests passed.\n"; }
  return failures ? 1 : 0;
}
