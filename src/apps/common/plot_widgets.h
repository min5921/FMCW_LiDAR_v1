#pragma once

#include "apps/common/application_controller.h"

#include <QColor>
#include <QElapsedTimer>
#include <QPair>
#include <QString>
#include <QVector>
#include <QWidget>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace fmcw {

struct PlotSeries {
  QString name;
  std::shared_ptr<const std::vector<float>> values;
  QColor color;
  std::size_t display_count = 0;
  bool contains_gap = false;
};

struct PlotDisplayMetrics {
  double interval_seconds = 0.0;
  double observed_source_hz = 0.0;
  double delivery_hz = 0.0;
  double paint_hz = 0.0;
  double set_series_p95_ms = 0.0;
  double paint_p95_ms = 0.0;
  double paint_max_ms = 0.0;
  std::uint64_t dma_sequences_not_delivered = 0;
  std::uint64_t gui_updates_merged = 0;
  std::uint64_t maximum_dma_step = 0;
  std::uint64_t delivery_count = 0;
  std::uint64_t paint_count = 0;
};

class LinePlotWidget final : public QWidget {
 public:
  explicit LinePlotWidget(QWidget* parent = nullptr);

  void setTitle(QString title);
  void setAxisLabels(QString x_label, QString y_label);
  void setSeries(QVector<PlotSeries> series,
                 std::optional<std::uint64_t> source_sequence = std::nullopt);
  void setAutoRange(bool enabled);
  void setManualRange(float minimum, float maximum);
  QPair<float, float> currentRange() const;
  PlotDisplayMetrics takeDisplayMetrics();
  void resetDisplayMetrics();
  void clear();

 protected:
  void paintEvent(QPaintEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;

 private:
  QString title_;
  QString x_label_;
  QString y_label_;
  QVector<PlotSeries> series_;
  std::size_t longest_series_ = 0;
  bool has_finite_value_ = false;
  float data_minimum_ = 0.0F;
  float data_maximum_ = 0.0F;
  bool auto_range_ = true;
  float range_minimum_ = -1.0F;
  float range_maximum_ = 1.0F;
  QElapsedTimer display_metrics_timer_;
  std::uint64_t display_delivery_count_ = 0;
  std::uint64_t display_paint_count_ = 0;
  std::uint64_t display_source_sequence_advance_ = 0;
  std::uint64_t display_dma_sequences_not_delivered_ = 0;
  std::uint64_t display_gui_updates_merged_ = 0;
  std::uint64_t display_maximum_dma_step_ = 0;
  std::uint64_t pending_series_updates_ = 0;
  std::uint64_t last_source_sequence_ = 0;
  bool has_source_sequence_ = false;
  std::vector<double> set_series_durations_ms_;
  std::vector<double> paint_durations_ms_;
};

class HeatmapWidget final : public QWidget {
 public:
  explicit HeatmapWidget(QWidget* parent = nullptr);

  void setData(std::uint32_t width, std::uint32_t height, const std::vector<float>& values,
               const std::vector<std::uint8_t>& valid, std::uint32_t completed_lines,
               std::uint64_t scan_frame_index);
  void setAutoRange(bool enabled);
  void setManualRange(float minimum, float maximum);
  QPair<float, float> currentRange() const;
  void clear();

 protected:
  void paintEvent(QPaintEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;

 private:
  std::uint32_t width_ = 0;
  std::uint32_t height_ = 0;
  std::uint32_t completed_lines_ = 0;
  std::uint64_t scan_frame_index_ = 0;
  QVector<float> values_;
  QVector<std::uint8_t> valid_;
  bool auto_range_ = true;
  float range_minimum_ = 0.0F;
  float range_maximum_ = 1.0F;
};

class SegmentationPlotWidget final : public QWidget {
 public:
  explicit SegmentationPlotWidget(QWidget* parent = nullptr);

  void setSnapshot(WaveformSnapshotPtr snapshot);
  void setSegments(SegmentRange up, SegmentRange down, std::uint32_t guard_samples);
  void clear();

 protected:
  void paintEvent(QPaintEvent* event) override;

 private:
  WaveformSnapshotPtr snapshot_;
  SegmentRange up_;
  SegmentRange down_;
  std::uint32_t guard_samples_ = 0;
};

}  // namespace fmcw
