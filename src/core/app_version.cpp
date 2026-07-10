#include "core/app_version.h"

#include <sstream>

namespace fmcw {

AppVersion currentVersion() {
  return {};
}

std::string versionString() {
  const auto version = currentVersion();
  std::ostringstream text;
  text << version.major << '.' << version.minor << '.' << version.patch;
  return text.str();
}

}  // namespace fmcw
