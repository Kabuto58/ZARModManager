# Run by publish.cmd: the public downloads, in release\, each with its SHA-256 for its page:
#   ZARModManager-<version>.zip   the app (the loader runtime inside it), LICENSES.txt, licenses\ (third-party notices)
#   <Mod>-<mod version>.zip       one per mod in mods\: the mod's .lua, the version read from its za.mod{...}
# Run with Windows PowerShell (powershell.exe, as publish.cmd does): PowerShell 7 writes different zip bytes, so a zip built
# with it has another SHA-256. To package one mod alone (leaving every other zip untouched): powershell -File package.ps1 -Only NameHider
param([string[]]$Only)
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot

function Write-Sha256($path) {
    # (with .NET directly: Get-FileHash is not always loaded when PowerShell is started from cmd)
    $stream = [System.IO.File]::OpenRead((Resolve-Path $path))
    try { $hash = [System.BitConverter]::ToString([System.Security.Cryptography.SHA256]::Create().ComputeHash($stream)).Replace('-', '') }
    finally { $stream.Dispose() }
    Write-Host ("{0,-36} SHA-256 {1}" -f (Split-Path $path -Leaf), $hash)
}

$version = (Get-Content VERSION).Trim()
$app = "release\ZARModManager-$version.zip"
if (-not $Only) {
    Compress-Archive -Path release\ZARModManager.exe, LICENSES.txt, licenses -DestinationPath $app -Force
    Write-Sha256 $app
}

foreach ($mod in Get-ChildItem mods\*.lua) {
    if ($Only -and ($Only -notcontains $mod.BaseName)) { continue }
    $m = Select-String -Path $mod.FullName -Pattern "version\s*=\s*'([^']+)'" | Select-Object -First 1
    if (-not $m) { throw "$($mod.Name): no version in its za.mod{...}" }
    $zip = "release\$($mod.BaseName)-$($m.Matches[0].Groups[1].Value).zip"
    Compress-Archive -Path $mod.FullName -DestinationPath $zip -Force
    Write-Sha256 $zip
}
