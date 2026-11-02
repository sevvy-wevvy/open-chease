# irm https://raw.githubusercontent.com/sevvy-wevvy/open-chease/beta/get.ps1 | iex
# no param() and no exit in here, iex chokes on the first and exit closes your whole window
& {
    $ErrorActionPreference = 'Stop'
    $ProgressPreference = 'SilentlyContinue' # the progress bar makes downloads like 10x slower on powershell 5
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

    $repo = 'sevvy-wevvy/open-chease'
    $data = if ($env:OPEN_CHEASE_DATA) { $env:OPEN_CHEASE_DATA } else { Join-Path $env:LOCALAPPDATA 'open-chease' }
    $app = Join-Path $data 'app'
    $new = "$app.new"
    $exe = Join-Path $app 'bin\open-chease.exe'
    $sf = Join-Path $data 'stockfish\stockfish.exe'

    $update = $false; $fetchOnly = $false; $wantSf = $true; $stageOnly = $false; $launch = $false; $desk = $false; $pass = @()
    foreach ($a in $args) {
        switch ($a) {
            { $_ -in '-Update', '--update' } { $update = $true } # readme uses the linux spelling
            '-FetchOnly' { $fetchOnly = $true }
            { $_ -in '-NoStockfish', '--no-stockfish' } { $wantSf = $false }
            '-Stage' { $stageOnly = $true } # app runs this, grabs the next version in the background
            '-Launch' { $launch = $true } # what the start menu shortcut runs
            { $_ -in '-DesktopShortcut', '--desktop-shortcut' } { $desk = $true }
            default { $pass += $a }
        }
    }

    $branch = if ($env:OPEN_CHEASE_BRANCH) { $env:OPEN_CHEASE_BRANCH } else { 'beta' }
    $cf = Join-Path $app '.commit'
    if (-not $env:OPEN_CHEASE_BRANCH -and ($launch -or $stageOnly) -and (Test-Path -LiteralPath $cf)) {
        $b = @(Get-Content -LiteralPath $cf)
        if ($b.Count -ge 2 -and $b[1] -match '^[A-Za-z0-9._/-]+$') { $branch = $b[1] } # stay on whatever branch it got installed from
    }

    New-Item -ItemType Directory -Force $data | Out-Null
    $ok = $true

    function Say($s) { try { [IO.File]::WriteAllText((Join-Path $data 'update-status'), "$s`n") } catch {} }

    function Latest {
        $u = if ($env:OPEN_CHEASE_LATEST_URL) { $env:OPEN_CHEASE_LATEST_URL } else { "https://api.github.com/repos/$repo/commits/$branch" }
        try {
            $c = Invoke-RestMethod -UseBasicParsing -TimeoutSec 10 $u
            if ($c.sha -match '^[0-9a-f]{40}$') { return $c.sha }
        } catch {}
        return ''
    }

    function Stage {
        Say 'downloading the new version...'
        $tmp = Join-Path $data ('dl.' + [guid]::NewGuid().ToString('N').Substring(0, 8))
        New-Item -ItemType Directory -Force $tmp | Out-Null
        try {
            $sha = Latest
            $ref = if ($sha) { $sha } else { "refs/heads/$branch" } # by sha so .commit matches what got downloaded
            $base = if ($env:OPEN_CHEASE_ARCHIVE) { $env:OPEN_CHEASE_ARCHIVE } else { "https://github.com/$repo/archive" }
            Invoke-WebRequest -UseBasicParsing "$base/$ref.zip" -OutFile "$tmp\app.zip"
            Say 'unpacking it...'
            Expand-Archive -LiteralPath "$tmp\app.zip" "$tmp\x" -Force
            $inner = Get-ChildItem -LiteralPath "$tmp\x" -Directory | Select-Object -First 1
            $e = Join-Path $inner.FullName 'bin\open-chease.exe'
            $mz = if (Test-Path -LiteralPath $e) { [IO.File]::ReadAllBytes($e) } else { @() }
            if ($mz.Count -lt 100000 -or $mz[0] -ne 77 -or $mz[1] -ne 90 -or -not (Test-Path -LiteralPath (Join-Path $inner.FullName 'get.ps1'))) {
                throw 'the download looks broken'
            }
            if ($sha) { [IO.File]::WriteAllText((Join-Path $inner.FullName '.commit'), "$sha`n$branch`n") }
            if (Test-Path -LiteralPath $new) { Remove-Item -LiteralPath $new -Recurse -Force }
            Move-Item -LiteralPath $inner.FullName $new
            [IO.File]::WriteAllText((Join-Path $new '.ready'), '')
            return $true
        } catch {
            Write-Host "couldnt download open-chease from github, check your internet and try again ($_)"
            return $false
        } finally {
            Remove-Item -LiteralPath $tmp -Recurse -Force -ErrorAction SilentlyContinue
        }
    }

    function Swap { # a ready app.new takes over app, waits for the old exe to close first
        if (-not (Test-Path -LiteralPath (Join-Path $new '.ready'))) { return $false }
        $old = "$app.old"
        if (Test-Path -LiteralPath $old) { Remove-Item -LiteralPath $old -Recurse -Force -ErrorAction SilentlyContinue }
        if (Test-Path -LiteralPath $app) {
            $moved = $false
            for ($i = 0; $i -lt 15 -and -not $moved; $i++) {
                try { Move-Item -LiteralPath $app $old; $moved = $true } catch { Start-Sleep -Seconds 1 }
            }
            if (-not $moved) { Write-Host 'open-chease is still running, close it and try again'; return $false }
        }
        try { Move-Item -LiteralPath $new $app } catch { Move-Item -LiteralPath $old $app; return $false }
        Remove-Item -LiteralPath (Join-Path $app '.ready') -Force -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath $old -Recurse -Force -ErrorAction SilentlyContinue
        return $true
    }

    function Shortcuts { # start menu entry that starts it without a console window
        $me = Join-Path $app 'get.ps1'
        if (Test-Path -LiteralPath $me) { Copy-Item -LiteralPath $me (Join-Path $data 'get.ps1') -Force }
        $ico = Join-Path $app 'assets\open-chease.ico'
        if (Test-Path -LiteralPath $ico) { Copy-Item -LiteralPath $ico (Join-Path $data 'open-chease.ico') -Force } # not in app, that folder gets swapped
        $mark = Join-Path $data '.launcher'
        $menu = Join-Path ([Environment]::GetFolderPath('Programs')) 'open-chease.lnk'
        $spots = @()
        if (-not (Test-Path -LiteralPath $mark) -or (Test-Path -LiteralPath $menu)) { $spots += $menu } # they deleted it, dont bring it back
        if ($desk) { $spots += Join-Path ([Environment]::GetFolderPath('Desktop')) 'open-chease.lnk' }
        $ch = Join-Path $env:SystemRoot 'System32\conhost.exe'
        $ps = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
        $run = "-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File `"$data\get.ps1`" -Launch"
        $sh = New-Object -ComObject WScript.Shell
        foreach ($p in $spots) {
            $l = $sh.CreateShortcut($p)
            if (Test-Path -LiteralPath $ch) { $l.TargetPath = $ch; $l.Arguments = "--headless `"$ps`" $run" } # headless conhost never shows a window, even with windows terminal
            else { $l.TargetPath = $ps; $l.Arguments = $run }
            $l.WorkingDirectory = $data
            $l.WindowStyle = 7
            $l.Description = 'Shows the best move on any chess board on your screen'
            if (Test-Path -LiteralPath (Join-Path $data 'open-chease.ico')) { $l.IconLocation = (Join-Path $data 'open-chease.ico') + ',0' }
            $l.Save()
        }
        [IO.File]::WriteAllText($mark, '')
    }

    if ($stageOnly) {
        Stage | Out-Null
        return
    }

    if ($update) { Start-Sleep -Seconds 2 } # exe is locked till the old one closes
    $swapped = Swap # one the app grabbed in the background

    if ($launch -and (Test-Path -LiteralPath $exe) -and (Test-Path -LiteralPath $cf)) {
        $env:OPEN_CHEASE_LAUNCHED = '1'
        $have = @(Get-Content -LiteralPath $cf)[0]
        $sha = Latest
        if ($sha -and $sha -ne $have) {
            Say 'downloading the new version...'
            $up = Join-Path $data 'updater'
            $screen = $null
            try { # runs from a copy so the real one can get swapped out
                New-Item -ItemType Directory -Force $up | Out-Null
                Copy-Item -LiteralPath $exe (Join-Path $up 'open-chease.exe') -Force
                $env:OPEN_CHEASE_DATA = $data
                $screen = Start-Process (Join-Path $up 'open-chease.exe') -ArgumentList '--updating' -NoNewWindow -PassThru
            } catch {}
            if ((Stage) -and (Swap)) { Say 'done' } else { Say 'update failed, starting the one you have'; Start-Sleep -Seconds 2 }
            if ($screen) {
                for ($i = 0; $i -lt 10 -and -not $screen.HasExited; $i++) { Start-Sleep -Milliseconds 300 }
                if (-not $screen.HasExited) { $screen | Stop-Process -Force -ErrorAction SilentlyContinue }
            }
        }
    }

    if (-not (Test-Path -LiteralPath $exe) -or ($update -and -not $swapped)) {
        Write-Host 'grabbing open-chease...'
        $ok = (Stage) -and (Swap)
    }
    Remove-Item -LiteralPath (Join-Path $data 'update-status') -Force -ErrorAction SilentlyContinue
    if (Test-Path -LiteralPath $exe) { try { Shortcuts } catch { Write-Host "couldnt make the start menu shortcut ($_)" } }

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
        $env:OPEN_CHEASE_BRANCH = $branch
        if (Test-Path -LiteralPath $sf) { $env:OPEN_CHEASE_ENGINE = $sf }
        & $exe @pass
    }
} @args
