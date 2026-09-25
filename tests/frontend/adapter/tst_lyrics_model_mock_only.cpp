#include "lyrics_model.h"

#include <QtTest/QTest>

namespace {

QVariant modelValue(const Seriona::App::LyricsModel &model, int row, int role)
{
    return model.data(model.index(row, 0), role);
}

}

// mock-only（SERIONA_BACKEND_SOURCE_DIR 显式置空，SERIONA_HAS_BACKEND=0）下没有后端
// 订阅源：歌词面板保持既有 QML 空态（rowCount()==0，不显示任何陈旧数据），且模型的
// 对外交互入口（selectLyric/setPlaybackPosition/toggleTranslation/lines）保持可用不崩。
class LyricsModelMockOnlyTest : public QObject
{
    Q_OBJECT

private slots:
    void staysEmptyAndInteractiveWithoutBackend();
};

void LyricsModelMockOnlyTest::staysEmptyAndInteractiveWithoutBackend()
{
    Seriona::App::LyricsModel model;
    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(model.currentIndex(), 0);
    QCOMPARE(model.playbackPosition(), 0.0);
    QVERIFY(model.lines().isEmpty());
    QCOMPARE(modelValue(model, 0, Seriona::App::LyricsModel::DisplayLineRole), QVariant());

    model.selectLyric(3);
    model.setPlaybackPosition(12.5);
    model.toggleTranslation();
    QCOMPARE(model.currentIndex(), 0);
    QCOMPARE(model.playbackPosition(), 12.5);
    QCOMPARE(model.showTranslation(), false);
}

QTEST_GUILESS_MAIN(LyricsModelMockOnlyTest)

#include "tst_lyrics_model_mock_only.moc"
