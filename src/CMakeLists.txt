set(FMCW_HAS_CUDA_FFT OFF)
if(FMCW_WITH_CUDA)
  include(CheckLanguage)
  check_language(CUDA)
  if(CMAKE_CUDA_COMPILER)
    enable_language(CUDA)
    find_package(CUDAToolkit QUIET)
    if(CUDAToolkit_FOUND)
      set(FMCW_HAS_CUDA_FFT ON)
    endif()
  endif()
endif()

set(FMCW_CORE_SOURCES
  core/acquisition_session.cpp
  core/app_version.cpp
  core/config_document.cpp
  core/config_manager.cpp
  core/config_policy.cpp
  core/config_profile.cpp
  core/config_types.cpp
  core/config_validation.cpp
  core/continuous_acquisition_worker.cpp
  core/digitizer_capabilities.cpp
  core/logging.cpp
  core/operation_controller.cpp
  core/raw_frame_batch_pool.cpp
  core/system_state.cpp
  drivers/alazar/alazar_digitizer.cpp
  drivers/alazar/alazar_sample_conversion.cpp
  drivers/edfa/edfa_protocol.cpp
  drivers/edfa/edfa_serial_controller.cpp
  drivers/mcu/mcu_protocol.cpp
  drivers/mcu/mcu_serial_controller.cpp
  drivers/replay/replay_digitizer.cpp
  drivers/runtime_adapter_factory.cpp
  drivers/serial/serial_transport.cpp
  drivers/simulator/fake_digitizer.cpp
  drivers/simulator/fake_edfa.cpp
  drivers/simulator/fake_mcu.cpp
  network/udp_point_protocol.cpp
  network/udp_sender_service.cpp
  processing/cpu/fftw_backend.cpp
  processing/fft_backends.cpp
  processing/processing_service.cpp
  processing/processing_snapshots.cpp
  processing/signal_processor.cpp
  storage/async_storage_service.cpp
  storage/binary_storage.cpp
)

if(FMCW_HAS_CUDA_FFT)
  list(APPEND FMCW_CORE_SOURCES
    processing/cuda/cuda_fft_backend.cu
    processing/cuda/cuda_signal_pipeline.cu
  )
else()
  list(APPEND FMCW_CORE_SOURCES processing/cuda/cuda_signal_pipeline_stub.cpp)
endif()

add_library(fmcw_core STATIC ${FMCW_CORE_SOURCES})
target_include_directories(fmcw_core
  PUBLIC
    ${PROJECT_SOURCE_DIR}/src
)
target_compile_features(fmcw_core PUBLIC cxx_std_17)

find_package(OpenMP QUIET COMPONENTS CXX)
if(OpenMP_CXX_FOUND)
  target_link_libraries(fmcw_core PRIVATE OpenMP::OpenMP_CXX)
  target_compile_definitions(fmcw_core PRIVATE FMCW_HAS_OPENMP=1)
  message(STATUS "OpenMP batch preprocessing: enabled")
else()
  target_compile_definitions(fmcw_core PRIVATE FMCW_HAS_OPENMP=0)
  message(STATUS "OpenMP batch preprocessing: disabled")
endif()

if(WIN32)
  target_link_libraries(fmcw_core PRIVATE ws2_32)
endif()

if(MSVC)
  target_compile_options(fmcw_core PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:/W4>
    $<$<COMPILE_LANGUAGE:CXX>:/permissive->
    $<$<COMPILE_LANGUAGE:CXX>:/wd4819>
    $<$<COMPILE_LANGUAGE:CUDA>:-Xcompiler=/wd4819>
  )
else()
  target_compile_options(fmcw_core PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-Wall>
    $<$<COMPILE_LANGUAGE:CXX>:-Wextra>
    $<$<COMPILE_LANGUAGE:CXX>:-Wpedantic>
  )
endif()

if(FMCW_TARGET_PLATFORM STREQUAL "WINDOWS")
  target_compile_definitions(fmcw_core PUBLIC FMCW_TARGET_WINDOWS=1)
else()
  target_compile_definitions(fmcw_core PUBLIC FMCW_TARGET_JETSON=1)
endif()

set(FMCW_HAS_ALAZAR_SDK OFF)
if(FMCW_WITH_ALAZAR)
  find_path(ALAZAR_SDK_INCLUDE_DIR
    NAMES AlazarApi.h AlazarCmd.h AlazarError.h
    HINTS "${ALAZAR_SDK_ROOT}"
    PATH_SUFFIXES include Samples_C/Include
  )
  if(FMCW_TARGET_PLATFORM STREQUAL "WINDOWS")
    find_library(ALAZAR_SDK_LIBRARY
      NAMES ATSApi
      HINTS "${ALAZAR_SDK_ROOT}"
      PATH_SUFFIXES lib lib/x64 Samples_C/Library Samples_C/Library/x64
    )
  else()
    find_library(ALAZAR_SDK_LIBRARY
      NAMES ATSApi
      HINTS "${ALAZAR_SDK_ROOT}" /usr/local/AlazarTech
      PATH_SUFFIXES lib lib64
    )
  endif()
  if(ALAZAR_SDK_INCLUDE_DIR AND ALAZAR_SDK_LIBRARY)
    set(FMCW_HAS_ALAZAR_SDK ON)
    target_include_directories(fmcw_core PRIVATE "${ALAZAR_SDK_INCLUDE_DIR}")
    target_link_libraries(fmcw_core PRIVATE "${ALAZAR_SDK_LIBRARY}")
  endif()
endif()

if(FMCW_HAS_ALAZAR_SDK)
  target_compile_definitions(fmcw_core PRIVATE FMCW_HAS_ALAZAR_SDK=1)
  message(STATUS "AlazarTech ATS-SDK adapter: enabled")
else()
  target_compile_definitions(fmcw_core PRIVATE FMCW_HAS_ALAZAR_SDK=0)
  message(STATUS "AlazarTech ATS-SDK adapter: disabled (set ALAZAR_SDK_ROOT to enable)")
endif()

set(FMCW_HAS_FFTW OFF)
if(FMCW_WITH_FFTW)
  if(FFTW_ROOT)
    find_package(FFTW3f CONFIG QUIET
      PATHS "${FFTW_ROOT}/share/fftw3f" "${FFTW_ROOT}/lib/cmake/fftw3f"
      NO_DEFAULT_PATH
    )
  else()
    find_package(FFTW3f CONFIG QUIET)
  endif()
  if(TARGET FFTW3::fftw3f)
    set(FMCW_HAS_FFTW ON)
    target_link_libraries(fmcw_core PRIVATE FFTW3::fftw3f)
  else()
    find_path(FFTW_INCLUDE_DIR NAMES fftw3.h HINTS "${FFTW_ROOT}" PATH_SUFFIXES include)
    find_library(FFTW_FLOAT_LIBRARY NAMES fftw3f libfftw3f-3 HINTS "${FFTW_ROOT}" PATH_SUFFIXES lib lib64)
    if(FFTW_INCLUDE_DIR AND FFTW_FLOAT_LIBRARY)
      set(FMCW_HAS_FFTW ON)
      target_include_directories(fmcw_core PRIVATE "${FFTW_INCLUDE_DIR}")
      target_link_libraries(fmcw_core PRIVATE "${FFTW_FLOAT_LIBRARY}")
    endif()
  endif()
endif()

if(FMCW_HAS_FFTW)
  target_compile_definitions(fmcw_core PRIVATE FMCW_HAS_FFTW=1)
  if(WIN32)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
      find_file(FMCW_FFTW_RUNTIME_DLL NAMES fftw3f.dll
        HINTS "${FFTW_ROOT}/debug/bin" "${FFTW_ROOT}/bin")
    else()
      find_file(FMCW_FFTW_RUNTIME_DLL NAMES fftw3f.dll
        HINTS "${FFTW_ROOT}/bin" "${FFTW_ROOT}/debug/bin")
    endif()
    set(FMCW_FFTW_RUNTIME_DLL "${FMCW_FFTW_RUNTIME_DLL}" CACHE INTERNAL "FFTW runtime DLL")
  endif()
  message(STATUS "FFTW CPU backend: enabled")
else()
  target_compile_definitions(fmcw_core PRIVATE FMCW_HAS_FFTW=0)
  unset(FMCW_FFTW_RUNTIME_DLL CACHE)
  message(STATUS "FFTW CPU backend: disabled (set FFTW_ROOT to enable)")
endif()

if(FMCW_HAS_CUDA_FFT)
  target_link_libraries(fmcw_core PRIVATE CUDA::cudart CUDA::cufft)
  set_target_properties(fmcw_core PROPERTIES
    CUDA_STANDARD 17
    CUDA_STANDARD_REQUIRED ON
    CUDA_ARCHITECTURES "${FMCW_CUDA_ARCHITECTURES}"
  )
  if(WIN32)
    find_file(FMCW_CUFFT_RUNTIME_DLL NAMES cufft64_13.dll cufft64_12.dll cufft64_11.dll
      HINTS "${CUDAToolkit_BIN_DIR}" "${CUDAToolkit_BIN_DIR}/x64")
    set(FMCW_CUFFT_RUNTIME_DLL "${FMCW_CUFFT_RUNTIME_DLL}" CACHE INTERNAL "cuFFT runtime DLL")
  endif()
  target_compile_definitions(fmcw_core PRIVATE FMCW_HAS_CUDA_FFT=1)
  message(STATUS "CUDA cuFFT backend: enabled for architectures ${FMCW_CUDA_ARCHITECTURES}")
else()
  target_compile_definitions(fmcw_core PRIVATE FMCW_HAS_CUDA_FFT=0)
  unset(FMCW_CUFFT_RUNTIME_DLL CACHE)
  message(STATUS "CUDA cuFFT backend: disabled")
endif()

if(FMCW_BUILD_QT_APPS)
  find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets)
  qt_standard_project_setup()

  add_library(fmcw_qt_common STATIC
    apps/common/application_controller.cpp
    apps/common/application_controller.h
    apps/common/main_window.cpp
    apps/common/main_window.h
    apps/common/point_cloud_widget.cpp
    apps/common/point_cloud_widget.h
    apps/common/plot_widgets.cpp
    apps/common/plot_widgets.h
  )
  target_link_libraries(fmcw_qt_common
    PUBLIC
      fmcw_core
      Qt6::Core
      Qt6::Gui
      Qt6::OpenGL
      Qt6::OpenGLWidgets
      Qt6::Widgets
  )
  target_include_directories(fmcw_qt_common PUBLIC ${PROJECT_SOURCE_DIR}/src)

  if(MSVC)
    target_compile_options(fmcw_qt_common PRIVATE /W4 /permissive-)
  else()
    target_compile_options(fmcw_qt_common PRIVATE -Wall -Wextra -Wpedantic)
  endif()

  if(FMCW_TARGET_PLATFORM STREQUAL "WINDOWS")
    add_executable(fmcw_lidar_windows WIN32 apps/windows/main.cpp)
    target_link_libraries(fmcw_lidar_windows PRIVATE fmcw_qt_common)
    target_compile_definitions(fmcw_lidar_windows PRIVATE FMCW_PLATFORM_NAME="Windows")
    foreach(_runtime_dll IN ITEMS "${FMCW_FFTW_RUNTIME_DLL}" "${FMCW_CUFFT_RUNTIME_DLL}")
      if(EXISTS "${_runtime_dll}")
        add_custom_command(TARGET fmcw_lidar_windows POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_runtime_dll}"
            $<TARGET_FILE_DIR:fmcw_lidar_windows>
        )
      endif()
    endforeach()
    install(TARGETS fmcw_lidar_windows RUNTIME DESTINATION bin)
  else()
    add_executable(fmcw_lidar_jetson apps/jetson/main.cpp)
    target_link_libraries(fmcw_lidar_jetson PRIVATE fmcw_qt_common)
    target_compile_definitions(fmcw_lidar_jetson PRIVATE FMCW_PLATFORM_NAME="Jetson")
    install(TARGETS fmcw_lidar_jetson RUNTIME DESTINATION bin)
  endif()
endif()
