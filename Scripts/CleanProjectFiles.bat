@echo off

set ROOT=%~dp0..
set BUILD_DIR=%ROOT%\Build\Windows

echo ROOT=%ROOT%
echo BUILD_DIR=%BUILD_DIR%

if exist "%BUILD_DIR%" (
    echo Cleaning build direcotry...
    rmdir /s /q "%BUILD_DIR%"
    echo Build directory removed.
) else (
    echo Nothing to clean.
)