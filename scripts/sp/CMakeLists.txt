message("----- KisakCOD Client(SP) -----")

project(KisakCOD-sp)

# Include all common files between mp/sp/dedi
include(${SCRIPTS_DIR}/common_files.cmake)

# Include all files specific to mp
include(sp_files.cmake)

# Create the source groups
include(${SCRIPTS_DIR}/source_groups.cmake)

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


# KISAK_OPENXR_SP_BEGIN
target_sources(
    ${PROJECT_NAME}
    PRIVATE
        ${SRC_DIR}/vr/vr_openxr.cpp
        ${SRC_DIR}/vr/vr_calibration.h
        ${SRC_DIR}/vr/vr_hud_layout.cpp
        ${SRC_DIR}/vr/vr_hud_layout.h
        ${SRC_DIR}/vr/vr_input_bindings.cpp
        ${SRC_DIR}/vr/vr_input_bindings.h
        ${SRC_DIR}/vr/vr_interactions.h
        ${SRC_DIR}/vr/vr_openvr_input.cpp
        ${SRC_DIR}/vr/vr_openvr_input.h
        ${SRC_DIR}/vr/vr_openxr_profiles.cpp
        ${SRC_DIR}/vr/vr_openxr_profiles.h
        ${SRC_DIR}/vr/vr_prompt_labels.cpp
        ${SRC_DIR}/vr/vr_prompt_labels.h
        ${SRC_DIR}/vr/vr_weapon_profiles.cpp
        ${SRC_DIR}/vr/vr_weapon_profiles.h
        ${SRC_DIR}/vr/vr_d3d9_capture.cpp
        ${SRC_DIR}/vr/vr_d3d9ex_interop_probe.cpp
        ${SRC_DIR}/vr/vr_d3d9ex_interop_probe.h
        ${SRC_DIR}/vr/vr_openxr.h
        ${SRC_DIR}/vr/vr_d3d9_capture.h
        ${SRC_DIR}/win32/win_crash_diagnostics.cpp
        ${SRC_DIR}/win32/win_crash_diagnostics.h
)

target_link_libraries(
    ${PROJECT_NAME}
    PRIVATE
        OpenXR::openxr_loader
        OpenVR::openvr_api
        d3d11
        dxgi
        d3dcompiler
)

target_compile_definitions(
    ${PROJECT_NAME}
    PRIVATE
        KISAK_OPENXR_ENABLED=1
        XR_USE_PLATFORM_WIN32
        XR_USE_GRAPHICS_API_D3D11
)

if(MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 4)
    target_link_options(
        ${PROJECT_NAME}
        PRIVATE
            /LARGEADDRESSAWARE
    )
endif()
# KISAK_OPENXR_SP_END

target_compile_definitions(${PROJECT_NAME} PUBLIC KISAK_SP CINEMA USE_SEPARATE_BLIT_TEXTURE)

# We need all of the pre/post-build commands
include(${SCRIPTS_DIR}/pre_build.cmake)
include(${SCRIPTS_DIR}/post_build.cmake)

# Tracey
include(${SCRIPTS_DIR}/extern/tracy.cmake)
