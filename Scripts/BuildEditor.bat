@echo off
rem Regenerates the project files and builds the editor (Debug), including NyxEngine.dll.
setlocal
set ROOT=%~dp0..

call "%~dp0GenerateProjectFiles.bat" || goto :failed
cmake --build "%ROOT%\Build\Windows" --config Debug --target NyxEditor || goto :failed
echo.
echo Editor built: %ROOT%\Build\Windows\Binaries\Debug\NyxEditor.exe
pause
exit /b 0

:failed
echo.
echo Build failed; see the messages above.
pause
exit /b 1
