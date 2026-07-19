#pragma once

#include <QtGlobal>

namespace Gui {

class ScaleHelper {
public:
    static void init();

    static qreal scaleFactor();
    static int px(int basePx);
    static int px(qreal basePx);
    static qreal fontSize(qreal multiplier = 1.0);

private:
    static qreal s_scale;
};

} // namespace Gui
