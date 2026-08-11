cmake_minimum_required(VERSION 3.16)

project(camera-calibrator VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 未提供有效 Qt6_DIR 时尝试定位 Qt，兼容终端与缺少 Qt PATH 的 IDE 环境
# 失效的 CMake 缓存值必须先清除，否则后续探测不会覆盖 *-NOTFOUND
if(NOT Qt6_DIR OR NOT EXISTS "${Qt6_DIR}/Qt6Config.cmake")
    unset(Qt6_DIR CACHE)

    # 首选由 qmake 报告的实际安装前缀
    find_program(QMAKE_EXECUTABLE qmake6 qmake)
    if(QMAKE_EXECUTABLE)
        execute_process(
            COMMAND ${QMAKE_EXECUTABLE} -query QT_INSTALL_PREFIX
            OUTPUT_VARIABLE _qt_install_prefix
            OUTPUT_STRIP_TRAILING_WHITESPACE)
        if(_qt_install_prefix AND NOT _qt_install_prefix STREQUAL "**Unknown**"
           AND EXISTS "${_qt_install_prefix}/lib/cmake/Qt6/Qt6Config.cmake")
            get_filename_component(_qt6_dir
                "${_qt_install_prefix}/lib/cmake/Qt6/Qt6Config.cmake" DIRECTORY)
            set(Qt6_DIR "${_qt6_dir}")
        endif()
    endif()

    # qmake 不可见时再检查 Qt 安装器的常用目录结构
    if(NOT Qt6_DIR)
        file(GLOB _qt_install_dirs
            "/opt/Qt/[0-9]*/gcc_64"
            "/opt/Qt/[0-9]*/gcc"
            "$ENV{HOME}/Qt/[0-9]*/gcc_64")
        foreach(_dir IN LISTS _qt_install_dirs)
            if(EXISTS "${_dir}/lib/cmake/Qt6/Qt6Config.cmake")
                get_filename_component(_qt6_dir
                    "${_dir}/lib/cmake/Qt6/Qt6Config.cmake" DIRECTORY)
                set(Qt6_DIR "${_qt6_dir}")
                break()
            endif()
        endforeach()
    endif()

    if(Qt6_DIR)
        set(Qt6_DIR "${Qt6_DIR}" CACHE PATH "Path to Qt6 CMake config directory")
        message(STATUS "Auto-detected Qt6_DIR: ${Qt6_DIR}")
    endif()
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets Concurrent)

# OpenCV 5 将原 calib3d 能力拆分到求解、特征检测和三维几何模块
# 显式列出组件可避免链接到错误的旧版聚合目标
file(GLOB _opencv5_dirs
    "/usr/local/lib/cmake/opencv5"
    "/usr/lib/x86_64-linux-gnu/cmake/opencv5"
    "/usr/lib/cmake/opencv5")
set(_preferred_opencv_dir "")
foreach(_dir IN LISTS _opencv5_dirs)
    if(EXISTS "${_dir}/OpenCVConfig.cmake")
        set(_preferred_opencv_dir "${_dir}")
        break()
    endif()
endforeach()
if(_preferred_opencv_dir)
    set(OpenCV_DIR "${_preferred_opencv_dir}" CACHE PATH
        "Path to OpenCV CMake config directory" FORCE)
    message(STATUS "Auto-detected OpenCV_DIR: ${OpenCV_DIR}")
endif()

find_package(OpenCV 5 REQUIRED COMPONENTS
    calib
    geometry
    objdetect
    imgproc
    imgcodecs
)
message(STATUS "OpenCV ${OpenCV_VERSION}: using calib/geometry/objdetect modules")

qt_standard_project_setup()

add_compile_options(
    -Wall
    -Wextra
    -Wpedantic
)

add_library(CalibrationCore STATIC
    src/calibration.cpp
    src/calibration_project.cpp
    src/image_quality.cpp
)
target_include_directories(CalibrationCore PUBLIC src)
target_link_libraries(CalibrationCore PUBLIC Qt6::Gui ${OpenCV_LIBS})

add_library(CalibrationUi STATIC
    src/algorithm_comparison_dialog.cpp
    src/mainwindow.cpp
    src/mainwindow.ui
    src/pose_result_dialog.cpp
    src/pose_result_dialog.ui
)
target_include_directories(CalibrationUi PUBLIC src)
target_link_libraries(CalibrationUi PUBLIC
    CalibrationCore
    Qt6::Widgets
    Qt6::Concurrent
)

# 开发构建默认从源码目录打开图片选择窗口
target_compile_definitions(CalibrationUi PRIVATE
    PROJECT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

add_executable(Calibration src/main.cpp)
target_link_libraries(Calibration PRIVATE CalibrationUi)

set_target_properties(Calibration PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
)

