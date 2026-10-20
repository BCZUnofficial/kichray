/* Standalone B0 diagnostic DLL. Never includes stdafx.h, HW9, or xrCore. */
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <string.h>
#include "../KichR4Bridge.h"

#if !defined(_M_IX86) && !defined(__i386__)
#error The Kichray B0 bridge must be built for Win32/x86.
#endif

namespace
{
KichR4ProbeInfo last_probe = {
    sizeof(KichR4ProbeInfo), KICH_R4_BRIDGE_ABI_VERSION, KICH_R4_PROBE_NOT_RUN,
    0, 0, 0, 0, 0, 0, {0}
};

typedef HRESULT (WINAPI *D3D11CreateDeviceFn)(IDXGIAdapter*, D3D_DRIVER_TYPE,
    HMODULE, UINT, const D3D_FEATURE_LEVEL*, UINT, UINT, ID3D11Device**,
    D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);

HMODULE LoadSystemD3D11()
{
    wchar_t path[MAX_PATH];
    const UINT length = GetSystemDirectoryW(path, MAX_PATH);
    const wchar_t filename[] = L"\\d3d11.dll";
    if (!length)
        return NULL;
    if (length + sizeof(filename) / sizeof(filename[0]) > MAX_PATH)
    {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return NULL;
    }
    memcpy(path + length, filename, sizeof(filename));
    return LoadLibraryW(path);
}

void ReadAdapterInfo(ID3D11Device* device)
{
    IDXGIDevice* dxgi_device = NULL;
    IDXGIAdapter* adapter = NULL;
    HRESULT result = device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgi_device));
    if (SUCCEEDED(result))
    {
        result = dxgi_device->GetAdapter(&adapter);
        if (SUCCEEDED(result))
        {
            DXGI_ADAPTER_DESC desc = {};
            if (SUCCEEDED(adapter->GetDesc(&desc)))
            {
                last_probe.vendor_id = desc.VendorId;
                last_probe.device_id = desc.DeviceId;
                last_probe.dedicated_video_memory_mb = static_cast<uint32_t>(desc.DedicatedVideoMemory / (1024 * 1024));
                if (!WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1,
                    last_probe.adapter_name, sizeof(last_probe.adapter_name), NULL, NULL))
                {
                    memcpy(last_probe.adapter_name, "<name unavailable>", sizeof("<name unavailable>"));
                }
                last_probe.flags |= KICH_R4_PROBE_ADAPTER_INFO;
            }
            adapter->Release();
        }
        dxgi_device->Release();
    }
}
}

/* Export names are fixed by xrRender_R4.def, including on x86. */
extern "C" uint32_t KICH_R4_CALL KichR4_GetBridgeVersion(void)
{
    return KICH_R4_BRIDGE_ABI_VERSION;
}

extern "C" int32_t KICH_R4_CALL KichR4_ProbeHardware(void)
{
    memset(&last_probe, 0, sizeof(last_probe));
    last_probe.struct_size = sizeof(last_probe);
    last_probe.abi_version = KICH_R4_BRIDGE_ABI_VERSION;

    HMODULE runtime = LoadSystemD3D11();
    if (!runtime)
    {
        last_probe.status = KICH_R4_PROBE_RUNTIME_MISSING;
        last_probe.hresult = HRESULT_FROM_WIN32(GetLastError());
        return 0;
    }

    const D3D11CreateDeviceFn create_device =
        reinterpret_cast<D3D11CreateDeviceFn>(GetProcAddress(runtime, "D3D11CreateDevice"));
    if (!create_device)
    {
        last_probe.status = KICH_R4_PROBE_ENTRYPOINT_MISSING;
        last_probe.hresult = HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
        FreeLibrary(runtime);
        return 0;
    }

    const D3D_FEATURE_LEVEL requested[] = {D3D_FEATURE_LEVEL_11_0};
    ID3D11Device* device = NULL;
    ID3D11DeviceContext* context = NULL;
    D3D_FEATURE_LEVEL actual_level = static_cast<D3D_FEATURE_LEVEL>(0);
    const HRESULT result = create_device(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0,
        requested, 1, D3D11_SDK_VERSION, &device, &actual_level, &context);
    last_probe.feature_level = static_cast<uint32_t>(actual_level);
    const bool supported = SUCCEEDED(result) && device && context && actual_level >= D3D_FEATURE_LEVEL_11_0;
    last_probe.hresult = !supported && SUCCEEDED(result) ? E_UNEXPECTED : result;
    last_probe.status = supported ? KICH_R4_PROBE_SUPPORTED : KICH_R4_PROBE_DEVICE_FAILED;
    if (supported)
        ReadAdapterInfo(device);

    /* All graphics references are gone before either DLL is unloaded or DX9 starts. */
    if (context)
        context->Release();
    if (device)
        device->Release();
    FreeLibrary(runtime);
    return supported ? 1 : 0;
}

extern "C" int32_t KICH_R4_CALL KichR4_GetProbeInfo(KichR4ProbeInfo* info, uint32_t info_size)
{
    if (!info || info_size < sizeof(last_probe))
        return 0;
    memcpy(info, &last_probe, sizeof(last_probe));
    return 1;
}

BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID)
{
    /* No graphics calls, engine globals, logging, or DLL loads under loader lock. */
    return TRUE;
}
