add_library(BasiliskCore STATIC
    src/Random.cpp
    src/world/WorldGraph.cpp
    src/systems/TurnResolver.cpp
)

add_library(Basilisk::Core ALIAS BasiliskCore)

target_include_directories(BasiliskCore
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(BasiliskCore PUBLIC cxx_std_20)
