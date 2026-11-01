# irm https://raw.githubusercontent.com/sevvy-wevvy/open-chease/beta/get.ps1 | iex
# no param() and no exit in here, iex chokes on the first and exit closes your whole window
& { # own scope or iex leaves all this set in your window
    $ErrorActionPreference = 'Stop'
    $ProgressPreference = 'SilentlyContinue' # the progress bar makes downloads like 10x slower on powershell 5
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

    $repo = 'sevvy-wevvy/open-chease'
    $branch = if ($env:OPEN_CHEASE_BRANCH) { $env:OPEN_CHEASE_BRANCH } else { 'beta' }
    $data = if ($env:OPEN_CHEASE_DATA) { $env:OPEN_CHEASE_DATA } else { Join-Path $env:LOCALAPPDATA 'open-chease' }
    $app = Join-Path $data 'app'
    $exe = Join-Path $app 'bin\open-chease.exe'
    $sf = Join-Path $data 'stockfish\stockfish.exe'

    $update = $false; $fetchOnly = $false; $wantSf = $true; $pass = @()
    foreach ($a in $args) {
        switch ($a) {
            '-Update' { $update = $true }
            '-FetchOnly' { $fetchOnly = $true }
            '-NoStockfish' { $wantSf = $false }
            default { $pass += $a }
        }
    }

    New-Item -ItemType Directory -Force $data | Out-Null
    $ok = $true

    if ($update -or -not (Test-Path -LiteralPath $exe)) {
        Write-Host 'grabbing open-chease...'
        if ($update) { Start-Sleep -Seconds 2 } # the old one is still closing and windows keeps its exe locked till then
        $tmp = Join-Path $data ('dl.' + [guid]::NewGuid().ToString('N').Substring(0, 8))
        New-Item -ItemType Directory -Force $tmp | Out-Null
        try {
            Invoke-WebRequest -UseBasicParsing "https://github.com/$repo/archive/refs/heads/$branch.zip" -OutFile "$tmp\app.zip"
            Expand-Archive -LiteralPath "$tmp\app.zip" "$tmp\x" -Force
            $inner = Get-ChildItem -LiteralPath "$tmp\x" -Directory | Select-Object -First 1
            $old = "$app.old"
            if (Test-Path -LiteralPath $old) { Remove-Item -LiteralPath $old -Recurse -Force -ErrorAction SilentlyContinue }
            if (Test-Path -LiteralPath $app) { # move it aside first, a half deleted folder would leave you with nothing that runs
                $moved = $false
                for ($i = 0; $i -lt 10 -and -not $moved; $i++) {
                    try { Move-Item -LiteralPath $app $old; $moved = $true } catch { Start-Sleep -Seconds 1 }
                }
                if (-not $moved) { throw 'open-chease is still running, close it and try again' }
            }
            try { Move-Item -LiteralPath $inner.FullName $app } catch { Move-Item -LiteralPath $old $app; throw }
            Remove-Item -LiteralPath $old -Recurse -Force -ErrorAction SilentlyContinue
        } catch {
            Write-Host "couldnt download open-chease from github, check your internet and try again ($_)"
            $ok = $false
        } finally {
            Remove-Item -LiteralPath $tmp -Recurse -Force -ErrorAction SilentlyContinue
        }
    }

    if ($ok -and $wantSf -and -not (Test-Path -LiteralPath $sf)) {
        Write-Host 'grabbing stockfish too (about 80 MB, only happens the first time)...'
        $tmp = Join-Path $data ('sf.' + [guid]::NewGuid().ToString('N').Substring(0, 8))
        New-Item -ItemType Directory -Force $tmp | Out-Null
        try {
            Invoke-WebRequest -UseBasicParsing 'https://github.com/official-stockfish/Stockfish/releases/latest/download/stockfish-windows-x86-64-universal.zip' -OutFile "$tmp\sf.zip"
            Expand-Archive -LiteralPath "$tmp\sf.zip" "$tmp\x" -Force
            $found = Get-ChildItem -LiteralPath "$tmp\x" -Recurse -Filter 'stockfish*.exe' | Sort-Object Length -Descending | Select-Object -First 1
            New-Item -ItemType Directory -Force (Split-Path $sf) | Out-Null
            Move-Item -LiteralPath $found.FullName $sf -Force
        } catch {
            Write-Host 'stockfish download failed, using my engine instead'
        } finally {
            Remove-Item -LiteralPath $tmp -Recurse -Force -ErrorAction SilentlyContinue
        }
    }

    if (-not $ok -and (Test-Path -LiteralPath $exe)) { Write-Host 'starting the one you already had' }
    if (-not $fetchOnly -and (Test-Path -LiteralPath $exe)) {
        $ErrorActionPreference = 'Continue' # powershell 5 can treat a native exes stderr as an error, and ours logs there
        $env:OPEN_CHEASE_DATA = $data
        $env:OPEN_CHEASE_HOME = $app
        $env:OPEN_CHEASE_UPDATE = 'get'
        $env:OPEN_CHEASE_BRANCH = $branch # the app reads its engine lists from the same branch
        if (Test-Path -LiteralPath $sf) { $env:OPEN_CHEASE_ENGINE = $sf } # just the default, whatever you pick in settings wins
        & $exe @pass
    }
} @args
