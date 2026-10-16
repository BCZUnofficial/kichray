#pragma once

// Included only by log.cpp. No engine allocator or filesystem on the writer thread.
namespace kichgelkhut_log
{
enum { line_size = 1152, queue_size = 1024, history_size = 256 };
static char queue[queue_size][line_size];
static char history[history_size][line_size];
static unsigned head, count, history_head, history_count, dropped;
static CRITICAL_SECTION lock;
static HANDLE wake = NULL, worker = NULL, stable = INVALID_HANDLE_VALUE;
static volatile LONG running = 0, stopping = 0, crash_busy = 0;
static char crash_path[MAX_PATH];
static char crash_history[history_size][line_size];
static DWORD last_report_tick;
static bool report_written;

static bool write_bytes(HANDLE file, const char* text, DWORD length)
{
    while (length)
    {
        DWORD written = 0;
        if (!WriteFile(file, text, length, &written, NULL) || !written) return false;
        text += written;
        length -= written;
    }
    return true;
}

static void write_text(HANDLE file, const char* text)
{
    write_bytes(file, text, static_cast<DWORD>(strlen(text)));
}

static DWORD WINAPI writer(void*)
{
    char batch[64 * line_size];
    for (;;)
    {
        WaitForSingleObject(wake, 1000);
        for (;;)
        {
            DWORD used = 0;
            unsigned lost = 0;
            EnterCriticalSection(&lock);
            lost = dropped;
            dropped = 0;
            for (unsigned n = 0; count && n < 64; ++n)
            {
                const size_t length = strlen(queue[head]);
                memcpy(batch + used, queue[head], length);
                used += static_cast<DWORD>(length);
                head = (head + 1) % queue_size;
                --count;
            }
            LeaveCriticalSection(&lock);
            if (lost)
            {
                char warning[160];
                sprintf_s(warning, "[LOGGER] Queue overflow: %u oldest lines omitted.\r\n", lost);
                write_text(stable, warning);
            }
            if (!used) break;
            if (!write_bytes(stable, batch, used))
                OutputDebugStringA("Kichgelkhut: unable to write log_stable.log\n");
        }
        if (InterlockedCompareExchange(&stopping, 0, 0)) break;
    }
    FlushFileBuffers(stable);
    return 0;
}

static void start()
{
    char path[MAX_PATH];
    const DWORD length = GetModuleFileNameA(NULL, path, MAX_PATH);
    if (!length || length >= MAX_PATH) return;
    char* slash = strrchr(path, '\\');
    if (!slash || static_cast<size_t>(slash - path) + 32 >= MAX_PATH) return;
    strcpy_s(slash + 1, MAX_PATH - (slash + 1 - path), "logs");
    if (!CreateDirectoryA(path, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) return;
    sprintf_s(crash_path, "%s\\log_crush.log", path);
    char stable_path[MAX_PATH];
    sprintf_s(stable_path, "%s\\log_stable.log", path);
    stable = CreateFileA(stable_path, GENERIC_WRITE, FILE_SHARE_READ, NULL,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (stable == INVALID_HANDLE_VALUE) return;
    // Keep the previous crash until a new one occurs; create an empty file on first run.
    HANDLE crash = CreateFileA(crash_path, GENERIC_WRITE, FILE_SHARE_READ, NULL,
        OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (crash != INVALID_HANDLE_VALUE) CloseHandle(crash);
    InitializeCriticalSection(&lock);
    head = count = history_head = history_count = dropped = 0;
    stopping = crash_busy = 0;
    report_written = false;
    SYSTEMTIME now;
    GetLocalTime(&now);
    char header[256];
    sprintf_s(header, "%s | compiled %s %s | session %04u-%02u-%02u %02u:%02u:%02u | PID %lu\r\n",
        KichgelkhutBuildLabel(), __DATE__, __TIME__, now.wYear, now.wMonth,
        now.wDay, now.wHour, now.wMinute, now.wSecond, GetCurrentProcessId());
    write_text(stable, header);
    wake = CreateEventA(NULL, FALSE, FALSE, NULL);
    if (wake) worker = CreateThread(NULL, 0, writer, NULL, 0, NULL);
    if (!worker)
    {
        if (wake) CloseHandle(wake);
        wake = NULL;
        CloseHandle(stable);
        stable = INVALID_HANDLE_VALUE;
        DeleteCriticalSection(&lock);
        OutputDebugStringA("Kichgelkhut: background logger could not start\n");
        return;
    }
    SetThreadPriority(worker, THREAD_PRIORITY_BELOW_NORMAL);
    InterlockedExchange(&running, 1);
}

static void add(const char* text)
{
    if (!InterlockedCompareExchange(&running, 0, 0)) return;
    char line[line_size];
    sprintf_s(line, "[tick %lu][thread %lu] %s\r\n", GetTickCount(), GetCurrentThreadId(), text);
    EnterCriticalSection(&lock);
    strcpy_s(history[history_head], line);
    history_head = (history_head + 1) % history_size;
    if (history_count < history_size) ++history_count;
    if (count == queue_size)
    {
        head = (head + 1) % queue_size;
        --count;
        ++dropped;
    }
    strcpy_s(queue[(head + count) % queue_size], line);
    ++count;
    LeaveCriticalSection(&lock);
}

static void crash(const char* reason, bool preserve_recent = false, const char* diagnostic = NULL)
{
    if (!InterlockedCompareExchange(&running, 0, 0) ||
        InterlockedCompareExchange(&crash_busy, 1, 0)) return;
    if (preserve_recent && report_written && GetTickCount() - last_report_tick < 5000)
    {
        InterlockedExchange(&crash_busy, 0);
        return;
    }
    HANDLE file = CreateFileA(crash_path, GENERIC_WRITE, FILE_SHARE_READ, NULL,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file != INVALID_HANDLE_VALUE)
    {
        write_text(file, KichgelkhutBuildLabel());
        write_text(file, "\r\n");
        SYSTEMTIME now;
        GetLocalTime(&now);
        char header[160];
        sprintf_s(header, "Crash report %04u-%02u-%02u %02u:%02u:%02u | PID %lu | thread %lu\r\n",
            now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond,
            GetCurrentProcessId(), GetCurrentThreadId());
        write_text(file, header);
        write_text(file, reason ? reason : "No additional description");
        if (diagnostic && diagnostic[0])
        {
            write_text(file, "\r\n");
            write_text(file, diagnostic);
        }
        write_text(file, "\r\n--- Recent engine messages ---\r\n");
        unsigned n = 0;
        // Never wait for a potentially faulted thread while handling a crash.
        if (TryEnterCriticalSection(&lock))
        {
            n = history_count;
            const unsigned first = (history_head + history_size - n) % history_size;
            for (unsigned i = 0; i < n; ++i)
                memcpy(crash_history[i], history[(first + i) % history_size], line_size);
            LeaveCriticalSection(&lock);
            for (unsigned i = 0; i < n; ++i) write_text(file, crash_history[i]);
        }
        else write_text(file, "History unavailable: logger lock is busy.\r\n");
        FlushFileBuffers(file);
        CloseHandle(file);
        last_report_tick = GetTickCount();
        report_written = true;
    }
    else OutputDebugStringA("Kichgelkhut: unable to write log_crush.log\n");
    InterlockedExchange(&crash_busy, 0);
}

static void stop()
{
    if (!InterlockedExchange(&running, 0)) return;
    InterlockedExchange(&stopping, 1);
    SetEvent(wake);
    WaitForSingleObject(worker, INFINITE);
    CloseHandle(worker);
    CloseHandle(wake);
    CloseHandle(stable);
    worker = wake = NULL;
    stable = INVALID_HANDLE_VALUE;
    DeleteCriticalSection(&lock);
}
}
