#pragma once

#include <QAbstractListModel>
#include <QQmlEngine>
#include <QVariantList>
#include <QVector>

#include <chrono>

#ifndef SERIONA_HAS_BACKEND
#define SERIONA_HAS_BACKEND 0
#endif

#if SERIONA_HAS_BACKEND
#include "seriona/control/control_contracts.h"
#endif

namespace Seriona::App {

// 歌词列表模型。数据来源是控制层的 `TrackLyricsSnapshot` 订阅（第 6 路订阅）：
// 后端已给出每行的 original / translation，前端只做「原样渲染 + 行推进」，
// 不再持有任何按分隔符切分的逻辑，也没有兜底切分分支。
class LyricsModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY currentIndexChanged)
    Q_PROPERTY(qreal playbackPosition READ playbackPosition WRITE setPlaybackPosition NOTIFY playbackPositionChanged)
    Q_PROPERTY(bool showTranslation READ showTranslation WRITE setShowTranslation NOTIFY showTranslationChanged)
    QML_ELEMENT

public:
    enum Role {
        RawLineRole = Qt::UserRole + 1,
        DisplayLineRole,
        TranslationRole,
        CurrentRole,
        TimestampRole,
        // W3 行级纠错（D14 菜单）：既有 5 个角色名/值逐字不变，新角色只追加在末尾。
        // manualOverride = 本行是否命中 manual（用户手工纠错）；
        // autoOriginal/autoTranslation = 被 manual 覆盖【之前】的自动判定结果，
        // 仅 manualOverride 为 true 时有意义（未覆盖时后端留空）。
        ManualOverrideRole,
        AutoOriginalRole,
        AutoTranslationRole
    };
    Q_ENUM(Role)

    explicit LyricsModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    int currentIndex() const;
    void setCurrentIndex(int index);

    qreal playbackPosition() const;
    void setPlaybackPosition(qreal position);

    bool showTranslation() const;
    void setShowTranslation(bool showTranslation);

#if SERIONA_HAS_BACKEND
    // 订阅回调的唯一落地入口：整份替换模型内容。行推进（当前行索引、播放位置同步）语义与旧的
    // applyPlayerStateSnapshot 相同；内容去重谓词已由 timestamp + text 扩展为
    // timestamp + text + original + translation（数据来源改为后端快照后，仅译文变化也必须被识别）。
    void applyTrackLyricsSnapshot(const seriona::control::TrackLyricsSnapshot &snapshot);
#endif

    Q_INVOKABLE void selectLyric(int index);
    Q_INVOKABLE void toggleTranslation();

    // 供 QML 侧切歌动画快照使用：返回全部行
    // {rawLine, displayLine, translation, timestampSec, manualOverride, autoOriginal, autoTranslation}
    // （QAbstractListModel 的 rowCount()/data() 非 Q_INVOKABLE，QML 无法直接调用）。
    // 既有 3 个键名（displayLine/translation/timestampSec）保持不变，其余为追加。
    Q_INVOKABLE QVariantList lines() const;

signals:
    void currentIndexChanged();
    void playbackPositionChanged();
    void showTranslationChanged();

private:
    struct Line {
        std::chrono::milliseconds timestamp{0};
        // 快照 text（cleanLine 之后的清洗行）；RawLineRole 原样透传
        QString text;
        // 快照 original（DisplayLineRole）
        QString original;
        // 快照 translation（空串 = 显式「此行无译文」，TranslationRole 原样返回）
        QString translation;
        // 快照 manualOverride：本行是否命中 manual（用户手工纠错）
        bool manualOverride = false;
        // 快照 autoOriginal/autoTranslation：被 manual 覆盖之前的自动判定结果
        QString autoOriginal;
        QString autoTranslation;
    };

    void clearLyrics();
    void replaceLyrics(QVector<Line> lines, bool hasTimedLyrics);
    int currentIndexForPlaybackPosition() const;
    void syncCurrentIndexToPlaybackPosition();
    void emitAllLyricsChanged(const QList<int> &roles);
    void emitCurrentRoleChanged(int index);

    QVector<Line> m_lines;
    int m_currentIndex = 0;
    qreal m_playbackPosition = 0.0;
    bool m_hasTimedLyrics = false;
    bool m_showTranslation = true;
};

}
