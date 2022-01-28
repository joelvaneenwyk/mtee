@echo off

setlocal EnableDelayedExpansion

call :RunTee "%~dp0"
exit /b

:RunTee
    setlocal EnableDelayedExpansion
    set _root=%~dp1
    if %_root:~-1%==\ set _root=%_root:~0,-1%

    set _mtee_config=Debug
    set _mtee=%_root%\workspace\x64\%_mtee_config%\mtee.exe

    set _msbuild=C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe

    "%_msbuild%" /property:Configuration=%_mtee_config% "%_root%\workspace\mtee.sln" > nul 2>&1
    if errorlevel 1 (
        echo [ERROR] Failed to build `mtee` solution.
        exit /b 99
    )

    "%_mtee%" %*
exit /b
