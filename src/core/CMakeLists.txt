add_library(easel_core STATIC
    color.h
    color.cpp
    tilestore.h
    tilestore.cpp
    tilepyramid.h
    tilepyramid.cpp
    tileatlas.h
    tileatlas.cpp
)

target_include_directories(easel_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(easel_core PUBLIC Qt6::Core Qt6::Gui)
