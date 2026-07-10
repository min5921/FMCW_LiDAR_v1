#include "core/config_policy.h"

#include "core/config_profile.h"

#include <algorithm>
#include <array>

namespace fmcw {
namespace {

template <std::size_t Size>
bool contains(const std::array<std::string_view, Size>& values, std::string_view value) {
  return std::find(values.begin(), values.end(), value) != values.end();
}

ConfigFieldPolicy classify(std::string_view path) {
  static constexpr std::array<std::string_view, 11> basic_paths = {
      "profile.id", "profile.name", "storage.raw_enabled", "storage.processed_enabled", "udp.enabled",
      "ui.mode", "ui.plot_update_hz", "ui.point_cloud_update_hz", "ui.segment_overlay", "ui.color_map",
      "ui.last_profile"};
  static constexpr std::array<std::string_view, 12> runtime_paths = {
      "processing.dc_removal", "processing.normalize", "processing.peak_threshold_db",
      "processing.peak_search_start_bin", "processing.peak_search_end_bin", "storage.raw_enabled",
      "storage.processed_enabled", "udp.enabled", "ui.mode", "ui.plot_update_hz",
      "ui.point_cloud_update_hz", "ui.segment_overlay"};
  static constexpr std::array<std::string_view, 7> preview_paths = {
      "chirp_segmentation.up_segment.start_sample", "chirp_segmentation.up_segment.end_sample_exclusive",
      "chirp_segmentation.down_segment.start_sample", "chirp_segmentation.down_segment.end_sample_exclusive",
      "chirp_segmentation.guard_samples", "processing.peak_search_start_bin", "processing.peak_search_end_bin"};

  ConfigFieldPolicy result;
  result.path = std::string(path);
  result.exposure = contains(basic_paths, path) ? UiExposure::Basic : UiExposure::Advanced;
  if (contains(runtime_paths, path)) {
    result.change_policy = ChangePolicy::Runtime;
  } else if (contains(preview_paths, path)) {
    result.change_policy = ChangePolicy::PreviewOnly;
  }
  return result;
}

}  // namespace

const std::vector<ConfigFieldPolicy>& allConfigFieldPolicies() {
  static const std::vector<ConfigFieldPolicy> policies = [] {
    std::vector<ConfigFieldPolicy> result;
    const auto document = ConfigProfileCodec::encode(SystemConfig{});
    for (const auto& entry : document.values()) {
      result.push_back(classify(entry.first));
    }
    return result;
  }();
  return policies;
}

ConfigFieldPolicy policyFor(std::string_view path) {
  const auto& policies = allConfigFieldPolicies();
  const auto found = std::find_if(policies.begin(), policies.end(), [path](const ConfigFieldPolicy& policy) {
    return policy.path == path;
  });
  return found == policies.end() ? classify(path) : *found;
}

std::string toString(UiExposure exposure) {
  return exposure == UiExposure::Basic ? "basic" : "advanced";
}

std::string toString(ChangePolicy policy) {
  switch (policy) {
    case ChangePolicy::Runtime: return "runtime";
    case ChangePolicy::PreviewOnly: return "preview_only";
    case ChangePolicy::RestartRequired: return "restart_required";
  }
  return "unknown";
}

}  // namespace fmcw
