@echo off
setlocal EnableExtensions
REM Pack RelWithDebInfo (or Debug) output with Velopack (vpk).
REM Prereq: dotnet tool install -g vpk
REM Usage:
REM   scripts\pack-velopack.bat 0.3.2
REM   scripts\pack-velopack.bat 0.3.2 Debug
REM
REM Output: releases\QiuckPrompts-win-Setup.exe, Portable.zip, *.nupkg, releases.win.json
REM Note: pack version must be >= 0.0.1 (Velopack rejects 0.0.0*).

if "%~1"=="" (
  echo Usage: %~nx0 VERSION [Debug^|RelWithDebInfo]
  echo Example: %~nx0 0.3.2
  exit /b 2
)

set "VER=%~1"
set "CFG=%~2"
if "%CFG%"=="" set "CFG=RelWithDebInfo"

if /I "%VER%"=="0.0.0" (
  echo Velopack requires pack version ^>= 0.0.1
  exit /b 2
)
if /I "%VER%"=="0.0.0-dev" (
  echo Velopack requires pack version ^>= 0.0.1 — use a real release tag version
  exit /b 2
)

cd /d "%~dp0.."
set "OUT=build\%CFG%"
set "PACKDIR=build\velopack-pack"
set "RELDIR=releases"

if not exist "%OUT%\qiuckprompts.exe" (
  echo Missing %OUT%\qiuckprompts.exe — build that config first.
  exit /b 1
)

where vpk >nul 2>&1
if errorlevel 1 (
  echo vpk not on PATH. Install:  dotnet tool install -g vpk
  exit /b 1
)

echo Packing version %VER% from %OUT% ...
if exist "%PACKDIR%" rmdir /s /q "%PACKDIR%"
mkdir "%PACKDIR%" || exit /b 1
copy /y "%OUT%\qiuckprompts.exe" "%PACKDIR%\" >nul || exit /b 1
if exist "%OUT%\qiuckprompts.pdb" copy /y "%OUT%\qiuckprompts.pdb" "%PACKDIR%\" >nul
if exist "%OUT%\config" xcopy /e /i /y "%OUT%\config" "%PACKDIR%\config\" >nul
if exist "%OUT%\extension" xcopy /e /i /y "%OUT%\extension" "%PACKDIR%\extension\" >nul
if exist "README.md" copy /y "README.md" "%PACKDIR%\" >nul
if exist "LICENSE" copy /y "LICENSE" "%PACKDIR%\" >nul

REM Lockstep companion version with pack version (Chrome-safe X.Y.Z only).
if exist "%PACKDIR%\extension\manifest.json" (
  cmake -DQP_EXT_DIR=%PACKDIR%\extension -DQP_EXT_VERSION=%VER% -P "%~dp0..\cmake\stamp_extension_version.cmake"
  if errorlevel 1 (
    echo Failed to stamp extension version to %VER%
    exit /b 1
  )
)

if not exist "%RELDIR%" mkdir "%RELDIR%"

vpk pack ^
  --packId QiuckPrompts ^
  --packVersion %VER% ^
  --packDir "%PACKDIR%" ^
  --mainExe qiuckprompts.exe ^
  --packTitle QiuckPrompts ^
  --packAuthors "Vladimir Sumarov" ^
  --outputDir "%RELDIR%" ^
  --icon "resources\app.ico" ^
  --yes
if errorlevel 1 exit /b 1

echo.
echo Done. Artifacts in %CD%\%RELDIR%\
dir /b "%RELDIR%"
exit /b 0
