@echo off
setlocal
call "%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" || exit /b 1
cd /d "%~dp0.."
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 || exit /b 1
cmake --build build --config Debug -- /m || exit /b 1
echo.
echo Exe: %cd%\build\Debug\qiuckprompts.exe
echo Run self-test:
build\Debug\qiuckprompts.exe --self-test
exit /b %ERRORLEVEL%
