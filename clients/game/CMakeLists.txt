include(FetchContent)

# SDL and the pinned SQLite amalgamation are implemented in C, while Basilisk
# Core itself only requires C++.
enable_language(C)

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

add_library(BasiliskGameAssets STATIC
    src/AssetCatalog.cpp
)

add_library(Basilisk::GameAssets ALIAS BasiliskGameAssets)

target_include_directories(BasiliskGameAssets
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(BasiliskGameAssets PUBLIC Basilisk::ClientMetadata)
target_compile_features(BasiliskGameAssets PUBLIC cxx_std_20)

add_library(BasiliskGameActions STATIC
    src/AuthScreen.cpp
    src/ActionCommands.cpp
    src/ActionPresentation.cpp
    src/ActionSelection.cpp
    src/ClientLifecycle.cpp
    src/ClientSessionController.cpp
    src/MainMenu.cpp
    src/MapActionMenu.cpp
    src/SnapshotPresentation.cpp
)

add_library(Basilisk::GameActions ALIAS BasiliskGameActions)

target_include_directories(BasiliskGameActions
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(
    BasiliskGameActions
    PUBLIC
        Basilisk::ClientMetadata
        Basilisk::ClientNavigation
        Basilisk::ClientPresentation
)
target_compile_features(BasiliskGameActions PUBLIC cxx_std_20)

add_library(BasiliskGameNetworkSession STATIC
    src/NetworkEndpointConfig.cpp
    src/NetworkGameSessionAdapter.cpp
    src/NetworkWireCodec.cpp
    src/WebSocketNetworkSession.cpp
)

add_library(
    Basilisk::GameNetworkSession
    ALIAS BasiliskGameNetworkSession
)

target_include_directories(BasiliskGameNetworkSession
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(
    BasiliskGameNetworkSession
    PUBLIC
        Basilisk::GameActions
        Basilisk::GameMapLayout
)
target_compile_features(BasiliskGameNetworkSession PUBLIC cxx_std_20)
if(WIN32)
    target_sources(BasiliskGameNetworkSession PRIVATE
        src/NativeNetworkRuntime.cpp
    )
    target_compile_definitions(BasiliskGameNetworkSession PUBLIC NOMINMAX)
endif()

if(EMSCRIPTEN)
    target_link_options(BasiliskGameNetworkSession PUBLIC "-lwebsocket.js")
else()
    if(APPLE)
        set(USE_TLS ON CACHE BOOL "" FORCE)
        set(USE_SECURE_TRANSPORT ON CACHE BOOL "" FORCE)
    elseif(WIN32)
        # IXWebSocket's supported Windows default is Mbed TLS. Build the LTS
        # release statically so packaged clients do not depend on external DLLs.
        set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
        set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
        set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
        set(USE_SHARED_MBEDTLS_LIBRARY OFF CACHE BOOL "" FORCE)
        set(USE_STATIC_MBEDTLS_LIBRARY ON CACHE BOOL "" FORCE)
        FetchContent_Declare(
            mbedtls
            URL https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-3.6.7/mbedtls-3.6.7.tar.bz2
            URL_HASH SHA256=a7e8bcbec0e6f761b4af24f25677626b35f762f68eef79c08677a363212d11f6
            DOWNLOAD_EXTRACT_TIMESTAMP OFF
        )
        FetchContent_MakeAvailable(mbedtls)
        set(MBEDTLS_FOUND TRUE)
        set(MBEDTLS_INCLUDE_DIRS "${mbedtls_SOURCE_DIR}/include")
        set(MBEDTLS_LIBRARIES mbedtls mbedx509 mbedcrypto)
        set(MBEDTLS_VERSION_GREATER_THAN_3 TRUE)
        set(USE_TLS ON CACHE BOOL "" FORCE)
        set(USE_MBED_TLS ON CACHE BOOL "" FORCE)
    else()
        set(USE_TLS OFF CACHE BOOL "" FORCE)
    endif()
    set(USE_ZLIB OFF CACHE BOOL "" FORCE)
    set(IXWEBSOCKET_INSTALL OFF CACHE BOOL "" FORCE)
    set(BUILD_DEMO OFF CACHE BOOL "" FORCE)
    set(USE_TEST OFF CACHE BOOL "" FORCE)
    set(_basilisk_ix_build_shared_was_defined FALSE)
    if(DEFINED BUILD_SHARED_LIBS)
        set(_basilisk_ix_build_shared_was_defined TRUE)
        set(_basilisk_ix_saved_build_shared "${BUILD_SHARED_LIBS}")
    endif()
    set(BUILD_SHARED_LIBS OFF)
    FetchContent_Declare(
        IXWebSocket
        URL https://github.com/machinezone/IXWebSocket/archive/refs/tags/v12.0.1.tar.gz
        URL_HASH SHA256=d23bdc91dbfe2b9ae13c322d539392d7a6b8b506560f41c90e227fa0f86a2405
        DOWNLOAD_EXTRACT_TIMESTAMP OFF
    )
    FetchContent_MakeAvailable(IXWebSocket)
    if(_basilisk_ix_build_shared_was_defined)
        set(BUILD_SHARED_LIBS "${_basilisk_ix_saved_build_shared}")
    else()
        unset(BUILD_SHARED_LIBS)
    endif()
    unset(_basilisk_ix_build_shared_was_defined)
    unset(_basilisk_ix_saved_build_shared)
    target_link_libraries(BasiliskGameNetworkSession PRIVATE ixwebsocket)
endif()

add_library(BasiliskAuthoritativeNetworkSession STATIC EXCLUDE_FROM_ALL
    server/AuthoritativeInMemoryMatch.cpp
    server/PublicAccountProfiles.cpp
    server/TrophyScoring.cpp
)
add_library(
    Basilisk::AuthoritativeNetworkSession
    ALIAS BasiliskAuthoritativeNetworkSession
)
target_include_directories(BasiliskAuthoritativeNetworkSession
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/server
)
target_link_libraries(
    BasiliskAuthoritativeNetworkSession
    PUBLIC
        Basilisk::GameNetworkSession
    PRIVATE
        Basilisk::Core
)
target_compile_features(
    BasiliskAuthoritativeNetworkSession
    PUBLIC cxx_std_20
)

if(NOT EMSCRIPTEN)
    add_library(BasiliskLobbyCoordinator STATIC EXCLUDE_FROM_ALL
        server/LobbyCoordinator.cpp
        server/LobbyProtocolService.cpp
    )
    add_library(Basilisk::LobbyCoordinator ALIAS BasiliskLobbyCoordinator)
    target_include_directories(BasiliskLobbyCoordinator PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/server
    )
    target_link_libraries(BasiliskLobbyCoordinator
        PUBLIC Basilisk::AuthoritativeNetworkSession
    )
    target_compile_features(BasiliskLobbyCoordinator PUBLIC cxx_std_20)

    FetchContent_Declare(
        argon2_reference
        GIT_REPOSITORY https://github.com/P-H-C/phc-winner-argon2.git
        GIT_TAG 20190702
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(argon2_reference)

    add_library(BasiliskArgon2 STATIC EXCLUDE_FROM_ALL
        ${argon2_reference_SOURCE_DIR}/src/argon2.c
        ${argon2_reference_SOURCE_DIR}/src/core.c
        ${argon2_reference_SOURCE_DIR}/src/encoding.c
        ${argon2_reference_SOURCE_DIR}/src/ref.c
        ${argon2_reference_SOURCE_DIR}/src/blake2/blake2b.c
    )
    target_include_directories(BasiliskArgon2 SYSTEM PUBLIC
        ${argon2_reference_SOURCE_DIR}/include
    )
    target_include_directories(BasiliskArgon2 PRIVATE
        ${argon2_reference_SOURCE_DIR}/src
    )
    target_compile_definitions(BasiliskArgon2 PRIVATE ARGON2_NO_THREADS)

    FetchContent_Declare(
        sqlite_amalgamation
        URL https://www.sqlite.org/2025/sqlite-amalgamation-3500400.zip
        URL_HASH SHA256=1d3049dd0f830a025a53105fc79fd2ab9431aea99e137809d064d8ee8356b032
        DOWNLOAD_EXTRACT_TIMESTAMP OFF
    )
    FetchContent_MakeAvailable(sqlite_amalgamation)

    add_library(BasiliskSQLite3 STATIC EXCLUDE_FROM_ALL
        ${sqlite_amalgamation_SOURCE_DIR}/sqlite3.c
    )
    target_include_directories(BasiliskSQLite3 SYSTEM PUBLIC
        ${sqlite_amalgamation_SOURCE_DIR}
    )
    target_compile_definitions(BasiliskSQLite3 PRIVATE
        SQLITE_OMIT_LOAD_EXTENSION
        SQLITE_THREADSAFE=1
    )

    add_library(BasiliskTrophyPersistenceSQLite STATIC EXCLUDE_FROM_ALL
        server/SQLiteTrophyPersistence.cpp
    )
    add_library(
        Basilisk::TrophyPersistenceSQLite
        ALIAS BasiliskTrophyPersistenceSQLite
    )
    target_include_directories(BasiliskTrophyPersistenceSQLite
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/server
    )
    target_link_libraries(BasiliskTrophyPersistenceSQLite
        PUBLIC
            Basilisk::AuthoritativeNetworkSession
        PRIVATE
            BasiliskSQLite3
    )
    target_compile_features(BasiliskTrophyPersistenceSQLite PUBLIC cxx_std_20)

    add_library(BasiliskAccountAuth STATIC EXCLUDE_FROM_ALL
        server/AccountAuth.cpp
        server/AccountAuthProtocol.cpp
    )
    add_library(Basilisk::AccountAuth ALIAS BasiliskAccountAuth)
    target_include_directories(BasiliskAccountAuth PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/server
    )
    target_link_libraries(BasiliskAccountAuth
        PUBLIC Basilisk::AuthoritativeNetworkSession
        PRIVATE BasiliskSQLite3 BasiliskArgon2
    )
    if(APPLE)
        target_link_libraries(BasiliskAccountAuth PRIVATE "-framework Security")
    elseif(WIN32)
        target_link_libraries(BasiliskAccountAuth PRIVATE bcrypt)
    endif()
    target_compile_features(BasiliskAccountAuth PUBLIC cxx_std_20)

    add_library(BasiliskWebSocketMatchServer STATIC EXCLUDE_FROM_ALL
        server/LocalWebSocketMatchServer.cpp
    )
    add_library(
        Basilisk::WebSocketMatchServer
        ALIAS BasiliskWebSocketMatchServer
    )
    target_include_directories(BasiliskWebSocketMatchServer
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/server
    )
    target_link_libraries(BasiliskWebSocketMatchServer
        PUBLIC
            Basilisk::AuthoritativeNetworkSession
        PRIVATE
            Basilisk::TrophyPersistenceSQLite
            Basilisk::AccountAuth
            Basilisk::LobbyCoordinator
            ixwebsocket
    )
    target_compile_features(BasiliskWebSocketMatchServer PUBLIC cxx_std_20)

    add_executable(BasiliskServer EXCLUDE_FROM_ALL server/main.cpp)
    target_link_libraries(BasiliskServer PRIVATE Basilisk::WebSocketMatchServer)
    target_compile_features(BasiliskServer PRIVATE cxx_std_20)
endif()

add_library(BasiliskGameLocalSession STATIC
    src/LocalGameSessionAdapter.cpp
)

add_library(Basilisk::GameLocalSession ALIAS BasiliskGameLocalSession)

target_include_directories(BasiliskGameLocalSession
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(
    BasiliskGameLocalSession
    PUBLIC
        Basilisk::GameActions
        Basilisk::GameMapLayout
    PRIVATE
        Basilisk::Core
)
target_compile_features(BasiliskGameLocalSession PUBLIC cxx_std_20)

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

# SDL3_image is built statically with only its self-contained SVG decoder.
set(SDLIMAGE_INSTALL OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_SAMPLES OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_TESTS OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_VENDORED ON CACHE BOOL "" FORCE)
set(SDLIMAGE_BACKEND_STB OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_BACKEND_IMAGEIO OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_BACKEND_WIC OFF CACHE BOOL "" FORCE)
foreach(_image_format IN ITEMS
        ANI APNG AVIF BMP GIF JPG JXL LBM PCX PNG PNM QOI TGA TIF WEBP XCF XPM XV)
    set("SDLIMAGE_${_image_format}" OFF CACHE BOOL "" FORCE)
endforeach()
set(SDLIMAGE_SVG ON CACHE BOOL "" FORCE)
# SDL_image 3.4.4 does not initialize this status variable when PNG is off.
set(SDLIMAGE_APNG_ENABLED OFF)

set(_basilisk_build_shared_libs_was_defined FALSE)
if(DEFINED BUILD_SHARED_LIBS)
    set(_basilisk_build_shared_libs_was_defined TRUE)
    set(_basilisk_saved_build_shared_libs "${BUILD_SHARED_LIBS}")
endif()
set(BUILD_SHARED_LIBS OFF)

FetchContent_Declare(
    SDL3_image
    URL https://github.com/libsdl-org/SDL_image/releases/download/release-3.4.4/SDL3_image-3.4.4.tar.gz
    URL_HASH SHA256=29751304a13d25ac513f24305fa25b06a6edd9607718c90129b8350d35fc5573
    DOWNLOAD_EXTRACT_TIMESTAMP OFF
)
FetchContent_MakeAvailable(SDL3_image)

if(_basilisk_build_shared_libs_was_defined)
    set(BUILD_SHARED_LIBS "${_basilisk_saved_build_shared_libs}")
else()
    unset(BUILD_SHARED_LIBS)
endif()
unset(_basilisk_build_shared_libs_was_defined)
unset(_basilisk_saved_build_shared_libs)
unset(_image_format)

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

add_library(BasiliskGameRuntime STATIC
    src/AuthScreenRenderer.cpp
    src/DemoActionCommandSink.cpp
    src/DemoMap.cpp
    src/DemoSessionCommandSink.cpp
    src/DemoUi.cpp
    src/MainMenuRenderer.cpp
    src/MapRenderer.cpp
    src/SvgTextureManager.cpp
    src/SessionTokenStorage.cpp
    src/TextRenderer.cpp
)

target_include_directories(BasiliskGameRuntime
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(BasiliskGameRuntime
    PUBLIC
        Basilisk::GameAssets
        SDL3::SDL3
        SDL3_image::SDL3_image
        SDL3_ttf::SDL3_ttf
)
target_compile_features(BasiliskGameRuntime PUBLIC cxx_std_20)

function(basilisk_add_game_executable target)
    set(_executable_options)
    if(APPLE AND "${target}" STREQUAL "BasiliskGame")
        list(APPEND _executable_options MACOSX_BUNDLE)
    endif()
    add_executable(${target} ${_executable_options}
        src/ConnectionStatusPresentation.cpp
        src/main.cpp
        src/ScreenShell.cpp
    )
    unset(_executable_options)
    if(APPLE AND "${target}" STREQUAL "BasiliskGame")
        set(_asset_root
            "$<TARGET_BUNDLE_CONTENT_DIR:${target}>/Resources/assets")
    else()
        set(_asset_root "$<TARGET_FILE_DIR:${target}>/assets")
    endif()
    target_link_libraries(
        ${target}
        PRIVATE
            BasiliskGameRuntime
            Basilisk::GameNetworkSession
    )
    target_compile_features(${target} PRIVATE cxx_std_20)

    add_custom_command(
        TARGET ${target}
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "${_asset_root}/fonts"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${BASILISK_GAME_FONT_ASSET_DIR}"
            "${_asset_root}/fonts"
        COMMENT "Packaging Inter font assets for ${target}"
    )

    add_custom_command(
        TARGET ${target}
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/assets/ui"
            "${_asset_root}/ui"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/assets/items"
            "${_asset_root}/items"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/assets/emblems"
            "${_asset_root}/emblems"
        COMMENT "Packaging SVG UI assets for ${target}"
    )
    unset(_asset_root)
endfunction()

basilisk_add_game_executable(BasiliskGame)
if(APPLE)
    set(_basilisk_macos_icon
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/macos/BasiliskGame.icns")
    set_source_files_properties("${_basilisk_macos_icon}" PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources
    )
    target_sources(BasiliskGame PRIVATE "${_basilisk_macos_icon}")
    set_target_properties(BasiliskGame PROPERTIES
        MACOSX_BUNDLE_BUNDLE_NAME "Basilisk"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.basilisk.game"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_ICON_FILE "BasiliskGame.icns"
    )
    unset(_basilisk_macos_icon)
endif()
target_link_libraries(
    BasiliskGame
    PRIVATE
        Basilisk::GameLocalSession
)

if(APPLE)
    install(TARGETS BasiliskGame
        BUNDLE DESTINATION .
        COMPONENT BasiliskGame
    )
elseif(WIN32)
    install(TARGETS BasiliskGame
        RUNTIME DESTINATION .
        COMPONENT BasiliskGame
    )
    install(DIRECTORY "${BASILISK_GAME_FONT_ASSET_DIR}/"
        DESTINATION assets/fonts
        COMPONENT BasiliskGame
    )
    foreach(_asset_directory IN ITEMS ui items emblems)
        install(DIRECTORY
            "${CMAKE_CURRENT_SOURCE_DIR}/assets/${_asset_directory}/"
            DESTINATION "assets/${_asset_directory}"
            COMPONENT BasiliskGame
        )
    endforeach()
    unset(_asset_directory)
endif()

if(NOT EMSCRIPTEN)
    add_library(BasiliskGameDebugSupport STATIC
        src/DebugMapProvider.cpp
        src/DebugMapRenderer.cpp
    )
    add_library(Basilisk::GameDebugSupport ALIAS BasiliskGameDebugSupport)
    target_include_directories(BasiliskGameDebugSupport
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_compile_definitions(
        BasiliskGameDebugSupport
        PUBLIC
            BASILISK_GAME_DEBUG_BUILD=1
    )
    target_link_libraries(
        BasiliskGameDebugSupport
        PUBLIC
            Basilisk::GameMapLayout
            SDL3::SDL3
            SDL3_ttf::SDL3_ttf
    )
    target_compile_features(BasiliskGameDebugSupport PUBLIC cxx_std_20)

    add_library(BasiliskGameDebugLocalSession STATIC
        src/LocalGameSessionAdapter.cpp
    )
    add_library(
        Basilisk::GameDebugLocalSession
        ALIAS BasiliskGameDebugLocalSession
    )
    target_include_directories(BasiliskGameDebugLocalSession
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_compile_definitions(
        BasiliskGameDebugLocalSession
        PUBLIC
            BASILISK_GAME_DEBUG_BUILD=1
    )
    target_link_libraries(
        BasiliskGameDebugLocalSession
        PUBLIC
            Basilisk::GameActions
            Basilisk::GameMapLayout
            Basilisk::GameDebugSupport
        PRIVATE
            Basilisk::Core
    )
    target_compile_features(BasiliskGameDebugLocalSession PUBLIC cxx_std_20)

    basilisk_add_game_executable(BasiliskGameDebug)
    target_compile_definitions(BasiliskGameDebug PRIVATE BASILISK_GAME_DEBUG=1)
    target_link_libraries(
        BasiliskGameDebug
        PRIVATE
            Basilisk::GameDebugLocalSession
            Basilisk::GameDebugSupport
    )
endif()

if(EMSCRIPTEN)
    set_target_properties(BasiliskGame PROPERTIES SUFFIX ".html")
    set_property(TARGET BasiliskGame APPEND PROPERTY LINK_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/web/network-arguments.js"
    )
    target_link_options(
        BasiliskGame
        PRIVATE
            "SHELL:--pre-js ${CMAKE_CURRENT_SOURCE_DIR}/web/network-arguments.js"
            "SHELL:--preload-file ${BASILISK_GAME_FONT_ASSET_DIR}@/assets/fonts"
            "SHELL:--preload-file ${CMAKE_CURRENT_SOURCE_DIR}/assets/ui@/assets/ui"
            "SHELL:--preload-file ${CMAKE_CURRENT_SOURCE_DIR}/assets/items@/assets/items"
            "SHELL:--preload-file ${CMAKE_CURRENT_SOURCE_DIR}/assets/emblems@/assets/emblems"
    )

    set(BASILISK_WEB_PACKAGE_DIR
        "${CMAKE_BINARY_DIR}/web-package"
        CACHE PATH
        "Output directory for the deployable Basilisk WebAssembly client"
    )
    add_custom_target(BasiliskGameWebPackage
        COMMAND ${CMAKE_COMMAND} -E remove_directory
            "${BASILISK_WEB_PACKAGE_DIR}"
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "${BASILISK_WEB_PACKAGE_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:BasiliskGame>"
            "${BASILISK_WEB_PACKAGE_DIR}/index.html"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE_DIR:BasiliskGame>/BasiliskGame.js"
            "${BASILISK_WEB_PACKAGE_DIR}/BasiliskGame.js"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE_DIR:BasiliskGame>/BasiliskGame.wasm"
            "${BASILISK_WEB_PACKAGE_DIR}/BasiliskGame.wasm"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE_DIR:BasiliskGame>/BasiliskGame.data"
            "${BASILISK_WEB_PACKAGE_DIR}/BasiliskGame.data"
        DEPENDS BasiliskGame
        COMMENT "Assembling deployable Basilisk WebAssembly package"
        VERBATIM
    )
endif()
