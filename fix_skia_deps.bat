@echo off
setlocal enabledelayedexpansion

REM ========================================
REM Skia - Manual Dependency Fix
REM Use this if git-sync-deps fails
REM ========================================

set "SCRIPT_DIR=%~dp0"
set "SKIA_DIR=%SCRIPT_DIR%deps\skia"

echo ========================================
echo Skia Dependency Fix Script
echo ========================================
echo.
echo This script helps fix common Skia dependency issues:
echo   - SSL errors during emsdk download
echo   - Missing third_party components
echo.

REM Check if Skia exists
if not exist "%SKIA_DIR%" (
    echo ERROR: Skia not found at %SKIA_DIR%
    echo Run sync_deps.bat first.
    exit /b 1
)

cd /d "%SKIA_DIR%"

REM ========================================
REM Option 1: Skip emsdk (not needed for native builds)
REM ========================================

echo.
echo Option 1: Disable emsdk (for native builds, not WebAssembly)
echo.

REM Create emsdk skip file
if not exist "third_party\externals" mkdir "third_party\externals"

REM Create a dummy emsdk directory
if not exist "third_party\externals\emsdk" mkdir "third_party\externals\emsdk"

echo Emsdk directory created. Skia will build without WebAssembly support.
echo This is fine for native Windows/Linux builds.

REM ========================================
REM Option 2: Re-run git-sync-deps with retries
REM ========================================

echo.
echo Option 2: Retry dependency sync
echo.

set /p RETRY="Retry git-sync-deps? (y/n): "

if /i "%RETRY%"=="y" (
    echo.
    echo Retrying with Python...
    
    REM Set SSL cert to ignore (workaround for SSL issues)
    set "PYTHONHTTPSVERIFY=0"
    
    python tools/git-sync-deps
    
    if %ERRORLEVEL% neq 0 (
        echo.
        echo git-sync-deps still failed.
        echo.
        echo This might be OK if the only failure is emsdk.
        echo Try building Skia anyway.
    )
)

REM ========================================
REM Option 3: Manual download of specific deps
REM ========================================

echo.
echo ========================================
echo Manual Fix Suggestions
echo ========================================
echo.
echo If git-sync-deps continues to fail:
echo.
echo 1. For SSL/emscripten issues:
echo    - These are not needed for native builds
echo    - The build should work anyway
echo.
echo 2. Try with a proxy:
echo    sync_deps.bat --proxy http://127.0.0.1:7890
echo.
echo 3. Use Python script directly:
echo    python sync_deps.py --skip-skia-deps
echo    Then manually run: cd deps\skia ^&^& python tools/git-sync-deps
echo.
echo 4. Download pre-built Skia:
echo    See: https://github.com/nickolasburr/skia-builds
echo.

cd /d "%SCRIPT_DIR%"
endlocal
