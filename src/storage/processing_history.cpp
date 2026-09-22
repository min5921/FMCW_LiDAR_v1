#include "storage/processing_history.h"
#include "core/config/config_profile.h"
#include <algorithm>
#include <charconv>

namespace fmcw {
bool writeProcessingEvent(const std::filesystem::path& directory, const std::string& stem,
                          const ProcessingConfigEvent& event, std::string& error) {
  const auto folder = directory / (stem + ".processing");
  std::error_code ec;
  std::filesystem::create_directories(folder, ec);
  if (ec) { error = "Cannot create processing history: " + ec.message(); return false; }
  const auto path = folder / (std::to_string(event.first_source_frame_id) + "_" +
                               std::to_string(event.revision) + ".yaml");
  const auto pending = std::filesystem::path(path.string() + ".pending");
  if (!ConfigProfileCodec::save(pending, event.setup, error)) { return false; }
  std::filesystem::rename(pending, path, ec);
  if (ec) { error = "Cannot finalize processing history: " + ec.message(); return false; }
  error.clear();
  return true;
}

bool readProcessingHistory(const std::filesystem::path& raw_file,
    std::vector<std::shared_ptr<const ProcessingConfigEvent>>& events, std::string& error) {
  events.clear();
  const auto name = raw_file.filename().string();
  const auto marker = name.rfind(".raw.");
  if (marker == std::string::npos) { error.clear(); return true; }
  const auto folder = raw_file.parent_path() / (name.substr(0, marker) + ".processing");
  std::error_code ec;
  const bool exists = std::filesystem::exists(folder, ec);
  if (ec) { error = ec.message(); return false; }
  if (!exists) { error.clear(); return true; }
  const auto parse = [](std::string_view text, std::uint64_t& number) {
    const auto result = std::from_chars(text.data(), text.data() + text.size(), number);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
  };
  for (std::filesystem::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
    if (it->path().extension() == ".pending") {
      error = "Incomplete processing history entry: " + it->path().string(); return false;
    }
    if (it->path().extension() != ".yaml") { continue; }
    const auto key = it->path().stem().string();
    const auto separator = key.find('_');
    auto event = std::make_shared<ProcessingConfigEvent>();
    if (separator == std::string::npos ||
        !parse(std::string_view(key).substr(0, separator), event->first_source_frame_id) ||
        !parse(std::string_view(key).substr(separator + 1U), event->revision)) {
      error = "Invalid processing history entry: " + key; return false;
    }
    const auto loaded = ConfigProfileCodec::loadLayered({it->path()});
    if (!loaded.ok()) { error = "Cannot decode processing history: " + it->path().string(); return false; }
    event->setup = loaded.config;
    events.push_back(std::move(event));
  }
  if (ec) { error = ec.message(); return false; }
  std::sort(events.begin(), events.end(), [](const auto& a, const auto& b) {
    return a->first_source_frame_id < b->first_source_frame_id;
  });
  for (std::size_t i = 1; i < events.size(); ++i) {
    if (events[i-1]->first_source_frame_id == events[i]->first_source_frame_id) {
      error = "Duplicate processing history boundary"; return false;
    }
  }
  error.clear();
  return true;
}
}  // namespace fmcw
