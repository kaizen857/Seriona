#include "lyric_split_boundary.h"

#include <QTest>

namespace {

using Seriona::App::LyricSplitBoundaryCut;
using Seriona::App::LyricSplitBoundaryParts;
using Seriona::App::boundaryDiffersFromCurrent;
using Seriona::App::findLyricSplitBoundaryCut;
using Seriona::App::lyricOriginalIsSubmittable;
using Seriona::App::lyricSplitBoundaryShouldCommit;
using Seriona::App::splitLyricLineAtBoundary;

const QString kRawLine = QStringLiteral("揺るぎない Spirit / 坚定不移的Spirit");

} // namespace

class LyricSplitBoundaryTest : public QObject
{
    Q_OBJECT

private slots:
    void splitsAtBoundaryWithTrim();
    void clampsOutOfRangeBoundary();
    void rejectsEmptyOriginal();
    void differsFromCurrentOnlyOnRealChange();
    void initialCutReproducesDisplayedPair();
    void spacedPairIsNotReproducibleBySingleIndex();
    void initialCutRejectsPairThatNoBoundaryExpresses();
    void commitGateRequiresRealChangeAndNonBlankOriginal();
};

// 拖动分界落在分隔符之前 → 提交的原文/译文必须等于该位置原样切出的两段（含两侧 trim）。
// 分隔符字形随分界落点归属某一侧（纯原样切片，不做按分隔符的再切分）。
// 变异（切点偏 1 个字符）会改变该归属，使本用例判负（证据记录）。
void LyricSplitBoundaryTest::splitsAtBoundaryWithTrim()
{
    const int separator = kRawLine.indexOf(QStringLiteral(" / "));
    QVERIFY(separator > 0);

    // 分界落在分隔符之前：原文干净，译文带分隔符。
    const LyricSplitBoundaryParts beforeSeparator = splitLyricLineAtBoundary(kRawLine, separator);
    QVERIFY(beforeSeparator.valid);
    QCOMPARE(beforeSeparator.original, QStringLiteral("揺るぎない Spirit"));
    QCOMPARE(beforeSeparator.translation, QStringLiteral("/ 坚定不移的Spirit"));

    // 分界落在分隔符之后：译文干净，原文带分隔符。
    const LyricSplitBoundaryParts afterSeparator = splitLyricLineAtBoundary(kRawLine, separator + 3);
    QVERIFY(afterSeparator.valid);
    QCOMPARE(afterSeparator.original, QStringLiteral("揺るぎない Spirit /"));
    QCOMPARE(afterSeparator.translation, QStringLiteral("坚定不移的Spirit"));

    // 两侧 trim：前导/尾随空白都不得留在任一段里。
    const QString padded = QStringLiteral("  abc  /  def  ");
    const LyricSplitBoundaryParts trimmed = splitLyricLineAtBoundary(padded, padded.indexOf(QStringLiteral("  /  ")) + 2);
    QVERIFY(trimmed.valid);
    QCOMPARE(trimmed.original, QStringLiteral("abc"));
    QCOMPARE(trimmed.translation, QStringLiteral("/  def"));
}

void LyricSplitBoundaryTest::clampsOutOfRangeBoundary()
{
    const QString raw = QStringLiteral("abc / def");

    const LyricSplitBoundaryParts atEnd = splitLyricLineAtBoundary(raw, 999);
    QVERIFY(atEnd.valid);
    QCOMPARE(atEnd.original, raw);
    QCOMPARE(atEnd.translation, QString());

    const LyricSplitBoundaryParts atStart = splitLyricLineAtBoundary(raw, -5);
    QCOMPARE(atStart.valid, false);
    QCOMPARE(atStart.original, QString());
    QCOMPARE(atStart.translation, raw);
}

void LyricSplitBoundaryTest::rejectsEmptyOriginal()
{
    const QString raw = QStringLiteral("  / x");
    QCOMPARE(splitLyricLineAtBoundary(raw, 0).valid, false);
    QCOMPARE(splitLyricLineAtBoundary(raw, 2).valid, false);
    QCOMPARE(splitLyricLineAtBoundary(raw, 2).translation, QStringLiteral("/ x"));
}

void LyricSplitBoundaryTest::differsFromCurrentOnlyOnRealChange()
{
    const int cut = kRawLine.indexOf(QStringLiteral(" / "));
    const LyricSplitBoundaryParts parts = splitLyricLineAtBoundary(kRawLine, cut);
    QVERIFY(parts.valid);
    QCOMPARE(boundaryDiffersFromCurrent(parts, parts.original, parts.translation), false);
    QCOMPARE(boundaryDiffersFromCurrent(parts, parts.original, QStringLiteral("旧译")), true);
    QCOMPARE(boundaryDiffersFromCurrent(parts, QStringLiteral("旧原文"), parts.translation), true);
}

// 窗口打开时的初始分界必须复现当前展示对：否则「选中一行、不拖动、直接保存」会写出
// 一个与当前显示不同的 manual 值（D33-1 缺陷）。两种 convention 各验一遍。
void LyricSplitBoundaryTest::initialCutReproducesDisplayedPair()
{
    struct Sample {
        const char *raw;
        const char *original;
        const char *translation;
    };
    const Sample samples[] = {
        {"揺るぎない Spirit / 坚定不移的Spirit", "揺るぎない Spirit", "坚定不移的Spirit"},
        {"原文/译文", "原文", "译文"},
        {"原文 ／ 译文", "原文", "译文"},
        {"日文 中文", "日文", "中文"},
    };

    for (const Sample &sample : samples) {
        const QString raw = QString::fromUtf8(sample.raw);
        const QString original = QString::fromUtf8(sample.original);
        const QString translation = QString::fromUtf8(sample.translation);

        const LyricSplitBoundaryCut cut = findLyricSplitBoundaryCut(raw, original, translation);
        QVERIFY2(cut.valid, sample.raw);
        QVERIFY(cut.leftEnd <= cut.rightStart);

        const LyricSplitBoundaryParts parts = splitLyricLineAtBoundary(raw, cut);
        QCOMPARE(parts.original, original);
        QCOMPARE(parts.translation, translation);
        QVERIFY(parts.valid);
        QCOMPARE(boundaryDiffersFromCurrent(parts, original, translation), false);
    }
}

// 「初始分界 = 原文结束处」这一单点写法对含分隔符的展示对不成立（后端把分隔符吃掉了），
// 故初始分界用「跳过被吃掉那一段」的分界表达。本用例是上面用例为何需要 gap 的证据。
void LyricSplitBoundaryTest::spacedPairIsNotReproducibleBySingleIndex()
{
    const QString original = QStringLiteral("揺るぎない Spirit");
    const QString translation = QStringLiteral("坚定不移的Spirit");

    int reproducingCount = 0;
    for (int i = 0; i <= kRawLine.size(); ++i) {
        const LyricSplitBoundaryParts parts = splitLyricLineAtBoundary(kRawLine, i);
        if (parts.original == original && parts.translation == translation)
            ++reproducingCount;
    }
    QCOMPARE(reproducingCount, 0);
}

void LyricSplitBoundaryTest::initialCutRejectsPairThatNoBoundaryExpresses()
{
    QCOMPARE(findLyricSplitBoundaryCut(kRawLine, QString(), QStringLiteral("x")).valid, false);
    QCOMPARE(findLyricSplitBoundaryCut(kRawLine, QStringLiteral("不存在的原文"), QStringLiteral("x")).valid, false);
    QCOMPARE(findLyricSplitBoundaryCut(kRawLine, QStringLiteral("揺るぎない Spirit"), QStringLiteral("不存在的译文")).valid, false);
    QCOMPARE(findLyricSplitBoundaryCut(QString(), QStringLiteral("a"), QStringLiteral("b")).valid, false);
}

// 提交门的唯一生产谓词（A3）：既要求原文非空，又要求构成真实变更。
// 本用例对三个方向都可判负 —— 无操作拒绝、真实变更放行、空原文拒绝；
// 若把谓词改成恒真/恒假或漏掉任一条件，至少一个 QCOMPARE 失败。
void LyricSplitBoundaryTest::commitGateRequiresRealChangeAndNonBlankOriginal()
{
    // 原文可提交性：空串 / 全空白都不可提交，非空可提交。
    QCOMPARE(lyricOriginalIsSubmittable(QString()), false);
    QCOMPARE(lyricOriginalIsSubmittable(QStringLiteral("   ")), false);
    QCOMPARE(lyricOriginalIsSubmittable(QStringLiteral("\t\n")), false);
    QCOMPARE(lyricOriginalIsSubmittable(QStringLiteral("原文")), true);

    const QString rawLine = QStringLiteral("日文 中文");
    const QString original = QStringLiteral("日文");
    const QString translation = QStringLiteral("中文");
    const int boundaryIndex = findLyricSplitBoundaryCut(rawLine, original, translation).leftEnd;
    const LyricSplitBoundaryParts parts = splitLyricLineAtBoundary(rawLine, boundaryIndex);
    QVERIFY(parts.valid);

    // 与当前值相同 → 无操作，不提交。
    QCOMPARE(lyricSplitBoundaryShouldCommit(parts, original, translation), false);
    // 当前值不同 → 真实变更，提交。
    QCOMPARE(lyricSplitBoundaryShouldCommit(parts, original, QStringLiteral("旧译")), true);
    QCOMPARE(lyricSplitBoundaryShouldCommit(parts, QStringLiteral("旧原文"), translation), true);

    // 原文为空的切点 → valid=false，门谓词必须拒绝（即使「当前值」不同）。
    const LyricSplitBoundaryParts blank =
        splitLyricLineAtBoundary(QStringLiteral("  / 中文"), 0);
    QCOMPARE(blank.valid, false);
    QCOMPARE(lyricSplitBoundaryShouldCommit(blank, QStringLiteral("任意"), QStringLiteral("任意")), false);
}

QTEST_GUILESS_MAIN(LyricSplitBoundaryTest)

#include "tst_lyric_split_boundary.moc"
