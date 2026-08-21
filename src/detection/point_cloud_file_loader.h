#pragma once

#include "processing/processing_snapshots.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace fmcw {

struct PointCloudFileLoadResult {
  std::shared_ptr<const PointCloudSnapshot> snapshot;
  std::string format_detail;
};

// Loads model-ready Nx5 float32 .bin, raw Waymo Nx6 float32 .bin, or
// ASCII/binary PCD. PCD intensity is treated as already preprocessed for the
// CenterPoint model and elongation defaults to zero when absent.
bool loadCenterPointCloudFile(const std::filesystem::path& path,
                              std::uint64_t scan_frame_index,
                              PointCloudFileLoadResult& result,
                              std::string& error);

}  // namespace fmcw
