cmake_minimum_required(VERSION 3.16)

project(KisakCOD)

# Set the default C++ standard
set(CMAKE_CXX_STANDARD 20)

# Set this to match the folder name in scripts/platform
# This will handle the path overrides if needed
set(KISAK_PLATFORM win32)

# Set Address Sanitizer
# NOTE: If you use this you will need to copy to the game's directory
# clang_rt.asan_dbg_dynamic-i386.dll and clang_rt.asan_dynamic-i386.dll from the MSVC C directory located below (or similar)
# C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Tools\MSVC\14.44.35207\bin\Hostx86\x86
set(USE_ASAN OFF)

# Set the gloabl directory variables
set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(DEPS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/deps)
set(BIN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/bin)
set(SCRIPTS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/scripts)
set(PLATFORM_DIR ${SCRIPTS_DIR}/platform)


# KISAK_OPENXR_SDK_BEGIN
#
# Build the vendored Khronos OpenXR loader as a static Win32 library.
# Tests, API layers, and specification targets are unnecessary here.
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_LOADER ON CACHE BOOL "" FORCE)
set(DYNAMIC_LOADER OFF CACHE BOOL "" FORCE)
set(BUILD_API_LAYERS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_CONFORMANCE_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_SDK_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_SPECIFICATION OFF CACHE BOOL "" FORCE)

add_subdirectory(
    ${DEPS_DIR}/openxr-sdk
    ${CMAKE_BINARY_DIR}/openxr-sdk
    EXCLUDE_FROM_ALL
)

# KisakCOD uses the static MSVC runtime. Build the static OpenXR
# loader with the matching runtime to avoid RuntimeLibrary conflicts.
set_property(
    TARGET openxr_loader
    PROPERTY MSVC_RUNTIME_LIBRARY
    "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)
# KISAK_OPENXR_SDK_END

# Set the output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${BIN_DIR})

# Include global scripts/tricks
include(${SCRIPTS_DIR}/extern/increment_build.cmake)

# Include platform overrides
include(${SCRIPTS_DIR}/platform_override.cmake)

# Log out the KISAK_PLATFORM
message("Building KISAK_PLATFORM: ${KISAK_PLATFORM}")

# Include the subprojects
add_subdirectory(${SCRIPTS_DIR}/mp)
add_subdirectory(${SCRIPTS_DIR}/sp)
add_subdirectory(${SCRIPTS_DIR}/dedi)