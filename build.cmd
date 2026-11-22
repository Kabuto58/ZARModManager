@echo off
rem Builds ZARModManager with the Visual Studio 2022 Build Tools and the .NET 9 SDK:
rem   build\version.dll            the runtime (Lua 5.4.7, the loader, version.dll forwarders, system.lua resource)
rem   build\dist\ZARModManager.exe the app (WPF): the runtime and the sample mods (mods\*.lua) are embedded in it
rem This build needs the .NET 9 Desktop Runtime on the machine that runs it; publish.cmd makes the standalone release.
rem One version for the app and the runtime: the VERSION file (written to build\version.h for the runtime; the app
rem project reads the file itself).
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul || exit /b 1
cd /d "%~dp0"
set LUA=third_party\lua-5.4.7\src
set /p VER=<VERSION
if not exist build mkdir build
> build\version.h echo #define ZAR_VERSION "%VER%"
>> build\version.h echo #define ZAR_VERSION_RC %VER:.=,%,0
if not exist build\lua mkdir build\lua
for %%f in (%LUA%\*.c) do (
    if /i not "%%~nf"=="lua" if /i not "%%~nf"=="luac" (
        if not exist build\lua\%%~nf.obj cl /nologo /c /O2 /MT /W1 /TC /Fobuild\lua\%%~nf.obj %%f >nul || exit /b 1
    )
)
build\luac.exe -p src\system.lua || exit /b 1
ml64 /nologo /c /Fobuild\forward.obj src\forward.asm || exit /b 1
rc /nologo /i src /i build /fo build\loader.res src\loader.rc || exit /b 1
cl /nologo /c /std:c++17 /O2 /EHa /MT /W3 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /I%LUA% /Ibuild /Fobuild\loader.obj src\loader.cpp || exit /b 1
link /nologo /DLL /DEF:src\exports.def /OUT:build\version.dll build\loader.obj build\forward.obj build\loader.res build\lua\*.obj user32.lib kernel32.lib || exit /b 1
echo Built build\version.dll
for %%m in (mods\*.lua) do build\luac.exe -p %%m || exit /b 1
rem every name a mod uses outside its own locals must be a standard library or za (check_mods.ps1)
powershell -NoProfile -ExecutionPolicy Bypass -File check_mods.ps1 || exit /b 1
rem the app, with the runtime and the mods embedded (framework-dependent single file)
dotnet publish gui\ZARModManager.csproj -c Release -r win-x64 --self-contained false -p:PublishSingleFile=true -o build\dist --nologo -v q || exit /b 1
del /q build\dist\*.pdb 2>nul
echo Built build\dist\ZARModManager.exe %VER%
