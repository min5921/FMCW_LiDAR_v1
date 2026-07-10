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
