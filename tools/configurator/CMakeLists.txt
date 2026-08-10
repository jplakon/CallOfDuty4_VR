project(KisakCOD-VR-Configurator LANGUAGES CXX)

add_executable(
    KisakCOD-VR-Configurator
    WIN32
    configurator.cpp
    compatibility_probe_win32.cpp
    compatibility_probe_win32.h
    settings_core.cpp
    settings_core.h
    ../../src/vr/vr_calibration.h
    ../../src/vr/vr_hud_layout.cpp
    ../../src/vr/vr_hud_layout.h
    ../../src/vr/vr_input_bindings.cpp
    ../../src/vr/vr_input_bindings.h
    ../../src/vr/vr_interactions.h
    ../../src/vr/vr_compatibility.cpp
    ../../src/vr/vr_compatibility.h
    ../../src/vr/vr_openxr_profiles.cpp
    ../../src/vr/vr_openxr_profiles.h
    ../../src/vr/vr_weapon_profiles.cpp
    ../../src/vr/vr_weapon_profiles.h
)

target_include_directories(
    KisakCOD-VR-Configurator
    PRIVATE
        ../../src
)

target_compile_features(
    KisakCOD-VR-Configurator
    PRIVATE
        cxx_std_20
)

target_compile_definitions(
    KisakCOD-VR-Configurator
    PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _WIN32_WINNT=0x0A00
)

target_link_libraries(
    KisakCOD-VR-Configurator
    PRIVATE
        comctl32
        comdlg32
        advapi32
        dxgi
        shell32
)

if(MSVC)
    set_property(
        TARGET KisakCOD-VR-Configurator
        PROPERTY MSVC_RUNTIME_LIBRARY
        "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )

    target_compile_options(
        KisakCOD-VR-Configurator
        PRIVATE
            /W4
            /permissive-
            /utf-8
    )
endif()

set_target_properties(
    KisakCOD-VR-Configurator
    PROPERTIES
        OUTPUT_NAME "KisakCOD-VR-Configurator"
)

add_executable(
    KisakCOD-VR-Configurator-Tests
    ../../tests/configurator_settings_tests.cpp
    settings_core.cpp
    settings_core.h
    ../../src/vr/vr_calibration.h
    ../../src/vr/vr_hud_layout.cpp
    ../../src/vr/vr_hud_layout.h
    ../../src/vr/vr_input_bindings.cpp
    ../../src/vr/vr_input_bindings.h
    ../../src/vr/vr_interactions.h
    ../../src/vr/vr_compatibility.cpp
    ../../src/vr/vr_compatibility.h
    ../../src/vr/vr_openvr_input.cpp
    ../../src/vr/vr_openvr_input.h
    ../../src/vr/vr_openxr_profiles.cpp
    ../../src/vr/vr_openxr_profiles.h
    ../../src/vr/vr_weapon_profiles.cpp
    ../../src/vr/vr_weapon_profiles.h
)

target_include_directories(
    KisakCOD-VR-Configurator-Tests
    PRIVATE
        ../../src
        ../../deps/openvr/headers
)

target_compile_features(
    KisakCOD-VR-Configurator-Tests
    PRIVATE
        cxx_std_20
)

if(MSVC)
    set_property(
        TARGET KisakCOD-VR-Configurator-Tests
        PROPERTY MSVC_RUNTIME_LIBRARY
        "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )

    target_compile_options(
        KisakCOD-VR-Configurator-Tests
        PRIVATE
            /W4
            /permissive-
            /utf-8
    )
endif()
