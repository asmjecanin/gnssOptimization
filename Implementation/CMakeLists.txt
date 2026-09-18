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

# macOS bundle metadata.
#
# The Makefiles and Ninja generators copy the Info.plist template verbatim,
# leaving Xcode's $(...) variables unexpanded, so Finder refuses to launch the
# bundle with "the application cannot be opened because its executable is
# missing". They also leave the bundle unsigned, which arm64 will not run.
#
# The Xcode generator is excluded on purpose: it expands the variables and
# signs the bundle itself, it uses a different plist template that lacks some
# of these keys, and a PlistBuddy "Set" on a missing key fails - which Xcode
# treats as a fatal build error. Adding this block without the generator check
# makes the build fail under Xcode.
if(APPLE AND NOT CMAKE_GENERATOR STREQUAL "Xcode" AND TARGET ${GNSS_APP_NAME})
    set(PLIST_TOOL /usr/libexec/PlistBuddy)
    set(BUNDLE_PLIST $<TARGET_BUNDLE_DIR:${GNSS_APP_NAME}>/Contents/Info.plist)
    add_custom_command(TARGET ${GNSS_APP_NAME} POST_BUILD
        COMMAND ${PLIST_TOOL} -c "Set :CFBundleExecutable ${GNSS_APP_NAME}" ${BUNDLE_PLIST}
        COMMAND ${PLIST_TOOL} -c "Set :CFBundleName ${GNSS_APP_NAME}" ${BUNDLE_PLIST}
        COMMAND ${PLIST_TOOL} -c "Set :CFBundleIdentifier com.example.gnssoptimization" ${BUNDLE_PLIST}
        COMMAND ${PLIST_TOOL} -c "Set :CFBundlePackageType APPL" ${BUNDLE_PLIST}
        COMMAND ${PLIST_TOOL} -c "Set :CFBundleDevelopmentRegion en" ${BUNDLE_PLIST}
        COMMAND ${PLIST_TOOL} -c "Set :LSMinimumSystemVersion 12.3" ${BUNDLE_PLIST}
        COMMAND codesign --force --deep -s - $<TARGET_BUNDLE_DIR:${GNSS_APP_NAME}>
        COMMENT "Fixing Info.plist and signing the app bundle"
    )
endif()