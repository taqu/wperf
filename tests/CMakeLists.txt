add_library(wperf_doctest INTERFACE)
target_include_directories(wperf_doctest SYSTEM INTERFACE third_party)

add_executable(wperf_tests test_app_logic.cpp test_lock_inspector.cpp test_lock_cli.cpp test_native_handles.cpp)
target_include_directories(wperf_tests PRIVATE ${SOURCE_DIR})
target_link_libraries(wperf_tests PRIVATE wperf_doctest wperf_lock_cli)
target_compile_options(wperf_tests PRIVATE /W4 /WX /permissive- /utf-8)

add_test(NAME wperf.unit COMMAND wperf_tests --no-colors)
set_tests_properties(wperf.unit PROPERTIES LABELS unit TIMEOUT 30)

find_program(WPERF_POWERSHELL NAMES pwsh powershell REQUIRED)
add_test(NAME wperf.cli_contract COMMAND ${WPERF_POWERSHELL} -NoProfile -ExecutionPolicy Bypass
    -File ${CMAKE_CURRENT_SOURCE_DIR}/test_lock_cli_integration.ps1 -Executable $<TARGET_FILE:wperf>)
set_tests_properties(wperf.cli_contract PROPERTIES LABELS integration TIMEOUT 150)

option(WPERF_BUILD_INTEGRATION_TESTS "Build opt-in Windows Restart Manager tests" OFF)
if(WPERF_BUILD_INTEGRATION_TESTS)
    add_test(NAME wperf.cli_deep COMMAND ${WPERF_POWERSHELL} -NoProfile -ExecutionPolicy Bypass
        -File ${CMAKE_CURRENT_SOURCE_DIR}/test_lock_cli_integration.ps1 -Executable $<TARGET_FILE:wperf> -Deep)
    set_tests_properties(wperf.cli_deep PROPERTIES LABELS integration TIMEOUT 90)
    add_test(NAME wperf.cli_resources COMMAND ${WPERF_POWERSHELL} -NoProfile -ExecutionPolicy Bypass
        -File ${CMAKE_CURRENT_SOURCE_DIR}/test_lock_cli_integration.ps1 -Executable $<TARGET_FILE:wperf> -Resources)
    set_tests_properties(wperf.cli_resources PROPERTIES LABELS integration TIMEOUT 100)
    add_executable(wperf_lock_integration_tests test_lock_inspector_integration.cpp)
    target_link_libraries(wperf_lock_integration_tests PRIVATE wperf_doctest wperf_lock_inspector)
    target_compile_options(wperf_lock_integration_tests PRIVATE /W4 /WX /permissive- /utf-8)
    add_test(NAME wperf.lock_integration COMMAND wperf_lock_integration_tests --no-colors)
    set_tests_properties(wperf.lock_integration PROPERTIES LABELS integration TIMEOUT 60)
endif()
