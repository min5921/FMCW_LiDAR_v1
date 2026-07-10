#include "core/operation_controller.h"

#include <utility>

namespace fmcw {

OperationState OperationController::state() const { return state_machine_.state(); }

const std::string& OperationController::lastError() const { return state_machine_.lastError(); }

const StopContext& OperationController::lastStop() const { return last_stop_; }

bool OperationController::markConnected() { return state_machine_.transitionTo(OperationState::Connected); }

bool OperationController::markConfigured() { return state_machine_.transitionTo(OperationState::Configured); }

bool OperationController::markReady() { return state_machine_.transitionTo(OperationState::Ready); }

bool OperationController::startPreview() { return state_machine_.transitionTo(OperationState::Preview); }

StartRequestResult OperationController::requestStart(const SystemConfig& config, std::uint64_t config_revision,
                                                      std::string config_snapshot_json) {
  StartRequestResult result;
  result.validation = ConfigValidator::validate(config);
  if (result.validation.hasErrors()) {
    result.message = "Start blocked by invalid configuration";
    return result;
  }
  if (!state_machine_.canStart() || !state_machine_.transitionTo(OperationState::Acquiring)) {
    result.message = state_machine_.lastError().empty() ? "System is not ready to start" : state_machine_.lastError();
    return result;
  }
  last_stop_ = {};
  result.accepted = true;
  result.config_revision = config_revision;
  result.config_snapshot_json = std::move(config_snapshot_json);
  result.message = "Acquisition started";
  return result;
}

bool OperationController::setRecording(bool enabled) {
  return state_machine_.transitionTo(enabled ? OperationState::Recording : OperationState::Acquiring);
}

bool OperationController::setPaused(bool enabled) {
  if (enabled) {
    return state_machine_.transitionTo(OperationState::Paused);
  }
  return state_machine_.transitionTo(OperationState::Acquiring);
}

bool OperationController::requestStop(StopCause cause, std::string source, std::string detail) {
  if (!state_machine_.canStop() || !state_machine_.transitionTo(OperationState::Stopping)) {
    return false;
  }
  last_stop_ = {cause, std::move(source), std::move(detail), 0, 0, 0};
  return true;
}

bool OperationController::handleQueueOverflow(std::string source, std::size_t queue_size,
                                              std::size_t queue_capacity, std::uint64_t last_frame_id) {
  if (!state_machine_.canStop() || !state_machine_.transitionTo(OperationState::Stopping)) {
    return false;
  }
  last_stop_ = {StopCause::QueueOverflow, std::move(source), "Real-time queue capacity exceeded",
                last_frame_id, queue_size, queue_capacity};
  return true;
}

bool OperationController::completeStop() {
  if (state_machine_.state() != OperationState::Stopping) {
    return false;
  }
  if (last_stop_.cause == StopCause::UserRequest || last_stop_.cause == StopCause::None) {
    return state_machine_.transitionTo(OperationState::Ready);
  }
  state_machine_.fail(toString(last_stop_.cause) + " from " + last_stop_.source + ": " + last_stop_.detail);
  return true;
}

void OperationController::failDevice(std::string source, std::string detail, std::uint64_t last_frame_id) {
  last_stop_ = {StopCause::DeviceError, std::move(source), std::move(detail), last_frame_id, 0, 0};
  state_machine_.fail(last_stop_.source + ": " + last_stop_.detail);
}

void OperationController::reset() {
  state_machine_.reset();
  last_stop_ = {};
}

std::string toString(StopCause cause) {
  switch (cause) {
    case StopCause::None: return "none";
    case StopCause::UserRequest: return "user_request";
    case StopCause::QueueOverflow: return "queue_overflow";
    case StopCause::WriterFailure: return "writer_failure";
    case StopCause::EmergencyStop: return "emergency_stop";
    case StopCause::DeviceError: return "device_error";
  }
  return "unknown";
}

}  // namespace fmcw
