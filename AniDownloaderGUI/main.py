import sys
import multiprocessing as mp 
from pathlib import Path
project_root = Path(__file__).parent.parent
sys.path.append(str(project_root))

from PyQt6.QtWidgets import QApplication
from PyQt6.QtCore import Qt
from gui.main_window import AniDownloaderGUI

if __name__ == '__main__':
    mp.freeze_support() # Aggiunta la chiamata a freeze_support()
    QApplication.setHighDpiScaleFactorRoundingPolicy(Qt.HighDpiScaleFactorRoundingPolicy.PassThrough)
    app = QApplication(sys.argv)
    app.setStyle("Fusion")
    window = AniDownloaderGUI()
    window.show()
    sys.exit(app.exec())
