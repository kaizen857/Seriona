// 单实例守卫测试：主/次实例判定、激活转发与 Wayland 令牌转交/消费、
// 主实例销毁后的接管、handler 就绪前到达的激活回放。
#include <QCoreApplication>
#include <QSignalSpy>
#include <QTest>

#include "single_instance_guard.h"

namespace {
QString testApplicationId()
{
    // 每个用例独立 id：避免与真实运行的应用或其它用例互相干扰。
    return QStringLiteral("seriona-single-instance-test-%1").arg(QCoreApplication::applicationPid());
}
}

class SingleInstanceGuardTest : public QObject
{
    Q_OBJECT

private slots:
    void secondaryForwardsActivationToPrimary();
    void activationTokenIsForwardedAndConsumed();
    void newInstanceTakesOverAfterPrimaryDestruction();
    void activationArrivingBeforeHandlerIsReplayed();
};

void SingleInstanceGuardTest::secondaryForwardsActivationToPrimary()
{
    qunsetenv("XDG_ACTIVATION_TOKEN");
    const QString applicationId = testApplicationId();
    SingleInstanceGuard primary{applicationId};
    QVERIFY(primary.isPrimaryInstance());

    QByteArray received;
    bool called = false;
    primary.setActivationHandler([&](const QByteArray &activationToken) {
        received = activationToken;
        called = true;
    });

    SingleInstanceGuard secondary{applicationId};
    QVERIFY(!secondary.isPrimaryInstance());

    QTRY_VERIFY_WITH_TIMEOUT(called, 5000);
    QVERIFY(received.isEmpty());
}

void SingleInstanceGuardTest::activationTokenIsForwardedAndConsumed()
{
    qputenv("XDG_ACTIVATION_TOKEN", "token-42");
    const QString applicationId = testApplicationId();
    SingleInstanceGuard primary{applicationId};
    QVERIFY(primary.isPrimaryInstance());

    QByteArray received;
    primary.setActivationHandler([&](const QByteArray &activationToken) { received = activationToken; });

    SingleInstanceGuard secondary{applicationId};
    QVERIFY(!secondary.isPrimaryInstance());

    QTRY_VERIFY_WITH_TIMEOUT(!received.isEmpty(), 5000);
    QCOMPARE(received, QByteArray{"token-42"});
    QVERIFY(qgetenv("XDG_ACTIVATION_TOKEN").isEmpty());
}

void SingleInstanceGuardTest::newInstanceTakesOverAfterPrimaryDestruction()
{
    const QString applicationId = testApplicationId();
    {
        SingleInstanceGuard primary{applicationId};
        QVERIFY(primary.isPrimaryInstance());
        SingleInstanceGuard secondary{applicationId};
        QVERIFY(!secondary.isPrimaryInstance());
    }
    SingleInstanceGuard successor{applicationId};
    QVERIFY(successor.isPrimaryInstance());
}

void SingleInstanceGuardTest::activationArrivingBeforeHandlerIsReplayed()
{
    const QString applicationId = testApplicationId();
    SingleInstanceGuard primary{applicationId};
    QVERIFY(primary.isPrimaryInstance());

    QSignalSpy receivedSpy{&primary, &SingleInstanceGuard::activationRequested};
    SingleInstanceGuard secondary{applicationId};
    QVERIFY(!secondary.isPrimaryInstance());
    // 信号计数 1 = 主实例已处理该激活（此时仍无 handler，激活进入待回放状态）。
    QTRY_COMPARE_WITH_TIMEOUT(receivedSpy.count(), 1, 5000);

    bool replayed = false;
    primary.setActivationHandler([&](const QByteArray &) { replayed = true; });
    QVERIFY2(replayed, "handler 安装前到达的激活必须在安装时回放");
}

QTEST_GUILESS_MAIN(SingleInstanceGuardTest)

#include "tst_single_instance_guard.moc"
