add_executable(BasiliskSim
    src/main_v314.cpp
)

target_link_libraries(BasiliskSim PRIVATE Basilisk::Core)
target_compile_features(BasiliskSim PRIVATE cxx_std_20)

add_library(BasiliskAiSimulation STATIC
    src/AiSimulation.cpp
    src/AiBenchmark.cpp
    src/SandboxStress.cpp
)
add_library(Basilisk::AiSimulation ALIAS BasiliskAiSimulation)
target_include_directories(BasiliskAiSimulation PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(BasiliskAiSimulation PUBLIC Basilisk::Core Basilisk::ClientAI)
target_compile_features(BasiliskAiSimulation PUBLIC cxx_std_20)

add_executable(BasiliskAiSim src/main_ai.cpp)
target_link_libraries(BasiliskAiSim PRIVATE Basilisk::AiSimulation)
target_compile_features(BasiliskAiSim PRIVATE cxx_std_20)

add_executable(BasiliskSandboxStress src/main_sandbox_stress.cpp)
target_link_libraries(BasiliskSandboxStress PRIVATE Basilisk::AiSimulation)
target_compile_features(BasiliskSandboxStress PRIVATE cxx_std_20)

# Old Hunter's Map is tracked by dedicated V3.14 telemetry rather than the
# legacy five-item arrays in main_v39.cpp. Suppress the resulting exhaustive
# switch warning for the simulator target without changing Core warnings.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(BasiliskSim PRIVATE -Wno-switch)
endif()
