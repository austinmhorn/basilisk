# The test suite uses assert() for both checks and test-driving expressions.
# Release configurations define NDEBUG globally, so explicitly undefine it for
# every C++ target created in this directory.
if(MSVC)
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/UNDEBUG>)
else()
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-UNDEBUG>)
endif()

add_executable(BasiliskCoreTests
    core_tests.cpp
)
target_link_libraries(BasiliskCoreTests PRIVATE Basilisk::Core)
add_test(NAME BasiliskCoreTests COMMAND BasiliskCoreTests)

add_executable(BasiliskSearchItemTests
    search_item_tests.cpp
)
target_link_libraries(BasiliskSearchItemTests PRIVATE Basilisk::Core)
add_test(NAME BasiliskSearchItemTests COMMAND BasiliskSearchItemTests)

add_executable(BasiliskObjectiveTests
    objective_tests.cpp
)
target_link_libraries(BasiliskObjectiveTests PRIVATE Basilisk::Core)
add_test(NAME BasiliskObjectiveTests COMMAND BasiliskObjectiveTests)

add_executable(BasiliskSimultaneousTests
    basilisk_simultaneous_tests.cpp
)
target_link_libraries(BasiliskSimultaneousTests PRIVATE Basilisk::Core)
add_test(NAME BasiliskSimultaneousTests COMMAND BasiliskSimultaneousTests)

add_executable(BasiliskWorldDangerTests
    world_danger_tests.cpp
)
target_link_libraries(BasiliskWorldDangerTests PRIVATE Basilisk::Core)
add_test(NAME BasiliskWorldDangerTests COMMAND BasiliskWorldDangerTests)

add_executable(BasiliskMapDiscoveryTests
    map_discovery_tests.cpp
)
target_link_libraries(BasiliskMapDiscoveryTests PRIVATE Basilisk::Core)
add_test(NAME BasiliskMapDiscoveryTests COMMAND BasiliskMapDiscoveryTests)

add_executable(BasiliskMapGeneratorTests
    map_generator_tests.cpp
)
target_link_libraries(BasiliskMapGeneratorTests PRIVATE Basilisk::Core)
add_test(NAME BasiliskMapGeneratorTests COMMAND BasiliskMapGeneratorTests)

add_executable(BasiliskObservationTests
    observation_tests.cpp
)
target_link_libraries(BasiliskObservationTests PRIVATE Basilisk::Core)
add_test(NAME BasiliskObservationTests COMMAND BasiliskObservationTests)

add_executable(BasiliskClientPresentationTests
    client_presentation_tests.cpp
)
target_link_libraries(BasiliskClientPresentationTests PRIVATE Basilisk::ClientPresentation)
add_test(NAME BasiliskClientPresentationTests COMMAND BasiliskClientPresentationTests)

add_executable(BasiliskClientProfileMetadataTests
    client_profile_metadata_tests.cpp
)
target_link_libraries(BasiliskClientProfileMetadataTests PRIVATE Basilisk::ClientMetadata)
add_test(NAME BasiliskClientProfileMetadataTests COMMAND BasiliskClientProfileMetadataTests)

add_executable(BasiliskClientViewContextTests
    client_view_context_tests.cpp
)
target_link_libraries(BasiliskClientViewContextTests PRIVATE Basilisk::ClientMetadata)
add_test(NAME BasiliskClientViewContextTests COMMAND BasiliskClientViewContextTests)

add_executable(BasiliskClientRoutePlannerTests
    client_route_planner_tests.cpp
)
target_link_libraries(BasiliskClientRoutePlannerTests PRIVATE Basilisk::ClientNavigation)
add_test(NAME BasiliskClientRoutePlannerTests COMMAND BasiliskClientRoutePlannerTests)

if(TARGET BasiliskGameMapLayout)
    add_executable(BasiliskGameMapLayoutTests
        game_map_layout_tests.cpp
    )
    target_link_libraries(BasiliskGameMapLayoutTests PRIVATE Basilisk::GameMapLayout)
    add_test(NAME BasiliskGameMapLayoutTests COMMAND BasiliskGameMapLayoutTests)
endif()

if(TARGET BasiliskGameAssets)
    add_executable(BasiliskGameAssetCatalogTests
        game_asset_catalog_tests.cpp
    )
    target_link_libraries(BasiliskGameAssetCatalogTests PRIVATE Basilisk::GameAssets)
    add_test(NAME BasiliskGameAssetCatalogTests COMMAND BasiliskGameAssetCatalogTests)
endif()

if(TARGET BasiliskGameDebugLocalSession)
    add_executable(BasiliskGameDebugMapTests
        game_debug_map_tests.cpp
    )
    target_link_libraries(
        BasiliskGameDebugMapTests
        PRIVATE
            Basilisk::GameDebugLocalSession
    )
    add_test(NAME BasiliskGameDebugMapTests COMMAND BasiliskGameDebugMapTests)
endif()

if(TARGET BasiliskGameActions)
    add_executable(BasiliskGameMainMenuTests
        game_main_menu_tests.cpp
    )
    target_link_libraries(
        BasiliskGameMainMenuTests
        PRIVATE
            Basilisk::GameActions
    )
    add_test(NAME BasiliskGameMainMenuTests COMMAND BasiliskGameMainMenuTests)

    add_executable(BasiliskGameActionTests
        game_action_tests.cpp
    )
    target_link_libraries(
        BasiliskGameActionTests
        PRIVATE
            Basilisk::GameLocalSession
    )
    add_test(NAME BasiliskGameActionTests COMMAND BasiliskGameActionTests)
endif()

if(TARGET BasiliskGameNetworkSession)
    add_executable(BasiliskGameNetworkSessionTests
        game_network_session_tests.cpp
    )
    target_link_libraries(
        BasiliskGameNetworkSessionTests
        PRIVATE
            Basilisk::GameNetworkSession
    )
    add_test(
        NAME BasiliskGameNetworkSessionTests
        COMMAND BasiliskGameNetworkSessionTests
    )
endif()

if(TARGET BasiliskAuthoritativeNetworkSession)
    add_executable(BasiliskTrophyScoringTests
        trophy_scoring_tests.cpp
    )
    target_link_libraries(
        BasiliskTrophyScoringTests
        PRIVATE
            Basilisk::AuthoritativeNetworkSession
    )
    add_test(
        NAME BasiliskTrophyScoringTests
        COMMAND BasiliskTrophyScoringTests
    )

    add_executable(BasiliskAuthoritativeNetworkSessionTests
        game_authoritative_network_session_tests.cpp
    )
    target_link_libraries(
        BasiliskAuthoritativeNetworkSessionTests
        PRIVATE
            Basilisk::AuthoritativeNetworkSession
    )
    add_test(
        NAME BasiliskAuthoritativeNetworkSessionTests
        COMMAND BasiliskAuthoritativeNetworkSessionTests
    )
endif()

if(TARGET BasiliskTrophyPersistenceSQLite)
    add_executable(BasiliskTrophyPersistenceTests
        trophy_persistence_tests.cpp
    )
    target_link_libraries(
        BasiliskTrophyPersistenceTests
        PRIVATE
            Basilisk::TrophyPersistenceSQLite
    )
    add_test(
        NAME BasiliskTrophyPersistenceTests
        COMMAND BasiliskTrophyPersistenceTests
    )
endif()

if(TARGET BasiliskAccountAuth)
    add_executable(BasiliskAccountAuthTests
        account_auth_tests.cpp
    )
    target_link_libraries(
        BasiliskAccountAuthTests
        PRIVATE
            Basilisk::AccountAuth
    )
    add_test(NAME BasiliskAccountAuthTests COMMAND BasiliskAccountAuthTests)
endif()

if(TARGET BasiliskLobbyCoordinator)
    add_executable(BasiliskLobbyCoordinatorTests
        lobby_coordinator_tests.cpp
    )
    target_link_libraries(BasiliskLobbyCoordinatorTests
        PRIVATE Basilisk::LobbyCoordinator
    )
    add_test(NAME BasiliskLobbyCoordinatorTests COMMAND BasiliskLobbyCoordinatorTests)
endif()

if(TARGET BasiliskWebSocketMatchServer)
    add_executable(BasiliskWebSocketNetworkTests
        game_websocket_network_tests.cpp
    )
    target_link_libraries(
        BasiliskWebSocketNetworkTests
        PRIVATE
            Basilisk::WebSocketMatchServer
    )
    target_include_directories(
        BasiliskWebSocketNetworkTests
        PRIVATE
            $<TARGET_PROPERTY:ixwebsocket,INTERFACE_INCLUDE_DIRECTORIES>
    )
    add_test(
        NAME BasiliskWebSocketNetworkTests
        COMMAND BasiliskWebSocketNetworkTests
    )
endif()

add_executable(BasiliskSnapshotTests
    snapshot_tests.cpp
)
target_link_libraries(BasiliskSnapshotTests PRIVATE Basilisk::Core)
add_test(NAME BasiliskSnapshotTests COMMAND BasiliskSnapshotTests)

add_executable(BasiliskPublicMatchMetadataTests
    public_match_metadata_tests.cpp
)
target_link_libraries(BasiliskPublicMatchMetadataTests PRIVATE Basilisk::Core)
add_test(NAME BasiliskPublicMatchMetadataTests COMMAND BasiliskPublicMatchMetadataTests)

add_executable(BasiliskMatchCoordinatorTests
    match_coordinator_tests.cpp
)
target_link_libraries(BasiliskMatchCoordinatorTests PRIVATE Basilisk::Core)
add_test(NAME BasiliskMatchCoordinatorTests COMMAND BasiliskMatchCoordinatorTests)

add_executable(BasiliskSoloCoordinatorTests
    solo_coordinator_tests.cpp
)
target_link_libraries(BasiliskSoloCoordinatorTests PRIVATE Basilisk::Core)
add_test(NAME BasiliskSoloCoordinatorTests COMMAND BasiliskSoloCoordinatorTests)

add_executable(BasiliskLooseArrowTests
    loose_arrow_tests.cpp
)
target_link_libraries(BasiliskLooseArrowTests PRIVATE Basilisk::Core)
add_test(NAME BasiliskLooseArrowTests COMMAND BasiliskLooseArrowTests)

add_executable(BasiliskPitInvestigationTests
    pit_investigation_tests.cpp
)
target_link_libraries(BasiliskPitInvestigationTests PRIVATE Basilisk::Core)
add_test(NAME BasiliskPitInvestigationTests COMMAND BasiliskPitInvestigationTests)

add_executable(BasiliskLooseArrowVisibilityTests
    loose_arrow_visibility_tests.cpp
)
target_link_libraries(BasiliskLooseArrowVisibilityTests PRIVATE Basilisk::Core)
add_test(NAME BasiliskLooseArrowVisibilityTests COMMAND BasiliskLooseArrowVisibilityTests)

add_executable(BasiliskEvadeRelocationTests
    basilisk_evade_relocation_tests.cpp
)
target_link_libraries(BasiliskEvadeRelocationTests PRIVATE Basilisk::Core)
add_test(NAME BasiliskEvadeRelocationTests COMMAND BasiliskEvadeRelocationTests)

add_executable(BasiliskOldHuntersMapTests
    old_hunters_map_tests.cpp
)
target_link_libraries(BasiliskOldHuntersMapTests PRIVATE Basilisk::Core)
add_test(NAME BasiliskOldHuntersMapTests COMMAND BasiliskOldHuntersMapTests)

find_program(BASILISK_NODE_EXECUTABLE NAMES node nodejs)
if(BASILISK_NODE_EXECUTABLE)
    add_test(
        NAME BasiliskWebRoutePlannerTests
        COMMAND "${BASILISK_NODE_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/web_route_planner_tests.js"
    )
else()
    message(STATUS "Node.js not found; web route planner unit tests will not be registered.")
endif()
