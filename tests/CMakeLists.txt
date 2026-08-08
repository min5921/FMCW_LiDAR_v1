add_executable(fmcw_core_tests
  core_contract_test.cpp
)

target_link_libraries(fmcw_core_tests PRIVATE fmcw_core)
target_compile_features(fmcw_core_tests PRIVATE cxx_std_17)

add_test(NAME fmcw_core_tests COMMAND fmcw_core_tests)

add_executable(fmcw_config_tests
  config_system_test.cpp
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

if(FMCW_BUILD_QT_APPS)
  find_package(Qt6 6.2 REQUIRED COMPONENTS Core)
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

if(FMCW_HAS_FFTW)
  add_phase7_realtime_qualification(fmcw_phase7_realtime_probe 2 0)
  add_test(NAME fmcw_phase7_realtime_probe COMMAND fmcw_phase7_realtime_probe)

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
