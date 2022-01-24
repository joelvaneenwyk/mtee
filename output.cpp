#include "header.h"

constexpr int MAX_DATE_TIME_LEN = 25;

BOOL WriteBufferToConsoleAndFilesA(LPARGS args, PCHAR lpBuf, DWORD dwCharsRead, BOOL AddDate, BOOL AddTime)
{
    DWORD dwBytesWritten;
    PFILEINFO fi;
    static BOOL bNewLine = TRUE;
    PCHAR pHead, pTail;

    if (AddDate | AddTime)
    {
        CHAR szDT[MAX_DATE_TIME_LEN];
        pHead = pTail = lpBuf;
        while (pTail < (lpBuf + dwCharsRead))
        {
            if (bNewLine)
            {
                DWORD dwBR = GetFormattedDateTimeA(szDT, AddDate, AddTime);
                if (!WriteBufferToConsoleAndFilesA(args, szDT, dwBR, FALSE, FALSE))
                    return FALSE;
                bNewLine = FALSE;
            }
            if (*pTail++ == 0x0A)
            {
                bNewLine = TRUE;
                if (!WriteBufferToConsoleAndFilesA(args, pHead, (DWORD)(pTail - pHead), FALSE, FALSE))
                    return FALSE;
                pHead = pTail;
            }
        }
        if (!WriteBufferToConsoleAndFilesA(args, pHead, (DWORD)(pTail - pHead), FALSE, FALSE))
            return FALSE;
    }
    else
    {
        fi = &args->fi;
        while (fi)
        {
            if (fi->hFile != INVALID_HANDLE_VALUE)
            {
                BOOL ok;
                if (fi->bIsConsole)
                    ok = WriteConsoleA(fi->hFile, lpBuf, dwCharsRead, &dwBytesWritten, nullptr);
                else
                    ok = WriteFile(fi->hFile, lpBuf, dwCharsRead, &dwBytesWritten, nullptr);
                if (!ok && !args->bContinue)
                    return FALSE;
            }
            fi = fi->fiNext;
        }
    }
    return TRUE;
}

BOOL WriteBufferToConsoleAndFilesW(LPARGS args, PWCHAR lpBuf, DWORD dwCharsRead, BOOL AddDate, BOOL AddTime)
{
    DWORD dwBytesWritten;
    PFILEINFO fi;
    static BOOL bNewLine = TRUE;
    PWCHAR pHead, pTail;

    if (AddDate | AddTime)
    {
        WCHAR szDT[MAX_DATE_TIME_LEN];
        pHead = pTail = (PWCHAR)lpBuf;
        while (pTail < (lpBuf + dwCharsRead))
        {
            if (bNewLine)
            {
                DWORD dwBR = GetFormattedDateTimeW(szDT, AddDate, AddTime);
                if (!WriteBufferToConsoleAndFilesW(args, szDT, dwBR, FALSE, FALSE))
                    return FALSE;
                bNewLine = FALSE;
            }
            if (*pTail++ == L'\n')
            {
                bNewLine = TRUE;
                if (!WriteBufferToConsoleAndFilesW(args, pHead, (DWORD)(pTail - pHead), FALSE, FALSE))
                    return FALSE;
                pHead = pTail;
            }
        }
        if (!WriteBufferToConsoleAndFilesW(args, pHead, (DWORD)(pTail - pHead), FALSE, FALSE))
            return FALSE;
    }
    else
    {
        fi = &args->fi;
        while (fi)
        {
            if (fi->hFile != INVALID_HANDLE_VALUE)
            {
                BOOL ok;
                if (fi->bIsConsole)
                    // The ANSI version perhaps works identically to WriteFile, however in UNICODE version
                    // WriteConsoleW is definitely needed so that it understands that we are outputting UNICODE
                    // characters
                    ok = WriteConsoleW(fi->hFile, lpBuf, dwCharsRead, &dwBytesWritten, nullptr);
                else
                    ok = WriteFile(fi->hFile, lpBuf, dwCharsRead * sizeof(WCHAR), &dwBytesWritten, nullptr);
                if (!ok && !args->bContinue)
                    return FALSE;
            }
            fi = fi->fiNext;
        }
    }
    return TRUE;
}

BOOL AnsiToUnicode(PWCHAR *lpDest, PCHAR lpSrc, LPDWORD lpSize)
{
    if (*lpDest)
    {
        if (!HeapFree(GetProcessHeap(), 0, *lpDest))
            return FALSE;
    }

    int iWideCharLen = MultiByteToWideChar(GetConsoleCP(), 0, lpSrc, (int)*lpSize, nullptr, 0);

    // temp change RE lienhart post
    // iWideCharLen = MultiByteToWideChar(CP_ACP, 0, lpSrc, *lpSize, NULL, 0);

    *lpDest = (PWCHAR)HeapAlloc(GetProcessHeap(), 0, (size_t)iWideCharLen * sizeof(WCHAR));
    if (lpDest == nullptr)
        return FALSE;
    //
    // set lpsize to number of chars
    //

    *lpSize = (DWORD)MultiByteToWideChar(GetConsoleCP(), 0, lpSrc, (int)*lpSize, *lpDest, iWideCharLen);

    // temp change RE lienhart post
    //*lpSize = MultiByteToWideChar(CP_ACP, 0, lpSrc, *lpSize, *lpDest, iWideCharLen);

    return TRUE;
}

BOOL UnicodeToAnsi(PCHAR *lpDest, PWCHAR lpSrc, LPDWORD lpSize)
{
    if (*lpDest)
    {
        if (!HeapFree(GetProcessHeap(), 0, *lpDest))
            return FALSE;
    }
    
    int iAnsiCharLen = WideCharToMultiByte(GetConsoleCP(), 0, lpSrc, (int)*lpSize, nullptr, 0, nullptr, nullptr);
    *lpDest = (PCHAR)HeapAlloc(GetProcessHeap(), 0, (size_t)iAnsiCharLen * sizeof(WCHAR));
    if (lpDest == nullptr)
        return FALSE;

    *lpSize = (DWORD)WideCharToMultiByte(GetConsoleCP(), 0, lpSrc, (int)*lpSize, *lpDest, iAnsiCharLen, nullptr, nullptr);

    // temp change RE lienhart post CP_ACP
    //*lpSize = WideCharToMultiByte(CP_ACP, 0, lpSrc, *lpSize, *lpDest, iAnsiCharLen, NULL, NULL);

    return TRUE;
}

BOOL WriteBom(PFILEINFO fi, BOOL bContinue)
{
    DWORD dwBytesWritten;
    WCHAR wcBom = 0xFEFF;
    while (fi)
    {
        DWORD dwFileSizeHigh = 0;
        DWORD siz = GetFileSize(fi->hFile, &dwFileSizeHigh);
        if ((fi->hFile != INVALID_HANDLE_VALUE) && (siz == 0 && dwFileSizeHigh == 0))
        {
            if (!WriteFile(fi->hFile, &wcBom, sizeof(WCHAR), &dwBytesWritten, nullptr))
            {
                if (!bContinue)
                    return FALSE;
            }
        }
        fi = fi->fiNext;
    }

    return TRUE;
}
