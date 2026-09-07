#pragma once

#include "core/config_types.h"
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace fmcw {
struct ProcessingConfigEvent {
  std::uint64_t first_source_frame_id = 0;
  std::uint64_t revision = 0;
  SystemConfig setup;
};
bool writeProcessingEvent(const std::filesystem::path& directory, const std::string& stem,
                          const ProcessingConfigEvent& event, std::string& error);
bool readProcessingHistory(const std::filesystem::path& raw_file,
    std::vector<std::shared_ptr<const ProcessingConfigEvent>>& events, std::string& error);
}  // namespace fmcw
