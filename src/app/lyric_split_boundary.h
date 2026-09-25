#pragma once

#include <QString>

namespace Seriona::App {

// 整首纠错窗口（D23）拖动分界时的纯换算结果。
// 前端不直连 DB/后端：本结构只描述「把清洗后的原始行在某处切开」这件事，
// 提交仍走 AppFacade → BackendBridge 的控制命令链。
struct LyricSplitBoundaryParts {
    QString original;
    QString translation;
    // false = 该分界切出的原文为空（含全空白）——用户无法解释的位置，不得提交。
    bool valid = false;
};

// 分界落点：原文 = raw[0:leftEnd]，译文 = raw[rightStart:]（两侧 trimmed）。
// 拖动手势产出的是单点分界（leftEnd == rightStart）；窗口打开时为了复现当前展示对，
// leftEnd 与 rightStart 之间可以留出一段被跳过的运行段 —— 该段是【由当前展示对反解】
// 出来的（见 findLyricSplitBoundaryCut），不是任何硬编码的分隔符字形。
struct LyricSplitBoundaryCut {
    int leftEnd = 0;
    int rightStart = 0;
    bool valid = false;
};

// 拖动分界与行级右键「修正原文/译文」弹窗的**同一口径**：原文为空（含全空白）
// 不可提交，两条路都必须经此判定，避免口径分裂。
[[nodiscard]] inline bool lyricOriginalIsSubmittable(const QString &original)
{
    return !original.trimmed().isEmpty();
}

// 清洗口径假设（证据 observability-choice.md 记录此假设，不假装与后端逐字节相同）：
// 后端自动切分用 Python `str.strip()` 家族（`trimEdges` 做 `rstrip(left)` + `lstrip(right)`）。
// Qt 的 `trimmed()` 同样剥离 Unicode 空白，故空白边界上两侧一致；本函数额外剥离原文
// 侧的前导空白作为手工纠错的归一化（后端保留它）。这是**用户手工**分界，不是重新实现
// 自动切分，前端不得据此重引入任何按分隔符切分的逻辑。
//
// 索引是 Qt UTF-16 码元索引；越界钳到合法区间。纯函数：不触碰后端、DB、文件系统。
[[nodiscard]] inline LyricSplitBoundaryParts splitLyricLineAtBoundary(const QString &rawLine,
                                                                     const LyricSplitBoundaryCut &cut)
{
    const int leftEnd = qBound(0, cut.leftEnd, rawLine.size());
    const int rightStart = qBound(leftEnd, cut.rightStart, rawLine.size());

    LyricSplitBoundaryParts parts;
    parts.original = rawLine.left(leftEnd).trimmed();
    parts.translation = rawLine.mid(rightStart).trimmed();
    // 原文为空（含全空白）的分界对用户不可解释：不做「只留译文」的写入。
    parts.valid = lyricOriginalIsSubmittable(parts.original);
    return parts;
}

[[nodiscard]] inline LyricSplitBoundaryParts splitLyricLineAtBoundary(const QString &rawLine,
                                                                     int boundaryIndex)
{
    const int clamped = qBound(0, boundaryIndex, rawLine.size());
    return splitLyricLineAtBoundary(rawLine, LyricSplitBoundaryCut{clamped, clamped, true});
}

// 反解「能复现当前展示对 (original, translation)」的分界：在 raw 中定位原文本体与
// 译文本体，两者之间被后端吃掉的那段即跳过的运行段。是否真的复现，只用
// splitLyricLineAtBoundary 的结果逐字比较来自证，全程不出现任何分隔符字形。
// valid == false 表示该行当前展示对无法由分界表达（例如原文为空、原文/译文不在 raw 中）
// —— 调用方不得据它提交。
[[nodiscard]] inline LyricSplitBoundaryCut findLyricSplitBoundaryCut(const QString &rawLine,
                                                                    const QString &original,
                                                                    const QString &translation)
{
    LyricSplitBoundaryCut cut;
    if (rawLine.isEmpty() || original.isEmpty()) {
        return cut;
    }

    const int originalAt = rawLine.indexOf(original);
    if (originalAt < 0) {
        return cut;
    }
    const int leftEnd = originalAt + original.size();
    if (leftEnd > rawLine.size()) {
        return cut;
    }

    int rightStart = rawLine.size();
    if (!translation.isEmpty()) {
        const int translationAt = rawLine.indexOf(translation, leftEnd);
        if (translationAt < 0) {
            return cut;
        }
        rightStart = translationAt;
    }

    cut.leftEnd = leftEnd;
    cut.rightStart = rightStart;
    const LyricSplitBoundaryParts parts = splitLyricLineAtBoundary(rawLine, cut);
    cut.valid = parts.valid && parts.original == original && parts.translation == translation;
    return cut;
}

// 该分界是否构成一次有效变更：原文与译文与当前展示值不同。
// 相同即为无操作——调用方不得发出命令。
[[nodiscard]] inline bool boundaryDiffersFromCurrent(const LyricSplitBoundaryParts &parts,
                                                     const QString &currentOriginal,
                                                     const QString &currentTranslation)
{
    return parts.original != currentOriginal || parts.translation != currentTranslation;
}

// 提交门的唯一生产谓词（A3 修复点）：既要求可解释（原文非空），又要求构成真实变更。
// AppFacade::commitLyricSplitBoundary 是它的薄封装；测试直接调用本谓词即可判负，
// 不再需要用恒真的 boundaryDiffersFromCurrent(parts, parts.original, parts.translation)。
[[nodiscard]] inline bool lyricSplitBoundaryShouldCommit(const LyricSplitBoundaryParts &parts,
                                                        const QString &currentOriginal,
                                                        const QString &currentTranslation)
{
    return parts.valid && boundaryDiffersFromCurrent(parts, currentOriginal, currentTranslation);
}

} // namespace Seriona::App
