/* Deliberately built without the engine/D3D9 precompiled header. */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <fenv.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include "KichR4Bridge.h"
#include "KichR4Probe.h"
#include "KichR4ResourceProbe.h"
#include "KichR4FrameProbe.h"

namespace
{
bool HasSwitch(const char* command_line, const char* flag)
{
    if (!command_line)
        return false;

    const size_t flag_length = strlen(flag);
    while (*command_line)
    {
        while (*command_line == ' ' || *command_line == '\t')
            ++command_line;

        bool quoted = false;
        size_t length = 0;
        bool matches = true;
        while (*command_line && (quoted || (*command_line != ' ' && *command_line != '\t')))
        {
            const char ch = *command_line++;
            if (ch == '"')
                quoted = !quoted;
            else
            {
                if (length >= flag_length || ch != flag[length])
                    matches = false;
                ++length;
            }
        }
        if (matches && !quoted && length == flag_length)
            return true;
    }
    return false;
}

void WriteLog(KichR4LogFn log, const char* format, ...)
{
    char text[768];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    text[sizeof(text) - 1] = 0;
    log(text);
}

const char* StatusName(int32_t status)
{
    switch (status)
    {
    case KICH_R4_PROBE_NOT_RUN: return "not run";
    case KICH_R4_PROBE_SUPPORTED: return "supported";
    case KICH_R4_PROBE_RUNTIME_MISSING: return "D3D11 runtime unavailable";
    case KICH_R4_PROBE_ENTRYPOINT_MISSING: return "D3D11CreateDevice missing";
    case KICH_R4_PROBE_DEVICE_FAILED: return "hardware FL11_0 device unavailable";
    default: return "invalid status";
    }
}

/* Keep SEH scopes free of C++ objects requiring stack unwinding (MSVC C2712). */
HMODULE LoadBridge(const wchar_t* path, DWORD* error, DWORD* exception)
{
    HMODULE module = NULL;
#if defined(_MSC_VER)
    __try
#endif
    {
        module = LoadLibraryW(path);
        if (!module)
            *error = GetLastError();
    }
#if defined(_MSC_VER)
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        *exception = GetExceptionCode();
    }
#else
    (void)exception;
#endif
    return module;
}

bool ReadVersion(KichR4GetBridgeVersionFn get_version, uint32_t* version, DWORD* exception)
{
#if defined(_MSC_VER)
    __try
#endif
    {
        *version = get_version();
        return true;
    }
#if defined(_MSC_VER)
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        *exception = GetExceptionCode();
        return false;
    }
#else
    (void)exception;
#endif
}

bool ReadProbe(KichR4ProbeHardwareFn probe, KichR4GetProbeInfoFn get_info,
    int32_t* supported, int32_t* copied, KichR4ProbeInfo* info, DWORD* exception)
{
#if defined(_MSC_VER)
    __try
#endif
    {
        *supported = probe();
        *copied = get_info(info, sizeof(*info));
        return true;
    }
#if defined(_MSC_VER)
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        *exception = GetExceptionCode();
        return false;
    }
#else
    (void)exception;
#endif
}

void UnloadBridge(HMODULE module, KichR4LogFn log)
{
#if defined(_MSC_VER)
    __try
#endif
    {
        if (!FreeLibrary(module))
            WriteLog(log, "[R4 bridge B0] FreeLibrary failed: Win32=%lu", GetLastError());
    }
#if defined(_MSC_VER)
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        WriteLog(log, "[R4 bridge B0] DLL unload exception: 0x%08lX", GetExceptionCode());
    }
#endif
}

void CheckBridge(HMODULE module, KichR4LogFn log, bool resources, bool frame, bool* can_unload)
{
    const KichR4GetBridgeVersionFn get_version =
        reinterpret_cast<KichR4GetBridgeVersionFn>(GetProcAddress(module, KICH_R4_EXPORT_VERSION));
    const KichR4ProbeHardwareFn probe =
        reinterpret_cast<KichR4ProbeHardwareFn>(GetProcAddress(module, KICH_R4_EXPORT_PROBE));
    const KichR4GetProbeInfoFn get_info =
        reinterpret_cast<KichR4GetProbeInfoFn>(GetProcAddress(module, KICH_R4_EXPORT_INFO));
    if (!get_version || !probe || !get_info)
    {
        WriteLog(log, "[R4 bridge B0] required exports missing: version=%d probe=%d info=%d",
            get_version != NULL, probe != NULL, get_info != NULL);
        return;
    }

    DWORD exception = 0;
    uint32_t version = 0;
    if (!ReadVersion(get_version, &version, &exception))
    {
        WriteLog(log, "[R4 bridge B0] version export exception: 0x%08lX", exception);
        return;
    }
    if (version != KICH_R4_BRIDGE_ABI_VERSION)
    {
        WriteLog(log, "[R4 bridge B0] ABI mismatch: engine=0x%08X DLL=0x%08X; probe skipped",
            KICH_R4_BRIDGE_ABI_VERSION, version);
        return;
    }
    WriteLog(log, "[R4 bridge B0] ABI OK: 0x%08X", version);

    int32_t supported = 0;
    int32_t copied = 0;
    KichR4ProbeInfo info = {};
    if (!ReadProbe(probe, get_info, &supported, &copied, &info, &exception))
    {
        WriteLog(log, "[R4 bridge B0] probe export exception: 0x%08lX", exception);
        return;
    }
    if (copied != 1 || info.struct_size != sizeof(info) || info.abi_version != version ||
        (supported != 0 && supported != 1) ||
        (supported == 1 && (info.status != KICH_R4_PROBE_SUPPORTED || info.hresult < 0 ||
            info.feature_level < KICH_R4_REQUIRED_FEATURE_LEVEL)) ||
        (supported == 0 && (info.status < KICH_R4_PROBE_RUNTIME_MISSING ||
            info.status > KICH_R4_PROBE_DEVICE_FAILED)))
    {
        WriteLog(log, "[R4 bridge B0] invalid probe report; result rejected");
        return;
    }
    info.adapter_name[sizeof(info.adapter_name) - 1] = 0;
    WriteLog(log, "[R4 bridge B0] hardware=%s; status=%s; HRESULT=0x%08X; feature_level=0x%04X",
        supported == 1 ? "yes" : "no", StatusName(info.status),
        static_cast<uint32_t>(info.hresult), info.feature_level);
    if (info.flags & KICH_R4_PROBE_ADAPTER_INFO)
    {
        WriteLog(log, "[R4 bridge B0] adapter=%s; vendor=0x%04X device=0x%04X; dedicated VRAM=%u MiB",
            info.adapter_name, info.vendor_id, info.device_id, info.dedicated_video_memory_mb);
    }
    if (supported == 1)
    {
        WriteLog(log, "[R4 bridge B0] PASS: temporary hardware FL11_0 device created and released");
        if (resources)
            KichR4_CheckResourceBridge(module, log);
        if (frame)
            *can_unload = KichR4_CheckFrameBridge(module, log);
    }
}

void RunProbe(KichR4LogFn log, bool resources, bool frame)
{
    WriteLog(log, frame ? "[R4 bridge B0] begin: hardware diagnostics before the requested B3 window" :
        "[R4 bridge B0] begin: diagnostics only; no DX11 scene or swap chain");
    wchar_t path[MAX_PATH];
    const DWORD length = GetModuleFileNameW(NULL, path, MAX_PATH);
    if (!length || length >= MAX_PATH)
    {
        WriteLog(log, "[R4 bridge B0] executable path unavailable or too long; probe skipped");
        return;
    }
    wchar_t* filename = wcsrchr(path, L'\\');
    if (!filename)
    {
        WriteLog(log, "[R4 bridge B0] executable directory unavailable; probe skipped");
        return;
    }
    ++filename;
    const wchar_t dll_name[] = L"xrRender_R4.dll";
    const size_t prefix_length = static_cast<size_t>(filename - path);
    if (prefix_length + sizeof(dll_name) / sizeof(dll_name[0]) > MAX_PATH)
    {
        WriteLog(log, "[R4 bridge B0] DLL path too long; probe skipped");
        return;
    }
    memcpy(filename, dll_name, sizeof(dll_name));

    DWORD error = 0;
    DWORD exception = 0;
    HMODULE module = LoadBridge(path, &error, &exception);
    if (!module)
    {
        WriteLog(log, "[R4 bridge B0] DLL unavailable: Win32=%lu exception=0x%08lX; probe skipped",
            error, exception);
        return;
    }
    bool can_unload = true;
    CheckBridge(module, log, resources, frame, &can_unload);
    if (can_unload)
        UnloadBridge(module, log);
    else
        WriteLog(log, "[R4 bridge B3] cleanup incomplete; DLL retained until process exit; continuing R1/R2");
}
}

void KichR4_RunHardwareProbeIfRequested(const char* command_line, KichR4LogFn log)
{
    const bool resources = HasSwitch(command_line, "-kich_r4_resources");
    const bool frame = HasSwitch(command_line, "-kich_r4_frame");
    if (!log || (!frame && !resources && !HasSwitch(command_line, "-kich_r4_probe")))
        return;

    /* D3D/driver initialization must not alter the legacy engine's FPU setup. */
    fenv_t saved_environment;
    const bool saved = fegetenv(&saved_environment) == 0;
    RunProbe(log, resources, frame);
    if (saved)
        fesetenv(&saved_environment);
    WriteLog(log, "[R4 bridge B0] end: continuing normal R1/R2 selection");
}
