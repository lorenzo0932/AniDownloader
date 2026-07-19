<#
.SYNOPSIS
    Disinstalla AniDownloader da Windows.
.DESCRIPTION
    Rimuove l'eseguibile, le DLL, gli shortcut e il task scheduler.
    Chiede se mantenere i file di configurazione (series_data.json, config.json).
#>

$ErrorActionPreference = "Stop"

# --- CONFIGURAZIONE ---
$AppName     = "AniDownloader"
$BinName     = "AniDownloader.exe"
$InstallDir  = "$env:LOCALAPPDATA\$AppName"
$ShortcutDir = "$env:APPDATA\Microsoft\Windows\Start Menu\Programs\$AppName"
$ConfigDir   = "$env:APPDATA\$AppName"
$TaskName    = "AniDownloader_AutoCheck"

Write-Host "--- Disinstallazione $AppName ---" -ForegroundColor Cyan

# 0. Domanda: mantenere la configurazione?
$keepConfig = Read-Host "Mantenere la configurazione? (serie, config) [S/n]"
$keepConfig = $keepConfig.Trim().ToLower()
if ($keepConfig -eq "") { $keepConfig = "s" }

# 1. Ferma il processo se in esecuzione
Write-Host "Fermo processi in esecuzione..." -ForegroundColor Yellow
$running = Get-Process -Name "AniDownloader" -ErrorAction SilentlyContinue
if ($running) {
    Stop-Process -Name "AniDownloader" -Force
    Start-Sleep -Seconds 2
}

# 2. Rimuovi task scheduler
Write-Host "Rimuovo task scheduler..." -ForegroundColor Yellow
if (Get-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue) {
    Stop-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue
    Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false
}

# 3. Rimuovi cartella installazione (exe, DLL, icona, log, uninstaller)
Write-Host "Rimuovo eseguibile e dipendenze..." -ForegroundColor Yellow
if (Test-Path $InstallDir) {
    Remove-Item -Path $InstallDir -Recurse -Force
}

# 4. Rimuovi shortcut Menu Start
Write-Host "Rimuovo shortcut..." -ForegroundColor Yellow
if (Test-Path $ShortcutDir) {
    Remove-Item -Path $ShortcutDir -Recurse -Force
}

# 5. Configurazione
if ($keepConfig -eq "s") {
    Write-Host ""
    Write-Host "Configurazione mantenuta in $ConfigDir" -ForegroundColor Green
} else {
    Write-Host "Rimuovo configurazione..." -ForegroundColor Yellow
    if (Test-Path $ConfigDir) {
        Remove-Item -Path $ConfigDir -Recurse -Force
    }
}

# 6. Riepilogo
Write-Host ""
Write-Host "--- Disinstallazione completata! ---" -ForegroundColor Green
if ($keepConfig -eq "s") {
    Write-Host "La configurazione e' stata mantenuta."
    Write-Host "  $ConfigDir"
}
