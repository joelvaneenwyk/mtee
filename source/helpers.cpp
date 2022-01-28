#include "header.h"

#include <stdio.h>
#include <stdlib.h>

//::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::
//
// CreateFullPathW creates the directory structure pointed to by szPath
// It creates everything upto the last backslash. For example:-
// c:\dir1\dir2\ <-- creates dir1 and dir2
// c:\dir1\file.dat <-- creates only dir1. This means you can pass this
// function the path to a file, and it will just create the required
// directories.
// Absolute or relative paths can be used, as can path length of 32,767
// characters, composed of components up to 255 characters in length. To
// specify such a path, use the "\\?\" prefix. For example, "\\?\D:\<path>".
// To specify such a UNC path, use the "\\?\UNC\" prefix. For example,
// "\\?\UNC\<server>\<share>".
//
//::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::
PWSTR CreateFullPathW(PWSTR szPath)
{
    PWCHAR p = szPath;

    while (*p++)
    {
        if (*p == L'\\')
        {
            *p = L'\0';
            CreateDirectoryW(szPath, NULL);
            *p = L'\\';
        }
    }
    return szPath;
}

VOID MsgBox(LPCTSTR msg)
{
    MessageBox(NULL, msg, TEXT("MTEE BETA"), MB_OK | MB_SETFOREGROUND);
}

VOID ShowFileType(HANDLE h)
{
    DWORD dwHndType;
    dwHndType = GetFileType(h);

    switch (dwHndType)
    {
    case FILE_TYPE_DISK: // stdin/stdout is redirected from/to disk: app.exe<in>out
        MsgBox(TEXT("FILE_TYPE_DISK"));
        break;
    case FILE_TYPE_CHAR: // stdin from keyboard or nul/con/aux/comx, stdout to console or nul
        MsgBox(TEXT("FILE_TYPE_CHAR"));
        break;
    case FILE_TYPE_PIPE: // stdin is from pipe prn/lptx, stdout piped to anything
        MsgBox(TEXT("FILE_TYPE_PIPE"));
        break;
    default:
        MsgBox(TEXT("FILE_TYPE_UNKNOWN"));
        break;
    }
}

BOOL IsSupportedWindowsVersion()
{
    OSVERSIONINFOEX osvi;
    DWORDLONG dwlConditionMask = 0;

    // Initialize the OSVERSIONINFOEX structure.
    ZeroMemory(&osvi, sizeof(OSVERSIONINFOEX));
    osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEX);
    osvi.dwMajorVersion = 5;
    osvi.dwMinorVersion = 1;
    osvi.wServicePackMajor = 2;
    osvi.wServicePackMinor = 0;

    // Initialize the condition mask.
    BYTE op = VER_GREATER_EQUAL;
    VER_SET_CONDITION(dwlConditionMask, VER_MAJORVERSION, op);
    VER_SET_CONDITION(dwlConditionMask, VER_MINORVERSION, op);
    VER_SET_CONDITION(dwlConditionMask, VER_SERVICEPACKMAJOR, op);
    VER_SET_CONDITION(dwlConditionMask, VER_SERVICEPACKMINOR, op);

    // Perform the test.
    return VerifyVersionInfo(
        &osvi, VER_MAJORVERSION | VER_MINORVERSION | VER_SERVICEPACKMAJOR | VER_SERVICEPACKMINOR, dwlConditionMask);
}

//----------------------------------------------------------------------------
//          dwPlatFormID  dwMajorVersion  dwMinorVersion  dwBuildNumber  GetWinVer()
// 95             1              4               0             950            0
// 95 SP1         1              4               0        >950 && <=1080      0
// 95 OSR2        1              4             <10           >1080            0
// 98             1              4              10            1998            0
// 98 SP1         1              4              10       >1998 && <2183       0
// 98 SE          1              4              10          >=2183            0
// ME             1              4              90            3000            0
//
// NT 3.51        2              3              51                            0
// NT 4           2              4               0            1381            1
//
// 2000           2              5               0            2195            2
// XP             2              5               1            xxxx            3
// 2003*          2              5               2            xxxx            4
//
// CE             3                                                           0
//
//----------------------------------------------------------------------------
DWORD GetFormattedDateTimeA(PCHAR lpBuf, BOOL bDate, BOOL bTime)
{
    SYSTEMTIME st;
    DWORD dwSize = 0;

    GetLocalTime(&st);

    if (bDate)
    {
        dwSize += wsprintfA(lpBuf, "%04u-%02u-%02u ", st.wYear, st.wMonth, st.wDay);
    }
    if (bTime)
    {
        dwSize += wsprintfA(lpBuf + dwSize, "%02u:%02u:%02u.%03u ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    }
    return dwSize;
}

//----------------------------------------------------------------------------
DWORD GetFormattedDateTimeW(PWCHAR lpBuf, BOOL bDate, BOOL bTime)
{
    SYSTEMTIME st;
    DWORD dwSize = 0;

    GetLocalTime(&st);

    if (bDate)
    {
        dwSize += wsprintfW(lpBuf, L"%04u-%02u-%02u ", st.wYear, st.wMonth, st.wDay);
    }
    if (bTime)
    {
        dwSize +=
            wsprintfW(lpBuf + dwSize, L"%02u:%02u:%02u.%03u ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    }
    return dwSize; // return characters written, not bytes
}

DWORD GetParentProcessId(VOID)
{
    DWORD ppid = 0, pid = GetCurrentProcessId();

    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE)
        Perror((DWORD)NULL);

    PROCESSENTRY32 pe = {0};
    pe.dwSize = sizeof(PROCESSENTRY32);

    //
    // find our PID in the process snapshot then lookup parent PID
    //
    if (Process32First(hSnapshot, &pe))
    {
        do
        {
            if (pe.th32ProcessID == pid)
            {
                ppid = pe.th32ParentProcessID;
                break;
            }
        } while (Process32Next(hSnapshot, &pe));
    }

    if (hSnapshot)
    {
        CloseHandle(hSnapshot);
    }

    return ppid;
}

static inline DWORD GetActualNumberOfConsoleProcesses(VOID)
{
    DWORD dummyProcessId;
    DWORD numberOfProcesses;

    numberOfProcesses = GetConsoleProcessList(&dummyProcessId, 1);

    return numberOfProcesses;
}

HANDLE GetPipedProcessHandle(VOID)
{
    //
    // returns a handle to the process piped into mtee
    //
    size_t dwProcCount = 0;
    DWORD *lpdwProcessList;
    HANDLE hPipedProcess = INVALID_HANDLE_VALUE;

    //
    // get an array of PIDs attached to this console
    //

    dwProcCount = GetActualNumberOfConsoleProcesses();
    lpdwProcessList = (DWORD *)malloc(dwProcCount * sizeof(DWORD));
    if (NULL != lpdwProcessList && dwProcCount > 0)
    {
        DWORD dwActualProcCount = GetConsoleProcessList(lpdwProcessList, (DWORD)dwProcCount);
        if (dwActualProcCount <= dwProcCount)
        {
            // in tests it __appears__ array element 0 is this PID, element 1 is process
            // piped into mtee, and last element is cmd.exe. if more than one pipe used,
            // element 2 is next process to rhe left:
            // eg A | B | C | mtee /e ==> lpdwProcessList[mtee][A][B][C][cmd]
            //
            // find the first PID that is not this PID and not parent PID.
            //
            DWORD ppid = GetParentProcessId();
            DWORD cpid = GetCurrentProcessId();
            for (DWORD dw = 0; dw < dwProcCount; dw++)
            {
                DWORD pid = lpdwProcessList[dw];
                if (pid != cpid && pid != ppid)
                {
                    HANDLE Handle =
                        OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, lpdwProcessList[dw]);
                    if (INVALID_HANDLE_VALUE != Handle)
                    {
                        hPipedProcess = Handle;
                        break;
                    }
                }
            }
        }
    }
    else
    {
        hPipedProcess = INVALID_HANDLE_VALUE;
    }

    free(lpdwProcessList);

    return hPipedProcess;
}

// determine whether the output is a console
// this is hard. I first tried to use GetConsoleMode but it returns FALSE in case: mtee > con
BOOL IsAnOutputConsoleDevice(HANDLE h)
{
    if (GetFileType(h) == FILE_TYPE_CHAR)
    {
        // CON, NUL, ...
        DWORD dwBytesWritten;
        if (WriteConsoleA(h, "", 0, &dwBytesWritten, NULL))
            return TRUE;
    }
    return FALSE;
}

int FormatElapsedTime(LARGE_INTEGER *elapsedTime, PCHAR outBuf, const size_t outBufSize)
{
    unsigned int h = 0;
    unsigned int m = 0;
    int len = 0;

    float s = float(elapsedTime->QuadPart / 1000000);
    m = (unsigned int)(s / 60.0);
    s = s - 60.0f * (float)m;

    h = (unsigned int)((float)m / 60.0);
    m = m - 60 * h;

    len = snprintf(outBuf, outBufSize, "Elapsed time: %02dh%02dm%06.3fs\n", h, m, s);

    return len;
}
