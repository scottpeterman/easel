add_library(easeletch_ui STATIC
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
    adjustpanel.h
    adjustpanel.cpp
    filterdialog.h
    filterdialog.cpp
    shadedialog.h
    shadedialog.cpp
    gradienteditor.h
    gradienteditor.cpp
    edittools.h
    edittools.cpp
    colortoalphadialog.h
    colortoalphadialog.cpp
    griddialog.h
    griddialog.cpp
    newdocumentdialog.h
    newdocumentdialog.cpp
    layerpanel.h
    layerpanel.cpp
    textpanel.h
    textpanel.cpp
    mainwindow.h
    mainwindow.cpp
    mainwindow_tools.cpp
    mainwindow_pages.cpp
    mainwindow_shapes.cpp
)

target_include_directories(easeletch_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(easeletch_ui PUBLIC easeletch_core Qt6::Widgets Qt6::GuiPrivate)
target_compile_definitions(easeletch_ui PUBLIC EASELETCH_VERSION="${PROJECT_VERSION}")

# Compiled to .qsb packs (SPIR-V, GLSL, HLSL, MSL) at build time; loaded from :/shaders/.
qt_add_shaders(easeletch_ui "easeletch_shaders"
    PREFIX "/"
    FILES
        shaders/tiles.vert
        shaders/tiles.frag
        shaders/outline.vert
        shaders/outline.frag
)
