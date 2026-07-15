#include "processing/cuda/cuda_signal_pipeline.h"

namespace fmcw {

struct CudaSignalPipeline::Impl {};

CudaSignalPipeline::CudaSignalPipeline() : impl_(std::make_unique<Impl>()) {}
CudaSignalPipeline::~CudaSignalPipeline() = default;

bool CudaSignalPipeline::available() { return false; }

bool CudaSignalPipeline::configure(const SystemConfig&,
                                   std::uint64_t,
                                   const std::vector<float>&,
                                   const std::vector<float>&,
                                   float,
                                   float,
                                   std::string& error) {
  error = "CUDA signal pipeline was not compiled";
  return false;
}

bool CudaSignalPipeline::updateRuntimeConfig(const ProcessingConfig&,
                                             std::uint64_t,
                                             std::string& error) {
  error = "CUDA signal pipeline was not compiled";
  return false;
}

bool CudaSignalPipeline::processBatch(const RawFrameBatch&,
                                      std::uint32_t,
                                      std::vector<ProcessedFrame>& processed_batch,
                                      std::string& error) {
  processed_batch.clear();
  error = "CUDA signal pipeline was not compiled";
  return false;
}

}  // namespace fmcw
