#pragma once

#include <cstdlib>

namespace fmcw {

inline void configureEagerCudaModuleLoading() {
  static const bool configured = [] {
#if defined(_WIN32)
    return _putenv_s("CUDA_MODULE_LOADING", "EAGER") == 0;
#else
    return setenv("CUDA_MODULE_LOADING", "EAGER", 1) == 0;
#endif
  }();
  static_cast<void>(configured);
}

}  // namespace fmcw
