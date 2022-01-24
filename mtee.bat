@echo off

setlocal EnableDelayedExpansion

set _root=%~dp0
set _mtee=%_root%\workspace\x64\Debug\mtee.exe

"%_mtee%" %*
