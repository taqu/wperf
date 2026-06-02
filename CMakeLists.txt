cmake_minimum_required(VERSION 4.2.0)
set(PROJECT_NAME wperf)
project(${PROJECT_NAME} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(PROJECT_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
include_directories(AFTER ${PROJECT_ROOT}/include)
set(HEADER_DIR ${PROJECT_ROOT}/include)
set(SOURCE_DIR ${PROJECT_ROOT}/src)
add_compile_definitions(UNICODE _UNICODE WINVER=0x0601 _WIN32_WINNT=0x0601)

set(HEADERS
    ${HEADER_DIR}/resource_monitor.h
)

set(SOURCES
    ${SOURCE_DIR}/main.cpp
    ${SOURCE_DIR}/resource_monitor.cpp
)

source_group("include" FILES ${HEADERS})
source_group("src" FILES ${SOURCES})

add_executable(${PROJECT_NAME} WIN32 ${HEADERS} ${SOURCES})

target_link_libraries(${PROJECT_NAME} PRIVATE
    gdi32
    msimg32
    iphlpapi
    sapi
    comctl32
    dxgi
    pdh
)

target_compile_options(${PROJECT_NAME} PRIVATE /W4)
target_compile_options(${PROJECT_NAME} PRIVATE $<$<CONFIG:Release>:/O2 /Oi /Gy /GL>)
target_link_options(${PROJECT_NAME} PRIVATE $<$<CONFIG:Release>:/OPT:REF /OPT:ICF /LTCG>)
set_target_properties(${PROJECT_NAME} PROPERTIES LINK_FLAGS "/HEAP:0,0 /STACK:0,0")
