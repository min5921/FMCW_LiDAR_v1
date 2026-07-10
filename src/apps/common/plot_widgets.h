#pragma once

#include "apps/common/application_controller.h"

#include <QColor>
#include <QPair>
#include <QString>
#include <QVector>
#include <QWidget>

#include <cstdint>

namespace fmcw {

struct PlotSeries {
  QString name;
  QVector<float> values;
  QColor color;
};

class LinePlotWidget final : public QWidget {
 public:
  explicit LinePlotWidget(QWidget* parent = nullptr);

  void setTitle(QString title);
  void setAxisLabels(QString x_label, QString y_label);
  void setSeries(QVector<PlotSeries> series);
  void setAutoRange(bool enabled);
  void setManualRange(float minimum, float maximum);
  QPair<float, float> currentRange() const;
  void clear();

 protected:
  void paintEvent(QPaintEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;

 private:
  QString title_;
  QString x_label_;
  QString y_label_;
  QVector<PlotSeries> series_;
  bool auto_range_ = true;
  float range_minimum_ = -1.0F;
  float range_maximum_ = 1.0F;
};

class HeatmapWidget final : public QWidget {
 public:
  explicit HeatmapWidget(QWidget* parent = nullptr);

  void setData(std::uint32_t width, std::uint32_t height, const std::vector<float>& values,
               const std::vector<std::uint8_t>& valid, std::uint32_t completed_lines);
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
