#include "drivers/runtime_adapter_factory.h"

#include "drivers/alazar/alazar_digitizer.h"
#include "drivers/edfa/edfa_serial_controller.h"
#include "drivers/mcu/mcu_serial_controller.h"
#include "drivers/replay/replay_digitizer.h"
#include "drivers/simulator/fake_digitizer.h"

#include <memory>

namespace fmcw {

RuntimeAdapters createRuntimeAdapters(AcquisitionSource source) {
  RuntimeAdapters adapters;
  switch (source) {
    case AcquisitionSource::Alazar:
      adapters.digitizer = std::make_unique<AlazarDigitizer>();
      adapters.display_name = "AlazarTech hardware";
      break;
    case AcquisitionSource::Replay:
      adapters.digitizer = std::make_unique<ReplayDigitizer>();
      adapters.display_name = "Raw recording replay";
      break;
    case AcquisitionSource::Simulator:
      adapters.digitizer = std::make_unique<FakeDigitizer>();
      adapters.display_name = "Signal simulator";
      break;
  }
  // Optional hardware follows its own profile mode. It must not silently turn
  // into a simulator merely because the digitizer source is simulated/replayed.
  adapters.edfa = std::make_unique<EdfaSerialController>();
  adapters.mcu = std::make_unique<McuSerialController>();
  return adapters;
}

}  // namespace fmcw
