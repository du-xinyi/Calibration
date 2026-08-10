cmake_minimum_required(VERSION 3.16)

project(Calibration VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 当 Qt6 未显式配置时自动探测其安装位置。
# 兼容两种场景：PATH 含 qmake6（终端构建）、不含（CLion 等 GUI 启动）。
# 注意：find_package 失败会把 Qt6_DIR 缓存成 *-NOTFOUND，因此用 EXISTS 显式校验
# 并在重试前清掉失效缓存，否则后续配置会因「Qt6_DIR 已定义」而跳过探测。
if(NOT Qt6_DIR OR NOT EXISTS "${Qt6_DIR}/Qt6Config.cmake")
    unset(Qt6_DIR CACHE)

    # 1) 优先用 PATH 中的 qmake6 查询安装前缀
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

    # 2) 回退：扫描 Qt 安装器常见目录（CLion 启动时 PATH 可能不含 Qt bin）
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

# OpenCV 5 将 calib3d 拆分为 calib + objdetect + geometry 等模块：
#   findChessboardCorners / findChessboardCornersSB / drawChessboardCorners / CALIB_CB_* -> objdetect
#   calibrateCamera / CALIB_FIX_K3 / CALIB_ZERO_TANGENT_DIST                          -> calib
#   Rodrigues                                                                          -> geometry
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

# 将项目源码目录作为编译期常量传入，作为图片对话框的默认起始路径。
target_compile_definitions(CalibrationUi PRIVATE
    PROJECT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

add_executable(Calibration src/main.cpp)
target_link_libraries(Calibration PRIVATE CalibrationUi)

set_target_properties(Calibration PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
)

include(CTest)

if(BUILD_TESTING)
    add_executable(CalibrationCompletionTest
        tests/calibration_completion_test.cpp
    )
    target_link_libraries(CalibrationCompletionTest PRIVATE
        CalibrationUi
    )

    add_test(
        NAME calibration_completion_does_not_crash
        COMMAND CalibrationCompletionTest
                "${CMAKE_CURRENT_SOURCE_DIR}/image/arx_camera0"
    )

    add_executable(CalibrationComparisonLifecycleTest
        tests/calibration_comparison_lifecycle_test.cpp
    )
    target_link_libraries(CalibrationComparisonLifecycleTest PRIVATE
        CalibrationUi
    )

    add_test(
        NAME algorithm_comparison_lifecycle_does_not_crash
        COMMAND CalibrationComparisonLifecycleTest
                "${CMAKE_CURRENT_SOURCE_DIR}/image/arx_camera0"
    )
    set_tests_properties(algorithm_comparison_lifecycle_does_not_crash PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        TIMEOUT 45
    )
    set_tests_properties(calibration_completion_does_not_crash PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        TIMEOUT 30
    )

    add_executable(CalibrationBoardTest
        tests/calibration_board_test.cpp
    )
    target_link_libraries(CalibrationBoardTest PRIVATE
        CalibrationCore
    )

    add_test(
        NAME calibration_board_types_and_square_counts
        COMMAND CalibrationBoardTest
                "${CMAKE_CURRENT_SOURCE_DIR}/image/arx_camera0"
    )
    set_tests_properties(calibration_board_types_and_square_counts PROPERTIES
        TIMEOUT 30
    )
endif()
