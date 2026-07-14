#pragma once

#include "core/config_types.h"
#include "core/device_interfaces.h"

#include <memory>
#include <string>

namespace fmcw {

struct RuntimeAdapters {
  std::unique_ptr<IDigitizer> digitizer;
  std::unique_ptr<IEdfaController> edfa;
  std::unique_ptr<IMcuController> mcu;
  std::string display_name;

  explicit operator bool() const {
    return digitizer != nullptr && edfa != nullptr && mcu != nullptr;
  }
};

RuntimeAdapters createRuntimeAdapters(AcquisitionSource source);

}  // namespace fmcw
