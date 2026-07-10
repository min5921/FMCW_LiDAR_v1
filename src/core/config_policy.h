#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace fmcw {

enum class UiExposure {
  Basic,
  Advanced,
};

enum class ChangePolicy {
  Runtime,
  PreviewOnly,
  RestartRequired,
};

struct ConfigFieldPolicy {
  std::string path;
  UiExposure exposure = UiExposure::Advanced;
  ChangePolicy change_policy = ChangePolicy::RestartRequired;
};

const std::vector<ConfigFieldPolicy>& allConfigFieldPolicies();
ConfigFieldPolicy policyFor(std::string_view path);
std::string toString(UiExposure exposure);
std::string toString(ChangePolicy policy);

}  // namespace fmcw
