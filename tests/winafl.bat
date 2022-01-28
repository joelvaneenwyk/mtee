@echo off

setlocal EnableDelayedExpansion

call :RunWinAfl "%~dp0..\"
exit /b

:RunWinAfl
    setlocal EnableDelayedExpansion
    set _root=%~dp1
    if %_root:~-1%==\ set _root=%_root:~0,-1%
    echo Root: !_root!

    set _tests=%_root%\tests
    del "%_tests%\afl.mtee.exe.*"
    del "%_tests%\afl.*.txt"
    del "%_tests%\mtee.txt"

    if exist "%_tests%\output" rmdir /s /q "%_tests%\output"
    mkdir "%_tests%\output"

    set _mtee_config=Debug
    set _mtee=%_root%\workspace\x64\%_mtee_config%\mtee.exe

    set _winafl_root=%_tests%\winafl
    set _afl_fuzz=%_winafl_root%\build\bin\Release\afl-fuzz.exe
    set _winafl=%_winafl_root%\build\bin\Release\winafl.dll

    set _dynamorio_root=%_tests%\dynamorio
    set _dynamorio_bin=%_dynamorio_root%\build\bin64

    set _msbuild=C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe
    set _dumpbin=C:\program files\Microsoft Visual Studio\2022\Enterprise\VC\Tools\MSVC\14.30.30705\bin\Hostx64\x64\dumpbin.exe

    set _input=%_tests%\input
    set _output=%_tests%\output
    set _target_method=main
    set _target_module=%_mtee%
    set _target_module_name=mtee.exe
    set _target_offset=0x14EF0
    set _target_args=2
    set _target_iterations=100

    ::
    :: Use MSBuild to compile `mtee` for the configuration we are testing
    ::

    call :Execute "%_msbuild%" /property:Configuration=%_mtee_config% "%_root%\workspace\mtee.sln"

    ::
    :: Build `dynamorio` from source.
    ::

    set "_dynamorio_config=RelWithDebInfo"
    set "_dynamorio_settings=-DCMAKE_BUILD_TYPE=%_dynamorio_config%"
    if not exist "%_dynamorio_root%\build" mkdir "%_dynamorio_root%\build"

    if exist "%_dynamorio_bin%\drrun.exe" goto:$SkipDynamorioBuild
        cmake -S "%_dynamorio_root%" -B "%_dynamorio_root%\build" -G "Visual Studio 16 2019" -A x64 %_dynamorio_settings%
        if errorlevel 1 (
            echo Failed to generate `dynamorio` project files.
            exit /b 1
        )

        cmake --build "%_dynamorio_root%\build" --config %_dynamorio_config%
        if errorlevel 1 (
            echo Failed to build `dynamorio` from source.
            exit /b 1
        )
    :$SkipDynamorioBuild

    ::
    :: Build `winafl` from source
    ::

    set "_winafl_config=Release"
    set "_winafl_settings=-DCMAKE_BUILD_TYPE=%_winafl_config%  -DUSE_COLOR=1 -DUSE_DRSYMS=1 -DDynamoRIO_DIR=%_root%/tests/dynamorio/build/cmake"

    if exist "%_afl_fuzz%" goto:$SkipWinAflBuild
        cmake -S "%_winafl_root%" -B "%_winafl_root%\build" -G "Visual Studio 16 2019" -A x64 %_winafl_settings% -DCMAKE_INSTALL_PREFIX="%_winafl_root%\bin64"
        if errorlevel 1 (
            echo Failed to generate `winafl` project files.
            exit /b 1
        )

        cmake --build "%_winafl_root%\build" --config %_winafl_config%
        if errorlevel 1 (
            echo Failed to build `winafl` from source.
            exit /b 1
        )
    :$SkipWinAflBuild

    ::
    :: `dumpbin` Dump all symbols so we can validate target offset
    ::

    echo ##[cmd] "%_dumpbin%" /all "%_mtee%"
    "%_dumpbin%" /all "%_mtee%" >"%_tests%\mtee.txt"

    ::
    :: `drrun` Use this to validate that we can instrument the executable
    ::

    echo ##[cmd] "%_dynamorio_bin%\drrun.exe" --help
    "%_dynamorio_bin%\drrun.exe" --help >"%_tests%\afl.drrun.txt"
    set _dr_run=-stats -mem -verbose
    set _dr_run=!_dr_run! -c "%_winafl%"
    set _dr_run=!_dr_run! -debug
    set _dr_run=!_dr_run! -target_method %_target_method%
    set _dr_run=!_dr_run! -target_module "%_target_module_name%"
    ::set _dr_run=!_dr_run! -target_offset %_target_offset%
    set _dr_run=!_dr_run! -fuzz_iterations %_target_iterations%
    set _dr_run=!_dr_run! -nargs %_target_args%
    call :Execute "%_dynamorio_bin%\drrun.exe" !_dr_run! -- "%_mtee%"
    if errorlevel 1 (
        echo Failed.
        exit /b 1
    )
    echo [SUCCESS] Instrumenting module succeeded.

    ::
    :: afl_fuzz
    ::

    set _afl_fuzz_args=-w "%_winafl%" -t 4000 -i "%_input%" -o "%_output%" -D "%_dynamorio_bin%"
    set _afl_instrumentation=-debug
    set _afl_instrumentation=!_afl_instrumentation! -coverage_module "%_target_module_name%"
    set _afl_instrumentation=!_afl_instrumentation! -target_module "%_target_module_name%"
    set _afl_instrumentation=!_afl_instrumentation! -target_method %_target_method%
    set _afl_instrumentation=!_afl_instrumentation! -nargs %_target_args%
    set _afl_instrumentation=!_afl_instrumentation! -fuzz_iterations %_target_iterations%
    ::set _afl_instrumentation=!_afl_instrumentation! -target_offset %_target_offset%
    ::set _afl_instrumentation=!_afl_instrumentation! -call_convention thiscall
    ::set _afl_instrumentation=!_afl_instrumentation! -covtype edge
    call :Execute "%_afl_fuzz%" %_afl_fuzz_args% -- !_afl_instrumentation! -- "%_mtee%"
exit /b

:Execute
    echo ##[cmd] %*
    %*
exit /b
