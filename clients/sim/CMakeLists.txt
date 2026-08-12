add_executable(BasiliskSim
    src/main_v310.cpp
)

target_link_libraries(BasiliskSim PRIVATE Basilisk::Core)
target_compile_features(BasiliskSim PRIVATE cxx_std_20)
