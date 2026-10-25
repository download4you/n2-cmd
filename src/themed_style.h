#pragma once
#include <QProxyStyle>

// Fusion renders the tab close button with the native Windows glyph, which is
// a bright red square and clashes with the dark theme. This proxy draws a
// muted cross instead and keeps every other Fusion detail untouched.
class ThemedStyle : public QProxyStyle {
public:
    explicit ThemedStyle(QStyle* base) : QProxyStyle(base) {}
    void drawPrimitive(PrimitiveElement element, const QStyleOption* option, QPainter* painter,
                       const QWidget* widget = nullptr) const override;
};
