#include "gui/Styles.hpp"
#include "gui/ScaleHelper.hpp"
#include <QString>

namespace Gui {

static QString applyScale(QString qss) {
    qss.replace("@@PX2@@",   QString::number(ScaleHelper::px(2)));
    qss.replace("@@PX4@@",   QString::number(ScaleHelper::px(4)));
    qss.replace("@@PX5@@",   QString::number(ScaleHelper::px(5)));
    qss.replace("@@PX6@@",   QString::number(ScaleHelper::px(6)));
    qss.replace("@@PX8@@",   QString::number(ScaleHelper::px(8)));
    qss.replace("@@PX10@@",  QString::number(ScaleHelper::px(10)));
    qss.replace("@@PX14@@",  QString::number(ScaleHelper::px(14)));
    qss.replace("@@PX16@@",  QString::number(ScaleHelper::px(16)));
    qss.replace("@@PX20@@",  QString::number(ScaleHelper::px(20)));
    qss.replace("@@PX24@@",  QString::number(ScaleHelper::px(24)));
    qss.replace("@@PX60@@",  QString::number(ScaleHelper::px(60)));
    qss.replace("@@PT_LOG@@",  QString::number(ScaleHelper::fontSize(0.9), 'f', 1));
    qss.replace("@@PT_FETCH@@", QString::number(ScaleHelper::fontSize(1.05), 'f', 1));
    qss.replace("@@PT_BASE@@", QString::number(ScaleHelper::fontSize(0.92), 'f', 1));
    return qss;
}

QString getDarkTheme() {
    return applyScale(R"raw(
/* --- GENERAL --- */
QMainWindow, QDialog, #slidingContainer, #downloadView, #managerView {
    background-color: #1e1e1e;
    color: #ffffff;
}

QWidget {
    font-size: @@PT_BASE@@pt;
    color: #ffffff;
}

/* --- NAVBAR SUPERIORE --- */
QWidget#topNavBar {
    background-color: #121212;
    border-bottom: 2px solid #333333;
}

QPushButton#navTabButton {
    background-color: transparent;
    border: none;
    border-bottom: 4px solid transparent;
    padding: 0px @@PX20@@px;
    font-weight: bold;
    color: #888888;
    min-height: @@PX60@@px;
}

QPushButton#navTabButton:hover {
    background-color: #252525;
    color: #ffffff;
}

QPushButton#navTabButton:checked {
    color: #6200ea;
    border-bottom: 4px solid #6200ea;
    background-color: #1a1a1a;
}

QPushButton#navTabButton:pressed {
    background-color: #000000;
}

/* --- GROUP BOX --- */
QGroupBox {
    border: 1px solid #404040;
    border-radius: @@PX6@@px;
    margin-top: @@PX24@@px;
    background-color: #2d2d2d;
}

QGroupBox::title {
    subcontrol-origin: margin;
    subcontrol-position: top left;
    padding: 0 @@PX10@@px;
    color: #b0b0b0;
    background-color: transparent;
}

/* --- BUTTONS --- */
QPushButton {
    background-color: #3a3a3a;
    border: 1px solid #505050;
    border-radius: @@PX6@@px;
    padding: @@PX8@@px @@PX16@@px;
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

/* Primary Action Buttons */
QPushButton#primaryButton {
    background-color: #6200ea;
    border: 1px solid #6200ea;
}

QPushButton#primaryButton:hover {
    background-color: #7c4dff;
    border-color: #7c4dff;
}

QPushButton#primaryButton:pressed {
    background-color: #5000ca;
}

QPushButton#dangerButton {
    background-color: #cf6679;
    color: #000000;
    border: 1px solid #cf6679;
}

QPushButton#dangerButton:hover {
    background-color: #ff8a80;
}

QPushButton#dangerButton:pressed {
    background-color: #b05566;
}

QPushButton#fetchNameButton {
    background-color: #2d2d2d;
    color: #bb86fc;
    border: 1px solid #404040;
    border-radius: @@PX4@@px;
    font-size: @@PT_FETCH@@pt;
    font-weight: bold;
}
QPushButton#fetchNameButton:hover {
    background-color: #3d3d3d;
    border-color: #bb86fc;
}
QPushButton#fetchNameButton:pressed {
    background-color: #1a1a1a;
}
QPushButton#fetchNameButton:disabled {
    background-color: #1e1e1e;
    color: #555555;
    border-color: #333333;
}

/* --- INPUTS & LISTS --- */
QLineEdit, QSpinBox {
    background-color: #121212;
    border: 1px solid #404040;
    border-radius: @@PX4@@px;
    padding: @@PX6@@px;
    color: #ffffff;
    selection-background-color: #6200ea;
}

QLineEdit:focus, QSpinBox:focus {
    border: 1px solid #6200ea;
}

QSpinBox::up-button, QSpinBox::down-button {
    background-color: #2d2d2d;
    border: 1px solid #404040;
    border-radius: @@PX2@@px;
    width: @@PX20@@px;
    height: @@PX14@@px;
    padding: 0;
}

QSpinBox::up-arrow {
    image: none;
    border-left: 4px solid transparent;
    border-right: 4px solid transparent;
    border-bottom: 6px solid #ffffff;
    width: 0;
    height: 0;
    padding: 0;
    margin: 0;
    subcontrol-origin: padding;
    subcontrol-position: center;
}

QSpinBox::down-arrow {
    image: none;
    border-left: 4px solid transparent;
    border-right: 4px solid transparent;
    border-top: 6px solid #ffffff;
    width: 0;
    height: 0;
    padding: 0;
    margin: 0;
    subcontrol-origin: padding;
    subcontrol-position: center;
}

QTableWidget {
    background-color: #121212;
    gridline-color: #303030;
    border: 1px solid #404040;
    border-radius: @@PX4@@px;
    outline: none;
}

QTableWidget::item {
    padding: @@PX5@@px;
    border: none;
}

QTableWidget::item:selected {
    background-color: #3d2c5e;
    color: #ffffff;
    outline: none;
    border: none;
}

QHeaderView::section {
    background-color: #2d2d2d;
    padding: @@PX6@@px;
    border: none;
    border-bottom: 1px solid #404040;
    border-right: 1px solid #303030;
    font-weight: bold;
}

/* --- SCROLLBARS --- */
QScrollBar:vertical {
    border: none;
    background: #1e1e1e;
    width: @@PX10@@px;
    margin: 0px;
}

QScrollBar::handle:vertical {
    background: #505050;
    min-height: @@PX20@@px;
    border-radius: @@PX5@@px;
}

QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
    height: 0px;
}

/* --- SPECIFIC WIDGETS --- */
QProgressBar {
    background-color: #121212;
    border: 1px solid #404040;
    border-radius: @@PX4@@px;
    text-align: center;
    color: #ffffff;
    height: @@PX20@@px;
}

QProgressBar::chunk {
    background-color: #6200ea;
    border-radius: @@PX2@@px;
}

QTextEdit {
    background-color: #121212;
    border: 1px solid #404040;
    border-radius: @@PX4@@px;
    font-family: "Consolas", "Monospace";
    font-size: @@PT_LOG@@pt;
}

/* Warning Box Label */
QLabel#warningLabel {
    background-color: #3e2723;
    color: #ffcc80;
    border: 1px solid #5d4037;
    border-radius: @@PX6@@px;
    padding: @@PX10@@px;
}

/* Series Manager Buttons */
QPushButton#addSeriesButton {
    background-color: #4CAF50;
    color: white;
    font-weight: bold;
    padding: @@PX8@@px;
    border-radius: @@PX4@@px;
}
QPushButton#addSeriesButton:hover {
    background-color: #66BB6A;
}
QPushButton#addSeriesButton:pressed {
    background-color: #388E3C;
}

QPushButton#editSeriesButton {
    padding: @@PX8@@px;
    border-radius: @@PX4@@px;
}
QPushButton#editSeriesButton:hover {
    background-color: #e0e0e0;
    border-color: #6200ea;
}

QPushButton#removeSeriesButton {
    background-color: #f44336;
    color: white;
    font-weight: bold;
    padding: @@PX8@@px;
    border-radius: @@PX4@@px;
}
QPushButton#removeSeriesButton:hover {
    background-color: #e53935;
}
QPushButton#removeSeriesButton:pressed {
    background-color: #c62828;
}

/* Container Footer in Settings */
QWidget#settingsFooter {
    background-color: #252525;
    border-top: 1px solid #404040;
}

QToolTip {
    background-color: #2d2d2d;
    color: #e0e0e0;
    border: 1px solid #6200ea;
    border-radius: @@PX6@@px;
    padding: @@PX4@@px @@PX8@@px;
}
)raw");
}

QString getLightTheme() {
    return applyScale(R"raw(
/* --- GENERAL --- */
QMainWindow, QDialog, #slidingContainer, #downloadView, #managerView {
    background-color: #f5f5f5;
    color: #212121;
}

QWidget {
    font-size: @@PT_BASE@@pt;
    color: #212121;
}

/* --- NAVBAR --- */
QWidget#topNavBar {
    background-color: #ffffff;
    border-bottom: 1px solid #dddddd;
}

QPushButton#navTabButton {
    background-color: transparent;
    border: none;
    border-bottom: 4px solid transparent;
    padding: 0px @@PX20@@px;
    color: #757575;
    min-height: @@PX60@@px;
}

QPushButton#navTabButton:checked {
    color: #6200ea;
    border-bottom: 4px solid #6200ea;
}

/* --- GROUP BOX --- */
QGroupBox {
    border: 1px solid #e0e0e0;
    border-radius: @@PX6@@px;
    margin-top: @@PX24@@px;
    background-color: #ffffff;
}

QGroupBox::title {
    subcontrol-origin: margin;
    subcontrol-position: top left;
    padding: 0 @@PX10@@px;
    color: #757575;
    background-color: transparent;
}

/* --- BUTTONS --- */
QPushButton {
    background-color: #ffffff;
    border: 1px solid #d0d0d0;
    border-radius: @@PX6@@px;
    padding: @@PX8@@px @@PX16@@px;
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

QPushButton#primaryButton:pressed {
    background-color: #5000ca;
}

QPushButton#dangerButton {
    background-color: #d32f2f;
    color: #ffffff;
    border: 1px solid #d32f2f;
}

QPushButton#dangerButton:hover {
    background-color: #e57373;
}

QPushButton#dangerButton:pressed {
    background-color: #b05566;
}

QPushButton#fetchNameButton {
    background-color: #f0f0f0;
    color: #6200ea;
    border: 1px solid #bdbdbd;
    border-radius: @@PX4@@px;
    font-size: @@PT_FETCH@@pt;
    font-weight: bold;
}
QPushButton#fetchNameButton:hover {
    background-color: #e0e0e0;
    border-color: #6200ea;
}
QPushButton#fetchNameButton:pressed {
    background-color: #d0d0d0;
}
QPushButton#fetchNameButton:disabled {
    background-color: #f5f5f5;
    color: #bdbdbd;
    border-color: #e0e0e0;
}

/* --- INPUTS & LISTS --- */
QLineEdit, QSpinBox {
    background-color: #ffffff;
    border: 1px solid #bdbdbd;
    border-radius: @@PX4@@px;
    padding: @@PX6@@px;
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
    border-radius: @@PX2@@px;
    width: @@PX20@@px;
    height: @@PX14@@px;
    padding: 0;
}

QSpinBox::up-arrow {
    image: none;
    border-left: 4px solid transparent;
    border-right: 4px solid transparent;
    border-bottom: 6px solid #424242;
    width: 0;
    height: 0;
    padding: 0;
    margin: 0;
    subcontrol-origin: padding;
    subcontrol-position: center;
}

QSpinBox::down-arrow {
    image: none;
    border-left: 4px solid transparent;
    border-right: 4px solid transparent;
    border-top: 6px solid #424242;
    width: 0;
    height: 0;
    padding: 0;
    margin: 0;
    subcontrol-origin: padding;
    subcontrol-position: center;
}

QTableWidget {
    background-color: #ffffff;
    gridline-color: #e0e0e0;
    border: 1px solid #d0d0d0;
    border-radius: @@PX4@@px;
    color: #212121;
    outline: none;
}

QTableWidget::item {
    padding: @@PX5@@px;
    border: none;
}

QTableWidget::item:selected {
    background-color: #ede7f6;
    color: #6200ea;
    outline: none;
    border: none;
}

QHeaderView::section {
    background-color: #eeeeee;
    padding: @@PX6@@px;
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
    width: @@PX10@@px;
    margin: 0px;
}

QScrollBar::handle:vertical {
    background: #bdbdbd;
    min-height: @@PX20@@px;
    border-radius: @@PX5@@px;
}

QScrollBar::handle:vertical:hover {
    background: #9e9e9e;
}

QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
    height: 0px;
}

/* --- SPECIFIC WIDGETS --- */
QProgressBar {
    background-color: #ffffff;
    border: 1px solid #d0d0d0;
    border-radius: @@PX4@@px;
    text-align: center;
    color: #212121;
    height: @@PX20@@px;
}

QProgressBar::chunk {
    background-color: #6200ea;
    border-radius: @@PX2@@px;
}

QTextEdit {
    background-color: #ffffff;
    border: 1px solid #d0d0d0;
    border-radius: @@PX4@@px;
    font-family: "Consolas", "Monospace";
    font-size: @@PT_LOG@@pt;
    color: #212121;
}

/* Warning Box Label */
QLabel#warningLabel {
    background-color: #fff3e0;
    color: #e65100;
    border: 1px solid #ffe0b2;
    border-radius: @@PX6@@px;
    padding: @@PX10@@px;
}

/* Series Manager Buttons */
QPushButton#addSeriesButton {
    background-color: #4CAF50;
    color: white;
    font-weight: bold;
    padding: @@PX8@@px;
    border-radius: @@PX4@@px;
}
QPushButton#addSeriesButton:hover {
    background-color: #66BB6A;
}
QPushButton#addSeriesButton:pressed {
    background-color: #388E3C;
}

QPushButton#editSeriesButton {
    padding: @@PX8@@px;
    border-radius: @@PX4@@px;
}
QPushButton#editSeriesButton:hover {
    background-color: #f0f0f0;
    border-color: #6200ea;
}

QPushButton#removeSeriesButton {
    background-color: #f44336;
    color: white;
    font-weight: bold;
    padding: @@PX8@@px;
    border-radius: @@PX4@@px;
}
QPushButton#removeSeriesButton:hover {
    background-color: #e53935;
}
QPushButton#removeSeriesButton:pressed {
    background-color: #c62828;
}

/* Container Footer in Settings */
QWidget#settingsFooter {
    background-color: #eeeeee;
    border-top: 1px solid #d0d0d0;
}

QToolTip {
    background-color: #ffffff;
    color: #212121;
    border: 1px solid #6200ea;
    border-radius: @@PX6@@px;
    padding: @@PX4@@px @@PX8@@px;
}
)raw");
}

} // namespace Gui
