find_package(Qt6 REQUIRED COMPONENTS Test)

# Pure CPU tests run headless on the offscreen platform.
function(easel_add_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE ${ARGN} Qt6::Test)
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

# GPU tests need a real window system (on Linux CI: xvfb-run with Mesa).
function(easel_add_gpu_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE ${ARGN} Qt6::Test)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

easel_add_test(tst_color easel_core)
easel_add_test(tst_tilestore easel_core)
easel_add_test(tst_tilepyramid easel_core)
easel_add_test(tst_tileatlas easel_core)
easel_add_test(tst_documentio easel_core Qt6::CorePrivate)
easel_add_test(tst_brush easel_core)
easel_add_test(tst_history easel_core)
easel_add_gpu_test(tst_canvasview easel_ui)
easel_add_gpu_test(tst_painting easel_ui)
easel_add_gpu_test(tst_canvasarea easel_ui)
easel_add_test(tst_colorui easel_ui)
