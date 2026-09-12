#pragma once

#include <QHash>
#include <QSet>
#include <QString>
#include <QVector>

#include <QtGlobal>

namespace Seriona::App {

// 行键差分引擎（header-only）：由旧行键序列与新行键序列计算一组"按序应用即可把
// 旧序列变为新序列"的行操作，供 LibraryFolderProjectionModel 增量重建使用
// （只发 rowsInserted / rowsRemoved / rowsMoved / dataChanged，绝不 reset 或
// no-hint layoutChanged）。
//
// 操作语义（Qt / QVector 坐标约定）：
//   Remove{first,count}  删除旧序列坐标 [first, first+count) 的行；
//                        多个不连续删除段按"高位段先发"降序排列，
//                        坐标保持原始旧序列值（先删高位不影响低位坐标）。
//   Insert{first,count}  在坐标 first 处连续插入 newKeys[first, first+count) 行。
//   Move{first,to}       把当前序列坐标 first 处的行移动到坐标 to 处
//                        （from > to 的向左移动；配合 beginMoveRows 目标坐标
//                        src < dest ? dest + 1 : dest 的通用公式，此时 dest == to）。
//
// 放置算法：先删除应移除段，再从前往后扫描新序列维护"模拟序列 + 键→坐标"映射。
// 前缀 [0, i) 已处于最终位置，因此：
//   - 键不在模拟序列（全新键）→ 并入连续缺失段，段结束时补一次 Insert；
//   - 键在模拟序列且坐标 c != i → Move{c, i}；由前缀已放置性质必有 c >= i。
//
// 复杂度：
//   - 删除段扫描：O(n)（n = 旧键数）；
//   - 放置扫描：O(m) 次键查找 + O(Σ 位移距离) 次映射更新（m = 新键数；
//     典型曲库刷新移动/插入少，最坏 O(n·m)）；
//   - 空间：O(n + m)。
//   键必须唯一（Q_ASSERT 校验），最终模拟序列必须等于 newKeys（Q_ASSERT 校验）。
struct RowOp
{
    enum class Kind { Remove, Insert, Move };

    Kind kind = Kind::Remove;
    int first = 0;
    int count = 1;
    int to = 0;
};

inline QVector<RowOp> diffRowKeys(const QVector<QString> &oldKeys, const QVector<QString> &newKeys)
{
    // 键唯一是协议前提：投影构建阶段已按 nodeId 去重。
    QSet<QString> seenKeys;
    seenKeys.reserve(oldKeys.size());
    for (const QString &key : oldKeys) {
        Q_ASSERT(!seenKeys.contains(key));
        seenKeys.insert(key);
    }
    seenKeys.clear();
    seenKeys.reserve(newKeys.size());
    for (const QString &key : newKeys) {
        Q_ASSERT(!seenKeys.contains(key));
        seenKeys.insert(key);
    }

    QVector<RowOp> ops;

    // 1) 删除段：扫描旧序列，把连续"不在新键集"的位置合并成一段，按高位段先发。
    QSet<QString> newKeySet;
    newKeySet.reserve(newKeys.size());
    for (const QString &key : newKeys) {
        newKeySet.insert(key);
    }

    QVector<RowOp> removals;
    int runStart = -1;
    for (int i = 0; i <= oldKeys.size(); ++i) {
        const bool absent = i < oldKeys.size() && !newKeySet.contains(oldKeys.at(i));
        if (absent) {
            if (runStart < 0) {
                runStart = i;
            }
            continue;
        }
        if (runStart >= 0) {
            removals.append(RowOp{RowOp::Kind::Remove, runStart, i - runStart, 0});
            runStart = -1;
        }
    }
    for (auto it = removals.crbegin(); it != removals.crend(); ++it) {
        ops.append(*it);
    }

    // 2) 模拟序列：删除段应用后的存活键（保持旧序），并建立键→坐标映射。
    QVector<QString> live;
    live.reserve(oldKeys.size());
    for (const QString &key : oldKeys) {
        if (newKeySet.contains(key)) {
            live.append(key);
        }
    }

    QHash<QString, int> liveIndex;
    liveIndex.reserve(live.size());
    for (int i = 0; i < live.size(); ++i) {
        liveIndex.insert(live.at(i), i);
    }

    // 3) 放置扫描：前缀 [0, i) 已处于最终位置。
    int i = 0;
    while (i < newKeys.size()) {
        if (!liveIndex.contains(newKeys.at(i))) {
            // 连续缺失段 = 一次 Insert；段起点即插入坐标（前缀已放置）。
            const int insertAt = i;
            while (i < newKeys.size() && !liveIndex.contains(newKeys.at(i))) {
                ++i;
            }
            const int runCount = i - insertAt;
            ops.append(RowOp{RowOp::Kind::Insert, insertAt, runCount, 0});
            for (int k = 0; k < runCount; ++k) {
                live.insert(insertAt + k, newKeys.at(insertAt + k));
            }
            for (int j = insertAt + runCount; j < live.size(); ++j) {
                liveIndex.insert(live.at(j), j);
            }
            continue;
        }

        const int current = liveIndex.value(newKeys.at(i));
        Q_ASSERT(current >= i);
        if (current != i) {
            ops.append(RowOp{RowOp::Kind::Move, current, 1, i});
            live.move(current, i);
            const int first = qMin(current, i);
            const int last = qMax(current, i);
            for (int j = first; j <= last; ++j) {
                liveIndex.insert(live.at(j), j);
            }
        }
        ++i;
    }

    Q_ASSERT(live == newKeys);
    return ops;
}

}
