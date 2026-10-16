#pragma once

// Included only by log.cpp. WinAPI resources are initialized before a crash.
#include <dbghelp.h>

namespace kichgelkhut_diagnostics
{
typedef BOOL (WINAPI* DumpFn)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE,
    PMINIDUMP_EXCEPTION_INFORMATION, PMINIDUMP_USER_STREAM_INFORMATION,
    PMINIDUMP_CALLBACK_INFORMATION);
static DumpFn dump_fn;
static HMODULE dbghelp_module;
static HANDLE request_event, done_event, dump_thread;
static volatile LONG stopping, requested;
static char directory[MAX_PATH], dump_path[MAX_PATH], dump_status[MAX_PATH + 128];
static EXCEPTION_RECORD exception_record;
static CONTEXT exception_context;
static EXCEPTION_POINTERS exception_pointers;
static DWORD exception_thread;
static DWORD WINAPI dump_writer(void*)
{
    WaitForSingleObject(request_event, INFINITE);
    if (InterlockedCompareExchange(&stopping, 0, 0)) return 0;
    HANDLE file = CreateFileA(dump_path, GENERIC_WRITE, FILE_SHARE_READ, NULL,
        CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
        sprintf_s(dump_status, "[DUMP] Cannot create file (Windows error %lu): %s",
            GetLastError(), dump_path);
    else
    {
        MINIDUMP_EXCEPTION_INFORMATION info;
        info.ThreadId = exception_thread;
        info.ExceptionPointers = &exception_pointers;
        info.ClientPointers = FALSE;
        // Stack/module data and address-space layout, without a full heap dump.
        const MINIDUMP_TYPE flags = static_cast<MINIDUMP_TYPE>(
            MiniDumpNormal | MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules |
            MiniDumpWithFullMemoryInfo);
        BOOL ok = dump_fn(GetCurrentProcess(), GetCurrentProcessId(), file,
            flags, &info, NULL, NULL);
        DWORD error = ok ? ERROR_SUCCESS : GetLastError();
        if (!ok && error == ERROR_INVALID_PARAMETER)
        {
            // Older DbgHelp may not support the extended flags.
            SetFilePointer(file, 0, NULL, FILE_BEGIN);
            SetEndOfFile(file);
            ok = dump_fn(GetCurrentProcess(), GetCurrentProcessId(), file,
                MiniDumpNormal, &info, NULL, NULL);
            error = ok ? ERROR_SUCCESS : GetLastError();
        }
        FlushFileBuffers(file);
        CloseHandle(file);
        if (ok) sprintf_s(dump_status, "[DUMP] Saved: %s", dump_path);
        else
        {
            DeleteFileA(dump_path);
            sprintf_s(dump_status, "[DUMP] MiniDumpWriteDump failed (Windows error %lu)", error);
        }
    }
    SetEvent(done_event);
    return 0;
}

static void start()
{
    DWORD length = GetModuleFileNameA(NULL, directory, MAX_PATH);
    if (!length || length >= MAX_PATH) return;
    char* slash = strrchr(directory, '\\');
    if (!slash || static_cast<size_t>(slash - directory) + 96 >= MAX_PATH) return;
    strcpy_s(slash + 1, MAX_PATH - (slash + 1 - directory), "logs");
    if (!CreateDirectoryA(directory, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) return;
    dbghelp_module = LoadLibraryA("dbghelp.dll");
    if (dbghelp_module) dump_fn = reinterpret_cast<DumpFn>(
        GetProcAddress(dbghelp_module, "MiniDumpWriteDump"));
    if (!dump_fn) return;
    stopping = requested = 0;
    dump_status[0] = 0;
    request_event = CreateEventA(NULL, FALSE, FALSE, NULL);
    done_event = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (request_event && done_event)
        dump_thread = CreateThread(NULL, 256 * 1024, dump_writer, NULL, 0, NULL);
    if (!dump_thread)
    {
        if (request_event) CloseHandle(request_event);
        if (done_event) CloseHandle(done_event);
        request_event = done_event = NULL;
    }
}

static bool capture(EXCEPTION_POINTERS* exception)
{
    if (!dump_thread || !exception || !exception->ExceptionRecord || !exception->ContextRecord)
    {
        strcpy_s(dump_status, "[DUMP] Writer unavailable or no Windows exception context.");
        return true;
    }
    // One attempt per process: recursion never starts a second DbgHelp call.
    if (InterlockedCompareExchange(&requested, 1, 0)) return false;
    exception_record = *exception->ExceptionRecord;
    exception_record.ExceptionRecord = NULL;
    exception_context = *exception->ContextRecord;
    exception_pointers.ExceptionRecord = &exception_record;
    exception_pointers.ContextRecord = &exception_context;
    exception_thread = GetCurrentThreadId();
    SYSTEMTIME now;
    GetLocalTime(&now);
    sprintf_s(dump_path, "%s\\crash_%04u%02u%02u_%02u%02u%02u_%03u_pid%lu.dmp",
        directory, now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute,
        now.wSecond, now.wMilliseconds, GetCurrentProcessId());
    SetEvent(request_event);
    if (WaitForSingleObject(done_event, 15000) == WAIT_OBJECT_0) return true;
    // The writer may own DbgHelp locks. The exception filter must now exit.
    return false;
}

static void stop()
{
    if (dump_thread)
    {
        InterlockedExchange(&stopping, 1);
        SetEvent(request_event);
        // Never unload a DLL underneath a stalled dump writer.
        if (WaitForSingleObject(dump_thread, 1000) != WAIT_OBJECT_0) return;
        CloseHandle(dump_thread);
        CloseHandle(request_event);
        CloseHandle(done_event);
        dump_thread = request_event = done_event = NULL;
    }
    if (dbghelp_module) FreeLibrary(dbghelp_module);
    dbghelp_module = NULL;
    dump_fn = NULL;
}
}

bool KichgelkhutWriteMiniDump(EXCEPTION_POINTERS* exception)
{
    const bool completed = kichgelkhut_diagnostics::capture(exception);
    const char* status = completed ? kichgelkhut_diagnostics::dump_status :
        "[DUMP] Writer timed out or recursive crash; skipping further DbgHelp calls.";
    // Preserve detailed assertion text already recorded before DEBUG_INVOKE.
    HANDLE file = CreateFileA(kichgelkhut_log::crash_path, FILE_APPEND_DATA,
        FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file != INVALID_HANDLE_VALUE)
    {
        kichgelkhut_log::write_text(file, "\r\n");
        kichgelkhut_log::write_text(file, status);
        kichgelkhut_log::write_text(file, "\r\n");
        FlushFileBuffers(file);
        CloseHandle(file);
    }
    return completed;
}
