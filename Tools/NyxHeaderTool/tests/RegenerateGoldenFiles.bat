@echo off
setlocal enabledelayedexpansion

set "SCRIPT_DIR=%~dp0"
for %%I in ("%SCRIPT_DIR%\..\..\..") do set "REPO_ROOT=%%~fI"
set "BUILD_DIR=%REPO_ROOT%\Build\Windows"
set "TOOL_EXE=%BUILD_DIR%\Binaries\Debug\NyxHeaderTool.exe"
set "FIXTURES_DIR=%SCRIPT_DIR%fixtures"
set "TEMP_ROOT=%BUILD_DIR%\NyxHeaderToolGoldenGen"

echo SCRIPT_DIR   = %SCRIPT_DIR%
echo REPO_ROOT    = %REPO_ROOT%
echo BUILD_DIR    = %BUILD_DIR%
echo TOOL_EXE     = %TOOL_EXE%
echo FIXTURES_DIR = %FIXTURES_DIR%
echo TEMP_ROOT    = %TEMP_ROOT%
echo.

if not exist "%TOOL_EXE%" (
    echo Could not find NyxHeaderTool at:
    echo   %TOOL_EXE%
    echo Build NyxHeaderTool first.
    exit /b 1
)

if not exist "%FIXTURES_DIR%" (
    echo Could not find fixtures directory:
    echo   %FIXTURES_DIR%
    exit /b 1
)

if exist "%TEMP_ROOT%" (
    rmdir /s /q "%TEMP_ROOT%"
)
mkdir "%TEMP_ROOT%"

set "FOUND_ANY=0"

for /D %%F in ("%FIXTURES_DIR%\*") do (
    if exist "%%~fF\Input.h" (
        set "FOUND_ANY=1"

        echo ========================================
        echo Regenerating fixture: %%~nF
        echo Fixture dir: %%~fF

        set "CASE_DIR=%%~fF"
        set "OUT_DIR=%TEMP_ROOT%\%%~nF"

        if exist "!OUT_DIR!" (
            rmdir /s /q "!OUT_DIR!"
        )
        mkdir "!OUT_DIR!"

        echo Running tool...
        "%TOOL_EXE%" ^
          --scan-root "!CASE_DIR!" ^
          --output-dir "!OUT_DIR!" ^
          --module-init-header "!OUT_DIR!\Runtime.reflect.init.h" ^
          --module-init-cpp "!OUT_DIR!\Runtime.reflect.init.cpp"

        if errorlevel 1 (
            echo Failed generating fixture %%~nF
            exit /b 1
        )

        echo Copying generated files back to fixture...
        copy /y "!OUT_DIR!\Input.reflect.h" "%%~fF\Expected.reflect.h" >nul
        if errorlevel 1 (
            echo Failed copying Expected.reflect.h for %%~nF
            exit /b 1
        )

        copy /y "!OUT_DIR!\Runtime.reflect.init.h" "%%~fF\Expected.init.h" >nul
        if errorlevel 1 (
            echo Failed copying Expected.init.h for %%~nF
            exit /b 1
        )

        copy /y "!OUT_DIR!\Runtime.reflect.init.cpp" "%%~fF\Expected.init.cpp" >nul
        if errorlevel 1 (
            echo Failed copying Expected.init.cpp for %%~nF
            exit /b 1
        )

        echo Done: %%~nF
        echo.
    )
)

if "%FOUND_ANY%"=="0" (
    echo No fixture directories containing Input.h were found under:
    echo   %FIXTURES_DIR%
    exit /b 1
)

echo ========================================
echo Golden files regenerated successfully.
exit /b 0