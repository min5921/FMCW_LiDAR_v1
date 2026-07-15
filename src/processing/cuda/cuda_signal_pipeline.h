#pragma once

#include "core/config_types.h"
#include "core/frame_types.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace fmcw {

class CudaSignalPipeline {
 public:
  CudaSignalPipeline();
  ~CudaSignalPipeline();

  CudaSignalPipeline(const CudaSignalPipeline&) = delete;
  CudaSignalPipeline& operator=(const CudaSignalPipeline&) = delete;

  static bool available();
  bool configure(const SystemConfig& config,
                 std::uint64_t processing_config_revision,
                 const std::vector<float>& up_window,
                 const std::vector<float>& down_window,
                 float up_window_sum,
                 float down_window_sum,
                 std::string& error);
  bool updateRuntimeConfig(const ProcessingConfig& config,
                           std::uint64_t processing_config_revision,
                           std::string& error);
  bool processBatch(const RawFrameBatch& raw_batch,
                    std::uint32_t selected_record_index,
                    std::vector<ProcessedFrame>& processed_batch,
                    std::string& error);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace fmcw
