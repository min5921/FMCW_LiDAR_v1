#pragma once

#include <string>

namespace fmcw {

enum class OperationState {
  Disconnected,
  Connected,
  Configured,
  Ready,
  Preview,
  Acquiring,
  Recording,
  Paused,
  Stopping,
  Error,
};

class StateMachine {
 public:
  OperationState state() const;
  const std::string& lastError() const;

  bool canStart() const;
  bool canStop() const;
  bool canTransitionTo(OperationState next) const;
  bool transitionTo(OperationState next);
  void fail(std::string message);
  void reset();

 private:
  OperationState state_ = OperationState::Disconnected;
  std::string last_error_;
};

bool isTransitionAllowed(OperationState from, OperationState to);
std::string toString(OperationState state);

}  // namespace fmcw
