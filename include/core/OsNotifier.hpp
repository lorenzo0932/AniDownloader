#pragma once

#include <string>

// Notifiche di sistema dal demone (non dal client Tauri): su Linux usa
// notify-send (con --wait, così il sender D-Bus resta vivo anche su GNOME)
// e in fallback gdbus. Su macOS/Windows non è supportato: il client Tauri
// resta il proprietario delle notifiche OS.
namespace Core {

    class OsNotifier {
      public:
        struct Result {
            bool ok{false};
            std::string method;
            std::string error;
        };

        // Compilato solo su Linux. `available()` verifica che almeno uno dei
        // due comandi sia nel PATH (non esegue nulla).
        static bool supported() noexcept;
        static bool available();

        // Bloccante al massimo ~3s. Le stringhe sono passate come argv
        // separati: nessuna shell, nessuna interpolazione.
        static Result send(const std::string& title, const std::string& body);
    };

} // namespace Core
