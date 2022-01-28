@echo off

setlocal EnableDelayedExpansion

set _root=%~dp0..\
if %_root:~-1%==\ set _root=%_root:~0,-1%

del "%_root%\afl.mtee.exe.*"
del "%_root%\afl.*.txt"
del "%_root%\mtee.txt"

if exist "%_root%\tests\output" rmdir /s /q "%_root%\tests\output"
mkdir "%_root%\tests\output"

set _mtee=%_root%\workspace\x64\Debug\mtee.exe

set _afl_fuzz=%_root%\tests\winafl\build\bin\Release\afl-fuzz.exe
set _winafl=%_root%\tests\winafl\build\bin\Release\winafl.dll
set _dynamo=%_root%\tests\DynamoRIO\9.0.0\bin64

set _msbuild=C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe
set _dumpbin=C:\program files\Microsoft Visual Studio\2022\Enterprise\VC\Tools\MSVC\14.30.30705\bin\Hostx64\x64\dumpbin.exe

set _input=%_root%\tests\input
set _output=%_root%\tests\output
set _target_method=main
set _target_module=%_mtee%
set _target_module_name=mtee.exe
set _target_offset=0x14EF0
set _target_args=2
set _target_iterations=100

::
:: Use MSBuild to compile Debug and Release binaries
::

call :Execute "%_msbuild%" /property:Configuration=Release "%_root%\workspace\mtee.sln"
call :Execute "%_msbuild%" /property:Configuration=Debug "%_root%\workspace\mtee.sln"

::
:: Build `winafl` from source
::

set "config=Release"
set "settings=-DCMAKE_BUILD_TYPE=%config%  -DUSE_COLOR=1 -DUSE_DRSYMS=1 -DDynamoRIO_DIR=%_root%/tests/DynamoRIO/9.0.0/cmake"
cmake -S "%_root%\tests\winafl" -B "%_root%\tests\winafl\build" -G "Visual Studio 16 2019" -A x64 %settings% -DCMAKE_INSTALL_PREFIX="%_root%\tests\winafl\bin64"
cmake --build "%_root%\tests\winafl\build" --config %config%
if errorlevel 1 (
    echo Failed to build `winafl` from source.
    exit /b 1
)

::
:: `dumpbin` Dump all symbols so we can validate target offset
::

echo ##[cmd] "%_dumpbin%" /all "%_mtee%"
"%_dumpbin%" /all "%_mtee%" >"%~dp0mtee.txt"

::
:: `drrun` Use this to validate that we can instrument the executable
::

echo ##[cmd] "%_dynamo%\drrun.exe" --help
"%_dynamo%\drrun.exe" --help >"%~dp0afl.drrun.txt"
set _dr_run=-stats -mem -verbose
set _dr_run=!_dr_run! -c "%_winafl%"
set _dr_run=!_dr_run! -debug
set _dr_run=!_dr_run! -target_method %_target_method%
set _dr_run=!_dr_run! -target_module "%_target_module%"
set _dr_run=!_dr_run! -target_offset %_target_offset%
set _dr_run=!_dr_run! -fuzz_iterations %_target_iterations%
set _dr_run=!_dr_run! -nargs %_target_args%
call :Execute "%_dynamo%\drrun.exe" !_dr_run! -- "%_mtee%"
if errorlevel 1 (
    echo Failed.
    exit /b 1
)
echo [SUCCESS] Instrumenting module succeeded.

::
:: afl_fuzz
::

set _afl_fuzz_args=-w "%_winafl%" -t 4000 -i "%_input%" -o "%_output%" -D "%_dynamo%"
set _afl_instrumentation=-debug
set _afl_instrumentation=!_afl_instrumentation! -coverage_module "%_target_module%"
set _afl_instrumentation=!_afl_instrumentation! -target_module "%_target_module%"
set _afl_instrumentation=!_afl_instrumentation! -target_offset %_target_offset%
set _afl_instrumentation=!_afl_instrumentation! -target_method %_target_method%
set _afl_instrumentation=!_afl_instrumentation! -nargs %_target_args%
set _afl_instrumentation=!_afl_instrumentation! -fuzz_iterations %_target_iterations%
::set _afl_instrumentation=!_afl_instrumentation! -call_convention thiscall
::set _afl_instrumentation=!_afl_instrumentation! -covtype edge
call :Execute "%_afl_fuzz%" %_afl_fuzz_args% -- !_afl_instrumentation! -- "%_mtee%"

exit /b

:Execute
    echo ##[cmd] %*
    %*
exit /b
