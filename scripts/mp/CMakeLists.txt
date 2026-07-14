message("----- KisakCOD Client(MP) -----")

# Set project name to mp
project(KisakCOD-mp)

# Include all common files between mp/sp/dedi
include(${SCRIPTS_DIR}/common_files.cmake)

# Include all files specific to mp
include(mp_files.cmake)

# Create the source groups
include(${SCRIPTS_DIR}/source_groups.cmake)

# message(${SRC_DIR})
include (${PLATFORM_DIR}/${KISAK_PLATFORM}/platform.cmake)

# Add the new executable
add_executable(${PROJECT_NAME} 
    ${SRCROOT} 
    ${DYNENTITY} 
    ${EFFECTSCORE} 
    ${AIM_ASSIST} 
    ${BGAME} 
    ${BINKLIB} 
    ${CGAME} 
    ${CGAME_MP} 
    ${CLIENT} 
    ${CLIENT_MP} 
    ${COMMON} 
    ${DATABASE} 
    ${DEVGUI} 
    ${GAME} 
    ${GAME_MP} 
    ${GFX_D3D} 
    ${GROUPVOICE} 
    ${GROUPVOICE_SPEEX} 
    ${MSSLIB} 
    ${ODE} 
    ${PHYSICS} 
    ${PHYSICS_ODE} 
    ${QCOMMON} 
    ${RAGDOLL} 
    ${SCRIPT} 
    ${SERVER} 
    ${SERVER_MP} 
    ${SOUND} 
    ${SPEEX} 
    ${STRINGED} 
    ${UI} 
    ${UI_MP} 
    ${UNIVERSAL} 
    ${WIN32} 
    ${XANIM} 
    ${ZLIB} 
    ${STEAM_HEADERS}
)


# KISAK_OPENXR_MP_BEGIN
target_sources(
    ${PROJECT_NAME}
    PRIVATE
        ${SRC_DIR}/vr/vr_openxr.cpp
        ${SRC_DIR}/vr/vr_d3d9ex_interop_probe.cpp
        ${SRC_DIR}/vr/vr_d3d9ex_interop_probe.h
        ${SRC_DIR}/vr/vr_openxr.h
)

target_link_libraries(
    ${PROJECT_NAME}
    PRIVATE
        OpenXR::openxr_loader
)
target_compile_definitions(
    ${PROJECT_NAME}
    PRIVATE
        KISAK_OPENXR_ENABLED=1
)
# KISAK_OPENXR_MP_END

# Add multiplayer flag
target_compile_definitions(${PROJECT_NAME} PUBLIC KISAK_MP CINEMA USE_SEPARATE_BLIT_TEXTURE)

# We need all of the pre/post-build commands
include(${SCRIPTS_DIR}/pre_build.cmake)
include(${SCRIPTS_DIR}/post_build.cmake)

if (USE_ASAN)
    target_compile_options(${PROJECT_NAME} PRIVATE /fsanitize=address)
    target_link_options(${PROJECT_NAME} PRIVATE /fsanitize=address /LARGEADDRESSAWARE)
else()
# Tracy
    include(${SCRIPTS_DIR}/extern/tracy.cmake)
endif()

# KISAK_OPENXR_D3D11_SOLID_FRAME_BEGIN
target_compile_definitions(
    ${PROJECT_NAME}
    PRIVATE
        XR_USE_PLATFORM_WIN32
        XR_USE_GRAPHICS_API_D3D11
)

target_link_libraries(
    ${PROJECT_NAME}
    PRIVATE
        d3d11
        dxgi
)
# KISAK_OPENXR_D3D11_SOLID_FRAME_END

# KISAK_OPENXR_D3D9_CAPTURE_BEGIN
target_sources(
    ${PROJECT_NAME}
    PRIVATE
        ../../src/vr/vr_d3d9_capture.cpp
)
# KISAK_OPENXR_D3D9_CAPTURE_END

# KISAK_LARGE_ADDRESS_AWARE_BEGIN
if(MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 4)
    target_link_options(
        ${PROJECT_NAME}
        PRIVATE
            /LARGEADDRESSAWARE
    )
endif()
# KISAK_LARGE_ADDRESS_AWARE_END
