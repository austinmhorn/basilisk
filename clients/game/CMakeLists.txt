include(FetchContent)

add_library(BasiliskGameMapLayout STATIC
    src/MapLayout.cpp
    src/MapPresentation.cpp
)

add_library(Basilisk::GameMapLayout ALIAS BasiliskGameMapLayout)

target_include_directories(BasiliskGameMapLayout
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(
    BasiliskGameMapLayout
    PUBLIC
        Basilisk::Core
        Basilisk::ClientNavigation
)
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

# SDL3_ttf is built statically with its own pinned dependencies so the game
# does not depend on platform font libraries being installed separately.
set(SDLTTF_INSTALL OFF CACHE BOOL "" FORCE)
set(SDLTTF_SAMPLES OFF CACHE BOOL "" FORCE)
set(SDLTTF_VENDORED ON CACHE BOOL "" FORCE)
set(SDLTTF_HARFBUZZ OFF CACHE BOOL "" FORCE)
set(SDLTTF_PLUTOSVG OFF CACHE BOOL "" FORCE)

set(_basilisk_build_shared_libs_was_defined FALSE)
if(DEFINED BUILD_SHARED_LIBS)
    set(_basilisk_build_shared_libs_was_defined TRUE)
    set(_basilisk_saved_build_shared_libs "${BUILD_SHARED_LIBS}")
endif()
set(BUILD_SHARED_LIBS OFF)

FetchContent_Declare(
    SDL3_ttf
    GIT_REPOSITORY https://github.com/libsdl-org/SDL_ttf.git
    GIT_TAG a1ce3670aec736ecbf0936c43f2f0cc53aa61e5b
    GIT_SUBMODULES external/freetype
    GIT_SUBMODULES_RECURSE TRUE
)
FetchContent_MakeAvailable(SDL3_ttf)

if(_basilisk_build_shared_libs_was_defined)
    set(BUILD_SHARED_LIBS "${_basilisk_saved_build_shared_libs}")
else()
    unset(BUILD_SHARED_LIBS)
endif()
unset(_basilisk_build_shared_libs_was_defined)
unset(_basilisk_saved_build_shared_libs)

FetchContent_Declare(
    InterFont
    URL https://github.com/rsms/inter/releases/download/v4.1/Inter-4.1.zip
    URL_HASH SHA256=9883fdd4a49d4fb66bd8177ba6625ef9a64aa45899767dde3d36aa425756b11e
    DOWNLOAD_EXTRACT_TIMESTAMP OFF
)
FetchContent_MakeAvailable(InterFont)

set(BASILISK_GAME_FONT_ASSET_DIR "${CMAKE_CURRENT_BINARY_DIR}/font-assets")
file(MAKE_DIRECTORY "${BASILISK_GAME_FONT_ASSET_DIR}")
foreach(_font_name IN ITEMS Regular Medium SemiBold Bold)
    configure_file(
        "${interfont_SOURCE_DIR}/extras/ttf/Inter-${_font_name}.ttf"
        "${BASILISK_GAME_FONT_ASSET_DIR}/Inter-${_font_name}.ttf"
        COPYONLY
    )
endforeach()
configure_file(
    "${interfont_SOURCE_DIR}/LICENSE.txt"
    "${BASILISK_GAME_FONT_ASSET_DIR}/Inter-LICENSE.txt"
    COPYONLY
)
unset(_font_name)

add_executable(BasiliskGame
    src/DemoMap.cpp
    src/DemoUi.cpp
    src/main.cpp
    src/MapRenderer.cpp
    src/ScreenShell.cpp
    src/TextRenderer.cpp
)

target_link_libraries(BasiliskGame
    PRIVATE
        Basilisk::GameMapLayout
        Basilisk::ClientPresentation
        SDL3::SDL3
        SDL3_ttf::SDL3_ttf
)
target_compile_features(BasiliskGame PRIVATE cxx_std_20)

add_custom_command(
    TARGET BasiliskGame
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:BasiliskGame>/assets/fonts"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${BASILISK_GAME_FONT_ASSET_DIR}"
        "$<TARGET_FILE_DIR:BasiliskGame>/assets/fonts"
    COMMENT "Packaging Inter font assets"
)

if(EMSCRIPTEN)
    set_target_properties(BasiliskGame PROPERTIES SUFFIX ".html")
    target_link_options(
        BasiliskGame
        PRIVATE
            "SHELL:--preload-file ${BASILISK_GAME_FONT_ASSET_DIR}@/assets/fonts"
    )
endif()
