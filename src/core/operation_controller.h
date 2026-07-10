#pragma once

#include "core/config_types.h"
#include "core/config_validation.h"
#include "core/system_state.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace fmcw {

enum class StopCause {
  None,
  UserRequest,
  QueueOverflow,
  WriterFailure,
  EmergencyStop,
  DeviceError,
};

struct StopContext {
  StopCause cause = StopCause::None;
  std::string source;
  std::string detail;
  std::uint64_t last_frame_id = 0;
  std::size_t queue_size = 0;
  std::size_t queue_capacity = 0;
};

struct StartRequestResult {
  bool accepted = false;
  ValidationResult validation;
  std::uint64_t config_revision = 0;
  std::string config_snapshot_json;
  std::string message;
};

class OperationController {
 public:
  OperationState state() const;
  const std::string& lastError() const;
  const StopContext& lastStop() const;

  bool markConnected();
  bool markConfigured();
  bool markReady();
  bool startPreview();
  StartRequestResult requestStart(const SystemConfig& config, std::uint64_t config_revision,
                                  std::string config_snapshot_json);
  bool setRecording(bool enabled);
  bool setPaused(bool enabled);
  bool requestStop(StopCause cause = StopCause::UserRequest, std::string source = "operator",
                   std::string detail = {});
  bool handleQueueOverflow(std::string source, std::size_t queue_size, std::size_t queue_capacity,
                           std::uint64_t last_frame_id);
  bool completeStop();
  void failDevice(std::string source, std::string detail, std::uint64_t last_frame_id = 0);
  void reset();

 private:
  StateMachine state_machine_;
  StopContext last_stop_;
};

std::string toString(StopCause cause);

}  // namespace fmcw
