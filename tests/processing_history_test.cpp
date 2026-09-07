#include "core/config_profile.h"
#include "drivers/simulator/fake_digitizer.h"
#include "drivers/replay/replay_digitizer.h"
#include "processing/processing_service.h"
#include "processing/fft_backends.h"
#include "storage/async_storage_service.h"
#include <chrono>
#include <cmath>
#include <iostream>
#include <fstream>

using namespace fmcw;
namespace {
int failures=0;
void expect(bool value, const char* message) { if (!value) { ++failures; std::cerr << message << '\n'; } }
void run(FftBackendKind kind) {
  auto c = SystemConfig{};
  c.processing.fft_backend=kind;
  c.digitizer.records_per_buffer=c.digitizer.a_scan_count=c.scan.x_pixel_count=4;
  c.digitizer.b_scan_count=c.scan.y_line_count=2;
  c.processing.peak_threshold_db=-100;
  const auto dir = std::filesystem::temp_directory_path() / ("fmcw_history_" +
      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(dir);
  std::string error;
  WriterOpenOptions options;
  options.session_directory=dir; options.file_stem="test";
  options.session.config_snapshot_yaml=ConfigProfileCodec::toYaml(c);
  options.session.config_snapshot_json=ConfigProfileCodec::toJsonSnapshot(c);
  options.raw_enabled=true; options.processed_enabled=false; options.queue_capacity=32;
  options.preallocate_raw_parts=false; options.minimum_free_space_bytes=0;
  options.raw_stream.format_version=kRawFrameBatchFormatVersion;
  options.raw_stream.record_length=c.digitizer.sample_point;
  options.raw_stream.sample_rate_hz=c.digitizer.sample_rate_hz;
  options.raw_stream.records_per_buffer=4;
  AsyncStorageService storage;
  expect(storage.start(options,error), "History writer starts");
  FakeDigitizer source;
  expect(source.configure(c,error)&&source.connect(error)&&source.start(error), "History source starts");
  ProcessingService live(createFftBackend(kind));
  std::vector<ProcessedFrame> expected;
  live.setProcessedFrameCallback([&](ProcessedFramePtr frame) { expected.push_back(*frame); });
  live.setProcessingConfigCallback([&](ProcessingConfigEvent event) {
    std::string event_error;
    expect(storage.enqueueProcessingEvent(std::move(event),event_error)==EnqueueResult::Accepted, "Applied config queued");
  });
  expect(live.configure(c,10,error)&&live.start(error), "History live processing starts");
  for (int i=0;i<4;++i) {
    if (i==1) { auto updated=c.processing; updated.peak_threshold_db=0;
      expect(live.updateRuntimeConfig(updated,11,error), "Update requested mid-frame"); }
    MutableRawFrameBatchPtr batch;
    expect(source.waitForBatch(batch,std::chrono::milliseconds(100),error)==FrameWaitResult::FrameReady, "Live batch read");
    expect(storage.enqueueRawBatch(batch,error)==EnqueueResult::Accepted, "Live raw queued");
    expect(live.enqueueBatch(batch,error)==ProcessingEnqueueResult::Accepted, "Live processing queued");
    expect(live.waitForProcessedBatches(i+1,std::chrono::seconds(2),error), "Live batch completes");
  }
  source.stop(error); live.requestStop("done"); expect(live.waitUntilStopped(error), "Live drains");
  storage.requestStop("done"); expect(storage.waitUntilStopped(error), "History recording finalizes");
  std::vector<std::shared_ptr<const ProcessingConfigEvent>> events;
  const auto raw=dir/"test.raw.0000.bin";
  expect(readProcessingHistory(raw,events,error), "History loads");
  expect(events.size()==2 && events[0]->first_source_frame_id==1 &&
      events[1]->first_source_frame_id==9 && events[1]->revision==11, "History records effective raster boundary, not request time");
  c.runtime.acquisition_source=AcquisitionSource::Replay; c.runtime.replay_file=raw.string();
  ReplayDigitizer replay;
  expect(replay.configure(c,error)&&replay.connect(error)&&replay.start(error), "Recorded replay connects");
  ProcessingService processing(createFftBackend(kind));
  std::vector<ProcessedFrame> actual;
  processing.setProcessedFrameCallback([&](ProcessedFramePtr frame) { actual.push_back(*frame); });
  expect(processing.configure(c,99,error)&&processing.start(error), "Replay processor starts");
  for(int i=0;i<4;++i) {
    MutableRawFrameBatchPtr batch;
    expect(replay.waitForBatch(batch,std::chrono::milliseconds(100),error)==FrameWaitResult::FrameReady, "Replay reads batch");
    expect(batch && batch->replay_processing, "Replay carries recorded config provenance");
    expect(processing.enqueueBatch(batch,error)==ProcessingEnqueueResult::Accepted, "Replay submits batch");
  }
  processing.requestStop("done"); expect(processing.waitUntilStopped(error), "Replay drains"); replay.stop(error);
  replay.disconnect();
  expect(actual.size()==16 && actual.size()==expected.size(), "All records replayed");
  expect(expected.size()==16 && expected.front().point.valid && !expected.back().point.valid,
         "Threshold change changes valid measurements, not only revision numbers");
  const auto equal=[](float a,float b) { return (std::isnan(a)&&std::isnan(b)) || std::abs(a-b)<1e-4F; };
  for(std::size_t i=0;i<std::min(actual.size(),expected.size());++i) {
    const auto& a=actual[i]; const auto& b=expected[i];
    expect(a.processing_config_revision==b.processing_config_revision && a.point.valid==b.point.valid &&
      equal(a.point.x,b.point.x)&&equal(a.point.y,b.point.y)&&equal(a.point.z,b.point.z)&&
      equal(a.point.intensity,b.point.intensity)&&equal(a.point.velocity,b.point.velocity), "Recorded revision and XYZIV parity");
  }
  c.runtime.replay_loop=true;
  expect(replay.configure(c,error)&&replay.connect(error)&&replay.start(error), "Loop replay starts");
  for (int i=0;i<5;++i) {
    MutableRawFrameBatchPtr batch;
    expect(replay.waitForBatch(batch,std::chrono::milliseconds(100),error)==FrameWaitResult::FrameReady,
           "Loop replay reads across EOF");
    if (i==4) expect(batch && batch->replay_processing && batch->replay_processing->revision==10,
                     "Loop restores initial recorded revision");
  }
  replay.stop(error); replay.disconnect();
  c.runtime.replay_processing_history=false;
  expect(replay.configure(c,error)&&replay.connect(error)&&replay.start(error), "Explicit replay override starts");
  MutableRawFrameBatchPtr overridden;
  expect(replay.waitForBatch(overridden,std::chrono::milliseconds(100),error)==FrameWaitResult::FrameReady &&
      overridden && !overridden->replay_processing, "Explicit override does not attach original processing settings");
  replay.stop(error); replay.disconnect();
  std::filesystem::rename(dir/"test.processing", dir/"saved.processing");
  expect(readProcessingHistory(raw,events,error) && events.empty(), "Old recording without history remains supported");
  std::filesystem::rename(dir/"saved.processing", dir/"test.processing");
  { std::ofstream pending(dir/"test.processing"/"incomplete.pending"); pending << "partial"; }
  expect(!readProcessingHistory(raw,events,error), "Incomplete config history cannot silently reproduce wrong results");
  std::error_code cleanup_error;
  std::filesystem::remove_all(dir, cleanup_error);
  expect(!cleanup_error, "Temporary history files can be removed after closing replay");
}
}
int main() {
  if (!FftwBackend::available() && !CudaFftBackend::available()) return 77;
  if(FftwBackend::available()) run(FftBackendKind::Fftw);
  if(CudaFftBackend::available()) run(FftBackendKind::Cuda);
  return failures?1:0;
}
