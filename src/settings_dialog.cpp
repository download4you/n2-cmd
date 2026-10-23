#include "settings_dialog.h"
#include "backdrop.h"
#include <QButtonGroup>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {
const QVector<QPair<QString, QString>> accentChoices = {
    {"Cyan", "#4cc9f0"}, {"Violet", "#9b8cff"}, {"Mint", "#3ddc97"}, {"Amber", "#ffb454"}, {"Rose", "#ff6f91"}
};
void section(QVBoxLayout* layout, const QString& text) {
    auto* label = new QLabel(text.toUpper());
    label->setObjectName("sectionLabel");
    layout->addWidget(label);
}
}

SettingsDialog::SettingsDialog(SettingsStore* store, const QVector<ShellProfile>& profiles, QWidget* parent)
    : QDialog(parent), store_(store) {
    setObjectName("settingsDialog");
    setWindowTitle("n2-cmd Settings");
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setModal(true);
    resize(590, 780);
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(10, 10, 10, 10);
    auto* panel = new QFrame(this);
    panel->setObjectName("settingsPanel");
    auto* shadow = new QGraphicsDropShadowEffect(panel);
    shadow->setBlurRadius(42);
    shadow->setOffset(0, 14);
    shadow->setColor(QColor(0, 0, 0, 160));
    panel->setGraphicsEffect(shadow);
    outer->addWidget(panel);
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(24, 20, 24, 18);
    layout->setSpacing(12);
    auto* heading = new QHBoxLayout;
    auto* titles = new QVBoxLayout;
    auto* title = new QLabel("Settings"); title->setObjectName("settingsTitle");
    auto* subtitle = new QLabel("Make the terminal feel like yours."); subtitle->setObjectName("mutedLabel");
    titles->addWidget(title); titles->addWidget(subtitle);
    heading->addLayout(titles); heading->addStretch();
    auto* close = new QPushButton(QString(QChar(0x00d7))); close->setObjectName("closeSettingsButton");
    close->setFixedSize(30, 30); close->setAccessibleName("Close settings");
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    heading->addWidget(close);
    layout->addLayout(heading);
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* content = new QWidget; content->setObjectName("settingsContent");
    auto* body = new QVBoxLayout(content); body->setContentsMargins(0, 2, 6, 0); body->setSpacing(12);
    scroll->setWidget(content); layout->addWidget(scroll, 1);

    section(body, "Background");
    auto* background = new QHBoxLayout;
    preview_ = new QLabel; preview_->setObjectName("wallpaperPreview"); preview_->setFixedSize(148, 78);
    preview_->setAlignment(Qt::AlignCenter); preview_->setAccessibleName("Wallpaper preview");
    background->addWidget(preview_);
    auto* options = new QVBoxLayout;
    backgroundName_ = new QLabel; backgroundName_->setObjectName("wallpaperName");
    backgroundName_->setWordWrap(true); options->addWidget(backgroundName_);
    auto* buttons = new QHBoxLayout;
    auto* choose = new QPushButton("Choose image..."); choose->setObjectName("chooseWallpaperButton");
    auto* reset = new QPushButton("Use default"); reset->setObjectName("defaultWallpaperButton");
    buttons->addWidget(choose); buttons->addWidget(reset); buttons->addStretch(); options->addLayout(buttons);
    auto* note = new QLabel("PNG, JPG, WEBP, BMP or GIF. A copy is stored in your profile.");
    note->setObjectName("mutedLabel"); note->setWordWrap(true); options->addWidget(note);
    background->addLayout(options, 1); body->addLayout(background);
    connect(choose, &QPushButton::clicked, this, &SettingsDialog::chooseWallpaper);
    connect(reset, &QPushButton::clicked, this, [this] { store_->settings.background.clear(); updatePreview(); commit(); });

    section(body, "Appearance");
    opacity_ = addSlider(body, "opacitySlider", "Window opacity", 30, 100, 1, "%");
    dim_ = addSlider(body, "dimSlider", "Background dim", 0, 92, 1, "%");
    blur_ = addSlider(body, "blurSlider", "Background blur", 0, 40, 1, "px");
    tint_ = addSlider(body, "tintSlider", "Terminal tint", 0, 88, 1, "%");
    connect(opacity_, &QSlider::valueChanged, this, [this](int value) { if (!loading_) { store_->settings.opacity = value / 100.0; commit(); } });
    connect(dim_, &QSlider::valueChanged, this, [this](int value) { if (!loading_) { store_->settings.bg_dim = value / 100.0; commit(); } });
    connect(blur_, &QSlider::valueChanged, this, [this](int value) { if (!loading_) { store_->settings.bg_blur = value; commit(); } });
    connect(tint_, &QSlider::valueChanged, this, [this](int value) { if (!loading_) { store_->settings.terminal_tint = value / 100.0; commit(); } });

    section(body, "Terminal");
    auto* fontRow = new QHBoxLayout;
    auto* fontLabel = new QLabel("Font"); fontLabel->setFixedWidth(132); fontRow->addWidget(fontLabel);
    font_ = new QComboBox; font_->setObjectName("fontCombo"); font_->setEditable(true); font_->setAccessibleName("Terminal font");
    font_->addItems({"Vazir Code Hack", "Cascadia Mono", "Cascadia Code", "Consolas", "JetBrains Mono", "Fira Code", "Courier New"});
    fontRow->addWidget(font_, 1); body->addLayout(fontRow); fontLabel->setBuddy(font_);
    fontSize_ = addSlider(body, "fontSizeSlider", "Font size", 15, 52, 2, "pt");
    connect(font_, &QComboBox::currentTextChanged, this, [this](const QString& value) { if (!loading_ && !value.trimmed().isEmpty()) { store_->settings.font_family = value.trimmed(); commit(); } });
    connect(fontSize_, &QSlider::valueChanged, this, [this](int value) { if (!loading_) { store_->settings.font_size = value / 2.0; commit(); } });
    copy_ = new QCheckBox("Copy automatically when I select text with the mouse"); copy_->setObjectName("copyOnSelectCheck");
    body->addWidget(copy_);
    connect(copy_, &QCheckBox::toggled, this, [this](bool enabled) { if (!loading_) { store_->settings.copy_on_select = enabled; commit(); } });
    auto* shellRow = new QHBoxLayout;
    auto* shellLabel = new QLabel("Default shell"); shellLabel->setFixedWidth(132); shellRow->addWidget(shellLabel);
    shell_ = new QComboBox; shell_->setObjectName("defaultShellCombo"); shell_->setAccessibleName("Default shell");
    for (const auto& profile : profiles) shell_->addItem(profile.label, profile.key);
    shellRow->addWidget(shell_, 1); body->addLayout(shellRow); shellLabel->setBuddy(shell_);
    connect(shell_, &QComboBox::currentIndexChanged, this, [this](int index) { if (!loading_ && index >= 0) { store_->settings.default_shell = shell_->itemData(index).toString(); commit(); } });
    auto* historyRow = new QHBoxLayout;
    auto* historyLabel = new QLabel("Scrollback"); historyLabel->setFixedWidth(132); historyRow->addWidget(historyLabel);
    scrollback_ = new QSpinBox; scrollback_->setObjectName("scrollbackSpin"); scrollback_->setRange(500, 50000);
    scrollback_->setSingleStep(500); scrollback_->setSuffix(" lines"); scrollback_->setAccessibleName("Scrollback lines");
    historyRow->addWidget(scrollback_, 1); body->addLayout(historyRow); historyLabel->setBuddy(scrollback_);
    connect(scrollback_, &QSpinBox::valueChanged, this, [this](int value) { if (!loading_) { store_->settings.scrollback = value; commit(); } });

    section(body, "Accent");
    auto* colors = new QHBoxLayout; colors->setSpacing(10);
    accents_ = new QButtonGroup(this); accents_->setExclusive(true);
    for (const auto& choice : accentChoices) {
        auto* color = new QPushButton; color->setObjectName("accent" + choice.first);
        color->setFixedSize(30, 30); color->setCheckable(true); color->setProperty("accent", choice.second);
        color->setToolTip(choice.first); color->setAccessibleName(choice.first + " accent");
        color->setStyleSheet(QString("QPushButton{background:%1;border:2px solid transparent;border-radius:15px;padding:0;} QPushButton:checked{border-color:#fff;} QPushButton:hover,QPushButton:focus{border-color:#aab7c8;}").arg(choice.second));
        accents_->addButton(color); colors->addWidget(color);
        connect(color, &QPushButton::clicked, this, [this, choice] { store_->settings.accent = choice.second; updateStyle(); commit(); });
    }
    colors->addStretch(); body->addLayout(colors);
    auto* help = new QLabel("Ctrl+Shift+T  new tab    Ctrl+Shift+W  close tab    Ctrl+Tab  cycle\n"
        "Ctrl+wheel  zoom    F2  rename tab    Ctrl+Shift+S  save output\n"
        "Ctrl+Shift+P  quick commands    F11  fullscreen\n"
        "Mouse: drag to select, double-click a word, triple-click a line; right-click for more.");
    help->setObjectName("shortcutHelp"); help->setWordWrap(true); body->addWidget(help); body->addStretch();
    error_ = new QLabel; error_->setObjectName("settingsError"); error_->setWordWrap(true); error_->hide(); layout->addWidget(error_);
    auto* footer = new QHBoxLayout;
    auto* resetAll = new QPushButton("Reset appearance"); resetAll->setObjectName("resetAppearanceButton");
    auto* done = new QPushButton("Done"); done->setObjectName("doneSettingsButton"); done->setDefault(true);
    footer->addWidget(resetAll); footer->addStretch(); footer->addWidget(done); layout->addLayout(footer);
    connect(resetAll, &QPushButton::clicked, this, &SettingsDialog::resetAppearance);
    connect(done, &QPushButton::clicked, this, &QDialog::accept);
    updateStyle(); loadControls();
}

QSlider* SettingsDialog::addSlider(QVBoxLayout* layout, const QString& name, const QString& title, int low, int high, double scale, const QString& suffix) {
    auto* row = new QHBoxLayout;
    auto* label = new QLabel(title); label->setFixedWidth(132); row->addWidget(label);
    auto* slider = new QSlider(Qt::Horizontal); slider->setObjectName(name); slider->setRange(low, high); slider->setAccessibleName(title);
    slider->setPageStep(qMax(1, (high - low) / 10)); row->addWidget(slider, 1); label->setBuddy(slider);
    auto* value = new QLabel; value->setObjectName(name + "Value"); value->setFixedWidth(60); value->setAlignment(Qt::AlignRight | Qt::AlignVCenter); row->addWidget(value);
    auto update = [value, scale, suffix](int number) { value->setText(QString::number(number / scale, 'f', scale == 2 ? 1 : 0) + " " + suffix); };
    connect(slider, &QSlider::valueChanged, this, update); update(low); layout->addLayout(row); return slider;
}

void SettingsDialog::loadControls() {
    loading_ = true;
    const auto& s = store_->settings;
    opacity_->setValue(qRound(s.opacity * 100)); dim_->setValue(qRound(s.bg_dim * 100)); blur_->setValue(s.bg_blur); tint_->setValue(qRound(s.terminal_tint * 100));
    font_->setCurrentText(s.font_family); fontSize_->setValue(qRound(s.font_size * 2)); copy_->setChecked(s.copy_on_select);
    int shellIndex = shell_->findData(s.default_shell == "cmd" ? "native" : s.default_shell);
    shell_->setCurrentIndex(shellIndex < 0 && shell_->count() ? 0 : shellIndex); scrollback_->setValue(s.scrollback);
    for (auto* button : accents_->buttons()) button->setChecked(button->property("accent").toString() == s.accent);
    loading_ = false; updatePreview();
}
void SettingsDialog::commit() {
    store_->sanitize(); QString error;
    const bool saved = store_->save(&error);
    error_->setText(saved ? QString() : "Could not save settings: " + error); error_->setVisible(!saved);
    emit settingsChanged();
}
void SettingsDialog::updateStyle() {
    setStyleSheet(QString(
        "#settingsPanel{background:rgba(11,17,27,252);border:1px solid #354153;border-radius:16px;}"
        "#settingsContent,QScrollArea{background:transparent;} QLabel,QCheckBox{color:#dce7f5;font:10pt 'Segoe UI';}"
        "#settingsTitle{font-size:20pt;font-weight:600;} #sectionLabel{color:%1;font-size:9pt;font-weight:700;margin-top:4px;}"
        "#mutedLabel,#shortcutHelp{color:#9aabc1;font-size:9pt;} #wallpaperPreview{background:#121e2e;border:1px solid #394659;border-radius:8px;}"
        "QPushButton{background:#182434;color:#dce7f5;border:1px solid #354153;border-radius:7px;padding:7px 12px;}"
        "QPushButton:hover{background:#26374b;} QPushButton:focus,QComboBox:focus,QSpinBox:focus{border:1px solid %1;}"
        "#doneSettingsButton{background:%1;color:#07101a;font-weight:700;} #settingsError{color:#ff9aa4;}"
        "QComboBox,QSpinBox{background:#152133;color:#dce7f5;border:1px solid #354153;border-radius:6px;padding:5px;}"
        "QComboBox QAbstractItemView{background:#152133;color:#dce7f5;selection-background-color:#31506a;}"
        "QSlider::groove:horizontal{height:5px;background:#263448;border-radius:2px;} QSlider::sub-page:horizontal{background:%1;}"
        "QSlider::handle:horizontal{width:14px;margin:-5px 0;background:#edf6ff;border:2px solid %1;border-radius:7px;}"
        "QScrollBar:vertical{background:transparent;width:8px;} QScrollBar::handle:vertical{background:#435269;border-radius:4px;min-height:24px;}"
        "QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;}"
    ).arg(store_->settings.accent));
}
void SettingsDialog::updatePreview() {
    const QString path = store_->settings.background.isEmpty() ? store_->assetPath("wallpaper.jpg") : store_->settings.background;
    QImageReader reader(path); reader.setAutoTransform(true); const QImage image = reader.read();
    if (!image.isNull()) {
        QImage thumb = image.scaled(preview_->size() * 2, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        thumb = thumb.copy((thumb.width() - preview_->width() * 2) / 2, (thumb.height() - preview_->height() * 2) / 2, preview_->width() * 2, preview_->height() * 2);
        QPixmap pixmap = QPixmap::fromImage(thumb); pixmap.setDevicePixelRatio(2); preview_->setPixmap(pixmap);
    } else { preview_->clear(); preview_->setText("Default gradient"); }
    backgroundName_->setText(store_->settings.background.isEmpty() ? "Default wallpaper" : QFileInfo(path).fileName());
    backgroundName_->setToolTip(path);
}
bool SettingsDialog::applyWallpaper(const QString& path, QString* error) {
    const QString target = QDir(store_->dataDirectory()).filePath("wallpaper_custom.png");
    QString detail;
    if (!Backdrop::importWallpaper(path, target, &detail)) {
        if (error) *error = detail; error_->setText(detail); error_->show(); return false;
    }
    const QDir profile(store_->dataDirectory());
    for (const QString& name : profile.entryList({QStringLiteral("wallpaper_custom.*")}, QDir::Files)) {
        const QString candidate = profile.filePath(name);
        if (candidate.compare(target, Qt::CaseInsensitive) != 0) QFile::remove(candidate);
    }
    store_->settings.background = target; updatePreview(); commit(); return true;
}
void SettingsDialog::chooseWallpaper() {
    const QString path = QFileDialog::getOpenFileName(this, "Choose a background image", QDir::homePath(), "Images (*.png *.jpg *.jpeg *.webp *.bmp *.gif)");
    if (!path.isEmpty()) applyWallpaper(path);
}
void SettingsDialog::resetAppearance() { store_->resetAppearance(); updateStyle(); loadControls(); commit(); }
