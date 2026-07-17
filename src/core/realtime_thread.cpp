#include "core/realtime_thread.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace fmcw {

void prioritizeCurrentRealtimeThread(RealtimeThreadPriority priority) {
#if defined(_WIN32)
  thread_local int configured_priority = THREAD_PRIORITY_NORMAL;
  const auto requested_priority = priority == RealtimeThreadPriority::Critical
      ? THREAD_PRIORITY_TIME_CRITICAL
      : THREAD_PRIORITY_HIGHEST;
  if (configured_priority >= requested_priority) {
    return;
  }
  const auto thread = GetCurrentThread();
  THREAD_POWER_THROTTLING_STATE power_state{};
  power_state.Version = THREAD_POWER_THROTTLING_CURRENT_VERSION;
  power_state.ControlMask = THREAD_POWER_THROTTLING_EXECUTION_SPEED;
  power_state.StateMask = 0U;
  SetThreadInformation(thread, ThreadPowerThrottling,
                       &power_state, sizeof(power_state));
  if (SetThreadPriority(thread, requested_priority) != FALSE) {
    SetThreadPriorityBoost(thread, FALSE);
    configured_priority = requested_priority;
  }
#else
  static_cast<void>(priority);
#endif
}

}  // namespace fmcw
