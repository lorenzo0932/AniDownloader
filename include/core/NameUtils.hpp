#pragma once

#include <string>

// Normalizzazione dei nomi serie: i titoli arrivano da HTML/URL e possono
// contenere entità (&amp;, &#039;), percent-encoding (%20, %27) e caratteri
// non validi su Windows/NTFS. L'utente copia spesso il nome per creare la
// cartella di destinazione, quindi il nome mostrato deve essere già un nome
// di cartella valido.
namespace Core {
    namespace NameUtils {

        // Decodifica le entità HTML più comuni e quelle numeriche (decimali
        // ed esadecimali). Le entità sconosciute restano invariate.
        std::string decodeHtmlEntities(const std::string& value);

        // Decodifica %XX e %uXXXX solo se il risultato è UTF-8 valido:
        // una percentuale letterale ("100%") resta intatta.
        std::string decodePercentEncoding(const std::string& value);

        // Entità + percent + spazi/controlli normalizzati, senza sanitizzazione
        // filesystem (utile per descrizioni e testi generici).
        std::string normalizeName(const std::string& value);

        // Nome sicuro per una componente di path NTFS/Windows (leading space
        // incluso: Explorer/NTFS non lo preservano in modo affidabile):
        // - < > : " / \ | ? * e caratteri di controllo sostituiti;
        // - spazi ripetuti compressi, trailing dot/spazi rimossi;
        // - nomi riservati (CON, PRN, AUX, NUL, COM1-9, LPT1-9) prefissati;
        // - massimo 240 code point UTF-8.
        // Non tocca i path: solo il nome della serie.
        std::string toNtfsSafeName(const std::string& value);

        // Diagnostica/test: true se il nome è già una componente NTFS valida.
        bool isNtfsSafe(const std::string& value);

    } // namespace NameUtils
} // namespace Core
