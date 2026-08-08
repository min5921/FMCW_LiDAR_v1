#include "core/realtime_thread.h"

#include <cstdint>
#include <mutex>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace fmcw {
namespace {

#if defined(_WIN32)
std::mutex process_priority_mutex;
std::uint32_t process_priority_users = 0U;
DWORD original_process_priority = 0U;
bool process_priority_changed = false;
#endif

}  // namespace

RealtimeProcessPriorityScope::RealtimeProcessPriorityScope() {
#if defined(_WIN32)
  std::lock_guard<std::mutex> lock(process_priority_mutex);
  if (process_priority_users == 0U) {
    original_process_priority = GetPriorityClass(GetCurrentProcess());
    process_priority_changed = original_process_priority != 0U &&
        original_process_priority != HIGH_PRIORITY_CLASS &&
        original_process_priority != REALTIME_PRIORITY_CLASS &&
        SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS) != FALSE;
  }
  ++process_priority_users;
  active_ = true;
#endif
}

RealtimeProcessPriorityScope::~RealtimeProcessPriorityScope() {
#if defined(_WIN32)
  std::lock_guard<std::mutex> lock(process_priority_mutex);
  if (!active_ || process_priority_users == 0U) {
    return;
  }
  --process_priority_users;
  if (process_priority_users == 0U) {
    if (process_priority_changed) {
      SetPriorityClass(GetCurrentProcess(), original_process_priority);
    }
    original_process_priority = 0U;
    process_priority_changed = false;
  }
#endif
}

void prioritizeCurrentRealtimeThread(RealtimeThreadPriority priority) {
#if defined(_WIN32)
  thread_local int configured_priority = THREAD_PRIORITY_NORMAL;
  // Keep acquisition ahead of ordinary work without starving Qt and DWM threads.
  const auto requested_priority = priority == RealtimeThreadPriority::Critical
      ? THREAD_PRIORITY_HIGHEST
      : THREAD_PRIORITY_ABOVE_NORMAL;
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
