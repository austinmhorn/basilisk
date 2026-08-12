add_library(BasiliskCore STATIC
    src/Random.cpp
    src/world/MapGenerator.cpp
    src/world/WorldGraph.cpp
    src/systems/ItemSystem.cpp
    src/systems/LooseArrowSystem.cpp
    src/systems/MapDiscoverySystem.cpp
    src/systems/MatchCoordinator.cpp
    src/systems/ObservationSystem.cpp
    src/systems/PitInvestigationSystem.cpp
    src/systems/RoundController.cpp
    src/systems/SearchSystem.cpp
    src/systems/SnapshotSystem.cpp
    src/systems/SoloCoordinator.cpp
    src/systems/TurnResolver.cpp
    src/systems/WorldDangerSystem.cpp
)

add_library(Basilisk::Core ALIAS BasiliskCore)

target_include_directories(BasiliskCore
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(BasiliskCore PUBLIC cxx_std_20)
