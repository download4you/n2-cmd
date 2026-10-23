#include "terminal_model.h"
#include "terminal_view.h"
#include "settings_store.h"
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QFontDatabase>
#include <QMouseEvent>
#include <QPainter>
#include <QTextLayout>
#include <QtTest>

class TerminalTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { QVERIFY(QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/Vazir-Code-Hack.ttf")) >= 0); }
    void unicodeStreaming() {
        TerminalModel model(80, 8);
        const QString value = QString::fromUtf8("abc \xd8\xb3\xd9\x84\xd8\xa7\xd9\x85 e\xcc\x81 \xf0\x9f\x98\x80 xyz");
        const auto bytes = value.toUtf8();
        for (char byte : bytes) model.feed(QByteArray(1, byte));
        QCOMPARE(model.plainText(), value);
    }
    void reflowAndPendingWrap() {
        for (int length : {6, 8, 12, 16, 24}) {
            TerminalModel model(8, 20);
            const QString value = QStringLiteral("abcdefghijklmnopqrstuvwxyz").left(length);
            model.feed(value.toUtf8());
            QCOMPARE(model.plainText(), value);
            for (int width : {3, 5, 4, 12, 8}) {
                model.resize(width, 20);
                QCOMPARE(model.plainText(), value);
            }
            model.resize(3, 20);
            model.feed("X");
            QCOMPARE(model.plainText(), value + u'X');
        }
    }
    void hardNewlineStaysHard() {
        TerminalModel model(8, 10);
        model.feed("abcdefgh\r\nsecond\r\n");
        model.resize(4, 10);
        model.feed("X");
        QCOMPARE(model.plainText(), QStringLiteral("abcdefgh\nsecond\nX"));
    }
    void longPersianAndMixedReflow() {
        const QString salam = QString::fromUtf8("\xd8\xb3\xd9\x84\xd8\xa7\xd9\x85");
        for (const QString& value : {salam.repeated(30), (salam + QStringLiteral("012345/long_")).repeated(14)}) {
            TerminalModel model(40, 40);
            model.feed(value.toUtf8());
            for (int width : {31, 17, 80, 40}) { model.resize(width, 40); QCOMPARE(model.plainText(), value); }
            model.feed("!");
            QCOMPARE(model.plainText(), value + u'!');
        }
    }
    void historyAndEviction() {
        TerminalModel model(12, 3, 3);
        model.feed("one\r\ntwo\r\nthree\r\nfour\r\nfive\r\nsix\r\nseven");
        QCOMPARE(model.historyRows(), 3);
        QCOMPARE(model.plainText(), QStringLiteral("two\nthree\nfour\nfive\nsix\nseven"));
        model.setHistoryLimit(1);
        QCOMPARE(model.plainText(), QStringLiteral("four\nfive\nsix\nseven"));
        model.clearHistory();
        QCOMPARE(model.plainText(), QStringLiteral("five\nsix\nseven"));
    }
    void wrappedHistoryCopy() {
        TerminalModel model(8, 3, 100);
        const QString value = QStringLiteral("0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ");
        model.feed(value.toUtf8());
        QCOMPARE(model.plainText(), value);
        model.resize(5, 3);
        QCOMPARE(model.plainText(), value);
        model.resize(20, 3);
        QCOMPARE(model.plainText(), value);
        model.feed("!");
        QCOMPARE(model.plainText(), value + u'!');
    }
    void wideCellReflow() {
        const QString emoji = QString::fromUcs4(U"\U0001f600");
        const QString value = (QStringLiteral("abc") + emoji + QStringLiteral("de") + emoji).repeated(4);
        TerminalModel model(8, 20);
        model.feed(value.toUtf8());
        QCOMPARE(model.plainText(), value);
        for (int width : {3, 5, 11, 8}) { model.resize(width, 20); QCOMPARE(model.plainText(), value); }
        model.feed("!"); QCOMPARE(model.plainText(), value + u'!');
    }
    void alternateScreen() {
        TerminalModel model(20, 4);
        model.feed("primary\x1b[?1049hsecondary");
        QVERIFY(model.alternateScreen());
        QVERIFY(model.plainText().contains(QStringLiteral("secondary")));
        model.feed("\x1b[?1049l");
        QVERIFY(!model.alternateScreen());
        QCOMPARE(model.plainText(), QStringLiteral("primary"));
    }
    void stylesAndQueries() {
        TerminalModel model(20, 4);
        QSignalSpy response(&model, &TerminalModel::responseReady);
        model.feed("\x1b[1;3;4;38;2;12;34;56mA\x1b[0mB\x1b[6n");
        const auto row = model.row(0);
        QVERIFY(row.cells[0].bold && row.cells[0].italic && row.cells[0].underline);
        QCOMPARE(row.cells[0].foreground, QColor(12, 34, 56));
        QVERIFY(!row.cells[1].bold);
        model.feed("\r\x1b[31mR\x1b[91;1mB");
        const auto colors = model.row(0);
        QCOMPARE(colors.cells[0].foreground, QColor(0xff, 0x6b, 0x7a));
        QCOMPARE(colors.cells[1].foreground, QColor(0xff, 0x8a, 0x96));
        QVERIFY(!response.isEmpty());
        QCOMPARE(response.last().first().toByteArray(), QByteArray("\x1b[1;3R"));
    }
    void keyboardModesAndPaste() {
        TerminalModel model;
        QSignalSpy response(&model, &TerminalModel::responseReady);
        model.key(VTERM_KEY_UP);
        QCOMPARE(response.takeLast().first().toByteArray(), QByteArray("\x1b[A"));
        model.feed("\x1b[?1h"); model.key(VTERM_KEY_UP);
        QCOMPARE(response.takeLast().first().toByteArray(), QByteArray("\x1bOA"));
        model.feed("\x1b[?2004h"); response.clear(); model.paste(QStringLiteral("hello"));
        QByteArray combined; for (const auto& value : response) combined += value.first().toByteArray();
        QCOMPARE(combined, QByteArray("\x1b[200~hello\x1b[201~"));
    }
    void oscTitleAndDirectory() {
        TerminalModel model;
        QSignalSpy title(&model, &TerminalModel::titleChanged), directory(&model, &TerminalModel::directoryChanged);
        model.feed("\x1b]2;test title\x07\x1b]7;file:///D:/folder%20name\x07");
        QCOMPARE(title.last().first().toString(), QStringLiteral("test title"));
        QVERIFY(!directory.isEmpty());
        QVERIFY(directory.last().first().toString().contains(QStringLiteral("folder name")));
    }
    void defaultTitlesAreIgnoredAndLongTitlesAreBounded() {
        TerminalModel model;
        QSignalSpy title(&model, &TerminalModel::titleChanged);
        model.feed("\x1b]0;Administrator: Command Prompt\x07");
        model.feed("\x1b]0;Windows PowerShell\x07");
        model.feed("\x1b]0;cmd.exe\x07");
        QVERIFY(title.isEmpty());
        const QString longTitle = QStringLiteral("0123456789").repeated(8);
        model.feed(("\x1b]0;" + longTitle + "\x07").toUtf8());
        QCOMPARE(title.last().first().toString(), longTitle.left(71) + QChar(0x2026));
    }
    void bidiCursorHitAndCopy() {
        QTemporaryDir directory;
        SettingsStore settings(directory.path());
        ShellProfile profile; profile.key = QStringLiteral("native");
        TerminalView view(&settings, profile, {}, nullptr, false);
        view.resize(850, 420);
        view.model().resize(40, 12);
        const QString text = QString::fromUtf8("abc \xd8\xb3\xd9\x84\xd8\xa7\xd9\x85 xyz");
        view.feedForTest(text.toUtf8());
        const auto first = view.cellRect(0, 4), last = view.cellRect(0, 7);
        QVERIFY2(first.left() > last.left(), "Persian cell order must be visually reversed");
        for (int column = 0; column < 12; ++column) QCOMPARE(view.cellAt(view.cellRect(0, column).center()), QPoint(column, 0));
        view.selectAll();
        QVERIFY(view.selectedText().startsWith(text));
        QCOMPARE(view.plainText(), text);
    }
    void shapedImageAndMouseSelection() {
        QTemporaryDir directory;
        SettingsStore settings(directory.path()); settings.settings.copy_on_select = false; settings.settings.terminal_tint = 0;
        ShellProfile profile; profile.key = QStringLiteral("native");
        TerminalView view(&settings, profile, {}, nullptr, false); view.resize(850, 420); view.model().resize(60, 12);
        const QString sample = QString::fromUtf8("abc \xd8\xb3\xd9\x84\xd8\xa7\xd9\x85 \xd9\x84\xd8\xa7 \xd9\x85\xdb\x8c\xe2\x80\x8c\xd8\xb1\xd9\x88\xd9\x85 xyz \xf0\x9f\x98\x80");
        view.feedForTest(sample.toUtf8());
        QImage actual(view.size(), QImage::Format_ARGB32_Premultiplied); actual.fill(Qt::transparent);
        { QPainter painter(&actual); view.render(&painter, {}, {}, QWidget::DrawChildren); }
        QImage expected(view.size(), QImage::Format_ARGB32_Premultiplied); expected.fill(Qt::transparent);
        QFont font(settings.settings.font_family); font.setFamilies({settings.settings.font_family, QStringLiteral("Vazir Code Hack"), QStringLiteral("Cascadia Mono"), QStringLiteral("Segoe UI Emoji")});
        font.setPointSizeF(settings.settings.font_size); font.setStyleHint(QFont::Monospace);
        const QString expectedText = sample + QString(60 - sample.size(), u' ');
        QTextLayout layout(expectedText, font); QTextOption option; option.setWrapMode(QTextOption::NoWrap); option.setAlignment(Qt::AlignLeft); option.setTextDirection(Qt::LayoutDirectionAuto); layout.setTextOption(option);
        QTextCharFormat format; format.setForeground(QColor(230, 238, 251)); layout.setFormats({{0, int(expectedText.size()), format}});
        layout.beginLayout(); auto line = layout.createLine(); line.setLineWidth(10000000); line.setPosition({0, 0}); layout.endLayout();
        { QPainter painter(&expected); painter.setRenderHint(QPainter::TextAntialiasing); painter.setRenderHint(QPainter::Antialiasing); line.draw(&painter, {17, 14 + (view.cellRect(0, 0).height() - line.height()) / 2}); }
        const QRect crop(17, 14, int(view.cellRect(0, 0).width() * 40), int(view.cellRect(0, 0).height()));
        const QImage actualCrop = actual.copy(crop), expectedCrop = expected.copy(crop);
        auto ink = [](const QImage& image) {
            int count = 0;
            for (int y = 0; y < image.height(); ++y)
                for (int x = 0; x < image.width(); ++x)
                    if (qAlpha(image.pixel(x, y)) > 16) ++count;
            return count;
        };
        if (ink(actualCrop) == 0 || ink(expectedCrop) == 0) {
            actual.save(directory.filePath(QStringLiteral("actual.png")));
            expected.save(directory.filePath(QStringLiteral("expected.png")));
        }
        QVERIFY2(ink(actualCrop) > 0, "Shaped text did not render pixels.");
        QVERIFY2(ink(expectedCrop) > 0, "Expected layout did not render pixels.");
        view.model().reset(); view.feedForTest(QString::fromUtf8("abc \xd8\xb3\xd9\x84\xd8\xa7\xd9\x85 xyz").toUtf8());
        auto drag = [&](QPointF from, QPointF to) {
            QMouseEvent press(QEvent::MouseButtonPress, from, from, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier); QApplication::sendEvent(&view, &press);
            QMouseEvent move(QEvent::MouseMove, to, to, Qt::NoButton, Qt::LeftButton, Qt::NoModifier); QApplication::sendEvent(&view, &move);
            QMouseEvent release(QEvent::MouseButtonRelease, to, to, Qt::LeftButton, Qt::NoButton, Qt::NoModifier); QApplication::sendEvent(&view, &release);
        };
        const QPointF from = view.cellRect(0, 2).center(), to = view.cellRect(0, 6).center();
        const QString selected = QString::fromUtf8("c \xd8\xa7\xd9\x85");
        drag(from, to); QCOMPARE(view.selectedText(), selected);
        drag(to, from); QCOMPARE(view.selectedText(), selected);
    }
};

QTEST_MAIN(TerminalTests)
#include "terminal_tests.moc"
