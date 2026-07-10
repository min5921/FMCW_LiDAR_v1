#pragma once

#include <string>

namespace fmcw {

struct AppVersion {
  int major = 0;
  int minor = 1;
  int patch = 0;
};

AppVersion currentVersion();
std::string versionString();

}  // namespace fmcw
