@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
for %%I in ("%SCRIPT_DIR%\..\..\..") do set "REPO_ROOT=%%~fI"
set "BUILD_DIR=%REPO_ROOT%\Build\Windows"

echo REPO_ROOT = %REPO_ROOT%
echo BUILD_DIR = %BUILD_DIR%
echo.

if not exist "%BUILD_DIR%" (
    echo Build directory does not exist:
    echo   %BUILD_DIR%
    exit /b 1
)

cd /d "%BUILD_DIR%"
ctest -C Debug -R NyxHeaderToolTests --output-on-failure

if errorlevel 1 (
    echo.
    echo NyxHeaderTool tests FAILED.
    exit /b 1
)

echo.
echo NyxHeaderTool tests passed.
exit /b 0