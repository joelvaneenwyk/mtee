//
// # History
//
// 01-MAR-2013, MTEE 2.1
//    Bug: In Windows 8, mtee resulted to "Incorrect function" if output was to pipe.
//    Fixed to not not rely on undocumented error codes from WriteConsole.
//    At the same time, got rid of separate functions for console and disk files,
//    and combined them to WriteBufferToConsoleAndFilesA and WriteBufferToConsoleAndFilesW and separating
//    the console case with fi->bIsConsole. Some re-org to the idea of args.fi : the first item
//    is always the std output, and the rest of the files are the given output files. The last item
//    in the linked list no longer a dummy item.
//    Bug: echo x x x x | mtee guessed that the input was Unicode.
//    Fixed to use IS_TEXT_UNICODE_NULL_BYTES instead of IS_TEXT_UNICODE_ASCII16 |    IS_TEXT_UNICODE_STATISTICS.
//    Bug: echo t013|mtee /u con entered a forever loop.
//    Fixed the bug in WriteBufferToDiskW loop.
//    Bug: assumed that all files are less than 4 GB.
//    Fixed by using also dwFileSizeHigh in GetFileSize.
//    Bug: redir to console and con as output file was not supported.
//    Fixed by not trying to truncate the result file with SetEndOfFile if it is a console.
//    (redir to con may be wanted if std output is already redirected to file)
//
// 27-APR-2016, MTEE 2.2
//    Workaround: Using ExitProcess at end to workaround an issue in Windows 10
//    Ref:
//
// https://connect.microsoft.com/VisualStudio/feedback/details/2640071/30-s-delay-after-returning-from-main-program
// 

#include "header.h"

#include <stdio.h>

#ifdef _MSC_VER
// This function or variable may be unsafe. Consider using XXX_s instead. To disable deprecation, use
// _CRT_SECURE_NO_WARNINGS. See online help for details.
#pragma warning(disable : 4996)

// pointer or reference to potentially throwing function passed to 'extern "C"' function under -EHc. Undefined 
// behavior may occur if this function throws an exception.
#pragma warning(disable : 5039)
#endif

#define PEEK_BUF_SIZE (0x400) // 1024
#define PEEK_WAIT_INT (10)

DWORD dwCtrlEvent; // set by ctrl handler

DWORD __declspec(noinline) tee(ARGS *args)
{
    PCHAR lpBuf = nullptr;           // pointer to main input buffer
    PCHAR lpAnsiBuf = nullptr;       // pointer to buffer for converting unicode to ansi
    PWCHAR lpUnicodeBuf = nullptr;   // pointer to buffer for converting ansi to unicode
    DWORD dwBytesRead = 0L;          // bytes read from input
    HANDLE hOut = nullptr;           // handle to stdout
    HANDLE hIn = nullptr;            // handle to stdin
    DWORD dwStdInType = (DWORD)NULL; // stdin's filetype (file/pipe/console)
    PFILEINFO fi = nullptr;          // pointer for indexing FILEINFO records
    BOOL bBomFound = FALSE;          // true if BOM found
    BOOL bCtrlHandler = FALSE;
    DWORD dwPeekBytesRead = 0L;
    DWORD dwPeekBytesAvailable = 0L;
    DWORD dwPeekBytesUnavailable = 0L;
    DWORD cPeekTimeout = 0L;
    BYTE byPeekBuf[PEEK_BUF_SIZE] = {0}; // holds peeked input for ansi/unicode test
    DWORD dwInFormat = OP_ANSI_IN;
    DWORD dwOperation;
    int iFlags;

#ifdef _DEBUG
    // MessageBox(0, L"start", L"mtee", MB_OK);
#endif

    if (!IsSupportedWindowsVersion())
    {
        Verbose(TEXT("This program requires Windows NT4, 2000, XP or 2003.\r\n"));
        return 1;
    }

    //
    // install ctrl handler to trap ctrl-c and ctrl-break
    //
    dwCtrlEvent = CTRL_CLEAR_EVENT;
    bCtrlHandler = SetConsoleCtrlHandler(HandlerRoutine, TRUE);

    //
    // parse the commandline
    //
    if (!ParseCommandlineW(args))
    {
        return 1;
    }

    //
    // did user want to display helpscreen?
    //
    if (args->bHelp)
        return ShowHelp();

    //
    // get handles to stdout/stdin
    //
    if ((hIn = GetStdHandle(STD_INPUT_HANDLE)) == INVALID_HANDLE_VALUE)
    {
        return Perror((DWORD)NULL);
    }

    if ((hOut = GetStdHandle(STD_OUTPUT_HANDLE)) == INVALID_HANDLE_VALUE)
    {
        return Perror((DWORD)NULL);
    }

    args->fi.hFile = hOut;

    //
    // determine whether the output is a console
    //
    args->fi.bIsConsole = IsAnOutputConsoleDevice(hOut);

    //
    // determine the type of input file then peek at content to ascertain if it's unicode
    //
    dwStdInType = GetFileType(hIn);

    //
    // if requested by user, get handle to piped process
    //
    HANDLE hPipedProcess = nullptr;
    if (args->bFwdExitCode)
        hPipedProcess = GetPipedProcessHandle();

    switch (dwStdInType)
    {
    case FILE_TYPE_DISK: // stdin is from a file: mtee.exe < infile
    {
        DWORD dwFileSizeAtLeast;
        DWORD dwFileSizeHigh = 0;

        //
        // try and determine if input file is unicode or ansi. first check the filesize
        // if its zero bytes then don't use readfile as this will block
        //
        dwFileSizeAtLeast = GetFileSize(hIn, &dwFileSizeHigh);
        if (dwFileSizeHigh != 0)
            dwFileSizeAtLeast = 0xFFFFFFFE;
        if (dwFileSizeAtLeast == 0xFFFFFFFF && GetLastError() != NO_ERROR)
        {
            if (!args->bContinue)
                return Perror((DWORD)NULL);
            else
                dwFileSizeAtLeast = 0L;
        }
        //
        // Only try and peek if there's at least a wchar available otherwise a test for
        // unicode is meaningless.
        //
        if (dwFileSizeAtLeast >= sizeof(WCHAR))
        {
            if (!ReadFile(
                    hIn, byPeekBuf, dwFileSizeAtLeast < sizeof(byPeekBuf) ? dwFileSizeAtLeast : sizeof(byPeekBuf),
                    &dwPeekBytesRead, nullptr))
            {
                //
                // if failed and if i/o errors not being ignored then quit
                //
                if (!args->bContinue)
                    return Perror((DWORD)NULL);
                else
                    break;
            }
            //
            // reset the file pointer to beginning
            //
            if (SetFilePointer(hIn, (LONG)NULL, nullptr, FILE_BEGIN) && (!args->bContinue))
                return Perror((DWORD)NULL);
        }
    }
    break;
    case FILE_TYPE_CHAR:
        // stdin from NUL, CON, CONIN$, AUX or COMx
        // if AUX or COMx, then quit without creating any files (not even zero byte files)
        args->dwBufSize = 1;
        {
            DWORD dwInMode;
            if (!GetConsoleMode(hIn, &dwInMode)) // fails (err 6) if NUL, COMx, AUX
            {
                COMMTIMEOUTS CommTimeouts;
                // suceeds if AUX or COMx so quit (allow NUL)
                if (GetCommTimeouts(hIn, &CommTimeouts))
                    return ERROR_SUCCESS;
            }
        }
        break;
    case FILE_TYPE_PIPE: // stdin is from pipe, prn or lpt1
        while ((!dwPeekBytesRead) && (cPeekTimeout < args->dwPeekTimeout) && (dwCtrlEvent == CTRL_CLEAR_EVENT))
        {
            if (!PeekNamedPipe(
                    hIn,                      // handle to pipe to copy from
                    byPeekBuf,                // pointer to data buffer
                    PEEK_BUF_SIZE,            // size, in bytes, of data buffer
                    &dwPeekBytesRead,         // pointer to number of bytes read
                    &dwPeekBytesAvailable,    // pointer to total number of bytes available
                    &dwPeekBytesUnavailable)) // pointer to unread bytes in this message
            {
                if (GetLastError() != ERROR_BROKEN_PIPE)
                    return Perror((DWORD)NULL);
            }
            Sleep(PEEK_WAIT_INT);
            cPeekTimeout += PEEK_WAIT_INT;
        }
        break;
    }

    //
    // open/create all the files after checking stdin, that way if there was an error then
    // zero byte files are not created
    //
    fi = args->fi.fiNext;
    while (fi)
    {
        fi->hFile = CreateFileW(
            args->bIntermediate ? CreateFullPathW(fi->lpFileName) : fi->lpFileName,
            GENERIC_WRITE,   // we definitely need write access
            FILE_SHARE_READ, // allow others to open file for read
            nullptr,         // security attr - no thanks
            OPEN_ALWAYS,     // creation disposition - we always want to open or append
            0,               // flags & attributes - gulp! have you seen the documentation?
            nullptr          // handle to a template? yer right
        );

        if ((fi->hFile == INVALID_HANDLE_VALUE) && !args->bContinue)
            return Perror((DWORD)NULL);

        //
        // If appending set file pointer to EOF.
        //
        if (fi->bAppend)
        {
            if ((SetFilePointer(fi->hFile, (LONG)NULL, nullptr, FILE_END) == INVALID_SET_FILE_POINTER) &&
                !args->bContinue)
            {
                return Perror((DWORD)NULL);
            }
        }

        //
        // Check if it happens to be CON or CONOUT$
        //
        fi->bIsConsole = IsAnOutputConsoleDevice(fi->hFile);

        //
        // Truncate the possibly existing file to zero size
        //
        if (!fi->bIsConsole && !SetEndOfFile(fi->hFile))
        {
            switch (GetLastError())
            {
            case ERROR_INVALID_HANDLE: // CON, CONOUT$, CONIN$ device, so close the record
                // Yes, this is OK also. fi->hFile = INVALID_HANDLE_VALUE;
                break;
            case ERROR_INVALID_FUNCTION:  // NUL device
            case ERROR_INVALID_PARAMETER: // PRN device
                break;
            default:
                if (!args->bContinue)
                    return Perror((DWORD)NULL);
            }
        }

        fi = fi->fiNext;
    }

    //
    // if enough bytes read for a meaningful unicode test...
    //
    if (dwPeekBytesRead >= sizeof(WCHAR) * 2)
    {
        // Verbose(TEXT("dwPeekBytesRead >= 4\r\n"));
        //
        //  first look for BOM
        //  TO DO. if BOM found then do not write it to the console
        //  maybe write to files and then advance input pointer two bytes
        //
        if ((byPeekBuf[0] == 0xFF) && (byPeekBuf[1] == 0xFE)) // notepad and wordpad's unicode format
        {
            bBomFound = TRUE;
            dwInFormat = OP_UNICODE_IN;
        }
        else
        {
            iFlags = IS_TEXT_UNICODE_NULL_BYTES;
            IsTextUnicode(byPeekBuf, (int)dwPeekBytesRead, &iFlags);
            if (iFlags & IS_TEXT_UNICODE_NULL_BYTES)
            {
                dwInFormat = OP_UNICODE_IN;
            }
        }
    }

    //    if(dwInFormat & OP_UNICODE_IN) Verbose("Unicode in...\r\n");
    //    else Verbose("ANSI in...\r\n");
    //
    // allocate the main I/O buffer
    //
    lpBuf = (PCHAR)HeapAlloc(GetProcessHeap(), 0, args->dwBufSize * sizeof(CHAR));
    if (!lpBuf)
        return Perror((DWORD)NULL);

    if (args->bAnsi)
        dwOperation = (dwInFormat | OP_ANSI_OUT);
    else if (args->bUnicode)
        dwOperation = (dwInFormat | OP_UNICODE_OUT);
    else
        dwOperation = dwInFormat | (dwInFormat << OP_IN_OUT_SHIFT);

    //
    // if input starts with a BOM and output is unicode skip over BOM
    //
    if (bBomFound)
    {
        if (!ReadFile(hIn, lpBuf, sizeof(WCHAR), &dwBytesRead, nullptr))
        {
            if (GetLastError() != ERROR_BROKEN_PIPE)
                return Perror((DWORD)NULL);
        }
    }
    //
    // if output is unicode and user specified unicode conversion, write BOM to files (but not to the std output)
    //
    if ((dwOperation & OP_UNICODE_OUT) && args->bUnicode)
    {
        if (!WriteBom(args->fi.fiNext, args->bContinue))
        {
            if (!args->bContinue)
                return Perror((DWORD)NULL);
        }
    }

    LARGE_INTEGER startTimestamp = {{0}};
    LARGE_INTEGER endTimestamp = {{0}};
    LARGE_INTEGER elapsedTime = {{0}};
    LARGE_INTEGER frequency = {{0}};

    if (args->bElapsedTime)
    {
        (void)QueryPerformanceFrequency(&frequency);
        (void)QueryPerformanceCounter(&startTimestamp);
    }

    unsigned long long nSamples = 0;
    double accumulatedCpuLoad = 0.0;

    cpuLoadInit();

    for (;;)
    {
        double currentCpuLoad = 0.0;
        BOOL rc = cpuLoadGetCurrentCpuLoad(&currentCpuLoad);

        if (rc)
        {
            ++nSamples;
            accumulatedCpuLoad += currentCpuLoad;
        }

        if (!ReadFile(hIn, lpBuf, args->dwBufSize * sizeof(CHAR), &dwBytesRead, nullptr))
        {
            if (GetLastError() != ERROR_BROKEN_PIPE)
            {
                Perror((DWORD)NULL);
                break;
            }
        }
        //
        // if nothing read or user entered EOF then break (ctrl event also causes nothing to be read )
        //
        if ((!dwBytesRead) || ((dwStdInType == FILE_TYPE_CHAR) && (*lpBuf == '\x1A')))
            break;
        if (dwOperation == OP_ANSI_IN_ANSI_OUT)
        {
            if (!WriteBufferToConsoleAndFilesA(args, lpBuf, dwBytesRead, args->bAddDate, args->bAddTime))
            {
                Perror((DWORD)NULL);
                break;
            }
        }
        else if (dwOperation == OP_UNICODE_IN_UNICODE_OUT)
        {
            if (!WriteBufferToConsoleAndFilesW(
                    args, (PWCHAR)lpBuf, dwBytesRead / sizeof(WCHAR), args->bAddDate, args->bAddTime))
            {
                Perror((DWORD)NULL);
                break;
            }
        }
        else if (dwOperation == OP_ANSI_IN_UNICODE_OUT)
        {
            AnsiToUnicode(&lpUnicodeBuf, lpBuf, &dwBytesRead);
            if (!WriteBufferToConsoleAndFilesW(args, (PWCHAR)lpUnicodeBuf, dwBytesRead, args->bAddDate, args->bAddTime))
            {
                Perror((DWORD)NULL);
                break;
            }
        }
        else if (dwOperation == OP_UNICODE_IN_ANSI_OUT)
        {
            UnicodeToAnsi(&lpAnsiBuf, (PWCHAR)lpBuf, &dwBytesRead);
            if (!WriteBufferToConsoleAndFilesA(
                    args, lpAnsiBuf, dwBytesRead / sizeof(WCHAR), args->bAddDate, args->bAddTime))
            {
                Perror((DWORD)NULL);
                break;
            }
        }
    }

    if (args->bElapsedTime)
    {
        char strElapsedTime[128];
        memset(strElapsedTime, 0, sizeof(strElapsedTime));

        (void)QueryPerformanceCounter(&endTimestamp);

        elapsedTime.QuadPart = endTimestamp.QuadPart - startTimestamp.QuadPart;
        elapsedTime.QuadPart *= 1000000L;
        elapsedTime.QuadPart /= frequency.QuadPart;

        int strLen = FormatElapsedTime(&elapsedTime, strElapsedTime, sizeof(strElapsedTime));
        WriteBufferToConsoleAndFilesA(args, strElapsedTime, (DWORD)strLen, FALSE, FALSE);
    }

    if (args->bMeasureCPUUsage)
    {
        char cpuLoadStr[128];
        double averageCpuLoad = (accumulatedCpuLoad / (double)nSamples);
        DWORD cpuLoadStrlen =
            (DWORD)snprintf(cpuLoadStr, sizeof(cpuLoadStr), "CPU Load (avg.) = %5.2f\n", averageCpuLoad);

        WriteBufferToConsoleAndFilesA(args, cpuLoadStr, cpuLoadStrlen, FALSE, FALSE);
    }

    //
    // close all open files (not the first entry that contains the std output)
    //
    fi = args->fi.fiNext;
    while (fi)
    {
        if (fi->hFile != INVALID_HANDLE_VALUE)
            CloseHandle(fi->hFile);
        fi = fi->fiNext;
    }
    if (bCtrlHandler)
        SetConsoleCtrlHandler(HandlerRoutine, FALSE);
    if (dwStdInType == FILE_TYPE_CHAR)
        FlushFileBuffers(hIn);

    DWORD dwExitCode = 0;
    //
    // if requested by user, get exit code of piped process
    //
    if (args->bFwdExitCode)
    {
        GetExitCodeProcess(hPipedProcess, &dwExitCode);
        CloseHandle(hPipedProcess);
    }

    FreeFileInfoStructs(&args->fi);

    return dwExitCode;
}

int fuzz()
{
    ARGS args;
    return (int)tee(&args);
}

//
//  WinAFL - A simple test binary that crashes on certain inputs:
//    - 'test1' with a normal write access violation at NULL
//    - 'test2' with a /GS stack cookie violation
//  -------------------------------------------------------------
//
//  Written and maintained by Ivan Fratric <ifratric@google.com>
//
//  Copyright 2016 Google Inc. All Rights Reserved.
//
//  Licensed under the Apache License, Version 2.0 (the "License");
//  you may not use this file except in compliance with the License.
//  You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
//  Unless required by applicable law or agreed to in writing, software
//  distributed under the License is distributed on an "AS IS" BASIS,
//  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//  See the License for the specific language governing permissions and
//  limitations under the License.
//
int __declspec(noinline) test_target(char *input_file_path, char *argv_0)
{
    char *crash = NULL;
    FILE *fp = fopen(input_file_path, "rb");
    char c;
    if (!fp)
    {
        printf("Error opening file\n");
        return 0;
    }
    if (fread(&c, 1, 1, fp) != 1)
    {
        printf("Error reading file\n");
        fclose(fp);
        return 0;
    }
    if (c != 't')
    {
        printf("Error 1\n");
        fclose(fp);
        return 0;
    }
    if (fread(&c, 1, 1, fp) != 1)
    {
        printf("Error reading file\n");
        fclose(fp);
        return 0;
    }
    if (c != 'e')
    {
        printf("Error 2\n");
        fclose(fp);
        return 0;
    }
    if (fread(&c, 1, 1, fp) != 1)
    {
        printf("Error reading file\n");
        fclose(fp);
        return 0;
    }
    if (c != 's')
    {
        printf("Error 3\n");
        fclose(fp);
        return 0;
    }
    if (fread(&c, 1, 1, fp) != 1)
    {
        printf("Error reading file\n");
        fclose(fp);
        return 0;
    }
    if (c != 't')
    {
        printf("Error 4\n");
        fclose(fp);
        return 0;
    }
    printf("!!!!!!!!!!OK!!!!!!!!!!\n");

    if (fread(&c, 1, 1, fp) != 1)
    {
        printf("Error reading file\n");
        fclose(fp);
        return 0;
    }
    if (c == '1')
    {
        // cause a crash
        crash[0] = 1;
    }
    else if (c == '2')
    {
        char buffer[5] = {0};
        // stack-based overflow to trigger the GS cookie corruption
        for (int i = 0; i < 5; ++i)
            strcat(buffer, argv_0);
        printf("buffer: %s\n", buffer);
    }
    else
    {
        printf("Error 5\n");
    }
    fclose(fp);
    return 0;
}

int main(int argc, char **argv)
{
    // ARGS args;
    // int result = tee(&args);
    //
    // //Use ExitProcess (instead of return) to workaround an issue in Windows 10
    // ExitProcess(result);
    //
    // printf("[mtee]\n");
    //
    // return 0;

    if (argc < 2)
    {
        printf("Usage: %s <input file>\n", argv[0]);
        return 0;
    }

    if (argc == 3 && !strcmp(argv[2], "loop"))
    {
        // loop inside application and call target infinitely
        while (true)
        {
            test_target(argv[1], argv[0]);
        }
    }
    else
    {
        // regular single target call
        return test_target(argv[1], argv[0]);
    }
}
