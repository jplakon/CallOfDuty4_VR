project(KisakCOD-VR-Input-Mapper LANGUAGES CXX)

add_executable(
    KisakCOD-VR-Input-Mapper
    WIN32
    main.cpp
    ../../src/vr/vr_input_bindings.cpp
    ../../src/vr/vr_input_bindings.h
    ../../src/vr/vr_openvr_input.cpp
    ../../src/vr/vr_openvr_input.h
    ../../src/vr/vr_openxr_profiles.cpp
    ../../src/vr/vr_openxr_profiles.h
)

target_compile_features(
    KisakCOD-VR-Input-Mapper
    PRIVATE
        cxx_std_20
)

target_include_directories(
    KisakCOD-VR-Input-Mapper
    PRIVATE
        ../../src
        ../../deps/openvr/headers
)

target_compile_definitions(
    KisakCOD-VR-Input-Mapper
    PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _WIN32_WINNT=0x0A00
        XR_USE_PLATFORM_WIN32
        XR_USE_GRAPHICS_API_D3D11
)

target_link_libraries(
    KisakCOD-VR-Input-Mapper
    PRIVATE
        OpenXR::openxr_loader
        OpenVR::openvr_api
        d3d11
        dxgi
        shell32
)

if(MSVC)
    set_property(
        TARGET KisakCOD-VR-Input-Mapper
        PROPERTY MSVC_RUNTIME_LIBRARY
        "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )

    target_compile_options(
        KisakCOD-VR-Input-Mapper
        PRIVATE
            /W4
            /permissive-
            /utf-8
    )
endif()

set_target_properties(
    KisakCOD-VR-Input-Mapper
    PROPERTIES
        OUTPUT_NAME "KisakCOD-VR-Input-Mapper"
)
