#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace fmcw {

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
