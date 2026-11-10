cmake_minimum_required(VERSION 3.24)

project(AetherEngine
    VERSION 0.5.0
    DESCRIPTION "Aether Engine - open source, AI-native 3D game engine"
    HOMEPAGE_URL "https://github.com/alicelikeeat/moonengine"
    LANGUAGES C CXX)

if(PROJECT_IS_TOP_LEVEL)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(AETHER_BUILD_TESTS "Build Aether unit tests" ON)
option(AETHER_BUILD_BENCHMARKS "Build Aether benchmarks" ON)
option(AETHER_BUILD_SAMPLES "Build sample applications (needs a windowing system)" ON)
option(AETHER_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
set(AETHER_SANITIZER "" CACHE STRING "Sanitizer to enable: address, thread, undefined, or empty")
set_property(CACHE AETHER_SANITIZER PROPERTY STRINGS "" address thread undefined)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(AetherOptions)
include(AetherModule)

find_package(Threads REQUIRED)

if(AETHER_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests/support)
endif()

add_subdirectory(third_party)
add_subdirectory(engine)
add_subdirectory(editor)

if(AETHER_BUILD_SAMPLES)
    add_subdirectory(samples)
endif()

if(AETHER_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()
