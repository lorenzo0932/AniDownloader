import os
from PyQt6.QtWidgets import QLabel
from PyQt6.QtGui import QPixmap
from PyQt6.QtCore import Qt

def load_poster_image(image_label: QLabel, series_path: str):
    """
    Carica e visualizza l'immagine della locandina (folder.jpg) associata a una serie.
    Se l'immagine non è trovata o il percorso non è valido, imposta un testo di placeholder.

    Args:
        image_label (QLabel): Il QLabel su cui visualizzare l'immagine.
        series_path (str): Il percorso completo di un file all'interno della cartella della serie
                           (es. "C:/SerieTV/NomeSerie/episodio.mkv").
                           La locandina viene cercata nella directory padre di questo percorso.
    """
    image_label.clear() # Pulisce qualsiasi contenuto precedente
    image_label.setAlignment(Qt.AlignmentFlag.AlignCenter) # Assicura l'allineamento

    if series_path and os.path.exists(os.path.dirname(series_path)):
        image_path = os.path.join(os.path.dirname(series_path), "folder.jpg")
        if os.path.exists(image_path):
            pixmap = QPixmap(image_path)
            if not pixmap.isNull():
                image_label.setPixmap(pixmap.scaled(image_label.size(),
                                                     Qt.AspectRatioMode.KeepAspectRatio,
                                                     Qt.TransformationMode.SmoothTransformation))
            else:
                image_label.setText("Locandina non valida")
        else:
            image_label.setText("Locandina non trovata")
    else:
        image_label.setText("Percorso non definito")
