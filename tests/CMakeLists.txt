add_executable(fmcw_core_tests
  core_contract_test.cpp
)

target_link_libraries(fmcw_core_tests PRIVATE fmcw_core)
target_compile_features(fmcw_core_tests PRIVATE cxx_std_17)

add_test(NAME fmcw_core_tests COMMAND fmcw_core_tests)
