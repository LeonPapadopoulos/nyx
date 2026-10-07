@echo off
rem Regenerates the project files, builds the startup preview and runs it.
setlocal
set ROOT=%~dp0..

call "%~dp0GenerateProjectFiles.bat" || goto :failed
cmake --build "%ROOT%\Build\Windows" --config Debug --target NyxStartupPreview || goto :failed
start "" "%ROOT%\Build\Windows\Binaries\Debug\NyxStartupPreview.exe"
exit /b 0

:failed
echo.
echo Build failed; see the messages above.
pause
exit /b 1
