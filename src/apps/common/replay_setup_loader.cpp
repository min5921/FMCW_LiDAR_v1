#include "apps/common/replay_setup_loader.h"

#include "core/config_profile.h"
#include "core/digitizer_capabilities.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace fmcw {
namespace {

std::filesystem::path fileSystemPath(const QString& path) {
#ifdef Q_OS_WIN
  return std::filesystem::path(path.toStdWString());
#else
  return std::filesystem::path(path.toStdString());
#endif
}

QString recordingPrefix(const QString& raw_path) {
  const auto name = QFileInfo(raw_path).fileName();
  const QRegularExpression expression(
      QStringLiteral("^(.+)\\.raw\\.[0-9]+\\.bin$"),
      QRegularExpression::CaseInsensitiveOption);
  const auto match = expression.match(name);
  return match.hasMatch() ? match.captured(1) : QFileInfo(raw_path).completeBaseName();
}

QString profileLoadError(const ConfigLoadResult& result) {
  QStringList messages;
  for (const auto& issue : result.issues) {
    const auto source = QString::fromStdString(issue.source);
    const auto message = QString::fromStdString(issue.message);
    messages.append(issue.line == 0U
                        ? QStringLiteral("%1: %2").arg(source, message)
                        : QStringLiteral("%1:%2 %3").arg(source).arg(issue.line).arg(message));
  }
  return messages.isEmpty() ? QStringLiteral("Recorded setup YAML could not be decoded")
                            : messages.join(QStringLiteral("; "));
}

bool decodeConfigSnapshot(const QJsonObject& snapshot, SystemConfig& config, QString& error) {
  auto document = ConfigProfileCodec::encode(SystemConfig{});
  for (auto iterator = snapshot.begin(); iterator != snapshot.end(); ++iterator) {
    const auto path = iterator.key().toStdString();
    const auto* expected = document.find(path);
    if (expected == nullptr) {
      continue;
    }
    const auto& value = iterator.value();
    switch (expected->kind) {
      case ConfigScalarKind::String:
        if (!value.isString()) {
          error = QString("Recorded setup field '%1' is not a string").arg(iterator.key());
          return false;
        }
        document.setString(path, value.toString().toStdString());
        break;
      case ConfigScalarKind::Boolean:
        if (!value.isBool()) {
          error = QString("Recorded setup field '%1' is not Boolean").arg(iterator.key());
          return false;
        }
        document.setBoolean(path, value.toBool());
        break;
      case ConfigScalarKind::Integer: {
        if (!value.isDouble()) {
          error = QString("Recorded setup field '%1' is not an integer").arg(iterator.key());
          return false;
        }
        const auto number = value.toDouble();
        const auto rounded = std::round(number);
        if (!std::isfinite(number) || std::abs(number - rounded) > 1.0e-9) {
          error = QString("Recorded setup field '%1' contains an invalid integer").arg(iterator.key());
          return false;
        }
        document.setInteger(path, static_cast<std::int64_t>(rounded));
        break;
      }
      case ConfigScalarKind::Number:
        if (!value.isDouble() || !std::isfinite(value.toDouble())) {
          error = QString("Recorded setup field '%1' is not numeric").arg(iterator.key());
          return false;
        }
        document.setNumber(path, value.toDouble());
        break;
    }
  }

  std::vector<ConfigProfileIssue> issues;
  if (!ConfigProfileCodec::decode(document, config, issues, "raw sidecar")) {
    error = issues.empty() ? QStringLiteral("Recorded setup could not be decoded")
                           : QString::fromStdString(issues.front().message);
    return false;
  }
  error.clear();
  return true;
}

bool validateRecordedBoard(const SystemConfig& config, QString& error) {
  const auto* capabilities = findDigitizerBoardCapabilities(config.digitizer.board_profile);
  if (capabilities != nullptr) {
    return true;
  }
  error = QStringLiteral("Recorded digitizer profile '%1' is not supported by this build. "
                         "The current setup was kept instead of substituting another board model.")
              .arg(QString::fromStdString(config.digitizer.board_profile));
  return false;
}

}  // namespace

RecordedSetupLoadResult loadRecordedSetup(const QString& raw_path) {
  RecordedSetupLoadResult result;
  const QFileInfo raw_info(raw_path);
  const QDir directory = raw_info.dir();
  const auto prefix = recordingPrefix(raw_path);
  const auto sidecar_path = directory.filePath(prefix + QStringLiteral(".raw.json"));
  QString setup_path = directory.filePath(prefix + QStringLiteral(".setup.yaml"));
  QJsonObject sidecar;

  QFile sidecar_file(sidecar_path);
  if (sidecar_file.exists()) {
    if (!sidecar_file.open(QIODevice::ReadOnly)) {
      result.warnings.append(
          QStringLiteral("RAW sidecar could not be opened: %1").arg(sidecar_path));
    } else {
      QJsonParseError parse_error;
      const auto document = QJsonDocument::fromJson(sidecar_file.readAll(), &parse_error);
      if (parse_error.error == QJsonParseError::NoError && document.isObject()) {
        sidecar = document.object();
        const auto setup_name = sidecar.value(QStringLiteral("setup_file")).toString();
        if (!setup_name.isEmpty()) {
          setup_path = directory.filePath(setup_name);
        }
      } else {
        result.warnings.append(
            QStringLiteral("RAW sidecar JSON is invalid: %1").arg(parse_error.errorString()));
      }
    }
  }

  QStringList load_failures;
  if (QFileInfo::exists(setup_path)) {
    const auto yaml = ConfigProfileCodec::loadLayered({fileSystemPath(setup_path)});
    if (yaml.ok()) {
      result.config = yaml.config;
      result.setup_source = setup_path;
    } else {
      const auto yaml_error = profileLoadError(yaml);
      load_failures.append(yaml_error);
      result.warnings.append(
          QStringLiteral("Recorded YAML is invalid; trying the RAW sidecar snapshot: %1")
              .arg(yaml_error));
    }
  } else {
    const auto missing_yaml =
        QStringLiteral("Recorded setup YAML was not found: %1").arg(setup_path);
    load_failures.append(missing_yaml);
    result.warnings.append(
        QStringLiteral("%1; trying the RAW sidecar snapshot").arg(missing_yaml));
  }

  if (result.setup_source.isEmpty() &&
      !sidecar.isEmpty() && sidecar.value(QStringLiteral("config_snapshot")).isObject()) {
    QString snapshot_error;
    if (decodeConfigSnapshot(sidecar.value(QStringLiteral("config_snapshot")).toObject(),
                             result.config, snapshot_error)) {
      result.setup_source = sidecar_path;
      result.used_json_fallback = true;
    } else {
      load_failures.append(QStringLiteral("RAW sidecar setup snapshot is invalid: %1")
                               .arg(snapshot_error));
    }
  }

  if (result.setup_source.isEmpty()) {
    if (sidecar.isEmpty()) {
      load_failures.append(QStringLiteral("No readable RAW sidecar setup snapshot was found: %1")
                               .arg(sidecar_path));
    } else if (!sidecar.value(QStringLiteral("config_snapshot")).isObject()) {
      load_failures.append(QStringLiteral("RAW sidecar does not contain a config_snapshot object: %1")
                               .arg(sidecar_path));
    }
    result.error = load_failures.join(QStringLiteral("\n"));
    return result;
  }

  if (!validateRecordedBoard(result.config, result.error)) {
    return result;
  }

  if (result.config.mcu.waveform_source == McuWaveformSource::LegacyXymFile) {
    const auto waveform_path = fileSystemPath(QString::fromStdString(result.config.mcu.waveform_file));
    if (waveform_path.is_relative()) {
      result.config.mcu.waveform_file =
          (fileSystemPath(directory.absolutePath()) / waveform_path).string();
    }
  }
  result.error.clear();
  return result;
}

QString normalizedReplayPath(const QString& path) {
  const QFileInfo info(path.trimmed());
  auto normalized = info.canonicalFilePath();
  if (normalized.isEmpty()) {
    normalized = info.absoluteFilePath();
  }
  return QDir::cleanPath(QDir::fromNativeSeparators(normalized));
}

bool replayPathChanged(const QString& candidate_path, const QString& loaded_path) {
  if (candidate_path.trimmed().isEmpty()) {
    return false;
  }
#ifdef Q_OS_WIN
  constexpr auto comparison = Qt::CaseInsensitive;
#else
  constexpr auto comparison = Qt::CaseSensitive;
#endif
  return normalizedReplayPath(candidate_path).compare(
             normalizedReplayPath(loaded_path), comparison) != 0;
}

}  // namespace fmcw
