add_library(easel_core STATIC
    color.h
    color.cpp
    tilestore.h
    tilestore.cpp
    tilepyramid.h
    tilepyramid.cpp
    tileatlas.h
    tileatlas.cpp
    documentio.h
    documentio.cpp
    brush.h
    brush.cpp
    layerstack.h
    layerstack.cpp
    history.h
    history.cpp
    selection.h
    selection.cpp
    regionops.h
    regionops.cpp
)

# The layer compositor spreads tiles over std::thread workers.
find_package(Threads REQUIRED)

target_include_directories(easel_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(easel_core PUBLIC Qt6::Core Qt6::Gui Threads::Threads)
# QZipReader / QZipWriter for .easel files (private Qt API).
target_link_libraries(easel_core PRIVATE Qt6::CorePrivate Qt6::GuiPrivate)
