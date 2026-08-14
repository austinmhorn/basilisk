include(FetchContent)

add_library(BasiliskGameMapLayout STATIC
    src/MapLayout.cpp
)

add_library(Basilisk::GameMapLayout ALIAS BasiliskGameMapLayout)

target_include_directories(BasiliskGameMapLayout
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(BasiliskGameMapLayout PUBLIC Basilisk::Core)
target_compile_features(BasiliskGameMapLayout PUBLIC cxx_std_20)

# SDL is implemented in C, while Basilisk Core itself only requires C++.
enable_language(C)

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
set(SDL_DISABLE_INSTALL_DOCS ON CACHE BOOL "" FORCE)

FetchContent_Declare(
    SDL3
    URL https://github.com/libsdl-org/SDL/releases/download/release-3.4.10/SDL3-3.4.10.tar.gz
    URL_HASH SHA256=12b34280415ec8418c864408b93d008a20a6530687ee613d60bfbd20411f2785
    DOWNLOAD_EXTRACT_TIMESTAMP OFF
)
FetchContent_MakeAvailable(SDL3)

add_executable(BasiliskGame
    src/DemoMap.cpp
    src/main.cpp
    src/MapRenderer.cpp
)

target_link_libraries(BasiliskGame
    PRIVATE
        Basilisk::GameMapLayout
        SDL3::SDL3
)
target_compile_features(BasiliskGame PRIVATE cxx_std_20)

if(EMSCRIPTEN)
    set_target_properties(BasiliskGame PROPERTIES SUFFIX ".html")
endif()
