set(GNSS_APP_NAME gnssOptimization)

file(GLOB GNSS_SOURCES         ${CMAKE_CURRENT_LIST_DIR}/src/*.cpp)
file(GLOB GNSS_INCS            ${CMAKE_CURRENT_LIST_DIR}/src/*.h)
file(GLOB GNSS_INC_SPARSE      ${NATID_SDK_INC}/sparse/*.h)
file(GLOB GNSS_INC_SPARSE_PRIV ${NATID_SDK_INC}/sparse/priv/*.h)

#Application icon
set(GNSS_PLIST ${CMAKE_CURRENT_LIST_DIR}/res/appIcon/AppIcon.plist)
if(WIN32)
    set(GNSS_WINAPP_ICON ${CMAKE_CURRENT_LIST_DIR}/res/appIcon/winAppIcon.rc)
else()
    set(GNSS_WINAPP_ICON ${CMAKE_CURRENT_LIST_DIR}/res/appIcon/winAppIcon.cpp)
endif()

# add executable
add_executable(${GNSS_APP_NAME}
    ${GNSS_INCS}
    ${GNSS_SOURCES}
    ${GNSS_INC_SPARSE}
    ${GNSS_INC_SPARSE_PRIV}
    ${GNSS_WINAPP_ICON}
)

source_group("inc"               FILES ${GNSS_INCS})
source_group("src"               FILES ${GNSS_SOURCES})
source_group("inc\\sparse"       FILES ${GNSS_INC_SPARSE})
source_group("inc\\sparse\\priv" FILES ${GNSS_INC_SPARSE_PRIV})

target_link_libraries(${GNSS_APP_NAME} debug ${MU_LIB_DEBUG} debug ${MATRIX_LIB_DEBUG} debug ${NATGUI_LIB_DEBUG}
                                        optimized ${MU_LIB_RELEASE} optimized ${MATRIX_LIB_RELEASE} optimized ${NATGUI_LIB_RELEASE})

setTargetPropertiesForGUIApp(${GNSS_APP_NAME} ${GNSS_PLIST})

setAppIcon(${GNSS_APP_NAME} ${CMAKE_CURRENT_LIST_DIR})

setIDEPropertiesForGUIExecutable(${GNSS_APP_NAME} ${CMAKE_CURRENT_LIST_DIR})

setPlatformDLLPath(${GNSS_APP_NAME})

# Belt-and-suspenders: also copy the GTK runtime DLLs directly next to the
# built exe. Windows always checks the exe's own folder for DLLs before
# consulting PATH, so this works even if VS_DEBUGGER_ENVIRONMENT (set by
# setPlatformDLLPath above) isn't being picked up for some reason.
if (WIN32)
    add_custom_command(TARGET ${GNSS_APP_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${NATID_SDK_BIN}/GTK"
            "$<TARGET_FILE_DIR:${GNSS_APP_NAME}>"
        COMMENT "Copying GTK runtime DLLs next to ${GNSS_APP_NAME}.exe"
    )
endif()

setIDEPropertiesForExecutable(${GNSS_APP_NAME})