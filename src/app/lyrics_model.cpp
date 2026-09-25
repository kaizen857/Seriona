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

struct DelimiterHit {
    qsizetype index = -1;
    qsizetype size = 0;
};

DelimiterHit earliestDelimiterHit(const QString &line, const QStringList &delimiters)
{
    DelimiterHit earliest;
    for (const QString &delimiter : delimiters) {
        if (delimiter.isEmpty()) {
            continue;
        }

        const qsizetype delimiterIndex = line.indexOf(delimiter);
        if (delimiterIndex >= 0 && (earliest.index < 0 || delimiterIndex < earliest.index)) {
            earliest = DelimiterHit{delimiterIndex, delimiter.size()};
        }
    }
    return earliest;
}

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

    const QString &line = m_lines.at(index.row()).text;
    switch (role) {
    case Qt::DisplayRole:
    case DisplayLineRole:
        return displayLine(line);
    case RawLineRole:
        return line;
    case TranslationRole:
        return translationLine(line);
    case CurrentRole:
        return index.row() == m_currentIndex;
    case LyricsModel::TimestampRole:
        return m_lines.at(index.row()).timestamp.count() / 1000.0;
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
            {LyricsModel::TimestampRole, "timestampSec"}};
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
void LyricsModel::applyPlayerStateSnapshot(
    const seriona::control::PlayerStateSnapshot &snapshot,
    const seriona::control::LibraryStateSnapshot *library)
{
    if (!snapshot.currentTrack || snapshot.currentTrack->trackId.empty()) {
        clearLyrics();
        return;
    }

    const QString visibleTrackId = fromBackendString(snapshot.currentTrack->trackId);
    if (library == nullptr || !library->libraryTree) {
        applyMissingTrackSnapshot(visibleTrackId);
        return;
    }

    const std::string &trackId = snapshot.currentTrack->trackId;
    const seriona::scanner::SongMetadata *song = nullptr;
    for (const seriona::scanner::PlaylistNode &node : library->libraryTree->nodes) {
        if (node.song && node.song->trackId == trackId) {
            song = &(*node.song);
            break;
        }
    }

    if (song == nullptr) {
        applyMissingTrackSnapshot(visibleTrackId);
        return;
    }

    m_visibleTrackId = visibleTrackId;
    if (song->effectiveLyrics.empty()) {
        replaceLyrics({}, false);
        return;
    }

    QVector<Line> lines;
    lines.reserve(static_cast<qsizetype>(song->effectiveLyrics.size()));
    bool hasTimedLyrics = false;
    for (const seriona::scanner::LyricLine &line : song->effectiveLyrics) {
        lines.append(Line{line.timestamp, fromBackendString(line.text)});
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
        entry.insert(QStringLiteral("displayLine"), displayLine(line.text));
        entry.insert(QStringLiteral("translation"), translationLine(line.text));
        entry.insert(QStringLiteral("timestampSec"), line.timestamp.count() / 1000.0);
        out.append(entry);
    }
    return out;
}

QString LyricsModel::displayLine(const QString &line) const
{
    const DelimiterHit hit = earliestDelimiterHit(line, m_lyricDelimiters);
    if (hit.index < 0) {
        return line;
    }

    return line.left(hit.index).trimmed();
}

QString LyricsModel::translationLine(const QString &line) const
{
    const DelimiterHit hit = earliestDelimiterHit(line, m_lyricDelimiters);
    if (hit.index < 0) {
        return {};
    }

    return line.mid(hit.index + hit.size).trimmed();
}

void LyricsModel::clearLyrics()
{
    m_visibleTrackId.clear();
    replaceLyrics({}, false);
}

void LyricsModel::applyMissingTrackSnapshot(const QString &trackId)
{
    if (!trackId.isEmpty() && trackId == m_visibleTrackId) {
        return;
    }

    clearLyrics();
}

void LyricsModel::replaceLyrics(QVector<Line> lines, bool hasTimedLyrics)
{
    bool sameLyrics = m_hasTimedLyrics == hasTimedLyrics && m_lines.size() == lines.size();
    for (qsizetype i = 0; sameLyrics && i < m_lines.size(); ++i) {
        sameLyrics = m_lines.at(i).timestamp == lines.at(i).timestamp && m_lines.at(i).text == lines.at(i).text;
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
