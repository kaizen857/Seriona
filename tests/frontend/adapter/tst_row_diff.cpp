// 行键差分引擎（row_diff.h）单元测试。
// 断言清单：
// 1. 空→n 单次 Insert；n→空单次 Remove；前/中/尾增删与连续段合并；
// 2. 不连续删除段按高位先发降序；
// 3. 相邻交换 1 次 Move；逆序 n-1 次 Move；全量替换 1 Remove + 1 Insert；
// 4. 逐操作重放（Remove/Insert/Move 顺序应用）必须复现 newKeys；
// 5. 固定种子随机序列重放性质（含增删移动混合）。

#include "row_diff.h"

#include <QtTest/QTest>

#include <algorithm>
#include <random>

using Seriona::App::RowOp;
using Seriona::App::diffRowKeys;

Q_DECLARE_METATYPE(RowOp)
Q_DECLARE_METATYPE(QVector<RowOp>)

namespace {

QVector<QString> keys(std::initializer_list<const char *> rawKeys)
{
    QVector<QString> result;
    result.reserve(static_cast<qsizetype>(rawKeys.size()));
    for (const char *key : rawKeys) {
        result.append(QString::fromLatin1(key));
    }
    return result;
}

RowOp removeOp(int first, int count)
{
    return RowOp{RowOp::Kind::Remove, first, count, 0};
}

RowOp insertOp(int first, int count)
{
    return RowOp{RowOp::Kind::Insert, first, count, 0};
}

RowOp moveOp(int first, int to)
{
    return RowOp{RowOp::Kind::Move, first, 1, to};
}

// 按 diffRowKeys 的语义逐操作重放：Remove 直接删；Insert 从 newKeys 取键插入；
// Move 用 QVector::move（与投影模型应用侧一致）。
QVector<QString> replay(const QVector<QString> &oldKeys, const QVector<QString> &newKeys, const QVector<RowOp> &ops)
{
    QVector<QString> live = oldKeys;
    for (const RowOp &op : ops) {
        switch (op.kind) {
        case RowOp::Kind::Remove:
            live.remove(op.first, op.count);
            break;
        case RowOp::Kind::Insert:
            for (int k = 0; k < op.count; ++k) {
                live.insert(op.first + k, newKeys.at(op.first + k));
            }
            break;
        case RowOp::Kind::Move:
            live.move(op.first, op.to);
            break;
        }
    }
    return live;
}

void compareOp(const RowOp &actual, const RowOp &expected)
{
    QCOMPARE(static_cast<int>(actual.kind), static_cast<int>(expected.kind));
    QCOMPARE(actual.first, expected.first);
    QCOMPARE(actual.count, expected.count);
    QCOMPARE(actual.to, expected.to);
}

} // namespace

class RowDiffTest : public QObject
{
    Q_OBJECT

private slots:
    void diffCases_data();
    void diffCases();
    void randomizedReplayReproducesNewKeys();
};

void RowDiffTest::diffCases_data()
{
    QTest::addColumn<QVector<QString>>("oldKeys");
    QTest::addColumn<QVector<QString>>("newKeys");
    QTest::addColumn<QVector<RowOp>>("expectedOps");

    QTest::newRow("identical") << keys({"a", "b", "c"}) << keys({"a", "b", "c"})
                               << QVector<RowOp>{};
    QTest::newRow("emptyToN") << QVector<QString>{} << keys({"a", "b", "c"})
                              << QVector<RowOp>{insertOp(0, 3)};
    QTest::newRow("nToEmpty") << keys({"a", "b", "c"}) << QVector<QString>{}
                              << QVector<RowOp>{removeOp(0, 3)};
    QTest::newRow("frontAddRun") << keys({"a", "b"}) << keys({"x", "y", "a", "b"})
                                 << QVector<RowOp>{insertOp(0, 2)};
    QTest::newRow("midAdd") << keys({"a", "b", "c"}) << keys({"a", "x", "b", "c"})
                            << QVector<RowOp>{insertOp(1, 1)};
    QTest::newRow("endAddRun") << keys({"a", "b"}) << keys({"a", "b", "x", "y"})
                               << QVector<RowOp>{insertOp(2, 2)};
    QTest::newRow("frontRemoveRun") << keys({"x", "y", "a", "b"}) << keys({"a", "b"})
                                    << QVector<RowOp>{removeOp(0, 2)};
    QTest::newRow("midRemove") << keys({"a", "x", "b", "c"}) << keys({"a", "b", "c"})
                               << QVector<RowOp>{removeOp(1, 1)};
    QTest::newRow("endRemoveRun") << keys({"a", "b", "x", "y"}) << keys({"a", "b"})
                                  << QVector<RowOp>{removeOp(2, 2)};
    QTest::newRow("nonContiguousRemovalRunsDescending")
        << keys({"a", "x", "b", "y", "c"}) << keys({"a", "b", "c"})
        << QVector<RowOp>{removeOp(3, 1), removeOp(1, 1)};
    QTest::newRow("adjacentSwap") << keys({"a", "b"}) << keys({"b", "a"})
                                  << QVector<RowOp>{moveOp(1, 0)};
    QTest::newRow("reversal") << keys({"a", "b", "c", "d"}) << keys({"d", "c", "b", "a"})
                              << QVector<RowOp>{moveOp(3, 0), moveOp(3, 1), moveOp(3, 2)};
    QTest::newRow("fullReplace") << keys({"a", "b"}) << keys({"c", "d"})
                                 << QVector<RowOp>{removeOp(0, 2), insertOp(0, 2)};
    QTest::newRow("moveWithRemoval") << keys({"a", "b", "c", "d"}) << keys({"d", "a", "c"})
                                     << QVector<RowOp>{removeOp(1, 1), moveOp(2, 0)};
    QTest::newRow("insertAfterRemovalRun") << keys({"a", "x", "b"}) << keys({"b", "y"})
                                           << QVector<RowOp>{removeOp(0, 2), insertOp(1, 1)};
    QTest::newRow("insertThenMove") << keys({"a", "b", "c", "d"}) << keys({"b", "x", "c", "a", "d"})
                                    << QVector<RowOp>{moveOp(1, 0), insertOp(1, 1), moveOp(3, 2)};
}

void RowDiffTest::diffCases()
{
    QFETCH(QVector<QString>, oldKeys);
    QFETCH(QVector<QString>, newKeys);
    QFETCH(QVector<RowOp>, expectedOps);

    const QVector<RowOp> ops = diffRowKeys(oldKeys, newKeys);
    QCOMPARE(ops.size(), expectedOps.size());
    for (int i = 0; i < ops.size(); ++i) {
        compareOp(ops.at(i), expectedOps.at(i));
    }

    // 重放性质：按序应用操作后必须逐键等于 newKeys。
    QCOMPARE(replay(oldKeys, newKeys, ops), newKeys);
}

void RowDiffTest::randomizedReplayReproducesNewKeys()
{
    // 固定种子：同一目录集合做随机重排 + 随机子集删除 + 新键前插，
    // 逐例断言重放复现 newKeys（操作坐标在旧序列坐标上必须自洽）。
    std::mt19937 generator(20260911u);
    for (int round = 0; round < 200; ++round) {
        const int oldCount = 1 + static_cast<int>(generator() % 40);
        QVector<QString> oldKeys;
        oldKeys.reserve(oldCount);
        for (int i = 0; i < oldCount; ++i) {
            oldKeys.append(QStringLiteral("k%1").arg(i));
        }

        // 随机删除一个子集，再随机插入若干新键，最后整体随机打乱。
        QVector<QString> newKeys;
        for (const QString &key : oldKeys) {
            if (generator() % 4 != 0) {
                newKeys.append(key);
            }
        }
        const int insertedCount = static_cast<int>(generator() % 5);
        for (int i = 0; i < insertedCount; ++i) {
            newKeys.append(QStringLiteral("new%1_%2").arg(round).arg(i));
        }
        std::shuffle(newKeys.begin(), newKeys.end(), generator);

        const QVector<RowOp> ops = diffRowKeys(oldKeys, newKeys);
        QCOMPARE(replay(oldKeys, newKeys, ops), newKeys);
    }
}

QTEST_GUILESS_MAIN(RowDiffTest)

#include "tst_row_diff.moc"
