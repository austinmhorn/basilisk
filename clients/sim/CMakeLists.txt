add_executable(BasiliskSim
    src/main_v314.cpp
)

target_link_libraries(BasiliskSim PRIVATE Basilisk::Core)
target_compile_features(BasiliskSim PRIVATE cxx_std_20)

# Old Hunter's Map is tracked by dedicated V3.14 telemetry rather than the
# legacy five-item arrays in main_v39.cpp. Suppress the resulting exhaustive
# switch warning for the simulator target without changing Core warnings.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(BasiliskSim PRIVATE -Wno-switch)
endif()
