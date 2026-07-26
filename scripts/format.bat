@echo off
setlocal EnableExtensions EnableDelayedExpansion

rem format.bat [--check]
rem Requires clang-format on PATH (LLVM) or common install locations.

set CHECK=0
if /I "%~1"=="--check" set CHECK=1
if /I "%~1"=="-n" set CHECK=1
if /I "%~1"=="--dry-run" set CHECK=1

cd /d "%~dp0.."
if errorlevel 1 exit /b 1

set "CF="
where clang-format >nul 2>nul && set "CF=clang-format"
if not defined CF if exist "C:\Program Files\LLVM\bin\clang-format.exe" set "CF=C:\Program Files\LLVM\bin\clang-format.exe"
if not defined CF if exist "C:\Program Files (x86)\LLVM\bin\clang-format.exe" set "CF=C:\Program Files (x86)\LLVM\bin\clang-format.exe"
if not defined CF (
  echo clang-format not found. Install LLVM or add clang-format to PATH.
  echo   winget install LLVM.LLVM
  exit /b 2
)

set COUNT=0
set FAIL=0

for %%R in (src include) do (
  if exist "%%R" (
    for /R "%%R" %%F in (*.cpp *.hpp) do (
      set /a COUNT+=1
      if !CHECK! EQU 1 (
        "%CF%" --dry-run --Werror --style=file "%%F"
        if errorlevel 1 (
          echo NEED FORMAT: %%F
          set FAIL=1
        )
      ) else (
        "%CF%" -i --style=file "%%F"
        if errorlevel 1 set FAIL=1
      )
    )
  )
)

if !COUNT! EQU 0 (
  echo No source files found.
  exit /b 1
)

if !CHECK! EQU 1 (
  if !FAIL! EQU 1 (
    echo Format check FAILED ^(!COUNT! files scanned^). Run scripts\format.bat
    exit /b 1
  )
  echo Format check OK ^(!COUNT! files^).
  exit /b 0
)

if !FAIL! EQU 1 (
  echo Format rewrite had errors.
  exit /b 1
)
echo Formatted !COUNT! files.
exit /b 0
