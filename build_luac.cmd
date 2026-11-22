@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul
cd /d "%~dp0"
cl /nologo /O2 /MT /TC /c /Fobuild\luac_main.obj third_party\lua-5.4.7\src\luac.c || exit /b 1
link /nologo /OUT:build\luac.exe build\luac_main.obj build\lua\*.obj || exit /b 1
echo built luac
