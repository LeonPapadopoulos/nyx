@echo off
set ROOT=%~dp0..
set BUILD_DIR=%ROOT%\Build\Windows

cmake -S "%ROOT%" -B "%BUILD_DIR%" -G "Visual Studio 17 2022" -A x64