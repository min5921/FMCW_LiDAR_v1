add_executable(fmcw_core_tests
  core_contract_test.cpp
)

target_link_libraries(fmcw_core_tests PRIVATE fmcw_core)
target_compile_features(fmcw_core_tests PRIVATE cxx_std_17)

add_test(NAME fmcw_core_tests COMMAND fmcw_core_tests)

add_executable(fmcw_config_tests
  config_system_test.cpp
  # Historical policy prototypes, intentionally not linked into the application.
  ../src/core/config_manager.cpp
  ../src/core/operation_controller.cpp
)

target_link_libraries(fmcw_config_tests PRIVATE fmcw_core)
target_compile_features(fmcw_config_tests PRIVATE cxx_std_17)
target_compile_definitions(fmcw_config_tests PRIVATE FMCW_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}")

add_test(NAME fmcw_config_tests COMMAND fmcw_config_tests)

add_executable(fmcw_acquisition_tests
  acquisition_driver_test.cpp
)

target_link_libraries(fmcw_acquisition_tests PRIVATE fmcw_core)
target_compile_features(fmcw_acquisition_tests PRIVATE cxx_std_17)
target_compile_definitions(fmcw_acquisition_tests PRIVATE FMCW_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}")

add_test(NAME fmcw_acquisition_tests COMMAND fmcw_acquisition_tests)

add_executable(fmcw_edfa_concurrency_tests
  edfa_concurrency_test.cpp
)
target_link_libraries(fmcw_edfa_concurrency_tests PRIVATE fmcw_core)
target_compile_features(fmcw_edfa_concurrency_tests PRIVATE cxx_std_17)
add_test(NAME fmcw_edfa_concurrency_tests COMMAND fmcw_edfa_concurrency_tests)
set_tests_properties(fmcw_edfa_concurrency_tests PROPERTIES TIMEOUT 30)

add_executable(fmcw_storage_failure_tests storage_failure_test.cpp)
target_link_libraries(fmcw_storage_failure_tests PRIVATE fmcw_core)
target_compile_features(fmcw_storage_failure_tests PRIVATE cxx_std_17)
add_test(NAME fmcw_storage_failure_tests COMMAND fmcw_storage_failure_tests)
set_tests_properties(fmcw_storage_failure_tests PROPERTIES TIMEOUT 30)
add_executable(fmcw_processing_history_tests processing_history_test.cpp)
target_link_libraries(fmcw_processing_history_tests PRIVATE fmcw_core)
add_test(NAME fmcw_processing_history_tests COMMAND fmcw_processing_history_tests)
set_tests_properties(fmcw_processing_history_tests PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77)
add_executable(fmcw_async_replay_tests async_replay_test.cpp)
target_link_libraries(fmcw_async_replay_tests PRIVATE fmcw_core)
add_test(NAME fmcw_async_replay_tests COMMAND fmcw_async_replay_tests)
set_tests_properties(fmcw_async_replay_tests PROPERTIES TIMEOUT 15)

if(FMCW_BUILD_QT_APPS)
  find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui)
  add_executable(fmcw_point_cloud_ui_tests point_cloud_ui_test.cpp)
  target_link_libraries(fmcw_point_cloud_ui_tests PRIVATE fmcw_qt_common)
  add_test(NAME fmcw_point_cloud_ui_tests COMMAND fmcw_point_cloud_ui_tests)
  set_tests_properties(fmcw_point_cloud_ui_tests PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77)
  add_executable(fmcw_point_cloud_camera_tests point_cloud_camera_test.cpp)
  target_link_libraries(fmcw_point_cloud_camera_tests PRIVATE Qt6::Gui)
  target_include_directories(fmcw_point_cloud_camera_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
  target_compile_features(fmcw_point_cloud_camera_tests PRIVATE cxx_std_17)
  add_test(NAME fmcw_point_cloud_camera_tests COMMAND fmcw_point_cloud_camera_tests)
  add_executable(fmcw_point_cloud_render_tests point_cloud_render_test.cpp)
  target_link_libraries(fmcw_point_cloud_render_tests PRIVATE fmcw_qt_common)
  foreach(_renderer IN ITEMS gpu painter)
    foreach(_scale IN ITEMS 1 2)
      set(_render_test fmcw_point_cloud_${_renderer}_${_scale}x)
      if(_renderer STREQUAL "painter")
        add_test(NAME ${_render_test} COMMAND fmcw_point_cloud_render_tests --painter)
      else()
        add_test(NAME ${_render_test} COMMAND fmcw_point_cloud_render_tests)
      endif()
      set_tests_properties(${_render_test} PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77
        ENVIRONMENT "QT_SCALE_FACTOR=${_scale}" RUN_SERIAL TRUE)
    endforeach()
  endforeach()
  if(WIN32)
    get_target_property(_fmcw_qt_core Qt6::Core IMPORTED_LOCATION_RELEASE)
    get_filename_component(_fmcw_qt_bin "${_fmcw_qt_core}" DIRECTORY)
    string(REPLACE ";" "\\;" _fmcw_ui_test_path "${_fmcw_qt_bin};$ENV{PATH}")
    foreach(_qt_test IN ITEMS fmcw_point_cloud_ui_tests fmcw_point_cloud_camera_tests
        fmcw_point_cloud_gpu_1x fmcw_point_cloud_gpu_2x
        fmcw_point_cloud_painter_1x fmcw_point_cloud_painter_2x)
      set_property(TEST ${_qt_test} APPEND PROPERTY ENVIRONMENT
        "PATH=${_fmcw_ui_test_path}" "QT_PLUGIN_PATH=${_fmcw_qt_bin}/../plugins")
    endforeach()
  endif()
  add_executable(fmcw_runtime_tests runtime_orchestration_test.cpp)
  target_link_libraries(fmcw_runtime_tests PRIVATE fmcw_qt_common Qt6::Core)
  add_test(NAME fmcw_runtime_tests COMMAND fmcw_runtime_tests)
  set_tests_properties(fmcw_runtime_tests PROPERTIES TIMEOUT 45 SKIP_RETURN_CODE 77)
  add_executable(fmcw_replay_setup_tests
    replay_setup_loader_test.cpp
  )
  target_link_libraries(fmcw_replay_setup_tests PRIVATE fmcw_qt_common Qt6::Core)
  target_compile_features(fmcw_replay_setup_tests PRIVATE cxx_std_17)
  if(WIN32)
    add_custom_command(TARGET fmcw_replay_setup_tests POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:Qt6::Core>
        $<TARGET_FILE_DIR:fmcw_replay_setup_tests>
    )
  endif()
  add_test(NAME fmcw_replay_setup_tests COMMAND fmcw_replay_setup_tests)
endif()

add_executable(fmcw_phase4_tests
  phase4_pipeline_test.cpp
)

target_link_libraries(fmcw_phase4_tests PRIVATE fmcw_core)
target_compile_features(fmcw_phase4_tests PRIVATE cxx_std_17)

if(WIN32)
  foreach(_runtime_dll IN ITEMS "${FMCW_FFTW_RUNTIME_DLL}" "${FMCW_CUFFT_RUNTIME_DLL}")
    if(EXISTS "${_runtime_dll}")
      add_custom_command(TARGET fmcw_phase4_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${_runtime_dll}"
          $<TARGET_FILE_DIR:fmcw_phase4_tests>
      )
    endif()
  endforeach()
endif()

add_test(NAME fmcw_phase4_tests COMMAND fmcw_phase4_tests)

add_executable(fmcw_point_cloud_postprocessor_tests
  point_cloud_postprocessor_test.cpp
)

target_link_libraries(fmcw_point_cloud_postprocessor_tests PRIVATE fmcw_core)
target_compile_features(fmcw_point_cloud_postprocessor_tests PRIVATE cxx_std_17)

add_test(NAME fmcw_point_cloud_postprocessor_tests COMMAND fmcw_point_cloud_postprocessor_tests)

add_executable(fmcw_point_cloud_replay_tests
  point_cloud_replay_test.cpp
)

target_link_libraries(fmcw_point_cloud_replay_tests PRIVATE fmcw_core)
target_compile_features(fmcw_point_cloud_replay_tests PRIVATE cxx_std_17)

add_test(NAME fmcw_point_cloud_replay_tests COMMAND fmcw_point_cloud_replay_tests)

add_executable(fmcw_phase6_network_tests
  phase6_network_test.cpp
)

target_link_libraries(fmcw_phase6_network_tests PRIVATE fmcw_core)
target_compile_features(fmcw_phase6_network_tests PRIVATE cxx_std_17)

add_test(NAME fmcw_phase6_network_tests COMMAND fmcw_phase6_network_tests)

if(FMCW_HAS_FFTW)
  add_executable(fmcw_phase7_performance_tests
    phase7_performance_test.cpp
  )

  target_link_libraries(fmcw_phase7_performance_tests PRIVATE fmcw_core)
  target_compile_features(fmcw_phase7_performance_tests PRIVATE cxx_std_17)

  if(WIN32)
    foreach(_runtime_dll IN ITEMS "${FMCW_FFTW_RUNTIME_DLL}" "${FMCW_CUFFT_RUNTIME_DLL}")
      if(EXISTS "${_runtime_dll}")
        add_custom_command(TARGET fmcw_phase7_performance_tests POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_runtime_dll}"
            $<TARGET_FILE_DIR:fmcw_phase7_performance_tests>
        )
      endif()
    endforeach()
  endif()

  add_test(NAME fmcw_phase7_performance_tests COMMAND fmcw_phase7_performance_tests)
endif()

if(FMCW_HAS_CUDA_FFT)
  add_executable(fmcw_phase7_cuda_performance_tests
    phase7_cuda_performance_test.cpp
  )

  target_link_libraries(fmcw_phase7_cuda_performance_tests PRIVATE fmcw_core)
  target_compile_features(fmcw_phase7_cuda_performance_tests PRIVATE cxx_std_17)

  if(WIN32)
    foreach(_runtime_dll IN ITEMS "${FMCW_CUFFT_RUNTIME_DLL}")
      if(EXISTS "${_runtime_dll}")
        add_custom_command(TARGET fmcw_phase7_cuda_performance_tests POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_runtime_dll}"
            $<TARGET_FILE_DIR:fmcw_phase7_cuda_performance_tests>
        )
      endif()
    endforeach()
  endif()

  add_test(NAME fmcw_phase7_cuda_performance_tests COMMAND fmcw_phase7_cuda_performance_tests)
endif()

function(add_phase7_realtime_qualification target_name duration_seconds enforce_hard_pass)
  add_executable(${target_name}
    phase7_realtime_qualification_test.cpp
  )
  target_link_libraries(${target_name} PRIVATE fmcw_core)
  target_compile_features(${target_name} PRIVATE cxx_std_17)
  target_compile_definitions(${target_name} PRIVATE
    FMCW_QUALIFICATION_DURATION_SECONDS=${duration_seconds}
    FMCW_QUALIFICATION_ENFORCE_HARD_PASS=${enforce_hard_pass}
  )

  if(WIN32)
    foreach(_runtime_dll IN ITEMS "${FMCW_FFTW_RUNTIME_DLL}" "${FMCW_CUFFT_RUNTIME_DLL}")
      if(EXISTS "${_runtime_dll}")
        add_custom_command(TARGET ${target_name} POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_runtime_dll}"
            $<TARGET_FILE_DIR:${target_name}>
        )
      endif()
    endforeach()
  endif()
endfunction()

if(FMCW_HAS_FFTW OR FMCW_HAS_CUDA_FFT)
  add_phase7_realtime_qualification(fmcw_phase7_realtime_probe 2 0)
  if(FMCW_HAS_FFTW)
    add_test(NAME fmcw_realtime_fftw COMMAND fmcw_phase7_realtime_probe --backend=fftw)
  endif()
  if(FMCW_HAS_CUDA_FFT)
    add_test(NAME fmcw_realtime_cuda COMMAND fmcw_phase7_realtime_probe --backend=cuda)
  endif()

  # Fixed-duration release gate. It is intentionally not part of normal CTest because it
  # runs for ten minutes per selected backend and returns failure on any 5 ms miss.
  add_phase7_realtime_qualification(fmcw_phase7_realtime_acceptance 600 1)

  add_executable(fmcw_phase7_storage_tests
    phase7_storage_test.cpp
  )
  target_link_libraries(fmcw_phase7_storage_tests PRIVATE fmcw_core)
  target_compile_features(fmcw_phase7_storage_tests PRIVATE cxx_std_17)
  if(WIN32)
    foreach(_runtime_dll IN ITEMS "${FMCW_FFTW_RUNTIME_DLL}" "${FMCW_CUFFT_RUNTIME_DLL}")
      if(EXISTS "${_runtime_dll}")
        add_custom_command(TARGET fmcw_phase7_storage_tests POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_runtime_dll}"
            $<TARGET_FILE_DIR:fmcw_phase7_storage_tests>
        )
      endif()
    endforeach()
  endif()
  add_test(NAME fmcw_phase7_storage_tests COMMAND fmcw_phase7_storage_tests)
endif()

function(add_phase7_storage_performance target_name block_count realtime_pacing enforce_acceptance)
  add_executable(${target_name}
    phase7_storage_performance_test.cpp
  )
  target_link_libraries(${target_name} PRIVATE fmcw_core)
  target_compile_features(${target_name} PRIVATE cxx_std_17)
  target_compile_definitions(${target_name} PRIVATE
    FMCW_STORAGE_BLOCK_COUNT=${block_count}
    FMCW_STORAGE_REALTIME_PACING=${realtime_pacing}
    FMCW_STORAGE_ENFORCE_ACCEPTANCE=${enforce_acceptance}
  )
endfunction()

add_phase7_storage_performance(fmcw_phase7_storage_probe 32 0 0)

# 120,240 strict DMA blocks equal ten minutes at 200 kHz / 998 records per block.
# The executable preflights roughly 1.2 TB of free space and is not part of normal CTest.
add_phase7_storage_performance(fmcw_phase7_storage_acceptance 120240 1 1)

if(WIN32 AND FMCW_HAS_CENTERPOINT AND FMCW_CUDNN_RUNTIME_DLL)
  get_filename_component(_fmcw_test_cudnn_runtime_dir "${FMCW_CUDNN_RUNTIME_DLL}" DIRECTORY)
  get_property(_fmcw_tests DIRECTORY PROPERTY TESTS)
  string(REPLACE ";" "\\;" _fmcw_test_path "${_fmcw_test_cudnn_runtime_dir};$ENV{PATH}")
  set_tests_properties(${_fmcw_tests} PROPERTIES ENVIRONMENT "PATH=${_fmcw_test_path}")
endif()
