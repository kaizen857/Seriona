#pragma once

#include <QByteArray>
#include <QLocalServer>
#include <QLockFile>
#include <QObject>
#include <QString>

#include <functional>
#include <optional>

class QLocalSocket;

// 单实例守卫（single-instance application）：第二次启动不新起实例，而是把激活请求
// （含 Wayland 激活令牌）经本地 socket 转交给已运行实例——由它把窗口带到前台——
// 本进程随即以退出码 0 结束。陈旧锁/陈旧 socket（崩溃残留）自动恢复；
// 任一环节不可恢复时放行启动（绝不阻塞应用）。
class SingleInstanceGuard : public QObject
{
    Q_OBJECT

public:
    explicit SingleInstanceGuard(const QString &applicationId, QObject *parent = nullptr);
    ~SingleInstanceGuard() override;

    // 主实例返回 true 并开始监听；检测到存活主实例时转发激活并返回 false（调用方应退出）。
    bool isPrimaryInstance();

    // 激活处理器：窗口就绪后安装；handler 安装前到达的激活会在安装时补发。
    void setActivationHandler(std::function<void(const QByteArray &activationToken)> handler);

signals:
    // 每当收到一次激活请求（无论处理器是否就绪）发出，便于观测与诊断。
    void activationRequested(const QByteArray &activationToken);

private:
    bool becomePrimary();
    bool deliverActivationToPrimary();
    void handlePendingConnections();
    void readActivationPayload(QLocalSocket *connection);
    void dispatchActivation(const QByteArray &activationToken);

    QString socketName_;
    QLockFile lockFile_;
    QLocalServer server_;
    std::function<void(const QByteArray &)> activationHandler_;
    std::optional<QByteArray> pendingActivationToken_;
};
