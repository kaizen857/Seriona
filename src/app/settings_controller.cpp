#include "settings_controller.h"

#include <QVariant>
#include <QVariantMap>

namespace Seriona::App {

namespace {

constexpr auto kOutputGroup = "output";
constexpr auto kLyricsGroup = "lyrics";
constexpr auto kLoggingGroup = "logging";
constexpr auto kTransitionGroup = "transition";
constexpr auto kOutputModeKey = "outputMode";
constexpr auto kSampleRateKey = "sampleRate";
constexpr auto kSampleFormatKey = "sampleFormat";
constexpr auto kBufferDurationMsKey = "bufferDurationMs";
constexpr auto kPreferredDeviceIdKey = "preferredDeviceId";
constexpr auto kLyricsDelimitersKey = "delimiters";
constexpr auto kFollowRestoreDelayMsKey = "followRestoreDelayMs";
constexpr auto kLogLevelKey = "logLevel";
constexpr auto kAutoAdvanceFadeModeKey = "autoAdvanceFadeMode";
constexpr auto kFadeOnTransportKey = "fadeOnTransport";
constexpr auto kFadeOnSeekKey = "fadeOnSeek";
constexpr auto kGaplessPreloadMsKey = "gaplessPreloadMs";
constexpr auto kCrossfadeMsKey = "crossfadeMs";
constexpr auto kTransportFadeMsKey = "transportFadeMs";
constexpr auto kSeekFadeMsKey = "seekFadeMs";
constexpr auto kManualAdvanceFadeModeKey = "manualAdvanceFadeMode";
constexpr auto kManualShortCrossfadeMsKey = "manualShortCrossfadeMs";

constexpr int kDefaultOutputMode = 0; // Direct
constexpr int kDefaultSampleRate = 48000;
constexpr int kDefaultSampleFormat = 0; // 跟随设备
constexpr int kDefaultBufferDurationMs = 300;
// 日志等级默认 info（2）：与 spdlog::level::level_enum 的 info 值一致；
// 前端持久化的用户默认，启动后经 applyLogLevel 同步后端。
constexpr int kDefaultLogLevel = 2;
constexpr int kMinLogLevel = 0; // trace
constexpr int kMaxLogLevel = 5; // critical
constexpr int kMinSampleRate = 8000;
constexpr int kMaxSampleRate = 768000;
constexpr int kMinBufferDurationMs = 50;
constexpr int kMaxBufferDurationMs = 1000;
// 歌词跟随恢复延迟：默认 5s；范围 1s-15s（业界实测 1s~10s，默认取中位）
constexpr int kDefaultFollowRestoreDelayMs = 5000;
constexpr int kMinFollowRestoreDelayMs = 1000;
constexpr int kMaxFollowRestoreDelayMs = 15000;
// 播放过渡组默认值/量程（与用户裁定表一致；滑块步进 kTransitionSliderStepMs=100ms，
// QML from/to/stepSize 与本常量对齐）：
constexpr int kDefaultAutoAdvanceFadeMode = 0; // 无
constexpr int kDefaultFadeOnTransport = 0;     // 关
constexpr int kDefaultFadeOnSeek = 0;          // 关
constexpr int kDefaultGaplessPreloadMs = 0;
constexpr int kDefaultCrossfadeMs = 3000;
constexpr int kDefaultTransportFadeMs = 300;
constexpr int kDefaultSeekFadeMs = 300;
constexpr int kDefaultManualAdvanceFadeMode = 0; // 无
constexpr int kDefaultManualShortCrossfadeMs = 500;
constexpr int kMinFadeMode = 0;
constexpr int kMaxFadeMode = 2;
constexpr int kMinCrossfadeMs = 0;
constexpr int kMaxCrossfadeMs = 10000;
constexpr int kMinGaplessPreloadMs = 0;
constexpr int kMaxGaplessPreloadMs = 5000;
constexpr int kMinShortFadeMs = 0; // transport/seek/manualShort 共用下限
constexpr int kMaxShortFadeMs = 3000;
constexpr int kTransitionSliderStepMs = 100;
// 连续控件去抖窗口（300-500ms 要求区间内）
constexpr int kDebounceIntervalMs = 400;

const QStringList kDefaultLyricDelimiters = {QStringLiteral(" / ")};

// 标准采样率/位深选项（0 = 跟随设备，恒保留；与既有 QML 硬编码模型一致）
const QList<int> kStandardSampleRates = {0, 44100, 48000, 96000, 192000};
// 1/2/3/4 对应后端 AudioSampleFormat 的 Int16/Int24/Int32/Float32
const QList<int> kStandardSampleFormats = {0, 1, 2, 3, 4};

bool capabilitiesEqual(const QList<PlaybackDeviceCapabilities> &lhs, const QList<PlaybackDeviceCapabilities> &rhs)
{
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (int i = 0; i < lhs.size(); ++i) {
        const auto &a = lhs.at(i);
        const auto &b = rhs.at(i);
        // 设备名变化不视为能力变化（不影响过滤选项）
        if (a.deviceId != b.deviceId || a.sampleFormats != b.sampleFormats || a.sampleRates != b.sampleRates) {
            return false;
        }
    }
    return true;
}

bool isValidOutputMode(int mode)
{
    return mode == 0 || mode == 1;
}

bool isValidSampleRate(int sampleRate)
{
    // 0 = 跟随设备
    return sampleRate == 0 || (sampleRate >= kMinSampleRate && sampleRate <= kMaxSampleRate);
}

bool isValidSampleFormat(int sampleFormat)
{
    // 0 = 设备默认；1/2/3/4 对应后端 AudioSampleFormat 的 Int16/Int24/Int32/Float32
    return sampleFormat == 0 || sampleFormat == 1 || sampleFormat == 2 || sampleFormat == 3 || sampleFormat == 4;
}

bool isValidBufferDurationMs(int bufferDurationMs)
{
    return bufferDurationMs >= kMinBufferDurationMs && bufferDurationMs <= kMaxBufferDurationMs;
}

bool isValidFollowRestoreDelayMs(int delayMs)
{
    return delayMs >= kMinFollowRestoreDelayMs && delayMs <= kMaxFollowRestoreDelayMs;
}

bool isValidLogLevel(int level)
{
    // 仅接受 [trace, critical]（0..5）；spdlog::level::level_enum 是 int 底层枚举，
    // 越界值会破坏 should_log 比较，前端直接拒绝（off=6 不在设置 UI 范围内）
    return level >= kMinLogLevel && level <= kMaxLogLevel;
}

bool isValidFadeMode(int mode)
{
    // 0=无 / 1=短时交叉（手动）/ 除 CUE 邻曲与无间隙组外交叉（自动）/ 2=全交叉；
    // 与后端 reducer 枚举域 0-2 一致
    return mode >= kMinFadeMode && mode <= kMaxFadeMode;
}

bool isValidCrossfadeMs(int ms)
{
    return ms >= kMinCrossfadeMs && ms <= kMaxCrossfadeMs;
}

bool isValidGaplessPreloadMs(int ms)
{
    return ms >= kMinGaplessPreloadMs && ms <= kMaxGaplessPreloadMs;
}

bool isValidShortFadeMs(int ms)
{
    return ms >= kMinShortFadeMs && ms <= kMaxShortFadeMs;
}

struct LogLevelName {
    const char *name;
    int value;
};

constexpr LogLevelName kLogLevelNames[] = {
    {"trace", 0},
    {"debug", 1},
    {"info", 2},
    {"warn", 3},
    {"error", 4},
    {"critical", 5},
};

} // namespace

SettingsController::SettingsController(QObject *parent)
    : QObject(parent)
{
    m_debounceTimer.setSingleShot(true);
    m_debounceTimer.setInterval(kDebounceIntervalMs);
    connect(&m_debounceTimer, &QTimer::timeout, this, &SettingsController::apply);
    m_transitionDebounceTimer.setSingleShot(true);
    m_transitionDebounceTimer.setInterval(kDebounceIntervalMs);
    connect(&m_transitionDebounceTimer, &QTimer::timeout, this, &SettingsController::applyTransitionConfig);
}

int SettingsController::outputMode() const
{
    return m_outputMode;
}

QStringList SettingsController::playbackDevices() const
{
    return m_playbackDevices;
}

QStringList SettingsController::playbackDeviceNames() const
{
    return m_playbackDeviceNames;
}

QString SettingsController::preferredDeviceId() const
{
    return m_preferredDeviceId;
}

int SettingsController::sampleRate() const
{
    return m_sampleRate;
}

int SettingsController::sampleFormat() const
{
    return m_sampleFormat;
}

int SettingsController::bufferDurationMs() const
{
    return m_bufferDurationMs;
}

QStringList SettingsController::lyricDelimiters() const
{
    return m_lyricDelimiters;
}

int SettingsController::followRestoreDelayMs() const
{
    return m_followRestoreDelayMs;
}

int SettingsController::logLevel() const
{
    return m_logLevel;
}

void SettingsController::setOutputMode(int mode)
{
    if (!isValidOutputMode(mode) || m_outputMode == mode) {
        return;
    }
    setOutputModeInternal(mode);
    persistOutputValue(kOutputModeKey, mode);
    apply();
}

void SettingsController::setSampleRate(int sampleRate)
{
    if (!isValidSampleRate(sampleRate) || m_sampleRate == sampleRate) {
        return;
    }
    setSampleRateInternal(sampleRate);
    persistOutputValue(kSampleRateKey, sampleRate);
    apply();
}

void SettingsController::setSampleFormat(int sampleFormat)
{
    if (!isValidSampleFormat(sampleFormat) || m_sampleFormat == sampleFormat) {
        return;
    }
    setSampleFormatInternal(sampleFormat);
    persistOutputValue(kSampleFormatKey, sampleFormat);
    apply();
}

void SettingsController::setLyricDelimiters(const QStringList &delimiters)
{
    if (m_lyricDelimiters == delimiters) {
        return;
    }
    setLyricDelimitersInternal(delimiters);
    persistValue(kLyricsGroup, kLyricsDelimitersKey, delimiters);
}

void SettingsController::setFollowRestoreDelayMs(int delayMs)
{
    if (!isValidFollowRestoreDelayMs(delayMs) || m_followRestoreDelayMs == delayMs) {
        return;
    }
    setFollowRestoreDelayMsInternal(delayMs);
    persistValue(kLyricsGroup, kFollowRestoreDelayMsKey, delayMs);
}

void SettingsController::setLogLevel(int level)
{
    if (!isValidLogLevel(level) || m_logLevel == level) {
        return;
    }
    setLogLevelInternal(level);
    persistValue(kLoggingGroup, kLogLevelKey, level);
    applyLogLevel();
}

int SettingsController::logLevelFromString(const QString &name)
{
    for (const auto &entry : kLogLevelNames) {
        if (name == QLatin1String(entry.name)) {
            return entry.value;
        }
    }
    return -1;
}

QString SettingsController::logLevelToString(int level)
{
    for (const auto &entry : kLogLevelNames) {
        if (entry.value == level) {
            return QLatin1String(entry.name);
        }
    }
    return QString();
}

void SettingsController::applyLogLevel()
{
    if (!m_logLevelExecutor) {
        return;
    }
    m_logLevelExecutor(m_logLevel);
}

void SettingsController::setBufferDurationMs(int bufferDurationMs)
{
    if (!isValidBufferDurationMs(bufferDurationMs) || m_bufferDurationMs == bufferDurationMs) {
        return;
    }
    setBufferDurationMsInternal(bufferDurationMs);
    persistOutputValue(kBufferDurationMsKey, bufferDurationMs);
    scheduleDebouncedApply();
}

int SettingsController::autoAdvanceFadeMode() const
{
    return m_autoAdvanceFadeMode;
}

bool SettingsController::fadeOnTransport() const
{
    return m_fadeOnTransport;
}

bool SettingsController::fadeOnSeek() const
{
    return m_fadeOnSeek;
}

int SettingsController::gaplessPreloadMs() const
{
    return m_gaplessPreloadMs;
}

int SettingsController::crossfadeMs() const
{
    return m_crossfadeMs;
}

int SettingsController::transportFadeMs() const
{
    return m_transportFadeMs;
}

int SettingsController::seekFadeMs() const
{
    return m_seekFadeMs;
}

int SettingsController::manualAdvanceFadeMode() const
{
    return m_manualAdvanceFadeMode;
}

int SettingsController::manualShortCrossfadeMs() const
{
    return m_manualShortCrossfadeMs;
}

void SettingsController::setAutoAdvanceFadeMode(int mode)
{
    if (!isValidFadeMode(mode) || m_autoAdvanceFadeMode == mode) {
        return;
    }
    setAutoAdvanceFadeModeInternal(mode);
    persistTransitionValue(kAutoAdvanceFadeModeKey, mode);
    applyTransitionConfig();
}

void SettingsController::setFadeOnTransport(bool enabled)
{
    if (m_fadeOnTransport == enabled) {
        return;
    }
    setFadeOnTransportInternal(enabled);
    persistTransitionValue(kFadeOnTransportKey, enabled);
    applyTransitionConfig();
}

void SettingsController::setFadeOnSeek(bool enabled)
{
    if (m_fadeOnSeek == enabled) {
        return;
    }
    setFadeOnSeekInternal(enabled);
    persistTransitionValue(kFadeOnSeekKey, enabled);
    applyTransitionConfig();
}

void SettingsController::setGaplessPreloadMs(int ms)
{
    if (!isValidGaplessPreloadMs(ms) || m_gaplessPreloadMs == ms) {
        return;
    }
    setGaplessPreloadMsInternal(ms);
    persistTransitionValue(kGaplessPreloadMsKey, ms);
    scheduleDebouncedTransitionApply();
}

void SettingsController::setCrossfadeMs(int ms)
{
    if (!isValidCrossfadeMs(ms) || m_crossfadeMs == ms) {
        return;
    }
    setCrossfadeMsInternal(ms);
    persistTransitionValue(kCrossfadeMsKey, ms);
    scheduleDebouncedTransitionApply();
}

void SettingsController::setTransportFadeMs(int ms)
{
    if (!isValidShortFadeMs(ms) || m_transportFadeMs == ms) {
        return;
    }
    setTransportFadeMsInternal(ms);
    persistTransitionValue(kTransportFadeMsKey, ms);
    scheduleDebouncedTransitionApply();
}

void SettingsController::setSeekFadeMs(int ms)
{
    if (!isValidShortFadeMs(ms) || m_seekFadeMs == ms) {
        return;
    }
    setSeekFadeMsInternal(ms);
    persistTransitionValue(kSeekFadeMsKey, ms);
    scheduleDebouncedTransitionApply();
}

void SettingsController::setManualAdvanceFadeMode(int mode)
{
    if (!isValidFadeMode(mode) || m_manualAdvanceFadeMode == mode) {
        return;
    }
    setManualAdvanceFadeModeInternal(mode);
    persistTransitionValue(kManualAdvanceFadeModeKey, mode);
    applyTransitionConfig();
}

void SettingsController::setManualShortCrossfadeMs(int ms)
{
    if (!isValidShortFadeMs(ms) || m_manualShortCrossfadeMs == ms) {
        return;
    }
    setManualShortCrossfadeMsInternal(ms);
    persistTransitionValue(kManualShortCrossfadeMsKey, ms);
    scheduleDebouncedTransitionApply();
}

void SettingsController::setPreferredDeviceId(const QString &deviceId)
{
    if (m_preferredDeviceId == deviceId) {
        return;
    }
    setPreferredDeviceIdInternal(deviceId);
    persistOutputValue(kPreferredDeviceIdKey, deviceId);
    apply();
}

void SettingsController::setDefaults(
    int outputMode,
    int sampleRate,
    int bufferDurationMs,
    const QString &preferredDeviceId)
{
    setOutputModeInternal(outputMode);
    setSampleRateInternal(sampleRate);
    setBufferDurationMsInternal(bufferDurationMs);
    setPreferredDeviceIdInternal(preferredDeviceId);
}

void SettingsController::reloadFromSettings()
{
    const int mode = m_settingsStorage.read(QString::fromUtf8(kOutputGroup),
                                            QString::fromUtf8(kOutputModeKey),
                                            kDefaultOutputMode)
                         .toInt();
    const int sampleRate = m_settingsStorage.read(QString::fromUtf8(kOutputGroup),
                                                  QString::fromUtf8(kSampleRateKey),
                                                  kDefaultSampleRate)
                               .toInt();
    const int sampleFormat = m_settingsStorage.read(QString::fromUtf8(kOutputGroup),
                                                    QString::fromUtf8(kSampleFormatKey),
                                                    kDefaultSampleFormat)
                                 .toInt();
    const int bufferDurationMs = m_settingsStorage.read(QString::fromUtf8(kOutputGroup),
                                                        QString::fromUtf8(kBufferDurationMsKey),
                                                        kDefaultBufferDurationMs)
                                     .toInt();
    const QString deviceId = m_settingsStorage.read(QString::fromUtf8(kOutputGroup),
                                                    QString::fromUtf8(kPreferredDeviceIdKey),
                                                    QString())
                                 .toString();

    const QStringList delimiters = m_settingsStorage.read(QString::fromUtf8(kLyricsGroup),
                                                          QString::fromUtf8(kLyricsDelimitersKey),
                                                          kDefaultLyricDelimiters)
                                       .toStringList();

    const int followRestoreDelayMs = m_settingsStorage.read(QString::fromUtf8(kLyricsGroup),
                                                            QString::fromUtf8(kFollowRestoreDelayMsKey),
                                                            kDefaultFollowRestoreDelayMs)
                                         .toInt();

    const int logLevel = m_settingsStorage.read(QString::fromUtf8(kLoggingGroup),
                                                QString::fromUtf8(kLogLevelKey),
                                                kDefaultLogLevel)
                             .toInt();

    const auto readTransition = [this](const char *key, int fallback) {
        return m_settingsStorage.read(QString::fromUtf8(kTransitionGroup), QString::fromUtf8(key), fallback).toInt();
    };
    const int autoAdvanceFadeMode = readTransition(kAutoAdvanceFadeModeKey, kDefaultAutoAdvanceFadeMode);
    const bool fadeOnTransport = m_settingsStorage
                                     .read(QString::fromUtf8(kTransitionGroup),
                                           QString::fromUtf8(kFadeOnTransportKey),
                                           kDefaultFadeOnTransport)
                                     .toBool();
    const bool fadeOnSeek = m_settingsStorage
                                .read(QString::fromUtf8(kTransitionGroup),
                                      QString::fromUtf8(kFadeOnSeekKey),
                                      kDefaultFadeOnSeek)
                                .toBool();
    const int gaplessPreloadMs = readTransition(kGaplessPreloadMsKey, kDefaultGaplessPreloadMs);
    const int crossfadeMs = readTransition(kCrossfadeMsKey, kDefaultCrossfadeMs);
    const int transportFadeMs = readTransition(kTransportFadeMsKey, kDefaultTransportFadeMs);
    const int seekFadeMs = readTransition(kSeekFadeMsKey, kDefaultSeekFadeMs);
    const int manualAdvanceFadeMode = readTransition(kManualAdvanceFadeModeKey, kDefaultManualAdvanceFadeMode);
    const int manualShortCrossfadeMs = readTransition(kManualShortCrossfadeMsKey, kDefaultManualShortCrossfadeMs);

    setOutputModeInternal(mode);
    setSampleRateInternal(sampleRate);
    setSampleFormatInternal(sampleFormat);
    setBufferDurationMsInternal(bufferDurationMs);
    setPreferredDeviceIdInternal(deviceId);
    setLyricDelimitersInternal(delimiters);
    setFollowRestoreDelayMsInternal(followRestoreDelayMs);
    setLogLevelInternal(logLevel);
    setAutoAdvanceFadeModeInternal(autoAdvanceFadeMode);
    setFadeOnTransportInternal(fadeOnTransport);
    setFadeOnSeekInternal(fadeOnSeek);
    setGaplessPreloadMsInternal(gaplessPreloadMs);
    setCrossfadeMsInternal(crossfadeMs);
    setTransportFadeMsInternal(transportFadeMs);
    setSeekFadeMsInternal(seekFadeMs);
    setManualAdvanceFadeModeInternal(manualAdvanceFadeMode);
    setManualShortCrossfadeMsInternal(manualShortCrossfadeMs);
}

void SettingsController::apply()
{
    if (!m_applyOutputConfigExecutor) {
        return;
    }
    recordLastValidSnapshot();
    m_applyOutputConfigExecutor(m_outputMode, m_sampleRate, m_sampleFormat, m_bufferDurationMs, m_preferredDeviceId);
}

void SettingsController::applyTransitionConfig()
{
    if (!m_applyTransitionConfigExecutor) {
        return;
    }
    m_applyTransitionConfigExecutor(m_autoAdvanceFadeMode,
                                    m_fadeOnTransport,
                                    m_fadeOnSeek,
                                    m_gaplessPreloadMs,
                                    m_crossfadeMs,
                                    m_transportFadeMs,
                                    m_seekFadeMs,
                                    m_manualAdvanceFadeMode,
                                    m_manualShortCrossfadeMs);
}

void SettingsController::rollbackRejectedOutputConfig()
{
    if (!m_hasCommittedSnapshot) {
        return;
    }
    setOutputModeInternal(m_lastValidOutputMode);
    setSampleRateInternal(m_lastValidSampleRate);
    setSampleFormatInternal(m_lastValidSampleFormat);
    setBufferDurationMsInternal(m_lastValidBufferDurationMs);
    setPreferredDeviceIdInternal(m_lastValidPreferredDeviceId);
}

void SettingsController::enumerateDevices()
{
    if (!m_enumerateDevicesExecutor) {
        return;
    }
    const QList<PlaybackDeviceCapabilities> devices = m_enumerateDevicesExecutor();
    QStringList ids;
    QStringList names;
    ids.reserve(devices.size());
    names.reserve(devices.size());
    for (const auto &device : devices) {
        ids.append(device.deviceId);
        names.append(device.deviceName);
    }
    const bool idsChanged = ids != m_playbackDevices;
    const bool namesChanged = names != m_playbackDeviceNames;
    const bool capsChanged = !capabilitiesEqual(devices, m_deviceCapabilities);
    if (!idsChanged && !namesChanged && !capsChanged) {
        return;
    }
    m_playbackDevices = ids;
    m_playbackDeviceNames = names;
    if (capsChanged) {
        m_deviceCapabilities = devices;
    }
    if (idsChanged) {
        emit playbackDevicesChanged();
    }
    if (namesChanged) {
        emit playbackDeviceNamesChanged();
    }
    if (capsChanged) {
        emit playbackDeviceCapabilitiesChanged();
        emit sampleRateOptionsChanged();
        emit sampleFormatOptionsChanged();
    }
}

bool SettingsController::sampleParamsGreyed() const
{
    return m_outputMode == 0;
}

bool SettingsController::advanceTransitionsGreyed() const
{
    // Direct（0）下交叉/预加载/手动档全无效：{1 自动档, 4 预加载, 5 交叉长度,
    // 8 手动档, 9 手动短交叉} 灰化；{2,3,6,7}（传送类）全局可用。
    return m_outputMode == 0;
}

int SettingsController::transitionSliderStepMs() const
{
    return kTransitionSliderStepMs;
}

QVariantList SettingsController::sampleRateOptions() const
{
    const PlaybackDeviceCapabilities *caps = selectedDeviceCaps();
    const QList<int> supported = caps ? caps->sampleRates : QList<int>();
    return buildOptions(kStandardSampleRates, supported, m_sampleRate, true);
}

QVariantList SettingsController::sampleFormatOptions() const
{
    const PlaybackDeviceCapabilities *caps = selectedDeviceCaps();
    const QList<int> supported = caps ? caps->sampleFormats : QList<int>();
    return buildOptions(kStandardSampleFormats, supported, m_sampleFormat, false);
}

QVariantList SettingsController::playbackDeviceCapabilities() const
{
    QVariantList result;
    result.reserve(m_deviceCapabilities.size());
    for (const auto &caps : m_deviceCapabilities) {
        QVariantMap entry;
        entry.insert(QStringLiteral("deviceId"), caps.deviceId);
        entry.insert(QStringLiteral("deviceName"), caps.deviceName);
        QVariantList formats;
        formats.reserve(caps.sampleFormats.size());
        for (int format : caps.sampleFormats) {
            formats.append(format);
        }
        QVariantList rates;
        rates.reserve(caps.sampleRates.size());
        for (int rate : caps.sampleRates) {
            rates.append(rate);
        }
        entry.insert(QStringLiteral("sampleFormats"), formats);
        entry.insert(QStringLiteral("sampleRates"), rates);
        result.append(entry);
    }
    return result;
}

void SettingsController::setApplyOutputConfigExecutor(ApplyOutputConfigExecutor executor)
{
    m_applyOutputConfigExecutor = std::move(executor);
}

void SettingsController::setApplyTransitionConfigExecutor(ApplyTransitionConfigExecutor executor)
{
    m_applyTransitionConfigExecutor = std::move(executor);
}

void SettingsController::setEnumerateDevicesExecutor(EnumerateDevicesExecutor executor)
{
    m_enumerateDevicesExecutor = std::move(executor);
}

void SettingsController::setLogLevelExecutor(LogLevelExecutor executor)
{
    m_logLevelExecutor = std::move(executor);
}

void SettingsController::setOutputModeInternal(int mode)
{
    if (!isValidOutputMode(mode) || m_outputMode == mode) {
        return;
    }
    m_outputMode = mode;
    emit outputModeChanged();
}

void SettingsController::setSampleRateInternal(int sampleRate)
{
    if (!isValidSampleRate(sampleRate) || m_sampleRate == sampleRate) {
        return;
    }
    m_sampleRate = sampleRate;
    emit sampleRateChanged();
}

void SettingsController::setSampleFormatInternal(int sampleFormat)
{
    if (!isValidSampleFormat(sampleFormat) || m_sampleFormat == sampleFormat) {
        return;
    }
    m_sampleFormat = sampleFormat;
    emit sampleFormatChanged();
}

void SettingsController::setLyricDelimitersInternal(const QStringList &delimiters)
{
    if (m_lyricDelimiters == delimiters) {
        return;
    }
    m_lyricDelimiters = delimiters;
    emit lyricDelimitersChanged();
}

void SettingsController::setFollowRestoreDelayMsInternal(int delayMs)
{
    if (!isValidFollowRestoreDelayMs(delayMs) || m_followRestoreDelayMs == delayMs) {
        return;
    }
    m_followRestoreDelayMs = delayMs;
    emit followRestoreDelayMsChanged();
}

void SettingsController::setLogLevelInternal(int level)
{
    if (!isValidLogLevel(level) || m_logLevel == level) {
        return;
    }
    m_logLevel = level;
    emit logLevelChanged();
}

void SettingsController::setBufferDurationMsInternal(int bufferDurationMs)
{
    if (!isValidBufferDurationMs(bufferDurationMs) || m_bufferDurationMs == bufferDurationMs) {
        return;
    }
    m_bufferDurationMs = bufferDurationMs;
    emit bufferDurationMsChanged();
}

void SettingsController::setAutoAdvanceFadeModeInternal(int mode)
{
    if (!isValidFadeMode(mode) || m_autoAdvanceFadeMode == mode) {
        return;
    }
    m_autoAdvanceFadeMode = mode;
    emit autoAdvanceFadeModeChanged();
}

void SettingsController::setFadeOnTransportInternal(bool enabled)
{
    if (m_fadeOnTransport == enabled) {
        return;
    }
    m_fadeOnTransport = enabled;
    emit fadeOnTransportChanged();
}

void SettingsController::setFadeOnSeekInternal(bool enabled)
{
    if (m_fadeOnSeek == enabled) {
        return;
    }
    m_fadeOnSeek = enabled;
    emit fadeOnSeekChanged();
}

void SettingsController::setGaplessPreloadMsInternal(int ms)
{
    if (!isValidGaplessPreloadMs(ms) || m_gaplessPreloadMs == ms) {
        return;
    }
    m_gaplessPreloadMs = ms;
    emit gaplessPreloadMsChanged();
}

void SettingsController::setCrossfadeMsInternal(int ms)
{
    if (!isValidCrossfadeMs(ms) || m_crossfadeMs == ms) {
        return;
    }
    m_crossfadeMs = ms;
    emit crossfadeMsChanged();
}

void SettingsController::setTransportFadeMsInternal(int ms)
{
    if (!isValidShortFadeMs(ms) || m_transportFadeMs == ms) {
        return;
    }
    m_transportFadeMs = ms;
    emit transportFadeMsChanged();
}

void SettingsController::setSeekFadeMsInternal(int ms)
{
    if (!isValidShortFadeMs(ms) || m_seekFadeMs == ms) {
        return;
    }
    m_seekFadeMs = ms;
    emit seekFadeMsChanged();
}

void SettingsController::setManualAdvanceFadeModeInternal(int mode)
{
    if (!isValidFadeMode(mode) || m_manualAdvanceFadeMode == mode) {
        return;
    }
    m_manualAdvanceFadeMode = mode;
    emit manualAdvanceFadeModeChanged();
}

void SettingsController::setManualShortCrossfadeMsInternal(int ms)
{
    if (!isValidShortFadeMs(ms) || m_manualShortCrossfadeMs == ms) {
        return;
    }
    m_manualShortCrossfadeMs = ms;
    emit manualShortCrossfadeMsChanged();
}

void SettingsController::setPreferredDeviceIdInternal(const QString &deviceId)
{
    if (m_preferredDeviceId == deviceId) {
        return;
    }
    m_preferredDeviceId = deviceId;
    emit preferredDeviceIdChanged();
    // 设备切换 → 采样率/位深过滤选项随之重算
    emit sampleRateOptionsChanged();
    emit sampleFormatOptionsChanged();
}

void SettingsController::scheduleDebouncedApply()
{
    m_debounceTimer.start();
}

void SettingsController::scheduleDebouncedTransitionApply()
{
    m_transitionDebounceTimer.start();
}

void SettingsController::recordLastValidSnapshot()
{
    m_lastValidOutputMode = m_outputMode;
    m_lastValidSampleRate = m_sampleRate;
    m_lastValidSampleFormat = m_sampleFormat;
    m_lastValidBufferDurationMs = m_bufferDurationMs;
    m_lastValidPreferredDeviceId = m_preferredDeviceId;
    m_hasCommittedSnapshot = true;
}

void SettingsController::setSettingsStorageBackend(AppSettingsBackend backend)
{
    m_settingsStorage.setBackend(std::move(backend));
}

void SettingsController::persistValue(const char *group, const char *key, const QVariant &value)
{
    m_settingsStorage.write(QString::fromUtf8(group), QString::fromUtf8(key), value);
}

void SettingsController::persistOutputValue(const char *key, const QVariant &value)
{
    persistValue(kOutputGroup, key, value);
}

void SettingsController::persistTransitionValue(const char *key, const QVariant &value)
{
    persistValue(kTransitionGroup, key, value);
}

const PlaybackDeviceCapabilities *SettingsController::selectedDeviceCaps() const
{
    if (m_deviceCapabilities.isEmpty()) {
        return nullptr;
    }
    for (const auto &caps : m_deviceCapabilities) {
        if (caps.deviceId == m_preferredDeviceId) {
            return &caps;
        }
    }
    // 未选择或所选设备已失效：回退第一台设备（与设备下拉默认显示一致）
    return &m_deviceCapabilities.first();
}

QVariantList SettingsController::buildOptions(const QList<int> &standardValues,
                                              const QList<int> &supportedValues,
                                              int savedValue,
                                              bool isSampleRate) const
{
    QList<int> filtered;
    const bool capsKnown = !supportedValues.isEmpty();
    if (capsKnown) {
        // 已枚举能力：与标准列表求交；0=跟随设备恒保留
        for (int value : standardValues) {
            if (value == 0 || supportedValues.contains(value)) {
                filtered.append(value);
            }
        }
    } else {
        // 空能力 = 未枚举/全支持：显示全部标准选项
        filtered = standardValues;
    }

    // 已保存值不在过滤结果中时保留并标注（已保存值本身不变，仅影响显示选项）
    const bool savedUnsupported = capsKnown && savedValue != 0 && !supportedValues.contains(savedValue);
    if (savedUnsupported && !filtered.contains(savedValue)) {
        filtered.append(savedValue);
    }

    QVariantList options;
    options.reserve(filtered.size());
    for (int value : filtered) {
        QVariantMap entry;
        entry.insert(QStringLiteral("value"), value);
        QString label = isSampleRate ? sampleRateLabel(value) : sampleFormatLabel(value);
        if (savedUnsupported && value == savedValue) {
            label += QStringLiteral("（设备不支持）");
        }
        entry.insert(QStringLiteral("label"), label);
        options.append(entry);
    }
    return options;
}

QString SettingsController::sampleRateLabel(int value) const
{
    switch (value) {
    case 0:
        return tr("跟随设备");
    case 44100:
        return QStringLiteral("44100 Hz");
    case 48000:
        return QStringLiteral("48000 Hz");
    case 96000:
        return QStringLiteral("96000 Hz");
    case 192000:
        return QStringLiteral("192000 Hz");
    default:
        return QStringLiteral("%1 Hz").arg(value);
    }
}

QString SettingsController::sampleFormatLabel(int value) const
{
    switch (value) {
    case 0:
        return tr("跟随设备");
    case 1:
        return QStringLiteral("16-bit");
    case 2:
        return QStringLiteral("24-bit");
    case 3:
        return QStringLiteral("32-bit");
    case 4:
        return QStringLiteral("32-bit float");
    default:
        return QStringLiteral("Format %1").arg(value);
    }
}

}
