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
add_subdirectory(clients/shared)

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
        add_test(NAME BasiliskAiSimSmoke COMMAND BasiliskAiSim --matches 2 --seed 123)
    endif()
endif()

if(BASILISK_BUILD_GAME AND (APPLE OR WIN32))
    set(CPACK_PACKAGE_NAME "Basilisk")
    set(CPACK_PACKAGE_VENDOR "Basilisk")
    set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Basilisk graphical game client")
    set(CPACK_MONOLITHIC_INSTALL ON)
    if(APPLE)
        set(CPACK_GENERATOR "DragNDrop")
        set(CPACK_PACKAGE_FILE_NAME
            "Basilisk-${PROJECT_VERSION}-macOS-${CMAKE_SYSTEM_PROCESSOR}")
    elseif(WIN32)
        set(CPACK_GENERATOR "ZIP")
        set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
        set(CPACK_PACKAGE_FILE_NAME
            "Basilisk-${PROJECT_VERSION}-Windows-${CMAKE_SYSTEM_PROCESSOR}")
    endif()
    include(CPack)
endif()
