add_library(wperf_doctest INTERFACE)
target_include_directories(wperf_doctest SYSTEM INTERFACE third_party)

add_executable(wperf_tests test_app_logic.cpp)
target_link_libraries(wperf_tests PRIVATE wperf_doctest)
target_compile_options(wperf_tests PRIVATE /W4 /WX /permissive- /utf-8)

add_test(NAME wperf.unit COMMAND wperf_tests --no-colors)
set_tests_properties(wperf.unit PROPERTIES LABELS unit TIMEOUT 30)
