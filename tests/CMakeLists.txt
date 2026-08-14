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

add_executable(BasiliskSnapshotTests
    snapshot_tests.cpp
)
target_link_libraries(BasiliskSnapshotTests PRIVATE Basilisk::Core)
add_test(NAME BasiliskSnapshotTests COMMAND BasiliskSnapshotTests)

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
