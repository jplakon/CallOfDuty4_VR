if (NOT KISAK_CONFIGURATION STREQUAL "Release")
    return()
endif()

if (NOT EXISTS "${KISAK_EXECUTABLE}")
    message(FATAL_ERROR
        "Release import audit could not find ${KISAK_EXECUTABLE}")
endif()

# PE import-table names are stored as ASCII strings in the executable.  Limit
# the scan to D3DX-style DLL names, normalize case, and then enforce the exact
# retail June 2010 dependency used by this project.
file(
    STRINGS "${KISAK_EXECUTABLE}"
    KISAK_D3DX_IMPORTS
    REGEX "[dD]3[dD][xX]9[dD]?_[0-9]+\\.[dD][lL][lL]"
)
string(TOLOWER "${KISAK_D3DX_IMPORTS}" KISAK_D3DX_IMPORTS_LOWER)

if (KISAK_D3DX_IMPORTS_LOWER MATCHES "d3dx9d_[0-9]+\\.dll")
    message(FATAL_ERROR
        "Release executable imports a debug-only D3DX runtime: "
        "${KISAK_D3DX_IMPORTS_LOWER}")
endif()

if (NOT KISAK_D3DX_IMPORTS_LOWER MATCHES "d3dx9_43\\.dll")
    message(FATAL_ERROR
        "Release executable does not import the expected retail "
        "d3dx9_43.dll runtime. Found: ${KISAK_D3DX_IMPORTS_LOWER}")
endif()

message(STATUS
    "Release D3DX import verified: ${KISAK_D3DX_IMPORTS_LOWER}")
