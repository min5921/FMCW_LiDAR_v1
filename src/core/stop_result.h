#pragma once

#include <string>
#include <utility>
#include <vector>

namespace fmcw {

struct StopStageResult {
  std::string stage;
  std::string error;
};

struct StopResult {
  std::vector<StopStageResult> stages;

  void add(std::string stage, std::string error) {
    stages.push_back({std::move(stage), std::move(error)});
  }
  std::string errorSummary() const {
    std::string result;
    for (const auto& stage : stages) {
      if (!stage.error.empty()) {
        if (!result.empty()) { result += "; "; }
        result += stage.stage + ": " + stage.error;
      }
    }
    return result;
  }
  bool succeeded() const { return errorSummary().empty(); }
};

}  // namespace fmcw
