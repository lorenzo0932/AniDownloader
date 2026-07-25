<#
.SYNOPSIS
    Installa AniDownloader su Windows.
.DESCRIPTION
    Menu interattivo: scegli cosa installare (CLI, timer, web).
#>
param()

$ErrorActionPreference = "Stop"

# ─── CONFIG ───
$AppName    = "AniDownloader"
$BinName    = "AniDownloader.exe"
$InstallDir = "$env:LOCALAPPDATA\$AppName"
$ShortcutDir= "$env:APPDATA\Microsoft\Windows\Start Menu\Programs\$AppName"
$ConfigDir  = "$env:APPDATA\$AppName"
$IconSource = "resources\logo.png"
$IconDest   = "$InstallDir\anidownloader_logo.png"
$TaskName   = "AniDownloader_AutoCheck"
$WebTaskName= "AniDownloader_WebServer"

# ─── 0. Verifica dipendenze ───
$missing = @()

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    $missing += "cmake (installa da https://cmake.org/download/)"
}

$hasNinja = [bool](Get-Command ninja -ErrorAction SilentlyContinue)
$hasMSVC = [bool](Get-Command msbuild -ErrorAction SilentlyContinue) -or
           [bool](Get-Command cl.exe -ErrorAction SilentlyContinue)

if (-not $hasNinja -and -not $hasMSVC) {
    $missing += "Né Ninja né MSVC trovati. Installa Visual Studio Build Tools o ninja."
}

if (-not (Get-Command node -ErrorAction SilentlyContinue)) {
    $missing += "Node.js non trovato (necessario per build frontend web). Installa da https://nodejs.org/"
}

if ($missing.Count -gt 0) {
    Write-Host "═══════════════════════════════════════════════" -ForegroundColor Red
    Write-Host " Dipendenze mancanti:" -ForegroundColor Red
    foreach ($m in $missing) { Write-Host "  - $m" -ForegroundColor Yellow }
    Write-Host "═══════════════════════════════════════════════" -ForegroundColor Red
    Write-Host "Risolvi le dipendenze e riprova."
    exit 1
}

# Runtime warnings (non bloccanti)
$runtimeWarn = @()
if (-not (Get-Command aria2c -ErrorAction SilentlyContinue)) {
    $runtimeWarn += "aria2c (download multi-thread) — opzionale, installa da https://aria2.github.io/"
}
if (-not (Get-Command ffmpeg -ErrorAction SilentlyContinue)) {
    $runtimeWarn += "ffmpeg (conversione video) — opzionale, installa da https://ffmpeg.org/"
}
if ($runtimeWarn.Count -gt 0) {
    Write-Host ""
    Write-Host "Attenzione — strumenti runtime mancanti:" -ForegroundColor DarkYellow
    foreach ($w in $runtimeWarn) { Write-Host "  - $w" -ForegroundColor DarkYellow }
}

# ─── 1. Menu ───
Clear-Host
Write-Host @"

╔══════════════════════════════════════════════════════════════╗
║           AniDownloader — Installer (Windows)               ║
╚══════════════════════════════════════════════════════════════╝

Il binario contiene tutte le modalità integrate:
CLI (senza flag), GUI (--gui) e Web UI (--web).
La scelta qui sotto determina solo quali servizi
automatici abilitare.

Scegli cosa installare:

  1) Binario base — nessun servizio
     Solo il binario. Avvia manualmente con --gui,
     --web o --burst. Nessun servizio in background.

  2) Binario + Timer automatico
     Aggiunge un'Attività Pianificata che controlla nuovi
     episodi ogni 15 minuti e al login.
     Consigliato per download automatici in background.

  3) Binario + Web UI
     Aggiunge il server web always-on (porta 8989).
     Gestisci tutto dal browser: download, progresso live (SSE).

  4) Tutto (Binario + Timer + Web UI)
     Timer automatico + server web. Massima flessibilità.

  0) Annulla

"@

$choice = Read-Host "Scelta [0-4] (default: 2)"
if ([string]::IsNullOrWhiteSpace($choice)) { $choice = "2" }

$WITH_TIMER = $false
$WITH_WEB = $false

switch ($choice) {
    "0" { Write-Host "Annullato."; exit 0 }
    "1" {  }
    "2" { $WITH_TIMER = $true }
    "3" { $WITH_WEB = $true }
    "4" { $WITH_TIMER = $true; $WITH_WEB = $true }
    default { Write-Host "Scelta non valida." -ForegroundColor Red; exit 1 }
}

Write-Host ""

# ─── 2. Ferma processi e task esistenti ───
Write-Host "Fermo processi in esecuzione..." -ForegroundColor Yellow
$running = Get-Process -Name $AppName -ErrorAction SilentlyContinue
if ($running) {
    Stop-Process -Name $AppName -Force
    Start-Sleep -Seconds 2
}

foreach ($tn in @($TaskName, $WebTaskName)) {
    if (Get-ScheduledTask -TaskName $tn -ErrorAction SilentlyContinue) {
        Stop-ScheduledTask -TaskName $tn -ErrorAction SilentlyContinue
        Unregister-ScheduledTask -TaskName $tn -Confirm:$false
    }
}

# ─── 3. Compilazione ───
Write-Host "Compilazione in corso..." -ForegroundColor Yellow
$genArgs = @()
if ($hasNinja) {
    $genArgs = @("-G", "Ninja")
}
$buildDir = "build"
cmake -B $buildDir @genArgs -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) { Write-Host "Errore cmake configure." -ForegroundColor Red; exit 1 }
cmake --build $buildDir --config Release
if ($LASTEXITCODE -ne 0) { Write-Host "Errore compilazione." -ForegroundColor Red; exit 1 }
Write-Host ""

# ─── 4. Trova eseguibile ───
$exePath = $null
foreach ($c in @("build\$BinName", "build\Release\$BinName", "build\Debug\$BinName")) {
    if (Test-Path $c) { $exePath = $c; break }
}
if (-not $exePath) { Write-Host "Eseguibile non trovato." -ForegroundColor Red; exit 1 }

# ─── 5. Crea directory ───
New-Item -ItemType Directory -Path $InstallDir -Force | Out-Null
New-Item -ItemType Directory -Path $ShortcutDir -Force | Out-Null
New-Item -ItemType Directory -Path $ConfigDir -Force | Out-Null

# ─── 6. Copia eseguibile ───
Write-Host "Installo binario in $InstallDir..."
Copy-Item -Path $exePath -Destination $InstallDir -Force

# ─── 7. Build frontend ───
if ($WITH_WEB) {
    Write-Host "Build frontend web..." -ForegroundColor Yellow
    if (Get-Command node -ErrorAction SilentlyContinue) {
        Push-Location web
        npm install --silent
        npm run build --silent
        if ($LASTEXITCODE -eq 0 -and (Test-Path "dist")) {
            New-Item -ItemType Directory -Path "$InstallDir\frontend" -Force | Out-Null
            Copy-Item -Path "dist\*" -Destination "$InstallDir\frontend" -Recurse -Force
            Write-Host "  Frontend build completato."
        } else {
            Write-Host "  Frontend build fallito (proseguo comunque)." -ForegroundColor DarkYellow
        }
        Pop-Location
    }
}

# ─── 8. Icona ───
if (Test-Path $IconSource) {
    Copy-Item -Path $IconSource -Destination $IconDest -Force
}

# ─── 9. windeployqt ───
Write-Host "Copio DLL Qt..." -ForegroundColor Yellow
$windeployqt = $null
$qtPaths = @(
    "$env:QT_ROOT_DIR\bin\windeployqt.exe",
    "$env:Qt6_DIR\..\..\..\bin\windeployqt.exe",
    "C:\Qt\6*\msvc*\bin\windeployqt.exe",
    "$env:LOCALAPPDATA\Qt\6*\msvc*\bin\windeployqt.exe"
)
foreach ($pattern in $qtPaths) {
    $found = Get-Item $pattern -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($found) { $windeployqt = $found.FullName; break }
}
if (-not $windeployqt) {
    $windeployqt = (Get-Command windeployqt.exe -ErrorAction SilentlyContinue).Source
}
if ($windeployqt) {
    & $windeployqt "$InstallDir\$BinName" --dir $InstallDir 2>$null
    Write-Host "  DLL Qt copiate."
} else {
    Write-Host "  windeployqt non trovato. Copia manuale necessaria." -ForegroundColor DarkYellow
}

# ─── 10. Shortcut Menu Start ───
Write-Host "Creazione shortcut..." -ForegroundColor Yellow
$WshShell = New-Object -ComObject WScript.Shell

$guiLink = $WshShell.CreateShortcut("$ShortcutDir\$AppName GUI.lnk")
$guiLink.TargetPath = "$InstallDir\$BinName"
$guiLink.Arguments = "--gui"
$guiLink.WorkingDirectory = $env:USERPROFILE
$guiLink.Description = "AniDownloader - Interfaccia Grafica"
if (Test-Path $IconDest) { $guiLink.IconLocation = "$IconDest,0" }
$guiLink.Save()

$cliLink = $WshShell.CreateShortcut("$ShortcutDir\$AppName CLI.lnk")
$cliLink.TargetPath = "$InstallDir\$BinName"
$cliLink.Arguments = "--burst"
$cliLink.WorkingDirectory = $env:USERPROFILE
$cliLink.Description = "AniDownloader - Modalità Background"
if (Test-Path $IconDest) { $cliLink.IconLocation = "$IconDest,0" }
$cliLink.Save()

if ($WITH_WEB) {
    $webLink = $WshShell.CreateShortcut("$ShortcutDir\$AppName Web.lnk")
    $webLink.TargetPath = "$InstallDir\$BinName"
    $webLink.Arguments = "--web"
    $webLink.WorkingDirectory = $env:USERPROFILE
    $webLink.Description = "AniDownloader - Server Web"
    if (Test-Path $IconDest) { $webLink.IconLocation = "$IconDest,0" }
    $webLink.Save()

    $browserLink = $WshShell.CreateShortcut("$ShortcutDir\$AppName Web UI.lnk")
    $browserLink.TargetPath = "http://localhost:8989"
    $browserLink.Description = "AniDownloader Web UI"
    $browserLink.Save()
}

# ─── 11. Task Scheduler ───
if ($WITH_TIMER) {
    Write-Host "Registrazione Attività Pianificata (timer)..." -ForegroundColor Yellow
    $targetExe = "$InstallDir\$BinName"
    $action = New-ScheduledTaskAction -Execute $targetExe -WorkingDirectory $env:USERPROFILE
    $triggerInterval = New-ScheduledTaskTrigger -Once -At (Get-Date) `
        -RepetitionInterval (New-TimeSpan -Minutes 15) `
        -RepetitionDuration (New-TimeSpan -Days 9999)
    $triggerLogin = New-ScheduledTaskTrigger -AtLogOn
    $settings = New-ScheduledTaskSettingsSet
    $settings.DisallowStartIfOnBatteries = $false
    $settings.StopIfGoingOnBatteries = $false
    $settings.ExecutionTimeLimit = "PT30M"
    $settings.RestartCount = 3
    $settings.RestartInterval = [System.TimeSpan]::FromMinutes(5)
    Register-ScheduledTask -TaskName $TaskName `
        -Action $action `
        -Trigger @($triggerInterval, $triggerLogin) `
        -Settings $settings `
        -Description "AniDownloader - Controllo automatico ogni 15 min" `
        -RunLevel Limited | Out-Null
    Write-Host "  Timer attivo: ogni 15 min + al login"
}

if ($WITH_WEB) {
    Write-Host "Registrazione Attività Pianificata (web)..." -ForegroundColor Yellow
    $targetExe = "$InstallDir\$BinName"
    $action = New-ScheduledTaskAction -Execute $targetExe -Argument "--web" -WorkingDirectory $env:USERPROFILE
    $triggerLogin = New-ScheduledTaskTrigger -AtLogOn
    $settings = New-ScheduledTaskSettingsSet
    $settings.DisallowStartIfOnBatteries = $false
    $settings.StopIfGoingOnBatteries = $false
    $settings.ExecutionTimeLimit = "PT0S"  # no time limit (always-on)
    $settings.RestartCount = 3
    $settings.RestartInterval = [System.TimeSpan]::FromMinutes(1)
    Register-ScheduledTask -TaskName $WebTaskName `
        -Action $action `
        -Trigger $triggerLogin `
        -Settings $settings `
        -Description "AniDownloader - Server Web Always-On" `
        -RunLevel Limited | Out-Null
    Write-Host "  Web task attivo: http://localhost:8989"
}

# ─── 12. Uninstaller ───
Copy-Item -Path "uninstall.ps1" -Destination $InstallDir -Force

# ─── 13. Riepilogo ───
Write-Host ""
Write-Host "╔═══════════════════════════════════════════════╗" -ForegroundColor Green
Write-Host "║  Installazione completata!                    ║" -ForegroundColor Green
Write-Host "╚═══════════════════════════════════════════════╝" -ForegroundColor Green
Write-Host ""
Write-Host "  Binario:       $InstallDir\$BinName"
Write-Host "  Config:        $ConfigDir"
Write-Host ""
if ($WITH_TIMER) {
    Write-Host "  Timer:         attivo (ogni 15 min + login)" -ForegroundColor Cyan
}
if ($WITH_WEB) {
    Write-Host "  Web UI:        http://localhost:8989" -ForegroundColor Cyan
}
Write-Host ""
Write-Host "  Disinstallare: $InstallDir\uninstall.ps1" -ForegroundColor DarkYellow
Write-Host ""
