/*
 * win.h - minimal hand written Win32 declarations (no Windows SDK needed).
 * Only what the libc23 Windows backend uses.  Import libraries are generated
 * from the .def files in this directory by build.py (llvm-dlltool).
 * Strings passed to the *W functions are UTF-16: use char16_t, NOT wchar_t
 * (libc23 builds with a 32-bit wchar_t).
 */
#ifndef LIBC23_WIN_H
#define LIBC23_WIN_H

#include <stddef.h>
#include <stdint.h>
/* WCHAR = UTF-16 code unit (clang __CHAR16_TYPE__), no <uchar.h> dependency. */

typedef int          BOOL;
typedef unsigned int   DWORD;
typedef unsigned short WORD;
typedef unsigned char  BYTE;
typedef long           LONG;
typedef unsigned long long ULONGLONG;
typedef long long      LONGLONG;
typedef void          *HANDLE;
typedef void          *LPVOID;
typedef const void    *LPCVOID;
typedef __CHAR16_TYPE__ WCHAR; /* UTF-16 code unit */
typedef WCHAR         *LPWSTR;
typedef const WCHAR   *LPCWSTR;
typedef uintptr_t      ULONG_PTR;
typedef size_t         SIZE_T;
typedef union { struct { DWORD LowPart; LONG HighPart; }; LONGLONG QuadPart; } LARGE_INTEGER;
typedef struct { DWORD dwLowDateTime, dwHighDateTime; } FILETIME;
typedef struct {
    WORD wYear, wMonth, wDayOfWeek, wDay, wHour, wMinute, wSecond, wMilliseconds;
} SYSTEMTIME;
typedef struct {
    LONG Bias;
    WCHAR StandardName[32];
    SYSTEMTIME StandardDate;
    LONG StandardBias;
    WCHAR DaylightName[32];
    SYSTEMTIME DaylightDate;
    LONG DaylightBias;
} TIME_ZONE_INFORMATION;
typedef struct {
    WORD wProcessorArchitecture, wReserved;
    DWORD dwPageSize;
    LPVOID lpMinimumApplicationAddress, lpMaximumApplicationAddress;
    ULONG_PTR dwActiveProcessorMask;
    DWORD dwNumberOfProcessors, dwProcessorType, dwAllocationGranularity;
    WORD wProcessorLevel, wProcessorRevision;
} SYSTEM_INFO;
typedef struct {
    DWORD cb; LPWSTR lpReserved, lpDesktop, lpTitle;
    DWORD dwX, dwY, dwXSize, dwYSize, dwXCountChars, dwYCountChars, dwFillAttribute, dwFlags;
    WORD wShowWindow, cbReserved2; BYTE *lpReserved2;
    HANDLE hStdInput, hStdOutput, hStdError;
} STARTUPINFOW;
typedef struct { HANDLE hProcess, hThread; DWORD dwProcessId, dwThreadId; } PROCESS_INFORMATION;

typedef struct {
    DWORD ExceptionCode, ExceptionFlags;
    struct EXCEPTION_RECORD *ExceptionRecord;
    LPVOID ExceptionAddress;
    DWORD NumberParameters;
    ULONG_PTR ExceptionInformation[15];
} EXCEPTION_RECORD;
typedef struct { EXCEPTION_RECORD *ExceptionRecord; void *ContextRecord; } EXCEPTION_POINTERS;

#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
#define STD_INPUT_HANDLE  ((DWORD)-10)
#define STD_OUTPUT_HANDLE ((DWORD)-11)
#define STD_ERROR_HANDLE  ((DWORD)-12)
#define INFINITE 0xFFFFFFFFu

#define GENERIC_READ        0x80000000u
#define GENERIC_WRITE       0x40000000u
#define FILE_APPEND_DATA    0x00000004u
#define FILE_SHARE_READ     1u
#define FILE_SHARE_WRITE    2u
#define FILE_SHARE_DELETE   4u
#define CREATE_NEW          1u
#define CREATE_ALWAYS       2u
#define OPEN_EXISTING       3u
#define OPEN_ALWAYS         4u
#define TRUNCATE_EXISTING   5u
#define FILE_ATTRIBUTE_NORMAL    0x80u
#define FILE_ATTRIBUTE_DIRECTORY 0x10u
#define FILE_ATTRIBUTE_TEMPORARY 0x100u
#define FILE_FLAG_DELETE_ON_CLOSE 0x04000000u
#define INVALID_FILE_ATTRIBUTES  0xFFFFFFFFu
#define FILE_TYPE_DISK 1
#define FILE_TYPE_UNKNOWN 0
#define MOVEFILE_REPLACE_EXISTING 1u
#define MEM_COMMIT  0x1000u
#define MEM_RESERVE 0x2000u
#define MEM_RELEASE 0x8000u
#define PAGE_READWRITE 4u
#define CP_UTF8 65001u
#define WAIT_OBJECT_0 0u
#define WAIT_TIMEOUT 258u
#define CREATE_UNICODE_ENVIRONMENT 0x400u
#define STACK_SIZE_PARAM_IS_A_RESERVATION 0x10000u
#define ENABLE_PROCESSED_OUTPUT 1u
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 4u
#define EXCEPTION_CONTINUE_SEARCH 0
#define TIME_ZONE_ID_INVALID 0xFFFFFFFFu

/* GetLastError values used */
#define ERROR_FILE_NOT_FOUND 2
#define ERROR_PATH_NOT_FOUND 3
#define ERROR_TOO_MANY_OPEN_FILES 4
#define ERROR_ACCESS_DENIED 5
#define ERROR_INVALID_HANDLE 6
#define ERROR_NOT_ENOUGH_MEMORY 8
#define ERROR_INVALID_DRIVE 15
#define ERROR_CURRENT_DIRECTORY 16
#define ERROR_NOT_SAME_DEVICE 17
#define ERROR_WRITE_PROTECT 19
#define ERROR_SHARING_VIOLATION 32
#define ERROR_LOCK_VIOLATION 33
#define ERROR_HANDLE_EOF 38
#define ERROR_HANDLE_DISK_FULL 39
#define ERROR_NOT_SUPPORTED 50
#define ERROR_FILE_EXISTS 80
#define ERROR_INVALID_PARAMETER 87
#define ERROR_BROKEN_PIPE 109
#define ERROR_DISK_FULL 112
#define ERROR_CALL_NOT_IMPLEMENTED 120
#define ERROR_INSUFFICIENT_BUFFER 122
#define ERROR_INVALID_NAME 123
#define ERROR_DIR_NOT_EMPTY 145
#define ERROR_ALREADY_EXISTS 183
#define ERROR_FILENAME_EXCED_RANGE 206
#define ERROR_NO_DATA 232
#define ERROR_NOT_ENOUGH_QUOTA 1816
#define ERROR_TIMEOUT 1460
#define ERROR_PIPE_NOT_CONNECTED 233
#define ERROR_OPERATION_ABORTED 995
#define ERROR_NEGATIVE_SEEK 131
#define ERROR_SEEK_ON_DEVICE 132
#define ERROR_CANT_ACCESS_FILE 1920

#define WINAPI_IMPORT __declspec(dllimport)

/* kernel32 */
WINAPI_IMPORT HANDLE GetStdHandle(DWORD);
WINAPI_IMPORT BOOL ReadFile(HANDLE, LPVOID, DWORD, DWORD *, LPVOID);
WINAPI_IMPORT BOOL WriteFile(HANDLE, LPCVOID, DWORD, DWORD *, LPVOID);
WINAPI_IMPORT BOOL CloseHandle(HANDLE);
WINAPI_IMPORT HANDLE CreateFileW(LPCWSTR, DWORD, DWORD, LPVOID, DWORD, DWORD, HANDLE);
WINAPI_IMPORT BOOL SetFilePointerEx(HANDLE, LARGE_INTEGER, LARGE_INTEGER *, DWORD);
WINAPI_IMPORT BOOL FlushFileBuffers(HANDLE);
WINAPI_IMPORT DWORD GetFileType(HANDLE);
WINAPI_IMPORT BOOL DeleteFileW(LPCWSTR);
WINAPI_IMPORT BOOL RemoveDirectoryW(LPCWSTR);
WINAPI_IMPORT BOOL MoveFileExW(LPCWSTR, LPCWSTR, DWORD);
WINAPI_IMPORT DWORD GetFileAttributesW(LPCWSTR);
WINAPI_IMPORT DWORD GetTempPathW(DWORD, LPWSTR);
WINAPI_IMPORT DWORD GetLastError(void);
WINAPI_IMPORT void SetLastError(DWORD);
WINAPI_IMPORT BOOL GetConsoleMode(HANDLE, DWORD *);
WINAPI_IMPORT BOOL SetConsoleMode(HANDLE, DWORD);
WINAPI_IMPORT BOOL SetConsoleOutputCP(unsigned);
WINAPI_IMPORT BOOL SetConsoleCP(unsigned);
WINAPI_IMPORT BOOL SetConsoleCtrlHandler(BOOL (*)(DWORD), BOOL);
WINAPI_IMPORT LPWSTR GetCommandLineW(void);
WINAPI_IMPORT LPWSTR GetEnvironmentStringsW(void);
WINAPI_IMPORT BOOL FreeEnvironmentStringsW(LPWSTR);
WINAPI_IMPORT DWORD GetEnvironmentVariableW(LPCWSTR, LPWSTR, DWORD);
WINAPI_IMPORT int WideCharToMultiByte(unsigned, DWORD, LPCWSTR, int, char *, int, const char *, BOOL *);
WINAPI_IMPORT int MultiByteToWideChar(unsigned, DWORD, const char *, int, LPWSTR, int);
WINAPI_IMPORT __attribute__((noreturn)) void ExitProcess(unsigned);
WINAPI_IMPORT BOOL TerminateProcess(HANDLE, unsigned);
WINAPI_IMPORT HANDLE GetCurrentProcess(void);
WINAPI_IMPORT HANDLE GetCurrentThread(void);
WINAPI_IMPORT DWORD GetCurrentThreadId(void);
WINAPI_IMPORT LPVOID VirtualAlloc(LPVOID, SIZE_T, DWORD, DWORD);
WINAPI_IMPORT BOOL VirtualFree(LPVOID, SIZE_T, DWORD);
WINAPI_IMPORT void GetSystemInfo(SYSTEM_INFO *);
WINAPI_IMPORT HANDLE GetProcessHeap(void);
WINAPI_IMPORT LPVOID HeapAlloc(HANDLE, DWORD, SIZE_T);
WINAPI_IMPORT BOOL HeapFree(HANDLE, DWORD, LPVOID);
WINAPI_IMPORT void GetSystemTimePreciseAsFileTime(FILETIME *);
WINAPI_IMPORT BOOL QueryPerformanceCounter(LARGE_INTEGER *);
WINAPI_IMPORT BOOL QueryPerformanceFrequency(LARGE_INTEGER *);
WINAPI_IMPORT BOOL GetProcessTimes(HANDLE, FILETIME *, FILETIME *, FILETIME *, FILETIME *);
WINAPI_IMPORT BOOL GetThreadTimes(HANDLE, FILETIME *, FILETIME *, FILETIME *, FILETIME *);
WINAPI_IMPORT void Sleep(DWORD);
WINAPI_IMPORT BOOL SwitchToThread(void);
WINAPI_IMPORT HANDLE CreateThread(LPVOID, SIZE_T, DWORD (*)(LPVOID), LPVOID, DWORD, DWORD *);
WINAPI_IMPORT __attribute__((noreturn)) void ExitThread(DWORD);
WINAPI_IMPORT DWORD WaitForSingleObject(HANDLE, DWORD);
WINAPI_IMPORT BOOL GetExitCodeProcess(HANDLE, DWORD *);
WINAPI_IMPORT BOOL CreateProcessW(LPCWSTR, LPWSTR, LPVOID, LPVOID, BOOL, DWORD, LPVOID, LPCWSTR,
                                  STARTUPINFOW *, PROCESS_INFORMATION *);
WINAPI_IMPORT BOOL GetTimeZoneInformationForYear(WORD, void *, TIME_ZONE_INFORMATION *);
WINAPI_IMPORT LPVOID AddVectoredExceptionHandler(unsigned long, long (*)(EXCEPTION_POINTERS *));
WINAPI_IMPORT unsigned long RemoveVectoredExceptionHandler(LPVOID);

/* api-ms-win-core-synch-l1-2-0 */
WINAPI_IMPORT BOOL WaitOnAddress(volatile void *, void *, SIZE_T, DWORD);
WINAPI_IMPORT void WakeByAddressSingle(void *);
WINAPI_IMPORT void WakeByAddressAll(void *);

/* bcryptprimitives */
WINAPI_IMPORT BOOL ProcessPrng(unsigned char *, SIZE_T);

#endif
