cmake_minimum_required(VERSION 3.25)

project(Basilisk VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BASILISK_BUILD_TESTS "Build BasiliskCore tests" ON)

add_subdirectory(core)

if(BASILISK_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
