#pragma once

#include "core/config_document.h"
#include "core/config_types.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace fmcw {

struct ConfigProfileIssue {
  std::string source;
  std::size_t line = 0;
  std::string path;
  std::string message;
};

struct ConfigLoadResult {
  SystemConfig config;
  ConfigDocument merged_document;
  std::vector<ConfigProfileIssue> issues;

  bool ok() const { return issues.empty(); }
};

class ConfigProfileCodec {
 public:
  static ConfigDocument encode(const SystemConfig& config);
  static bool decode(const ConfigDocument& document, SystemConfig& config,
                     std::vector<ConfigProfileIssue>& issues, std::string source = "<document>");

  static ConfigLoadResult decodeYaml(std::string_view yaml, std::string source = "<memory>");
  static ConfigLoadResult loadLayered(const std::vector<std::filesystem::path>& paths);
  static bool save(const std::filesystem::path& path, const SystemConfig& config, std::string& error);

  static std::string toYaml(const SystemConfig& config);
  static std::string toJsonSnapshot(const SystemConfig& config);
};

}  // namespace fmcw
