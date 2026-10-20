/* Minimal Win32 seam for native tests of the real engine-side B0 loader. */
#ifndef KICH_R4_TEST_WINDOWS_H
#define KICH_R4_TEST_WINDOWS_H
#include <stddef.h>
typedef unsigned long DWORD;
typedef int BOOL;
typedef void* HMODULE;
typedef void (*FARPROC)(void);
#define MAX_PATH 260
DWORD GetModuleFileNameW(HMODULE, wchar_t*, DWORD);
HMODULE LoadLibraryW(const wchar_t*);
BOOL FreeLibrary(HMODULE);
DWORD GetLastError(void);
FARPROC GetProcAddress(HMODULE, const char*);
#endif
