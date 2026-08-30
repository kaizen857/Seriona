#include "playback_controller.h"

#if SERIONA_HAS_BACKEND
#include "seriona/control/control_contracts.h"
#endif

#include <QUrl>
#include <QtMath>

#include <chrono>
#include <cmath>
#include <optional>
#include <utility>

namespace Seriona::App {

namespace {

bool sameCurrentTrackViewState(const CurrentTrackViewState &left, const CurrentTrackViewState &right)
{
    return left.trackId == right.trackId
        && left.nodeId == right.nodeId
        && left.title == right.title
        && left.artist == right.artist
        && left.album == right.album
        && left.artworkPath == right.artworkPath
        && left.preferredArtworkPath == right.preferredArtworkPath
        && left.fallbackThumbnailPath == right.fallbackThumbnailPath
        && qAbs(left.durationSeconds - right.durationSeconds) < 0.001
        && left.audioFormat == right.audioFormat
        && left.audioSampleRate == right.audioSampleRate
        && left.audioBitDepth == right.audioBitDepth
        && left.audioChannels == right.audioChannels;
}

#if SERIONA_HAS_BACKEND
constexpr int kTimelineSmoothingIntervalMs = 100;

qreal secondsFromMilliseconds(std::chrono::milliseconds value);

qreal secondsFromMilliseconds(std::chrono::milliseconds value)
{
    return static_cast<qreal>(value.count()) / 1000.0;
}

std::optional<std::chrono::milliseconds> millisecondsFromSeconds(qreal seconds)
{
    if (!std::isfinite(seconds)) {
        return std::nullopt;
    }

    return std::chrono::milliseconds{qRound64(qMax(0.0, seconds) * 1000.0)};
}

std::optional<float> volumeFromQml(qreal volume)
{
    if (!std::isfinite(volume)) {
        return std::nullopt;
    }

    return static_cast<float>(qBound(0.0, volume, 1.0));
}

seriona::control::RepeatMode repeatModeFromIndex(int repeatMode)
{
    switch (qBound(0, repeatMode, 2)) {
    case 1:
        return seriona::control::RepeatMode::All;
    case 2:
        return seriona::control::RepeatMode::One;
    case 0:
    default:
        return seriona::control::RepeatMode::Off;
    }
}

std::chrono::milliseconds elapsedSince(std::chrono::steady_clock::time_point sampledAt)
{
    if (sampledAt == std::chrono::steady_clock::time_point{}) {
        return std::chrono::milliseconds{0};
    }

    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    if (now <= sampledAt) {
        return std::chrono::milliseconds{0};
    }

    return std::chrono::duration_cast<std::chrono::milliseconds>(now - sampledAt);
}
#endif

}

PlaybackController::PlaybackController(QObject *parent)
    : PlaybackController(decodeGradientPalette, parent)
{
}

PlaybackController::PlaybackController(ArtworkPaletteWorker::Decoder decoder, QObject *parent)
    : QObject(parent)
    , m_paletteWorker(std::move(decoder))
{
    connect(&m_paletteWorker, &ArtworkPaletteWorker::paletteReady, this, &PlaybackController::applyPaletteResult);
#if SERIONA_HAS_BACKEND
    m_timelineTimer.setInterval(kTimelineSmoothingIntervalMs);
    m_timelineTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_timelineTimer, &QTimer::timeout, this, [this] {
        static_cast<void>(updateSmoothedTimelinePosition());
    });
#endif
}

PlaybackController::~PlaybackController()
{
    m_paletteWorker.shutdown();
}

bool PlaybackController::ready() const
{
    return true;
}

QString PlaybackController::capability() const
{
    return m_capability;
}

bool PlaybackController::isPlaying() const
{
    return m_isPlaying;
}

void PlaybackController::setPlaying(bool playing)
{
#if SERIONA_HAS_BACKEND
    seriona::control::MediaControlCommand command;
    command.kind = playing ? seriona::control::MediaControlCommandKind::Play : seriona::control::MediaControlCommandKind::Pause;
    submitCommand(command);
#else
    Q_UNUSED(playing)
#endif
}

void PlaybackController::applyPlaying(bool playing)
{
    if (m_isPlaying == playing) {
        return;
    }

    m_isPlaying = playing;
    emit isPlayingChanged();
}

qreal PlaybackController::currentPosition() const
{
    return m_currentPosition;
}

void PlaybackController::setCurrentPosition(qreal position)
{
    seek(position);
}

void PlaybackController::applyCurrentPosition(qreal position)
{
    position = qMax(0.0, position);
    if (m_totalDuration > 0.0) {
        position = qMin(position, m_totalDuration);
    }
    if (qAbs(m_currentPosition - position) < 0.001) {
        return;
    }

    m_currentPosition = position;
    emit currentPositionChanged();
    emit durationDisplayChanged();
}

qreal PlaybackController::totalDuration() const
{
    return m_totalDuration;
}

void PlaybackController::setTotalDuration(qreal duration)
{
    applyTotalDuration(duration);
}

void PlaybackController::applyTotalDuration(qreal duration)
{
    duration = qMax(0.0, duration);
    if (qAbs(m_totalDuration - duration) < 0.001) {
        return;
    }

    m_totalDuration = duration;
    if (m_totalDuration > 0.0 && m_currentPosition > m_totalDuration) {
        m_currentPosition = m_totalDuration;
        emit currentPositionChanged();
    }
    emit totalDurationChanged();
    emit durationDisplayChanged();
}

qreal PlaybackController::volume() const
{
    return m_volume;
}

void PlaybackController::setVolume(qreal volume)
{
#if SERIONA_HAS_BACKEND
    seriona::control::MediaControlCommand command;
    command.kind = seriona::control::MediaControlCommandKind::SetVolume;
    command.volume = volumeFromQml(volume);
    submitCommand(command);
#else
    Q_UNUSED(volume)
#endif
}

void PlaybackController::applyVolume(qreal volume)
{
    volume = clamp(volume, 0.0, 1.0);
    if (qAbs(m_volume - volume) < 0.001) {
        return;
    }

    m_volume = volume;
    emit volumeChanged();
}

bool PlaybackController::isShuffle() const
{
    return m_isShuffle;
}

void PlaybackController::setShuffle(bool shuffle)
{
#if SERIONA_HAS_BACKEND
    seriona::control::MediaControlCommand command;
    command.kind = seriona::control::MediaControlCommandKind::SetShuffle;
    command.shuffle = shuffle;
    submitCommand(command);
#else
    Q_UNUSED(shuffle)
#endif
}

void PlaybackController::applyShuffle(bool shuffle)
{
    if (m_isShuffle == shuffle) {
        return;
    }

    m_isShuffle = shuffle;
    emit isShuffleChanged();
}

int PlaybackController::repeatMode() const
{
    return m_repeatMode;
}

void PlaybackController::setRepeatMode(int repeatMode)
{
#if SERIONA_HAS_BACKEND
    seriona::control::MediaControlCommand command;
    command.kind = seriona::control::MediaControlCommandKind::SetRepeatMode;
    command.repeatMode = repeatModeForIndex(repeatMode);
    submitCommand(command);
#else
    Q_UNUSED(repeatMode)
#endif
}

void PlaybackController::applyRepeatMode(int repeatMode)
{
    repeatMode = qBound(0, repeatMode, 2);
    if (m_repeatMode == repeatMode) {
        return;
    }

    m_repeatMode = repeatMode;
    emit repeatModeChanged();
}

QString PlaybackController::songTitle() const
{
    return m_currentTrack.title;
}

QString PlaybackController::artistName() const
{
    return m_currentTrack.artist;
}

QString PlaybackController::albumName() const
{
    return m_currentTrack.album;
}

QString PlaybackController::currentTrackId() const
{
    return m_currentTrack.trackId;
}

QString PlaybackController::currentTrackNodeId() const
{
    return m_currentTrack.nodeId;
}

QString PlaybackController::coverArtworkPath() const
{
    return m_currentTrack.artworkPath;
}

QString PlaybackController::coverArtworkSource() const
{
    return m_currentTrack.preferredArtworkPath.isEmpty()
        ? QString()
        : QUrl::fromLocalFile(m_currentTrack.preferredArtworkPath).toString();
}

QString PlaybackController::coverThumbnailSource() const
{
    return m_currentTrack.fallbackThumbnailPath.isEmpty()
        ? QString()
        : QUrl::fromLocalFile(m_currentTrack.fallbackThumbnailPath).toString();
}

QString PlaybackController::coverPlaceholderText() const
{
    return m_coverPlaceholderText;
}

qreal PlaybackController::currentTrackDuration() const
{
    return m_currentTrack.durationSeconds;
}

QString PlaybackController::audioFormat() const
{
    return m_currentTrack.audioFormat;
}

int PlaybackController::audioSampleRate() const
{
    return m_currentTrack.audioSampleRate;
}

int PlaybackController::audioBitDepth() const
{
    return m_currentTrack.audioBitDepth;
}

int PlaybackController::audioChannels() const
{
    return m_currentTrack.audioChannels;
}

QString PlaybackController::currentPositionText() const
{
    return formatDuration(m_currentPosition);
}

QString PlaybackController::totalDurationText() const
{
    return formatDuration(m_totalDuration);
}

QString PlaybackController::remainingDurationText() const
{
    return QStringLiteral("-%1").arg(formatDuration(m_totalDuration - m_currentPosition));
}

QVariantList PlaybackController::waveformHeights() const
{
    return m_waveformHeights;
}

int PlaybackController::waveformBarWidth() const
{
    return m_waveformBarWidth;
}

void PlaybackController::applyWaveform(const QVariantList &heights, int barWidth)
{
    barWidth = qMax(0, barWidth);
    const bool heightsChanged = m_waveformHeights != heights;
    const bool barWidthChanged = m_waveformBarWidth != barWidth;

    if (!heightsChanged && !barWidthChanged) {
        return;
    }

    m_waveformHeights = heights;
    m_waveformBarWidth = barWidth;

    if (heightsChanged) {
        emit waveformHeightsChanged();
    }
    if (barWidthChanged) {
        emit waveformBarWidthChanged();
    }
}

QVariantList PlaybackController::queueEntries() const
{
    return m_queueEntries;
}

void PlaybackController::applyQueueEntries(const QVariantList &entries)
{
    if (m_queueEntries == entries) {
        return;
    }

    m_queueEntries = entries;
    emit queueEntriesChanged();
}

void PlaybackController::setTrackStartedHandler(TrackStartedHandler handler)
{
    m_trackStartedHandler = std::move(handler);
}

#if SERIONA_HAS_BACKEND
void PlaybackController::setCommandExecutor(CommandExecutor executor)
{
    m_commandExecutor = std::move(executor);
}

void PlaybackController::applyPlayerStateSnapshot(
    const seriona::control::PlayerStateSnapshot &snapshot,
    const seriona::control::LibraryStateSnapshot *library)
{
    const PlayerSnapshotViewState mapped = mapPlayerSnapshot(snapshot, library);

    // 播放次数计数点（T16）：新曲目开始播放（轨道切换/PlaybackEnded 后续播）时通知
    // TrackStatsController 自增；同一曲目重复快照不重复计数。
    const QString nextTrackId = mapped.currentTrack.trackId;
    if (!nextTrackId.isEmpty() && nextTrackId != m_currentTrack.trackId && m_trackStartedHandler) {
        m_trackStartedHandler(nextTrackId);
    }

    applyPlaying(mapped.isPlaying);
    applyTimelineSnapshot(mapped.timeline);
    applyVolume(mapped.volume);
    applyShuffle(mapped.shuffle);
    applyRepeatMode(mapped.repeatMode);
    setCurrentTrackViewState(mapped.currentTrack);
    setCapability(mapped.capability);
    applyQueueEntries(mapQueueEntries(snapshot, library));
}

void PlaybackController::applyTimelineSnapshot(const TimelineSnapshotViewState &snapshot)
{
    m_timelineSnapshotPosition = snapshot.position;
    m_timelineSnapshotDuration = snapshot.duration;
    m_timelineSnapshotSampledAt = snapshot.sampledAt;
    m_timelineSnapshotVersion = snapshot.version;

    applyTotalDuration(snapshot.durationSeconds);
    if (snapshot.smooth) {
        const bool reachedEnd = updateSmoothedTimelinePosition();
        if (!reachedEnd && !m_timelineTimer.isActive()) {
            m_timelineTimer.start();
        }
        return;
    }

    stopTimelineSmoothing();
    applyCurrentPosition(secondsFromMilliseconds(m_timelineSnapshotPosition));
}

bool PlaybackController::updateSmoothedTimelinePosition()
{
    std::chrono::milliseconds position = m_timelineSnapshotPosition + elapsedSince(m_timelineSnapshotSampledAt);
    bool reachedEnd = false;
    if (m_timelineSnapshotDuration && position >= *m_timelineSnapshotDuration) {
        position = *m_timelineSnapshotDuration;
        reachedEnd = true;
    }

    applyCurrentPosition(secondsFromMilliseconds(position));
    if (reachedEnd) {
        stopTimelineSmoothing();
    }
    return reachedEnd;
}

void PlaybackController::stopTimelineSmoothing()
{
    if (m_timelineTimer.isActive()) {
        m_timelineTimer.stop();
    }
}
#endif

void PlaybackController::play()
{
#if SERIONA_HAS_BACKEND
    seriona::control::MediaControlCommand command;
    command.kind = seriona::control::MediaControlCommandKind::Play;
    submitCommand(command);
#endif
}

void PlaybackController::pause()
{
#if SERIONA_HAS_BACKEND
    seriona::control::MediaControlCommand command;
    command.kind = seriona::control::MediaControlCommandKind::Pause;
    submitCommand(command);
#endif
}

void PlaybackController::togglePlay()
{
#if SERIONA_HAS_BACKEND
    seriona::control::MediaControlCommand command;
    command.kind = seriona::control::MediaControlCommandKind::TogglePlayPause;
    submitCommand(command);
#endif
}

void PlaybackController::seek(qreal position)
{
#if SERIONA_HAS_BACKEND
    seriona::control::MediaControlCommand command;
    command.kind = seriona::control::MediaControlCommandKind::SeekTo;
    command.position = millisecondsFromSeconds(position);
    submitCommand(command);
#else
    Q_UNUSED(position)
#endif
}

void PlaybackController::toggleShuffle()
{
    setShuffle(!m_isShuffle);
}

void PlaybackController::cycleRepeatMode()
{
    setRepeatMode((m_repeatMode + 1) % 3);
}

void PlaybackController::skipPrevious()
{
#if SERIONA_HAS_BACKEND
    seriona::control::MediaControlCommand command;
    command.kind = seriona::control::MediaControlCommandKind::SkipPrevious;
    submitCommand(command);
#endif
}

void PlaybackController::skipNext()
{
#if SERIONA_HAS_BACKEND
    seriona::control::MediaControlCommand command;
    command.kind = seriona::control::MediaControlCommandKind::SkipNext;
    submitCommand(command);
#endif
}

void PlaybackController::setMuted(bool muted)
{
#if SERIONA_HAS_BACKEND
    seriona::control::MediaControlCommand command;
    command.kind = seriona::control::MediaControlCommandKind::SetMuted;
    command.muted = muted;
    submitCommand(command);
#else
    Q_UNUSED(muted)
#endif
}

#if SERIONA_HAS_BACKEND
seriona::control::RepeatMode PlaybackController::repeatModeForIndex(int repeatMode)
{
    return repeatModeFromIndex(repeatMode);
}

void PlaybackController::submitCommand(const seriona::control::MediaControlCommand &command)
{
    if (!m_commandExecutor) {
        return;
    }

    static_cast<void>(m_commandExecutor(command));
}
#endif

qreal PlaybackController::clamp(qreal value, qreal minimum, qreal maximum)
{
    return qMin(qMax(value, minimum), maximum);
}

QString PlaybackController::formatDuration(qreal seconds)
{
    const int clampedSeconds = qMax(0, qFloor(seconds));
    const int minutes = clampedSeconds / 60;
    const int remainder = clampedSeconds % 60;
    return QStringLiteral("%1:%2")
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(remainder, 2, 10, QLatin1Char('0'));
}

void PlaybackController::setCurrentTrackViewState(const CurrentTrackViewState &state)
{
    if (sameCurrentTrackViewState(m_currentTrack, state)) {
        return;
    }

    const bool thumbnailChanged = m_currentTrack.fallbackThumbnailPath != state.fallbackThumbnailPath;
    m_currentTrack = state;
    if (thumbnailChanged) {
        if (m_currentTrack.fallbackThumbnailPath.isEmpty()) {
            ++m_lastPaletteRequestGeneration;
            applyGradientPalette(defaultGradientPalette());
        } else {
            m_lastPaletteRequestGeneration = m_paletteWorker.requestPalette(m_currentTrack.fallbackThumbnailPath);
        }
    }
    emit currentSongChanged();
}

void PlaybackController::setCapability(const QString &capability)
{
    if (m_capability == capability) {
        return;
    }

    m_capability = capability;
    emit capabilityChanged();
}

void PlaybackController::applyGradientPalette(const GradientPalette &palette)
{
    if (m_gradientColor0 == palette[0] && m_gradientColor1 == palette[1] && m_gradientColor2 == palette[2]) {
        return;
    }

    m_gradientColor0 = palette[0];
    m_gradientColor1 = palette[1];
    m_gradientColor2 = palette[2];
    emit gradientColorsChanged();
}

void PlaybackController::applyPaletteResult(quint64 generation, const QString &color0, const QString &color1, const QString &color2)
{
    if (generation != m_lastPaletteRequestGeneration) {
        return;
    }
    applyGradientPalette(GradientPalette{color0, color1, color2});
}

QString PlaybackController::gradientColor0() const
{
    return m_gradientColor0;
}

QString PlaybackController::gradientColor1() const
{
    return m_gradientColor1;
}

QString PlaybackController::gradientColor2() const
{
    return m_gradientColor2;
}

}
