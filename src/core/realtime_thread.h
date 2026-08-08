#pragma once

namespace fmcw {

enum class RealtimeThreadPriority {
  High,
  Critical,
};

class RealtimeProcessPriorityScope {
 public:
  RealtimeProcessPriorityScope();
  ~RealtimeProcessPriorityScope();

  RealtimeProcessPriorityScope(const RealtimeProcessPriorityScope&) = delete;
  RealtimeProcessPriorityScope& operator=(const RealtimeProcessPriorityScope&) = delete;

 private:
  bool active_ = false;
};

void prioritizeCurrentRealtimeThread(
    RealtimeThreadPriority priority = RealtimeThreadPriority::Critical);

}  // namespace fmcw
