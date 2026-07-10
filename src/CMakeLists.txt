set(FMCW_CORE_SOURCES
  core/acquisition_session.cpp
  core/app_version.cpp
  core/config_document.cpp
  core/config_manager.cpp
  core/config_policy.cpp
  core/config_profile.cpp
  core/config_types.cpp
  core/config_validation.cpp
  core/logging.cpp
  core/operation_controller.cpp
  core/system_state.cpp
  drivers/alazar/alazar_digitizer.cpp
  drivers/edfa/edfa_protocol.cpp
  drivers/edfa/edfa_serial_controller.cpp
  drivers/mcu/mcu_protocol.cpp
  drivers/mcu/mcu_serial_controller.cpp
  drivers/serial/serial_transport.cpp
  drivers/simulator/fake_digitizer.cpp
  drivers/simulator/fake_edfa.cpp
  drivers/simulator/fake_mcu.cpp
)

add_library(fmcw_core STATIC ${FMCW_CORE_SOURCES})
target_include_directories(fmcw_core
  PUBLIC
    ${PROJECT_SOURCE_DIR}/src
)
target_compile_features(fmcw_core PUBLIC cxx_std_17)

if(MSVC)
  target_compile_options(fmcw_core PRIVATE /W4 /permissive-)
else()
  target_compile_options(fmcw_core PRIVATE -Wall -Wextra -Wpedantic)
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

if(FMCW_BUILD_QT_APPS)
  find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)
  qt_standard_project_setup()

  add_library(fmcw_qt_common STATIC
    apps/common/main_window.cpp
    apps/common/main_window.h
  )
  target_link_libraries(fmcw_qt_common
    PUBLIC
      fmcw_core
      Qt6::Core
      Qt6::Gui
      Qt6::Widgets
  )
  target_include_directories(fmcw_qt_common PUBLIC ${PROJECT_SOURCE_DIR}/src)

  if(MSVC)
    target_compile_options(fmcw_qt_common PRIVATE /W4 /permissive-)
  else()
    target_compile_options(fmcw_qt_common PRIVATE -Wall -Wextra -Wpedantic)
  endif()

  if(FMCW_TARGET_PLATFORM STREQUAL "WINDOWS")
    add_executable(fmcw_lidar_windows apps/windows/main.cpp)
    target_link_libraries(fmcw_lidar_windows PRIVATE fmcw_qt_common)
    target_compile_definitions(fmcw_lidar_windows PRIVATE FMCW_PLATFORM_NAME="Windows")
    install(TARGETS fmcw_lidar_windows RUNTIME DESTINATION bin)
  else()
    add_executable(fmcw_lidar_jetson apps/jetson/main.cpp)
    target_link_libraries(fmcw_lidar_jetson PRIVATE fmcw_qt_common)
    target_compile_definitions(fmcw_lidar_jetson PRIVATE FMCW_PLATFORM_NAME="Jetson")
    install(TARGETS fmcw_lidar_jetson RUNTIME DESTINATION bin)
  endif()
endif()
