#include "core/config_manager.h"

#include "core/config_profile.h"

#include <iterator>
#include <utility>

namespace fmcw {
namespace {

bool scalarEqual(const ConfigScalar& left, const ConfigScalar& right) {
  return left.kind == right.kind && left.text == right.text;
}

bool isIdle(OperationState state) {
  return state == OperationState::Disconnected || state == OperationState::Connected ||
         state == OperationState::Configured || state == OperationState::Ready || state == OperationState::Error;
}

bool applyImmediately(ChangePolicy policy, OperationState state) {
  if (isIdle(state)) {
    return true;
  }
  if (state == OperationState::Preview) {
    return policy == ChangePolicy::Runtime || policy == ChangePolicy::PreviewOnly;
  }
  if (state == OperationState::Acquiring || state == OperationState::Recording || state == OperationState::Paused) {
    return policy == ChangePolicy::Runtime;
  }
  return false;
}

std::string rendered(const ConfigScalar& scalar) {
  return scalar.text;
}

}  // namespace

ConfigManager::ConfigManager(SystemConfig initial_config) : active_config_(std::move(initial_config)) {}

const SystemConfig& ConfigManager::activeConfig() const { return active_config_; }

const std::optional<SystemConfig>& ConfigManager::pendingConfig() const { return pending_config_; }

std::uint64_t ConfigManager::revision() const { return revision_; }

bool ConfigManager::hasPendingChanges() const { return pending_config_.has_value(); }

ConfigUpdateResult ConfigManager::requestUpdate(const SystemConfig& requested, OperationState state) {
  ConfigUpdateResult result;
  result.validation = ConfigValidator::validate(requested);
  if (result.validation.hasErrors()) {
    result.message = "Configuration update rejected by validation";
    return result;
  }

  auto active_document = ConfigProfileCodec::encode(active_config_);
  const auto requested_document = ConfigProfileCodec::encode(requested);
  for (const auto& entry : requested_document.values()) {
    const auto* current = active_document.find(entry.first);
    if (current != nullptr && scalarEqual(*current, entry.second)) {
      continue;
    }

    const auto policy = policyFor(entry.first);
    ConfigChange change{entry.first, current == nullptr ? std::string{} : rendered(*current),
                        rendered(entry.second), policy.change_policy};
    if (applyImmediately(policy.change_policy, state)) {
      active_document.setScalar(entry.first, entry.second);
      result.applied_changes.push_back(std::move(change));
    } else {
      result.pending_changes.push_back(std::move(change));
    }
  }

  if (!result.applied_changes.empty()) {
    SystemConfig updated = active_config_;
    std::vector<ConfigProfileIssue> issues;
    if (!ConfigProfileCodec::decode(active_document, updated, issues, "<active update>")) {
      result.message = "Internal error while applying configuration update";
      return result;
    }
    const auto active_validation = ConfigValidator::validate(updated);
    if (active_validation.hasErrors() && !result.pending_changes.empty()) {
      result.pending_changes.insert(result.pending_changes.end(),
                                    std::make_move_iterator(result.applied_changes.begin()),
                                    std::make_move_iterator(result.applied_changes.end()));
      result.applied_changes.clear();
    } else if (active_validation.hasErrors()) {
      result.message = "Configuration update would make the active configuration invalid";
      return result;
    } else {
      active_config_ = std::move(updated);
      ++revision_;
    }
  }

  if (result.pending_changes.empty()) {
    pending_config_.reset();
  } else {
    pending_config_ = requested;
  }
  result.accepted = true;
  result.message = result.pending_changes.empty() ? "Configuration applied" : "Restart-required changes are pending";
  return result;
}

bool ConfigManager::applyPending(OperationState state, std::string& error) {
  if (!pending_config_.has_value()) {
    return true;
  }
  if (!isIdle(state)) {
    error = "Pending configuration can only be applied while acquisition is idle";
    return false;
  }
  const auto validation = ConfigValidator::validate(*pending_config_);
  if (validation.hasErrors()) {
    error = "Pending configuration is no longer valid";
    return false;
  }
  active_config_ = std::move(*pending_config_);
  pending_config_.reset();
  ++revision_;
  error.clear();
  return true;
}

void ConfigManager::discardPending() { pending_config_.reset(); }

std::string ConfigManager::activeSnapshotJson() const {
  return ConfigProfileCodec::toJsonSnapshot(active_config_);
}

}  // namespace fmcw
