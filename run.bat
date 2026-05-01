@echo off
setlocal enabledelayedexpansion

title Orion Dumper
echo.
echo  =========================================
echo    Orion Dumper - Roblox Offset Extractor
echo  =========================================
echo.

:: ─── Locate Dumper.exe ───────────────────────────────────────────────────────
set "DUMPER_EXE="

if exist "build\Dumping\Release\Dumper.exe"     set "DUMPER_EXE=build\Dumping\Release\Dumper.exe"
if exist "Dumping\build\Release\Dumper.exe"     set "DUMPER_EXE=Dumping\build\Release\Dumper.exe"
if exist "build\Release\Dumper.exe"             set "DUMPER_EXE=build\Release\Dumper.exe"
if exist "Dumper.exe"                           set "DUMPER_EXE=Dumper.exe"

if not defined DUMPER_EXE (
    echo [ERROR] Dumper.exe not found. Please build the project first.
    echo         Expected locations:
    echo           build\Dumping\Release\Dumper.exe
    echo           Dumping\build\Release\Dumper.exe
    pause
    exit /b 1
)

echo [Orion] Found dumper: %DUMPER_EXE%
echo [Orion] Starting dump...
echo.

:: ─── Run the Dumper ──────────────────────────────────────────────────────────
"%DUMPER_EXE%"
set "DUMP_EXIT=%ERRORLEVEL%"

echo.
if %DUMP_EXIT% NEQ 0 (
    echo [ERROR] Dumper exited with code %DUMP_EXIT%. Check output above.
    pause
    exit /b %DUMP_EXIT%
)

echo [Orion] Dump completed successfully.
echo.

:: ─── Check if Offsets.hpp was produced ───────────────────────────────────────
set "OFFSETS_FOUND=0"
if exist "Dumps\Offsets.hpp" set "OFFSETS_FOUND=1"
if exist "Offsets.hpp"       set "OFFSETS_FOUND=1"

if "%OFFSETS_FOUND%"=="0" (
    echo [WARNING] No Offsets.hpp found after dump. Skipping GitHub release.
    pause
    exit /b 0
)

:: ─── Auto-publish countdown ──────────────────────────────────────────────────
echo  =========================================
echo    Auto-publishing to GitHub in 10 seconds
echo    Press CTRL+C to cancel
echo  =========================================
echo.

:: Countdown using ping delay trick (works without timeout.exe)
for /L %%i in (10,-1,1) do (
    <nul set /p "=  Publishing in %%i seconds...   ^r"
    ping -n 2 127.0.0.1 >nul
)
echo.
echo.

:: ─── Publish ─────────────────────────────────────────────────────────────────
echo [Orion] Launching publisher...
echo.
python publish.py
set "PUB_EXIT=%ERRORLEVEL%"

echo.
if %PUB_EXIT% EQU 0 (
    echo [Orion] All done! Check GitHub for your new release.
) else (
    echo [Orion] Publisher exited with errors. See output above.
)

echo.
pause
