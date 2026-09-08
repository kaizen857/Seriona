#include "settings_controller.h"

#include "equalizer_presets.h"

#include <cmath>
#include <iterator>

#include <QVariant>
#include <QVariantMap>

namespace Seriona::App {

namespace {

constexpr auto kOutputGroup = "output";
constexpr auto kLyricsGroup = "lyrics";
constexpr auto kLoggingGroup = "logging";
constexpr auto kTransitionGroup = "transition";
constexpr auto kEqualizerGroup = "equalizer";
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
// 均衡器组键（键组 "equalizer"）：7 个用户可设键 + 用户预设 10/31 独立键落盘；
// spectrumBins/curvePoints/curveFrequencies 是订阅镜像键（纯内存态，写者仅限
// mirrorSpectrumBins/mirrorEqualizerCurve——由 F1.3 AppFacade 注入链驱动；本文件
// 不存在任何镜像持久化路径，防按频谱帧率写存储）。
constexpr auto kEqEnabledKey = "enabled";
constexpr auto kEqBandModeKey = "bandMode";
constexpr auto kEqPreGainDbKey = "preGainDb";
constexpr auto kEqBandGains10Key = "bandGains10";
constexpr auto kEqBandGains31Key = "bandGains31";
constexpr auto kEqLimiterEnabledKey = "limiterEnabled";
constexpr auto kEqSpectrumEnabledKey = "spectrumEnabled";
constexpr auto kEqUserPresets10Key = "userPresets10";
constexpr auto kEqUserPresets31Key = "userPresets31";

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
// 均衡器连续参数去抖窗口（EQ 规格 50ms：滑块拖动合并；独立于 output/transition 的
// 400ms timer——勿改动/复用，离散项与预设/复位不走去抖，见 applyEqualizerConfig 注释）
constexpr int kEqDebounceIntervalMs = 50;
// 均衡器组默认值（档位默认 10 段；用户预设默认空——两档独立键，与预设表
// equalizer_presets.h 同源引用波段数/增益钳位域）
constexpr int kDefaultEqEnabled = 0;
constexpr int kDefaultEqBandMode = kEqBandMode10;
constexpr double kDefaultEqPreGainDb = 0.0;
constexpr int kDefaultEqLimiterEnabled = 0;
constexpr int kDefaultEqSpectrumEnabled = 0;
constexpr int kMaxEqPresetNameLength = 64;

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
        // 设备名变化不视为能力变化（不影响过滤选项）；isDefault 参与比较（影响默认高亮）。
        if (a.deviceId != b.deviceId || a.sampleFormats != b.sampleFormats || a.sampleRates != b.sampleRates
            || a.isDefault != b.isDefault) {
            return false;
        }
    }
    return true;
}

// 历史版本持久化的 preferredDeviceId 是"枚举索引"（纯十进制字符串，见后端
// miniaudio_device_id_encoding.h 迁移说明）；后端稳定文本 id 永不呈纯数字形态
// （整数类后端带 "backend:" 前缀），故纯数字且不命中任何 id 的值可安全判定为旧值。
bool isLegacyNumericDeviceId(const QString &deviceId)
{
    if (deviceId.isEmpty()) {
        return false;
    }
    for (const QChar c : deviceId) {
        if (!c.isDigit()) {
            return false;
        }
    }
    return true;
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

bool isValidEqBandMode(int bandMode)
{
    return bandMode == kEqBandMode10 || bandMode == kEqBandMode31;
}

// dB 为小数域（后端 EqualizerConfig 用 float dB；F2.3 滑块 0.5 步进/数值框 0.1
// 精度）——全部增益/preGain 以 double 处理，写入前统一归一到 0.1 网格
// （round 后比较/存储，保证 setter 去重 == 与 0.1 网格值稳定）
double roundEqGainTenth(double gainDb)
{
    return std::round(gainDb * 10.0) / 10.0;
}

bool isValidEqGainDb(double gainDb)
{
    return gainDb >= kMinEqGainDb && gainDb <= kMaxEqGainDb;
}

double clampEqGainDb(double gainDb)
{
    // 非有限值（NaN/Inf）先归 0.0：NaN 的两次比较均为 false 会穿透钳位直入存储，
    // F1.2 组包全量提交后会把 NaN 传给后端 reducer（该侧对 NaN 拒绝）——此漏斗
    // 一并在 normalizeEqGains/makeEqPresetEntry/sanitizeStoredEqPresets 挡住。
    const double rounded = std::isfinite(gainDb) ? roundEqGainTenth(gainDb) : 0.0;
    return rounded < kMinEqGainDb ? kMinEqGainDb : (rounded > kMaxEqGainDb ? kMaxEqGainDb : rounded);
}

QVariantList makeZeroEqGains(int bandCount)
{
    QVariantList gains;
    gains.reserve(bandCount);
    for (int i = 0; i < bandCount; ++i) {
        gains.append(0.0);
    }
    return gains;
}

// 增益写入防御归一化：逐项 round 0.1 + 钳位 ±15dB、定长补零/截断
// （非数值项按 0.0 处理），非法输入不原样进入存储
QVariantList normalizeEqGains(const QVariantList &raw, int bandCount)
{
    QVariantList gains;
    gains.reserve(bandCount);
    for (int i = 0; i < bandCount; ++i) {
        const double value = i < raw.size() ? raw.at(i).toDouble() : 0.0;
        gains.append(clampEqGainDb(value));
    }
    return gains;
}

QVariantList eqGainsFromArray(const double *gains, int bandCount)
{
    QVariantList result;
    result.reserve(bandCount);
    for (int i = 0; i < bandCount; ++i) {
        result.append(gains[i]);
    }
    return result;
}

bool isValidEqPresetName(const QString &name)
{
    const QString trimmed = name.trimmed();
    return !trimmed.isEmpty() && trimmed.size() <= kMaxEqPresetNameLength;
}

QString nextEqUserPresetId(const QVariantList &presets)
{
    int maxIndex = 0;
    for (const QVariant &entry : presets) {
        const QString id = entry.toMap().value(QLatin1String("id")).toString();
        if (id.startsWith(QLatin1String("user-"))) {
            bool ok = false;
            const int index = id.mid(5).toInt(&ok);
            if (ok && index > maxIndex) {
                maxIndex = index;
            }
        }
    }
    return QStringLiteral("user-%1").arg(maxIndex + 1);
}

QVariantMap makeEqPresetEntry(const QString &id, const QString &name, double preGainDb, const QVariantList &gains)
{
    QVariantMap entry;
    entry.insert(QLatin1String("id"), id);
    entry.insert(QLatin1String("name"), name);
    entry.insert(QLatin1String("preGainDb"), clampEqGainDb(preGainDb));
    entry.insert(QLatin1String("gains"), gains);
    return entry;
}

int indexOfEqUserPreset(const QVariantList &presets, const QString &presetId)
{
    for (int i = 0; i < presets.size(); ++i) {
        if (presets.at(i).toMap().value(QLatin1String("id")).toString() == presetId) {
            return i;
        }
    }
    return -1;
}

// 存储读出的用户预设净化：逐项校验并重建（名字非空/长度受限、id 需 "user-" 前缀
// 且不重复、preGain 钳位、gains 按档位归一化）；脏数据条目丢弃，绝不原样回灌
QVariantList sanitizeStoredEqPresets(const QVariant &raw, int bandCount)
{
    QVariantList result;
    const QVariantList rawList = raw.toList();
    result.reserve(rawList.size());
    for (const QVariant &item : rawList) {
        const QVariantMap map = item.toMap();
        const QString name = map.value(QLatin1String("name")).toString().trimmed();
        const QString id = map.value(QLatin1String("id")).toString();
        if (name.isEmpty() || name.size() > kMaxEqPresetNameLength || !id.startsWith(QLatin1String("user-"))
            || indexOfEqUserPreset(result, id) >= 0) {
            continue;
        }
        const double preGainDb = map.value(QLatin1String("preGainDb")).toDouble();
        result.append(makeEqPresetEntry(id,
                                        name,
                                        clampEqGainDb(preGainDb),
                                        normalizeEqGains(map.value(QLatin1String("gains")).toList(), bandCount)));
    }
    return result;
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
    // 均衡器连续参数去抖（50ms，EQ 规格；独立 timer，勿与上方 400ms 共享）
    m_eqDebounceTimer.setSingleShot(true);
    m_eqDebounceTimer.setInterval(kEqDebounceIntervalMs);
    connect(&m_eqDebounceTimer, &QTimer::timeout, this, &SettingsController::applyEqualizerConfig);
    // 均衡器两档增益默认全 0（定长 10/31；缺省时 reload 前 getter 即返回合法默认）
    m_bandGains10 = makeZeroEqGains(kEqBandCount10);
    m_bandGains31 = makeZeroEqGains(kEqBandCount31);
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

bool SettingsController::enabled() const
{
    return m_enabled;
}

int SettingsController::bandMode() const
{
    return m_bandMode;
}

double SettingsController::preGainDb() const
{
    return m_preGainDb;
}

QVariantList SettingsController::bandGains10() const
{
    return m_bandGains10;
}

QVariantList SettingsController::bandGains31() const
{
    return m_bandGains31;
}

bool SettingsController::limiterEnabled() const
{
    return m_limiterEnabled;
}

bool SettingsController::spectrumEnabled() const
{
    return m_spectrumEnabled;
}

QVariantList SettingsController::spectrumBins() const
{
    return m_spectrumBins;
}

QVariantList SettingsController::curvePoints() const
{
    return m_curvePoints;
}

QVariantList SettingsController::curveFrequencies() const
{
    return m_curveFrequencies;
}

void SettingsController::mirrorEqualizerCurve(const QVariantList &curvePoints, const QVariantList &curveFrequencies)
{
    // 订阅镜像（内存态）：定长契约不符（后端 std::array 定长，正常不可达）或与现值
    // 相同（幂等重推）时跳过，不发 NOTIFY。长度用 kEqCurvePointCount（181）核对。
    if (curvePoints.size() != kEqCurvePointCount || curveFrequencies.size() != kEqCurvePointCount) {
        return;
    }
    if (curvePoints == m_curvePoints && curveFrequencies == m_curveFrequencies) {
        return;
    }
    m_curvePoints = curvePoints;
    m_curveFrequencies = curveFrequencies;
    emit curvePointsChanged();
    emit curveFrequenciesChanged();
}

void SettingsController::mirrorSpectrumBins(const QVariantList &bins)
{
    if (bins.size() != kEqSpectrumBinCount) {
        return;
    }
    if (bins == m_spectrumBins) {
        return;
    }
    m_spectrumBins = bins;
    emit spectrumBinsChanged();
}

void SettingsController::setEnabled(bool value)
{
    if (m_enabled == value) {
        return;
    }
    setEnabledInternal(value);
    persistEqValue(kEqEnabledKey, m_enabled);
    // F1.2 开关 = 立即项：落盘即时 + 立即提交（未绑定 executor = no-op）
    applyEqualizerConfig();
}

void SettingsController::setBandMode(int bandMode)
{
    if (!isValidEqBandMode(bandMode) || m_bandMode == bandMode) {
        return;
    }
    setBandModeInternal(bandMode);
    persistEqValue(kEqBandModeKey, m_bandMode);
    // F1.2 档位 = 立即项：立即提交（组包按新档位取对应增益数组）
    applyEqualizerConfig();
}

void SettingsController::setPreGainDb(double gainDb)
{
    const double normalized = roundEqGainTenth(gainDb);
    if (!isValidEqGainDb(normalized) || m_preGainDb == normalized) {
        return;
    }
    setPreGainDbInternal(normalized);
    persistEqValue(kEqPreGainDbKey, m_preGainDb);
    // F1.2 连续参数 = 去抖项：50ms 合并后提交
    scheduleDebouncedEqualizerApply();
}

void SettingsController::setBandGains10(const QVariantList &gains)
{
    const QVariantList normalized = normalizeEqGains(gains, kEqBandCount10);
    if (m_bandGains10 == normalized) {
        return;
    }
    setBandGains10Internal(normalized);
    persistEqValue(kEqBandGains10Key, m_bandGains10);
    // F1.2 连续参数 = 去抖项：50ms 合并后提交
    scheduleDebouncedEqualizerApply();
}

void SettingsController::setBandGains31(const QVariantList &gains)
{
    const QVariantList normalized = normalizeEqGains(gains, kEqBandCount31);
    if (m_bandGains31 == normalized) {
        return;
    }
    setBandGains31Internal(normalized);
    persistEqValue(kEqBandGains31Key, m_bandGains31);
    // F1.2 连续参数 = 去抖项：50ms 合并后提交
    scheduleDebouncedEqualizerApply();
}

void SettingsController::setLimiterEnabled(bool value)
{
    if (m_limiterEnabled == value) {
        return;
    }
    setLimiterEnabledInternal(value);
    persistEqValue(kEqLimiterEnabledKey, m_limiterEnabled);
    // F1.2 开关 = 立即项：落盘即时 + 立即提交（未绑定 executor = no-op）
    applyEqualizerConfig();
}

void SettingsController::setSpectrumEnabled(bool value)
{
    if (m_spectrumEnabled == value) {
        return;
    }
    setSpectrumEnabledInternal(value);
    persistEqValue(kEqSpectrumEnabledKey, m_spectrumEnabled);
    // F1.2 开关 = 立即项：落盘即时 + 立即提交（未绑定 executor = no-op）
    applyEqualizerConfig();
}

QVariantList SettingsController::eqPresetList() const
{
    QVariantList result;
    // 内置 8 款恒在前（只读，不可删改/覆盖）：按当前档位取对应增益数组
    result.reserve(std::size(kBuiltinEqPresets) + eqUserPresets().size());
    const int bandCount = m_bandMode == kEqBandMode31 ? kEqBandCount31 : kEqBandCount10;
    for (const auto &preset : kBuiltinEqPresets) {
        QVariantMap entry;
        entry.insert(QStringLiteral("id"), QString::fromUtf8(preset.id));
        entry.insert(QStringLiteral("name"), QString::fromUtf8(preset.displayName));
        entry.insert(QStringLiteral("builtin"), true);
        entry.insert(QStringLiteral("preGainDb"), preset.preGainDb);
        entry.insert(QStringLiteral("gains"),
                     eqGainsFromArray(bandCount == kEqBandCount31 ? preset.gains31 : preset.gains10, bandCount));
        result.append(entry);
    }
    // 用户预设（当前档位独立存储，条目存储时已净化）
    for (const QVariant &item : eqUserPresets()) {
        QVariantMap entry = item.toMap();
        entry.insert(QStringLiteral("builtin"), false);
        result.append(entry);
    }
    return result;
}

bool SettingsController::addEqPreset(const QString &name)
{
    if (!isValidEqPresetName(name)) {
        return false;
    }
    QVariantList &presets = eqUserPresetsSlot();
    const QVariantList &currentGains = m_bandMode == kEqBandMode31 ? m_bandGains31 : m_bandGains10;
    presets.append(makeEqPresetEntry(nextEqUserPresetId(presets), name.trimmed(), m_preGainDb, currentGains));
    persistEqUserPresets(presets);
    emit eqPresetListChanged();
    return true;
}

bool SettingsController::addEqPresetWithValues(const QString &name, const QVariantList &gains, double preGainDb)
{
    if (!isValidEqPresetName(name)) {
        return false;
    }
    QVariantList &presets = eqUserPresetsSlot();
    const int bandCount = m_bandMode == kEqBandMode31 ? kEqBandCount31 : kEqBandCount10;
    presets.append(makeEqPresetEntry(nextEqUserPresetId(presets),
                                     name.trimmed(),
                                     clampEqGainDb(preGainDb),
                                     normalizeEqGains(gains, bandCount)));
    persistEqUserPresets(presets);
    emit eqPresetListChanged();
    return true;
}

bool SettingsController::renameEqPreset(const QString &presetId, const QString &newName)
{
    if (!isValidEqPresetName(newName)) {
        return false;
    }
    QVariantList &presets = eqUserPresetsSlot();
    const int index = indexOfEqUserPreset(presets, presetId);
    if (index < 0) {
        // 内置预设或未知 id：不可重命名/不存在
        return false;
    }
    QVariantMap entry = presets.at(index).toMap();
    entry.insert(QStringLiteral("name"), newName.trimmed());
    presets.replace(index, entry);
    persistEqUserPresets(presets);
    emit eqPresetListChanged();
    return true;
}

bool SettingsController::deleteEqPreset(const QString &presetId)
{
    QVariantList &presets = eqUserPresetsSlot();
    const int index = indexOfEqUserPreset(presets, presetId);
    if (index < 0) {
        // 内置预设不可删（不在用户表）
        return false;
    }
    presets.removeAt(index);
    persistEqUserPresets(presets);
    emit eqPresetListChanged();
    return true;
}

bool SettingsController::applyEqPreset(const QString &presetId)
{
    double preGainDb = 0.0;
    QVariantList gains;
    bool found = false;
    // 内置预设：按当前档位取对应增益数组
    for (const auto &preset : kBuiltinEqPresets) {
        if (presetId == QLatin1String(preset.id)) {
            preGainDb = preset.preGainDb;
            const int bandCount = m_bandMode == kEqBandMode31 ? kEqBandCount31 : kEqBandCount10;
            gains = eqGainsFromArray(bandCount == kEqBandCount31 ? preset.gains31 : preset.gains10, bandCount);
            found = true;
            break;
        }
    }
    // 用户预设（当前档位独立存储）
    if (!found) {
        const QVariantList &presets = eqUserPresetsSlot();
        const int index = indexOfEqUserPreset(presets, presetId);
        if (index < 0) {
            return false;
        }
        const QVariantMap entry = presets.at(index).toMap();
        preGainDb = entry.value(QStringLiteral("preGainDb")).toDouble();
        gains = entry.value(QStringLiteral("gains")).toList();
    }
    // 落值（内部 setter：NOTIFY + 实际变更才动；persist 逐键即时，与 setter 语义同）。
    // F1.2 预设 = 立即项：绕过 public setter 的去抖，任一键实际变更后立即提交一次
    // 全量组包（值全未变 = 预设与当前一致，无动作无推送）。
    bool changed = false;
    const int bandCount = m_bandMode == kEqBandMode31 ? kEqBandCount31 : kEqBandCount10;
    const QVariantList normalizedGains = normalizeEqGains(gains, bandCount);
    if (m_bandMode == kEqBandMode31) {
        if (m_bandGains31 != normalizedGains) {
            setBandGains31Internal(normalizedGains);
            persistEqValue(kEqBandGains31Key, m_bandGains31);
            changed = true;
        }
    } else if (m_bandGains10 != normalizedGains) {
        setBandGains10Internal(normalizedGains);
        persistEqValue(kEqBandGains10Key, m_bandGains10);
        changed = true;
    }
    // preGain 越界钳位（用户预设存储时已钳位，此处仅防御；与 addEqPresetWithValues 同语义）
    const double clampedPreGain = clampEqGainDb(preGainDb);
    if (m_preGainDb != clampedPreGain) {
        setPreGainDbInternal(clampedPreGain);
        persistEqValue(kEqPreGainDbKey, m_preGainDb);
        changed = true;
    }
    if (changed) {
        applyEqualizerConfig();
    }
    return true;
}

bool SettingsController::resetEq()
{
    // 复位 = 平坦：enabled/bandMode/limiterEnabled/spectrumEnabled 保持，
    // 两档 bandGains 全 0.0 + preGainDb 0.0（落盘逐键即时，仅实际变更的键写）；
    // 复位 = 立即项：任一值实际变更后立即提交一次；已全平 = no-op（仍返回 true）。
    bool changed = false;
    const QVariantList zero10 = makeZeroEqGains(kEqBandCount10);
    const QVariantList zero31 = makeZeroEqGains(kEqBandCount31);
    if (m_bandGains10 != zero10) {
        setBandGains10Internal(zero10);
        persistEqValue(kEqBandGains10Key, m_bandGains10);
        changed = true;
    }
    if (m_bandGains31 != zero31) {
        setBandGains31Internal(zero31);
        persistEqValue(kEqBandGains31Key, m_bandGains31);
        changed = true;
    }
    if (m_preGainDb != 0.0) {
        setPreGainDbInternal(0.0);
        persistEqValue(kEqPreGainDbKey, m_preGainDb);
        changed = true;
    }
    if (changed) {
        applyEqualizerConfig();
    }
    return true;
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

void SettingsController::setDefaults(int sampleRate, int bufferDurationMs, const QString &preferredDeviceId)
{
    setSampleRateInternal(sampleRate);
    setBufferDurationMsInternal(bufferDurationMs);
    setPreferredDeviceIdInternal(preferredDeviceId);
}

void SettingsController::reloadFromSettings()
{
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

    // 均衡器组持久化键（只读用户可设项；spectrumBins/curvePoints/curveFrequencies
    // 为订阅镜像——纯内存态，绝不出现在 reload 读取，勿按频谱帧率落盘）
    const auto readEq = [this](const char *key, const QVariant &fallback) {
        return m_settingsStorage.read(QString::fromUtf8(kEqualizerGroup), QString::fromUtf8(key), fallback);
    };
    const bool eqEnabled = readEq(kEqEnabledKey, kDefaultEqEnabled).toBool();
    const int eqBandMode = readEq(kEqBandModeKey, kDefaultEqBandMode).toInt();
    const double eqPreGainDb = readEq(kEqPreGainDbKey, kDefaultEqPreGainDb).toDouble();
    const QVariantList eqBandGains10 =
        normalizeEqGains(readEq(kEqBandGains10Key, makeZeroEqGains(kEqBandCount10)).toList(), kEqBandCount10);
    const QVariantList eqBandGains31 =
        normalizeEqGains(readEq(kEqBandGains31Key, makeZeroEqGains(kEqBandCount31)).toList(), kEqBandCount31);
    const bool eqLimiterEnabled = readEq(kEqLimiterEnabledKey, kDefaultEqLimiterEnabled).toBool();
    const bool eqSpectrumEnabled = readEq(kEqSpectrumEnabledKey, kDefaultEqSpectrumEnabled).toBool();
    const QVariantList eqUserPresets10 = sanitizeStoredEqPresets(readEq(kEqUserPresets10Key, QVariant()), kEqBandCount10);
    const QVariantList eqUserPresets31 = sanitizeStoredEqPresets(readEq(kEqUserPresets31Key, QVariant()), kEqBandCount31);

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
    setEnabledInternal(eqEnabled);
    setBandModeInternal(eqBandMode);
    setPreGainDbInternal(eqPreGainDb);
    setBandGains10Internal(eqBandGains10);
    setBandGains31Internal(eqBandGains31);
    setLimiterEnabledInternal(eqLimiterEnabled);
    setSpectrumEnabledInternal(eqSpectrumEnabled);
    setUserPresetsInternal(eqUserPresets10, eqUserPresets31);
}

void SettingsController::apply()
{
    if (m_applyOutputConfigExecutor) {
        recordLastValidSnapshot();
        // 载荷首字段恒写 1（Mixed）：模式选择已由前端移除（F0），后端 ConfigureOutput
        // 载荷结构不变；executor typedef 的 mode 形参名在 .h 中保留（桥层契约边界，
        // 本调用仅按位置传参，不依赖形参名）。
        m_applyOutputConfigExecutor(1, m_sampleRate, m_sampleFormat, m_bufferDurationMs, m_preferredDeviceId);
    }
    // F1.2 EQ 段：启动 reloadFromSettings 后的 apply() 顺带推一次全量 EQ 组包（同
    // output 配置模式；F1.3 在 AppFacade 注入 executor 后启动即同步后端一次）。
    // output 与 EQ 两通道相互独立：任一未绑定 executor 只使该通道 no-op。
    applyEqualizerConfig();
}

void SettingsController::applyEqualizerConfig()
{
    if (!m_applyEqualizerConfigExecutor) {
        return;
    }
    // 全量组包（无部分状态）：按当前 bandMode 取一档增益数组（10/31 定长），
    // 消费方（F1.3 BackendBridge::submitEqualizerConfig）不需感知另一档。
    const QVariantList &bandGains = m_bandMode == kEqBandMode31 ? m_bandGains31 : m_bandGains10;
    m_applyEqualizerConfigExecutor(m_enabled,
                                   m_bandMode,
                                   m_preGainDb,
                                   bandGains,
                                   m_limiterEnabled,
                                   m_spectrumEnabled);
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
    // 无条件下发最新能力：选项/下拉模型一律从 devices 派生（设备改名只有 names 变化时
    // capabilitiesEqual 返回 false 路径不重建，旧缓存会让下拉显示过期名字）。
    m_deviceCapabilities = devices;
    if (idsChanged) {
        emit playbackDevicesChanged();
    }
    if (namesChanged) {
        emit playbackDeviceNamesChanged();
    }
    if (idsChanged || namesChanged || capsChanged) {
        emit outputDeviceOptionsChanged();
    }
    // 旧版持久化值（枚举索引，纯数字）在稳定 id 列表里必然失配：清空回跟随系统默认
    // 并持久化，让下拉高亮与后端实际设备（系统默认）保持一致。
    if (!m_preferredDeviceId.isEmpty() && !ids.contains(m_preferredDeviceId)
        && isLegacyNumericDeviceId(m_preferredDeviceId)) {
        setPreferredDeviceIdInternal(QString());
        persistOutputValue(kPreferredDeviceIdKey, QString());
    }
    if (capsChanged) {
        emit playbackDeviceCapabilitiesChanged();
        emit sampleRateOptionsChanged();
        emit sampleFormatOptionsChanged();
    }
    // 列表或默认标记变化 → 生效设备（高亮与能力过滤基准）随之变化
    emit effectiveDeviceIdChanged();
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
        entry.insert(QStringLiteral("isDefault"), caps.isDefault);
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

QVariantList SettingsController::outputDeviceOptions() const
{
    QVariantList result;
    result.reserve(m_deviceCapabilities.size());
    for (const auto &caps : m_deviceCapabilities) {
        QVariantMap entry;
        entry.insert(QStringLiteral("deviceId"), caps.deviceId);
        entry.insert(QStringLiteral("deviceName"), caps.deviceName);
        entry.insert(QStringLiteral("isDefault"), caps.isDefault);
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

void SettingsController::setApplyEqualizerConfigExecutor(ApplyEqualizerConfigExecutor executor)
{
    m_applyEqualizerConfigExecutor = std::move(executor);
}

void SettingsController::setEnumerateDevicesExecutor(EnumerateDevicesExecutor executor)
{
    m_enumerateDevicesExecutor = std::move(executor);
}

void SettingsController::setLogLevelExecutor(LogLevelExecutor executor)
{
    m_logLevelExecutor = std::move(executor);
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

void SettingsController::setEnabledInternal(bool value)
{
    if (m_enabled == value) {
        return;
    }
    m_enabled = value;
    emit enabledChanged();
}

void SettingsController::setBandModeInternal(int bandMode)
{
    if (!isValidEqBandMode(bandMode) || m_bandMode == bandMode) {
        return;
    }
    m_bandMode = bandMode;
    emit bandModeChanged();
    // 档位切换 → 合并预设列表（内置按另一档数组、用户另一档存储）重算
    emit eqPresetListChanged();
}

void SettingsController::setPreGainDbInternal(double gainDb)
{
    const double normalized = roundEqGainTenth(gainDb);
    if (!isValidEqGainDb(normalized) || m_preGainDb == normalized) {
        return;
    }
    m_preGainDb = normalized;
    emit preGainDbChanged();
}

void SettingsController::setBandGains10Internal(const QVariantList &gains)
{
    const QVariantList normalized = normalizeEqGains(gains, kEqBandCount10);
    if (m_bandGains10 == normalized) {
        return;
    }
    m_bandGains10 = normalized;
    emit bandGains10Changed();
}

void SettingsController::setBandGains31Internal(const QVariantList &gains)
{
    const QVariantList normalized = normalizeEqGains(gains, kEqBandCount31);
    if (m_bandGains31 == normalized) {
        return;
    }
    m_bandGains31 = normalized;
    emit bandGains31Changed();
}

void SettingsController::setLimiterEnabledInternal(bool value)
{
    if (m_limiterEnabled == value) {
        return;
    }
    m_limiterEnabled = value;
    emit limiterEnabledChanged();
}

void SettingsController::setSpectrumEnabledInternal(bool value)
{
    if (m_spectrumEnabled == value) {
        return;
    }
    m_spectrumEnabled = value;
    emit spectrumEnabledChanged();
}

void SettingsController::setUserPresetsInternal(const QVariantList &presets10, const QVariantList &presets31)
{
    bool changed = false;
    if (m_userPresets10 != presets10) {
        m_userPresets10 = presets10;
        changed = true;
    }
    if (m_userPresets31 != presets31) {
        m_userPresets31 = presets31;
        changed = true;
    }
    if (changed) {
        emit eqPresetListChanged();
    }
}

QVariantList &SettingsController::eqUserPresetsSlot()
{
    return m_bandMode == kEqBandMode31 ? m_userPresets31 : m_userPresets10;
}

const QVariantList &SettingsController::eqUserPresets() const
{
    return m_bandMode == kEqBandMode31 ? m_userPresets31 : m_userPresets10;
}

void SettingsController::persistEqValue(const char *key, const QVariant &value)
{
    persistValue(kEqualizerGroup, key, value);
}

void SettingsController::persistEqUserPresets(const QVariantList &presets)
{
    persistEqValue(m_bandMode == kEqBandMode31 ? kEqUserPresets31Key : kEqUserPresets10Key, presets);
}

void SettingsController::setPreferredDeviceIdInternal(const QString &deviceId)
{
    if (m_preferredDeviceId == deviceId) {
        return;
    }
    m_preferredDeviceId = deviceId;
    emit preferredDeviceIdChanged();
    emit effectiveDeviceIdChanged();
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

void SettingsController::scheduleDebouncedEqualizerApply()
{
    m_eqDebounceTimer.start();
}

void SettingsController::recordLastValidSnapshot()
{
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
    // 解析顺序与设备下拉高亮一致（见 effectiveDeviceId）：显式选择 → 系统默认
    // （isDefault）→ 列表首台。此回退同时服务采样率/位深过滤：跟随系统默认时
    // 候选选项必须按"真正生效设备"（默认设备）的能力过滤，而不是列表首台。
    if (!m_preferredDeviceId.isEmpty()) {
        for (const auto &caps : m_deviceCapabilities) {
            if (caps.deviceId == m_preferredDeviceId) {
                return &caps;
            }
        }
    }
    for (const auto &caps : m_deviceCapabilities) {
        if (caps.isDefault) {
            return &caps;
        }
    }
    return &m_deviceCapabilities.first();
}

QString SettingsController::effectiveDeviceId() const
{
    const PlaybackDeviceCapabilities *caps = selectedDeviceCaps();
    return caps != nullptr ? caps->deviceId : QString();
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
