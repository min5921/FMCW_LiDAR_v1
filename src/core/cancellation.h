#pragma once

#include <functional>

namespace fmcw {
using CancellationCheck = std::function<bool()>;
inline bool isCancelled(const CancellationCheck& check) { return check && check(); }
}  // namespace fmcw
