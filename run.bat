@echo off
setlocal enabledelayedexpansion

echo [Orion] Starting Roblox Dumper...

:: Run the dumper
:: Note: Adjust path if necessary. We assume the user has built the project.
if exist "build\Dumping\Release\Dumper.exe" (
    "build\Dumping\Release\Dumper.exe"
) else if exist "Dumping\build\Release\Dumper.exe" (
    "Dumping\build\Release\Dumper.exe"
) else (
    echo [!] Dumper.exe not found. Please build the project first.
    pause
    exit /b
)

echo.
echo [Orion] Dump complete.
echo.

set /p choice="Do you want to publish these offsets to GitHub? (y/n): "

if /i "%choice%"=="y" (
    echo [Orion] Syncing with GitHub...
    python publish.py
) else (
    echo [Orion] Publish skipped.
)

pause
