cmake_minimum_required(VERSION 3.18)

set(SOLUTION_NAME GNSSOptimization)

#BEGIN common part for all solutions
project(${SOLUTION_NAME})
set(SOURCE_ROOT ${CMAKE_CURRENT_LIST_DIR})
if (WIN32)
    set(HOME_ROOT $ENV{USERPROFILE})
    string(REPLACE "\\" "/" HOME_ROOT "${HOME_ROOT}")
else()
    set(HOME_ROOT $ENV{HOME})
endif()
set(NATID_SDK_ROOT ${HOME_ROOT}/natID.SDK)
include(${NATID_SDK_ROOT}/DevEnv/Common.cmake)
#END common part for all solutions

# GUI (windows, canvas, native widgets).
include(${NATID_SDK_ROOT}/DevEnv/natGUI.cmake)

# Sparse solver (LDLT) for the Gauss-Newton/gradient-descent numerics.
include(${NATID_SDK_ROOT}/DevEnv/MatrixLib.cmake)

# gnssOptimization.cmake now follows the same GUI-app template as
# MineSweeper.cmake, including the app-icon macros - copy the res/appIcon
# folder from an existing natGUI project (e.g. your Minesweeper project)
# into this project's root, unchanged, before building.

include(gnssOptimization.cmake)