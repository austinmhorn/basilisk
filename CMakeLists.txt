cmake_minimum_required(VERSION 3.25)

project(Basilisk VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BASILISK_BUILD_TESTS "Build BasiliskCore tests" ON)
option(BASILISK_BUILD_CLI "Build the headless playable CLI" ON)
option(BASILISK_BUILD_SIM "Build the automated simulation harness" ON)
option(BASILISK_BUILD_GAME "Build the SDL3 graphical client" OFF)

add_subdirectory(core)

if(BASILISK_BUILD_CLI)
    add_subdirectory(clients/cli)
endif()

if(BASILISK_BUILD_SIM)
    add_subdirectory(clients/sim)
endif()

if(BASILISK_BUILD_GAME)
    add_subdirectory(clients/game)
endif()

if(BASILISK_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)

    if(BASILISK_BUILD_CLI)
        add_test(NAME BasiliskCliSmoke COMMAND BasiliskCli --smoke)
    endif()

    if(BASILISK_BUILD_SIM)
        add_test(NAME BasiliskSimSmoke COMMAND BasiliskSim 10 40 100000 500000)
    endif()
endif()
