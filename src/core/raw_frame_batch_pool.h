#pragma once

#include "core/frame_types.h"

#include <cstddef>
#include <memory>

namespace fmcw {

class RawFrameBatchPool {
 public:
  explicit RawFrameBatchPool(std::size_t maximum_cached_batches = 8);

  MutableRawFrameBatchPtr acquire();
  std::size_t cachedBatchCount() const;

 private:
  struct State;
  std::shared_ptr<State> state_;
};

}  // namespace fmcw
