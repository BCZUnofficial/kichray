#ifndef KICH_R4_GAME_TEST_WINDOWS_H
#define KICH_R4_GAME_TEST_WINDOWS_H
#include <stddef.h>
typedef unsigned long DWORD;
typedef int BOOL;
typedef void* HMODULE;
typedef void* HWND;
typedef void (*FARPROC)(void);
#define MAX_PATH 260
DWORD GetModuleFileNameW(HMODULE,wchar_t*,DWORD);
HMODULE LoadLibraryW(const wchar_t*);
BOOL FreeLibrary(HMODULE);
DWORD GetLastError();
FARPROC GetProcAddress(HMODULE,const char*);
BOOL IsWindow(HWND);
int GetWindowTextW(HWND,wchar_t*,int);
BOOL SetWindowTextW(HWND,const wchar_t*);
#endif
