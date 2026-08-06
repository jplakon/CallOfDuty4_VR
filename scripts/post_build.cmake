# [POST_BUILD] Copy over MILES dependency
add_custom_command(
        TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${DEPS_DIR}/msslib/dlls
        $<TARGET_FILE_DIR:${PROJECT_NAME}>
        COMMENT "COPYING MILES DEPENDENCIES"
)
# [POST_BUILD] Copy over steam depdendency
add_custom_command(
        TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${DEPS_DIR}/steamsdk/steam_api.dll
        $<TARGET_FILE_DIR:${PROJECT_NAME}>
        COMMENT "COPYING STEAM DEPENDENCIES"
)

# A distributable Release executable must use the retail June 2010 D3DX
# runtime.  Fail the build immediately if solution-generation state ever
# causes the debug-only import to return.
add_custom_command(
        TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND}
        -DKISAK_EXECUTABLE=$<TARGET_FILE:${PROJECT_NAME}>
        -DKISAK_CONFIGURATION=$<CONFIG>
        -P ${SCRIPTS_DIR}/verify_release_imports.cmake
        COMMENT "VERIFYING RELEASE RUNTIME IMPORTS"
        VERBATIM
)
