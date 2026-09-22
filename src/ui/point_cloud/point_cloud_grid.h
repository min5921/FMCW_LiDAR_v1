#pragma once

#include <algorithm>
#include <cmath>

namespace fmcw {

// Grid coordinates are world meters, independent of fit normalization and zoom.
// Bound the line count; a larger cloud never silently changes the selected unit.
struct PointCloudGrid {
  static constexpr int maximum_half_cells = 100;
  float spacing_m;
  int half_cells;

  PointCloudGrid(float radius_m, float selected_spacing_m)
      : spacing_m(std::isfinite(selected_spacing_m) && selected_spacing_m > 0.0F
                      ? selected_spacing_m : 1.0F),
        half_cells(static_cast<int>(std::clamp(
            std::ceil(static_cast<double>(std::max(radius_m, 0.0F)) / spacing_m),
            1.0, static_cast<double>(maximum_half_cells)))) {}

  float coordinate(int index) const { return index * spacing_m; }
  float radius() const { return coordinate(half_cells); }
};

}  // namespace fmcw
