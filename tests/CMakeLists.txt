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
