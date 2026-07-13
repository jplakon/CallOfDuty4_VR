cmake_minimum_required(VERSION 3.16)

project(KisakCOD_OpenXR_Probe LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build only the static OpenXR loader.
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_LOADER ON CACHE BOOL "" FORCE)
set(DYNAMIC_LOADER OFF CACHE BOOL "" FORCE)
set(BUILD_API_LAYERS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_CONFORMANCE_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_SDK_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_SPECIFICATION OFF CACHE BOOL "" FORCE)

add_subdirectory(
    "${CMAKE_CURRENT_LIST_DIR}/../../deps/openxr-sdk"
    "${CMAKE_CURRENT_BINARY_DIR}/openxr-sdk"
    EXCLUDE_FROM_ALL
)

add_executable(xr_probe main.cpp)

target_link_libraries(
    xr_probe
    PRIVATE
    OpenXR::openxr_loader
)
