#pragma once

struct IDirect3DDevice9;
struct ID3D11Device;
struct _LUID;
typedef struct _LUID LUID;

// Runs a one-time, non-invasive proof that:
// 1. Direct3DCreate9Ex is available.
// 2. A D3D9Ex adapter matches the OpenXR-requested DXGI adapter LUID.
// 3. A D3D9Ex render-target texture can be opened and read by D3D11.
// 4. The currently running game D3D9 device appears to use the same adapter.
//
// This creates a temporary D3D9Ex device and does not replace the game's
// existing D3D9 renderer.
bool VR_ProbeD3D9ExD3D11Interop(
    IDirect3DDevice9* gameD3D9Device,
    ID3D11Device* openXrD3D11Device,
    const LUID& openXrAdapterLuid);
