#pragma once

#include "core/config_types.h"
#include "core/frame_types.h"
#include "processing/fft_backend.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace fmcw {

class SignalProcessor {
 public:
  explicit SignalProcessor(std::unique_ptr<IFftBackend> fft_backend);
  ~SignalProcessor();

  SignalProcessor(const SignalProcessor&) = delete;
  SignalProcessor& operator=(const SignalProcessor&) = delete;

  bool configure(const SystemConfig& config, std::uint64_t processing_config_revision, std::string& error);
  bool initializeWorkerThread(std::string& error);
  bool updateRuntimeConfig(const ProcessingConfig& config, std::uint64_t processing_config_revision,
                           std::string& error);
  bool process(const RawFrame& raw, ProcessedFrame& processed, std::string& error);
  bool processBatch(const RawFrameBatch& raw_batch, std::uint32_t selected_record_index,
                    std::vector<ProcessedFrame>& processed_batch, std::string& error);
  bool supportsAsyncBatchProcessing() const;
  std::size_t asyncBatchCapacity() const;
  std::size_t inFlightBatchCount() const;
  bool submitBatch(RawFrameBatchPtr raw_batch, std::uint32_t selected_record_index,
                   std::string& error);
  bool releaseCompletedBatchInputs(bool wait_for_oldest, std::string& error);
  bool collectNextBatch(bool wait, RawFrameBatchPtr& raw_batch,
                        std::vector<ProcessedFrame>& processed_batch,
                        bool& collected, std::string& error);

  std::string backendName() const;
  std::uint64_t processingConfigRevision() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace fmcw
