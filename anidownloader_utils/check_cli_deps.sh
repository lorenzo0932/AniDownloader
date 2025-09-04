#! /usr/bin/python3

import shutil
import sys

def check_system_dependencies():
    """
    Verifica la presenza delle dipendenze di sistema (programmi a riga di comando)
    e stampa un resoconto.
    """
    print("--- Controllo Dipendenze di Sistema ---")
    
    # Lista delle dipendenze da controllare
    dependencies_to_check = ["aria2c", "ffmpeg"]
    
    missing_dependencies = []

    # Ciclo su ogni dipendenza e controllo se esiste nel PATH
    for dep in dependencies_to_check:
        if not shutil.which(dep):
            missing_dependencies.append(dep)

    # Stampa il resoconto finale
    if not missing_dependencies:
        print("✅ Successo! Tutte le dipendenze di sistema sono state trovate.")
        sys.exit(0) # Esce con codice 0 (successo)
    else:
        missing_list = ', '.join(missing_dependencies)
        print(f"❌ ERRORE: Le seguenti dipendenze di sistema non sono state trovate nel PATH: {missing_list}.")
        print("\nQuesti sono strumenti a riga di comando e non possono essere installati tramite pip.")
        print("Si prega di installarli manualmente utilizzando il gestore di pacchetti del sistema.")
        
        print("\nEsempi di comandi per l'installazione:")
        print("  - Debian/Ubuntu: sudo apt update && sudo apt install aria2 ffmpeg")
        print("  - Fedora:        sudo dnf install aria2 ffmpeg")
        print("  - macOS:         brew install aria2 ffmpeg")
        print("  - Windows:       winget install aria2 ffmpeg")
        
        sys.exit(1) # Esce con codice 1 (errore)

if __name__ == '__main__':
    check_system_dependencies()