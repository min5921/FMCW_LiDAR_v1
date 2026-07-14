#include "core/raw_frame_batch_pool.h"

#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace fmcw {

struct RawFrameBatchPool::State {
  explicit State(std::size_t maximum) : maximum_cached_batches(maximum) {}

  mutable std::mutex mutex;
  std::vector<std::unique_ptr<RawFrameBatch>> cached_batches;
  std::size_t maximum_cached_batches = 0;
};

RawFrameBatchPool::RawFrameBatchPool(std::size_t maximum_cached_batches)
    : state_(std::make_shared<State>(maximum_cached_batches)) {}

MutableRawFrameBatchPtr RawFrameBatchPool::acquire() {
  std::unique_ptr<RawFrameBatch> batch;
  {
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (!state_->cached_batches.empty()) {
      batch = std::move(state_->cached_batches.back());
      state_->cached_batches.pop_back();
    }
  }
  if (!batch) {
    batch = std::make_unique<RawFrameBatch>();
  }

  batch->metadata = {};
  for (auto& record : batch->records) {
    record.metadata = {};
  }

  auto state = state_;
  return MutableRawFrameBatchPtr(batch.release(), [state = std::move(state)](RawFrameBatch* released) {
    std::unique_ptr<RawFrameBatch> owned(released);
    std::lock_guard<std::mutex> lock(state->mutex);
    if (state->cached_batches.size() < state->maximum_cached_batches) {
      state->cached_batches.push_back(std::move(owned));
    }
  });
}

std::size_t RawFrameBatchPool::cachedBatchCount() const {
  std::lock_guard<std::mutex> lock(state_->mutex);
  return state_->cached_batches.size();
}

}  // namespace fmcw
