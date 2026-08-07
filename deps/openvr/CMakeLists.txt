cmake_minimum_required(VERSION 3.16)

# Valve OpenVR SDK v2.15.6, commit
# 0924064316de3effbcd1acf1e309182a2deb1c05.
#
# KisakCOD links the public OpenVR client shim statically. The shim locates
# SteamVR through openvrpaths.vrpath and loads SteamVR's architecture-matched
# vrclient.dll at runtime. No SteamVR binary is redistributed by this target.
add_library(
    kisak_openvr_api
    STATIC
        src/openvr_api_public.cpp
        src/jsoncpp.cpp
        src/vrcore/dirtools_public.cpp
        src/vrcore/envvartools_public.cpp
        src/vrcore/pathtools_public.cpp
        src/vrcore/sharedlibtools_public.cpp
        src/vrcore/hmderrors_public.cpp
        src/vrcore/vrpathregistry_public.cpp
        src/vrcore/strtools_public.cpp
)

target_include_directories(
    kisak_openvr_api
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/headers
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/vrcore
)

target_compile_definitions(
    kisak_openvr_api
    PUBLIC
        OPENVR_BUILD_STATIC
    PRIVATE
        # Both the OpenVR client shim and the static OpenXR loader vendor
        # JsonCpp. Keep OpenVR's reviewed 1.6.5 implementation private so the
        # final executable never receives duplicate Json::* definitions.
        Json=OpenVRJson
        VR_API_PUBLIC
        VRCORE_NO_PLATFORM
)

if(WIN32)
    target_compile_definitions(
        kisak_openvr_api
        PRIVATE
            WIN32
    )
endif()

set_property(
    TARGET kisak_openvr_api
    PROPERTY MSVC_RUNTIME_LIBRARY
    "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)

add_library(OpenVR::openvr_api ALIAS kisak_openvr_api)
