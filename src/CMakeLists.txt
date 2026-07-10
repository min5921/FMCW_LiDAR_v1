set(FMCW_CORE_SOURCES
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
