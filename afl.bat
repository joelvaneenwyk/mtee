@echo off

setlocal EnableDelayedExpansion

set _root=%~dp0
if %_root:~-1%==\ set _root=%_root:~0,-1%

del "%_root%\afl.mtee.exe.*"
rmdir /s /q "%_root%\tests\output"
mkdir "%_root%\tests\output"

set _msbuild=C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe
"%_msbuild%" /property:Configuration=Release "%_root%\workspace\mtee.sln"
"%_msbuild%" /property:Configuration=Debug "%_root%\workspace\mtee.sln"

set _mtee=%_root%\workspace\x64\Release\mtee.exe

set _dumpbin=C:\Program Files (x86)\Microsoft Visual Studio\2019\Enterprise\VC\Tools\MSVC\14.29.30133\bin\Hostx86\x86\dumpbin.exe
"%_dumpbin%" /all "%_mtee%" >mtee.txt

set _afl_fuzz=%_root%\..\winafl\build\bin\Release\afl-fuzz.exe
set _winafl=%_root%\..\winafl\build\bin\Release\winafl.dll
set _dynamo=%_root%\..\DynamoRIO\9.0.0\bin64
set _input=%~dp0tests\input
set _output=%~dp0tests\output

"%_dynamo%\drrun.exe" --help >afl.drrun.txt
set _dr_run=-stats -mem -verbose
set _dr_run=!_dr_run! -c "%_winafl%"
set _dr_run=!_dr_run! -debug
set _dr_run=!_dr_run! -target_function main
set _dr_run=!_dr_run! -target_module "%_mtee%"
set _dr_run=!_dr_run! -target_offset 0x10d0
set _dr_run=!_dr_run! -fuzz_iterations 100
set _dr_run=!_dr_run! -nargs 2
echo ##[cmd] "%_dynamo%\drrun.exe" !_dr_run! -- "%_mtee%"
"%_dynamo%\drrun.exe" !_dr_run! -- "%_mtee%"
if errorlevel 1 (
    echo Failed.
    exit /b 1
)

set _afl_fuzz_args=-w "%_winafl%" -t 4000 -i "%_input%" -o "%_output%" -D "%_dynamo%"
set _instrumentation=-debug
set _instrumentation=!_instrumentation! -coverage_module mtee.exe
set _instrumentation=!_instrumentation! -target_module mtee.exe
set _instrumentation=!_instrumentation! -target_offset 0x10d0
set _instrumentation=!_instrumentation! -target_method main -nargs 2
set _instrumentation=!_instrumentation! -fuzz_iterations 5000
set _instrumentation=!_instrumentation! -call_convention thiscall
set _instrumentation=!_instrumentation! -covtype edge

echo ##[cmd] "%_afl_fuzz%" %_afl_fuzz_args% -- !_instrumentation! -- "%_mtee%"
"%_afl_fuzz%" %_afl_fuzz_args% -- !_instrumentation! -- "%_mtee%"
