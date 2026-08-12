add_executable(BasiliskCoreTests
    core_tests.cpp
)

target_link_libraries(BasiliskCoreTests PRIVATE Basilisk::Core)

add_test(NAME BasiliskCoreTests COMMAND BasiliskCoreTests)
