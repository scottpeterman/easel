cmake_minimum_required(VERSION 3.21)

project(Easeletch VERSION 0.4.0 DESCRIPTION "Balanced layered image editor" LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "Minimum macOS version")
endif()

find_package(Qt6 6.7 REQUIRED COMPONENTS Core Gui Widgets ShaderTools)
# QRhi headers (rhi/qrhi.h) come with Qt::GuiPrivate, and the zip reader and
# writer for .easeletch files with Qt::CorePrivate (Qt::GuiPrivate before 6.9).
# Since Qt 6.9 private modules must be requested by name.
if(Qt6_VERSION VERSION_GREATER_EQUAL 6.9)
    # Expected: bundles ship the exact Qt they were built with.
    set(QT_NO_PRIVATE_MODULE_WARNING ON)
    find_package(Qt6 REQUIRED COMPONENTS CorePrivate GuiPrivate)
endif()
qt_standard_project_setup()

include(GNUInstallDirs)

option(EASELETCH_BUILD_TESTS "Build unit tests" ON)

if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_subdirectory(src/core)
add_subdirectory(src/ui)
add_subdirectory(src/app)

if(EASELETCH_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
