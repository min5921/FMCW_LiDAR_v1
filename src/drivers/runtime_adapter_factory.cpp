#include "drivers/runtime_adapter_factory.h"

#include "drivers/alazar/alazar_digitizer.h"
#include "drivers/edfa/edfa_serial_controller.h"
#include "drivers/mcu/mcu_serial_controller.h"
#include "drivers/replay/replay_digitizer.h"
#include "drivers/simulator/fake_digitizer.h"
#include "drivers/simulator/fake_edfa.h"
#include "drivers/simulator/fake_mcu.h"

#include <memory>

namespace fmcw {

RuntimeAdapters createRuntimeAdapters(AcquisitionSource source) {
  RuntimeAdapters adapters;
  switch (source) {
    case AcquisitionSource::Alazar:
      adapters.digitizer = std::make_unique<AlazarDigitizer>();
      adapters.edfa = std::make_unique<EdfaSerialController>();
      adapters.mcu = std::make_unique<McuSerialController>();
      adapters.display_name = "Alazar ATS9371 hardware";
      break;
    case AcquisitionSource::Replay:
      adapters.digitizer = std::make_unique<ReplayDigitizer>();
      adapters.edfa = std::make_unique<FakeEdfaController>();
      adapters.mcu = std::make_unique<FakeMcuController>();
      adapters.display_name = "Raw recording replay";
      break;
    case AcquisitionSource::Simulator:
      adapters.digitizer = std::make_unique<FakeDigitizer>();
      adapters.edfa = std::make_unique<FakeEdfaController>();
      adapters.mcu = std::make_unique<FakeMcuController>();
      adapters.display_name = "Signal simulator";
      break;
  }
  return adapters;
}

}  // namespace fmcw
