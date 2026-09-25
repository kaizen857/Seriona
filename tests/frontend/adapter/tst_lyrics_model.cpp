#include "lyrics_model.h"

#include "seriona/control/control_contracts.h"

#include <QSignalSpy>
#include <QVariantList>
#include <QVariantMap>
#include <QtTest/QTest>

#include <chrono>

namespace {

seriona::control::SplitLyricLine makeSnapshotLine(std::chrono::milliseconds timestamp,
                                                  const std::string &text,
                                                  const std::string &original,
                                                  const std::string &translation)
{
    seriona::control::SplitLyricLine line;
    line.timestamp = timestamp;
    line.text = text;
    line.original = original;
    line.translation = translation;
    line.split = !translation.empty();
    return line;
}

seriona::control::TrackLyricsSnapshot makeSnapshot(
    const std::string &trackId,
    const std::vector<seriona::control::SplitLyricLine> &lines)
{
    seriona::control::TrackLyricsSnapshot snapshot;
    snapshot.trackId = trackId;
    snapshot.targetLanguage = "zh";
    snapshot.lines = lines;
    return snapshot;
}

QVariant modelValue(const Seriona::App::LyricsModel &model, int row, int role)
{
    return model.data(model.index(row, 0), role);
}

}

class LyricsModelTest : public QObject
{
    Q_OBJECT

private slots:
    void snapshotLinesDrivePlayback();
    void missingEmpty();
    void correctionRolesExposeAutoAndOverride();
    void correctionFieldsParticipateInDedup();
};

void LyricsModelTest::snapshotLinesDrivePlayback()
{
    Seriona::App::LyricsModel model;
    model.setShowTranslation(false);

    model.applyTrackLyricsSnapshot(makeSnapshot("selected-track", {
        makeSnapshotLine(std::chrono::milliseconds{0}, "Intro / 开场", "Intro", "开场"),
        makeSnapshotLine(std::chrono::milliseconds{5000}, "Verse / 主歌", "Verse", "主歌"),
        makeSnapshotLine(std::chrono::milliseconds{10000}, "Chorus / 副歌", "Chorus", "副歌")}));

    QCOMPARE(model.rowCount(), 3);
    QCOMPARE(model.showTranslation(), false);
    QCOMPARE(modelValue(model, 0, Seriona::App::LyricsModel::DisplayLineRole).toString(), QStringLiteral("Intro"));
    QCOMPARE(modelValue(model, 0, Seriona::App::LyricsModel::TranslationRole).toString(), QStringLiteral("开场"));
    QCOMPARE(modelValue(model, 0, Seriona::App::LyricsModel::RawLineRole).toString(), QStringLiteral("Intro / 开场"));

    model.setPlaybackPosition(6.0);
    QCOMPARE(model.currentIndex(), 1);
    QCOMPARE(modelValue(model, 1, Seriona::App::LyricsModel::CurrentRole).toBool(), true);
    QCOMPARE(modelValue(model, 1, Seriona::App::LyricsModel::DisplayLineRole).toString(), QStringLiteral("Verse"));

    model.setPlaybackPosition(11.0);
    QCOMPARE(model.currentIndex(), 2);
    model.selectLyric(0);
    QCOMPARE(model.currentIndex(), 0);
    model.setPlaybackPosition(6.5);
    QCOMPARE(model.currentIndex(), 1);

    model.applyTrackLyricsSnapshot(makeSnapshot("selected-track", {
        makeSnapshotLine(std::chrono::milliseconds{0}, "Plain one", "Plain one", ""),
        makeSnapshotLine(std::chrono::milliseconds{0}, "Plain two", "Plain two", "")}));
    model.setPlaybackPosition(90.0);
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.currentIndex(), 0);
}

void LyricsModelTest::missingEmpty()
{
    Seriona::App::LyricsModel model;
    model.setShowTranslation(false);

    model.applyTrackLyricsSnapshot(seriona::control::TrackLyricsSnapshot{});
    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(model.currentIndex(), 0);

    // 带曲目 + 有行：正常落地
    model.applyTrackLyricsSnapshot(makeSnapshot("selected-track", {
        makeSnapshotLine(std::chrono::milliseconds{0}, "Available / 可用", "Available", "可用")}));
    QCOMPARE(model.rowCount(), 1);

    // 空行回退快照（trackId 仍为当前曲目）⇒ 清空，不残留上一份内容
    model.applyTrackLyricsSnapshot(makeSnapshot("selected-track", {}));
    QCOMPARE(model.rowCount(), 0);

    model.selectLyric(12);
    model.setPlaybackPosition(20.0);
    model.toggleTranslation();
    QCOMPARE(model.currentIndex(), 0);
    QCOMPARE(model.showTranslation(), true);
}

void LyricsModelTest::correctionRolesExposeAutoAndOverride()
{
    Seriona::App::LyricsModel model;

    seriona::control::SplitLyricLine overridden;
    overridden.timestamp = std::chrono::milliseconds{0};
    overridden.text = "raw / line";
    overridden.original = "cur-orig";
    overridden.translation = "cur-tr";
    overridden.manualOverride = true;
    overridden.autoOriginal = "auto-orig";
    overridden.autoTranslation = "auto-tr";

    seriona::control::SplitLyricLine plain;
    plain.timestamp = std::chrono::milliseconds{1000};
    plain.text = "plain line";
    plain.original = "plain";
    plain.translation = "";
    plain.manualOverride = false;

    model.applyTrackLyricsSnapshot(makeSnapshot("correction-track", {overridden, plain}));

    // 新角色：manualOverride + 被覆盖【之前】的 auto 值
    QCOMPARE(modelValue(model, 0, Seriona::App::LyricsModel::ManualOverrideRole).toBool(), true);
    QCOMPARE(modelValue(model, 0, Seriona::App::LyricsModel::AutoOriginalRole).toString(), QStringLiteral("auto-orig"));
    QCOMPARE(modelValue(model, 0, Seriona::App::LyricsModel::AutoTranslationRole).toString(), QStringLiteral("auto-tr"));
    // 当前值与被覆盖前的自动值不同 —— 菜单第 4 项必须能区分这两者，故断言二者确实不同
    QVERIFY(modelValue(model, 0, Seriona::App::LyricsModel::AutoOriginalRole).toString()
            != modelValue(model, 0, Seriona::App::LyricsModel::DisplayLineRole).toString());
    QVERIFY(modelValue(model, 0, Seriona::App::LyricsModel::AutoTranslationRole).toString()
            != modelValue(model, 0, Seriona::App::LyricsModel::TranslationRole).toString());

    // 未命中 manual 的行：auto 为空，manualOverride 为 false
    QCOMPARE(modelValue(model, 1, Seriona::App::LyricsModel::ManualOverrideRole).toBool(), false);
    QCOMPARE(modelValue(model, 1, Seriona::App::LyricsModel::AutoOriginalRole).toString(), QString());

    // 既有 5 个角色名逐字不变，新角色只追加
    const QHash<int, QByteArray> roles = model.roleNames();
    QCOMPARE(roles.value(Seriona::App::LyricsModel::RawLineRole), QByteArray("rawLine"));
    QCOMPARE(roles.value(Seriona::App::LyricsModel::DisplayLineRole), QByteArray("displayLine"));
    QCOMPARE(roles.value(Seriona::App::LyricsModel::TranslationRole), QByteArray("translation"));
    QCOMPARE(roles.value(Seriona::App::LyricsModel::CurrentRole), QByteArray("isCurrent"));
    QCOMPARE(roles.value(Seriona::App::LyricsModel::TimestampRole), QByteArray("timestampSec"));
    QCOMPARE(roles.value(Seriona::App::LyricsModel::ManualOverrideRole), QByteArray("manualOverride"));
    QCOMPARE(roles.value(Seriona::App::LyricsModel::AutoOriginalRole), QByteArray("autoOriginal"));
    QCOMPARE(roles.value(Seriona::App::LyricsModel::AutoTranslationRole), QByteArray("autoTranslation"));

    // lines()：既有 3 键保留，新键追加且值正确（QML delegate/菜单数据来源）
    const QVariantList lines = model.lines();
    QCOMPARE(lines.size(), 2);
    const QVariantMap first = lines.at(0).toMap();
    QCOMPARE(first.value(QStringLiteral("displayLine")).toString(), QStringLiteral("cur-orig"));
    QCOMPARE(first.value(QStringLiteral("translation")).toString(), QStringLiteral("cur-tr"));
    QCOMPARE(first.value(QStringLiteral("timestampSec")).toDouble(), 0.0);
    QCOMPARE(first.value(QStringLiteral("rawLine")).toString(), QStringLiteral("raw / line"));
    QCOMPARE(first.value(QStringLiteral("manualOverride")).toBool(), true);
    QCOMPARE(first.value(QStringLiteral("autoOriginal")).toString(), QStringLiteral("auto-orig"));
    QCOMPARE(first.value(QStringLiteral("autoTranslation")).toString(), QStringLiteral("auto-tr"));
}

void LyricsModelTest::correctionFieldsParticipateInDedup()
{
    Seriona::App::LyricsModel model;
    QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);

    seriona::control::SplitLyricLine line;
    line.timestamp = std::chrono::milliseconds{0};
    line.text = "raw / line";
    line.original = "orig";
    line.translation = "";
    line.manualOverride = true;
    line.autoOriginal = "auto-orig-v1";
    line.autoTranslation = "auto-tr";

    model.applyTrackLyricsSnapshot(makeSnapshot("correction-track", {line}));
    QCOMPARE(model.rowCount(), 1);

    // 全等快照：去重命中，不重排模型
    resetSpy.clear();
    model.applyTrackLyricsSnapshot(makeSnapshot("correction-track", {line}));
    QCOMPARE(resetSpy.count(), 0);

    // 仅 autoOriginal 变化：必须被去重谓词识别为「不同」并重排
    // （若去重键漏掉 auto 字段，此断言失败——纠错/自动判定变化将不再刷新界面）
    line.autoOriginal = "auto-orig-v2";
    resetSpy.clear();
    model.applyTrackLyricsSnapshot(makeSnapshot("correction-track", {line}));
    QCOMPARE(resetSpy.count(), 1);
    QCOMPARE(modelValue(model, 0, Seriona::App::LyricsModel::AutoOriginalRole).toString(), QStringLiteral("auto-orig-v2"));

    // manualOverride 单独变化也必须重排
    line.manualOverride = false;
    resetSpy.clear();
    model.applyTrackLyricsSnapshot(makeSnapshot("correction-track", {line}));
    QCOMPARE(resetSpy.count(), 1);
}

QTEST_GUILESS_MAIN(LyricsModelTest)

#include "tst_lyrics_model.moc"
