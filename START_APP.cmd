@echo off
cd /d "%~dp0"
if not exist "build\Release\portfolio_app.exe" (
 echo Build the project first: cmake -S . -B build
 echo cmake --build build --config Release
 pause
 exit /b 1
)
"build\Release\portfolio_app.exe"
if errorlevel 1 pause
