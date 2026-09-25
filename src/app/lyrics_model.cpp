#include "lyrics_model.h"

#include <QtMath>

#include <cmath>
#include <utility>

namespace Seriona::App {

namespace {

#if SERIONA_HAS_BACKEND
QString fromBackendString(const std::string &value)
{
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}
#endif

}

LyricsModel::LyricsModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int LyricsModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }

    return m_lines.size();
}

QVariant LyricsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_lines.size()) {
        return {};
    }

    const Line &line = m_lines.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case DisplayLineRole:
        // 原文直接取后端快照的 original；前端不做任何切分
        return line.original;
    case RawLineRole:
        return line.text;
    case TranslationRole:
        // 译文直接取后端快照的 translation（空串 = 此行无译文）
        return line.translation;
    case CurrentRole:
        return index.row() == m_currentIndex;
    case LyricsModel::TimestampRole:
        return line.timestamp.count() / 1000.0;
    case ManualOverrideRole:
        return line.manualOverride;
    case AutoOriginalRole:
        // 被 manual 覆盖【之前】的自动判定原文；未命中 manual 的行后端留空
        return line.autoOriginal;
    case AutoTranslationRole:
        // 被 manual 覆盖【之前】的自动判定译文；未命中 manual 的行后端留空
        return line.autoTranslation;
    default:
        return {};
    }
}

QHash<int, QByteArray> LyricsModel::roleNames() const
{
    return {{RawLineRole, "rawLine"},
            {DisplayLineRole, "displayLine"},
            {TranslationRole, "translation"},
            {CurrentRole, "isCurrent"},
            {LyricsModel::TimestampRole, "timestampSec"},
            {ManualOverrideRole, "manualOverride"},
            {AutoOriginalRole, "autoOriginal"},
            {AutoTranslationRole, "autoTranslation"}};
}

int LyricsModel::currentIndex() const
{
    return m_currentIndex;
}

void LyricsModel::setCurrentIndex(int index)
{
    if (m_lines.isEmpty()) {
        index = 0;
    } else if (index < 0) {
        index = 0;
    } else if (index >= m_lines.size()) {
        index = m_lines.size() - 1;
    }

    if (m_currentIndex == index) {
        return;
    }

    const int previousIndex = m_currentIndex;
    m_currentIndex = index;
    emitCurrentRoleChanged(previousIndex);
    emitCurrentRoleChanged(m_currentIndex);
    emit currentIndexChanged();
}

qreal LyricsModel::playbackPosition() const
{
    return m_playbackPosition;
}

void LyricsModel::setPlaybackPosition(qreal position)
{
    const qreal normalizedPosition = std::isfinite(position) ? qMax(0.0, position) : 0.0;
    if (qAbs(m_playbackPosition - normalizedPosition) < 0.001) {
        return;
    }

    m_playbackPosition = normalizedPosition;
    emit playbackPositionChanged();
    syncCurrentIndexToPlaybackPosition();
}

bool LyricsModel::showTranslation() const
{
    return m_showTranslation;
}

void LyricsModel::setShowTranslation(bool showTranslation)
{
    if (m_showTranslation == showTranslation) {
        return;
    }

    m_showTranslation = showTranslation;
    emit showTranslationChanged();
}

#if SERIONA_HAS_BACKEND
void LyricsModel::applyTrackLyricsSnapshot(const seriona::control::TrackLyricsSnapshot &snapshot)
{
    // 空行快照（后端在无当前曲目/曲目不在树中/清洗后无正文行/store 失败时发布的权威回退结论）
    // 与空 trackId 快照都整份清空，使订阅面不保留上一首/上次的行。
    if (snapshot.lines.empty() || fromBackendString(snapshot.trackId).isEmpty()) {
        clearLyrics();
        return;
    }

    QVector<Line> lines;
    lines.reserve(static_cast<qsizetype>(snapshot.lines.size()));
    bool hasTimedLyrics = false;
    for (const seriona::control::SplitLyricLine &line : snapshot.lines) {
        lines.append(Line{line.timestamp,
                          fromBackendString(line.text),
                          fromBackendString(line.original),
                          fromBackendString(line.translation),
                          line.manualOverride,
                          fromBackendString(line.autoOriginal),
                          fromBackendString(line.autoTranslation)});
        hasTimedLyrics = hasTimedLyrics || line.timestamp.count() > 0;
    }

    replaceLyrics(std::move(lines), hasTimedLyrics);
}
#endif

void LyricsModel::selectLyric(int index)
{
    setCurrentIndex(index);
}

void LyricsModel::toggleTranslation()
{
    setShowTranslation(!m_showTranslation);
}

QVariantList LyricsModel::lines() const
{
    QVariantList out;
    out.reserve(m_lines.size());
    for (const auto &line : m_lines) {
        QVariantMap entry;
        entry.insert(QStringLiteral("displayLine"), line.original);
        entry.insert(QStringLiteral("translation"), line.translation);
        entry.insert(QStringLiteral("timestampSec"), line.timestamp.count() / 1000.0);
        entry.insert(QStringLiteral("rawLine"), line.text);
        entry.insert(QStringLiteral("manualOverride"), line.manualOverride);
        entry.insert(QStringLiteral("autoOriginal"), line.autoOriginal);
        entry.insert(QStringLiteral("autoTranslation"), line.autoTranslation);
        out.append(entry);
    }
    return out;
}

void LyricsModel::clearLyrics()
{
    replaceLyrics({}, false);
}

void LyricsModel::replaceLyrics(QVector<Line> lines, bool hasTimedLyrics)
{
    bool sameLyrics = m_hasTimedLyrics == hasTimedLyrics && m_lines.size() == lines.size();
    for (qsizetype i = 0; sameLyrics && i < m_lines.size(); ++i) {
        const Line &current = m_lines.at(i);
        const Line &incoming = lines.at(i);
        sameLyrics = current.timestamp == incoming.timestamp && current.text == incoming.text
                     && current.original == incoming.original && current.translation == incoming.translation
                     && current.manualOverride == incoming.manualOverride
                     && current.autoOriginal == incoming.autoOriginal
                     && current.autoTranslation == incoming.autoTranslation;
    }
    if (sameLyrics) {
        syncCurrentIndexToPlaybackPosition();
        return;
    }

    const int previousIndex = m_currentIndex;
    beginResetModel();
    m_lines = std::move(lines);
    m_hasTimedLyrics = hasTimedLyrics;
    m_currentIndex = currentIndexForPlaybackPosition();
    endResetModel();

    if (m_currentIndex != previousIndex) {
        emit currentIndexChanged();
    }
}

int LyricsModel::currentIndexForPlaybackPosition() const
{
    if (m_lines.isEmpty() || !m_hasTimedLyrics) {
        return 0;
    }

    const std::chrono::milliseconds playbackTimestamp{qRound64(m_playbackPosition * 1000.0)};
    int synchronizedIndex = 0;
    std::chrono::milliseconds synchronizedTimestamp{0};
    for (qsizetype i = 0; i < m_lines.size(); ++i) {
        const std::chrono::milliseconds lineTimestamp = m_lines.at(i).timestamp;
        if (lineTimestamp <= playbackTimestamp && lineTimestamp >= synchronizedTimestamp) {
            synchronizedIndex = static_cast<int>(i);
            synchronizedTimestamp = lineTimestamp;
        }
    }
    return synchronizedIndex;
}

void LyricsModel::syncCurrentIndexToPlaybackPosition()
{
    setCurrentIndex(currentIndexForPlaybackPosition());
}

void LyricsModel::emitAllLyricsChanged(const QList<int> &roles)
{
    if (m_lines.isEmpty()) {
        return;
    }

    emit dataChanged(index(0, 0), index(m_lines.size() - 1, 0), roles);
}

void LyricsModel::emitCurrentRoleChanged(int index)
{
    if (index < 0 || index >= m_lines.size()) {
        return;
    }

    const QModelIndex modelIndex = this->index(index, 0);
    emit dataChanged(modelIndex, modelIndex, {CurrentRole});
}

}
