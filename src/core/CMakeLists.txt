add_library(easeletch_core STATIC
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
    transform.h
    transform.cpp
    textrender.h
    textrender.cpp
)

# The layer compositor spreads tiles over std::thread workers.
find_package(Threads REQUIRED)

target_include_directories(easeletch_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(easeletch_core PUBLIC Qt6::Core Qt6::Gui Threads::Threads)
# QZipReader / QZipWriter for .easeletch files (private Qt API).
target_link_libraries(easeletch_core PRIVATE Qt6::CorePrivate Qt6::GuiPrivate)
