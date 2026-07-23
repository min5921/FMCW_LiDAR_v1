#include "apps/common/plot_widgets.h"

#include <QImage>
#include <QLinearGradient>
#include <QLine>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPalette>
#include <QPoint>
#include <QToolTip>

#include <algorithm>
#include <cmath>
#include <limits>

namespace fmcw {
namespace {

constexpr int kLeft = 68;
constexpr int kTop = 42;
constexpr int kRight = 22;
constexpr int kBottom = 54;

QRectF plotRect(const QWidget& widget) {
  return QRectF(kLeft, kTop, std::max(1, widget.width() - kLeft - kRight),
                std::max(1, widget.height() - kTop - kBottom));
}

void drawFrame(QPainter& painter, const QWidget& widget, const QString& title,
               const QString& x_label, const QString& y_label) {
  const auto area = plotRect(widget);
  const auto palette = widget.palette();
  painter.fillRect(widget.rect(), palette.color(QPalette::Base));
  painter.setRenderHint(QPainter::Antialiasing);
  auto grid_color = palette.color(QPalette::Mid);
  grid_color.setAlpha(150);
  painter.setPen(grid_color);
  for (int index = 0; index <= 5; ++index) {
    const auto x = area.left() + area.width() * index / 5.0;
    const auto y = area.top() + area.height() * index / 5.0;
    painter.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
    painter.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
  }
  painter.setPen(QPen(palette.color(QPalette::Midlight), 1.0));
  painter.drawRect(area);

  QFont title_font = painter.font();
  title_font.setPointSize(10);
  title_font.setBold(true);
  painter.setFont(title_font);
  painter.setPen(palette.color(QPalette::Text));
  painter.drawText(QRectF(kLeft, 10, area.width(), 24), Qt::AlignLeft | Qt::AlignVCenter, title);

  QFont axis_font = painter.font();
  axis_font.setPointSize(8);
  axis_font.setBold(false);
  painter.setFont(axis_font);
  painter.setPen(palette.color(QPalette::PlaceholderText));
  painter.drawText(QRectF(area.left(), area.bottom() + 31, area.width(), 18),
                   Qt::AlignCenter, x_label);
  painter.save();
  painter.translate(16, area.center().y());
  painter.rotate(-90.0);
  painter.drawText(QRectF(-area.height() / 2.0, -10, area.height(), 20), Qt::AlignCenter, y_label);
  painter.restore();
}

QString axisValue(double value, bool integer) {
  if (integer) {
    return QString::number(static_cast<qlonglong>(std::llround(value)));
  }
  const auto magnitude = std::abs(value);
  if (magnitude >= 10000.0 || (magnitude > 0.0 && magnitude < 0.001)) {
    return QString::number(value, 'e', 2);
  }
  return QString::number(value, 'g', 4);
}

double percentile(std::vector<double> values, double quantile) {
  if (values.empty()) {
    return 0.0;
  }
  std::sort(values.begin(), values.end());
  const auto rank = static_cast<std::size_t>(std::ceil(
      std::clamp(quantile, 0.0, 1.0) * static_cast<double>(values.size())));
  return values[std::min(values.size() - 1U, rank == 0U ? 0U : rank - 1U)];
}

void drawAxisTicks(QPainter& painter, const QWidget& widget, double x_minimum, double x_maximum,
                   double y_minimum, double y_maximum, bool integer_x, bool integer_y = false) {
  const auto area = plotRect(widget);
  const auto color = widget.palette().color(QPalette::PlaceholderText);
  painter.save();
  painter.setClipping(false);
  painter.setPen(color);
  QFont tick_font = painter.font();
  tick_font.setPointSize(8);
  tick_font.setBold(false);
  painter.setFont(tick_font);

  constexpr int kTickCount = 5;
  for (int index = 0; index <= kTickCount; ++index) {
    const auto ratio = static_cast<double>(index) / kTickCount;
    const auto x = area.left() + area.width() * ratio;
    const auto x_value = x_minimum + (x_maximum - x_minimum) * ratio;
    painter.drawLine(QPointF(x, area.bottom()), QPointF(x, area.bottom() + 4));
    const QRectF label_rect(index == 0 ? area.left() :
                            index == kTickCount ? area.right() - 68.0 : x - 34.0,
                            area.bottom() + 5.0, 68.0, 17.0);
    const auto alignment = index == 0 ? Qt::AlignLeft :
                           index == kTickCount ? Qt::AlignRight : Qt::AlignHCenter;
    painter.drawText(label_rect, alignment | Qt::AlignTop, axisValue(x_value, integer_x));

    const auto y = area.bottom() - area.height() * ratio;
    const auto y_value = y_minimum + (y_maximum - y_minimum) * ratio;
    painter.drawLine(QPointF(area.left() - 4, y), QPointF(area.left(), y));
    painter.drawText(QRectF(2, y - 9.0, kLeft - 8.0, 18.0), Qt::AlignRight | Qt::AlignVCenter,
                     axisValue(y_value, integer_y));
  }
  painter.restore();
}

QColor heatColor(float value) {
  value = std::clamp(value, 0.0F, 1.0F);
  if (value < 0.25F) {
    return QColor::fromRgbF(0.06F, 0.12F + value * 1.8F, 0.30F + value * 2.0F);
  }
  if (value < 0.55F) {
    const auto t = (value - 0.25F) / 0.30F;
    return QColor::fromRgbF(0.05F, 0.57F + t * 0.25F, 0.62F - t * 0.35F);
  }
  if (value < 0.80F) {
    const auto t = (value - 0.55F) / 0.25F;
    return QColor::fromRgbF(0.05F + t * 0.90F, 0.82F - t * 0.14F, 0.27F - t * 0.15F);
  }
  const auto t = (value - 0.80F) / 0.20F;
  return QColor::fromRgbF(0.95F, 0.68F - t * 0.55F, 0.12F - t * 0.08F);
}

std::size_t displayedValueCount(const PlotSeries& series) {
  if (series.values == nullptr) {
    return 0U;
  }
  return series.display_count == 0U
      ? series.values->size()
      : std::min(series.display_count, series.values->size());
}

int mappedY(float value, const QRectF& area, float minimum, float maximum) {
  const auto normalized = static_cast<double>(value - minimum) /
      static_cast<double>(maximum - minimum);
  return qRound(area.bottom() - area.height() * normalized);
}

void drawDenseEnvelope(QPainter& painter, const std::vector<float>& values,
                       std::size_t value_count, const QRectF& area,
                       float minimum, float maximum) {
  const int column_count = std::max(1, std::min(
      static_cast<int>(value_count), static_cast<int>(std::floor(area.width()))));
  std::vector<QLine> lines;
  lines.reserve(static_cast<std::size_t>(column_count));
  for (int column = 0; column < column_count; ++column) {
    const auto begin = static_cast<std::size_t>(column) * value_count /
        static_cast<std::size_t>(column_count);
    const auto end = std::max(begin + 1U,
        static_cast<std::size_t>(column + 1) * value_count /
            static_cast<std::size_t>(column_count));
    float bucket_minimum = std::numeric_limits<float>::max();
    float bucket_maximum = std::numeric_limits<float>::lowest();
    bool valid = false;
    for (std::size_t index = begin; index < std::min(end, value_count); ++index) {
      const float value = values[index];
      if (!std::isfinite(value)) {
        continue;
      }
      bucket_minimum = std::min(bucket_minimum, value);
      bucket_maximum = std::max(bucket_maximum, value);
      valid = true;
    }
    if (!valid) {
      continue;
    }
    const int x = qRound(area.left() +
        (static_cast<double>(column) + 0.5) * area.width() /
            static_cast<double>(column_count));
    lines.emplace_back(x, mappedY(bucket_maximum, area, minimum, maximum),
                       x, mappedY(bucket_minimum, area, minimum, maximum));
  }
  if (!lines.empty()) {
    painter.drawLines(lines.data(), static_cast<int>(lines.size()));
  }
}

void drawSparsePolyline(QPainter& painter, const std::vector<float>& values,
                        std::size_t value_count, const QRectF& area,
                        float minimum, float maximum) {
  std::vector<QPoint> points;
  points.reserve(value_count);
  const auto flush = [&painter, &points] {
    if (points.size() >= 2U) {
      painter.drawPolyline(points.data(), static_cast<int>(points.size()));
    } else if (points.size() == 1U) {
      painter.drawPoint(points.front());
    }
    points.clear();
  };
  for (std::size_t index = 0; index < value_count; ++index) {
    const float value = values[index];
    if (!std::isfinite(value)) {
      flush();
      continue;
    }
    const int x = qRound(area.left() + area.width() * static_cast<double>(index) /
        static_cast<double>(value_count - 1U));
    points.emplace_back(x, mappedY(value, area, minimum, maximum));
  }
  flush();
}

}  // namespace

LinePlotWidget::LinePlotWidget(QWidget* parent) : QWidget(parent) {
  setMinimumSize(360, 220);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  setMouseTracking(true);
  setAttribute(Qt::WA_OpaquePaintEvent);
  display_metrics_timer_.start();
}

void LinePlotWidget::setTitle(QString title) {
  title_ = std::move(title);
  update();
}

void LinePlotWidget::setAxisLabels(QString x_label, QString y_label) {
  x_label_ = std::move(x_label);
  y_label_ = std::move(y_label);
  update();
}

void LinePlotWidget::setSeries(QVector<PlotSeries> series,
                               std::optional<std::uint64_t> source_sequence) {
  QElapsedTimer elapsed;
  elapsed.start();
  series_ = std::move(series);
  longest_series_ = 0U;
  has_finite_value_ = false;
  data_minimum_ = std::numeric_limits<float>::max();
  data_maximum_ = std::numeric_limits<float>::lowest();
  for (auto& series_item : series_) {
    series_item.contains_gap = false;
    if (series_item.values == nullptr) {
      continue;
    }
    const auto value_count = displayedValueCount(series_item);
    longest_series_ = std::max(longest_series_, value_count);
    for (std::size_t index = 0; index < value_count; ++index) {
      const float value = (*series_item.values)[index];
      if (!std::isfinite(value)) {
        series_item.contains_gap = true;
        continue;
      }
      has_finite_value_ = true;
      data_minimum_ = std::min(data_minimum_, value);
      data_maximum_ = std::max(data_maximum_, value);
    }
  }
  ++display_delivery_count_;
  ++pending_series_updates_;
  if (source_sequence.has_value()) {
    if (has_source_sequence_ && *source_sequence > last_source_sequence_) {
      const auto step = *source_sequence - last_source_sequence_;
      display_source_sequence_advance_ += step;
      display_maximum_dma_step_ = std::max(display_maximum_dma_step_, step);
      if (step > 1U) {
        display_dma_sequences_not_delivered_ += step - 1U;
      }
    }
    last_source_sequence_ = *source_sequence;
    has_source_sequence_ = true;
  }
  set_series_durations_ms_.push_back(static_cast<double>(elapsed.nsecsElapsed()) / 1.0e6);
  update();
}

void LinePlotWidget::setAutoRange(bool enabled) {
  auto_range_ = enabled;
  update();
}

void LinePlotWidget::setManualRange(float minimum, float maximum) {
  if (std::isfinite(minimum) && std::isfinite(maximum) && minimum < maximum) {
    range_minimum_ = minimum;
    range_maximum_ = maximum;
    auto_range_ = false;
    update();
  }
}

QPair<float, float> LinePlotWidget::currentRange() const {
  return {range_minimum_, range_maximum_};
}

PlotDisplayMetrics LinePlotWidget::takeDisplayMetrics() {
  PlotDisplayMetrics metrics;
  const auto elapsed_ms = std::max<qint64>(1, display_metrics_timer_.elapsed());
  metrics.interval_seconds = static_cast<double>(elapsed_ms) / 1000.0;
  metrics.observed_source_hz = static_cast<double>(display_source_sequence_advance_) /
      metrics.interval_seconds;
  metrics.delivery_hz = static_cast<double>(display_delivery_count_) / metrics.interval_seconds;
  metrics.paint_hz = static_cast<double>(display_paint_count_) / metrics.interval_seconds;
  metrics.set_series_p95_ms = percentile(set_series_durations_ms_, 0.95);
  metrics.paint_p95_ms = percentile(paint_durations_ms_, 0.95);
  metrics.paint_max_ms = paint_durations_ms_.empty()
      ? 0.0
      : *std::max_element(paint_durations_ms_.begin(), paint_durations_ms_.end());
  metrics.dma_sequences_not_delivered = display_dma_sequences_not_delivered_;
  metrics.gui_updates_merged = display_gui_updates_merged_;
  metrics.maximum_dma_step = display_maximum_dma_step_;
  metrics.delivery_count = display_delivery_count_;
  metrics.paint_count = display_paint_count_;

  display_delivery_count_ = 0;
  display_paint_count_ = 0;
  display_source_sequence_advance_ = 0;
  display_dma_sequences_not_delivered_ = 0;
  display_gui_updates_merged_ = 0;
  display_maximum_dma_step_ = 0;
  set_series_durations_ms_.clear();
  paint_durations_ms_.clear();
  display_metrics_timer_.restart();
  return metrics;
}

void LinePlotWidget::resetDisplayMetrics() {
  display_delivery_count_ = 0;
  display_paint_count_ = 0;
  display_source_sequence_advance_ = 0;
  display_dma_sequences_not_delivered_ = 0;
  display_gui_updates_merged_ = 0;
  display_maximum_dma_step_ = 0;
  pending_series_updates_ = 0;
  last_source_sequence_ = 0;
  has_source_sequence_ = false;
  set_series_durations_ms_.clear();
  paint_durations_ms_.clear();
  display_metrics_timer_.restart();
}

void LinePlotWidget::clear() {
  series_.clear();
  longest_series_ = 0U;
  has_finite_value_ = false;
  update();
}

void LinePlotWidget::paintEvent(QPaintEvent* event) {
  QElapsedTimer elapsed;
  elapsed.start();
  const auto finish_measurement = [this, &elapsed] {
    if (pending_series_updates_ == 0U) {
      return;
    }
    ++display_paint_count_;
    if (pending_series_updates_ > 1U) {
      display_gui_updates_merged_ += pending_series_updates_ - 1U;
    }
    pending_series_updates_ = 0U;
    paint_durations_ms_.push_back(static_cast<double>(elapsed.nsecsElapsed()) / 1.0e6);
  };
  static_cast<void>(event);
  QPainter painter(this);
  drawFrame(painter, *this, title_, x_label_, y_label_);
  const auto area = plotRect(*this);

  if (longest_series_ < 2U || !has_finite_value_) {
    drawAxisTicks(painter, *this, 0.0, 1.0, 0.0, 1.0, false);
    painter.setPen(palette().color(QPalette::PlaceholderText));
    painter.drawText(area, Qt::AlignCenter, "Waiting for data");
    finish_measurement();
    return;
  }
  float minimum = data_minimum_;
  float maximum = data_maximum_;
  if (std::abs(maximum - minimum) < 1.0e-6F) {
    minimum -= 1.0F;
    maximum += 1.0F;
  }
  if (auto_range_) {
    const auto padding = (maximum - minimum) * 0.08F;
    minimum -= padding;
    maximum += padding;
    range_minimum_ = minimum;
    range_maximum_ = maximum;
  } else {
    minimum = range_minimum_;
    maximum = range_maximum_;
  }

  drawAxisTicks(painter, *this, 0.0, static_cast<double>(longest_series_ - 1U), minimum, maximum, true);

  painter.setClipRect(area.adjusted(1, 1, -1, -1));
  painter.setRenderHint(QPainter::Antialiasing, false);
  for (const auto& series : series_) {
    const auto value_count = displayedValueCount(series);
    if (value_count < 2U) {
      continue;
    }
    QPen pen(series.color);
    pen.setWidth(1);
    pen.setCosmetic(true);
    pen.setCapStyle(Qt::FlatCap);
    pen.setJoinStyle(Qt::MiterJoin);
    painter.setPen(pen);
    if (value_count > static_cast<std::size_t>(std::floor(area.width()))) {
      drawDenseEnvelope(painter, *series.values, value_count, area, minimum, maximum);
    } else {
      drawSparsePolyline(painter, *series.values, value_count, area, minimum, maximum);
    }
  }
  painter.setClipping(false);

  painter.setFont(QFont(painter.font().family(), 8));
  int legend_x = static_cast<int>(area.right()) - 10;
  for (int index = series_.size() - 1; index >= 0; --index) {
    const auto width = painter.fontMetrics().horizontalAdvance(series_[index].name) + 26;
    legend_x -= width;
    painter.setPen(QPen(series_[index].color, 3));
    painter.drawLine(legend_x, 23, legend_x + 14, 23);
    painter.setPen(palette().color(QPalette::Text));
    painter.drawText(legend_x + 18, 28, series_[index].name);
  }
  finish_measurement();
}

void LinePlotWidget::mouseMoveEvent(QMouseEvent* event) {
  const auto area = plotRect(*this);
  if (!area.contains(event->position())) {
    QToolTip::hideText();
    return;
  }
  qsizetype longest = 0;
  for (const auto& series : series_) {
    longest = std::max(longest, static_cast<qsizetype>(displayedValueCount(series)));
  }
  if (longest < 2) {
    return;
  }
  const auto normalized = std::clamp((event->position().x() - area.left()) / area.width(), 0.0, 1.0);
  const auto index = static_cast<qsizetype>(std::round(normalized * static_cast<double>(longest - 1)));
  QStringList lines{QString("Index %1").arg(index)};
  for (const auto& series : series_) {
    if (series.values != nullptr && index < static_cast<qsizetype>(displayedValueCount(series)) &&
        std::isfinite((*series.values)[static_cast<std::size_t>(index)])) {
      lines.append(QString("%1: %2").arg(series.name)
                       .arg((*series.values)[static_cast<std::size_t>(index)], 0, 'g', 6));
    }
  }
  QToolTip::showText(event->globalPosition().toPoint(), lines.join('\n'), this);
}

HeatmapWidget::HeatmapWidget(QWidget* parent) : QWidget(parent) {
  setMinimumSize(480, 280);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  setMouseTracking(true);
}

void HeatmapWidget::setData(std::uint32_t width, std::uint32_t height, const std::vector<float>& values,
                            const std::vector<std::uint8_t>& valid, std::uint32_t completed_lines) {
  width_ = width;
  height_ = height;
  completed_lines_ = completed_lines;
  values_ = QVector<float>(values.begin(), values.end());
  valid_ = QVector<std::uint8_t>(valid.begin(), valid.end());
  update();
}

void HeatmapWidget::setAutoRange(bool enabled) {
  auto_range_ = enabled;
  update();
}

void HeatmapWidget::setManualRange(float minimum, float maximum) {
  if (std::isfinite(minimum) && std::isfinite(maximum) && minimum < maximum) {
    range_minimum_ = minimum;
    range_maximum_ = maximum;
    auto_range_ = false;
    update();
  }
}

QPair<float, float> HeatmapWidget::currentRange() const {
  return {range_minimum_, range_maximum_};
}

void HeatmapWidget::clear() {
  width_ = 0;
  height_ = 0;
  completed_lines_ = 0;
  values_.clear();
  valid_.clear();
  update();
}

void HeatmapWidget::paintEvent(QPaintEvent* event) {
  static_cast<void>(event);
  QPainter painter(this);
  drawFrame(painter, *this, "B-scan Depth Map", "X pixel", "B-scan line");
  const auto area = plotRect(*this);
  const auto expected = static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_);
  if (width_ == 0U || height_ == 0U || static_cast<std::size_t>(values_.size()) < expected) {
    drawAxisTicks(painter, *this, 0.0, 1.0, 0.0, 1.0, false);
    painter.setPen(palette().color(QPalette::PlaceholderText));
    painter.drawText(area, Qt::AlignCenter, "Waiting for completed scan lines");
    return;
  }

  float minimum = std::numeric_limits<float>::max();
  float maximum = std::numeric_limits<float>::lowest();
  for (std::size_t index = 0; index < expected; ++index) {
    if (index < static_cast<std::size_t>(valid_.size()) && valid_[static_cast<int>(index)] != 0U &&
        std::isfinite(values_[static_cast<int>(index)])) {
      minimum = std::min(minimum, values_[static_cast<int>(index)]);
      maximum = std::max(maximum, values_[static_cast<int>(index)]);
    }
  }
  if (minimum == std::numeric_limits<float>::max()) {
    minimum = 0.0F;
    maximum = 1.0F;
  }
  if (std::abs(maximum - minimum) < 1.0e-6F) {
    maximum = minimum + 1.0F;
  }
  if (auto_range_) {
    range_minimum_ = minimum;
    range_maximum_ = maximum;
  } else {
    minimum = range_minimum_;
    maximum = range_maximum_;
  }

  QImage image(static_cast<int>(width_), static_cast<int>(height_), QImage::Format_RGB32);
  for (std::uint32_t y = 0; y < height_; ++y) {
    for (std::uint32_t x = 0; x < width_; ++x) {
      const auto index = static_cast<int>(y * width_ + x);
      const bool is_valid = index < valid_.size() && valid_[index] != 0U &&
          std::isfinite(values_[index]);
      const auto normalized = is_valid ? (values_[index] - minimum) / (maximum - minimum) : 0.0F;
      image.setPixelColor(static_cast<int>(x), static_cast<int>(y),
                          is_valid ? heatColor(normalized) : palette().color(QPalette::AlternateBase));
    }
  }
  painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
  painter.drawImage(area, image);
  drawAxisTicks(painter, *this, 0.0, static_cast<double>(width_ - 1U),
                static_cast<double>(height_ - 1U), 0.0, true, true);
  painter.setPen(palette().color(QPalette::Text));
  painter.drawText(QRectF(area.left(), 10, area.width(), 24), Qt::AlignRight | Qt::AlignVCenter,
                   QString("%1 / %2 lines   Depth %3 to %4 m")
                       .arg(completed_lines_)
                       .arg(height_)
                       .arg(minimum, 0, 'f', 3)
                       .arg(maximum, 0, 'f', 3));
}

void HeatmapWidget::mouseMoveEvent(QMouseEvent* event) {
  const auto area = plotRect(*this);
  if (!area.contains(event->position()) || width_ == 0U || height_ == 0U) {
    QToolTip::hideText();
    return;
  }
  const auto x = std::min(width_ - 1U, static_cast<std::uint32_t>(
      (event->position().x() - area.left()) / area.width() * static_cast<double>(width_)));
  const auto y = std::min(height_ - 1U, static_cast<std::uint32_t>(
      (event->position().y() - area.top()) / area.height() * static_cast<double>(height_)));
  const auto index = static_cast<int>(y * width_ + x);
  const bool valid = index < valid_.size() && valid_[index] != 0U && index < values_.size();
  const auto text = valid ? QString("X %1 | Line %2 | Depth %3 m").arg(x).arg(y).arg(values_[index], 0, 'f', 5)
                          : QString("X %1 | Line %2 | Invalid").arg(x).arg(y);
  QToolTip::showText(event->globalPosition().toPoint(), text, this);
}

SegmentationPlotWidget::SegmentationPlotWidget(QWidget* parent) : QWidget(parent) {
  setMinimumSize(520, 280);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void SegmentationPlotWidget::setSnapshot(WaveformSnapshotPtr snapshot) {
  snapshot_ = std::move(snapshot);
  if (snapshot_ != nullptr) {
    up_ = snapshot_->up_segment;
    down_ = snapshot_->down_segment;
  }
  update();
}

void SegmentationPlotWidget::setSegments(SegmentRange up, SegmentRange down, std::uint32_t guard_samples) {
  up_ = up;
  down_ = down;
  guard_samples_ = guard_samples;
  update();
}

void SegmentationPlotWidget::clear() {
  snapshot_.reset();
  update();
}

void SegmentationPlotWidget::paintEvent(QPaintEvent* event) {
  static_cast<void>(event);
  QPainter painter(this);
  drawFrame(painter, *this, "Frozen Full-period Chirp Segmentation", "Sample", "ADC full scale");
  const auto area = plotRect(*this);
  if (snapshot_ == nullptr || snapshot_->full_scale_samples.size() < 2U) {
    drawAxisTicks(painter, *this, 0.0, 1.0, -1.0, 1.0, false);
    painter.setPen(palette().color(QPalette::PlaceholderText));
    painter.drawText(area, Qt::AlignCenter, "Capture a frame while running");
    return;
  }

  const auto sample_count = static_cast<double>(snapshot_->full_scale_samples.size());
  auto sampleToX = [&](std::uint32_t sample) {
    return area.left() + area.width() * std::clamp(static_cast<double>(sample) / sample_count, 0.0, 1.0);
  };
  const auto drawBand = [&](SegmentRange range, const QColor& color) {
    const auto left = sampleToX(range.start_sample);
    const auto right = sampleToX(range.end_sample_exclusive);
    painter.fillRect(QRectF(left, area.top(), std::max(1.0, right - left), area.height()), color);
  };
  drawBand(up_, QColor(30, 154, 113, 42));
  drawBand(down_, QColor(227, 111, 57, 42));
  if (guard_samples_ > 0U) {
    drawBand({up_.end_sample_exclusive,
              std::min(down_.start_sample, up_.end_sample_exclusive + guard_samples_)},
             QColor(237, 183, 52, 55));
    drawBand({down_.start_sample > guard_samples_ ? down_.start_sample - guard_samples_ : 0U,
              down_.start_sample}, QColor(237, 183, 52, 55));
  }

  QPainterPath path;
  for (std::size_t index = 0; index < snapshot_->full_scale_samples.size(); ++index) {
    const auto x = area.left() + area.width() * static_cast<double>(index) /
                                    static_cast<double>(snapshot_->full_scale_samples.size() - 1U);
    const auto y = area.center().y() - snapshot_->full_scale_samples[index] * area.height() * 0.45;
    if (index == 0U) {
      path.moveTo(x, y);
    } else {
      path.lineTo(x, y);
    }
  }
  painter.setClipRect(area);
  painter.setPen(QPen(palette().color(QPalette::Highlight), 1.2));
  painter.drawPath(path);
  painter.setClipping(false);
  drawAxisTicks(painter, *this, 0.0,
                static_cast<double>(snapshot_->full_scale_samples.size() - 1U), -1.0, 1.0, true);
  painter.setPen(QColor("#4bc596"));
  painter.drawText(QPointF(sampleToX(up_.start_sample) + 5, area.top() + 18), "UP");
  painter.setPen(QColor("#e78a59"));
  painter.drawText(QPointF(sampleToX(down_.start_sample) + 5, area.top() + 18), "DOWN");
  painter.setPen(palette().color(QPalette::PlaceholderText));
  painter.drawText(QRectF(area.left(), 10, area.width(), 24), Qt::AlignRight | Qt::AlignVCenter,
                   QString("Frozen frame %1").arg(snapshot_->frame_id));
}

}  // namespace fmcw
