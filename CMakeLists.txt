cmake_minimum_required(VERSION 3.22)

project(CollabDAW VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(COLLAB_BUILD_APP   "Build the JUCE / Tracktion desktop app" ON)
option(COLLAB_BUILD_TESTS "Build unit tests for the core library" ON)

# Static MSVC runtime to match Tracktion's examples and ease distribution.
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    add_compile_options(/utf-8 /bigobj)
endif()

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
endif()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(cmake/Dependencies.cmake)

add_subdirectory(app/core)
add_subdirectory(app/chord)

if(COLLAB_BUILD_TESTS)
    enable_testing()
    add_subdirectory(app/core/tests)
endif()

if(COLLAB_BUILD_APP)
    add_subdirectory(app)
endif()
