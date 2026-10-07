@echo off
rem Formats Nyx's own C++ code with the rules in .clang-format (project root).
rem   FormatCode.bat         formats all files in place
rem   FormatCode.bat check   changes nothing, only lists files that aren't formatted yet
rem External code (ThirdParty, Dependencies) and the header tool's test fixtures are
rem skipped through their own .clang-format files.
setlocal
set ROOT=%~dp0..
set FOLDERS=Engine Editor Tools Startup
set MODE=%~1

call :FindClangFormat || goto :notFound

rem An older clang-format rejects the whole config, so test it on one file first.
"%CLANG_FORMAT%" --dry-run "%ROOT%\Editor\Source\main.cpp" >nul 2>&1 || goto :badConfig

set /a FILES=0, UNFORMATTED=0, FAILED=0
for %%D in (%FOLDERS%) do call :FormatFolder "%ROOT%\%%D"

echo.
if /i "%MODE%"=="check" (
    echo %UNFORMATTED% of %FILES% files need formatting.
    pause
    if %UNFORMATTED% gtr 0 exit /b 1
    exit /b 0
)
echo Formatted %FILES% files.
if %FAILED% gtr 0 (
    echo %FAILED% files could not be formatted; see the messages above.
    pause
    exit /b 1
)
pause
exit /b 0

:FormatFolder
for /r "%~1" %%F in (*.h *.cpp) do (
    set /a FILES+=1
    if /i "%MODE%"=="check" (
        "%CLANG_FORMAT%" --dry-run --Werror "%%F" >nul 2>&1 || (
            echo Needs formatting: %%F
            set /a UNFORMATTED+=1
        )
    ) else (
        "%CLANG_FORMAT%" -i "%%F" || set /a FAILED+=1
    )
)
exit /b 0

rem Uses clang-format from PATH, otherwise the one that comes with Visual Studio.
:FindClangFormat
where clang-format >nul 2>&1 && (
    set CLANG_FORMAT=clang-format
    exit /b 0
)
set VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe
if not exist "%VSWHERE%" exit /b 1
for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -property installationPath`) do set VS_DIR=%%I
for %%C in (
    "%VS_DIR%\VC\Tools\Llvm\x64\bin\clang-format.exe"
    "%VS_DIR%\VC\Tools\Llvm\bin\clang-format.exe"
    "%VS_DIR%\Common7\IDE\VC\vcpackages\clang-format.exe"
) do if exist %%C (
    set CLANG_FORMAT=%%~C
    exit /b 0
)
exit /b 1

:notFound
echo clang-format was not found. Install LLVM or the "C++ Clang tools for Windows"
echo component in the Visual Studio Installer, or add clang-format to your PATH.
pause
exit /b 1

:badConfig
echo clang-format rejected .clang-format: "%CLANG_FORMAT%" is probably older than version 17.
echo Install a newer LLVM and make sure its clang-format comes first on your PATH.
pause
exit /b 1
