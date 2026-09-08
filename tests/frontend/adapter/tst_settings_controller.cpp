#include "settings_controller.h"

#include "app_settings_storage.h"

#include <QHash>
#include <QSignalSpy>
#include <QVariant>
#include <QtTest/QTest>

#include <limits>
#include <memory>

namespace {

using Seriona::App::kEqBandCount10;
using Seriona::App::kEqBandCount31;
using Seriona::App::kEqBandMode10;
using Seriona::App::kEqBandMode31;
using Seriona::App::kEqBassBoostGains10;
using Seriona::App::kEqCurvePointCount;
using Seriona::App::kEqSpectrumBinCount;

constexpr auto kOutputGroup = "output";
constexpr auto kEqGroup = "equalizer";

QString storageKey(const QString &group, const QString &key)
{
    return group + QLatin1Char('\x1f') + key;
}

// 等差数列双精度表（first + i*step；调用方保证落在 0.1 增益网格上——
// 写入侧 roundEqGainTenth 归一到 0.1，离网值会使断言与存储值失配）
QVariantList makeEqGains(int count, double first, double step)
{
    QVariantList gains;
    gains.reserve(count);
    for (int i = 0; i < count; ++i) {
        gains.append(first + i * step);
    }
    return gains;
}

} // namespace

class SettingsControllerTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void defaults();
    void propertySettersPersistAndNotify();
    void invalidValuesRejected();
    void applyAssemblesPayload();
    void discreteControlsPushImmediately();
    void bufferDurationDebounces();
    void persistenceRoundTrip();
    void setDefaultsLandsPropertiesWithoutPersistenceOrPush();
    void enumerateDevicesUpdatesList();
    void outputDeviceOptionsMarkDefaultDevice();
    void legacyNumericPreferredDeviceIdResetOnEnumerate();
    void startupPushSequence();
    void lyricDelimitersPersistRoundTrip();
    void sampleFormatPersistRoundTrip();
    void applyIncludesSampleFormat();
    void rollbackRejectedOutputConfigRestoresSnapshot();
    void deviceCapsEmptyShowsAllOptions();
    void deviceCapsFilterSampleRatesAndFormats();
    void deviceCapsFollowSelectedDevice();
    void effectiveDeviceTracksDefaultAndFallback();
    void enumerateDevicesExposesCapabilities();
    void unsupportedSavedValueKeptAndMarked();
    void logLevelPersistRoundTrip();
    void logLevelMapping();
    void logLevelPushAndDefense();
    void followRestoreDelayPersistRoundTrip();
    void transitionDefaults();
    void transitionPropertySettersPersistAndNotify();
    void transitionInvalidValuesRejected();
    void transitionSliderSettersDebounceMerged();
    void transitionPersistRoundTrip();
    void transitionApplyPacksCurrentNineFields();
    void transitionExecutorUnsetNoopPersistence();
    void transitionStartupApplySequence();
    void eqDefaults();
    void eqPersistRoundTrip();
    void eqMirrorWriterContract();
    void eqDebounceMergeAndImmediateCommit();
    void eqPresetCrudBuiltinProtected();
    void eqPresetTenThirtyOneIndependent();
    void eqPresetSanitizeDirtyStorage();

private:
    Seriona::App::AppSettingsBackend testBackend();
    QVariant storedValue(const QString &group, const QString &key, const QVariant &defaultValue = QVariant()) const;
    bool storedContains(const QString &group, const QString &key) const;
    void removeStored(const QString &group, const QString &key);

    QHash<QString, QVariant> m_store;
};

Seriona::App::AppSettingsBackend SettingsControllerTest::testBackend()
{
    return Seriona::App::AppSettingsBackend{
        .read = [this](const QString &group, const QString &key, const QVariant &defaultValue) -> std::optional<QVariant> {
            return m_store.value(storageKey(group, key), defaultValue);
        },
        .write = [this](const QString &group, const QString &key, const QVariant &value) {
            m_store.insert(storageKey(group, key), value);
        },
        .remove = [this](const QString &group, const QString &key) {
            m_store.remove(storageKey(group, key));
        },
    };
}

QVariant SettingsControllerTest::storedValue(const QString &group, const QString &key, const QVariant &defaultValue) const
{
    return m_store.value(storageKey(group, key), defaultValue);
}

bool SettingsControllerTest::storedContains(const QString &group, const QString &key) const
{
    return m_store.contains(storageKey(group, key));
}

void SettingsControllerTest::removeStored(const QString &group, const QString &key)
{
    m_store.remove(storageKey(group, key));
}

void SettingsControllerTest::init()
{
    m_store.clear();
}

void SettingsControllerTest::cleanup()
{
    m_store.clear();
}

void SettingsControllerTest::defaults()
{
    Seriona::App::SettingsController settings;

    QVERIFY(settings.playbackDevices().isEmpty());
    QVERIFY(settings.preferredDeviceId().isEmpty());
    QCOMPARE(settings.sampleRate(), 48000);
    QCOMPARE(settings.sampleFormat(), 0);
    QCOMPARE(settings.bufferDurationMs(), 300);
    const QStringList defaultDelimiters{QStringLiteral(" / ")};
    QCOMPARE(settings.lyricDelimiters(), defaultDelimiters);
    // 日志等级默认 info（与前端持久化默认一致，启动后经 applyLogLevel 同步后端）
    QCOMPARE(settings.logLevel(), 2);
    // 歌词跟随恢复延迟默认 5s（与前端持久化默认一致）
    QCOMPARE(settings.followRestoreDelayMs(), 5000);
}

void SettingsControllerTest::propertySettersPersistAndNotify()
{
    Seriona::App::SettingsController settings;
    settings.setSettingsStorageBackend(testBackend());
    QSignalSpy rateSpy(&settings, &Seriona::App::SettingsController::sampleRateChanged);
    QSignalSpy formatSpy(&settings, &Seriona::App::SettingsController::sampleFormatChanged);
    QSignalSpy durationSpy(&settings, &Seriona::App::SettingsController::bufferDurationMsChanged);
    QSignalSpy deviceSpy(&settings, &Seriona::App::SettingsController::preferredDeviceIdChanged);
    QSignalSpy delimiterSpy(&settings, &Seriona::App::SettingsController::lyricDelimitersChanged);

    settings.setSampleRate(96000);
    settings.setSampleFormat(2);
    settings.setBufferDurationMs(500);
    settings.setPreferredDeviceId(QStringLiteral("dev-1"));
    settings.setLyricDelimiters(QStringList{QStringLiteral(" / "), QStringLiteral(" | ")});

    QCOMPARE(settings.sampleRate(), 96000);
    QCOMPARE(settings.sampleFormat(), 2);
    QCOMPARE(settings.bufferDurationMs(), 500);
    QCOMPARE(settings.preferredDeviceId(), QStringLiteral("dev-1"));
    const QStringList expectedDelimiters{QStringLiteral(" / "), QStringLiteral(" | ")};
    QCOMPARE(settings.lyricDelimiters(), expectedDelimiters);
    QCOMPARE(rateSpy.count(), 1);
    QCOMPARE(formatSpy.count(), 1);
    QCOMPARE(durationSpy.count(), 1);
    QCOMPARE(deviceSpy.count(), 1);
    QCOMPARE(delimiterSpy.count(), 1);

    // 相同值不重复 NOTIFY
    settings.setSampleRate(96000);
    settings.setSampleFormat(2);
    settings.setLyricDelimiters(expectedDelimiters);
    QCOMPARE(rateSpy.count(), 1);
    QCOMPARE(formatSpy.count(), 1);
    QCOMPARE(delimiterSpy.count(), 1);

    QCOMPARE(storedValue(QStringLiteral("output"), QStringLiteral("sampleRate")).toInt(), 96000);
    QCOMPARE(storedValue(QStringLiteral("output"), QStringLiteral("sampleFormat")).toInt(), 2);
    QCOMPARE(storedValue(QStringLiteral("output"), QStringLiteral("bufferDurationMs")).toInt(), 500);
    QCOMPARE(storedValue(QStringLiteral("output"), QStringLiteral("preferredDeviceId")).toString(), QStringLiteral("dev-1"));
    QCOMPARE(storedValue(QStringLiteral("lyrics"), QStringLiteral("delimiters")).toStringList(), expectedDelimiters);
}

void SettingsControllerTest::invalidValuesRejected()
{
    Seriona::App::SettingsController settings;
    settings.setSettingsStorageBackend(testBackend());

    settings.setSampleRate(100);
    QCOMPARE(settings.sampleRate(), 48000);
    settings.setSampleRate(-1);
    QCOMPARE(settings.sampleRate(), 48000);
    settings.setSampleRate(768001);
    QCOMPARE(settings.sampleRate(), 48000);

    settings.setSampleFormat(6);
    QCOMPARE(settings.sampleFormat(), 0);
    settings.setSampleFormat(5);
    QCOMPARE(settings.sampleFormat(), 0);
    settings.setSampleFormat(99);
    QCOMPARE(settings.sampleFormat(), 0);
    settings.setSampleFormat(-1);
    QCOMPARE(settings.sampleFormat(), 0);

    settings.setBufferDurationMs(49);
    QCOMPARE(settings.bufferDurationMs(), 300);
    settings.setBufferDurationMs(2000);
    QCOMPARE(settings.bufferDurationMs(), 300);

    settings.setFollowRestoreDelayMs(999);
    QCOMPARE(settings.followRestoreDelayMs(), 5000);
    settings.setFollowRestoreDelayMs(16000);
    QCOMPARE(settings.followRestoreDelayMs(), 5000);
    settings.setFollowRestoreDelayMs(0);
    QCOMPARE(settings.followRestoreDelayMs(), 5000);

    // 边界值合法
    settings.setSampleRate(8000);
    settings.setSampleRate(768000);
    settings.setSampleFormat(1);
    settings.setSampleFormat(2);
    settings.setSampleFormat(3);
    settings.setSampleFormat(4);
    settings.setBufferDurationMs(50);
    settings.setBufferDurationMs(1000);
    settings.setFollowRestoreDelayMs(1000);
    settings.setFollowRestoreDelayMs(15000);
    QCOMPARE(settings.sampleRate(), 768000);
    QCOMPARE(settings.sampleFormat(), 4);
    QCOMPARE(settings.bufferDurationMs(), 1000);
    QCOMPARE(settings.followRestoreDelayMs(), 15000);

    // 非法值不写入存储
    settings.setSampleRate(100);
    settings.setSampleFormat(6);
    settings.setBufferDurationMs(2000);
    settings.setFollowRestoreDelayMs(42);
    QCOMPARE(storedValue(QStringLiteral("output"), QStringLiteral("sampleRate")).toInt(), 768000);
    QCOMPARE(storedValue(QStringLiteral("output"), QStringLiteral("sampleFormat")).toInt(), 4);
    QCOMPARE(storedValue(QStringLiteral("output"), QStringLiteral("bufferDurationMs")).toInt(), 1000);
    QCOMPARE(storedValue(QStringLiteral("lyrics"), QStringLiteral("followRestoreDelayMs")).toInt(), 15000);
}

void SettingsControllerTest::applyAssemblesPayload()
{
    Seriona::App::SettingsController settings;
    QStringList payloads;
    settings.setApplyOutputConfigExecutor(
        [&payloads](int mode, int sampleRate, int sampleFormat, int bufferDurationMs, const QString &preferredDeviceId) {
            payloads.append(QStringLiteral("%1|%2|%3|%4|%5")
                                .arg(mode)
                                .arg(sampleRate)
                                .arg(sampleFormat)
                                .arg(bufferDurationMs)
                                .arg(preferredDeviceId));
        });

    settings.setSampleRate(96000);
    settings.setPreferredDeviceId(QStringLiteral("dev-x"));
    settings.apply();

    QCOMPARE(payloads.size(), 3);
    QCOMPARE(payloads.at(0), QStringLiteral("1|96000|0|300|"));
    QCOMPARE(payloads.at(1), QStringLiteral("1|96000|0|300|dev-x"));
    QCOMPARE(payloads.at(2), QStringLiteral("1|96000|0|300|dev-x"));
}

void SettingsControllerTest::discreteControlsPushImmediately()
{
    Seriona::App::SettingsController settings;
    int pushes = 0;
    settings.setApplyOutputConfigExecutor(
        [&pushes](int, int, int, int, const QString &) {
            ++pushes;
        });

    settings.setSampleRate(44100);
    settings.setPreferredDeviceId(QStringLiteral("dev-2"));
    QCOMPARE(pushes, 2);
}

void SettingsControllerTest::bufferDurationDebounces()
{
    Seriona::App::SettingsController settings;
    int pushes = 0;
    settings.setApplyOutputConfigExecutor(
        [&pushes](int, int, int, int, const QString &) {
            ++pushes;
        });

    // 连续控件去抖：5 次变更只产生一次下发
    for (int i = 0; i < 5; ++i) {
        settings.setBufferDurationMs(100 + i * 10);
    }
    QCOMPARE(pushes, 0);
    QCOMPARE(settings.bufferDurationMs(), 140);

    QTRY_COMPARE_WITH_TIMEOUT(pushes, 1, 5000);

    QTest::qWait(800);
    QCOMPARE(pushes, 1);
}

void SettingsControllerTest::persistenceRoundTrip()
{
    {
        Seriona::App::SettingsController writer;
        writer.setSettingsStorageBackend(testBackend());
        writer.setSampleRate(192000);
        writer.setBufferDurationMs(800);
        writer.setPreferredDeviceId(QStringLiteral("dev-3"));
    }

    Seriona::App::SettingsController reader;
    reader.setSettingsStorageBackend(testBackend());
    int pushes = 0;
    reader.setApplyOutputConfigExecutor(
        [&pushes](int, int, int, int, const QString &) {
            ++pushes;
        });
    reader.reloadFromSettings();

    QCOMPARE(reader.sampleRate(), 192000);
    QCOMPARE(reader.bufferDurationMs(), 800);
    QCOMPARE(reader.preferredDeviceId(), QStringLiteral("dev-3"));
    // reload 只还原属性，不推送
    QCOMPARE(pushes, 0);
}

void SettingsControllerTest::setDefaultsLandsPropertiesWithoutPersistenceOrPush()
{
    Seriona::App::SettingsController settings;
    settings.setSettingsStorageBackend(testBackend());
    int pushes = 0;
    settings.setApplyOutputConfigExecutor(
        [&pushes](int, int, int, int, const QString &) {
            ++pushes;
        });

    settings.setDefaults(96000, 500, QStringLiteral("dev-4"));

    QCOMPARE(settings.sampleRate(), 96000);
    QCOMPARE(settings.bufferDurationMs(), 500);
    QCOMPARE(settings.preferredDeviceId(), QStringLiteral("dev-4"));
    QCOMPARE(pushes, 0);

    QVERIFY(!storedContains(QStringLiteral("output"), QStringLiteral("sampleRate")));
}

void SettingsControllerTest::enumerateDevicesUpdatesList()
{
    Seriona::App::SettingsController settings;
    settings.setEnumerateDevicesExecutor(
        [] {
            return QList<Seriona::App::PlaybackDeviceCapabilities>{
                {QStringLiteral("dev-1"), QStringLiteral("Device One"), {}, {}},
                {QStringLiteral("dev-2"), QStringLiteral("Device Two"), {}, {}},
            };
        });
    QSignalSpy devicesSpy(&settings, &Seriona::App::SettingsController::playbackDevicesChanged);
    QSignalSpy namesSpy(&settings, &Seriona::App::SettingsController::playbackDeviceNamesChanged);

    settings.enumerateDevices();
    QCOMPARE(settings.playbackDevices(), QStringList({QStringLiteral("dev-1"), QStringLiteral("dev-2")}));
    QCOMPARE(settings.playbackDeviceNames(), QStringList({QStringLiteral("Device One"), QStringLiteral("Device Two")}));
    QCOMPARE(devicesSpy.count(), 1);
    QCOMPARE(namesSpy.count(), 1);

    // 相同列表不重复 NOTIFY（id 与名字都跳过）
    settings.enumerateDevices();
    QCOMPARE(devicesSpy.count(), 1);
    QCOMPARE(namesSpy.count(), 1);

    // 仅名字变化时只发 playbackDeviceNamesChanged，id 列表不变
    settings.setEnumerateDevicesExecutor(
        [] {
            return QList<Seriona::App::PlaybackDeviceCapabilities>{
                {QStringLiteral("dev-1"), QStringLiteral("Device One Renamed"), {}, {}},
                {QStringLiteral("dev-2"), QStringLiteral("Device Two"), {}, {}},
            };
        });
    settings.enumerateDevices();
    QCOMPARE(settings.playbackDevices(), QStringList({QStringLiteral("dev-1"), QStringLiteral("dev-2")}));
    QCOMPARE(settings.playbackDeviceNames(), QStringList({QStringLiteral("Device One Renamed"), QStringLiteral("Device Two")}));
    QCOMPARE(devicesSpy.count(), 1);
    QCOMPARE(namesSpy.count(), 2);

    // 无 executor（mock 模式）时无副作用
    Seriona::App::SettingsController mockController;
    mockController.enumerateDevices();
    QVERIFY(mockController.playbackDevices().isEmpty());
    QVERIFY(mockController.playbackDeviceNames().isEmpty());
    QVERIFY(mockController.playbackDeviceCapabilities().isEmpty());
    QVERIFY(mockController.outputDeviceOptions().isEmpty());
}

void SettingsControllerTest::outputDeviceOptionsMarkDefaultDevice()
{
    Seriona::App::SettingsController settings;
    settings.setEnumerateDevicesExecutor(
        [] {
            return QList<Seriona::App::PlaybackDeviceCapabilities>{
                {QStringLiteral("dev-1"), QStringLiteral("Device One"), {}, {}, false},
                {QStringLiteral("dev-2"), QStringLiteral("Device Two"), {}, {}, true},
            };
        });
    QSignalSpy optionsSpy(&settings, &Seriona::App::SettingsController::outputDeviceOptionsChanged);

    settings.enumerateDevices();
    const QVariantList options = settings.outputDeviceOptions();
    QCOMPARE(options.size(), 2);
    QCOMPARE(options.at(0).toMap().value(QStringLiteral("deviceId")).toString(), QStringLiteral("dev-1"));
    QVERIFY(!options.at(0).toMap().value(QStringLiteral("isDefault")).toBool());
    QCOMPARE(options.at(1).toMap().value(QStringLiteral("deviceName")).toString(), QStringLiteral("Device Two"));
    QVERIFY(options.at(1).toMap().value(QStringLiteral("isDefault")).toBool());
    // capabilities 映射同步携带 isDefault
    QVERIFY(settings.playbackDeviceCapabilities().at(1).toMap().value(QStringLiteral("isDefault")).toBool());
    QCOMPARE(optionsSpy.count(), 1);

    // 相同列表不重复 NOTIFY
    settings.enumerateDevices();
    QCOMPARE(optionsSpy.count(), 1);

    // isDefault 翻转 → 能力变化 → 选项模型 NOTIFY
    settings.setEnumerateDevicesExecutor(
        [] {
            return QList<Seriona::App::PlaybackDeviceCapabilities>{
                {QStringLiteral("dev-1"), QStringLiteral("Device One"), {}, {}, true},
                {QStringLiteral("dev-2"), QStringLiteral("Device Two"), {}, {}, false},
            };
        });
    settings.enumerateDevices();
    QCOMPARE(optionsSpy.count(), 2);
    QVERIFY(settings.outputDeviceOptions().at(0).toMap().value(QStringLiteral("isDefault")).toBool());
    QVERIFY(!settings.outputDeviceOptions().at(1).toMap().value(QStringLiteral("isDefault")).toBool());
}

void SettingsControllerTest::legacyNumericPreferredDeviceIdResetOnEnumerate()
{
    Seriona::App::SettingsController settings;
    settings.setSettingsStorageBackend(testBackend());
    settings.setEnumerateDevicesExecutor(
        [] {
            return QList<Seriona::App::PlaybackDeviceCapabilities>{
                {QStringLiteral("alsa_output.pci-0000_0a_00.4.analog-stereo"), QStringLiteral("Device One"), {}, {}, true},
                {QStringLiteral("hw:0,0"), QStringLiteral("Device Two"), {}, {}, false},
            };
        });
    QSignalSpy preferredSpy(&settings, &Seriona::App::SettingsController::preferredDeviceIdChanged);

    // 历史版本持久化的纯数字"枚举索引"在稳定 id 列表失配 → 清空回跟随系统默认并持久化
    settings.setPreferredDeviceId(QStringLiteral("1"));
    QCOMPARE(settings.preferredDeviceId(), QStringLiteral("1"));
    QCOMPARE(preferredSpy.count(), 1);
    settings.enumerateDevices();
    QVERIFY(settings.preferredDeviceId().isEmpty());
    QCOMPARE(preferredSpy.count(), 2);
    QCOMPARE(storedValue(QStringLiteral("output"), QStringLiteral("preferredDeviceId")).toString(), QString());

    // 非纯数字的失配 id（设备被拔出等）保留原值，不回退（拔插恢复后仍可命中）
    settings.setPreferredDeviceId(QStringLiteral("usb-dac-removed"));
    settings.enumerateDevices();
    QCOMPARE(settings.preferredDeviceId(), QStringLiteral("usb-dac-removed"));

    // 命中列表的稳定 id 不受影响
    settings.setPreferredDeviceId(QStringLiteral("hw:0,0"));
    settings.enumerateDevices();
    QCOMPARE(settings.preferredDeviceId(), QStringLiteral("hw:0,0"));
}

void SettingsControllerTest::deviceCapsEmptyShowsAllOptions()
{
    Seriona::App::SettingsController settings;
    settings.setEnumerateDevicesExecutor(
        [] {
            return QList<Seriona::App::PlaybackDeviceCapabilities>{
                {QStringLiteral("dev-1"), QStringLiteral("Device One"), {}, {}},
            };
        });
    settings.enumerateDevices();
    settings.setPreferredDeviceId(QStringLiteral("dev-1"));

    // 空能力 = 未枚举/全支持 → 显示全部标准选项（含 0=跟随设备）
    const QVariantList rates = settings.sampleRateOptions();
    QCOMPARE(rates.size(), 5);
    QCOMPARE(rates.at(0).toMap().value(QStringLiteral("value")).toInt(), 0);
    QCOMPARE(rates.at(1).toMap().value(QStringLiteral("value")).toInt(), 44100);
    QCOMPARE(rates.at(2).toMap().value(QStringLiteral("value")).toInt(), 48000);
    QCOMPARE(rates.at(3).toMap().value(QStringLiteral("value")).toInt(), 96000);
    QCOMPARE(rates.at(4).toMap().value(QStringLiteral("value")).toInt(), 192000);

    const QVariantList formats = settings.sampleFormatOptions();
    QCOMPARE(formats.size(), 5);
    QCOMPARE(formats.at(0).toMap().value(QStringLiteral("value")).toInt(), 0);
    QCOMPARE(formats.at(1).toMap().value(QStringLiteral("value")).toInt(), 1);
    QCOMPARE(formats.at(2).toMap().value(QStringLiteral("value")).toInt(), 2);
    QCOMPARE(formats.at(3).toMap().value(QStringLiteral("value")).toInt(), 3);
    QCOMPARE(formats.at(4).toMap().value(QStringLiteral("value")).toInt(), 4);
}

void SettingsControllerTest::deviceCapsFilterSampleRatesAndFormats()
{
    Seriona::App::SettingsController settings;
    settings.setEnumerateDevicesExecutor(
        [] {
            return QList<Seriona::App::PlaybackDeviceCapabilities>{
                {QStringLiteral("dev-1"), QStringLiteral("Device One"), {1, 4}, {44100, 48000}},
            };
        });
    settings.enumerateDevices();
    settings.setPreferredDeviceId(QStringLiteral("dev-1"));

    // 已枚举能力 → 与标准列表求交（0=跟随设备恒保留）
    const QVariantList rates = settings.sampleRateOptions();
    QCOMPARE(rates.size(), 3);
    QCOMPARE(rates.at(0).toMap().value(QStringLiteral("value")).toInt(), 0);
    QCOMPARE(rates.at(1).toMap().value(QStringLiteral("value")).toInt(), 44100);
    QCOMPARE(rates.at(2).toMap().value(QStringLiteral("value")).toInt(), 48000);

    const QVariantList formats = settings.sampleFormatOptions();
    QCOMPARE(formats.size(), 3);
    QCOMPARE(formats.at(0).toMap().value(QStringLiteral("value")).toInt(), 0);
    QCOMPARE(formats.at(1).toMap().value(QStringLiteral("value")).toInt(), 1);
    QCOMPARE(formats.at(2).toMap().value(QStringLiteral("value")).toInt(), 4);
}

void SettingsControllerTest::deviceCapsFollowSelectedDevice()
{
    Seriona::App::SettingsController settings;
    settings.setEnumerateDevicesExecutor(
        [] {
            return QList<Seriona::App::PlaybackDeviceCapabilities>{
                {QStringLiteral("dev-1"), QStringLiteral("Device One"), {1, 4}, {44100, 48000}},
                {QStringLiteral("dev-2"), QStringLiteral("Device Two"), {1, 2}, {48000, 96000}},
            };
        });
    settings.enumerateDevices();
    settings.setPreferredDeviceId(QStringLiteral("dev-1"));
    QSignalSpy rateOptionsSpy(&settings, &Seriona::App::SettingsController::sampleRateOptionsChanged);
    QSignalSpy formatOptionsSpy(&settings, &Seriona::App::SettingsController::sampleFormatOptionsChanged);

    settings.setPreferredDeviceId(QStringLiteral("dev-2"));
    const QVariantList rates = settings.sampleRateOptions();
    QCOMPARE(rates.size(), 3);
    QCOMPARE(rates.at(1).toMap().value(QStringLiteral("value")).toInt(), 48000);
    QCOMPARE(rates.at(2).toMap().value(QStringLiteral("value")).toInt(), 96000);
    const QVariantList formats = settings.sampleFormatOptions();
    QCOMPARE(formats.size(), 3);
    QCOMPARE(formats.at(1).toMap().value(QStringLiteral("value")).toInt(), 1);
    QCOMPARE(formats.at(2).toMap().value(QStringLiteral("value")).toInt(), 2);
    QCOMPARE(rateOptionsSpy.count(), 1);
    QCOMPARE(formatOptionsSpy.count(), 1);
}

void SettingsControllerTest::effectiveDeviceTracksDefaultAndFallback()
{
    Seriona::App::SettingsController settings;
    settings.setEnumerateDevicesExecutor(
        [] {
            return QList<Seriona::App::PlaybackDeviceCapabilities>{
                {QStringLiteral("dev-1"), QStringLiteral("Device One"), {}, {44100, 48000}},
                {QStringLiteral("dev-2"), QStringLiteral("Device Two"), {}, {48000, 96000, 192000}, true},
            };
        });
    settings.enumerateDevices();

    // 未显式选择（跟随系统默认）→ 生效设备 = isDefault 项；采样率候选按它的能力过滤
    QCOMPARE(settings.effectiveDeviceId(), QStringLiteral("dev-2"));
    const QVariantList defaultRates = settings.sampleRateOptions();
    QCOMPARE(defaultRates.size(), 4); // 0 + 48000/96000/192000（44100 不在 dev-2 能力内）
    QCOMPARE(defaultRates.at(3).toMap().value(QStringLiteral("value")).toInt(), 192000);

    // 已保存值 96000 在默认设备下受支持 → 不标注"设备不支持"
    settings.setSampleRate(96000);
    for (const auto &entry : settings.sampleRateOptions()) {
        if (entry.toMap().value(QStringLiteral("value")).toInt() == 96000) {
            QVERIFY(!entry.toMap().value(QStringLiteral("label")).toString().contains(QStringLiteral("设备不支持")));
        }
    }

    // 显式选择非默认设备 → 生效设备跟随，候选按其能力过滤；已保存 96000 变不支持
    settings.setPreferredDeviceId(QStringLiteral("dev-1"));
    QCOMPARE(settings.effectiveDeviceId(), QStringLiteral("dev-1"));
    const QVariantList dev1Rates = settings.sampleRateOptions();
    QCOMPARE(dev1Rates.size(), 4); // 0 + 44100/48000 + 96000（已保存值保留并标注）
    bool savedRateMarked = false;
    for (const auto &entry : dev1Rates) {
        if (entry.toMap().value(QStringLiteral("value")).toInt() == 96000) {
            savedRateMarked = entry.toMap().value(QStringLiteral("label")).toString().contains(QStringLiteral("设备不支持"));
        }
    }
    QVERIFY(savedRateMarked);

    // 列表无 isDefault 标记 → 回退列表首台
    settings.setEnumerateDevicesExecutor(
        [] {
            return QList<Seriona::App::PlaybackDeviceCapabilities>{
                {QStringLiteral("dev-1"), QStringLiteral("Device One"), {}, {44100, 48000}},
                {QStringLiteral("dev-2"), QStringLiteral("Device Two"), {}, {48000, 96000, 192000}},
            };
        });
    settings.enumerateDevices();
    QCOMPARE(settings.effectiveDeviceId(), QStringLiteral("dev-1"));

    // isDefault 标记翻转 → 生效设备与候选自动重算（无需显式选择）
    settings.setEnumerateDevicesExecutor(
        [] {
            return QList<Seriona::App::PlaybackDeviceCapabilities>{
                {QStringLiteral("dev-1"), QStringLiteral("Device One"), {}, {44100, 48000}, true},
                {QStringLiteral("dev-2"), QStringLiteral("Device Two"), {}, {48000, 96000, 192000}},
            };
        });
    settings.enumerateDevices();
    QCOMPARE(settings.effectiveDeviceId(), QStringLiteral("dev-1"));
}

void SettingsControllerTest::enumerateDevicesExposesCapabilities()
{
    Seriona::App::SettingsController settings;
    settings.setEnumerateDevicesExecutor(
        [] {
            return QList<Seriona::App::PlaybackDeviceCapabilities>{
                {QStringLiteral("dev-1"), QStringLiteral("Device One"), {1, 4}, {48000}},
            };
        });
    QSignalSpy capsSpy(&settings, &Seriona::App::SettingsController::playbackDeviceCapabilitiesChanged);

    settings.enumerateDevices();
    const QVariantList caps = settings.playbackDeviceCapabilities();
    QCOMPARE(caps.size(), 1);
    const QVariantMap device = caps.at(0).toMap();
    QCOMPARE(device.value(QStringLiteral("deviceId")).toString(), QStringLiteral("dev-1"));
    QCOMPARE(device.value(QStringLiteral("deviceName")).toString(), QStringLiteral("Device One"));
    QCOMPARE(device.value(QStringLiteral("sampleFormats")).toList(), QVariantList({1, 4}));
    QCOMPARE(device.value(QStringLiteral("sampleRates")).toList(), QVariantList{QVariant(48000)});
    QCOMPARE(capsSpy.count(), 1);

    // 相同列表不重复 NOTIFY
    settings.enumerateDevices();
    QCOMPARE(capsSpy.count(), 1);
}

void SettingsControllerTest::unsupportedSavedValueKeptAndMarked()
{
    Seriona::App::SettingsController settings;
    settings.setSampleRate(96000);
    settings.setEnumerateDevicesExecutor(
        [] {
            return QList<Seriona::App::PlaybackDeviceCapabilities>{
                {QStringLiteral("dev-1"), QStringLiteral("Device One"), {1, 4}, {44100, 48000}},
            };
        });
    settings.enumerateDevices();
    settings.setPreferredDeviceId(QStringLiteral("dev-1"));

    // 已保存值不在设备支持列表 → 保留该选项并标注；已保存值本身不变
    bool foundSaved = false;
    const QVariantList rates = settings.sampleRateOptions();
    for (const auto &entry : rates) {
        const QVariantMap map = entry.toMap();
        if (map.value(QStringLiteral("value")).toInt() == 96000) {
            foundSaved = true;
            QVERIFY(map.value(QStringLiteral("label")).toString().contains(QStringLiteral("设备不支持")));
        }
    }
    QVERIFY(foundSaved);
    QCOMPARE(settings.sampleRate(), 96000);

    // 支持列表内已保存值不标注
    settings.setSampleRate(44100);
    const QVariantList supportedRates = settings.sampleRateOptions();
    for (const auto &entry : supportedRates) {
        QVERIFY(!entry.toMap().value(QStringLiteral("label")).toString().contains(QStringLiteral("设备不支持")));
    }
}

void SettingsControllerTest::logLevelPersistRoundTrip()
{
    {
        Seriona::App::SettingsController writer;
        writer.setSettingsStorageBackend(testBackend());
        writer.setLogLevel(1); // debug
        QCOMPARE(writer.logLevel(), 1);
    }

    // 持久化键位于 logging 组，值为写入的枚举 int
    QCOMPARE(storedValue(QStringLiteral("logging"), QStringLiteral("logLevel")).toInt(), 1);

    Seriona::App::SettingsController reader;
    reader.setSettingsStorageBackend(testBackend());
    reader.reloadFromSettings();
    QCOMPARE(reader.logLevel(), 1);

    // 未写入时默认 info
    removeStored(QStringLiteral("logging"), QStringLiteral("logLevel"));
    Seriona::App::SettingsController defaultReader;
    defaultReader.setSettingsStorageBackend(testBackend());
    defaultReader.reloadFromSettings();
    QCOMPARE(defaultReader.logLevel(), 2);
}

void SettingsControllerTest::logLevelMapping()
{
    using Seriona::App::SettingsController;

    // 字符串 → 枚举（spdlog::level::level_enum 值：trace=0..critical=5）
    QCOMPARE(SettingsController::logLevelFromString(QStringLiteral("trace")), 0);
    QCOMPARE(SettingsController::logLevelFromString(QStringLiteral("debug")), 1);
    QCOMPARE(SettingsController::logLevelFromString(QStringLiteral("info")), 2);
    QCOMPARE(SettingsController::logLevelFromString(QStringLiteral("warn")), 3);
    QCOMPARE(SettingsController::logLevelFromString(QStringLiteral("error")), 4);
    QCOMPARE(SettingsController::logLevelFromString(QStringLiteral("critical")), 5);
    QCOMPARE(SettingsController::logLevelFromString(QStringLiteral("off")), -1);
    QCOMPARE(SettingsController::logLevelFromString(QStringLiteral("bogus")), -1);
    QCOMPARE(SettingsController::logLevelFromString(QString()), -1);

    // 枚举 → 字符串（往返一致）
    QCOMPARE(SettingsController::logLevelToString(0), QStringLiteral("trace"));
    QCOMPARE(SettingsController::logLevelToString(1), QStringLiteral("debug"));
    QCOMPARE(SettingsController::logLevelToString(2), QStringLiteral("info"));
    QCOMPARE(SettingsController::logLevelToString(3), QStringLiteral("warn"));
    QCOMPARE(SettingsController::logLevelToString(4), QStringLiteral("error"));
    QCOMPARE(SettingsController::logLevelToString(5), QStringLiteral("critical"));
    QCOMPARE(SettingsController::logLevelToString(6), QString());
    QCOMPARE(SettingsController::logLevelToString(-1), QString());
}

void SettingsControllerTest::logLevelPushAndDefense()
{
    Seriona::App::SettingsController settings;
    settings.setSettingsStorageBackend(testBackend());
    QList<int> pushed;
    settings.setLogLevelExecutor([&pushed](int level) {
        pushed.append(level);
    });

    // 离散变更立即推送，值正确
    settings.setLogLevel(1);
    QCOMPARE(pushed, QList<int>({1}));
    settings.setLogLevel(4);
    QCOMPARE(pushed, QList<int>({1, 4}));

    // 相同值不重复推送
    settings.setLogLevel(4);
    QCOMPARE(pushed.size(), 2);

    // 越界值防御：spdlog::level::level_enum 是 int 底层枚举，
    // 越界值会破坏 should_log 比较，前端直接拒绝（不推送、不持久化）
    settings.setLogLevel(-1);
    settings.setLogLevel(6);
    settings.setLogLevel(99);
    QCOMPARE(settings.logLevel(), 4);
    QCOMPARE(pushed.size(), 2);

    // 拒绝不写入存储
    QCOMPARE(storedValue(QStringLiteral("logging"), QStringLiteral("logLevel")).toInt(), 4);

    // applyLogLevel：推送当前值，不持久化（启动路径：reload 后同步后端）
    removeStored(QStringLiteral("logging"), QStringLiteral("logLevel"));
    settings.applyLogLevel();
    QCOMPARE(pushed.size(), 3);
    QCOMPARE(pushed.at(2), 4);
    QVERIFY(!storedContains(QStringLiteral("logging"), QStringLiteral("logLevel")));

    // 无 executor（mock-only）时 setter/applyLogLevel 无副作用
    Seriona::App::SettingsController mockController;
    mockController.setLogLevel(3);
    mockController.applyLogLevel();
    QCOMPARE(mockController.logLevel(), 3);

    // reload 防御：存储中的非法值回退默认
    m_store.insert(storageKey(QStringLiteral("logging"), QStringLiteral("logLevel")), 42);
    Seriona::App::SettingsController corruptReader;
    corruptReader.setSettingsStorageBackend(testBackend());
    corruptReader.reloadFromSettings();
    QCOMPARE(corruptReader.logLevel(), 2);
}

void SettingsControllerTest::lyricDelimitersPersistRoundTrip()
{
    const QStringList delimiters{QStringLiteral(" / "), QStringLiteral(" | "), QStringLiteral(" - ")};
    {
        Seriona::App::SettingsController writer;
        writer.setSettingsStorageBackend(testBackend());
        writer.setLyricDelimiters(delimiters);
        QCOMPARE(writer.lyricDelimiters(), delimiters);
    }

    Seriona::App::SettingsController reader;
    reader.setSettingsStorageBackend(testBackend());
    reader.reloadFromSettings();
    QCOMPARE(reader.lyricDelimiters(), delimiters);

    // 空列表语义：合法（清空后歌词不切分），持久化并重读一致，不得崩溃
    {
        Seriona::App::SettingsController emptyWriter;
        emptyWriter.setSettingsStorageBackend(testBackend());
        emptyWriter.setLyricDelimiters({});
        QVERIFY(emptyWriter.lyricDelimiters().isEmpty());
    }
    Seriona::App::SettingsController emptyReader;
    emptyReader.setSettingsStorageBackend(testBackend());
    emptyReader.reloadFromSettings();
    QVERIFY(emptyReader.lyricDelimiters().isEmpty());
}

void SettingsControllerTest::followRestoreDelayPersistRoundTrip()
{
    {
        Seriona::App::SettingsController writer;
        writer.setSettingsStorageBackend(testBackend());
        writer.setFollowRestoreDelayMs(8000);
        QCOMPARE(writer.followRestoreDelayMs(), 8000);
    }

    // 持久化键位于 lyrics 组（与分隔符同组），值为毫秒 int
    QCOMPARE(storedValue(QStringLiteral("lyrics"), QStringLiteral("followRestoreDelayMs")).toInt(), 8000);

    Seriona::App::SettingsController reader;
    reader.setSettingsStorageBackend(testBackend());
    reader.reloadFromSettings();
    QCOMPARE(reader.followRestoreDelayMs(), 8000);

    // 未写入时默认 5000
    removeStored(QStringLiteral("lyrics"), QStringLiteral("followRestoreDelayMs"));
    Seriona::App::SettingsController defaultReader;
    defaultReader.setSettingsStorageBackend(testBackend());
    defaultReader.reloadFromSettings();
    QCOMPARE(defaultReader.followRestoreDelayMs(), 5000);

    // reload 防御：存储中的非法值回退默认
    m_store.insert(storageKey(QStringLiteral("lyrics"), QStringLiteral("followRestoreDelayMs")), 123456);
    Seriona::App::SettingsController corruptReader;
    corruptReader.setSettingsStorageBackend(testBackend());
    corruptReader.reloadFromSettings();
    QCOMPARE(corruptReader.followRestoreDelayMs(), 5000);
}

void SettingsControllerTest::sampleFormatPersistRoundTrip()
{
    {
        Seriona::App::SettingsController writer;
        writer.setSettingsStorageBackend(testBackend());
        writer.setSampleFormat(4);
        QCOMPARE(writer.sampleFormat(), 4);
    }

    Seriona::App::SettingsController reader;
    reader.setSettingsStorageBackend(testBackend());
    reader.reloadFromSettings();
    QCOMPARE(reader.sampleFormat(), 4);

    // 未写入时默认 0
    removeStored(QStringLiteral("output"), QStringLiteral("sampleFormat"));
    Seriona::App::SettingsController defaultReader;
    defaultReader.setSettingsStorageBackend(testBackend());
    defaultReader.reloadFromSettings();
    QCOMPARE(defaultReader.sampleFormat(), 0);
}

void SettingsControllerTest::applyIncludesSampleFormat()
{
    Seriona::App::SettingsController settings;
    QStringList payloads;
    settings.setApplyOutputConfigExecutor(
        [&payloads](int, int, int sampleFormat, int, const QString &) {
            payloads.append(QString::number(sampleFormat));
        });

    settings.setSampleFormat(2);
    QCOMPARE(payloads.size(), 1);
    QCOMPARE(payloads.at(0), QStringLiteral("2"));

    settings.apply();
    QCOMPARE(payloads.size(), 2);
    QCOMPARE(payloads.at(1), QStringLiteral("2"));
}

void SettingsControllerTest::rollbackRejectedOutputConfigRestoresSnapshot()
{
    Seriona::App::SettingsController settings;
    settings.setSettingsStorageBackend(testBackend());
    int pushes = 0;
    settings.setApplyOutputConfigExecutor(
        [&pushes](int, int, int, int, const QString &) {
            ++pushes;
        });

    // 无快照（从未提交）时回退忽略
    settings.rollbackRejectedOutputConfig();
    QCOMPARE(settings.sampleFormat(), 0);
    QCOMPARE(pushes, 0);

    // 离散 setter 每次提交并记录快照；bufferDurationMs 去抖未提交
    settings.setSampleFormat(2);
    settings.setBufferDurationMs(500);
    QCOMPARE(pushes, 1);

    // 拒绝回退 → 恢复最近一次已提交的值，未提交的 bufferDurationMs 变更被回滚
    settings.rollbackRejectedOutputConfig();
    QCOMPARE(settings.sampleRate(), 48000);
    QCOMPARE(settings.sampleFormat(), 2);
    QCOMPARE(settings.bufferDurationMs(), 300);
    QCOMPARE(settings.preferredDeviceId(), QString());
    // 回退本身不推送
    QCOMPARE(pushes, 1);

    // 回退不持久化：存储保持 setter 写入的值
    QCOMPARE(storedValue(QStringLiteral("output"), QStringLiteral("sampleFormat")).toInt(), 2);
    QCOMPARE(storedValue(QStringLiteral("output"), QStringLiteral("bufferDurationMs")).toInt(), 500);
}

void SettingsControllerTest::startupPushSequence()
{
    Seriona::App::SettingsController settings;
    settings.setSettingsStorageBackend(testBackend());
    QStringList payloads;
    settings.setApplyOutputConfigExecutor(
        [&payloads](int mode, int sampleRate, int sampleFormat, int bufferDurationMs, const QString &preferredDeviceId) {
            payloads.append(QStringLiteral("%1|%2|%3|%4")
                                .arg(mode)
                                .arg(sampleRate)
                                .arg(bufferDurationMs)
                                .arg(preferredDeviceId));
        });

    // 预置历史版本遗留的旧存储键 → reload 忽略该键（纯字符串数据，非当前键集），
    // 启动路径 reloadFromSettings → apply 仍恰好推送一次默认值；断言经 push 载荷
    m_store.insert(storageKey(QStringLiteral("output"), QStringLiteral("outputMode")), 0);

    settings.reloadFromSettings();
    settings.apply();

    QCOMPARE(payloads.size(), 1);
    QCOMPARE(payloads.at(0), QStringLiteral("1|48000|300|"));
}

void SettingsControllerTest::transitionDefaults()
{
    Seriona::App::SettingsController settings;

    // 裁定表默认值：自动档无(0)/开关关/false/预加载 0/交叉 3000/传送 300/seek 300/
    // 手动档无(0)/手动短交叉 500；滑块步进 100ms
    QCOMPARE(settings.autoAdvanceFadeMode(), 0);
    QCOMPARE(settings.fadeOnTransport(), false);
    QCOMPARE(settings.fadeOnSeek(), false);
    QCOMPARE(settings.gaplessPreloadMs(), 0);
    QCOMPARE(settings.crossfadeMs(), 3000);
    QCOMPARE(settings.transportFadeMs(), 300);
    QCOMPARE(settings.seekFadeMs(), 300);
    QCOMPARE(settings.manualAdvanceFadeMode(), 0);
    QCOMPARE(settings.manualShortCrossfadeMs(), 500);
    QCOMPARE(settings.transitionSliderStepMs(), 100);
}

void SettingsControllerTest::transitionPropertySettersPersistAndNotify()
{
    Seriona::App::SettingsController settings;
    settings.setSettingsStorageBackend(testBackend());
    QSignalSpy autoSpy(&settings, &Seriona::App::SettingsController::autoAdvanceFadeModeChanged);
    QSignalSpy transportSpy(&settings, &Seriona::App::SettingsController::fadeOnTransportChanged);
    QSignalSpy seekSpy(&settings, &Seriona::App::SettingsController::fadeOnSeekChanged);
    QSignalSpy preloadSpy(&settings, &Seriona::App::SettingsController::gaplessPreloadMsChanged);
    QSignalSpy crossfadeSpy(&settings, &Seriona::App::SettingsController::crossfadeMsChanged);
    QSignalSpy transportFadeSpy(&settings, &Seriona::App::SettingsController::transportFadeMsChanged);
    QSignalSpy seekFadeSpy(&settings, &Seriona::App::SettingsController::seekFadeMsChanged);
    QSignalSpy manualSpy(&settings, &Seriona::App::SettingsController::manualAdvanceFadeModeChanged);
    QSignalSpy manualShortSpy(&settings, &Seriona::App::SettingsController::manualShortCrossfadeMsChanged);

    settings.setAutoAdvanceFadeMode(1);
    settings.setFadeOnTransport(true);
    settings.setFadeOnSeek(true);
    settings.setGaplessPreloadMs(1200);
    settings.setCrossfadeMs(4500);
    settings.setTransportFadeMs(600);
    settings.setSeekFadeMs(700);
    settings.setManualAdvanceFadeMode(2);
    settings.setManualShortCrossfadeMs(800);

    QCOMPARE(settings.autoAdvanceFadeMode(), 1);
    QCOMPARE(settings.fadeOnTransport(), true);
    QCOMPARE(settings.fadeOnSeek(), true);
    QCOMPARE(settings.gaplessPreloadMs(), 1200);
    QCOMPARE(settings.crossfadeMs(), 4500);
    QCOMPARE(settings.transportFadeMs(), 600);
    QCOMPARE(settings.seekFadeMs(), 700);
    QCOMPARE(settings.manualAdvanceFadeMode(), 2);
    QCOMPARE(settings.manualShortCrossfadeMs(), 800);
    QCOMPARE(autoSpy.count(), 1);
    QCOMPARE(transportSpy.count(), 1);
    QCOMPARE(seekSpy.count(), 1);
    QCOMPARE(preloadSpy.count(), 1);
    QCOMPARE(crossfadeSpy.count(), 1);
    QCOMPARE(transportFadeSpy.count(), 1);
    QCOMPARE(seekFadeSpy.count(), 1);
    QCOMPARE(manualSpy.count(), 1);
    QCOMPARE(manualShortSpy.count(), 1);

    // 相同值不重复 NOTIFY
    settings.setAutoAdvanceFadeMode(1);
    settings.setFadeOnTransport(true);
    settings.setCrossfadeMs(4500);
    QCOMPARE(autoSpy.count(), 1);
    QCOMPARE(transportSpy.count(), 1);
    QCOMPARE(crossfadeSpy.count(), 1);

    // 键名恰为 transition 组 9 键（无 stray 键写入其它组）
    QCOMPARE(storedValue(QStringLiteral("transition"), QStringLiteral("autoAdvanceFadeMode")).toInt(), 1);
    QCOMPARE(storedValue(QStringLiteral("transition"), QStringLiteral("fadeOnTransport")).toBool(), true);
    QCOMPARE(storedValue(QStringLiteral("transition"), QStringLiteral("fadeOnSeek")).toBool(), true);
    QCOMPARE(storedValue(QStringLiteral("transition"), QStringLiteral("gaplessPreloadMs")).toInt(), 1200);
    QCOMPARE(storedValue(QStringLiteral("transition"), QStringLiteral("crossfadeMs")).toInt(), 4500);
    QCOMPARE(storedValue(QStringLiteral("transition"), QStringLiteral("transportFadeMs")).toInt(), 600);
    QCOMPARE(storedValue(QStringLiteral("transition"), QStringLiteral("seekFadeMs")).toInt(), 700);
    QCOMPARE(storedValue(QStringLiteral("transition"), QStringLiteral("manualAdvanceFadeMode")).toInt(), 2);
    QCOMPARE(storedValue(QStringLiteral("transition"), QStringLiteral("manualShortCrossfadeMs")).toInt(), 800);
    QVERIFY(!storedContains(QStringLiteral("output"), QStringLiteral("autoAdvanceFadeMode")));
    QVERIFY(!storedContains(QStringLiteral("output"), QStringLiteral("crossfadeMs")));
    QVERIFY(!storedContains(QStringLiteral("lyrics"), QStringLiteral("crossfadeMs")));
    QVERIFY(!storedContains(QStringLiteral("logging"), QStringLiteral("crossfadeMs")));
}

void SettingsControllerTest::transitionInvalidValuesRejected()
{
    Seriona::App::SettingsController settings;
    settings.setSettingsStorageBackend(testBackend());

    // 枚举仅 0-2
    settings.setAutoAdvanceFadeMode(3);
    QCOMPARE(settings.autoAdvanceFadeMode(), 0);
    settings.setAutoAdvanceFadeMode(-1);
    QCOMPARE(settings.autoAdvanceFadeMode(), 0);
    settings.setManualAdvanceFadeMode(3);
    QCOMPARE(settings.manualAdvanceFadeMode(), 0);
    settings.setManualAdvanceFadeMode(7);
    QCOMPARE(settings.manualAdvanceFadeMode(), 0);

    // crossfadeMs 0-10000
    settings.setCrossfadeMs(-1);
    QCOMPARE(settings.crossfadeMs(), 3000);
    settings.setCrossfadeMs(10001);
    QCOMPARE(settings.crossfadeMs(), 3000);
    // 非法值保留旧值（先置合法再试越界）
    settings.setCrossfadeMs(4000);
    settings.setCrossfadeMs(10001);
    QCOMPARE(settings.crossfadeMs(), 4000);

    // transportFadeMs/seekFadeMs/manualShortCrossfadeMs 0-3000
    settings.setTransportFadeMs(3001);
    QCOMPARE(settings.transportFadeMs(), 300);
    settings.setSeekFadeMs(-1);
    QCOMPARE(settings.seekFadeMs(), 300);
    settings.setSeekFadeMs(3001);
    QCOMPARE(settings.seekFadeMs(), 300);
    settings.setManualShortCrossfadeMs(-5);
    QCOMPARE(settings.manualShortCrossfadeMs(), 500);
    settings.setManualShortCrossfadeMs(3001);
    QCOMPARE(settings.manualShortCrossfadeMs(), 500);

    // gaplessPreloadMs 0-5000
    settings.setGaplessPreloadMs(-1);
    QCOMPARE(settings.gaplessPreloadMs(), 0);
    settings.setGaplessPreloadMs(5001);
    QCOMPARE(settings.gaplessPreloadMs(), 0);

    // 边界值合法（0 合法：无交叉/无预加载即时语义；最大值合法）
    settings.setAutoAdvanceFadeMode(2);
    settings.setCrossfadeMs(0);
    QCOMPARE(settings.crossfadeMs(), 0);
    settings.setCrossfadeMs(10000);
    settings.setGaplessPreloadMs(5000);
    settings.setTransportFadeMs(0);
    settings.setTransportFadeMs(3000);
    settings.setSeekFadeMs(0);
    settings.setSeekFadeMs(3000);
    settings.setManualShortCrossfadeMs(0);
    settings.setManualShortCrossfadeMs(3000);
    QCOMPARE(settings.autoAdvanceFadeMode(), 2);
    QCOMPARE(settings.crossfadeMs(), 10000);
    QCOMPARE(settings.gaplessPreloadMs(), 5000);
    QCOMPARE(settings.transportFadeMs(), 3000);
    QCOMPARE(settings.seekFadeMs(), 3000);
    QCOMPARE(settings.manualShortCrossfadeMs(), 3000);

    // 非法值不写入存储
    QCOMPARE(storedValue(QStringLiteral("transition"), QStringLiteral("crossfadeMs")).toInt(), 10000);
    settings.setCrossfadeMs(20000);
    QCOMPARE(storedValue(QStringLiteral("transition"), QStringLiteral("crossfadeMs")).toInt(), 10000);
    QCOMPARE(storedValue(QStringLiteral("transition"), QStringLiteral("gaplessPreloadMs")).toInt(), 5000);
    QCOMPARE(storedValue(QStringLiteral("transition"), QStringLiteral("manualAdvanceFadeMode")).toInt(), 0);
}

void SettingsControllerTest::transitionSliderSettersDebounceMerged()
{
    Seriona::App::SettingsController settings;
    QStringList payloads;
    settings.setApplyTransitionConfigExecutor(
        [&payloads](int autoAdvanceFadeMode, bool fadeOnTransport, bool fadeOnSeek, int gaplessPreloadMs,
                    int crossfadeMs, int transportFadeMs, int seekFadeMs, int manualAdvanceFadeMode,
                    int manualShortCrossfadeMs) {
            payloads.append(QStringLiteral("%1|%2|%3|%4|%5|%6|%7|%8|%9")
                                .arg(autoAdvanceFadeMode)
                                .arg(fadeOnTransport)
                                .arg(fadeOnSeek)
                                .arg(gaplessPreloadMs)
                                .arg(crossfadeMs)
                                .arg(transportFadeMs)
                                .arg(seekFadeMs)
                                .arg(manualAdvanceFadeMode)
                                .arg(manualShortCrossfadeMs));
        });
    // 过渡滑块去抖不得触发 output executor
    int outputPushes = 0;
    settings.setApplyOutputConfigExecutor(
        [&outputPushes](int, int, int, int, const QString &) {
            ++outputPushes;
        });

    // 档位/开关立即推送
    settings.setAutoAdvanceFadeMode(1);
    settings.setFadeOnTransport(true);
    QCOMPARE(payloads.size(), 2);
    QCOMPARE(payloads.at(0), QStringLiteral("1|0|0|0|3000|300|300|0|500"));
    QCOMPARE(payloads.at(1), QStringLiteral("1|1|0|0|3000|300|300|0|500"));
    QCOMPARE(outputPushes, 0);

    // 滑块多次变更（跨 5 把滑块交错）合并为一次下发，末值正确
    settings.setCrossfadeMs(100);
    settings.setCrossfadeMs(200);
    settings.setManualShortCrossfadeMs(600);
    settings.setGaplessPreloadMs(200);
    settings.setTransportFadeMs(400);
    settings.setSeekFadeMs(800);
    settings.setCrossfadeMs(900);
    QCOMPARE(payloads.size(), 2);
    QCOMPARE(settings.crossfadeMs(), 900);
    QCOMPARE(settings.manualShortCrossfadeMs(), 600);
    QCOMPARE(settings.gaplessPreloadMs(), 200);
    QCOMPARE(settings.transportFadeMs(), 400);
    QCOMPARE(settings.seekFadeMs(), 800);
    // 去抖窗口内 output executor 不受影响
    QCOMPARE(outputPushes, 0);

    QTRY_COMPARE_WITH_TIMEOUT(payloads.size(), 3, 5000);
    QCOMPARE(payloads.at(2), QStringLiteral("1|1|0|200|900|400|800|0|600"));

    // 不再有额外推送
    QTest::qWait(800);
    QCOMPARE(payloads.size(), 3);
    QCOMPARE(outputPushes, 0);
}

void SettingsControllerTest::transitionPersistRoundTrip()
{
    {
        Seriona::App::SettingsController writer;
        writer.setSettingsStorageBackend(testBackend());
        writer.setAutoAdvanceFadeMode(2);
        writer.setFadeOnTransport(true);
        writer.setFadeOnSeek(true);
        writer.setGaplessPreloadMs(1500);
        writer.setCrossfadeMs(6000);
        writer.setTransportFadeMs(900);
        writer.setSeekFadeMs(100);
        writer.setManualAdvanceFadeMode(1);
        writer.setManualShortCrossfadeMs(250);
        QCOMPARE(writer.autoAdvanceFadeMode(), 2);
        QCOMPARE(writer.manualAdvanceFadeMode(), 1);
    }

    Seriona::App::SettingsController reader;
    reader.setSettingsStorageBackend(testBackend());
    int pushes = 0;
    reader.setApplyTransitionConfigExecutor(
        [&pushes](int, bool, bool, int, int, int, int, int, int) {
            ++pushes;
        });
    reader.reloadFromSettings();

    QCOMPARE(reader.autoAdvanceFadeMode(), 2);
    QCOMPARE(reader.fadeOnTransport(), true);
    QCOMPARE(reader.fadeOnSeek(), true);
    QCOMPARE(reader.gaplessPreloadMs(), 1500);
    QCOMPARE(reader.crossfadeMs(), 6000);
    QCOMPARE(reader.transportFadeMs(), 900);
    QCOMPARE(reader.seekFadeMs(), 100);
    QCOMPARE(reader.manualAdvanceFadeMode(), 1);
    QCOMPARE(reader.manualShortCrossfadeMs(), 250);
    // reload 只还原属性，不推送
    QCOMPARE(pushes, 0);

    // 未写入时全部回默认（含默认值情形 roundtrip）
    m_store.clear();
    Seriona::App::SettingsController defaultReader;
    defaultReader.setSettingsStorageBackend(testBackend());
    defaultReader.reloadFromSettings();
    QCOMPARE(defaultReader.autoAdvanceFadeMode(), 0);
    QCOMPARE(defaultReader.fadeOnTransport(), false);
    QCOMPARE(defaultReader.fadeOnSeek(), false);
    QCOMPARE(defaultReader.gaplessPreloadMs(), 0);
    QCOMPARE(defaultReader.crossfadeMs(), 3000);
    QCOMPARE(defaultReader.transportFadeMs(), 300);
    QCOMPARE(defaultReader.seekFadeMs(), 300);
    QCOMPARE(defaultReader.manualAdvanceFadeMode(), 0);
    QCOMPARE(defaultReader.manualShortCrossfadeMs(), 500);

    // reload 防御：存储中的非法值回退默认
    m_store.clear();
    m_store.insert(storageKey(QStringLiteral("transition"), QStringLiteral("crossfadeMs")), 99999);
    m_store.insert(storageKey(QStringLiteral("transition"), QStringLiteral("autoAdvanceFadeMode")), 7);
    m_store.insert(storageKey(QStringLiteral("transition"), QStringLiteral("fadeOnSeek")), true);
    Seriona::App::SettingsController corruptReader;
    corruptReader.setSettingsStorageBackend(testBackend());
    corruptReader.reloadFromSettings();
    QCOMPARE(corruptReader.crossfadeMs(), 3000);
    QCOMPARE(corruptReader.autoAdvanceFadeMode(), 0);
    QCOMPARE(corruptReader.fadeOnSeek(), true);
}

void SettingsControllerTest::transitionApplyPacksCurrentNineFields()
{
    Seriona::App::SettingsController settings;
    QStringList payloads;
    settings.setApplyTransitionConfigExecutor(
        [&payloads](int autoAdvanceFadeMode, bool fadeOnTransport, bool fadeOnSeek, int gaplessPreloadMs,
                    int crossfadeMs, int transportFadeMs, int seekFadeMs, int manualAdvanceFadeMode,
                    int manualShortCrossfadeMs) {
            payloads.append(QStringLiteral("%1|%2|%3|%4|%5|%6|%7|%8|%9")
                                .arg(autoAdvanceFadeMode)
                                .arg(fadeOnTransport)
                                .arg(fadeOnSeek)
                                .arg(gaplessPreloadMs)
                                .arg(crossfadeMs)
                                .arg(transportFadeMs)
                                .arg(seekFadeMs)
                                .arg(manualAdvanceFadeMode)
                                .arg(manualShortCrossfadeMs));
        });

    // 直接调用（AppFacade 启动路径形态）：默认值按契约顺序组包（毫秒 int / bool 0-1）
    settings.applyTransitionConfig();
    QCOMPARE(payloads.size(), 1);
    QCOMPARE(payloads.at(0), QStringLiteral("0|0|0|0|3000|300|300|0|500"));

    // 变更后再次组包：档位/开关 setter 各自立即推送；滑块走去抖（无事件循环不触发）；
    // 显式调用推送全量末值 → 共 6 次记录，末条 = 全部 9 字段末值
    settings.setAutoAdvanceFadeMode(2);
    settings.setFadeOnTransport(true);
    settings.setFadeOnSeek(true);
    settings.setGaplessPreloadMs(1200);
    settings.setCrossfadeMs(2500);
    settings.setTransportFadeMs(600);
    settings.setSeekFadeMs(700);
    settings.setManualAdvanceFadeMode(1);
    settings.setManualShortCrossfadeMs(800);
    settings.applyTransitionConfig();
    QCOMPARE(payloads.size(), 6);
    QCOMPARE(payloads.at(1), QStringLiteral("2|0|0|0|3000|300|300|0|500"));
    QCOMPARE(payloads.at(2), QStringLiteral("2|1|0|0|3000|300|300|0|500"));
    QCOMPARE(payloads.at(3), QStringLiteral("2|1|1|0|3000|300|300|0|500"));
    // 手动档 setter 立即推送时滑块属性已更新（去抖未触发）→ 推送全量现值
    QCOMPARE(payloads.at(4), QStringLiteral("2|1|1|1200|2500|600|700|1|500"));
    QCOMPARE(payloads.at(5), QStringLiteral("2|1|1|1200|2500|600|700|1|800"));
}

void SettingsControllerTest::transitionExecutorUnsetNoopPersistence()
{
    // mock-only：executor 未设（AppFacade 无后端时不注入）→ setter/applyTransitionConfig
    // 均为 no-op（不崩、不推送），本地持久化仍工作
    Seriona::App::SettingsController settings;
    settings.setSettingsStorageBackend(testBackend());

    settings.setAutoAdvanceFadeMode(2);
    settings.setFadeOnTransport(true);
    settings.setFadeOnSeek(true);
    settings.setGaplessPreloadMs(1500);
    settings.setCrossfadeMs(6000);
    settings.setTransportFadeMs(900);
    settings.setSeekFadeMs(100);
    settings.setManualAdvanceFadeMode(1);
    settings.setManualShortCrossfadeMs(250);
    settings.applyTransitionConfig();

    QCOMPARE(settings.autoAdvanceFadeMode(), 2);
    QCOMPARE(settings.crossfadeMs(), 6000);
    QCOMPARE(storedValue(QStringLiteral("transition"), QStringLiteral("autoAdvanceFadeMode")).toInt(), 2);
    QCOMPARE(storedValue(QStringLiteral("transition"), QStringLiteral("fadeOnTransport")).toBool(), true);
    QCOMPARE(storedValue(QStringLiteral("transition"), QStringLiteral("fadeOnSeek")).toBool(), true);
    QCOMPARE(storedValue(QStringLiteral("transition"), QStringLiteral("gaplessPreloadMs")).toInt(), 1500);
    QCOMPARE(storedValue(QStringLiteral("transition"), QStringLiteral("crossfadeMs")).toInt(), 6000);
    QCOMPARE(storedValue(QStringLiteral("transition"), QStringLiteral("transportFadeMs")).toInt(), 900);
    QCOMPARE(storedValue(QStringLiteral("transition"), QStringLiteral("seekFadeMs")).toInt(), 100);
    QCOMPARE(storedValue(QStringLiteral("transition"), QStringLiteral("manualAdvanceFadeMode")).toInt(), 1);
    QCOMPARE(storedValue(QStringLiteral("transition"), QStringLiteral("manualShortCrossfadeMs")).toInt(), 250);

    // reload（无 executor）值完整还原、零推送
    Seriona::App::SettingsController reader;
    reader.setSettingsStorageBackend(testBackend());
    int pushes = 0;
    reader.setApplyTransitionConfigExecutor(
        [&pushes](int, bool, bool, int, int, int, int, int, int) {
            ++pushes;
        });
    reader.reloadFromSettings();
    QCOMPARE(reader.autoAdvanceFadeMode(), 2);
    QCOMPARE(reader.fadeOnTransport(), true);
    QCOMPARE(reader.gaplessPreloadMs(), 1500);
    QCOMPARE(reader.crossfadeMs(), 6000);
    QCOMPARE(reader.transportFadeMs(), 900);
    QCOMPARE(reader.seekFadeMs(), 100);
    QCOMPARE(reader.manualAdvanceFadeMode(), 1);
    QCOMPARE(reader.manualShortCrossfadeMs(), 250);
    QCOMPARE(pushes, 0);
}

void SettingsControllerTest::transitionStartupApplySequence()
{
    // mock roundtrip：持久化 → reload → apply 调用记录（AppFacade 启动挂钩契约）
    {
        Seriona::App::SettingsController writer;
        writer.setSettingsStorageBackend(testBackend());
        writer.setAutoAdvanceFadeMode(1);
        writer.setFadeOnTransport(true);
        writer.setCrossfadeMs(2500);
        writer.setSeekFadeMs(700);
    }

    Seriona::App::SettingsController settings;
    settings.setSettingsStorageBackend(testBackend());
    QStringList transitionPayloads;
    settings.setApplyTransitionConfigExecutor(
        [&transitionPayloads](int autoAdvanceFadeMode, bool fadeOnTransport, bool fadeOnSeek, int gaplessPreloadMs,
                              int crossfadeMs, int transportFadeMs, int seekFadeMs, int manualAdvanceFadeMode,
                              int manualShortCrossfadeMs) {
            transitionPayloads.append(QStringLiteral("%1|%2|%3|%4|%5|%6|%7|%8|%9")
                                          .arg(autoAdvanceFadeMode)
                                          .arg(fadeOnTransport)
                                          .arg(fadeOnSeek)
                                          .arg(gaplessPreloadMs)
                                          .arg(crossfadeMs)
                                          .arg(transportFadeMs)
                                          .arg(seekFadeMs)
                                          .arg(manualAdvanceFadeMode)
                                          .arg(manualShortCrossfadeMs));
        });
    int outputPushes = 0;
    settings.setApplyOutputConfigExecutor(
        [&outputPushes](int, int, int, int, const QString &) {
            ++outputPushes;
        });

    // 启动路径：reloadFromSettings（不推送）→ apply + applyTransitionConfig 各恰好一次；
    // 过渡组载荷 = reload 还原的持久化值（缺省键回默认）
    settings.reloadFromSettings();
    QCOMPARE(transitionPayloads.size(), 0);
    QCOMPARE(outputPushes, 0);

    settings.apply();
    settings.applyTransitionConfig();
    QCOMPARE(transitionPayloads.size(), 1);
    QCOMPARE(outputPushes, 1);
    QCOMPARE(transitionPayloads.at(0), QStringLiteral("1|1|0|0|2500|300|700|0|500"));

    // 通道分离：apply（输出组）不触发过渡 executor，反之亦然
    settings.applyTransitionConfig();
    settings.apply();
    QCOMPARE(transitionPayloads.size(), 2);
    QCOMPARE(outputPushes, 2);
}

void SettingsControllerTest::eqDefaults()
{
    Seriona::App::SettingsController settings;

    // 默认：enabled=false / bandMode=10 / preGain 0.0 / 两档增益全 0 定长 10|31 /
    // limiter=false / spectrum=false；订阅镜像与用户预设区为空
    QCOMPARE(settings.enabled(), false);
    QCOMPARE(settings.bandMode(), kEqBandMode10);
    QCOMPARE(settings.preGainDb(), 0.0);
    QCOMPARE(settings.bandGains10().size(), kEqBandCount10);
    QCOMPARE(settings.bandGains31().size(), kEqBandCount31);
    for (const QVariant &gain : settings.bandGains10()) {
        QCOMPARE(gain.toDouble(), 0.0);
    }
    for (const QVariant &gain : settings.bandGains31()) {
        QCOMPARE(gain.toDouble(), 0.0);
    }
    QCOMPARE(settings.limiterEnabled(), false);
    QCOMPARE(settings.spectrumEnabled(), false);
    QVERIFY(settings.spectrumBins().isEmpty());
    QVERIFY(settings.curvePoints().isEmpty());
    QVERIFY(settings.curveFrequencies().isEmpty());

    // 预设列表默认 = 内置 8 款恒序在前（builtin=true 只读，10 档 gains 定长）
    const QVariantList presets = settings.eqPresetList();
    QCOMPARE(presets.size(), 8);
    const QStringList builtinIds{QStringLiteral("flat"), QStringLiteral("bass-boost"), QStringLiteral("treble-boost"),
                                 QStringLiteral("vocal"), QStringLiteral("pop"), QStringLiteral("rock"),
                                 QStringLiteral("classical"), QStringLiteral("jazz")};
    for (int i = 0; i < presets.size(); ++i) {
        const QVariantMap entry = presets.at(i).toMap();
        QCOMPARE(entry.value(QStringLiteral("id")).toString(), builtinIds.at(i));
        QVERIFY(entry.value(QStringLiteral("builtin")).toBool());
        QCOMPARE(entry.value(QStringLiteral("preGainDb")).toDouble(), 0.0);
        QCOMPARE(entry.value(QStringLiteral("gains")).toList().size(), kEqBandCount10);
    }
}

void SettingsControllerTest::eqPersistRoundTrip()
{
    // 0.5 步进全在 0.1 增益网格上（写入归一化后与输入逐位一致，double 精度不损）
    const QVariantList gains10 = makeEqGains(kEqBandCount10, -3.0, 0.5);
    const QVariantList gains31 = makeEqGains(kEqBandCount31, -1.5, 0.5);
    {
        Seriona::App::SettingsController writer;
        writer.setSettingsStorageBackend(testBackend());
        writer.setEnabled(true);
        writer.setBandMode(kEqBandMode31);
        writer.setPreGainDb(-3.3);
        writer.setBandGains10(gains10);
        writer.setBandGains31(gains31);
        writer.setLimiterEnabled(true);
        writer.setSpectrumEnabled(true);
        QCOMPARE(writer.enabled(), true);
        QCOMPARE(writer.bandMode(), kEqBandMode31);
        QCOMPARE(writer.preGainDb(), -3.3);
        QCOMPARE(writer.bandGains10(), gains10);
        QCOMPARE(writer.bandGains31(), gains31);
        QCOMPARE(writer.limiterEnabled(), true);
        QCOMPARE(writer.spectrumEnabled(), true);
    }

    // 落盘键 = equalizer 组 7 个用户可设键（无 stray 键、无镜像键）
    QCOMPARE(m_store.size(), 7);
    QCOMPARE(storedValue(kEqGroup, QStringLiteral("enabled")).toBool(), true);
    QCOMPARE(storedValue(kEqGroup, QStringLiteral("bandMode")).toInt(), kEqBandMode31);
    QCOMPARE(storedValue(kEqGroup, QStringLiteral("preGainDb")).toDouble(), -3.3);
    QCOMPARE(storedValue(kEqGroup, QStringLiteral("bandGains10")).toList(), gains10);
    QCOMPARE(storedValue(kEqGroup, QStringLiteral("bandGains31")).toList(), gains31);
    QCOMPARE(storedValue(kEqGroup, QStringLiteral("limiterEnabled")).toBool(), true);
    QCOMPARE(storedValue(kEqGroup, QStringLiteral("spectrumEnabled")).toBool(), true);

    // 第二实例共享假后端 → reload 值完整还原（含小数 double）；reload 不推送
    Seriona::App::SettingsController reader;
    reader.setSettingsStorageBackend(testBackend());
    int pushes = 0;
    reader.setApplyEqualizerConfigExecutor(
        [&pushes](bool, int, double, const QVariantList &, bool, bool) {
            ++pushes;
        });
    reader.reloadFromSettings();
    QCOMPARE(reader.enabled(), true);
    QCOMPARE(reader.bandMode(), kEqBandMode31);
    QCOMPARE(reader.preGainDb(), -3.3);
    QCOMPARE(reader.bandGains10(), gains10);
    QCOMPARE(reader.bandGains31(), gains31);
    QCOMPARE(reader.limiterEnabled(), true);
    QCOMPARE(reader.spectrumEnabled(), true);
    QCOMPARE(pushes, 0);

    // 镜像 3 键零持久化：存储无 equalizer 组外键；reload 后镜像仍为空默认
    QVERIFY(!storedContains(kEqGroup, QStringLiteral("spectrumBins")));
    QVERIFY(!storedContains(kEqGroup, QStringLiteral("curvePoints")));
    QVERIFY(!storedContains(kEqGroup, QStringLiteral("curveFrequencies")));
    QVERIFY(reader.spectrumBins().isEmpty());
    QVERIFY(reader.curvePoints().isEmpty());
    QVERIFY(reader.curveFrequencies().isEmpty());
}

void SettingsControllerTest::eqMirrorWriterContract()
{
    Seriona::App::SettingsController settings;
    settings.setSettingsStorageBackend(testBackend());
    QSignalSpy pointsSpy(&settings, &Seriona::App::SettingsController::curvePointsChanged);
    QSignalSpy freqsSpy(&settings, &Seriona::App::SettingsController::curveFrequenciesChanged);
    QSignalSpy binsSpy(&settings, &Seriona::App::SettingsController::spectrumBinsChanged);
    int pushes = 0;
    settings.setApplyEqualizerConfigExecutor(
        [&pushes](bool, int, double, const QVariantList &, bool, bool) {
            ++pushes;
        });

    const QVariantList points = makeEqGains(kEqCurvePointCount, 0.0, 1.0);
    const QVariantList freqs = makeEqGains(kEqCurvePointCount, 20.0, 110.5);
    const QVariantList bins = makeEqGains(kEqSpectrumBinCount, -60.0, 1.0);

    // 定长契约：长度不符（曲线 181/180、180/181，频谱 kEqSpectrumBinCount±1）整体丢弃，不落值不发 NOTIFY
    settings.mirrorEqualizerCurve(points, makeEqGains(kEqCurvePointCount - 1, 20.0, 110.5));
    settings.mirrorEqualizerCurve(makeEqGains(kEqCurvePointCount - 1, 0.0, 1.0), freqs);
    settings.mirrorSpectrumBins(makeEqGains(kEqSpectrumBinCount - 1, -60.0, 1.0));
    settings.mirrorSpectrumBins(makeEqGains(kEqSpectrumBinCount + 1, -60.0, 1.0));
    QVERIFY(settings.curvePoints().isEmpty());
    QVERIFY(settings.curveFrequencies().isEmpty());
    QVERIFY(settings.spectrumBins().isEmpty());
    QCOMPARE(pointsSpy.count(), 0);
    QCOMPARE(freqsSpy.count(), 0);
    QCOMPARE(binsSpy.count(), 0);

    // 合法定长（181/181/kEqSpectrumBinCount）→ 落值 + NOTIFY
    settings.mirrorEqualizerCurve(points, freqs);
    settings.mirrorSpectrumBins(bins);
    QCOMPARE(settings.curvePoints(), points);
    QCOMPARE(settings.curveFrequencies(), freqs);
    QCOMPARE(settings.spectrumBins(), bins);
    QCOMPARE(pointsSpy.count(), 1);
    QCOMPARE(freqsSpy.count(), 1);
    QCOMPARE(binsSpy.count(), 1);

    // 同值重推幂等去重：不发 NOTIFY
    settings.mirrorEqualizerCurve(points, freqs);
    settings.mirrorSpectrumBins(bins);
    QCOMPARE(pointsSpy.count(), 1);
    QCOMPARE(freqsSpy.count(), 1);
    QCOMPARE(binsSpy.count(), 1);

    // 变值（单点差）→ 再 NOTIFY，值更新
    QVariantList changedPoints = points;
    changedPoints[kEqCurvePointCount / 2] = changedPoints.at(kEqCurvePointCount / 2).toDouble() + 1.0;
    settings.mirrorEqualizerCurve(changedPoints, freqs);
    QCOMPARE(settings.curvePoints(), changedPoints);
    QCOMPARE(settings.curveFrequencies(), freqs);
    QCOMPARE(pointsSpy.count(), 2);
    QCOMPARE(freqsSpy.count(), 2);

    // 镜像写者零持久化（存储零键零增长）+ 零推送（executor 探针零次）
    QVERIFY(m_store.isEmpty());
    QCOMPARE(pushes, 0);
}

void SettingsControllerTest::eqDebounceMergeAndImmediateCommit()
{
    struct EqPack {
        bool enabled = false;
        int bandMode = 0;
        double preGainDb = 0.0;
        QVariantList gains;
        bool limiterEnabled = false;
        bool spectrumEnabled = false;
    };

    Seriona::App::SettingsController settings;
    settings.setSettingsStorageBackend(testBackend());
    QList<EqPack> packs;
    settings.setApplyEqualizerConfigExecutor(
        [&packs](bool enabled, int bandMode, double preGainDb, const QVariantList &bandGains, bool limiterEnabled,
                 bool spectrumEnabled) {
            packs.append(EqPack{enabled, bandMode, preGainDb, bandGains, limiterEnabled, spectrumEnabled});
        });
    int outputPushes = 0;
    settings.setApplyOutputConfigExecutor(
        [&outputPushes](int, int, int, int, const QString &) {
            ++outputPushes;
        });

    // 连续参数（bandGains10 + preGainDb）多次连写 → 50ms 窗口内零推送，
    // 到期单次推送且载荷 = 全量终值（enabled/bandMode/limiter/spectrum 同步携带）
    const QVariantList final10 = makeEqGains(kEqBandCount10, 1.0, 0.5);
    settings.setBandGains10(makeEqGains(kEqBandCount10, 0.5, 0.5));
    settings.setPreGainDb(2.0);
    settings.setBandGains10(makeEqGains(kEqBandCount10, 0.0, 0.5));
    settings.setPreGainDb(4.0);
    settings.setBandGains10(final10);
    QCOMPARE(packs.size(), 0);
    QTRY_COMPARE_WITH_TIMEOUT(packs.size(), 1, 5000);
    QCOMPARE(packs.at(0).enabled, false);
    QCOMPARE(packs.at(0).bandMode, kEqBandMode10);
    QCOMPARE(packs.at(0).preGainDb, 4.0);
    QCOMPARE(packs.at(0).gains, final10);
    QCOMPARE(packs.at(0).limiterEnabled, false);
    QCOMPARE(packs.at(0).spectrumEnabled, false);
    QTest::qWait(600);
    QCOMPARE(packs.size(), 1);

    // 窗口外再写 → 再推（独立第二次）
    settings.setPreGainDb(-2.5);
    QCOMPARE(packs.size(), 1);
    QTRY_COMPARE_WITH_TIMEOUT(packs.size(), 2, 5000);
    QCOMPARE(packs.at(1).preGainDb, -2.5);
    QCOMPARE(packs.at(1).gains, final10);
    QTest::qWait(600);
    QCOMPARE(packs.size(), 2);

    // 离散项（enabled/bandMode/limiter/spectrum）写即推（不等窗口）
    settings.setEnabled(true);
    QCOMPARE(packs.size(), 3);
    QCOMPARE(packs.at(2).enabled, true);
    settings.setBandMode(kEqBandMode31);
    QCOMPARE(packs.size(), 4);
    QCOMPARE(packs.at(3).bandMode, kEqBandMode31);
    QCOMPARE(packs.at(3).gains.size(), kEqBandCount31);
    settings.setLimiterEnabled(true);
    QCOMPARE(packs.size(), 5);
    QCOMPARE(packs.at(4).limiterEnabled, true);
    settings.setSpectrumEnabled(true);
    QCOMPARE(packs.size(), 6);
    QCOMPARE(packs.at(5).spectrumEnabled, true);
    QCOMPARE(packs.at(5).bandMode, kEqBandMode31);

    // 预设/复位 = 立即项：applyEqPreset 立即推一次（按 31 档取内置数组），resetEq 再立即推
    QVERIFY(settings.applyEqPreset(QStringLiteral("bass-boost")));
    QCOMPARE(packs.size(), 7);
    QCOMPARE(packs.at(6).preGainDb, 0.0);
    QCOMPARE(packs.at(6).gains.size(), kEqBandCount31);
    QVERIFY(settings.resetEq());
    QCOMPARE(packs.size(), 8);
    QCOMPARE(packs.at(7).preGainDb, 0.0);
    QCOMPARE(packs.at(7).gains, makeEqGains(kEqBandCount31, 0.0, 0.0));
    QCOMPARE(packs.at(7).enabled, true);
    QTest::qWait(400);
    QCOMPARE(packs.size(), 8);

    // 启动路径 apply()：EQ executor 顺带推一次全量现值；output executor 各归其道
    QCOMPARE(outputPushes, 0);
    settings.apply();
    QCOMPARE(packs.size(), 9);
    QCOMPARE(packs.at(8).enabled, true);
    QCOMPARE(packs.at(8).bandMode, kEqBandMode31);
    QCOMPARE(outputPushes, 1);

    // 未绑定 executor（默认 mock-only 形态）全链 no-op：不崩、持久化仍工作
    Seriona::App::SettingsController unbound;
    unbound.setSettingsStorageBackend(testBackend());
    unbound.setEnabled(true);
    unbound.setBandMode(kEqBandMode31);
    unbound.setPreGainDb(2.5);
    unbound.setBandGains10(makeEqGains(kEqBandCount10, 0.5, 0.5));
    unbound.setBandGains31(makeEqGains(kEqBandCount31, -1.0, 0.5));
    unbound.setLimiterEnabled(true);
    unbound.setSpectrumEnabled(true);
    QVERIFY(unbound.addEqPreset(QStringLiteral("unbound-preset")));
    QVERIFY(unbound.applyEqPreset(QStringLiteral("flat")));
    QVERIFY(unbound.resetEq());
    QTest::qWait(150); // 去抖到期经未绑定 executor 亦为 no-op（不崩）

    // 复位后落盘终值：enabled/bandMode/limiter/spectrum 保持，两档增益与 preGain 平坦
    QCOMPARE(storedValue(kEqGroup, QStringLiteral("enabled")).toBool(), true);
    QCOMPARE(storedValue(kEqGroup, QStringLiteral("bandMode")).toInt(), kEqBandMode31);
    QCOMPARE(storedValue(kEqGroup, QStringLiteral("limiterEnabled")).toBool(), true);
    QCOMPARE(storedValue(kEqGroup, QStringLiteral("spectrumEnabled")).toBool(), true);
    QCOMPARE(storedValue(kEqGroup, QStringLiteral("preGainDb")).toDouble(), 0.0);
    QCOMPARE(storedValue(kEqGroup, QStringLiteral("bandGains10")).toList(), makeEqGains(kEqBandCount10, 0.0, 0.0));
    QCOMPARE(storedValue(kEqGroup, QStringLiteral("bandGains31")).toList(), makeEqGains(kEqBandCount31, 0.0, 0.0));
    QCOMPARE(storedValue(kEqGroup, QStringLiteral("userPresets31")).toList().size(), 1);

    // 无绑定下的 reload 全链还原（含 31 档用户预设），零推送
    Seriona::App::SettingsController unboundReader;
    unboundReader.setSettingsStorageBackend(testBackend());
    int readerPushes = 0;
    unboundReader.setApplyEqualizerConfigExecutor(
        [&readerPushes](bool, int, double, const QVariantList &, bool, bool) {
            ++readerPushes;
        });
    unboundReader.reloadFromSettings();
    QCOMPARE(unboundReader.enabled(), true);
    QCOMPARE(unboundReader.bandMode(), kEqBandMode31);
    QCOMPARE(unboundReader.preGainDb(), 0.0);
    QCOMPARE(unboundReader.limiterEnabled(), true);
    QCOMPARE(unboundReader.spectrumEnabled(), true);
    QCOMPARE(unboundReader.eqPresetList().size(), 9);
    QCOMPARE(unboundReader.eqPresetList().at(8).toMap().value(QStringLiteral("name")).toString(),
             QStringLiteral("unbound-preset"));
    QCOMPARE(readerPushes, 0);
}

void SettingsControllerTest::eqPresetCrudBuiltinProtected()
{
    Seriona::App::SettingsController settings;
    settings.setSettingsStorageBackend(testBackend());
    QSignalSpy listSpy(&settings, &Seriona::App::SettingsController::eqPresetListChanged);

    // 内置 8 款恒在前 builtin=true；10 档下各档数组定长 10，值同常量表
    const QVariantList initial = settings.eqPresetList();
    QCOMPARE(initial.size(), 8);
    const QVariantMap bass = initial.at(1).toMap();
    const QVariantList bassGains = bass.value(QStringLiteral("gains")).toList();
    QCOMPARE(bassGains.size(), kEqBandCount10);
    for (int i = 0; i < kEqBandCount10; ++i) {
        QCOMPARE(bassGains.at(i).toDouble(), kEqBassBoostGains10[i]);
    }

    // 内置保护：rename/delete 内置 id 与未知 id 均 false，列表不动
    QVERIFY(!settings.renameEqPreset(QStringLiteral("bass-boost"), QStringLiteral("改写内置")));
    QVERIFY(!settings.deleteEqPreset(QStringLiteral("flat")));
    QVERIFY(!settings.renameEqPreset(QStringLiteral("no-such-id"), QStringLiteral("x")));
    QVERIFY(!settings.deleteEqPreset(QStringLiteral("no-such-id")));
    QCOMPARE(settings.eqPresetList(), initial);
    QCOMPARE(listSpy.count(), 0);

    // 用户 add：以当前增益/预增益存档；列表尾随内置（builtin=false）
    const QVariantList custom = makeEqGains(kEqBandCount10, 0.5, 0.5);
    settings.setBandGains10(custom);
    settings.setPreGainDb(2.0);
    QVERIFY(settings.addEqPreset(QStringLiteral("  我的曲线 ")));
    QCOMPARE(listSpy.count(), 1);
    QCOMPARE(settings.eqPresetList().size(), 9);
    const QVariantMap userEntry = settings.eqPresetList().at(8).toMap();
    QCOMPARE(userEntry.value(QStringLiteral("id")).toString(), QStringLiteral("user-1"));
    QCOMPARE(userEntry.value(QStringLiteral("name")).toString(), QStringLiteral("我的曲线"));
    QVERIFY(!userEntry.value(QStringLiteral("builtin")).toBool());
    QCOMPARE(userEntry.value(QStringLiteral("preGainDb")).toDouble(), 2.0);
    QCOMPARE(userEntry.value(QStringLiteral("gains")).toList(), custom);
    QCOMPARE(storedValue(kEqGroup, QStringLiteral("userPresets10")).toList().size(), 1);

    // rename 用户预设
    QVERIFY(settings.renameEqPreset(QStringLiteral("user-1"), QStringLiteral("改名后")));
    QCOMPARE(listSpy.count(), 2);
    QCOMPARE(settings.eqPresetList().at(8).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("改名后"));

    // 非法名拒绝（空 / 全空白 / 超 64 字符），列表与存储不动
    const QString tooLong(65, QLatin1Char('a'));
    QVERIFY(!settings.renameEqPreset(QStringLiteral("user-1"), QString()));
    QVERIFY(!settings.renameEqPreset(QStringLiteral("user-1"), QStringLiteral("   ")));
    QVERIFY(!settings.renameEqPreset(QStringLiteral("user-1"), tooLong));
    QVERIFY(!settings.addEqPreset(QStringLiteral(" ")));
    QVERIFY(!settings.addEqPreset(tooLong));
    QCOMPARE(listSpy.count(), 2);
    QCOMPARE(settings.eqPresetList().at(8).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("改名后"));

    // delete 用户预设；删除后存储键清空；重复删除 false
    QVERIFY(settings.deleteEqPreset(QStringLiteral("user-1")));
    QCOMPARE(listSpy.count(), 3);
    QCOMPARE(settings.eqPresetList(), initial);
    QVERIFY(!settings.deleteEqPreset(QStringLiteral("user-1")));
    QCOMPARE(storedValue(kEqGroup, QStringLiteral("userPresets10")).toList().size(), 0);

    // addEqPresetWithValues 归一化：越界钳位 ±15、短表补零定长 10
    const QVariantList raw{25.0, -25.0, 3.3, 4.5};
    QVERIFY(settings.addEqPresetWithValues(QStringLiteral("vals"), raw, 30.0));
    QCOMPARE(listSpy.count(), 4);
    const QVariantMap valEntry = settings.eqPresetList().at(8).toMap();
    QCOMPARE(valEntry.value(QStringLiteral("preGainDb")).toDouble(), 15.0);
    const QVariantList normalized = valEntry.value(QStringLiteral("gains")).toList();
    QCOMPARE(normalized.size(), kEqBandCount10);
    QCOMPARE(normalized.at(0).toDouble(), 15.0);
    QCOMPARE(normalized.at(1).toDouble(), -15.0);
    QCOMPARE(normalized.at(2).toDouble(), 3.3);
    QCOMPARE(normalized.at(3).toDouble(), 4.5);
    for (int i = 4; i < kEqBandCount10; ++i) {
        QCOMPARE(normalized.at(i).toDouble(), 0.0);
    }

    // NaN 项归一为 0.0（防非有限值穿透预设存储）；非法名拒绝
    const QVariantList nanGains{std::numeric_limits<double>::quiet_NaN()};
    QVERIFY(settings.addEqPresetWithValues(QStringLiteral("nan-entry"), nanGains, 0.0));
    QCOMPARE(settings.eqPresetList().size(), 10);
    QCOMPARE(settings.eqPresetList().at(9).toMap().value(QStringLiteral("gains")).toList().at(0).toDouble(), 0.0);
    QVERIFY(!settings.addEqPresetWithValues(QString(), raw, 0.0));
}

void SettingsControllerTest::eqPresetTenThirtyOneIndependent()
{
    Seriona::App::SettingsController settings;
    settings.setSettingsStorageBackend(testBackend());

    // 10 档存档用户预设（当前档增益/预增益快照）
    const QVariantList tenGains = makeEqGains(kEqBandCount10, 1.0, 0.5);
    settings.setBandGains10(tenGains);
    settings.setPreGainDb(1.5);
    QVERIFY(settings.addEqPreset(QStringLiteral("ten-mode-preset")));
    QCOMPARE(settings.eqPresetList().size(), 9);
    QCOMPARE(settings.eqPresetList().at(8).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("user-1"));

    // 切 31 档：10 档用户预设不可见；内置 8 款恒在前且 gains 按 31 档定长
    QSignalSpy listSpy(&settings, &Seriona::App::SettingsController::eqPresetListChanged);
    settings.setBandMode(kEqBandMode31);
    QCOMPARE(listSpy.count(), 1);
    QCOMPARE(settings.eqPresetList().size(), 8);
    for (const QVariant &entry : settings.eqPresetList()) {
        const QVariantMap map = entry.toMap();
        QVERIFY(map.value(QStringLiteral("builtin")).toBool());
        QCOMPARE(map.value(QStringLiteral("gains")).toList().size(), kEqBandCount31);
    }

    // 31 档独立存档（与 10 档同 id 序列 user-1，互不覆盖）
    QVERIFY(settings.addEqPreset(QStringLiteral("thirty-one-preset")));
    QCOMPARE(settings.eqPresetList().size(), 9);

    // 切回 10 档：10 档预设仍在、31 档预设不可见
    settings.setBandMode(kEqBandMode10);
    QCOMPARE(settings.eqPresetList().size(), 9);
    QCOMPARE(settings.eqPresetList().at(8).toMap().value(QStringLiteral("name")).toString(),
             QStringLiteral("ten-mode-preset"));

    // 两档存储键独立
    QCOMPARE(storedValue(kEqGroup, QStringLiteral("userPresets10")).toList().size(), 1);
    QCOMPARE(storedValue(kEqGroup, QStringLiteral("userPresets31")).toList().size(), 1);
    QCOMPARE(storedValue(kEqGroup, QStringLiteral("userPresets10")).toList().at(0).toMap().value(
                 QStringLiteral("name"))
                 .toString(),
             QStringLiteral("ten-mode-preset"));
    QCOMPARE(storedValue(kEqGroup, QStringLiteral("userPresets31")).toList().at(0).toMap().value(
                 QStringLiteral("name"))
                 .toString(),
             QStringLiteral("thirty-one-preset"));

    // 预设应用按当前档位取对应存储：31 档改值后应用 user-1 → 恢复 31 档自身存档
    settings.setBandMode(kEqBandMode31);
    settings.setBandGains31(makeEqGains(kEqBandCount31, 0.0, 0.5));
    QVERIFY(settings.applyEqPreset(QStringLiteral("user-1")));
    QCOMPARE(settings.bandGains31(), makeEqGains(kEqBandCount31, 0.0, 0.0));
    QCOMPARE(settings.preGainDb(), 1.5); // 存档于 10/31 档添加时点的当前预增益

    // 10 档 preset 在 31 档不可应用（id 不在 31 档槽）→ 但 31 档 user-1 属 thirty-one-preset
    settings.setBandMode(kEqBandMode31);
    QVERIFY(settings.applyEqPreset(QStringLiteral("user-1"))); // 值未变：无动作仍返回 true
    QCOMPARE(settings.bandGains31(), makeEqGains(kEqBandCount31, 0.0, 0.0));
}

void SettingsControllerTest::eqPresetSanitizeDirtyStorage()
{
    // 脏数据预置（手动灌入假后端 10 档槽）：
    // 丢弃：空名（首尾空白）/"user-" 前缀缺失/重复 id/超长名；
    // 保留但归一化：preGain 越界钳位、gains 短表补零 + 越界项钳位
    QVariantList dirty;
    QVariantMap good;
    good.insert(QStringLiteral("id"), QStringLiteral("user-1"));
    good.insert(QStringLiteral("name"), QStringLiteral("  合法条目  "));
    good.insert(QStringLiteral("preGainDb"), 0.0);
    good.insert(QStringLiteral("gains"), makeEqGains(kEqBandCount10, 1.0, 0.5));
    dirty.append(good);
    QVariantMap emptyName;
    emptyName.insert(QStringLiteral("id"), QStringLiteral("user-2"));
    emptyName.insert(QStringLiteral("name"), QStringLiteral("   "));
    emptyName.insert(QStringLiteral("gains"), makeEqGains(kEqBandCount10, 0.0, 0.0));
    dirty.append(emptyName);
    QVariantMap dupId;
    dupId.insert(QStringLiteral("id"), QStringLiteral("user-1"));
    dupId.insert(QStringLiteral("name"), QStringLiteral("重复id"));
    dupId.insert(QStringLiteral("gains"), makeEqGains(kEqBandCount10, 0.0, 0.0));
    dirty.append(dupId);
    QVariantMap noPrefix;
    noPrefix.insert(QStringLiteral("id"), QStringLiteral("evil-1"));
    noPrefix.insert(QStringLiteral("name"), QStringLiteral("无前缀"));
    noPrefix.insert(QStringLiteral("gains"), makeEqGains(kEqBandCount10, 0.0, 0.0));
    dirty.append(noPrefix);
    QVariantMap longName;
    longName.insert(QStringLiteral("id"), QStringLiteral("user-3"));
    longName.insert(QStringLiteral("name"), QString(65, QLatin1Char('x')));
    longName.insert(QStringLiteral("gains"), makeEqGains(kEqBandCount10, 0.0, 0.0));
    dirty.append(longName);
    QVariantMap clamped;
    clamped.insert(QStringLiteral("id"), QStringLiteral("user-4"));
    clamped.insert(QStringLiteral("name"), QStringLiteral("钳位"));
    clamped.insert(QStringLiteral("preGainDb"), 40.0);
    clamped.insert(QStringLiteral("gains"), QVariantList{99.0, -99.0});
    dirty.append(clamped);
    m_store.insert(storageKey(kEqGroup, QStringLiteral("userPresets10")), dirty);

    Seriona::App::SettingsController settings;
    settings.setSettingsStorageBackend(testBackend());
    settings.reloadFromSettings();

    // 仅合法 + 钳位两条存活（8 内置 + 2 用户）
    const QVariantList presets = settings.eqPresetList();
    QCOMPARE(presets.size(), 10);
    const QVariantMap user0 = presets.at(8).toMap();
    QCOMPARE(user0.value(QStringLiteral("id")).toString(), QStringLiteral("user-1"));
    QCOMPARE(user0.value(QStringLiteral("name")).toString(), QStringLiteral("合法条目")); // 首尾空白被裁
    QVERIFY(!user0.value(QStringLiteral("builtin")).toBool());
    QCOMPARE(user0.value(QStringLiteral("gains")).toList(), makeEqGains(kEqBandCount10, 1.0, 0.5));
    const QVariantMap user1 = presets.at(9).toMap();
    QCOMPARE(user1.value(QStringLiteral("id")).toString(), QStringLiteral("user-4"));
    QCOMPARE(user1.value(QStringLiteral("preGainDb")).toDouble(), 15.0);
    const QVariantList clampedGains = user1.value(QStringLiteral("gains")).toList();
    QCOMPARE(clampedGains.size(), kEqBandCount10);
    QCOMPARE(clampedGains.at(0).toDouble(), 15.0);
    QCOMPARE(clampedGains.at(1).toDouble(), -15.0);
    for (int i = 2; i < kEqBandCount10; ++i) {
        QCOMPARE(clampedGains.at(i).toDouble(), 0.0);
    }

    // 另一档无脏数据 → 切 31 档仅内置 8 款；镜像键不受 reload 影响（仍空）
    settings.setBandMode(kEqBandMode31);
    QCOMPARE(settings.eqPresetList().size(), 8);
    QVERIFY(settings.curvePoints().isEmpty());
    QVERIFY(settings.spectrumBins().isEmpty());
}

QTEST_GUILESS_MAIN(SettingsControllerTest)

#include "tst_settings_controller.moc"
