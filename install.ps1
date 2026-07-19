<#
.SYNOPSIS
    Installa AniDownloader come applicazione userspace su Windows.
.DESCRIPTION
    Compila il progetto, copia l'eseguibile e le dipendenze Qt,
    crea gli shortcut nel Menu Start e registra un task scheduler
    equivalente al timer systemd di Linux.
.PARAMETER NoBuild
    Salta la compilazione e usa l'eseguibile gia' presente in build/.
.PARAMETER NoTask
    Non registrare il task scheduler automatico.
#>
param(
    [switch]$NoBuild,
    [switch]$NoTask
)

$ErrorActionPreference = "Stop"

# --- CONFIGURAZIONE ---
$AppName    = "AniDownloader"
$BinName    = "AniDownloader.exe"
$InstallDir = "$env:LOCALAPPDATA\$AppName"
$ShortcutDir= "$env:APPDATA\Microsoft\Windows\Start Menu\Programs\$AppName"
$IconSource = "resources\logo.png"
$IconDest   = "$InstallDir\anidownloader_logo.png"
$TaskName   = "AniDownloader_AutoCheck"

Write-Host "--- Installazione $AppName ---" -ForegroundColor Cyan

# 0. Ferma processi in esecuzione
Write-Host "Fermo processi in esecuzione..." -ForegroundColor Yellow
$running = Get-Process -Name "AniDownloader" -ErrorAction SilentlyContinue
if ($running) {
    Stop-Process -Name "AniDownloader" -Force
    Start-Sleep -Seconds 2
}

# Ferma anche il task scheduler se esiste
if (Get-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue) {
    Stop-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue
}

# 1. Compilazione
if (-not $NoBuild) {
    Write-Host "Compilazione in corso..." -ForegroundColor Yellow

    # Rileva il generatore: Ninja se disponibile, altrimenti Visual Studio
    $genArgs = @()
    if (Get-Command ninja -ErrorAction SilentlyContinue) {
        $genArgs = @("-G", "Ninja")
        Write-Host "  Generatore: Ninja"
    } else {
        Write-Host "  Generatore: Visual Studio (default)"
    }

    $buildDir = "build"
    if (-not (Test-Path $buildDir)) {
        New-Item -ItemType Directory -Path $buildDir | Out-Null
    }

    cmake -B $buildDir @genArgs -DCMAKE_BUILD_TYPE=Release
    if ($LASTEXITCODE -ne 0) {
        Write-Host "Errore durante cmake configure." -ForegroundColor Red
        exit 1
    }

    cmake --build $buildDir --config Release
    if ($LASTEXITCODE -ne 0) {
        Write-Host "Errore durante la compilazione." -ForegroundColor Red
        exit 1
    }
    Write-Host "Compilazione completata." -ForegroundColor Green
} else {
    Write-Host "Compilazione saltata (-NoBuild)." -ForegroundColor Yellow
}

# 2. Trova l'eseguibile compilato
$exePath = $null
$candidates = @(
    "build\AniDownloader.exe",
    "build\Release\AniDownloader.exe",
    "build\Debug\AniDownloader.exe"
)
foreach ($c in $candidates) {
    if (Test-Path $c) { $exePath = $c; break }
}
if (-not $exePath) {
    Write-Host "Eseguibile non trovato. Esegui senza -NoBuild." -ForegroundColor Red
    exit 1
}

# 3. Crea directory di installazione
Write-Host "Creazione directory in $InstallDir..." -ForegroundColor Yellow
New-Item -ItemType Directory -Path $InstallDir -Force | Out-Null
New-Item -ItemType Directory -Path $ShortcutDir -Force | Out-Null

# 4. Copia eseguibile
Write-Host "Copia eseguibile..." -ForegroundColor Yellow
Copy-Item -Path $exePath -Destination $InstallDir -Force

# 5. Copia icona
if (Test-Path $IconSource) {
    Copy-Item -Path $IconSource -Destination $IconDest -Force
    Write-Host "Icona copiata." -ForegroundColor Green
} else {
    Write-Host "Icona non trovata ($IconSource). L'app avra' l'icona di default." -ForegroundColor DarkYellow
}

# 6. Copia dipendenze Qt con windeployqt
Write-Host "Ricerca windeployqt..." -ForegroundColor Yellow
$windeployqt = $null

# Cerca nella cartella Qt di Qt6 installation
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

# Fallback: prova a cercare nel PATH
if (-not $windeployqt) {
    $windeployqt = (Get-Command windeployqt.exe -ErrorAction SilentlyContinue).Source
}

if ($windeployqt) {
    Write-Host "  Trovato: $windeployqt"
    $targetExe = Join-Path $InstallDir $BinName
    & $windeployqt $targetExe --dir $InstallDir 2>$null
    Write-Host "DLL Qt copiate." -ForegroundColor Green
} else {
    Write-Host "windeployqt non trovato. Le DLL Qt dovranno essere copiate manualmente." -ForegroundColor DarkYellow
}

# 7. Crea shortcut nel Menu Start
Write-Host "Creazione shortcut..." -ForegroundColor Yellow

$WshShell = New-Object -ComObject WScript.Shell

# Shortcut GUI
$guiLink = $WshShell.CreateShortcut("$ShortcutDir\$AppName GUI.lnk")
$guiLink.TargetPath = $InstallDir
$guiLink.Arguments = "--gui"
$guiLink.WorkingDirectory = $env:USERPROFILE
$guiLink.Description = "AniDownloader - Interfaccia Grafica"
if (Test-Path $IconDest) { $guiLink.IconLocation = "$IconDest,0" }
$guiLink.Save()

# Shortcut CLI (background/burst)
$cliLink = $WshShell.CreateShortcut("$ShortcutDir\$AppName Background.lnk")
$cliLink.TargetPath = $InstallDir
$cliLink.Arguments = "--burst"
$cliLink.WorkingDirectory = $env:USERPROFILE
$cliLink.Description = "AniDownloader - Modalita' Background (Burst)"
if (Test-Path $IconDest) { $cliLink.IconLocation = "$IconDest,0" }
$cliLink.Save()

Write-Host "Shortcut creati in $ShortcutDir" -ForegroundColor Green

# 8. Registra task scheduler (equivalente systemd timer: ogni 15 min + al login)
if (-not $NoTask) {
    Write-Host "Registrazione task scheduler..." -ForegroundColor Yellow

    $targetExe = Join-Path $InstallDir $BinName

    # Azione: esegui il programma in background
    $action = New-ScheduledTaskAction -Execute $targetExe -WorkingDirectory $env:USERPROFILE

    # Trigger 1: ogni 15 minuti
    $triggerInterval = New-ScheduledTaskTrigger -Once -At (Get-Date) `
        -RepetitionInterval (New-TimeSpan -Minutes 15) `
        -RepetitionDuration (New-TimeSpan -Days 9999)

    # Trigger 2: al login dell'utente
    $triggerLogin = New-ScheduledTaskTrigger -AtLogOn

    # Impostazioni: non bloccare se il PC e' in bassa energia, riprova dopo 5 min
    $settings = New-ScheduledTaskSettingsSet
    $settings.DisallowStartIfOnBatteries = $false
    $settings.StopIfGoingOnBatteries = $false
    $settings.ExecutionTimeLimit = "PT30M"
    $settings.RestartCount = 3
    $settings.RestartInterval = [System.TimeSpan]::FromMinutes(5)

    # Rimuovi task esistente
    Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false -ErrorAction SilentlyContinue

    Register-ScheduledTask -TaskName $TaskName `
        -Action $action `
        -Trigger @($triggerInterval, $triggerLogin) `
        -Settings $settings `
        -Description "AniDownloader - Controllo automatico periodico (ogni 15 min)" `
        -RunLevel Limited

    Write-Host "Task scheduler registrato: $TaskName" -ForegroundColor Green
} else {
    Write-Host "Task scheduler saltato (-NoTask)." -ForegroundColor Yellow
}

# 9. Installa uninstaller
Write-Host "Installazione uninstaller..." -ForegroundColor Yellow
Copy-Item -Path "uninstall.ps1" -Destination $InstallDir -Force
Write-Host "Uninstaller disponibile in $InstallDir\uninstall.ps1" -ForegroundColor Green

# 10. Riepilogo
Write-Host ""
Write-Host "--- Installazione completata! ---" -ForegroundColor Green
Write-Host ""
Write-Host "  Eseguibile:    $InstallDir\$BinName"
Write-Host "  Shortcut:      $ShortcutDir\"
if (-not $NoTask) {
    Write-Host "  Task:          $TaskName (ogni 15 min + al login)"
}
Write-Host ""
Write-Host "Avvia l'app dal Menu Start oppure da riga di comando:"
Write-Host "  $InstallDir\$BinName --gui" -ForegroundColor Cyan
Write-Host "  $InstallDir\$BinName --burst" -ForegroundColor Cyan
Write-Host ""
Write-Host "Per disinstallare: $InstallDir\uninstall.ps1" -ForegroundColor DarkYellow
