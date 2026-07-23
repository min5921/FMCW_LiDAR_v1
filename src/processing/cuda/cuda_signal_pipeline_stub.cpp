#include "processing/cuda/cuda_signal_pipeline.h"

namespace fmcw {

struct CudaSignalPipeline::Impl {};

CudaSignalPipeline::CudaSignalPipeline() : impl_(std::make_unique<Impl>()) {}
CudaSignalPipeline::~CudaSignalPipeline() = default;

bool CudaSignalPipeline::available() { return false; }

std::size_t CudaSignalPipeline::capacity() const { return 0U; }

std::size_t CudaSignalPipeline::inFlightCount() const { return 0U; }

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

bool CudaSignalPipeline::submitBatch(RawFrameBatchPtr,
                                     std::uint32_t,
                                     std::string& error) {
  error = "CUDA signal pipeline was not compiled";
  return false;
}

bool CudaSignalPipeline::releaseCompletedInputs(bool, std::string& error) {
  error = "CUDA signal pipeline was not compiled";
  return false;
}

bool CudaSignalPipeline::collectNext(
    bool,
    RawFrameBatchPtr& raw_batch,
    std::vector<ProcessedFrame>& processed_batch,
    bool& collected,
    std::string& error) {
  raw_batch.reset();
  processed_batch.clear();
  collected = false;
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
