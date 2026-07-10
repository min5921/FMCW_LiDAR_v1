#include "core/device_interfaces.h"
#include "core/frame_types.h"
#include "core/system_state.h"
#include "processing/fft_backend.h"
#include "storage/writer_interfaces.h"

#include <iostream>
#include <string>

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

void testSegmentRange() {
  const fmcw::SegmentRange segment{100, 300};
  expect(segment.length() == 200, "segment length uses a half-open interval");
  expect(segment.validFor(300), "segment end may equal record length");
  expect(!fmcw::SegmentRange{300, 300}.validFor(300), "empty segment is invalid");
  expect(!fmcw::SegmentRange{200, 100}.validFor(300), "reversed segment is invalid");
  expect(!fmcw::SegmentRange{100, 301}.validFor(300), "segment outside record is invalid");
}

void testStateMachineHappyPath() {
  fmcw::StateMachine state;
  expect(state.state() == fmcw::OperationState::Disconnected, "initial state is Disconnected");
  expect(state.transitionTo(fmcw::OperationState::Connected), "connect transition");
  expect(state.transitionTo(fmcw::OperationState::Configured), "configure transition");
  expect(!state.canStart(), "Configured is not ready to start");
  expect(state.transitionTo(fmcw::OperationState::Ready), "ready transition");
  expect(state.canStart(), "Ready can start");
  expect(state.transitionTo(fmcw::OperationState::Acquiring), "acquisition transition");
  expect(state.transitionTo(fmcw::OperationState::Recording), "recording transition");
  expect(state.canStop(), "Recording can stop");
  expect(state.transitionTo(fmcw::OperationState::Stopping), "stopping transition");
  expect(state.transitionTo(fmcw::OperationState::Ready), "clean stop returns to Ready");
}

void testStateMachineRejectsUnsafeTransitions() {
  fmcw::StateMachine state;
  expect(!state.transitionTo(fmcw::OperationState::Recording), "Disconnected cannot enter Recording");
  expect(state.state() == fmcw::OperationState::Disconnected, "rejected transition preserves state");
  expect(!state.lastError().empty(), "rejected transition reports a reason");

  state.fail("digitizer timeout");
  expect(state.state() == fmcw::OperationState::Error, "failure enters Error");
  expect(!state.transitionTo(fmcw::OperationState::Acquiring), "Error cannot resume acquisition directly");
  state.reset();
  expect(state.state() == fmcw::OperationState::Disconnected, "reset returns to Disconnected");
  expect(state.lastError().empty(), "reset clears the error");
}

}  // namespace

int main() {
  testSegmentRange();
  testStateMachineHappyPath();
  testStateMachineRejectsUnsafeTransitions();

  if (failures == 0) {
    std::cout << "All core contract tests passed.\n";
  }
  return failures == 0 ? 0 : 1;
}
