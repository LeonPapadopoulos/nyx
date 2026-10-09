@echo off
rem Regenerates the project files, builds the game (Debug) and starts it.
rem Optional argument: the scene file to run. Without one, the game runs Assets\Scenes\Default.nyxscene.
setlocal
set ROOT=%~dp0..
set BINARIES=%ROOT%\Build\Windows\Binaries\Debug

call "%~dp0GenerateProjectFiles.bat" || goto :failed
cmake --build "%ROOT%\Build\Windows" --config Debug --target NyxGame || goto :failed
start "" "%BINARIES%\NyxGame.exe" %1
exit /b 0

:failed
echo.
echo Build failed; see the messages above.
pause
exit /b 1
