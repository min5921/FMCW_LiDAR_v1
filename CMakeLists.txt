cmake_minimum_required(VERSION 3.24)

project(FMCW_LiDAR_v1
  VERSION 0.1.0
  DESCRIPTION "FMCW LiDAR control, acquisition, processing, and visualization"
  LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(FMCW_BUILD_QT_APPS "Build Qt desktop applications" ON)
option(FMCW_BUILD_TESTS "Build core contract tests" ON)
option(FMCW_WITH_FFTW "Enable FFTW CPU backend when fftw3 is available" ON)
option(FMCW_WITH_CUDA "Enable CUDA/cuFFT backend when CUDA is available" ON)
option(FMCW_WITH_ALAZAR "Enable AlazarTech ATS-SDK adapter when the SDK is available" ON)

set(FMCW_TARGET_PLATFORM "AUTO" CACHE STRING "Target platform: AUTO, WINDOWS, or JETSON")
set_property(CACHE FMCW_TARGET_PLATFORM PROPERTY STRINGS AUTO WINDOWS JETSON)
set(ALAZAR_SDK_ROOT "" CACHE PATH "AlazarTech SDK root directory")
set(FFTW_ROOT "" CACHE PATH "FFTW installation root directory")

string(TOUPPER "${FMCW_TARGET_PLATFORM}" FMCW_TARGET_PLATFORM)
if(FMCW_TARGET_PLATFORM STREQUAL "AUTO")
  if(WIN32)
    set(FMCW_TARGET_PLATFORM "WINDOWS")
  elseif(UNIX AND NOT APPLE)
    set(FMCW_TARGET_PLATFORM "JETSON")
  else()
    message(FATAL_ERROR "AUTO platform detection supports Windows and Jetson/Linux only.")
  endif()
endif()

if(NOT FMCW_TARGET_PLATFORM MATCHES "^(WINDOWS|JETSON)$")
  message(FATAL_ERROR "FMCW_TARGET_PLATFORM must be AUTO, WINDOWS, or JETSON.")
endif()

if(FMCW_TARGET_PLATFORM STREQUAL "WINDOWS" AND NOT WIN32)
  message(FATAL_ERROR "The WINDOWS target must be configured on Windows.")
endif()

if(FMCW_TARGET_PLATFORM STREQUAL "JETSON" AND NOT (UNIX AND NOT APPLE))
  message(FATAL_ERROR "The JETSON target must be configured on Jetson/Linux.")
endif()

message(STATUS "FMCW target platform: ${FMCW_TARGET_PLATFORM}")

add_subdirectory(src)

if(FMCW_BUILD_TESTS)
  include(CTest)
  enable_testing()
  add_subdirectory(tests)
endif()
