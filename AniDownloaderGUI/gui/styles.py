# Palette
# Background Dark: #1e1e1e
# Surface: #2d2d2d
# Accent: #6200ea (Deep Purple) -> Hover: #7c4dff
# Text Main: #ffffff
# Text Secondary: #b0b0b0
# Borders: #404040

DARK_THEME_QSS = """
/* --- GENERAL --- */
QMainWindow, QDialog {
    background-color: #1e1e1e;
    color: #ffffff;
}

QWidget {
    font-family: "Segoe UI", "Roboto", "Helvetica Neue", sans-serif;
    font-size: 10pt;
    color: #ffffff;
}

/* --- GROUP BOX --- */
QGroupBox {
    border: 1px solid #404040;
    border-radius: 6px;
    margin-top: 24px;
    background-color: #2d2d2d;
}

QGroupBox::title {
    subcontrol-origin: margin;
    subcontrol-position: top left;
    padding: 0 10px;
    color: #b0b0b0;
    background-color: transparent;
}

/* --- BUTTONS --- */
QPushButton {
    background-color: #3a3a3a;
    border: 1px solid #505050;
    border-radius: 6px;
    padding: 8px 16px;
    color: #ffffff;
    font-weight: bold;
}

QPushButton:hover {
    background-color: #4a4a4a;
    border-color: #6200ea;
}

QPushButton:pressed {
    background-color: #2a2a2a;
}

QPushButton:disabled {
    background-color: #2a2a2a;
    color: #606060;
    border-color: #303030;
}

/* Primary Action Buttons (Custom ObjectName) */
QPushButton#primaryButton {
    background-color: #6200ea;
    border: 1px solid #6200ea;
}

QPushButton#primaryButton:hover {
    background-color: #7c4dff;
    border-color: #7c4dff;
}

QPushButton#dangerButton {
    background-color: #cf6679; /* Red-ish for dark mode */
    color: #000000;
    border: 1px solid #cf6679;
}

QPushButton#dangerButton:hover {
    background-color: #ff8a80;
}

/* --- INPUTS & LISTS --- */
QLineEdit, QSpinBox {
    background-color: #121212;
    border: 1px solid #404040;
    border-radius: 4px;
    padding: 6px;
    color: #ffffff;
    selection-background-color: #6200ea;
}

QLineEdit:focus, QSpinBox:focus {
    border: 1px solid #6200ea;
}

QSpinBox::up-button, QSpinBox::down-button {
    background-color: #2d2d2d;
    border: 1px solid #404040;
    border-radius: 2px;
    width: 20px;
    height: 14px; /* Explicit height for each button */
    padding: 0; /* Important for clean arrows */
}

QSpinBox::up-arrow {
    image: none;
    border-left: 4px solid transparent;
    border-right: 4px solid transparent;
    border-bottom: 6px solid #ffffff; /* White arrow for dark theme */
    width: 0;
    height: 0;
    padding: 0;
    margin: 0;
    subcontrol-origin: padding; /* Center arrow within padding */
    subcontrol-position: center;
}

QSpinBox::down-arrow {
    image: none;
    border-left: 4px solid transparent;
    border-right: 4px solid transparent;
    border-top: 6px solid #ffffff; /* White arrow for dark theme */
    width: 0;
    height: 0;
    padding: 0;
    margin: 0;
    subcontrol-origin: padding; /* Center arrow within padding */
    subcontrol-position: center;
}

QTableWidget {
    background-color: #121212;
    gridline-color: #303030;
    border: 1px solid #404040;
    border-radius: 4px;
    outline: none;
}

QTableWidget::item {
    padding: 5px;
    border: none;
}

QTableWidget::item:selected {
    background-color: #3d2c5e; /* Low purple opacity */
    color: #ffffff;
    outline: none;
    border: none;
}

QHeaderView::section {
    background-color: #2d2d2d;
    padding: 6px;
    border: none;
    border-bottom: 1px solid #404040;
    border-right: 1px solid #303030;
    font-weight: bold;
}

/* --- SCROLLBARS --- */
QScrollBar:vertical {
    border: none;
    background: #1e1e1e;
    width: 10px;
    margin: 0px;
}

QScrollBar::handle:vertical {
    background: #505050;
    min-height: 20px;
    border-radius: 5px;
}

QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
    height: 0px;
}

/* --- SPECIFIC WIDGETS --- */
QTextEdit {
    background-color: #121212;
    border: 1px solid #404040;
    border-radius: 4px;
    font-family: "Consolas", "Monospace";
    font-size: 9pt;
}

/* Warning Box Label */
QLabel#warningLabel {
    background-color: #3e2723; /* Dark orange/brown */
    color: #ffcc80;
    border: 1px solid #5d4037;
    border-radius: 6px;
    padding: 10px;
}

/* Container Footer in Settings */
QWidget#settingsFooter {
    background-color: #252525;
    border-top: 1px solid #404040;
}
"""

# Palette Light
# Background Light: #f5f5f5
# Surface: #ffffff
# Accent: #6200ea (Deep Purple) -> Hover: #7c4dff
# Text Main: #212121
# Text Secondary: #757575
# Borders: #e0e0e0

LIGHT_THEME_QSS = """
/* --- GENERAL --- */
QMainWindow, QDialog {
    background-color: #f5f5f5;
    color: #212121;
}

QWidget {
    font-family: "Segoe UI", "Roboto", "Helvetica Neue", sans-serif;
    font-size: 10pt;
    color: #212121;
}

/* --- GROUP BOX --- */
QGroupBox {
    border: 1px solid #e0e0e0;
    border-radius: 6px;
    margin-top: 24px;
    background-color: #ffffff;
}

QGroupBox::title {
    subcontrol-origin: margin;
    subcontrol-position: top left;
    padding: 0 10px;
    color: #757575;
    background-color: transparent;
}

/* --- BUTTONS --- */
QPushButton {
    background-color: #ffffff;
    border: 1px solid #d0d0d0;
    border-radius: 6px;
    padding: 8px 16px;
    color: #212121;
    font-weight: bold;
}

QPushButton:hover {
    background-color: #f0f0f0;
    border-color: #6200ea;
}

QPushButton:pressed {
    background-color: #e0e0e0;
}

QPushButton:disabled {
    background-color: #eeeeee;
    color: #bdbdbd;
    border-color: #e0e0e0;
}

/* Primary Action Buttons */
QPushButton#primaryButton {
    background-color: #6200ea;
    border: 1px solid #6200ea;
    color: #ffffff;
}

QPushButton#primaryButton:hover {
    background-color: #7c4dff;
    border-color: #7c4dff;
}

QPushButton#dangerButton {
    background-color: #d32f2f;
    color: #ffffff;
    border: 1px solid #d32f2f;
}

QPushButton#dangerButton:hover {
    background-color: #e57373;
}

/* --- INPUTS & LISTS --- */
QLineEdit, QSpinBox {
    background-color: #ffffff;
    border: 1px solid #bdbdbd;
    border-radius: 4px;
    padding: 6px;
    color: #212121;
    selection-background-color: #6200ea;
    selection-color: #ffffff;
}

QLineEdit:focus, QSpinBox:focus {
    border: 1px solid #6200ea;
}

QSpinBox::up-button, QSpinBox::down-button {
    background-color: #eeeeee;
    border: 1px solid #d0d0d0;
    border-radius: 2px;
    width: 20px;
    height: 14px; /* Explicit height for each button */
    padding: 0; /* Important for clean arrows */
}

QSpinBox::up-arrow {
    image: none;
    border-left: 4px solid transparent;
    border-right: 4px solid transparent;
    border-bottom: 6px solid #424242; /* Dark arrow for light theme */
    width: 0;
    height: 0;
    padding: 0;
    margin: 0;
    subcontrol-origin: padding; /* Center arrow within padding */
    subcontrol-position: center;
}

QSpinBox::down-arrow {
    image: none;
    border-left: 4px solid transparent;
    border-right: 4px solid transparent;
    border-top: 6px solid #424242; /* Dark arrow for light theme */
    width: 0;
    height: 0;
    padding: 0;
    margin: 0;
    subcontrol-origin: padding; /* Center arrow within padding */
    subcontrol-position: center;
}

QTableWidget {
    background-color: #ffffff;
    gridline-color: #e0e0e0;
    border: 1px solid #d0d0d0;
    border-radius: 4px;
    color: #212121;
    outline: none;
}

QTableWidget::item {
    padding: 5px;
    border: none;
}

QTableWidget::item:selected {
    background-color: #ede7f6; /* Very light purple */
    color: #6200ea;
    outline: none;
    border: none;
}

QHeaderView::section {
    background-color: #eeeeee;
    padding: 6px;
    border: none;
    border-bottom: 1px solid #d0d0d0;
    border-right: 1px solid #e0e0e0;
    font-weight: bold;
    color: #424242;
}

/* --- SCROLLBARS --- */
QScrollBar:vertical {
    border: none;
    background: #f5f5f5;
    width: 10px;
    margin: 0px;
}

QScrollBar::handle:vertical {
    background: #bdbdbd;
    min-height: 20px;
    border-radius: 5px;
}

QScrollBar::handle:vertical:hover {
    background: #9e9e9e;
}

QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
    height: 0px;
}

/* --- SPECIFIC WIDGETS --- */
QTextEdit {
    background-color: #ffffff;
    border: 1px solid #d0d0d0;
    border-radius: 4px;
    font-family: "Consolas", "Monospace";
    font-size: 9pt;
    color: #212121;
}

/* Warning Box Label */
QLabel#warningLabel {
    background-color: #fff3e0; /* Light orange */
    color: #e65100;
    border: 1px solid #ffe0b2;
    border-radius: 6px;
    padding: 10px;
}

/* Container Footer in Settings */
QWidget#settingsFooter {
    background-color: #eeeeee;
    border-top: 1px solid #d0d0d0;
}
"""
