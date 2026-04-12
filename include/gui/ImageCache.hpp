#pragma once

#include <QPixmap>
#include <QCache>
#include <QString>
#include <QImage>

namespace Gui {

/**
 * @brief Gestore della Cache delle immagini per AniDownloader.
 * Sfrutta la potenza della CPU per caricamenti sincroni istantanei,
 * mantenendo le immagini scalate in RAM per un accesso immediato.
 */
class ImageCache {
public:
    static ImageCache& instance() {
        static ImageCache inst;
        return inst;
    }

    /**
     * @brief Recupera un'immagine dalla cache o la carica dal disco.
     * @param path Percorso del file immagine.
     * @param w Larghezza desiderata.
     * @param h Altezza desiderata.
     * @return QPixmap L'immagine caricata e scalata, o un pixmap nullo in caso di errore.
     */
    QPixmap get(const QString& path, int w, int h) {
        if (path.isEmpty()) return QPixmap();

        // Chiave unica basata su percorso e dimensioni
        QString key = QString("%1_%2x%3").arg(path).arg(w).arg(h);
        
        // 1. Controllo se l'immagine è già in RAM
        if (m_cache.contains(key)) {
            return *m_cache.object(key);
        }

        // 2. Caricamento dal disco (Istantaneo su SSD/NVMe)
        QImage img;
        if (img.load(path)) {
            // Scalatura di alta qualità (fulminea su Ryzen 5950X)
            QPixmap pix = QPixmap::fromImage(img.scaled(w, h, 
                                            Qt::KeepAspectRatio, 
                                            Qt::SmoothTransformation));
            
            // Inserimento in cache (costo basato sul numero di immagini)
            m_cache.insert(key, new QPixmap(pix));
            return pix;
        }

        return QPixmap();
    }

    /**
     * @brief Pulisce la cache RAM.
     */
    void clear() {
        m_cache.clear();
    }

private:
    // Costruttore privato (Singleton)
    ImageCache() {
        // Impostiamo il limite a 150 immagini scalate in memoria
        m_cache.setMaxCost(150);
    }
    
    ~ImageCache() = default;

    // Disabilita copia
    ImageCache(const ImageCache&) = delete;
    ImageCache& operator=(const ImageCache&) = delete;

    QCache<QString, QPixmap> m_cache;
};

} // namespace Gui