#include "storage/async_point_cloud_replay.h"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>

namespace fmcw {
struct AsyncPointCloudReplay::Impl {
  struct Result {
    PointCloudReadResult kind = PointCloudReadResult::Error;
    PointCloudSnapshot frame;
    std::string error;
  };
  mutable std::mutex mutex;
  std::condition_variable condition;
  std::atomic_uint64_t generation{0};
  std::thread worker;
  std::filesystem::path path;
  PointCloudReplayInfo info;
  std::optional<Result> result;
  bool open = false;
  bool reopen = false;
  bool need_read = false;
  bool stopping = false;

  void run() {
    PointCloudReplayReader reader;
    while (true) {
      std::filesystem::path next_path;
      bool reset;
      std::uint64_t epoch;
      {
        std::unique_lock<std::mutex> lock(mutex);
        condition.wait(lock, [&] { return stopping || reopen || (need_read && !result); });
        if (stopping) { return; }
        epoch = generation.load();
        reset = reopen;
        reopen = false;
        need_read = false;
        next_path = path;
      }
      const auto cancelled = [this, epoch] { return generation.load() != epoch; };
      Result next;
      try {
        if (next_path.empty()) { reader.close(); continue; }
        if (!reset || reader.open(next_path, next.error, cancelled)) {
          next.kind = reader.readNext(next.frame, next.error, cancelled);
        }
      } catch (const std::exception& e) {
        next.error = std::string("Point-cloud reader failure: ") + e.what();
      }
      std::lock_guard<std::mutex> lock(mutex);
      if (cancelled() || stopping) { continue; }
      info = reader.info();
      result = std::move(next);
    }
  }
};

AsyncPointCloudReplay::AsyncPointCloudReplay() : impl_(std::make_unique<Impl>()) {
  impl_->worker = std::thread([this] { impl_->run(); });
}
AsyncPointCloudReplay::~AsyncPointCloudReplay() {
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->stopping = true;
    ++impl_->generation;
    impl_->condition.notify_all();
  }
  impl_->worker.join();
}
bool AsyncPointCloudReplay::open(const std::filesystem::path& path, std::string& error) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  ++impl_->generation;
  impl_->path = path;
  impl_->open = !path.empty();
  impl_->info = {};
  impl_->result.reset();
  impl_->reopen = true;
  impl_->condition.notify_one();
  error.clear();
  return impl_->open;
}
PointCloudReadResult AsyncPointCloudReplay::readNext(PointCloudSnapshot& frame, std::string& error) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (!impl_->open) { error = "No point-cloud file is open"; return PointCloudReadResult::Error; }
  if (!impl_->result) { error.clear(); return PointCloudReadResult::Pending; }
  auto result = std::move(*impl_->result);
  impl_->result.reset();
  if (result.kind != PointCloudReadResult::FrameReady) {
    impl_->result = Impl::Result{result.kind, {}, result.error};
  }
  frame = std::move(result.frame);
  error = std::move(result.error);
  impl_->need_read = result.kind == PointCloudReadResult::FrameReady;
  impl_->condition.notify_one();
  return result.kind;
}
bool AsyncPointCloudReplay::rewind(std::string& error) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (!impl_->open) { error = "No point-cloud file is open"; return false; }
  ++impl_->generation;
  impl_->reopen = true;
  impl_->result.reset();
  impl_->condition.notify_one();
  error.clear();
  return true;
}
void AsyncPointCloudReplay::close() { std::string ignored; open({}, ignored); }
bool AsyncPointCloudReplay::isOpen() const {
  std::lock_guard<std::mutex> lock(impl_->mutex); return impl_->open;
}
PointCloudReplayInfo AsyncPointCloudReplay::info() const {
  std::lock_guard<std::mutex> lock(impl_->mutex); return impl_->info;
}
}  // namespace fmcw
