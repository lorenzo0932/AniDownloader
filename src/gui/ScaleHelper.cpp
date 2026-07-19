#include "gui/ScaleHelper.hpp"
#include <QApplication>
#include <QFontMetrics>

namespace Gui {

qreal ScaleHelper::s_scale = 1.0;

void ScaleHelper::init() {
    QFont font = QApplication::font();
    QFontMetrics fm(font);
    s_scale = fm.height() / 20.0;
}

qreal ScaleHelper::scaleFactor() {
    return s_scale;
}

int ScaleHelper::px(int basePx) {
    return qRound(basePx * s_scale);
}

int ScaleHelper::px(qreal basePx) {
    return qRound(basePx * s_scale);
}

qreal ScaleHelper::fontSize(qreal multiplier) {
    return QApplication::font().pointSizeF() * multiplier;
}

} // namespace Gui
