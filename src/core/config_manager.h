#pragma once

#include "core/config_policy.h"
#include "core/config_types.h"
#include "core/config_validation.h"
#include "core/system_state.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace fmcw {

struct ConfigChange {
  std::string path;
  std::string before;
  std::string after;
  ChangePolicy policy = ChangePolicy::RestartRequired;
};

struct ConfigUpdateResult {
  bool accepted = false;
  ValidationResult validation;
  std::vector<ConfigChange> applied_changes;
  std::vector<ConfigChange> pending_changes;
  std::string message;
};

class ConfigManager {
 public:
  explicit ConfigManager(SystemConfig initial_config = {});

  const SystemConfig& activeConfig() const;
  const std::optional<SystemConfig>& pendingConfig() const;
  std::uint64_t revision() const;
  bool hasPendingChanges() const;

  ConfigUpdateResult requestUpdate(const SystemConfig& requested, OperationState state);
  bool applyPending(OperationState state, std::string& error);
  void discardPending();

  std::string activeSnapshotJson() const;

 private:
  SystemConfig active_config_;
  std::optional<SystemConfig> pending_config_;
  std::uint64_t revision_ = 1;
};

}  // namespace fmcw
