@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" || exit /b 1
cd /d C:\work\repos\qiuckprompts
cmake --build build --config Debug -- /m || exit /b 1
echo BUILD_OK
