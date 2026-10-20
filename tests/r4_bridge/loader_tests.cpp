/*
 * Run from the repository root (the fake Windows header is used only here):
 * g++ -std=c++11 -Wall -Wextra -Werror -Itests/r4_bridge/fake_win32 \
 *   tests/r4_bridge/loader_tests.cpp xr_3da/KichR4Probe.cpp \
 *   xr_3da/KichR4ResourceProbe.cpp -o /tmp/kich_r4_loader_tests
 * /tmp/kich_r4_loader_tests
 * This does not emulate a GPU or validate MSVC SEH / Windows driver behavior.
 */
#include <windows.h>
#include <fenv.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <stdexcept>
#include <string>
#include <vector>
#include "../../xr_3da/KichR4Bridge.h"
#include "../../xr_3da/KichR4Probe.h"
#include "../../xr_3da/KichR4Resources.h"

static_assert(sizeof(KichR4ProbeInfo) == 164, "ABI size");
static_assert(offsetof(KichR4ProbeInfo, adapter_name) == 36, "ABI offset");

namespace
{
struct Fixture
{
    bool available;
    bool long_path;
    bool change_fpu;
    std::string missing_export;
    uint32_t version;
    int32_t supported;
    int32_t copied;
    KichR4ProbeInfo info;
    int loads;
    int unloads;
    int version_calls;
    int probe_calls;
    int info_calls;
    int resource_api_calls;
    std::wstring loaded_path;
    std::vector<std::string> logs;
};
Fixture f;
int checks = 0;

void Require(bool condition, const char* message)
{
    ++checks;
    if (!condition)
        throw std::runtime_error(message);
}

void Reset()
{
    f = Fixture();
    f.available = true;
    f.version = KICH_R4_BRIDGE_ABI_VERSION;
    f.supported = 1;
    f.copied = 1;
    f.info.struct_size = sizeof(f.info);
    f.info.abi_version = f.version;
    f.info.status = KICH_R4_PROBE_SUPPORTED;
    f.info.feature_level = KICH_R4_REQUIRED_FEATURE_LEVEL;
    f.info.flags = KICH_R4_PROBE_ADAPTER_INFO;
    memcpy(f.info.adapter_name, "Fake GPU", sizeof("Fake GPU"));
}

void Log(const char* text) { f.logs.push_back(text); }
bool HasLog(const char* fragment)
{
    for (size_t i = 0; i < f.logs.size(); ++i)
        if (f.logs[i].find(fragment) != std::string::npos)
            return true;
    return false;
}
void Run(const char* args = "game.exe -kich_r4_probe")
{
    KichR4_RunHardwareProbeIfRequested(args, Log);
}

uint32_t FakeVersion() { ++f.version_calls; return f.version; }
int32_t FakeProbe()
{
    ++f.probe_calls;
    if (f.change_fpu)
        fesetround(FE_UPWARD);
    return f.supported;
}
int32_t FakeInfo(KichR4ProbeInfo* info, uint32_t bytes)
{
    ++f.info_calls;
    Require(bytes == sizeof(*info), "caller passes exact buffer size");
    *info = f.info;
    return f.copied;
}
int32_t FakeResourceAPI(uint32_t version, KichR4ResourceAPI* api, uint32_t bytes)
{
    ++f.resource_api_calls;
    Require(version == KICH_R4_RESOURCE_ABI && bytes == sizeof(*api), "resource ABI request");
    if (f.change_fpu) fesetround(FE_UPWARD);
    memset(api, 0, sizeof(*api)); /* Malformed table must be rejected before calls. */
    return KICH_R4_OK;
}

void TestInactive()
{
    const char* cases[] = {NULL, "", "game.exe", "game.exe -kich_r4_probe_extra",
        "game.exe prefix-kich_r4_probe", "\"C:\\folder -kich_r4_probe\\game.exe\"",
        "game.exe \"unterminated -kich_r4_probe", "game.exe -kich_r4_resources_extra",
        "game.exe prefix-kich_r4_resources", "\"C:\\folder -kich_r4_resources\\game.exe\""};
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
    {
        Reset();
        Run(cases[i]);
        Require(f.loads == 0 && f.logs.empty(), "no opt-in means no DLL load or logs");
    }
    Reset();
    KichR4_RunHardwareProbeIfRequested("-kich_r4_probe", NULL);
    Require(f.loads == 0, "NULL callback is safe");
}

void TestSuccess()
{
    const char* cases[] = {"game.exe -kich_r4_probe", "-kich_r4_probe",
        "\"C:\\my game\\game.exe\"\t\"-kich_r4_probe\" -r2"};
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
    {
        Reset();
        Run(cases[i]);
        Require(f.loads == 1 && f.unloads == 1, "DLL released after success");
        Require(f.loaded_path == L"C:\\game\\bin\\xrRender_R4.dll", "DLL path uses EXE directory");
        Require(f.version_calls == 1 && f.probe_calls == 1 && f.info_calls == 1, "one handshake");
        Require(HasLog("PASS:") && HasLog("Fake GPU"), "success and hardware logged");
        Require(HasLog("continuing normal R1/R2 selection"), "legacy selection continues");
    }
}

void TestFailures()
{
    Reset();
    f.available = false;
    Run();
    Require(f.loads == 1 && f.unloads == 0 && f.probe_calls == 0, "missing DLL is non-fatal");
    Require(HasLog("DLL unavailable") && HasLog("continuing normal"), "missing DLL logged");

    Reset();
    f.long_path = true;
    Run();
    Require(f.loads == 0 && HasLog("too long"), "truncated EXE path cannot load another DLL");

    const char* names[] = {KICH_R4_EXPORT_VERSION, KICH_R4_EXPORT_PROBE, KICH_R4_EXPORT_INFO};
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
    {
        Reset();
        f.missing_export = names[i];
        Run();
        Require(f.version_calls == 0 && f.probe_calls == 0 && f.unloads == 1, "missing export skips calls and unloads");
        Require(HasLog("required exports missing"), "missing export logged");
    }

    Reset();
    ++f.version;
    Run();
    Require(f.probe_calls == 0 && f.info_calls == 0 && f.unloads == 1, "wrong ABI skips hardware probe");
    Require(HasLog("ABI mismatch"), "ABI mismatch logged");

    for (int status = KICH_R4_PROBE_RUNTIME_MISSING; status <= KICH_R4_PROBE_DEVICE_FAILED; ++status)
    {
        Reset();
        f.supported = 0;
        f.info.status = status;
        f.info.hresult = static_cast<int32_t>(UINT32_C(0x80004005));
        f.info.feature_level = 0;
        f.info.flags = 0;
        Run();
        Require(HasLog("hardware=no") && !HasLog("PASS:"), "unsupported runtime/hardware is not success");
        Require(f.unloads == 1 && HasLog("continuing normal"), "unsupported hardware keeps fallback");
    }
}

void TestMalformedReports()
{
    for (int i = 0; i < 8; ++i)
    {
        Reset();
        switch (i)
        {
        case 0: f.copied = 0; break;
        case 1: --f.info.struct_size; break;
        case 2: ++f.info.abi_version; break;
        case 3: f.supported = 2; break;
        case 4: f.info.status = KICH_R4_PROBE_DEVICE_FAILED; break;
        case 5: f.info.hresult = static_cast<int32_t>(UINT32_C(0x80004005)); break;
        case 6: f.info.feature_level = UINT32_C(0xa100); break;
        case 7: f.supported = 0; break;
        }
        Run();
        Require(HasLog("invalid probe report") && !HasLog("PASS:"), "malformed report rejected");
        Require(f.unloads == 1, "DLL released after malformed report");
    }

    Reset();
    memset(f.info.adapter_name, '%', sizeof(f.info.adapter_name));
    Run();
    Require(HasLog("PASS:"), "unterminated adapter name is clamped, never used as a format");
}

void TestFpuPreserved()
{
    Reset();
    const int original = fegetround();
    Require(fesetround(FE_DOWNWARD) == 0, "set preexisting engine rounding");
    f.change_fpu = true;
    Run();
    const int restored = fegetround();
    fesetround(original);
    Require(restored == FE_DOWNWARD, "probe preserves preexisting engine FPU state");
}

void TestResourceSwitch()
{
    Reset();Run();
    Require(!f.resource_api_calls && !HasLog("[R4 bridge B2]"), "B0 switch alone never allocates B2 resources");
    const char* args[] = {"game.exe -kich_r4_resources", "game.exe -kich_r4_probe -kich_r4_resources",
        "game.exe \"-kich_r4_resources\""};
    for (size_t i=0;i<sizeof(args)/sizeof(args[0]);++i) {
        Reset();Run(args[i]);
        Require(f.probe_calls==1 && f.resource_api_calls==1 && f.unloads==1, "B2 switch implies B0 and runs one resource handshake");
        Require(HasLog("ABI/table mismatch") && HasLog("continuing normal"), "bad B2 API keeps fallback");
    }
    Reset();f.missing_export=KICH_R4_EXPORT_RESOURCES;Run("game.exe -kich_r4_resources");
    Require(!f.resource_api_calls && f.unloads==1 && HasLog("resource export missing"), "B0 DLL without B2 export remains loadable");
    Reset();++f.version;Run("game.exe -kich_r4_resources");
    Require(!f.probe_calls && !f.resource_api_calls && f.unloads==1, "B0 ABI mismatch prevents B2 calls");
    Reset();f.supported=0;f.info.status=KICH_R4_PROBE_DEVICE_FAILED;
    Run("game.exe -kich_r4_resources");
    Require(!f.resource_api_calls && f.unloads==1, "unsupported hardware skips B2 resources");
    Reset();f.change_fpu=true;const int original=fegetround();fesetround(FE_DOWNWARD);
    Run("game.exe -kich_r4_resources");const int restored=fegetround();fesetround(original);
    Require(restored==FE_DOWNWARD && f.resource_api_calls==1, "FPU is restored after B2 too");
}
}

DWORD GetModuleFileNameW(HMODULE, wchar_t* path, DWORD capacity)
{
    const wchar_t name[] = L"C:\\game\\bin\\XR_3DA.exe";
    if (f.long_path)
        return capacity;
    wmemcpy(path, name, sizeof(name) / sizeof(name[0]));
    return static_cast<DWORD>(wcslen(name));
}
HMODULE LoadLibraryW(const wchar_t* path)
{
    ++f.loads;
    f.loaded_path = path;
    return f.available ? reinterpret_cast<HMODULE>(1) : NULL;
}
BOOL FreeLibrary(HMODULE) { ++f.unloads; return 1; }
DWORD GetLastError(void) { return 126; }
FARPROC GetProcAddress(HMODULE, const char* name)
{
    if (f.missing_export == name)
        return NULL;
    if (!strcmp(name, KICH_R4_EXPORT_VERSION))
        return reinterpret_cast<FARPROC>(FakeVersion);
    if (!strcmp(name, KICH_R4_EXPORT_PROBE))
        return reinterpret_cast<FARPROC>(FakeProbe);
    if (!strcmp(name, KICH_R4_EXPORT_INFO))
        return reinterpret_cast<FARPROC>(FakeInfo);
    if (!strcmp(name, KICH_R4_EXPORT_RESOURCES))
        return reinterpret_cast<FARPROC>(FakeResourceAPI);
    return NULL;
}

int main()
{
    try
    {
        TestInactive();
        TestSuccess();
        TestFailures();
        TestMalformedReports();
        TestFpuPreserved();
        TestResourceSwitch();
        printf("B0 loader: %d checks passed\n", checks);
        return 0;
    }
    catch (const std::exception& e)
    {
        fprintf(stderr, "B0 loader FAILED: %s\n", e.what());
        return 1;
    }
}
