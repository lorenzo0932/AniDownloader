import sys
import subprocess
import os

# --- Spostati nella root del progetto ---
# Calcola il percorso della directory principale (un livello sopra la cartella 'utils')
project_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# Cambia la directory di lavoro corrente nella root del progetto
os.chdir(project_root)
# ------------------------------------

# --- Configurazione ---
APP_NAME = "AniDownloader_CLI"
ENTRY_POINT = "AniDownloader.py"  # Sostituisci con il nome del tuo script principale
# --------------------

def get_platform_specific_args():
    """
    Restituisce gli argomenti di PyInstaller specifici per il sistema operativo corrente.
    """
    # os.pathsep è il separatore corretto per PyInstaller (';' su Windows, ':' su Linux)
    separator = os.pathsep
    
    # Argomenti per l'inclusione dei dati (comuni a entrambi i sistemi)
    add_data_args = [
        f'--add-data=anidownloader_config{separator}anidownloader_config',
        f'--add-data=anidownloader_core{separator}anidownloader_core'
    ]

    # Rileva il sistema operativo
    if sys.platform == 'win32':
        print("Sistema operativo rilevato: Windows")
        # Su Windows non sono necessarie le librerie SSL/crypto manualmente
        return add_data_args
        
    elif sys.platform.startswith('linux'):
        print("Sistema operativo rilevato: Linux")
        # Su Linux aggiungiamo i binari per SSL
        add_binary_args = [
            f'--add-binary=/usr/lib64/libssl.so.3{separator}.',
            f'--add-binary=/usr/lib64/libcrypto.so.3{separator}.'
        ]
        return add_data_args + add_binary_args
        
    else:
        # Puoi aggiungere il supporto per altri sistemi (es. 'darwin' per macOS) o lanciare un errore
        print(f"ATTENZIONE: Il sistema operativo '{sys.platform}' non è supportato da questo script di build.")
        # Continuiamo solo con gli argomenti base, potrebbe funzionare o meno
        return add_data_args

def main():
    """
    Funzione principale che assembla ed esegue il comando PyInstaller.
    """
    # Argomenti di base di PyInstaller, comuni a tutte le piattaforme
    base_command = [
        'pyinstaller',
        '--onefile',
        '--console',
        '--clean',  # Pulisce la cache di PyInstaller, utile per evitare errori
        f'--name={APP_NAME}',
        '--paths=.',
        '--distpath=./dist',
        '--workpath=./build',
        '--specpath=.',
        '--hidden-import=requests',
        '--hidden-import=bs4'
    ]
    
    # Ottieni gli argomenti specifici della piattaforma
    platform_args = get_platform_specific_args()
    
    # Assembla il comando finale
    final_command = base_command + platform_args + [ENTRY_POINT]
    
    print("\nComando PyInstaller che verrà eseguito:")
    # Stampiamo il comando in un formato leggibile
    print(' '.join(f'"{arg}"' if ' ' in arg else arg for arg in final_command))
    
    try:
        # Esegui il comando
        print("\n--- Inizio del processo di build di PyInstaller ---")
        subprocess.run(final_command, check=True)
        print("--- Build completato con successo! ---")
        print(f"L'eseguibile si trova nella cartella 'dist'.")
        
    except FileNotFoundError:
        print("\nERRORE: Il comando 'pyinstaller' non è stato trovato.")
        print("Assicurati di aver installato PyInstaller (pip install pyinstaller) e che sia nel PATH di sistema.")
    except subprocess.CalledProcessError as e:
        print(f"\nERRORE: PyInstaller ha terminato con un errore: {e}")

if __name__ == '__main__':
    main()
