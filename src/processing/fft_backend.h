#pragma once

#include <complex>
#include <string>
#include <vector>

namespace fmcw {

enum class FftBackendKind {
  Cuda,
  Fftw,
};

struct FftPlan {
  std::size_t length = 0;
  std::size_t batch = 1;
};

class IFftBackend {
 public:
  virtual ~IFftBackend() = default;
  virtual std::string name() const = 0;
  virtual FftBackendKind kind() const = 0;
  virtual bool prepare(const FftPlan& plan, std::string& error) = 0;
  virtual bool execute(const std::vector<float>& input, std::vector<std::complex<float>>& output, std::string& error) = 0;
};

}  // namespace fmcw
