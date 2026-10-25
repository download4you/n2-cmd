#include "themed_style.h"
#include <QPainter>
#include <QStyleOption>

void ThemedStyle::drawPrimitive(PrimitiveElement element, const QStyleOption* option, QPainter* painter,
                                const QWidget* widget) const {
    if (element == PE_IndicatorTabClose && option && painter) {
        const QRectF rect = option->rect;
        const double side = qMin(rect.width(), rect.height());
        QColor color(150, 166, 186);
        if (option->state & State_Sunken) color = QColor(226, 238, 251);
        else if (option->state & State_MouseOver) color = QColor(238, 246, 255);
        const double arm = side * 0.26;
        const QPointF center = rect.center();
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(QPen(color, 1.4, Qt::SolidLine, Qt::RoundCap));
        painter->drawLine(center + QPointF(-arm, -arm), center + QPointF(arm, arm));
        painter->drawLine(center + QPointF(arm, -arm), center + QPointF(-arm, arm));
        painter->restore();
        return;
    }
    QProxyStyle::drawPrimitive(element, option, painter, widget);
}
