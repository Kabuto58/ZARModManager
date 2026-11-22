@echo off
rem The public release: build.cmd, then ZARModManager.exe published self-contained (the .NET runtime inside it, so users
rem need nothing installed) as one compressed file in release\. The first run downloads the .NET 9 runtime packs
rem (Microsoft.NETCore.App.Runtime.win-x64, Microsoft.WindowsDesktop.App.Runtime.win-x64) from nuget.org.
setlocal
cd /d "%~dp0"
call "%~dp0build.cmd" || exit /b 1
dotnet publish gui\ZARModManager.csproj -c Release -r win-x64 --self-contained true -p:PublishSingleFile=true -p:BundleMods=false -o release --nologo -v q || exit /b 1
del /q release\*.pdb 2>nul
echo Published release\ZARModManager.exe
rem the download: the app, the mods as files, and the license notices, zipped (package.ps1)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0package.ps1" || exit /b 1
