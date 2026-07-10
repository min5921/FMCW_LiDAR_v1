#pragma once

#include "core/config_types.h"

#include <string>
#include <vector>

namespace fmcw {

enum class ValidationSeverity {
  Info,
  Warning,
  Error,
};

struct ValidationIssue {
  ValidationSeverity severity = ValidationSeverity::Error;
  std::string path;
  std::string message;
  std::string action;
};

struct ValidationResult {
  std::vector<ValidationIssue> issues;

  bool hasErrors() const;
  bool hasWarnings() const;
};

class ConfigValidator {
 public:
  static ValidationResult validate(const SystemConfig& config);
};

std::string toString(ValidationSeverity severity);

}  // namespace fmcw
