#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace fmcw {

// Historical Phase 2 policy index. RuntimeWorker is the shipping transition policy;
// this table must not be used as evidence of GUI command behavior.

enum class FieldPresentation {
  Primary,
  Detailed,
};

enum class ChangePolicy {
  Runtime,
  PreviewOnly,
  RestartRequired,
};

struct ConfigFieldPolicy {
  std::string path;
  FieldPresentation presentation = FieldPresentation::Detailed;
  ChangePolicy change_policy = ChangePolicy::RestartRequired;
};

const std::vector<ConfigFieldPolicy>& allConfigFieldPolicies();
ConfigFieldPolicy policyFor(std::string_view path);
std::string toString(FieldPresentation presentation);
std::string toString(ChangePolicy policy);

}  // namespace fmcw
