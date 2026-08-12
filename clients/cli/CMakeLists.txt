add_executable(BasiliskCli
    src/main.cpp
)

target_link_libraries(BasiliskCli PRIVATE Basilisk::Core)
target_compile_features(BasiliskCli PRIVATE cxx_std_20)
