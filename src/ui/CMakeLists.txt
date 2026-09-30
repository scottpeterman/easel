add_library(easel_ui STATIC
    canvasview.h
    canvasview.cpp
    canvasarea.h
    canvasarea.cpp
    brushtool.h
    brushtool.cpp
    brushoptionsbar.h
    brushoptionsbar.cpp
    colorpanel.h
    colorpanel.cpp
    colorwheel.h
    colorwheel.cpp
    eyedroppertool.h
    eyedroppertool.cpp
    selecttools.h
    selecttools.cpp
    newdocumentdialog.h
    newdocumentdialog.cpp
    mainwindow.h
    mainwindow.cpp
)

target_include_directories(easel_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(easel_ui PUBLIC easel_core Qt6::Widgets Qt6::GuiPrivate)
target_compile_definitions(easel_ui PUBLIC EASEL_VERSION="${PROJECT_VERSION}")

# Compiled to .qsb packs (SPIR-V, GLSL, HLSL, MSL) at build time; loaded from :/shaders/.
qt_add_shaders(easel_ui "easel_shaders"
    PREFIX "/"
    FILES
        shaders/tiles.vert
        shaders/tiles.frag
        shaders/outline.vert
        shaders/outline.frag
)
