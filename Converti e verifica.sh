#!/bin/bash
START_TIME=$(date +%s)
IFS=$'\n'

fail_list=""

for i in $NAUTILUS_SCRIPT_SELECTED_FILE_PATHS; do
    # Definizione dei percorsi per il file di output e il relativo log
    output="/home/lorenzo/Video/Convertiti/$(basename "$i")"
    log_file="${output}.log"
    
    # Conversione del file con ffmpeg
    nice -n 5 ffmpeg  -y -i "$i" -c:v libx265 -crf 23 -preset veryfast -threads 12 -x265-params hist-scenecut=1 -c:a copy "$output"
    
    # Verifica del file convertito; gli errori vengono reindirizzati nel file .log
    nice -n 5 ffmpeg -y -v error -i "$output" -f null - 2>"$log_file"
    
    # Se il file log è vuoto (0 byte), lo elimina, altrimenti aggiunge il nome del file alla lista degli errori
    if [ ! -s "$log_file" ]; then
        rm "$log_file"
        rm "$i"
    else
        fail_list="${fail_list}\n$(basename "$i")"
        rm "$output"
    fi
done

END_TIME=$(date +%s)
ELAPSED_TIME=$((END_TIME-START_TIME))

# Notifica l'utente al termine della conversione, segnalando anche eventuali errori
# if [ -n "$fail_list" ]; then
#     zenity --error --text "Conversione completata in $ELAPSED_TIME secondi.\nI seguenti file hanno errori:\n$fail_list"
# else
#     zenity --info --text "Conversione e verifica file completati in $ELAPSED_TIME secondi senza errori."
# fi
