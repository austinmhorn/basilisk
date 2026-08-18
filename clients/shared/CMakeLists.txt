add_library(BasiliskClientMetadata INTERFACE)

add_library(Basilisk::ClientMetadata ALIAS BasiliskClientMetadata)

target_include_directories(BasiliskClientMetadata
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(BasiliskClientMetadata INTERFACE Basilisk::Core)
target_compile_features(BasiliskClientMetadata INTERFACE cxx_std_20)

add_library(BasiliskClientPresentation STATIC
    src/Presentation.cpp
)

add_library(Basilisk::ClientPresentation ALIAS BasiliskClientPresentation)

target_include_directories(BasiliskClientPresentation
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(BasiliskClientPresentation PUBLIC Basilisk::Core)
target_compile_features(BasiliskClientPresentation PUBLIC cxx_std_20)

add_library(BasiliskClientNavigation STATIC
    src/RoutePlanner.cpp
)

add_library(Basilisk::ClientNavigation ALIAS BasiliskClientNavigation)

target_include_directories(BasiliskClientNavigation
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(BasiliskClientNavigation PUBLIC Basilisk::Core)
target_compile_features(BasiliskClientNavigation PUBLIC cxx_std_20)
