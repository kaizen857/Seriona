#pragma once

#include "library_model.h"

#include <QAbstractListModel>
#include <QHash>
#include <QString>
#include <QVector>

namespace Seriona::App {

// 每级文件夹的独立投影模型（B'' 架构模型层部分）：
// Sidebar 每个文件夹级别的视图绑定各自独立的 LibraryFolderProjectionModel 实例，
// 视图存活期间模型实例不被替换 → 滚动位置天然保留（Qt 无"reset 后恢复滚动位置"契约，
// 因此不做恢复，而是让视图不换模型）。主模型的投影能力（applyBrowsingState /
// setProjectionNodeIds）保留给搜索、曲库页等其他使用者。
//
// 数据 = folderNodeId 的直接子节点投影（过滤空 nodeId、跳过 rootNodeId、跳过重复项，
// 过滤规则与 LibraryModel::setProjectionNodeIds 完全一致）；排序复用
// LibraryModel::sortedProjectionNodeIds（行为与主模型投影一致，现有测试已覆盖语义）。
//
// 生命周期同步：主模型 treeChanged → 全量重建（reset + revision 递增，重建后视图位置
// 归零属"内容变化"语义）；主模型 playingTrackIdChanged / focusedNodeIdChanged →
// 仅对投影内存在的行发 dataChanged（IsPlaying / IsFocused role）。
class LibraryFolderProjectionModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int projectionRevision READ projectionRevision NOTIFY projectionRevisionChanged)

public:
    using Entry = LibraryModel::Entry;
    using SortRule = LibraryModel::SortRule;

    explicit LibraryFolderProjectionModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    // 投影行内的 Entry 指针（行号越界返回 nullptr）；测试与控制器只读使用。
    const Entry *entryAt(int row) const;
    // 节点在本投影中的行号（不在投影内返回 -1）；QML 锚点恢复按 nodeId 定位行。
    Q_INVOKABLE int rowForNodeId(const QString &nodeId) const;

    // 本投影对应的文件夹节点 id（空 = 根投影，投影 rootProjectionNodeIds）。
    QString folderNodeId() const;

    // 本投影当前生效的排序规则（setSource 传入）。
    QVector<SortRule> sortRules() const;

    // 投影 revision：每次完整重建（setSource 或主模型树变化）后递增并发出变化信号。
    int projectionRevision() const;

    // 设置数据源与文件夹；每次重建时 ++m_projectionRevision 并发 projectionRevisionChanged。
    // folderNodeId 为空表示根投影（投影主模型 rootProjectionNodeIds）。
    void setSource(LibraryModel *source, const QString &folderNodeId, const QVector<SortRule> &sortRules);

signals:
    void projectionRevisionChanged();

private:
    void disconnectSource();
    void rebuildFromSource();
    void onSourcePlayingTrackChanged();
    void onSourceFocusedNodeChanged();

    LibraryModel *m_source = nullptr;
    QString m_folderNodeId;
    QVector<SortRule> m_sortRules;
    QVector<Entry> m_entries;
    QHash<QString, int> m_rowByNodeId;
    int m_projectionRevision = 0;
};

}
