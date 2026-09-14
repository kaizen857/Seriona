// 关于界面 overlay（T19）组件测试：加载 qml/components/AboutOverlay.qml，
// 断言初始不可见、open()/close() 的可见性切换、closePolicy 含 Esc/点击遮罩关闭。
// Esc 键关闭由 closePolicy 的 CloseOnEscape 位覆盖（offscreen 平台无窗口激活
// 语义，QTest::keyClick 无法触发 QQuickShortcut，故不做按键注入测试）。
//
// 依赖构建目录的 Seriona QML 模块产物（build/Seriona/qmldir），
// 因此 CMake 侧对测试目标 add_dependencies(Seriona) 保证产物先就绪。
#include <QGuiApplication>
#include <QQuickItem>
#include <QQuickWindow>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QSignalSpy>
#include <QTest>
#include <QWindow>

namespace {

constexpr auto kEscape = 0x10;          // QQuickPopup::CloseOnEscape
constexpr auto kPressOutside = 0x01;    // QQuickPopup::CloseOnPressOutside（Qt 6.11 枚举值）

// 内联宿主窗口：AboutOverlay 是 Popup，必须挂在一个可见 Window 的场景中
// 才能完成 open()/close() 的可见性切换。
constexpr auto kHostQml = R"(
import QtQuick
import QtQuick.Window
import Seriona

Window {
    id: testWindow
    width: 800
    height: 600
    visible: true

    AboutOverlay {
        id: aboutOverlay
        objectName: "aboutOverlay"
    }
}
)";

} // namespace

class AboutOverlayTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void initiallyHidden();
    void openShowsOverlay();
    void closeHidesOverlay();
    void closePolicyAllowsEscapeAndPressOutside();
    void versionLabelShowsApplicationVersion();

private:
    QQuickWindow *window = nullptr;
    // Popup 在 Qt Quick Templates 2 中并非 QQuickItem 子类（QQuickPopup
    // 继承 QObject），故以 QObject* 持有并用元对象接口访问 visible/closePolicy。
    QObject *overlay = nullptr;
    QQmlApplicationEngine engine;

    bool overlayVisible() const;
};

void AboutOverlayTest::initTestCase()
{
    // 版本胶囊读取 Qt.application.version（与 main.cpp 的 setApplicationVersion 同源）。
    QCoreApplication::setApplicationVersion(QStringLiteral("9.9.9"));
    engine.addImportPath(QCoreApplication::applicationDirPath());
    engine.loadData(QByteArray(kHostQml), QUrl(QStringLiteral("qrc:/seriona_about_overlay_test.qml")));

    QVERIFY2(!engine.rootObjects().isEmpty(), "host Window failed to load");
    window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    QVERIFY2(window != nullptr, "host root is not a QQuickWindow");
    QVERIFY2(window->isVisible(), "host window is not visible");

    overlay = engine.rootObjects().first()->findChild<QObject *>(QStringLiteral("aboutOverlay"));
    QVERIFY2(overlay != nullptr, "AboutOverlay instance not found");
}

bool AboutOverlayTest::overlayVisible() const
{
    return overlay->property("visible").toBool();
}

void AboutOverlayTest::initiallyHidden()
{
    QCOMPARE(overlayVisible(), false);
}

void AboutOverlayTest::openShowsOverlay()
{
    QVERIFY(QMetaObject::invokeMethod(overlay, "open"));
    QTRY_VERIFY(overlayVisible());
    QVERIFY(QMetaObject::invokeMethod(overlay, "close"));
    QTRY_VERIFY(!overlayVisible());
}

void AboutOverlayTest::closeHidesOverlay()
{
    QVERIFY(QMetaObject::invokeMethod(overlay, "open"));
    QTRY_VERIFY(overlayVisible());
    QVERIFY(QMetaObject::invokeMethod(overlay, "close"));
    QTRY_VERIFY(!overlayVisible());
}

void AboutOverlayTest::closePolicyAllowsEscapeAndPressOutside()
{
    const int policy = overlay->property("closePolicy").toInt();
    QVERIFY2((policy & kEscape) != 0, "closePolicy misses CloseOnEscape");
    QVERIFY2((policy & kPressOutside) != 0, "closePolicy misses CloseOnPressOutside");
}

void AboutOverlayTest::versionLabelShowsApplicationVersion()
{
    QObject *label = overlay->findChild<QObject *>(QStringLiteral("aboutVersionLabel"));
    QVERIFY2(label != nullptr, "aboutVersionLabel not found");
    const QString text = label->property("text").toString();
    QVERIFY2(text.contains(QStringLiteral("v9.9.9")), qPrintable(text));
}

QTEST_MAIN(AboutOverlayTest)

#include "tst_about_overlay.moc"
