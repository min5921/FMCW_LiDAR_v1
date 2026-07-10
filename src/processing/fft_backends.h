#pragma once

#include "processing/fft_backend.h"

#include <memory>

namespace fmcw {

class FftwBackend final : public IFftBackend {
 public:
  FftwBackend();
  ~FftwBackend() override;

  FftwBackend(const FftwBackend&) = delete;
  FftwBackend& operator=(const FftwBackend&) = delete;

  static bool available();
  std::string name() const override;
  FftBackendKind kind() const override;
  bool prepare(const FftPlan& plan, std::string& error) override;
  bool execute(const std::vector<float>& input, std::vector<std::complex<float>>& output,
               std::string& error) override;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

class CudaFftBackend final : public IFftBackend {
 public:
  CudaFftBackend();
  ~CudaFftBackend() override;

  CudaFftBackend(const CudaFftBackend&) = delete;
  CudaFftBackend& operator=(const CudaFftBackend&) = delete;

  static bool available();
  std::string name() const override;
  FftBackendKind kind() const override;
  bool prepare(const FftPlan& plan, std::string& error) override;
  bool execute(const std::vector<float>& input, std::vector<std::complex<float>>& output,
               std::string& error) override;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

std::unique_ptr<IFftBackend> createFftBackend(FftBackendKind kind);

}  // namespace fmcw
