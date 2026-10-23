#include "chrome.h"
#include <QContextMenuEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QWindow>
#include <cmath>

namespace {
QIcon controlIcon(const QString& kind, const QColor& color = QColor("#a4b2c7")) {
    QPixmap pixmap(24, 24);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(color, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    if (kind == "close") { p.drawLine(8, 8, 16, 16); p.drawLine(16, 8, 8, 16); }
    else if (kind == "minimize") p.drawLine(7, 12, 17, 12);
    else if (kind == "maximize") p.drawRect(QRectF(7, 7, 10, 10));
    else if (kind == "restore") { p.drawRect(QRectF(6, 9, 9, 9)); p.drawLine(9, 6, 18, 6); p.drawLine(18, 6, 18, 15); }
    else if (kind == "plus") { p.drawLine(7, 12, 17, 12); p.drawLine(12, 7, 12, 17); }
    else if (kind == "quick") {
        QPainterPath bolt;
        bolt.moveTo(14, 3); bolt.lineTo(6, 14); bolt.lineTo(11, 14);
        bolt.lineTo(9, 22); bolt.lineTo(19, 10); bolt.lineTo(14, 10); bolt.closeSubpath();
        p.fillPath(bolt, color);
    } else {
        p.drawEllipse(QPointF(12, 12), 5, 5);
        p.drawEllipse(QPointF(12, 12), 2, 2);
        for (int i = 0; i < 8; ++i) {
            const double angle = i * 3.141592653589793 / 4;
            p.drawLine(QPointF(12 + 6 * std::cos(angle), 12 + 6 * std::sin(angle)), QPointF(12 + 8 * std::cos(angle), 12 + 8 * std::sin(angle)));
        }
    }
    return QIcon(pixmap);
}

QToolButton* button(QWidget* parent, const QString& name, const QString& text, const QString& icon) {
    auto* b = new QToolButton(parent);
    b->setObjectName(name);
    b->setAccessibleName(text);
    b->setToolTip(text);
    b->setIcon(controlIcon(icon));
    b->setIconSize(QSize(24, 24));
    b->setFixedSize(36, 30);
    b->setCursor(Qt::PointingHandCursor);
    b->setFocusPolicy(Qt::StrongFocus);
    return b;
}
}

TerminalTabBar::TerminalTabBar(QWidget* parent) : QTabBar(parent) {
    setObjectName(QStringLiteral("terminalTabs"));
    setAccessibleName(QStringLiteral("Terminal tabs"));
    setTabsClosable(true);
    setExpanding(false);
    setUsesScrollButtons(true);
    setElideMode(Qt::ElideRight);
    setDocumentMode(true);
    setDrawBase(false);
    setFocusPolicy(Qt::StrongFocus);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
}

void TerminalTabBar::contextMenuEvent(QContextMenuEvent* event) {
    const int index = tabAt(event->pos());
    if (index >= 0) { emit menuRequested(index, event->globalPos()); event->accept(); }
}
void TerminalTabBar::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::MiddleButton && tabAt(event->position().toPoint()) >= 0) {
        emit tabCloseRequested(tabAt(event->position().toPoint())); event->accept(); return;
    }
    QTabBar::mousePressEvent(event);
}
void TerminalTabBar::mouseDoubleClickEvent(QMouseEvent* event) {
    const int index = tabAt(event->position().toPoint());
    if (event->button() == Qt::LeftButton && index >= 0) { emit renameRequested(index); event->accept(); return; }
    QTabBar::mouseDoubleClickEvent(event);
}
void TerminalTabBar::wheelEvent(QWheelEvent* event) {
    if (event->angleDelta().y()) emit cycleRequested(event->angleDelta().y() > 0 ? -1 : 1);
    event->accept();
}

TopBar::TopBar(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("topBar"));
    setFixedHeight(88);
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(12, 6, 8, 5);
    outer->setSpacing(4);
    auto* title = new QHBoxLayout;
    title->setSpacing(4);
    brand_ = new QLabel(QStringLiteral(">_  n2-cmd"), this);
    brand_->setObjectName(QStringLiteral("brandLabel"));
    brand_->setAccessibleName(QStringLiteral("n2-cmd"));
    title->addWidget(brand_);
    title->addStretch();
    quick_ = button(this, "quickButton", "Quick commands (Ctrl+Shift+P)", "quick");
    auto* settings = button(this, "settingsButton", "Settings (Ctrl+,)", "settings");
    auto* minimize = button(this, "minimizeButton", "Minimize", "minimize");
    maximize_ = button(this, "maximizeButton", "Maximize", "maximize");
    auto* close = button(this, "closeWindowButton", "Close window", "close");
    for (auto* b : {quick_, settings, minimize, maximize_, close}) title->addWidget(b);
    outer->addLayout(title);
    auto* navigation = new QHBoxLayout;
    navigation->setContentsMargins(0, 0, 0, 0);
    navigation->setSpacing(5);
    tabs_ = new TerminalTabBar(this);
    navigation->addWidget(tabs_);
    auto* plus = button(this, "newTabButton", "New tab (Ctrl+Shift+T); right-click to choose shell", "plus");
    plus->setContextMenuPolicy(Qt::CustomContextMenu);
    navigation->addWidget(plus);
    navigation->addStretch();
    outer->addLayout(navigation);
    connect(tabs_, &QTabBar::currentChanged, this, &TopBar::tabActivated);
    connect(tabs_, &QTabBar::tabCloseRequested, this, &TopBar::tabCloseRequested);
    connect(tabs_, &TerminalTabBar::renameRequested, this, &TopBar::tabRenameRequested);
    connect(tabs_, &TerminalTabBar::menuRequested, this, &TopBar::tabMenuRequested);
    connect(tabs_, &TerminalTabBar::cycleRequested, this, &TopBar::cycleRequested);
    connect(plus, &QToolButton::clicked, this, &TopBar::newTabRequested);
    connect(plus, &QWidget::customContextMenuRequested, this, [this, plus](QPoint p) { emit profileMenuRequested(plus->mapToGlobal(p)); });
    connect(quick_, &QToolButton::clicked, this, [this] { emit quickMenuRequested(quickAnchor()); });
    connect(settings, &QToolButton::clicked, this, &TopBar::settingsRequested);
    connect(minimize, &QToolButton::clicked, this, &TopBar::minimizeRequested);
    connect(maximize_, &QToolButton::clicked, this, &TopBar::maximizeRequested);
    connect(close, &QToolButton::clicked, this, &TopBar::closeRequested);
    setAccent(QStringLiteral("#3ddc97"));
}

void TopBar::setTabs(const QStringList& titles, int activeIndex) {
    const QSignalBlocker blocker(tabs_);
    while (tabs_->count() > titles.size()) tabs_->removeTab(tabs_->count() - 1);
    for (int i = 0; i < titles.size(); ++i) {
        if (i >= tabs_->count()) tabs_->addTab(titles[i]); else tabs_->setTabText(i, titles[i]);
        tabs_->setTabToolTip(i, titles[i]);
    }
    tabs_->setCurrentIndex(activeIndex);
}

void TopBar::setAccent(const QString& accent) {
    brand_->setStyleSheet(QStringLiteral("color:%1;font:600 12pt 'Segoe UI';letter-spacing:1px;").arg(accent));
    quick_->setIcon(controlIcon("quick", QColor(accent)));
    setStyleSheet(QStringLiteral(
        "#topBar{background:rgba(10,16,27,190);border-bottom:1px solid rgba(255,255,255,22);border-top-left-radius:12px;border-top-right-radius:12px;}"
        "QToolButton{border:0;border-radius:6px;background:transparent;} QToolButton:hover{background:rgba(255,255,255,22);}"
        "QToolButton:pressed{background:rgba(255,255,255,36);} QToolButton:focus{border:1px solid %1;}"
        "#closeWindowButton:hover{background:#b52c43;}"
        "QTabBar::tab{color:#99a9be;background:rgba(14,22,34,170);border:1px solid rgba(255,255,255,17);border-radius:7px;min-width:105px;max-width:220px;padding:7px 10px;margin-right:5px;font:10pt 'Segoe UI';}"
        "QTabBar::tab:selected{color:#e6eefb;background:rgba(40,65,65,190);border-bottom:2px solid %1;}"
        "QTabBar::tab:hover{color:#e6eefb;background:rgba(255,255,255,18);}"
        "QTabBar::scroller{width:25px;} QTabBar QToolButton{background:#13202e;}").arg(accent));
}

void TopBar::setMaximized(bool maximized) {
    maximize_->setIcon(controlIcon(maximized ? "restore" : "maximize"));
    maximize_->setAccessibleName(maximized ? "Restore" : "Maximize");
    maximize_->setToolTip(maximized ? "Restore" : "Maximize");
}
QPoint TopBar::quickAnchor() const { return quick_->mapToGlobal(QPoint(0, quick_->height())); }
void TopBar::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && event->position().y() < 42 && window()->windowHandle()) {
        window()->windowHandle()->startSystemMove(); event->accept(); return;
    }
    QWidget::mousePressEvent(event);
}
void TopBar::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && event->position().y() < 42) { emit maximizeRequested(); event->accept(); return; }
    QWidget::mouseDoubleClickEvent(event);
}

TerminalStatusBar::TerminalStatusBar(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("terminalStatus"));
    setFixedHeight(30);
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(14, 0, 14, 0);
    state_ = new QLabel(this); state_->setObjectName("sessionStateLabel");
    grid_ = new QLabel(this); grid_->setObjectName("gridSizeLabel");
    help_ = new QLabel(this); help_->setObjectName("statusHelpLabel");
    help_->setMinimumWidth(0); help_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    layout->addWidget(state_, 1); layout->addWidget(grid_, 0, Qt::AlignCenter); layout->addWidget(help_, 1, Qt::AlignRight);
    setStyleSheet("#terminalStatus{background:rgba(10,16,27,200);border-top:1px solid rgba(255,255,255,22);} QLabel{font:8pt 'Segoe UI';color:#94a4ba;}");
}
void TerminalStatusBar::setAccent(const QString& accent) { accent_ = accent; }
void TerminalStatusBar::setInfo(const QString& shell, bool running, int columns, int rows, double opacity) {
    const QString state = running ? QStringLiteral("running") : QStringLiteral("ended - Enter restarts");
    state_->setText(QStringLiteral("<span style='color:%1'>%2</span>  %3  %4")
        .arg(running ? accent_ : QStringLiteral("#ff8c9a"), QString(QChar(0x25cf)), shell.toHtmlEscaped(), state));
    grid_->setText(columns > 0 ? QStringLiteral("%1 %2 %3").arg(columns).arg(QChar(0x00d7)).arg(rows) : QString());
    help_->setText(QStringLiteral("opacity %1%    UTF-8    Ctrl+Shift+T  new tab    Ctrl+Shift+C / V  copy / paste").arg(qRound(opacity * 100)));
    help_->setToolTip(help_->text());
}
