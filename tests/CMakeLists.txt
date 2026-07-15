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

add_test(NAME fmcw_acquisition_tests COMMAND fmcw_acquisition_tests)

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
