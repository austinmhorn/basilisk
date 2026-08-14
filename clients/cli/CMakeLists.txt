add_executable(BasiliskCli
    src/main.cpp
)

target_link_libraries(BasiliskCli PRIVATE Basilisk::ClientPresentation)
target_compile_features(BasiliskCli PRIVATE cxx_std_20)

add_executable(BasiliskVisualCli
    src/main_visual.cpp
)

target_link_libraries(BasiliskVisualCli PRIVATE Basilisk::ClientPresentation)
target_compile_features(BasiliskVisualCli PRIVATE cxx_std_20)
