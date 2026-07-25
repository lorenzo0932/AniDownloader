<#
.SYNOPSIS
    Disinstalla AniDownloader da Windows.
.DESCRIPTION
    Menu interattivo: scegli cosa rimuovere (solo binario, solo servizi, tutto).
#>

$ErrorActionPreference = "Stop"

# ─── CONFIG ───
$AppName     = "AniDownloader"
$BinName     = "AniDownloader.exe"
$InstallDir  = "$env:LOCALAPPDATA\$AppName"
$ShortcutDir = "$env:APPDATA\Microsoft\Windows\Start Menu\Programs\$AppName"
$ConfigDir   = "$env:APPDATA\$AppName"
$TaskName    = "AniDownloader_AutoCheck"
$WebTaskName = "AniDownloader_WebServer"

# ─── 1. Menu ───
Clear-Host
Write-Host @"

╔══════════════════════════════════════════════════════════════╗
║           AniDownloader — Uninstaller (Windows)              ║
╚══════════════════════════════════════════════════════════════╝

Scegli cosa rimuovere:

  1) Solo il binario
     Rimuove eseguibile, DLL, shortcut Menu Start e icona.
     Mantiene: Attività Pianificate, configurazione (config.json,
     series_data.json).
     Utile per reinstallare senza perdere dati.

  2) Solo i servizi
     Rimuove le Attività Pianificate (timer e web).
     Mantiene: binario e configurazione.
     Utile se non vuoi più l'esecuzione automatica.

  3) Tutto
     Rimuove binario, DLL, shortcut, Attività Pianificate,
     configurazione (config.json, series_data.json).
     ⚠️  I dati sono IRRECUPERABILI senza backup.

  0) Annulla

"@

$choice = Read-Host "Scelta [0-3]"
Write-Host ""

switch ($choice) {
    "0" { Write-Host "Annullato."; exit 0 }
    "1" { $MODE = "binary" }
    "2" { $MODE = "services" }
    "3" { $MODE = "all" }
    default { Write-Host "Scelta non valida." -ForegroundColor Red; exit 1 }
}

$FERMA_SERVIZI = ($MODE -eq "services") -or ($MODE -eq "all")
$RIMUOVI_BINARIO = ($MODE -eq "binary") -or ($MODE -eq "all")
$RIMUOVI_CONFIG = ($MODE -eq "all")

# ─── 2. Ferma processi ───
Write-Host "Fermo processi in esecuzione..." -ForegroundColor Yellow
$running = Get-Process -Name $AppName -ErrorAction SilentlyContinue
if ($running) {
    Stop-Process -Name $AppName -Force
    Start-Sleep -Seconds 2
}

# ─── 3. Rimuovi Attività Pianificate ───
if ($FERMA_SERVIZI) {
    Write-Host "Rimuovo Attività Pianificate..." -ForegroundColor Yellow
    foreach ($tn in @($TaskName, $WebTaskName)) {
        if (Get-ScheduledTask -TaskName $tn -ErrorAction SilentlyContinue) {
            Stop-ScheduledTask -TaskName $tn -ErrorAction SilentlyContinue
            Unregister-ScheduledTask -TaskName $tn -Confirm:$false
            Write-Host "  Rimossa: $tn"
        }
    }
}

# ─── 4. Rimuovi binario ───
if ($RIMUOVI_BINARIO) {
    Write-Host "Rimuovo eseguibile e dipendenze..." -ForegroundColor Yellow
    if (Test-Path $InstallDir) {
        Remove-Item -Path $InstallDir -Recurse -Force
        Write-Host "  Rimossa: $InstallDir"
    }

    Write-Host "Rimuovo shortcut Menu Start..." -ForegroundColor Yellow
    if (Test-Path $ShortcutDir) {
        Remove-Item -Path $ShortcutDir -Recurse -Force
        Write-Host "  Rimossa: $ShortcutDir"
    }
}

# ─── 5. Rimuovi configurazione ───
if ($RIMUOVI_CONFIG) {
    Write-Host "Rimuovo configurazione..." -ForegroundColor Yellow
    if (Test-Path $ConfigDir) {
        Remove-Item -Path $ConfigDir -Recurse -Force
        Write-Host "  Rimossa: $ConfigDir"
    }
}

# ─── 7. Riepilogo ───
Write-Host ""
Write-Host "╔═══════════════════════════════════════════════╗" -ForegroundColor Green
Write-Host "║  Disinstallazione completata!                 ║" -ForegroundColor Green
Write-Host "╚═══════════════════════════════════════════════╝" -ForegroundColor Green
Write-Host ""
if ($FERMA_SERVIZI) { Write-Host "  Attività Pianificate: rimosse" }
if ($RIMUOVI_BINARIO) { Write-Host "  Binario:              rimosso" }
if (-not $RIMUOVI_CONFIG) { Write-Host "  Configurazione:       mantenuta ($ConfigDir)" }
Write-Host ""
