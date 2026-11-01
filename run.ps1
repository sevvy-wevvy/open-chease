# windows version of run.sh, use run.bat if powershell scripts are blocked on your pc
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$data = if ($env:OPEN_CHEASE_DATA) { $env:OPEN_CHEASE_DATA } else { Join-Path $env:LOCALAPPDATA 'open-chease' }
New-Item -ItemType Directory -Force $data | Out-Null

$update = $false; $pass = @()
foreach ($a in $args) { if ($a -eq '-Update') { $update = $true } else { $pass += $a } }
if ($update) {
    Start-Sleep -Seconds 2 # let the old one close so git can replace the exe
    git -C $here pull --ff-only
}

$env:OPEN_CHEASE_DATA = $data
$env:OPEN_CHEASE_HOME = $here
$env:OPEN_CHEASE_UPDATE = if (Test-Path -LiteralPath (Join-Path $here '.git')) { 'git' }
    elseif ((Resolve-Path -LiteralPath $here).Path -eq (Resolve-Path -LiteralPath (Join-Path $data 'app') -ErrorAction SilentlyContinue).Path) { 'get' }
    else { $null }
if (-not $env:OPEN_CHEASE_ENGINE -and -not ($pass -contains '--engine') -and -not ($pass -contains '--builtin')) {
    $sf = Join-Path $data 'stockfish\stockfish.exe'
    if (-not (Test-Path $sf)) { $c = Get-Command stockfish -ErrorAction SilentlyContinue; if ($c) { $sf = $c.Source } }
    if (Test-Path $sf) { $env:OPEN_CHEASE_ENGINE = $sf }
}
& (Join-Path $here 'bin\open-chease.exe') @pass
