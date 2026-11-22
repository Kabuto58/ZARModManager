# Run by build.cmd: every name a mod reads or writes outside its own locals must be a standard library or za. A
# misspelt local, or one used above the line that declares it, is otherwise a global that only fails in game.
$ok = 'za', 'ipairs', 'pairs', 'next', 'select', 'type', 'tostring', 'tonumber', 'error', 'pcall', 'assert', 'print',
      'setmetatable', 'getmetatable', 'rawget', 'rawset', 'rawequal', 'rawlen', 'math', 'string', 'table', 'utf8'
$failed = $false
foreach ($mod in Get-ChildItem "$PSScriptRoot\mods\*.lua") {
    $listing = & "$PSScriptRoot\build\luac.exe" -l -l -p $mod.FullName
    $bad = $listing | Select-String 'TABUP.*_ENV "(\w+)"' | ForEach-Object { $_.Matches[0].Groups[1].Value } |
           Where-Object { $ok -notcontains $_ } | Sort-Object -Unique
    if ($bad) { Write-Host "$($mod.Name): unknown globals: $($bad -join ', ')"; $failed = $true }
}
if ($failed) { exit 1 }
