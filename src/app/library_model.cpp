#include "library_model.h"

#include "backend_snapshot_mapper.h"
#include "library_folder_projection_model.h"
#include "path_text.h"

#include <QByteArray>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QUrl>
#include <QVariantMap>

#include <algorithm>
#include <utility>

#if SERIONA_HAS_BACKEND
#include <chrono>
#include <functional>
#endif

namespace Seriona::App {

namespace {

bool sortRulesEqual(const QVector<LibraryModel::SortRule> &lhs, const QVector<LibraryModel::SortRule> &rhs)
{
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (qsizetype i = 0; i < lhs.size(); ++i) {
        if (lhs.at(i).field != rhs.at(i).field || lhs.at(i).order != rhs.at(i).order) {
            return false;
        }
    }
    return true;
}

QString normalizeFolderSortRootPath(const QString &rootPath)
{
    if (rootPath.isEmpty()) {
        return QString();
    }

    const QFileInfo rootInfo(rootPath);
    const QString absolutePath = rootInfo.absoluteFilePath();
    return absolutePath.isEmpty() ? QDir::cleanPath(rootPath) : QDir::cleanPath(absolutePath);
}

}

#if SERIONA_HAS_BACKEND
namespace {
QString toQString(const std::string &value)
{
    return QString::fromStdString(value);
}

std::string toStdString(const QString &value)
{
    const QByteArray utf8 = value.toUtf8();
    return std::string(utf8.constData(), static_cast<std::size_t>(utf8.size()));
}

seriona::control::TrackIdentity trackIdentityForEntry(const LibraryModel::Entry &entry)
{
    seriona::control::TrackIdentity identity;
    identity.trackId = toStdString(entry.trackId);
    return identity;
}

QString fromBackendPath(const std::filesystem::path &path)
{
    return QString::fromStdString(pathTextUtf8(path));
}

std::filesystem::path toBackendPath(const QString &path)
{
    const QByteArray utf8 = path.toUtf8();
    return pathFromUtf8(std::string(utf8.constData(), static_cast<std::size_t>(utf8.size())));
}

std::optional<seriona::control::FolderSortField> backendSortField(const QString &field)
{
    if (field == QStringLiteral("title")) {
        return seriona::control::FolderSortField::Title;
    }
    if (field == QStringLiteral("artist")) {
        return seriona::control::FolderSortField::Artist;
    }
    if (field == QStringLiteral("album")) {
        return seriona::control::FolderSortField::Album;
    }
    if (field == QStringLiteral("filename")) {
        return seriona::control::FolderSortField::Filename;
    }
    if (field == QStringLiteral("year")) {
        return seriona::control::FolderSortField::Year;
    }
    if (field == QStringLiteral("duration")) {
        return seriona::control::FolderSortField::Duration;
    }
    if (field == QStringLiteral("createdDate")) {
        return seriona::control::FolderSortField::CreatedDate;
    }
    if (field == QStringLiteral("discNumber")) {
        return seriona::control::FolderSortField::DiscNumber;
    }
    if (field == QStringLiteral("trackNumber")) {
        return seriona::control::FolderSortField::TrackNumber;
    }
    return std::nullopt;
}

QString modelSortField(seriona::control::FolderSortField field)
{
    switch (field) {
    case seriona::control::FolderSortField::Title:
        return QStringLiteral("title");
    case seriona::control::FolderSortField::Artist:
        return QStringLiteral("artist");
    case seriona::control::FolderSortField::Album:
        return QStringLiteral("album");
    case seriona::control::FolderSortField::Filename:
        return QStringLiteral("filename");
    case seriona::control::FolderSortField::Year:
        return QStringLiteral("year");
    case seriona::control::FolderSortField::Duration:
        return QStringLiteral("duration");
    case seriona::control::FolderSortField::CreatedDate:
        return QStringLiteral("createdDate");
    case seriona::control::FolderSortField::DiscNumber:
        return QStringLiteral("discNumber");
    case seriona::control::FolderSortField::TrackNumber:
        return QStringLiteral("trackNumber");
    }
    return QString();
}

seriona::control::FolderSortDirection backendSortDirection(const QString &order)
{
    return order == QStringLiteral("desc")
        ? seriona::control::FolderSortDirection::Descending
        : seriona::control::FolderSortDirection::Ascending;
}

QString modelSortOrder(seriona::control::FolderSortDirection direction)
{
    return direction == seriona::control::FolderSortDirection::Descending
        ? QStringLiteral("desc")
        : QStringLiteral("asc");
}

QString typeForNode(const LibraryTreeStore::Node &node)
{
    return node.isFolder ? QStringLiteral("folder") : QStringLiteral("file");
}

QString formatDuration(std::chrono::milliseconds duration)
{
    const qint64 totalSeconds = duration.count() / 1000;
    const qint64 hours = totalSeconds / 3600;
    const qint64 minutes = (totalSeconds % 3600) / 60;
    const qint64 seconds = totalSeconds % 60;

    if (hours > 0) {
        return QStringLiteral("%1:%2:%3").arg(hours).arg(minutes, 2, 10, QLatin1Char('0')).arg(seconds, 2, 10, QLatin1Char('0'));
    }

    return QStringLiteral("%1:%2").arg(minutes).arg(seconds, 2, 10, QLatin1Char('0'));
}

QString formatFromNode(const LibraryTreeStore::Node &node)
{
    const std::filesystem::path &path = !node.sourceFilePath.empty() ? node.sourceFilePath : node.filePath;
    QString extension = QString::fromStdString(pathTextUtf8(path.extension()));
    if (extension.startsWith(QLatin1Char('.'))) {
        extension.remove(0, 1);
    }
    return extension.toUpper();
}

QString fileNameFromNode(const LibraryTreeStore::Node &node)
{
    const std::filesystem::path &path = !node.sourceFilePath.empty() ? node.sourceFilePath : node.filePath;
    if (path.empty()) {
        return node.displayName;
    }
    return QString::fromStdString(pathTextUtf8(path.filename()));
}

LibraryModel::Entry entryFromNode(const LibraryTreeStore::Node &node, const QString &parentName)
{
    LibraryModel::Entry entry;
    entry.nodeId = node.nodeId;
    entry.type = typeForNode(node);
    entry.parentName = parentName;
    entry.songCount = node.isFolder ? node.descendantTrackCount : 0;
    entry.isFolder = node.isFolder;
    entry.fileName = fileNameFromNode(node);

    if (entry.isFolder) {
        entry.name = node.displayName;
    } else {
        entry.trackId = node.trackId;
        entry.title = node.title.isEmpty() ? node.displayName : node.title;
        entry.artist = node.artist;
        entry.album = node.album;
        entry.format = formatFromNode(node);
        entry.sampleRate = node.sampleRate;
        entry.bitDepth = node.bitDepth;
        entry.duration = node.duration.has_value() ? formatDuration(*node.duration) : QString();
        entry.durationValue = node.duration;
        entry.year = node.year;
        entry.discNumber = node.discNumber;
        entry.trackNumber = node.trackNumber;
        entry.createdDate = node.fileMtime;
    }

    const std::filesystem::path &artworkPath = !node.thumbnailPath.empty() ? node.thumbnailPath : node.artworkPath;
    if (!artworkPath.empty()) {
        entry.artworkSource = QUrl::fromLocalFile(QString::fromStdString(pathTextUtf8(artworkPath))).toString();
    }
    return entry;
}
}
#endif

namespace {

QString localDirectoryPath(const QUrl &rootUrl)
{
    QString rootPath;
    if (rootUrl.isLocalFile()) {
        rootPath = rootUrl.toLocalFile();
    } else if (rootUrl.scheme().isEmpty()) {
        rootPath = rootUrl.path();
    }

    const QFileInfo rootInfo(rootPath);
    if (!rootInfo.exists() || !rootInfo.isDir()) {
        return QString();
    }

    return rootInfo.absoluteFilePath();
}

std::optional<QString> canonicalSortField(const QString &field)
{
    const QString trimmed = field.trimmed();
    if (trimmed.compare(QStringLiteral("title"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("title");
    }
    if (trimmed.compare(QStringLiteral("artist"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("artist");
    }
    if (trimmed.compare(QStringLiteral("album"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("album");
    }
    if (trimmed.compare(QStringLiteral("filename"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("filename");
    }
    if (trimmed.compare(QStringLiteral("year"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("year");
    }
    if (trimmed.compare(QStringLiteral("duration"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("duration");
    }
    if (trimmed.compare(QStringLiteral("createdDate"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("createdDate");
    }
    if (trimmed.compare(QStringLiteral("discNumber"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("discNumber");
    }
    if (trimmed.compare(QStringLiteral("trackNumber"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("trackNumber");
    }
    return std::nullopt;
}

std::optional<QString> canonicalSortOrder(const QString &order)
{
    const QString trimmed = order.trimmed();
    if (trimmed.compare(QStringLiteral("asc"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("asc");
    }
    if (trimmed.compare(QStringLiteral("desc"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("desc");
    }
    return std::nullopt;
}

struct ProjectionSortValue {
    bool hasValue = false;
    QString text;
    std::optional<qint64> number;
};

ProjectionSortValue textSortValue(QString value)
{
    value = value.trimmed();
    if (value.isEmpty()) {
        return {};
    }
    ProjectionSortValue result;
    result.hasValue = true;
    result.text = value;
    return result;
}

ProjectionSortValue numberSortValue(std::optional<qint64> value)
{
    if (!value.has_value()) {
        return {};
    }
    ProjectionSortValue result;
    result.hasValue = true;
    result.number = *value;
    return result;
}

ProjectionSortValue sortValueForEntry(const LibraryModel::Entry &entry, const QString &field)
{
    if (field == QStringLiteral("title")) {
        return textSortValue(entry.title.isEmpty() ? entry.name : entry.title);
    }
    if (field == QStringLiteral("artist")) {
        return textSortValue(entry.artist);
    }
    if (field == QStringLiteral("album")) {
        return textSortValue(entry.album);
    }
    if (field == QStringLiteral("filename")) {
        return textSortValue(entry.fileName.isEmpty() ? entry.name : entry.fileName);
    }
    if (field == QStringLiteral("year")) {
        return numberSortValue(entry.year.has_value() ? std::optional<qint64>{static_cast<qint64>(*entry.year)} : std::nullopt);
    }
    if (field == QStringLiteral("duration")) {
        return numberSortValue(entry.durationValue.has_value() ? std::optional<qint64>{static_cast<qint64>(entry.durationValue->count())} : std::nullopt);
    }
    if (field == QStringLiteral("createdDate")) {
        return numberSortValue(entry.createdDate.has_value() ? std::optional<qint64>{static_cast<qint64>(entry.createdDate->time_since_epoch().count())} : std::nullopt);
    }
    if (field == QStringLiteral("discNumber")) {
        return numberSortValue(entry.discNumber.has_value() ? std::optional<qint64>{static_cast<qint64>(*entry.discNumber)} : std::nullopt);
    }
    if (field == QStringLiteral("trackNumber")) {
        return numberSortValue(entry.trackNumber.has_value() ? std::optional<qint64>{static_cast<qint64>(*entry.trackNumber)} : std::nullopt);
    }
    return {};
}

int compareProjectionSortValues(const ProjectionSortValue &left, const ProjectionSortValue &right)
{
    if (left.hasValue != right.hasValue) {
        return left.hasValue ? -1 : 1;
    }
    if (!left.hasValue) {
        return 0;
    }
    if (left.number.has_value() || right.number.has_value()) {
        if (!left.number.has_value() || !right.number.has_value()) {
            return left.number.has_value() ? -1 : 1;
        }
        if (*left.number < *right.number) {
            return -1;
        }
        if (*right.number < *left.number) {
            return 1;
        }
        return 0;
    }

    const int caseInsensitive = QString::compare(left.text, right.text, Qt::CaseInsensitive);
    if (caseInsensitive != 0) {
        return caseInsensitive < 0 ? -1 : 1;
    }
    const int caseSensitive = QString::compare(left.text, right.text, Qt::CaseSensitive);
    if (caseSensitive != 0) {
        return caseSensitive < 0 ? -1 : 1;
    }
    return 0;
}

constexpr int kSearchWeightTitle = 10;
constexpr int kSearchWeightArtist = 5;
constexpr int kSearchWeightAlbum = 3;
constexpr int kSearchWeightFilename = 2;

int fieldSearchScore(const QString &fieldValue, const QString &queryLower, int weight)
{
    if (fieldValue.isEmpty()) {
        return 0;
    }

    const QString fieldLower = fieldValue.toLower();
    const qsizetype pos = fieldLower.indexOf(queryLower);
    if (pos < 0) {
        return 0;
    }
    if (fieldLower == queryLower) {
        return weight * 10;
    }
    if (pos == 0) {
        return weight * 5;
    }
    return weight * 1;
}

}

LibraryModel::LibraryModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int LibraryModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }

    return m_entries.size();
}

QVariant LibraryModel::data(const QModelIndex &index, int role) const
{
    const Entry *entry = entryAt(index.row());
    if (entry == nullptr) {
        return {};
    }

    switch (role) {
    case TypeRole:
        return entry->type;
    case NameRole:
        return entry->name;
    case TitleRole:
        return entry->title;
    case ArtistRole:
        return entry->artist;
    case AlbumRole:
        return entry->album;
    case ParentNameRole:
        return entry->parentName;
    case SongCountRole:
        return entry->songCount;
    case DurationRole:
        return entry->duration;
    case FormatRole:
        return entry->format;
    case SampleRateRole:
        return entry->sampleRate;
    case BitDepthRole:
        return entry->bitDepth;
    case NodeIdRole:
        return entry->nodeId;
    case TrackIdRole:
        return entry->trackId;
    case IsFolderRole:
        return entry->isFolder;
    case IsPlayingRole:
        return entry->isPlaying;
    case IsFocusedRole:
        return entry->isFocused;
    case ParentNodeIdRole:
        return entry->parentNodeId;
    case ArtworkSourceRole:
        return entry->artworkSource;
    case YearRole:
        return entry->year.has_value() ? QVariant{static_cast<qint64>(*entry->year)} : QVariant{};
    default:
        return {};
    }
}

QHash<int, QByteArray> LibraryModel::roleNames() const
{
    return {{TypeRole, "type"},
            {NameRole, "name"},
            {TitleRole, "title"},
            {ArtistRole, "artist"},
            {AlbumRole, "album"},
            {ParentNameRole, "parentName"},
            {SongCountRole, "songCount"},
            {DurationRole, "duration"},
            {FormatRole, "format"},
            {SampleRateRole, "sampleRate"},
            {BitDepthRole, "bitDepth"},
            {NodeIdRole, "nodeId"},
            {TrackIdRole, "trackId"},
            {IsFolderRole, "isFolder"},
            {IsPlayingRole, "isPlaying"},
            {IsFocusedRole, "isFocused"},
            {ParentNodeIdRole, "parentNodeId"},
            {ArtworkSourceRole, "artworkSource"},
            {YearRole, "year"}};
}

const LibraryModel::Entry *LibraryModel::entryAt(int row) const
{
    if (row < 0 || row >= m_entries.size()) {
        return nullptr;
    }

    return &m_entries.at(row);
}

const LibraryModel::Entry *LibraryModel::entryByNodeId(const QString &nodeId) const
{
    const auto entryIt = m_nodeById.constFind(nodeId);
    if (entryIt == m_nodeById.cend()) {
        return nullptr;
    }

    return &entryIt.value();
}

QVector<QString> LibraryModel::childNodeIds(const QString &nodeId) const
{
    return m_childrenById.value(nodeId);
}

QString LibraryModel::parentNodeId(const QString &nodeId) const
{
    return m_parentById.value(nodeId);
}

QString LibraryModel::nodeIdForTrackId(const QString &trackId) const
{
    return m_trackIdToNodeId.value(trackId);
}

QString LibraryModel::absoluteFilePathForNode(const QString &nodeId) const
{
    const LibraryTreeStore::Node *node = m_treeStore.nodeById(nodeId);
    if (node == nullptr) {
        return QString();
    }

    const auto effectivePath = [](const LibraryTreeStore::Node &target) -> const std::filesystem::path & {
        return !target.sourceFilePath.empty() ? target.sourceFilePath : target.filePath;
    };
    const auto pathText = [](const std::filesystem::path &path) {
        return path.empty() ? QString() : QString::fromStdString(pathTextUtf8(path));
    };

    if (!node->isFolder) {
        return pathText(effectivePath(*node));
    }

    // 文件夹：沿 显示名链 从某个后代曲目重建完整目录路径。任一父级显示名与路径段
    // 不一致（cue 容器/虚拟目录等无文件系统实体）则放弃该曲目，全部失败返回空——
    // 宁可删除被后端拒绝，也不把错误路径送进删除命令。
    QVector<const LibraryTreeStore::Node *> tracks;
    QVector<QString> pending = node->childNodeIds;
    for (qsizetype i = 0; i < pending.size(); ++i) {
        const LibraryTreeStore::Node *child = m_treeStore.nodeById(pending.at(i));
        if (child == nullptr) {
            continue;
        }
        if (child->isFolder) {
            pending += child->childNodeIds;
        } else {
            tracks.append(child);
        }
    }

    for (const LibraryTreeStore::Node *track : tracks) {
        const std::filesystem::path trackPath = effectivePath(*track);
        if (trackPath.empty()) {
            continue;
        }
        std::filesystem::path dir = trackPath.parent_path();
        QString cursor = track->parentNodeId;
        bool matched = true;
        while (!cursor.isEmpty() && cursor != nodeId) {
            const LibraryTreeStore::Node *parent = m_treeStore.nodeById(cursor);
            if (parent == nullptr || !parent->isFolder || parent->parentNodeId.isEmpty()
                || dir.empty() || QString::fromStdString(pathTextUtf8(dir.filename())) != parent->displayName) {
                matched = false;
                break;
            }
            dir = dir.parent_path();
            cursor = parent->parentNodeId;
        }
        if (matched && cursor == nodeId) {
            // 返回前校验目标文件夹自身显示名与最终 dir.filename() 一致（R1 修复）：
            // 上方循环只校验非目标的中间父级，cue 曲目是 cue 容器的直接子级时循环
            // 零次执行，会把 .cue 所在真实目录送进删除命令——此处兜底校验目标自身。
            // root 虚拟根（parentNodeId 为空）不在投影中，显示名从未被校验，行为不变；
            // 真实文件夹显示名必然与路径段一致，不误伤；不一致宁可返回空拒绝删除。
            if (!node->parentNodeId.isEmpty() && !dir.empty()
                && QString::fromStdString(pathTextUtf8(dir.filename())) != node->displayName) {
                continue;
            }
            return pathText(dir);
        }
    }
    return QString();
}

bool LibraryModel::containsNodeId(const QString &nodeId) const
{
    return !nodeId.isEmpty() && m_nodeById.contains(nodeId);
}

int LibraryModel::rowForNodeId(const QString &nodeId) const
{
    return m_rowByNodeId.value(nodeId, -1);
}

QString LibraryModel::firstNodeId() const
{
    if (!m_rootNodeId.isEmpty() && containsNodeId(m_rootNodeId)) {
        return m_rootNodeId;
    }
    for (const QString &nodeId : m_nodeOrder) {
        if (!nodeId.isEmpty() && containsNodeId(nodeId)) {
            return nodeId;
        }
    }
    for (const Entry &entry : m_entries) {
        if (!entry.nodeId.isEmpty()) {
            return entry.nodeId;
        }
    }
    return {};
}

QVector<QString> LibraryModel::ancestorChainForNode(const QString &nodeId) const
{
    QVector<QString> chain;
    QSet<QString> visited;
    QString currentNodeId = nodeId;

    while (!currentNodeId.isEmpty() && !visited.contains(currentNodeId)) {
        chain.append(currentNodeId);
        visited.insert(currentNodeId);
        currentNodeId = m_parentById.value(currentNodeId);
    }

    const QString rootNodeId = firstNodeId();
    if (!rootNodeId.isEmpty() && !chain.contains(rootNodeId)) {
        chain.append(rootNodeId);
    }

    return chain;
}

std::uint64_t LibraryModel::version() const
{
    return m_version;
}

int LibraryModel::projectionRevision() const
{
    return m_projectionRevision;
}

QString LibraryModel::rootNodeId() const
{
    return m_rootNodeId;
}

QVector<QString> LibraryModel::rootProjectionNodeIds() const
{
    return m_rootProjectionNodeIds;
}

QString LibraryModel::focusedNodeId() const
{
    return m_focusedNodeId;
}

QString LibraryModel::playingTrackId() const
{
    return m_playingTrackId;
}

bool LibraryModel::setFocusedNodeId(const QString &nodeId)
{
    if (!nodeId.isEmpty() && !containsNodeId(nodeId)) {
        return false;
    }
    if (m_focusedNodeId == nodeId) {
        return false;
    }

    const QString previousFocusedNodeId = m_focusedNodeId;
    m_focusedNodeId = nodeId;

    bool changed = false;
    const int previousRow = rowForNodeId(previousFocusedNodeId);
    if (previousRow >= 0) {
        changed = setEntryRoleFlag(previousRow, IsFocusedRole, false, true) || changed;
    }
    const int nextRow = rowForNodeId(m_focusedNodeId);
    if (nextRow >= 0) {
        changed = setEntryRoleFlag(nextRow, IsFocusedRole, true, true) || changed;
    }
    emit focusedNodeIdChanged();
    return changed;
}

bool LibraryModel::setPlayingTrackId(const QString &trackId)
{
    if (m_playingTrackId == trackId) {
        return false;
    }

    const QString previousNodeId = nodeIdForTrackId(m_playingTrackId);
    m_playingTrackId = trackId;
    const QString nextNodeId = nodeIdForTrackId(m_playingTrackId);

    bool changed = false;
    const int previousRow = rowForNodeId(previousNodeId);
    if (previousRow >= 0) {
        changed = setEntryRoleFlag(previousRow, IsPlayingRole, false, true) || changed;
    }
    const int nextRow = rowForNodeId(nextNodeId);
    if (nextRow >= 0) {
        changed = setEntryRoleFlag(nextRow, IsPlayingRole, true, true) || changed;
    }
    emit playingTrackIdChanged();
    return changed;
}

void LibraryModel::applyBrowsingState(const QString &focusedNodeId,
                                      const QString &playingTrackId,
                                      const QString &searchQuery,
                                      const QString &browserRootNodeId,
                                      const QVector<SortRule> &sortRules)
{
    const QString previousFocusedNodeId = m_focusedNodeId;
    const QString previousPlayingTrackId = m_playingTrackId;
    m_focusedNodeId = containsNodeId(focusedNodeId) ? focusedNodeId : QString();
    m_playingTrackId = playingTrackId;
    const QString trimmedQuery = searchQuery.trimmed();

    QVector<QString> projectionNodeIds;
    if (!trimmedQuery.isEmpty()) {
        const QString searchRootNodeId = (!browserRootNodeId.isEmpty() && containsNodeId(browserRootNodeId))
            ? browserRootNodeId
            : m_rootNodeId;
        projectionNodeIds = searchProjectionNodeIds(trimmedQuery, searchRootNodeId);
    } else {
        m_searchScoreByNodeId.clear();
        if (!browserRootNodeId.isEmpty() && containsNodeId(browserRootNodeId)) {
            projectionNodeIds = childNodeIds(browserRootNodeId);
        } else {
            projectionNodeIds = m_rootProjectionNodeIds;
        }
    }

    projectionNodeIds = sortedProjectionNodeIds(std::move(projectionNodeIds), sortRules);
    setProjectionNodeIds(projectionNodeIds);

    // 浏览状态重投影可能绕过 setFocusedNodeId / setPlayingTrackId（如快照对账回退），
    // 身份实际变化时补发信号，让每级文件夹投影模型同步行内标记。
    if (m_focusedNodeId != previousFocusedNodeId) {
        emit focusedNodeIdChanged();
    }
    if (m_playingTrackId != previousPlayingTrackId) {
        emit playingTrackIdChanged();
    }
}

int LibraryModel::visibleNodeCount() const
{
    return m_entries.size();
}

QString LibraryModel::firstVisibleNodeId() const
{
    for (const Entry &entry : m_entries) {
        if (!entry.nodeId.isEmpty()) {
            return entry.nodeId;
        }
    }
    return {};
}

QString LibraryModel::firstVisibleMatchingNodeId() const
{
    for (const Entry &entry : m_entries) {
        if (!entry.nodeId.isEmpty()) {
            return entry.nodeId;
        }
    }
    return {};
}

bool LibraryModel::hasLibraryContent() const
{
    for (const QString &nodeId : m_nodeOrder) {
        if (!nodeId.isEmpty() && nodeId != m_rootNodeId && m_nodeById.contains(nodeId)) {
            return true;
        }
    }
    return false;
}

bool LibraryModel::setEntryRoleFlag(int row, Role role, bool value, bool notify)
{
    if (row < 0 || row >= m_entries.size()) {
        return false;
    }

    Entry &entry = m_entries[row];
    bool *flag = nullptr;
    switch (role) {
    case IsPlayingRole:
        flag = &entry.isPlaying;
        break;
    case IsFocusedRole:
        flag = &entry.isFocused;
        break;
    default:
        return false;
    }

    if (*flag == value) {
        return false;
    }

    *flag = value;
    if (!entry.nodeId.isEmpty()) {
        m_nodeById.insert(entry.nodeId, entry);
    }
    if (notify) {
        const QModelIndex changedIndex = index(row, 0);
        emit dataChanged(changedIndex, changedIndex, QList<int>{role});
    }
    return true;
}

int LibraryModel::entrySearchScore(const Entry &entry, const QString &trimmedQuery) const
{
    if (entry.isFolder || trimmedQuery.isEmpty()) {
        return 0;
    }

    const QString queryLower = trimmedQuery.toLower();
    int score = 0;
    score += fieldSearchScore(entry.title, queryLower, kSearchWeightTitle);
    score += fieldSearchScore(entry.artist, queryLower, kSearchWeightArtist);
    score += fieldSearchScore(entry.album, queryLower, kSearchWeightAlbum);
    score += fieldSearchScore(entry.fileName, queryLower, kSearchWeightFilename);
    return score;
}

void LibraryModel::rebuildEntryIndexes()
{
    m_nodeById.clear();
    m_trackIdToNodeId.clear();
    m_rowByNodeId.clear();
    m_nodeOrder.clear();
    m_rootProjectionNodeIds.clear();

    for (int row = 0; row < m_entries.size(); ++row) {
        const Entry &entry = m_entries.at(row);
        if (!entry.nodeId.isEmpty()) {
            m_nodeById.insert(entry.nodeId, entry);
            m_rowByNodeId.insert(entry.nodeId, row);
            m_nodeOrder.append(entry.nodeId);
            m_rootProjectionNodeIds.append(entry.nodeId);
        }
        if (!entry.nodeId.isEmpty() && !entry.parentNodeId.isEmpty() && !m_parentById.contains(entry.nodeId)) {
            m_parentById.insert(entry.nodeId, entry.parentNodeId);
        }
        if (!entry.trackId.isEmpty()) {
            m_trackIdToNodeId.insert(entry.trackId, entry.nodeId);
        }
    }
}

void LibraryModel::rebuildProjectionIndexes()
{
    m_rowByNodeId.clear();
    for (int row = 0; row < m_entries.size(); ++row) {
        const Entry &entry = m_entries.at(row);
        if (!entry.nodeId.isEmpty()) {
            m_rowByNodeId.insert(entry.nodeId, row);
        }
    }
}

void LibraryModel::setProjectionNodeIds(const QVector<QString> &nodeIds)
{
    const QString playingNodeId = nodeIdForTrackId(m_playingTrackId);
    QVector<Entry> projectedEntries;
    projectedEntries.reserve(nodeIds.size());
    QSet<QString> projectedNodeIds;

    for (const QString &nodeId : nodeIds) {
        if (nodeId.isEmpty() || nodeId == m_rootNodeId || projectedNodeIds.contains(nodeId)) {
            continue;
        }
        const auto entryIt = m_nodeById.constFind(nodeId);
        if (entryIt == m_nodeById.cend()) {
            continue;
        }

        Entry entry = entryIt.value();
        entry.isFocused = entry.nodeId == m_focusedNodeId;
        entry.isPlaying = !playingNodeId.isEmpty() && entry.nodeId == playingNodeId;
        projectedEntries.append(entry);
        projectedNodeIds.insert(nodeId);
    }

    // 完整投影替换必须用 reset 语义：全量 remove+insert 会把完整替换伪装成
    // 增量 change set，ListView 会按增量重新定位可见条目并保留旧滚动位置，
    // 使"进入子文件夹从顶部开始"与"返回恢复锚点"都不可靠。reset 会重新生成
    // delegate 并把视口推到内容起点（进入子文件夹的期望行为）；返回父文件夹
    // 的锚点恢复由 QML 在 projectionRevisionChanged 信号后执行。
    beginResetModel();
    m_entries = projectedEntries;
    rebuildProjectionIndexes();
    endResetModel();
    ++m_projectionRevision;
    emit projectionRevisionChanged();
}

QVector<QString> LibraryModel::sortedProjectionNodeIds(QVector<QString> nodeIds, const QVector<SortRule> &sortRules) const
{
    if (sortRules.isEmpty() || nodeIds.size() < 2) {
        return nodeIds;
    }

    std::stable_sort(nodeIds.begin(), nodeIds.end(), [this, &sortRules](const QString &leftNodeId, const QString &rightNodeId) {
        const auto leftIt = m_nodeById.constFind(leftNodeId);
        const auto rightIt = m_nodeById.constFind(rightNodeId);
        if (leftIt == m_nodeById.cend() || rightIt == m_nodeById.cend()) {
            return false;
        }

        for (const SortRule &rule : sortRules) {
            if (rule.field == QStringLiteral("searchScore")) {
                const int leftScore = m_searchScoreByNodeId.value(leftNodeId, 0);
                const int rightScore = m_searchScoreByNodeId.value(rightNodeId, 0);
                if (leftScore != rightScore) {
                    return rule.order == QStringLiteral("desc") ? leftScore > rightScore : leftScore < rightScore;
                }
                continue;
            }

            const ProjectionSortValue leftValue = sortValueForEntry(leftIt.value(), rule.field);
            const ProjectionSortValue rightValue = sortValueForEntry(rightIt.value(), rule.field);
            if (leftValue.hasValue != rightValue.hasValue) {
                return leftValue.hasValue;
            }

            int comparison = compareProjectionSortValues(leftValue, rightValue);
            if (rule.order == QStringLiteral("desc")) {
                comparison = -comparison;
            }
            if (comparison < 0) {
                return true;
            }
            if (comparison > 0) {
                return false;
            }
        }

        return false;
    });
    return nodeIds;
}

QVector<QString> LibraryModel::searchProjectionNodeIds(const QString &searchQuery, const QString &subtreeRootNodeId)
{
    m_searchScoreByNodeId.clear();
    const QString trimmedQuery = searchQuery.trimmed();
    QVector<QString> nodeIds;
    if (trimmedQuery.isEmpty() || subtreeRootNodeId.isEmpty() || !containsNodeId(subtreeRootNodeId)) {
        return nodeIds;
    }

    QVector<QString> pending{subtreeRootNodeId};
    while (!pending.isEmpty()) {
        const QString nodeId = pending.takeLast();
        const auto entryIt = m_nodeById.constFind(nodeId);
        if (entryIt == m_nodeById.cend()) {
            continue;
        }

        const Entry &entry = entryIt.value();
        if (entry.isFolder) {
            const QVector<QString> children = m_childrenById.value(nodeId);
            for (auto it = children.crbegin(); it != children.crend(); ++it) {
                pending.append(*it);
            }
            continue;
        }

        const int score = entrySearchScore(entry, trimmedQuery);
        if (score > 0) {
            nodeIds.append(nodeId);
            m_searchScoreByNodeId.insert(nodeId, score);
        }
    }
    return nodeIds;
}

#if SERIONA_HAS_BACKEND
void LibraryModel::setPlaylistTreeSnapshot(const seriona::scanner::PlaylistTreeSnapshot &snapshot)
{
    m_treeStore.setSnapshot(snapshot);

    for (const LibraryTreeStore::SkippedChild &skippedChild : m_treeStore.skippedChildren()) {
        qWarning().noquote() << QStringLiteral("LibraryModel skipped missing child node %1 under %2").arg(skippedChild.childNodeId, skippedChild.parentNodeId);
    }
    if (!m_treeStore.missingRootNodeId().isEmpty()) {
        qWarning().noquote() << QStringLiteral("LibraryModel missing root node %1; falling back to snapshot node order").arg(m_treeStore.missingRootNodeId());
    }

    QHash<QString, Entry> nodeById;
    QHash<QString, QVector<QString>> childrenById;
    QHash<QString, QString> parentById;
    QHash<QString, QString> trackIdToNodeId;

    for (const QString &nodeId : m_treeStore.nodeOrder()) {
        const LibraryTreeStore::Node *node = m_treeStore.nodeById(nodeId);
        if (node == nullptr) {
            continue;
        }

        QString parentName;
        const QString parentNodeId = m_treeStore.parentNodeId(nodeId);
        if (!parentNodeId.isEmpty()) {
            const LibraryTreeStore::Node *parentNode = m_treeStore.nodeById(parentNodeId);
            if (parentNode != nullptr) {
                parentName = parentNode->displayName;
            }
        }

        Entry entry = entryFromNode(*node, parentName);
        entry.parentNodeId = parentNodeId;
        nodeById.insert(nodeId, entry);
        childrenById.insert(nodeId, m_treeStore.childNodeIds(nodeId));
        if (!parentNodeId.isEmpty()) {
            parentById.insert(nodeId, parentNodeId);
        }
        if (!node->trackId.isEmpty() && !trackIdToNodeId.contains(node->trackId)) {
            trackIdToNodeId.insert(node->trackId, nodeId);
        }
        if (!node->logicalTrackId.isEmpty() && !trackIdToNodeId.contains(node->logicalTrackId)) {
            trackIdToNodeId.insert(node->logicalTrackId, nodeId);
        }
    }

    beginResetModel();
    m_nodeById = nodeById;
    m_childrenById = childrenById;
    m_parentById = parentById;
    m_trackIdToNodeId = trackIdToNodeId;
    m_nodeOrder = m_treeStore.nodeOrder();
    m_rootNodeId = m_treeStore.rootNodeId();
    m_rootProjectionNodeIds = m_treeStore.rootChildNodeIds();
    m_focusedNodeId.clear();
    m_playingTrackId.clear();
    m_entries.clear();
    const QString playingNodeId = nodeIdForTrackId(m_playingTrackId);
    QSet<QString> projectedNodeIds;
    for (const QString &nodeId : m_rootProjectionNodeIds) {
        if (nodeId.isEmpty() || nodeId == m_rootNodeId || projectedNodeIds.contains(nodeId)) {
            continue;
        }
        const auto entryIt = m_nodeById.constFind(nodeId);
        if (entryIt == m_nodeById.cend()) {
            continue;
        }
        Entry entry = entryIt.value();
        entry.isFocused = false;
        entry.isPlaying = !playingNodeId.isEmpty() && entry.nodeId == playingNodeId;
        m_entries.append(entry);
        projectedNodeIds.insert(nodeId);
    }
    rebuildProjectionIndexes();
    m_version = snapshot.version;
    endResetModel();
    ++m_projectionRevision;
    emit projectionRevisionChanged();
    emit treeChanged();
}
#endif

LibraryController::LibraryController(QObject *parent)
    : QObject(parent)
    , m_model(this)
{
    reconcileBrowsingState({}, {});
}

LibraryModel *LibraryController::model()
{
    return &m_model;
}

QString LibraryController::currentFolderName() const
{
    return m_currentFolderName;
}

bool LibraryController::canGoBack() const
{
    return !m_currentFolderNodeId.isEmpty();
}

QString LibraryController::searchQuery() const
{
    return m_searchQuery;
}

QString LibraryController::focusedNodeId() const
{
    return m_focusedNodeId;
}

void LibraryController::setFocusedNodeId(const QString &nodeId)
{
    const bool previousCanGoBack = canGoBack();
    if (!nodeId.isEmpty() && !m_model.containsNodeId(nodeId)) {
        return;
    }
    if (m_focusedNodeId == nodeId) {
        return;
    }

    m_focusedNodeId = nodeId;
    m_model.setFocusedNodeId(m_focusedNodeId);
    emit focusedNodeIdChanged();
    if (previousCanGoBack != canGoBack()) {
        emit canGoBackChanged();
    }
}

QString LibraryController::selectedBrowserNodeId() const
{
    return m_selectedBrowserNodeId;
}

void LibraryController::setSelectedBrowserNodeId(const QString &nodeId)
{
    const bool previousCanGoBack = canGoBack();
    if (!nodeId.isEmpty() && !m_model.containsNodeId(nodeId)) {
        return;
    }
    if (m_selectedBrowserNodeId == nodeId) {
        setFocusedNodeId(nodeId);
        return;
    }

    m_selectedBrowserNodeId = nodeId;
    setFocusedNodeId(nodeId);
    emit selectedBrowserNodeIdChanged();
    if (previousCanGoBack != canGoBack()) {
        emit canGoBackChanged();
    }
}

QString LibraryController::scrollRequest() const
{
    return m_scrollRequest;
}

int LibraryController::visibleNodeCount() const
{
    return m_visibleNodeCount;
}

QString LibraryController::scanStatus() const
{
    return m_scanStatus;
}

int LibraryController::scanProgress() const
{
    return m_scanProgress;
}

quint64 LibraryController::scannedSongCount() const
{
    return m_scannedSongCount;
}

quint64 LibraryController::totalSongCount() const
{
    return m_totalSongCount;
}

QString LibraryController::lastError() const
{
    return m_lastError;
}

QString LibraryController::savedRootPath() const
{
    return m_savedRootPath;
}

QString LibraryController::playingTrackId() const
{
    return m_playingTrackId;
}

void LibraryController::setPlayingTrackId(const QString &trackId)
{
    if (m_playingTrackId == trackId) {
        return;
    }

    m_playingTrackId = trackId;
    m_model.setPlayingTrackId(m_playingTrackId);
    emit playingTrackIdChanged();

    if (!m_followCurrentlyPlaying) {
        return;
    }

    const QString playingNodeId = m_model.nodeIdForTrackId(m_playingTrackId);
    if (!playingNodeId.isEmpty()) {
        if (!showNodeInBrowserProjection(playingNodeId)) {
            return;
        }
        setSelectedBrowserNodeId(playingNodeId);
        requestScrollToNode(playingNodeId);
    }
}

bool LibraryController::followCurrentlyPlaying() const
{
    return m_followCurrentlyPlaying;
}

void LibraryController::setFollowCurrentlyPlaying(bool follow)
{
    if (m_followCurrentlyPlaying == follow) {
        return;
    }

    m_followCurrentlyPlaying = follow;
    emit followCurrentlyPlayingChanged();

    if (!m_followCurrentlyPlaying) {
        return;
    }

    const QString playingNodeId = m_model.nodeIdForTrackId(m_playingTrackId);
    if (!playingNodeId.isEmpty()) {
        if (!showNodeInBrowserProjection(playingNodeId)) {
            return;
        }
        setSelectedBrowserNodeId(playingNodeId);
        requestScrollToNode(playingNodeId);
    }
}

bool LibraryController::libraryEmpty() const
{
    return !m_model.hasLibraryContent();
}

bool LibraryController::backendAvailable() const
{
    return m_backendAvailable;
}

QString LibraryController::libraryState() const
{
    if (!m_backendAvailable) {
        return QStringLiteral("backendUnavailable");
    }
    if (libraryEmpty()) {
        return QStringLiteral("empty");
    }
    return QStringLiteral("ready");
}

QVariantList LibraryController::currentSortRules() const
{
    return sortRuleVariantsFromModelRules(sortRulesForCurrentProjection());
}

#if SERIONA_HAS_BACKEND
void LibraryController::setCommandExecutor(CommandExecutor executor)
{
    m_commandExecutor = std::move(executor);
}

void LibraryController::setFolderSortExecutor(FolderSortExecutor executor)
{
    m_folderSortExecutor = std::move(executor);
}

void LibraryController::setScanExecutor(ScanExecutor executor)
{
    m_scanExecutor = std::move(executor);
    setBackendAvailable(static_cast<bool>(m_scanExecutor));
}

void LibraryController::setPlaylistTreeSnapshot(const seriona::scanner::PlaylistTreeSnapshot &snapshot)
{
    const bool previousLibraryEmpty = libraryEmpty();
    const QString previousLibraryState = libraryState();
    const bool previousBackendAvailable = m_backendAvailable;
    const QVector<QString> focusedFallbackChain = m_model.ancestorChainForNode(m_focusedNodeId);
    const QVector<QString> selectedFallbackChain = m_model.ancestorChainForNode(m_selectedBrowserNodeId);

    m_backendAvailable = true;
    m_model.setPlaylistTreeSnapshot(snapshot);
    reconcileBrowsingState(focusedFallbackChain, selectedFallbackChain);
    ++m_projectionGeneration;
    emit projectionGenerationChanged();
    if (previousBackendAvailable != m_backendAvailable) {
        emit backendAvailableChanged();
    }
    emitLibraryStateChanges(previousLibraryEmpty, previousLibraryState);
}

void LibraryController::applyPlayerStateSnapshot(const seriona::control::PlayerStateSnapshot &snapshot, bool forceReapply)
{
    const QString trackId = playingTrackIdFromSnapshot(snapshot);
    if (forceReapply && !trackId.isEmpty() && m_playingTrackId == trackId) {
        setPlayingTrackId(QString());
    }
    setPlayingTrackId(trackId);
}

void LibraryController::applyLibraryStateSnapshot(const seriona::control::LibraryStateSnapshot &snapshot)
{
    setBackendAvailable(true);
    const LibrarySnapshotViewState mapped = mapLibrarySnapshot(snapshot);
    setScanStatus(mapped.scanStatus);
    setScanProgress(mapped.scanProgress);
    setScanCounts(mapped.scannedSongCount, mapped.totalSongCount);
    setLastError(mapped.lastError);
    if (snapshot.libraryTree.has_value()) {
        setPlaylistTreeSnapshot(*snapshot.libraryTree);
    }
}

void LibraryController::applyFolderSortSetting(const seriona::control::FolderSortSetting &setting)
{
    const QString rootPath = normalizeFolderSortRootPath(fromBackendPath(setting.rootPath));
    const QString folderNodeId = toQString(setting.folderNodeId);
    const std::optional<QVector<LibraryModel::SortRule>> rules = modelSortRulesFromBackendRules(setting.rules);
    if (rootPath.isEmpty() || folderNodeId.isEmpty() || !rules.has_value()) {
        return;
    }

    rememberFolderSortRules(rootPath, folderNodeId, *rules);
    if (m_searchQuery.trimmed().isEmpty() && rootPath == m_savedRootPath && folderNodeId == m_currentFolderNodeId) {
        m_sortRules = *rules;
        applyBrowsingState();
        refreshCurrentFolderProjection();
        emit currentSortRulesChanged();
    }
}
#endif

void LibraryController::setSearchQuery(const QString &query)
{
    if (m_searchQuery == query) {
        return;
    }

    m_searchQuery = query;
    applyBrowsingState();
    emit searchQueryChanged();
}

void LibraryController::enterFolder(int index)
{
    const LibraryModel::Entry *entry = m_model.entryAt(index);
    if (entry == nullptr || entry->type != QStringLiteral("folder")) {
        return;
    }

    enterFolder(entry->nodeId);
}

void LibraryController::enterFolder(const QString &nodeId)
{
    const LibraryModel::Entry *entry = m_model.entryByNodeId(nodeId);
    if (entry == nullptr || !entry->isFolder) {
        return;
    }

    const bool previousCanGoBack = canGoBack();
    const bool folderChanged = m_currentFolderNodeId != nodeId;

    setCurrentFolderNodeId(nodeId, entry->name);
    setSelectedBrowserNodeId(nodeId);
    restoreSortRulesForCurrentFolder();
    applyBrowsingState();
    syncFolderStackToCurrentFolder();

    if (previousCanGoBack != canGoBack() || folderChanged) {
        emit canGoBackChanged();
    }
}

void LibraryController::goBack()
{
    if (!canGoBack()) {
        return;
    }

    if (!m_currentFolderNodeId.isEmpty()) {
        const bool previousCanGoBack = canGoBack();
        const QString previousFolderNodeId = m_currentFolderNodeId;
        const QString parentNodeId = m_model.parentNodeId(m_currentFolderNodeId);
        const QString rootNodeId = m_model.firstNodeId();

        if (!parentNodeId.isEmpty() && parentNodeId != rootNodeId) {
            enterFolder(parentNodeId);
        } else {
            setCurrentFolderNodeId(QString(), QStringLiteral("My Music"));
            m_sortRules.clear();
            m_activeFolderSortKey.clear();
            if (!previousFolderNodeId.isEmpty()) {
                setSelectedBrowserNodeId(previousFolderNodeId);
                requestScrollToNode(previousFolderNodeId);
            }
            applyBrowsingState();
            syncFolderStackToCurrentFolder();
            if (previousCanGoBack != canGoBack()) {
                emit canGoBackChanged();
            }
        }
        return;
    }
}

bool LibraryController::refresh()
{
    if (m_savedRootPath.isEmpty()) {
        setScanStatus(QStringLiteral("error"));
        setScanProgress(0);
        setLastError(tr("尚未选择曲库文件夹"));
        return false;
    }

#if SERIONA_HAS_BACKEND
    return requestScanForRoot(m_savedRootPath, seriona::scanner::ScanMode::Incremental);
#else
    return requestScanForRoot(m_savedRootPath);
#endif
}

bool LibraryController::forceRescan()
{
    if (m_savedRootPath.isEmpty()) {
        setScanStatus(QStringLiteral("error"));
        setScanProgress(0);
        setLastError(tr("尚未选择曲库文件夹"));
        return false;
    }

#if SERIONA_HAS_BACKEND
    // 显式 Full：后端 decideScanMode 对 Full 请求直接短路，不做增量决策，强制全量重扫
    return requestScanForRoot(m_savedRootPath, seriona::scanner::ScanMode::Full);
#else
    return requestScanForRoot(m_savedRootPath);
#endif
}

void LibraryController::clearSavedRootPath(const QString &message)
{
    setSavedRootPath(QString());
    setScanStatus(QStringLiteral("error"));
    setScanProgress(0);
    setLastError(message);
}

bool LibraryController::scanLibrary(const QUrl &rootUrl)
{
    const QString rootPath = localDirectoryPath(rootUrl);
    if (rootPath.isEmpty()) {
        setScanStatus(QStringLiteral("error"));
        setScanProgress(0);
        setLastError(tr("请选择有效的曲库文件夹"));
        return false;
    }

#if SERIONA_HAS_BACKEND
    return requestScanForRoot(rootPath, seriona::scanner::ScanMode::Incremental);
#else
    return requestScanForRoot(rootPath);
#endif
}

#if SERIONA_HAS_BACKEND
bool LibraryController::scanLibrary(const QUrl &rootUrl, seriona::scanner::ScanMode mode)
{
    const QString rootPath = localDirectoryPath(rootUrl);
    if (rootPath.isEmpty()) {
        setScanStatus(QStringLiteral("error"));
        setScanProgress(0);
        setLastError(tr("请选择有效的曲库文件夹"));
        return false;
    }

    return requestScanForRoot(rootPath, mode);
}

bool LibraryController::requestScanForRoot(const QString &rootPath, seriona::scanner::ScanMode mode)
#else
bool LibraryController::requestScanForRoot(const QString &rootPath)
#endif
{
    setScanStatus(QStringLiteral("running"));
    setScanProgress(0);
    setScanCounts(0, 0);
    setLastError(QString());

#if SERIONA_HAS_BACKEND
    if (!m_scanExecutor) {
        setBackendAvailable(false);
        setScanStatus(QStringLiteral("error"));
        setLastError(tr("后端扫描服务不可用"));
        return false;
    }

    const seriona::control::MediaControllerCommandResult result = m_scanExecutor(rootPath, mode);
    if (!result.accepted) {
        setScanStatus(QStringLiteral("error"));
        setLastError(result.message.empty() ? tr("后端拒绝扫描请求") : toQString(result.message));
        return false;
    }
#else
    setBackendAvailable(false);
    setScanStatus(QStringLiteral("error"));
    setLastError(tr("后端扫描服务不可用"));
    return false;
#endif

    setSavedRootPath(rootPath);
    restoreSortRulesForCurrentFolder();
    applyBrowsingState();
    return true;
}

void LibraryController::setScanStatus(const QString &status)
{
    if (m_scanStatus == status) {
        return;
    }

    m_scanStatus = status;
    emit scanStatusChanged();
}

void LibraryController::setScanProgress(int progress)
{
    progress = qBound(0, progress, 100);
    if (m_scanProgress == progress) {
        return;
    }

    m_scanProgress = progress;
    emit scanProgressChanged();
}

void LibraryController::setScanCounts(quint64 scanned, quint64 total)
{
    if (m_scannedSongCount == scanned && m_totalSongCount == total) {
        return;
    }

    m_scannedSongCount = scanned;
    m_totalSongCount = total;
    emit scanCountsChanged();
}

void LibraryController::setLastError(const QString &error)
{
    if (m_lastError == error) {
        return;
    }

    m_lastError = error;
    emit lastErrorChanged();
}

void LibraryController::setSavedRootPath(const QString &rootPath)
{
    const QString normalizedRootPath = normalizeFolderSortRootPath(rootPath);
    if (m_savedRootPath == normalizedRootPath) {
        return;
    }

    m_savedRootPath = normalizedRootPath;
    emit savedRootPathChanged();
}

void LibraryController::setBackendAvailable(bool available)
{
    if (m_backendAvailable == available) {
        return;
    }

    const bool previousLibraryEmpty = libraryEmpty();
    const QString previousLibraryState = libraryState();
    m_backendAvailable = available;
    emit backendAvailableChanged();
    emitLibraryStateChanges(previousLibraryEmpty, previousLibraryState);
}

void LibraryController::emitLibraryStateChanges(bool previousLibraryEmpty, const QString &previousLibraryState)
{
    if (previousLibraryEmpty != libraryEmpty()) {
        emit libraryEmptyChanged();
    }
    if (previousLibraryState != libraryState()) {
        emit libraryStateChanged();
    }
}

void LibraryController::playItem(int index)
{
    activateTrack(m_model.entryAt(index));
}

void LibraryController::playItem(const QString &nodeId)
{
    activateTrack(m_model.entryByNodeId(nodeId));
}

void LibraryController::locateCurrentSong()
{
    const QString playingNodeId = m_model.nodeIdForTrackId(m_playingTrackId);
    if (playingNodeId.isEmpty()) {
        return;
    }

    if (!showNodeInBrowserProjection(playingNodeId)) {
        return;
    }
    setSelectedBrowserNodeId(playingNodeId);
    requestScrollToNode(playingNodeId);
}

void LibraryController::activateTrack(const LibraryModel::Entry *entry)
{
    if (entry == nullptr || entry->isFolder || entry->trackId.isEmpty()) {
        return;
    }
    if (m_model.rowForNodeId(entry->nodeId) < 0) {
        return;
    }

#if SERIONA_HAS_BACKEND
    if (m_savedRootPath.isEmpty()) {
        setLastError(tr("尚未选择曲库文件夹，无法启动上下文播放"));
        return;
    }

    const seriona::control::TrackIdentity identity = trackIdentityForEntry(*entry);
    std::optional<std::vector<seriona::control::FolderSortRule>> sortRules = backendSortRulesFromModelRules(sortRulesForCurrentProjection());
    if (!sortRules.has_value()) {
        setLastError(tr("排序规则格式无效"));
        return;
    }

    const QString rootNodeId = m_model.firstNodeId();
    QString contextFolderNodeId = m_searchQuery.trimmed().isEmpty() ? m_currentFolderNodeId : QString();
    if (!m_searchQuery.trimmed().isEmpty()) {
        const QString parentNodeId = entry->parentNodeId;
        if (!parentNodeId.isEmpty() && parentNodeId != rootNodeId) {
            const LibraryModel::Entry *parentEntry = m_model.entryByNodeId(parentNodeId);
            if (parentEntry != nullptr && parentEntry->isFolder) {
                contextFolderNodeId = parentNodeId;
            }
        }
    }

    seriona::control::PlaybackContextDescriptor context;
    context.scope = contextFolderNodeId.isEmpty()
        ? seriona::control::PlaybackContextScope::Root
        : seriona::control::PlaybackContextScope::Folder;
    context.rootPath = toBackendPath(m_savedRootPath);
    context.folderNodeId = toStdString(contextFolderNodeId);
    context.anchorTrack = identity;
    context.sortRules = std::move(*sortRules);

    seriona::control::MediaControlCommand command;
    command.kind = seriona::control::MediaControlCommandKind::StartPlaybackFromContext;
    command.track = identity;
    command.playbackContext = std::move(context);
    submitCommand(command);
#endif
}

#if SERIONA_HAS_BACKEND
seriona::control::MediaControllerCommandResult LibraryController::submitCommand(const seriona::control::MediaControlCommand &command)
{
    if (!m_commandExecutor) {
        seriona::control::MediaControllerCommandResult result;
        result.accepted = false;
        result.code = seriona::control::MediaControllerErrorCode::ControllerStopped;
        result.message = "Frontend command executor is unavailable";
        return result;
    }

    return m_commandExecutor(command);
}
#endif

void LibraryController::clearSearch()
{
    if (m_searchQuery.isEmpty()) {
        return;
    }

    m_searchQuery.clear();
    if (!m_currentFolderNodeId.isEmpty()) {
        restoreSortRulesForCurrentFolder();
    }
    applyBrowsingState();
    emit searchQueryChanged();
}

void LibraryController::submitSearch()
{
    if (m_model.rowCount() <= 0 || m_visibleNodeCount <= 0) {
        return;
    }

    const QString nodeId = m_searchQuery.trimmed().isEmpty()
        ? m_model.firstVisibleNodeId()
        : m_model.firstVisibleMatchingNodeId();
    if (nodeId.isEmpty()) {
        return;
    }

    setSelectedBrowserNodeId(nodeId);
    requestScrollToNode(nodeId);
}

void LibraryController::selectBrowserNode(const QString &nodeId)
{
    setSelectedBrowserNodeId(nodeId);
}

void LibraryController::requestScrollToNode(const QString &nodeId)
{
    if (!m_model.containsNodeId(nodeId)) {
        return;
    }

    m_scrollRequest = nodeId;
    emit scrollRequestChanged();
}

int LibraryController::rowForNodeId(const QString &nodeId) const
{
    return m_model.rowForNodeId(nodeId);
}

QString LibraryController::currentFolderNodeId() const
{
    return m_currentFolderNodeId;
}

int LibraryController::projectionGeneration() const
{
    return m_projectionGeneration;
}

int LibraryController::folderStackDepth() const
{
    // 深度 = 当前文件夹祖先链（排除根）长度：根浏览为 0，
    // 与 QML 视图层级约定一致（0=根视图，1=第一级，…）。
    if (m_currentFolderNodeId.isEmpty()) {
        return 0;
    }
    QStringList chain = ancestorChainForNode(m_currentFolderNodeId);
    return qMax(0, chain.size());
}

QObject *LibraryController::projectionModelForNodeId(const QString &folderNodeId)
{
    const auto cacheIt = m_folderProjectionCache.constFind(folderNodeId);
    if (cacheIt != m_folderProjectionCache.cend()) {
        LibraryFolderProjectionModel *model = cacheIt.value();
        const QVector<LibraryModel::SortRule> rules = sortRulesForProjectionLevel(folderNodeId);
        if (!sortRulesEqual(model->sortRules(), rules)) {
            // 排序规则变化：同一源+同一文件夹 → setSource 走增量重建（行键差分 → rowsMoved
            // 等行操作 + 批量 dataChanged，模型对象身份不变，不 reset、视口不归零）。
            model->setSource(&m_model, folderNodeId, rules);
        }
        return model;
    }
    LibraryFolderProjectionModel *model = createFolderProjection(folderNodeId);
    m_folderProjectionCache.insert(folderNodeId, model);
    return model;
}

QStringList LibraryController::ancestorChainForNode(const QString &nodeId) const
{
    // 对 LibraryModel::ancestorChainForNode（:619-637，返回 [目标, 父, …, 根]）
    // 做"去根 + 倒序"等价变换：返回 [根向目标、排除根]（QML JS 调用，非绑定）。
    const QVector<QString> chain = m_model.ancestorChainForNode(nodeId);
    const QString rootNodeId = m_model.firstNodeId();
    QStringList result;
    result.reserve(chain.size());
    for (auto it = chain.crbegin(); it != chain.crend(); ++it) {
        if (*it == rootNodeId) {
            continue;
        }
        result.append(*it);
    }
    return result;
}

int LibraryController::projectionCacheSize() const
{
    return m_folderProjectionCache.size();
}

void LibraryController::locateNodeInFolderStack(const QString &nodeId)
{
    const LibraryModel::Entry *entry = m_model.entryByNodeId(nodeId);
    if (entry == nullptr) {
        return;
    }

    // 目标所在级 = 目标直接父文件夹的投影级；父为空或为根节点 → 根浏览（栈内只有根）。
    const QString rootNodeId = m_model.rootNodeId();
    const QString parentNodeId = m_model.parentNodeId(nodeId);
    const QString targetFolderNodeId = (!parentNodeId.isEmpty() && parentNodeId != rootNodeId) ? parentNodeId : QString();

    if (m_currentFolderNodeId == targetFolderNodeId) {
        return;
    }

    // 从根开始逐级进入：最终路径栈 [根, ..., 目标父级]，目标不在任何已建投影时进入其直接父级；
    // enterFolder 内部的路径栈同步会复用既有前缀级实例。
    if (!targetFolderNodeId.isEmpty()) {
        enterFolder(targetFolderNodeId);
    } else {
        while (canGoBack()) {
            goBack();
        }
    }
}

void LibraryController::syncFolderStackToCurrentFolder()
{
    // 缓存遍历：确保祖先链各级（含根键）缓存条目存在；不销毁任何模型
    // （get-or-create 内部会按 sortRulesForProjectionLevel 校验并原地重建规则漂移的条目）。
    projectionModelForNodeId(QString());
    if (!m_currentFolderNodeId.isEmpty()) {
        const QStringList chain = ancestorChainForNode(m_currentFolderNodeId);
        for (const QString &folderNodeId : chain) {
            projectionModelForNodeId(folderNodeId);
        }
    }
}

LibraryFolderProjectionModel *LibraryController::createFolderProjection(const QString &folderNodeId)
{
    auto *projection = new LibraryFolderProjectionModel(this);
    QQmlEngine::setObjectOwnership(projection, QQmlEngine::CppOwnership);
    projection->setSource(&m_model, folderNodeId, sortRulesForProjectionLevel(folderNodeId));
    return projection;
}

QVector<LibraryModel::SortRule> LibraryController::sortRulesForProjectionLevel(const QString &folderNodeId) const
{
    if (folderNodeId.isEmpty()) {
        return {};
    }
    if (folderNodeId == m_currentFolderNodeId) {
        return m_sortRules;
    }
    const QString key = folderSortKey(m_savedRootPath, folderNodeId);
    const auto rulesIt = m_savedFolderSortRules.constFind(key);
    return rulesIt == m_savedFolderSortRules.cend() ? QVector<LibraryModel::SortRule>{} : rulesIt.value();
}

void LibraryController::refreshCurrentFolderProjection()
{
    LibraryFolderProjectionModel *model = m_folderProjectionCache.value(m_currentFolderNodeId, nullptr);
    if (model == nullptr) {
        return;
    }
    const QVector<LibraryModel::SortRule> rules = sortRulesForProjectionLevel(m_currentFolderNodeId);
    if (!sortRulesEqual(model->sortRules(), rules)) {
        model->setSource(&m_model, m_currentFolderNodeId, rules);
    }
}

void LibraryController::setCurrentFolderNodeId(const QString &nodeId, const QString &folderName)
{
    const int previousDepth = folderStackDepth();
    const bool folderChanged = m_currentFolderNodeId != nodeId;
    const bool nameChanged = m_currentFolderName != folderName;

    m_currentFolderNodeId = nodeId;
    m_currentFolderName = folderName;

    if (folderChanged) {
        emit currentFolderNodeIdChanged();
    }
    if (nameChanged) {
        emit currentFolderNameChanged();
    }
    if (folderStackDepth() != previousDepth) {
        emit folderStackDepthChanged();
    }
}

void LibraryController::applySortRules(const QVariantList &rules)
{
    const std::optional<QVector<LibraryModel::SortRule>> parsedRules = sortRulesFromVariants(rules);
    if (!parsedRules.has_value()) {
        return;
    }

    if (!m_searchQuery.trimmed().isEmpty()) {
        return;
    }

    m_sortRules = *parsedRules;
    emit currentSortRulesChanged();
    refreshCurrentFolderProjection();

    if (m_currentFolderNodeId.isEmpty()) {
        setLastError(tr("请先进入文件夹后再保存排序规则"));
        applyBrowsingState();
        return;
    }
    if (m_savedRootPath.isEmpty()) {
        setLastError(tr("尚未选择曲库文件夹，无法保存排序规则"));
        applyBrowsingState();
        return;
    }

    if (persistCurrentFolderSortRules(m_sortRules)) {
        rememberFolderSortRules(m_savedRootPath, m_currentFolderNodeId, m_sortRules);
    }
    applyBrowsingState();
}

void LibraryController::applyBrowsingState()
{
    QVector<LibraryModel::SortRule> sortRules;
    if (m_searchQuery.trimmed().isEmpty()) {
        sortRules = sortRulesForCurrentProjection();
    } else {
        sortRules = QVector<LibraryModel::SortRule>{LibraryModel::SortRule{QStringLiteral("searchScore"), QStringLiteral("desc")}};
    }
    m_model.applyBrowsingState(m_focusedNodeId, m_playingTrackId, m_searchQuery, m_currentFolderNodeId, sortRules);
    updateVisibleNodeCount();
}

QVector<LibraryModel::SortRule> LibraryController::sortRulesForCurrentProjection() const
{
    return m_sortRules;
}

QString LibraryController::folderSortKey(const QString &rootPath, const QString &folderNodeId) const
{
    const QString normalizedRootPath = normalizeFolderSortRootPath(rootPath);
    if (normalizedRootPath.isEmpty() || folderNodeId.isEmpty()) {
        return QString();
    }
    return normalizedRootPath + QLatin1Char('\n') + folderNodeId;
}

QString LibraryController::currentFolderSortKey() const
{
    return folderSortKey(m_savedRootPath, m_currentFolderNodeId);
}

void LibraryController::restoreSortRulesForCurrentFolder()
{
    const QString key = currentFolderSortKey();
    m_activeFolderSortKey = key;
    if (key.isEmpty()) {
        if (!m_currentFolderNodeId.isEmpty() && m_savedRootPath.isEmpty()) {
            return;
        }
        m_sortRules.clear();
        emit currentSortRulesChanged();
        return;
    }

    const auto rulesIt = m_savedFolderSortRules.constFind(key);
    m_sortRules = rulesIt == m_savedFolderSortRules.cend() ? QVector<LibraryModel::SortRule>{} : rulesIt.value();
    emit currentSortRulesChanged();
}

void LibraryController::rememberFolderSortRules(const QString &rootPath, const QString &folderNodeId, const QVector<LibraryModel::SortRule> &rules)
{
    const QString key = folderSortKey(rootPath, folderNodeId);
    if (key.isEmpty()) {
        return;
    }

    m_savedFolderSortRules.insert(key, rules);
    if (rootPath == m_savedRootPath && folderNodeId == m_currentFolderNodeId) {
        m_activeFolderSortKey = key;
    }
}

bool LibraryController::persistCurrentFolderSortRules(const QVector<LibraryModel::SortRule> &rules)
{
#if SERIONA_HAS_BACKEND
    if (m_folderSortExecutor) {
        const seriona::control::MediaControllerCommandResult result = m_folderSortExecutor(
            m_savedRootPath,
            m_currentFolderNodeId,
            sortRuleVariantsFromModelRules(rules));
        if (!result.accepted) {
            setLastError(result.message.empty() ? tr("后端拒绝保存排序规则") : toQString(result.message));
            return false;
        }

        setLastError(QString());
        return true;
    }

    const std::optional<std::vector<seriona::control::FolderSortRule>> backendRules = backendSortRulesFromModelRules(rules);
    if (!backendRules.has_value()) {
        setLastError(tr("排序规则格式无效"));
        return false;
    }

    seriona::control::FolderSortSetting setting;
    setting.rootPath = toBackendPath(m_savedRootPath);
    setting.folderNodeId = toStdString(m_currentFolderNodeId);
    setting.rules = *backendRules;

    seriona::control::MediaControlCommand command;
    command.kind = seriona::control::MediaControlCommandKind::ApplyFolderSortRules;
    command.folderSortSetting = std::move(setting);

    const seriona::control::MediaControllerCommandResult result = submitCommand(command);
    if (!result.accepted) {
        setLastError(result.message.empty() ? tr("后端拒绝保存排序规则") : toQString(result.message));
        return false;
    }

    setLastError(QString());
    return true;
#else
    static_cast<void>(rules);
    setLastError(tr("后端排序设置服务不可用"));
    return false;
#endif
}

QVariantList LibraryController::sortRuleVariantsFromModelRules(const QVector<LibraryModel::SortRule> &rules) const
{
    QVariantList variants;
    variants.reserve(rules.size());
    for (const LibraryModel::SortRule &rule : rules) {
        QVariantMap ruleMap;
        ruleMap.insert(QStringLiteral("field"), rule.field);
        ruleMap.insert(QStringLiteral("order"), rule.order);
        variants.append(ruleMap);
    }
    return variants;
}

#if SERIONA_HAS_BACKEND
std::optional<std::vector<seriona::control::FolderSortRule>> LibraryController::backendSortRulesFromModelRules(const QVector<LibraryModel::SortRule> &rules) const
{
    std::vector<seriona::control::FolderSortRule> backendRules;
    backendRules.reserve(static_cast<std::size_t>(rules.size()));
    for (const LibraryModel::SortRule &rule : rules) {
        const std::optional<seriona::control::FolderSortField> field = backendSortField(rule.field);
        if (!field.has_value()) {
            return std::nullopt;
        }
        seriona::control::FolderSortRule backendRule;
        backendRule.field = *field;
        backendRule.direction = backendSortDirection(rule.order);
        backendRule.missingValuePolicy = seriona::control::FolderSortMissingValuePolicy::Last;
        backendRules.push_back(backendRule);
    }
    return backendRules;
}

std::optional<QVector<LibraryModel::SortRule>> LibraryController::modelSortRulesFromBackendRules(const std::vector<seriona::control::FolderSortRule> &rules) const
{
    QVector<LibraryModel::SortRule> modelRules;
    modelRules.reserve(static_cast<qsizetype>(rules.size()));
    for (const seriona::control::FolderSortRule &rule : rules) {
        const QString field = modelSortField(rule.field);
        if (field.isEmpty()) {
            return std::nullopt;
        }
        modelRules.append(LibraryModel::SortRule{field, modelSortOrder(rule.direction)});
    }
    return modelRules;
}
#endif

bool LibraryController::showNodeInBrowserProjection(const QString &nodeId)
{
    const LibraryModel::Entry *entry = m_model.entryByNodeId(nodeId);
    if (entry == nullptr) {
        return false;
    }

    const bool previousCanGoBack = canGoBack();
    const QString rootNodeId = m_model.firstNodeId();
    const QString parentNodeId = m_model.parentNodeId(nodeId);
    QString folderNodeId;

    if (!parentNodeId.isEmpty() && parentNodeId != rootNodeId) {
        folderNodeId = parentNodeId;
    }

    QString folderName = QStringLiteral("My Music");
    if (!folderNodeId.isEmpty()) {
        const LibraryModel::Entry *folderEntry = m_model.entryByNodeId(folderNodeId);
        if (folderEntry == nullptr || !folderEntry->isFolder) {
            return false;
        }
        folderName = folderEntry->name;
    }

    const bool folderChanged = m_currentFolderNodeId != folderNodeId;
    const bool searchChanged = !m_searchQuery.isEmpty();

    setCurrentFolderNodeId(folderNodeId, folderName);
    if (searchChanged) {
        m_searchQuery.clear();
    }
    restoreSortRulesForCurrentFolder();

    applyBrowsingState();
    syncFolderStackToCurrentFolder();

    if (searchChanged) {
        emit searchQueryChanged();
    }
    if (previousCanGoBack != canGoBack() || folderChanged) {
        emit canGoBackChanged();
    }

    return m_model.rowForNodeId(nodeId) >= 0;
}

void LibraryController::updateVisibleNodeCount()
{
    const int visibleNodeCount = m_model.visibleNodeCount();
    if (m_visibleNodeCount == visibleNodeCount) {
        return;
    }

    m_visibleNodeCount = visibleNodeCount;
    emit visibleNodeCountChanged();
}

void LibraryController::reconcileBrowsingState(const QVector<QString> &focusedFallbackChain, const QVector<QString> &selectedFallbackChain)
{
    const QString previousFocusedNodeId = m_focusedNodeId;
    const QString previousSelectedNodeId = m_selectedBrowserNodeId;
    const bool previousCanGoBack = canGoBack();
    bool focusedNeedsVisibleFallback = false;
    bool selectedNeedsVisibleFallback = false;
    bool folderWasCleared = false;

    if (!m_focusedNodeId.isEmpty() && !m_model.containsNodeId(m_focusedNodeId)) {
        m_focusedNodeId = firstExistingNode(focusedFallbackChain);
        focusedNeedsVisibleFallback = true;
    }
    if (m_focusedNodeId.isEmpty()) {
        m_focusedNodeId = m_model.firstNodeId();
        focusedNeedsVisibleFallback = true;
    }

    if (!m_selectedBrowserNodeId.isEmpty() && !m_model.containsNodeId(m_selectedBrowserNodeId)) {
        m_selectedBrowserNodeId = firstExistingNode(selectedFallbackChain);
        selectedNeedsVisibleFallback = true;
    }
    if (!m_currentFolderNodeId.isEmpty() && !m_model.containsNodeId(m_currentFolderNodeId)) {
        setCurrentFolderNodeId(QString(), QStringLiteral("My Music"));
        m_sortRules.clear();
        m_activeFolderSortKey.clear();
        folderWasCleared = true;
    }
    if (!folderWasCleared && !m_currentFolderNodeId.isEmpty()) {
        restoreSortRulesForCurrentFolder();
    }
    if (m_selectedBrowserNodeId.isEmpty()) {
        m_selectedBrowserNodeId = m_focusedNodeId;
        selectedNeedsVisibleFallback = focusedNeedsVisibleFallback;
    }

    applyBrowsingState();
    syncFolderStackToCurrentFolder();

	const QString firstVisibleNodeId = m_model.firstVisibleNodeId();
	const QString rootNodeId = m_model.firstNodeId();
	if (!firstVisibleNodeId.isEmpty()) {
		if (focusedNeedsVisibleFallback && (folderWasCleared || m_focusedNodeId != rootNodeId) && m_model.rowForNodeId(m_focusedNodeId) < 0) {
			m_focusedNodeId = firstVisibleNodeId;
			m_model.setFocusedNodeId(m_focusedNodeId);
		}
		if (selectedNeedsVisibleFallback && (folderWasCleared || m_selectedBrowserNodeId != rootNodeId) && m_model.rowForNodeId(m_selectedBrowserNodeId) < 0) {
			m_selectedBrowserNodeId = firstVisibleNodeId;
		}
	}

    if (previousFocusedNodeId != m_focusedNodeId) {
        emit focusedNodeIdChanged();
    }
    if (previousSelectedNodeId != m_selectedBrowserNodeId) {
        emit selectedBrowserNodeIdChanged();
    }
    if (previousCanGoBack != canGoBack()) {
        emit canGoBackChanged();
    }
}

QString LibraryController::firstExistingNode(const QVector<QString> &nodeIds) const
{
    for (const QString &nodeId : nodeIds) {
        if (m_model.containsNodeId(nodeId)) {
            return nodeId;
        }
    }
    return m_model.firstNodeId();
}

std::optional<LibraryModel::SortRule> LibraryController::sortRuleFromVariant(const QVariant &ruleVar) const
{
    if (!ruleVar.canConvert<QVariantMap>()) {
        return std::nullopt;
    }

    const QVariantMap ruleMap = ruleVar.toMap();
    const std::optional<QString> field = canonicalSortField(ruleMap.value(QStringLiteral("field")).toString());
    const std::optional<QString> order = canonicalSortOrder(ruleMap.value(QStringLiteral("order")).toString());
    if (!field.has_value() || !order.has_value()) {
        return std::nullopt;
    }

    return LibraryModel::SortRule{*field, *order};
}

std::optional<QVector<LibraryModel::SortRule>> LibraryController::sortRulesFromVariants(const QVariantList &rules) const
{
    QVector<LibraryModel::SortRule> parsedRules;
    parsedRules.reserve(rules.size());
    for (const QVariant &ruleVar : rules) {
        const std::optional<LibraryModel::SortRule> parsedRule = sortRuleFromVariant(ruleVar);
        if (!parsedRule.has_value()) {
            return std::nullopt;
        }
        parsedRules.append(*parsedRule);
    }
    return parsedRules;
}

}
