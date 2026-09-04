#include "settings_controller.h"

#include "app_settings_storage.h"

#include <QHash>
#include <QSignalSpy>
#include <QVariant>
#include <QtTest/QTest>

#include <memory>

namespace {

constexpr auto kOutputGroup = "output";

QString storageKey(const QString &group, const QString &key)
{
    return group + QLatin1Char('\x1f') + key;
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
    void startupPushSequence();
    void lyricDelimitersPersistRoundTrip();
    void sampleFormatPersistRoundTrip();
    void applyIncludesSampleFormat();
    void rollbackRejectedOutputConfigRestoresSnapshot();
    void directOutputGreyState();
    void deviceCapsEmptyShowsAllOptions();
    void deviceCapsFilterSampleRatesAndFormats();
    void deviceCapsFollowSelectedDevice();
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
    void advanceTransitionsGreyedFollowsOutputMode();
    void transitionApplyPacksCurrentNineFields();
    void transitionExecutorUnsetNoopPersistence();
    void transitionStartupApplySequence();

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

    QCOMPARE(settings.outputMode(), 0);
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
    QSignalSpy modeSpy(&settings, &Seriona::App::SettingsController::outputModeChanged);
    QSignalSpy rateSpy(&settings, &Seriona::App::SettingsController::sampleRateChanged);
    QSignalSpy formatSpy(&settings, &Seriona::App::SettingsController::sampleFormatChanged);
    QSignalSpy durationSpy(&settings, &Seriona::App::SettingsController::bufferDurationMsChanged);
    QSignalSpy deviceSpy(&settings, &Seriona::App::SettingsController::preferredDeviceIdChanged);
    QSignalSpy delimiterSpy(&settings, &Seriona::App::SettingsController::lyricDelimitersChanged);

    settings.setOutputMode(1);
    settings.setSampleRate(96000);
    settings.setSampleFormat(2);
    settings.setBufferDurationMs(500);
    settings.setPreferredDeviceId(QStringLiteral("dev-1"));
    settings.setLyricDelimiters(QStringList{QStringLiteral(" / "), QStringLiteral(" | ")});

    QCOMPARE(settings.outputMode(), 1);
    QCOMPARE(settings.sampleRate(), 96000);
    QCOMPARE(settings.sampleFormat(), 2);
    QCOMPARE(settings.bufferDurationMs(), 500);
    QCOMPARE(settings.preferredDeviceId(), QStringLiteral("dev-1"));
    const QStringList expectedDelimiters{QStringLiteral(" / "), QStringLiteral(" | ")};
    QCOMPARE(settings.lyricDelimiters(), expectedDelimiters);
    QCOMPARE(modeSpy.count(), 1);
    QCOMPARE(rateSpy.count(), 1);
    QCOMPARE(formatSpy.count(), 1);
    QCOMPARE(durationSpy.count(), 1);
    QCOMPARE(deviceSpy.count(), 1);
    QCOMPARE(delimiterSpy.count(), 1);

    // 相同值不重复 NOTIFY
    settings.setOutputMode(1);
    settings.setSampleRate(96000);
    settings.setSampleFormat(2);
    settings.setLyricDelimiters(expectedDelimiters);
    QCOMPARE(modeSpy.count(), 1);
    QCOMPARE(rateSpy.count(), 1);
    QCOMPARE(formatSpy.count(), 1);
    QCOMPARE(delimiterSpy.count(), 1);

    QCOMPARE(storedValue(QStringLiteral("output"), QStringLiteral("outputMode")).toInt(), 1);
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

    settings.setOutputMode(7);
    QCOMPARE(settings.outputMode(), 0);

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
    settings.setOutputMode(1);
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
    QCOMPARE(settings.outputMode(), 1);
    QCOMPARE(settings.sampleRate(), 768000);
    QCOMPARE(settings.sampleFormat(), 4);
    QCOMPARE(settings.bufferDurationMs(), 1000);
    QCOMPARE(settings.followRestoreDelayMs(), 15000);

    // 非法值不写入存储
    settings.setOutputMode(7);
    settings.setSampleRate(100);
    settings.setSampleFormat(6);
    settings.setBufferDurationMs(2000);
    settings.setFollowRestoreDelayMs(42);
    QCOMPARE(storedValue(QStringLiteral("output"), QStringLiteral("outputMode")).toInt(), 1);
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
        [&payloads](int outputMode, int sampleRate, int sampleFormat, int bufferDurationMs, const QString &preferredDeviceId) {
            payloads.append(QStringLiteral("%1|%2|%3|%4|%5")
                                .arg(outputMode)
                                .arg(sampleRate)
                                .arg(sampleFormat)
                                .arg(bufferDurationMs)
                                .arg(preferredDeviceId));
        });

    settings.setOutputMode(1);
    settings.setSampleRate(96000);
    settings.setPreferredDeviceId(QStringLiteral("dev-x"));
    settings.apply();

    QCOMPARE(payloads.size(), 4);
    QCOMPARE(payloads.at(0), QStringLiteral("1|48000|0|300|"));
    QCOMPARE(payloads.at(1), QStringLiteral("1|96000|0|300|"));
    QCOMPARE(payloads.at(2), QStringLiteral("1|96000|0|300|dev-x"));
    QCOMPARE(payloads.at(3), QStringLiteral("1|96000|0|300|dev-x"));
}

void SettingsControllerTest::discreteControlsPushImmediately()
{
    Seriona::App::SettingsController settings;
    int pushes = 0;
    settings.setApplyOutputConfigExecutor(
        [&pushes](int, int, int, int, const QString &) {
            ++pushes;
        });

    settings.setOutputMode(1);
    settings.setSampleRate(44100);
    settings.setPreferredDeviceId(QStringLiteral("dev-2"));
    QCOMPARE(pushes, 3);
}

void SettingsControllerTest::bufferDurationDebounces()
{
    Seriona::App::SettingsController settings;
    int pushes = 0;
    settings.setApplyOutputConfigExecutor(
        [&pushes](int, int, int, int, const QString &) {
            ++pushes;
        });

    // 离散控件立即推送
    settings.setOutputMode(1);
    QCOMPARE(pushes, 1);

    // 连续控件去抖：5 次变更只产生一次下发
    for (int i = 0; i < 5; ++i) {
        settings.setBufferDurationMs(100 + i * 10);
    }
    QCOMPARE(pushes, 1);
    QCOMPARE(settings.bufferDurationMs(), 140);

    QTRY_COMPARE_WITH_TIMEOUT(pushes, 2, 2000);

    QTest::qWait(800);
    QCOMPARE(pushes, 2);
}

void SettingsControllerTest::persistenceRoundTrip()
{
    {
        Seriona::App::SettingsController writer;
        writer.setSettingsStorageBackend(testBackend());
        writer.setOutputMode(1);
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

    QCOMPARE(reader.outputMode(), 1);
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

    settings.setDefaults(1, 96000, 500, QStringLiteral("dev-4"));

    QCOMPARE(settings.outputMode(), 1);
    QCOMPARE(settings.sampleRate(), 96000);
    QCOMPARE(settings.bufferDurationMs(), 500);
    QCOMPARE(settings.preferredDeviceId(), QStringLiteral("dev-4"));
    QCOMPARE(pushes, 0);

    QVERIFY(!storedContains(QStringLiteral("output"), QStringLiteral("outputMode")));
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
}

void SettingsControllerTest::directOutputGreyState()
{
    Seriona::App::SettingsController settings;
    QSignalSpy modeSpy(&settings, &Seriona::App::SettingsController::outputModeChanged);

    // 默认直接输出（0）→ 采样率/位深行灰化
    QVERIFY(settings.sampleParamsGreyed());

    settings.setOutputMode(1);
    QVERIFY(!settings.sampleParamsGreyed());

    settings.setOutputMode(0);
    QVERIFY(settings.sampleParamsGreyed());
    QCOMPARE(modeSpy.count(), 2);
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
        [&payloads](int outputMode, int sampleRate, int sampleFormat, int bufferDurationMs, const QString &preferredDeviceId) {
            Q_UNUSED(outputMode);
            Q_UNUSED(sampleRate);
            Q_UNUSED(bufferDurationMs);
            Q_UNUSED(preferredDeviceId);
            payloads.append(QString::number(sampleFormat));
        });

    settings.setOutputMode(1);
    QCOMPARE(payloads.size(), 1);
    QCOMPARE(payloads.at(0), QStringLiteral("0"));

    settings.setSampleFormat(2);
    QCOMPARE(payloads.size(), 2);
    QCOMPARE(payloads.at(1), QStringLiteral("2"));

    settings.apply();
    QCOMPARE(payloads.size(), 3);
    QCOMPARE(payloads.at(2), QStringLiteral("2"));
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
    QCOMPARE(settings.outputMode(), 0);
    QCOMPARE(settings.sampleFormat(), 0);
    QCOMPARE(pushes, 0);

    // 离散 setter 每次提交并记录快照；bufferDurationMs 去抖未提交
    settings.setOutputMode(1);
    settings.setSampleFormat(2);
    settings.setBufferDurationMs(500);
    QCOMPARE(pushes, 2);

    // 拒绝回退 → 恢复最近一次已提交的值，未提交的 bufferDurationMs 变更被回滚
    settings.rollbackRejectedOutputConfig();
    QCOMPARE(settings.outputMode(), 1);
    QCOMPARE(settings.sampleRate(), 48000);
    QCOMPARE(settings.sampleFormat(), 2);
    QCOMPARE(settings.bufferDurationMs(), 300);
    QCOMPARE(settings.preferredDeviceId(), QString());
    // 回退本身不推送
    QCOMPARE(pushes, 2);

    // 回退不持久化：存储保持 setter 写入的值
    QCOMPARE(storedValue(QStringLiteral("output"), QStringLiteral("outputMode")).toInt(), 1);
    QCOMPARE(storedValue(QStringLiteral("output"), QStringLiteral("sampleFormat")).toInt(), 2);
    QCOMPARE(storedValue(QStringLiteral("output"), QStringLiteral("bufferDurationMs")).toInt(), 500);
}

void SettingsControllerTest::startupPushSequence()
{
    Seriona::App::SettingsController settings;
    QStringList payloads;
    settings.setApplyOutputConfigExecutor(
        [&payloads](int outputMode, int sampleRate, int sampleFormat, int bufferDurationMs, const QString &preferredDeviceId) {
            payloads.append(QStringLiteral("%1|%2|%3|%4")
                                .arg(outputMode)
                                .arg(sampleRate)
                                .arg(bufferDurationMs)
                                .arg(preferredDeviceId));
        });

    // 启动路径：reloadFromSettings → apply 恰好推送一次（空设置 → 默认值）
    settings.reloadFromSettings();
    settings.apply();

    QCOMPARE(payloads.size(), 1);
    QCOMPARE(payloads.at(0), QStringLiteral("0|48000|300|"));
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
    // 默认 Direct（outputMode 0）→ 过渡类行灰化
    QCOMPARE(settings.outputMode(), 0);
    QVERIFY(settings.advanceTransitionsGreyed());
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

    QTRY_COMPARE_WITH_TIMEOUT(payloads.size(), 3, 2000);
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

void SettingsControllerTest::advanceTransitionsGreyedFollowsOutputMode()
{
    Seriona::App::SettingsController settings;
    QSignalSpy modeSpy(&settings, &Seriona::App::SettingsController::outputModeChanged);

    // 默认 Direct（0）→ 过渡类行灰化
    QVERIFY(settings.advanceTransitionsGreyed());

    // Mixed（1）→ 全部可用
    settings.setOutputMode(1);
    QVERIFY(!settings.advanceTransitionsGreyed());

    // 切回 Direct → 灰化
    settings.setOutputMode(0);
    QVERIFY(settings.advanceTransitionsGreyed());
    QCOMPARE(modeSpy.count(), 2);

    // 灰化不影响取值与校验（Direct 下仍可设过渡值，UI 层负责禁用）
    settings.setCrossfadeMs(2500);
    settings.setAutoAdvanceFadeMode(1);
    QCOMPARE(settings.crossfadeMs(), 2500);
    QCOMPARE(settings.autoAdvanceFadeMode(), 1);
    // 过渡值变化不触发 outputMode 信号
    QCOMPARE(modeSpy.count(), 2);
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

QTEST_GUILESS_MAIN(SettingsControllerTest)

#include "tst_settings_controller.moc"
