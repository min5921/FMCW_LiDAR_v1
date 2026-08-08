#pragma once

#include "core/config_types.h"

#include <QString>
#include <QStringList>

namespace fmcw {

struct RecordedSetupLoadResult {
  SystemConfig config;
  QString setup_source;
  QStringList warnings;
  QString error;
  bool used_json_fallback = false;

  bool ok() const { return error.isEmpty(); }
};

RecordedSetupLoadResult loadRecordedSetup(const QString& raw_path);
QString normalizedReplayPath(const QString& path);
bool replayPathChanged(const QString& candidate_path, const QString& loaded_path);

}  // namespace fmcw
