cmake_minimum_required(VERSION 4.2.0)
set(PROJECT_NAME wperf)
project(${PROJECT_NAME} VERSION 0.1.0 LANGUAGES CXX)

set(WPERF_VERSION_SUFFIX "" CACHE STRING "Optional semantic version prerelease suffix, for example -rc1")
if(NOT WPERF_VERSION_SUFFIX MATCHES "^(-[0-9A-Za-z.-]+)?$")
    message(FATAL_ERROR "WPERF_VERSION_SUFFIX must be empty or like -rc1")
endif()
set(WPERF_VERSION_STRING "${PROJECT_VERSION}${WPERF_VERSION_SUFFIX}")
set(WPERF_FILE_VERSION_COMMAS "${PROJECT_VERSION_MAJOR},${PROJECT_VERSION_MINOR},${PROJECT_VERSION_PATCH},0")
set(WPERF_FILE_VERSION_STRING "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}.0")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/cmake/version_config.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/version_config.h @ONLY)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(PROJECT_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
set(HEADER_DIR ${PROJECT_ROOT}/include)
set(SOURCE_DIR ${PROJECT_ROOT}/src)
set(RESOURCE_DIR ${PROJECT_ROOT}/resource)

include_directories(AFTER ${HEADER_DIR})
include_directories(AFTER ${RESOURCE_DIR})
include_directories(AFTER ${CMAKE_CURRENT_BINARY_DIR}/generated)

add_compile_definitions(UNICODE _UNICODE WINVER=0x0601 _WIN32_WINNT=0x0601)

set(HEADERS
    ${HEADER_DIR}/app_logic.h
    ${HEADER_DIR}/lock_gui.h
    ${HEADER_DIR}/process_controller.h
    ${HEADER_DIR}/resource_monitor.h
    ${HEADER_DIR}/purge_memory.h
)

set(SOURCES
    ${SOURCE_DIR}/main.cpp
    ${SOURCE_DIR}/lock_gui.cpp
    ${SOURCE_DIR}/resource_monitor.cpp
    ${SOURCE_DIR}/purge_memory.cpp
)
set(RESOURCES
    ${RESOURCE_DIR}/resource.h
    ${RESOURCE_DIR}/resource.rc
)

source_group("include" FILES ${HEADERS})
source_group("src" FILES ${SOURCES})
source_group("resource" FILES ${RESOURCES})

add_executable(${PROJECT_NAME} WIN32 ${HEADERS} ${SOURCES} ${RESOURCES})

add_library(wperf_lock_inspector STATIC
    ${HEADER_DIR}/lock_inspector.h
    ${SOURCE_DIR}/lock_inspector_internal.h
    ${SOURCE_DIR}/lock_inspector.cpp
    ${SOURCE_DIR}/native_handle_backend.h
    ${SOURCE_DIR}/native_handle_backend.cpp
    ${SOURCE_DIR}/lock_path.cpp
    ${SOURCE_DIR}/lock_result_merge.cpp
)
target_include_directories(wperf_lock_inspector PUBLIC ${HEADER_DIR})
target_compile_options(wperf_lock_inspector PRIVATE /W4 /WX /permissive- /utf-8)
target_link_libraries(wperf_lock_inspector PRIVATE Rstrtmgr)

add_library(wperf_lock_cli STATIC ${HEADER_DIR}/lock_cli.h ${SOURCE_DIR}/lock_cli.cpp)
target_link_libraries(wperf_lock_cli PUBLIC wperf_lock_inspector)
target_compile_options(wperf_lock_cli PRIVATE /W4 /WX /permissive- /utf-8)

add_library(wperf_lock_gui_presenter STATIC
    ${HEADER_DIR}/lock_gui_presenter.h
    ${SOURCE_DIR}/lock_gui_presenter.cpp
)
target_link_libraries(wperf_lock_gui_presenter PUBLIC wperf_lock_inspector)
target_compile_options(wperf_lock_gui_presenter PRIVATE /W4 /WX /permissive- /utf-8)

add_library(wperf_process_controller STATIC
    ${HEADER_DIR}/process_controller.h
    ${SOURCE_DIR}/process_controller.cpp
)
target_compile_options(wperf_process_controller PRIVATE /W4 /WX /permissive- /utf-8)

target_link_libraries(${PROJECT_NAME} PRIVATE
    wperf_lock_inspector
    wperf_lock_gui_presenter
    wperf_process_controller
    shell32
    gdi32
    iphlpapi
    comctl32
    dxgi
    pdh
    ole32
)

target_compile_options(${PROJECT_NAME} PRIVATE /W4 /Zc:inline /GR- /permissive- /utf-8)
target_compile_options(${PROJECT_NAME} PRIVATE $<$<CONFIG:Release>:/O2 /Oi /Gy /GL>)
target_link_options(${PROJECT_NAME} PRIVATE $<$<CONFIG:Release>:/OPT:REF /OPT:ICF /LTCG>)
set_target_properties(${PROJECT_NAME} PROPERTIES LINK_FLAGS "/HEAP:0,0 /STACK:0,0")

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
