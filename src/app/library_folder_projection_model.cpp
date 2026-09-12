#include "library_folder_projection_model.h"

#include "row_diff.h"

#include <QSet>

#include <utility>

namespace Seriona::App {

namespace {

// 比较 role 暴露字段（data() 可达的全部角色）；fileName/durationValue/discNumber/
// trackNumber/createdDate 不经角色暴露，忽略。用于增量重建的更新检测。
bool roleFieldsEqual(const LibraryModel::Entry &left, const LibraryModel::Entry &right)
{
    return left.type == right.type
        && left.name == right.name
        && left.title == right.title
        && left.artist == right.artist
        && left.album == right.album
        && left.parentName == right.parentName
        && left.songCount == right.songCount
        && left.duration == right.duration
        && left.format == right.format
        && left.sampleRate == right.sampleRate
        && left.bitDepth == right.bitDepth
        && left.nodeId == right.nodeId
        && left.trackId == right.trackId
        && left.isFolder == right.isFolder
        && left.isPlaying == right.isPlaying
        && left.isFocused == right.isFocused
        && left.parentNodeId == right.parentNodeId
        && left.artworkSource == right.artworkSource
        && left.year == right.year;
}

}

LibraryFolderProjectionModel::LibraryFolderProjectionModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int LibraryFolderProjectionModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }

    return m_entries.size();
}

QVariant LibraryFolderProjectionModel::data(const QModelIndex &index, int role) const
{
    const Entry *entry = entryAt(index.row());
    if (entry == nullptr) {
        return {};
    }

    // 角色取值与 LibraryModel 完全一致（QML 侧两种模型可互换绑定）。
    switch (role) {
    case LibraryModel::TypeRole:
        return entry->type;
    case LibraryModel::NameRole:
        return entry->name;
    case LibraryModel::TitleRole:
        return entry->title;
    case LibraryModel::ArtistRole:
        return entry->artist;
    case LibraryModel::AlbumRole:
        return entry->album;
    case LibraryModel::ParentNameRole:
        return entry->parentName;
    case LibraryModel::SongCountRole:
        return entry->songCount;
    case LibraryModel::DurationRole:
        return entry->duration;
    case LibraryModel::FormatRole:
        return entry->format;
    case LibraryModel::SampleRateRole:
        return entry->sampleRate;
    case LibraryModel::BitDepthRole:
        return entry->bitDepth;
    case LibraryModel::NodeIdRole:
        return entry->nodeId;
    case LibraryModel::TrackIdRole:
        return entry->trackId;
    case LibraryModel::IsFolderRole:
        return entry->isFolder;
    case LibraryModel::IsPlayingRole:
        return entry->isPlaying;
    case LibraryModel::IsFocusedRole:
        return entry->isFocused;
    case LibraryModel::ParentNodeIdRole:
        return entry->parentNodeId;
    case LibraryModel::ArtworkSourceRole:
        return entry->artworkSource;
    case LibraryModel::YearRole:
        return entry->year.has_value() ? QVariant{static_cast<qint64>(*entry->year)} : QVariant{};
    default:
        return {};
    }
}

QHash<int, QByteArray> LibraryFolderProjectionModel::roleNames() const
{
    // 与 LibraryModel::roleNames 完全一致（键为 LibraryModel::Role 数值）。
    return {{LibraryModel::TypeRole, "type"},
            {LibraryModel::NameRole, "name"},
            {LibraryModel::TitleRole, "title"},
            {LibraryModel::ArtistRole, "artist"},
            {LibraryModel::AlbumRole, "album"},
            {LibraryModel::ParentNameRole, "parentName"},
            {LibraryModel::SongCountRole, "songCount"},
            {LibraryModel::DurationRole, "duration"},
            {LibraryModel::FormatRole, "format"},
            {LibraryModel::SampleRateRole, "sampleRate"},
            {LibraryModel::BitDepthRole, "bitDepth"},
            {LibraryModel::NodeIdRole, "nodeId"},
            {LibraryModel::TrackIdRole, "trackId"},
            {LibraryModel::IsFolderRole, "isFolder"},
            {LibraryModel::IsPlayingRole, "isPlaying"},
            {LibraryModel::IsFocusedRole, "isFocused"},
            {LibraryModel::ParentNodeIdRole, "parentNodeId"},
            {LibraryModel::ArtworkSourceRole, "artworkSource"},
            {LibraryModel::YearRole, "year"}};
}

const LibraryFolderProjectionModel::Entry *LibraryFolderProjectionModel::entryAt(int row) const
{
    if (row < 0 || row >= m_entries.size()) {
        return nullptr;
    }

    return &m_entries.at(row);
}

int LibraryFolderProjectionModel::rowForNodeId(const QString &nodeId) const
{
    return m_rowByNodeId.value(nodeId, -1);
}

QString LibraryFolderProjectionModel::folderNodeId() const
{
    return m_folderNodeId;
}

QVector<LibraryFolderProjectionModel::SortRule> LibraryFolderProjectionModel::sortRules() const
{
    return m_sortRules;
}

int LibraryFolderProjectionModel::projectionRevision() const
{
    return m_projectionRevision;
}

void LibraryFolderProjectionModel::setSource(LibraryModel *source,
                                             const QString &folderNodeId,
                                             const QVector<SortRule> &sortRules)
{
    // 同一数据源 + 同一文件夹：仅排序规则变化（规则漂移/后端 FolderSortSetting 事件/显式排序）
    // → 增量重建（行键差分 → rowsMoved 等行操作 + 批量 dataChanged），不 disconnect、不 reset，
    // 视口不归零。规则相同重复调用 = 零行操作 + 零模型信号，仅 revision 递增（幂等）。
    if (m_source == source && m_folderNodeId == folderNodeId) {
        m_sortRules = sortRules;
        rebuildFromSource();
        return;
    }

    // 首建 / 数据源切换 / 文件夹切换：保持 reset 语义（视图整体换源）。
    disconnectSource();
    m_source = source;
    m_folderNodeId = folderNodeId;
    m_sortRules = sortRules;
    if (m_source != nullptr) {
        connect(m_source, &LibraryModel::treeChanged, this, &LibraryFolderProjectionModel::rebuildFromSource);
        connect(m_source, &LibraryModel::playingTrackIdChanged, this, &LibraryFolderProjectionModel::onSourcePlayingTrackChanged);
        connect(m_source, &LibraryModel::focusedNodeIdChanged, this, &LibraryFolderProjectionModel::onSourceFocusedNodeChanged);
    }
    resetFromSource();
}

void LibraryFolderProjectionModel::disconnectSource()
{
    if (m_source == nullptr) {
        return;
    }

    disconnect(m_source, &LibraryModel::treeChanged, this, &LibraryFolderProjectionModel::rebuildFromSource);
    disconnect(m_source, &LibraryModel::playingTrackIdChanged, this, &LibraryFolderProjectionModel::onSourcePlayingTrackChanged);
    disconnect(m_source, &LibraryModel::focusedNodeIdChanged, this, &LibraryFolderProjectionModel::onSourceFocusedNodeChanged);
}

LibraryFolderProjectionModel::ProjectionData LibraryFolderProjectionModel::buildProjection() const
{
    QVector<QString> nodeIds;
    if (m_source != nullptr) {
        nodeIds = m_folderNodeId.isEmpty() ? m_source->rootProjectionNodeIds() : m_source->childNodeIds(m_folderNodeId);
        nodeIds = m_source->sortedProjectionNodeIds(std::move(nodeIds), m_sortRules);
    }

    const QString rootNodeId = m_source != nullptr ? m_source->rootNodeId() : QString();
    const QString focusedNodeId = m_source != nullptr ? m_source->focusedNodeId() : QString();
    const QString playingNodeId = m_source != nullptr ? m_source->nodeIdForTrackId(m_source->playingTrackId()) : QString();

    ProjectionData projected;
    projected.entries.reserve(nodeIds.size());
    projected.keys.reserve(nodeIds.size());
    QSet<QString> projectedNodeIds;

    // 过滤规则与 LibraryModel::setProjectionNodeIds 完全一致：
    // 跳过空 nodeId、跳过 rootNodeId、跳过重复项、跳过未知节点。
    for (const QString &nodeId : nodeIds) {
        if (nodeId.isEmpty() || nodeId == rootNodeId || projectedNodeIds.contains(nodeId)) {
            continue;
        }
        const Entry *sourceEntry = m_source != nullptr ? m_source->entryByNodeId(nodeId) : nullptr;
        if (sourceEntry == nullptr) {
            continue;
        }

        Entry entry = *sourceEntry;
        entry.isFocused = entry.nodeId == focusedNodeId;
        entry.isPlaying = !playingNodeId.isEmpty() && entry.nodeId == playingNodeId;
        projected.keys.append(nodeId);
        projected.byKey.insert(nodeId, entry);
        projected.entries.append(entry);
        projectedNodeIds.insert(nodeId);
    }

    return projected;
}

void LibraryFolderProjectionModel::resetFromSource()
{
    ProjectionData projected = buildProjection();

    // reset 语义仅用于 setSource（首建/数据源切换/文件夹切换）；treeChanged 走增量路径。
    beginResetModel();
    m_entries = std::move(projected.entries);
    m_rowByNodeId.clear();
    m_rowByNodeId.reserve(projected.keys.size());
    for (int row = 0; row < projected.keys.size(); ++row) {
        m_rowByNodeId.insert(projected.keys.at(row), row);
    }
    endResetModel();
    ++m_projectionRevision;
    emit projectionRevisionChanged();
}

void LibraryFolderProjectionModel::rebuildFromSource()
{
    ProjectionData projected = buildProjection();
    const QVector<QString> &newKeys = projected.keys;

    QVector<QString> oldKeys;
    oldKeys.reserve(m_entries.size());
    for (const Entry &entry : std::as_const(m_entries)) {
        oldKeys.append(entry.nodeId);
    }

    // 1) 更新检测（行操作前，旧行内容仍有效）：只比较存活键的角色字段；
    //    新键由 Insert 信号承载、删除键由 Remove 信号承载，均不参与 dataChanged。
    QSet<QString> changedKeys;
    if (!m_entries.isEmpty() && !newKeys.isEmpty()) {
        QHash<QString, const Entry *> oldEntryByKey;
        oldEntryByKey.reserve(m_entries.size());
        for (const Entry &entry : std::as_const(m_entries)) {
            oldEntryByKey.insert(entry.nodeId, &entry);
        }
        for (const QString &key : newKeys) {
            const auto oldIt = oldEntryByKey.constFind(key);
            if (oldIt == oldEntryByKey.cend()) {
                continue;
            }
            const auto newIt = projected.byKey.constFind(key);
            Q_ASSERT(newIt != projected.byKey.cend());
            if (!roleFieldsEqual(*oldIt.value(), newIt.value())) {
                changedKeys.insert(key);
            }
        }
    }

    // 2) 行操作：按 diffRowKeys 发射顺序应用（删除段降序 → 放置扫描顺序）；
    //    所有操作在同一事件循环内完成，视图合并为一次布局，contentY 不被赋值。
    const QVector<RowOp> ops = diffRowKeys(oldKeys, newKeys);
    for (const RowOp &op : ops) {
        switch (op.kind) {
        case RowOp::Kind::Remove:
            beginRemoveRows(QModelIndex(), op.first, op.first + op.count - 1);
            m_entries.remove(op.first, op.count);
            endRemoveRows();
            break;
        case RowOp::Kind::Insert:
            beginInsertRows(QModelIndex(), op.first, op.first + op.count - 1);
            for (int row = op.first; row < op.first + op.count; ++row) {
                m_entries.insert(row, projected.byKey.value(newKeys.at(row)));
            }
            endInsertRows();
            break;
        case RowOp::Kind::Move: {
            // 向左移动：同父目标坐标直接取 to（src < dest 时才需 +1）。
            const int destinationChild = (op.first < op.to) ? op.to + 1 : op.to;
            if (beginMoveRows(QModelIndex(), op.first, op.first, QModelIndex(), destinationChild)) {
                m_entries.move(op.first, op.to);
                endMoveRows();
            } else {
                // 防御回退（新键唯一且前缀已放置时不会触发）：移除 + 插入等价替换。
                beginRemoveRows(QModelIndex(), op.first, op.first);
                const Entry moved = m_entries.at(op.first);
                m_entries.remove(op.first, 1);
                endRemoveRows();
                beginInsertRows(QModelIndex(), op.to, op.to);
                m_entries.insert(op.to, moved);
                endInsertRows();
            }
            break;
        }
        }
    }

    // 3) 全量覆写：行内容与本次构建结果对齐（isPlaying/isFocused 等派生字段同步源）。
    for (int row = 0; row < newKeys.size(); ++row) {
        m_entries[row] = projected.byKey.value(newKeys.at(row));
    }

    // 4) 重建 nodeId → 行号索引（QML rowForNodeId 定位使用）。
    m_rowByNodeId.clear();
    m_rowByNodeId.reserve(newKeys.size());
    for (int row = 0; row < newKeys.size(); ++row) {
        m_rowByNodeId.insert(newKeys.at(row), row);
    }

    // 5) 批量 dataChanged：命中最终行号，连续变化行合并为一段；
    //    空 roles = 全角色（与 LibraryModel 语义一致）。
    int changedRunStart = -1;
    for (int row = 0; row < newKeys.size(); ++row) {
        const bool changed = changedKeys.contains(newKeys.at(row));
        if (changed && changedRunStart < 0) {
            changedRunStart = row;
        }
        const bool runEnded = !changed || row == newKeys.size() - 1;
        if (runEnded && changedRunStart >= 0) {
            const int last = changed ? row : row - 1;
            emit dataChanged(index(changedRunStart, 0), index(last, 0), {});
            changedRunStart = -1;
        }
    }

    ++m_projectionRevision;
    emit projectionRevisionChanged();
}

void LibraryFolderProjectionModel::onSourcePlayingTrackChanged()
{
    if (m_source == nullptr || m_entries.isEmpty()) {
        return;
    }

    const QString playingNodeId = m_source->nodeIdForTrackId(m_source->playingTrackId());
    for (int row = 0; row < m_entries.size(); ++row) {
        Entry &entry = m_entries[row];
        const bool playing = !playingNodeId.isEmpty() && entry.nodeId == playingNodeId;
        if (entry.isPlaying != playing) {
            entry.isPlaying = playing;
            const QModelIndex changedIndex = index(row, 0);
            emit dataChanged(changedIndex, changedIndex, QList<int>{LibraryModel::IsPlayingRole});
        }
    }
}

void LibraryFolderProjectionModel::onSourceFocusedNodeChanged()
{
    if (m_source == nullptr || m_entries.isEmpty()) {
        return;
    }

    const QString focusedNodeId = m_source->focusedNodeId();
    for (int row = 0; row < m_entries.size(); ++row) {
        Entry &entry = m_entries[row];
        const bool focused = !focusedNodeId.isEmpty() && entry.nodeId == focusedNodeId;
        if (entry.isFocused != focused) {
            entry.isFocused = focused;
            const QModelIndex changedIndex = index(row, 0);
            emit dataChanged(changedIndex, changedIndex, QList<int>{LibraryModel::IsFocusedRole});
        }
    }
}

}
