#include "core/system_state.h"

#include <sstream>
#include <utility>

namespace fmcw {

OperationState StateMachine::state() const {
  return state_;
}

const std::string& StateMachine::lastError() const {
  return last_error_;
}

bool StateMachine::canStart() const {
  return state_ == OperationState::Ready || state_ == OperationState::Preview;
}

bool StateMachine::canStop() const {
  return state_ == OperationState::Preview || state_ == OperationState::Acquiring || state_ == OperationState::Recording ||
         state_ == OperationState::Paused;
}

bool StateMachine::canTransitionTo(OperationState next) const {
  return isTransitionAllowed(state_, next);
}

bool StateMachine::transitionTo(OperationState next) {
  if (!canTransitionTo(next)) {
    std::ostringstream message;
    message << "Invalid state transition: " << toString(state_) << " -> " << toString(next);
    last_error_ = message.str();
    return false;
  }

  state_ = next;
  if (next != OperationState::Error) {
    last_error_.clear();
  }
  return true;
}

void StateMachine::fail(std::string message) {
  last_error_ = std::move(message);
  state_ = OperationState::Error;
}

void StateMachine::reset() {
  state_ = OperationState::Disconnected;
  last_error_.clear();
}

bool isTransitionAllowed(OperationState from, OperationState to) {
  switch (from) {
    case OperationState::Disconnected:
      return to == OperationState::Connected || to == OperationState::Error;
    case OperationState::Connected:
      return to == OperationState::Disconnected || to == OperationState::Configured || to == OperationState::Error;
    case OperationState::Configured:
      return to == OperationState::Connected || to == OperationState::Ready || to == OperationState::Error;
    case OperationState::Ready:
      return to == OperationState::Connected || to == OperationState::Preview || to == OperationState::Acquiring ||
             to == OperationState::Error;
    case OperationState::Preview:
      return to == OperationState::Ready || to == OperationState::Acquiring || to == OperationState::Stopping ||
             to == OperationState::Error;
    case OperationState::Acquiring:
      return to == OperationState::Recording || to == OperationState::Paused || to == OperationState::Stopping ||
             to == OperationState::Error;
    case OperationState::Recording:
      return to == OperationState::Acquiring || to == OperationState::Paused || to == OperationState::Stopping ||
             to == OperationState::Error;
    case OperationState::Paused:
      return to == OperationState::Acquiring || to == OperationState::Recording || to == OperationState::Stopping ||
             to == OperationState::Error;
    case OperationState::Stopping:
      return to == OperationState::Ready || to == OperationState::Disconnected || to == OperationState::Error;
    case OperationState::Error:
      return to == OperationState::Stopping || to == OperationState::Disconnected;
  }

  return false;
}

std::string toString(OperationState state) {
  switch (state) {
    case OperationState::Disconnected:
      return "Disconnected";
    case OperationState::Connected:
      return "Connected";
    case OperationState::Configured:
      return "Configured";
    case OperationState::Ready:
      return "Ready";
    case OperationState::Preview:
      return "Preview";
    case OperationState::Acquiring:
      return "Acquiring";
    case OperationState::Recording:
      return "Recording";
    case OperationState::Paused:
      return "Paused";
    case OperationState::Stopping:
      return "Stopping";
    case OperationState::Error:
      return "Error";
  }

  return "Unknown";
}

}  // namespace fmcw
