#pragma once

namespace fmcw {

enum class RealtimeThreadPriority {
  High,
  Critical,
};

void prioritizeCurrentRealtimeThread(
    RealtimeThreadPriority priority = RealtimeThreadPriority::Critical);

}  // namespace fmcw
