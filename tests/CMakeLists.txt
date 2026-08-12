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
