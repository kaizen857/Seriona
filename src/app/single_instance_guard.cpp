#include "single_instance_guard.h"

#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QStandardPaths>
#include <QtLogging>

#include <utility>

#if defined(Q_OS_WIN)
#include <windows.h>
#endif

namespace {

constexpr int kConnectAttempts = 10;
constexpr int kConnectTimeoutMs = 100;
constexpr int kConnectBudgetMs = 1500;
constexpr int kWriteTimeoutMs = 1000;
constexpr int kAckTimeoutMs = 1000;

// 运行期目录（XDG_RUNTIME_DIR / 各平台等价目录）：锁文件放这里天然按用户隔离。
QString runtimeDirectory()
{
    QString directory = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (directory.isEmpty()) {
        directory = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    }
    return directory;
}

// socket 名附每用户哈希后缀：多用户共享 /tmp 时避免同名 socket 互相干扰/误删；
// 对同一用户稳定（由运行期目录 + 家目录派生），长度固定不触 sun_path 上限。
QString userScopeToken()
{
    const QString scope = runtimeDirectory() + QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    return QString::fromLatin1(QCryptographicHash::hash(scope.toUtf8(), QCryptographicHash::Sha256).toHex().left(12));
}

}

SingleInstanceGuard::SingleInstanceGuard(const QString &applicationId, QObject *parent)
    : QObject(parent),
      socketName_(applicationId + QLatin1Char('-') + userScopeToken()),
      lockFile_(runtimeDirectory() + QLatin1Char('/') + applicationId + QStringLiteral(".lock")),
      server_(this)
{
    server_.setSocketOptions(QLocalServer::UserAccessOption);
}

SingleInstanceGuard::~SingleInstanceGuard() = default;

bool SingleInstanceGuard::isPrimaryInstance()
{
    if (lockFile_.tryLock()) {
        return becomePrimary();
    }
    if (deliverActivationToPrimary()) {
        return false;
    }
    lockFile_.removeStaleLockFile();
    if (lockFile_.tryLock()) {
        return becomePrimary();
    }
    qWarning("single instance guard: lock unavailable, continuing without guard");
    return true;
}

bool SingleInstanceGuard::becomePrimary()
{
    if (!server_.listen(socketName_)) {
        QLocalServer::removeServer(socketName_);
        if (!server_.listen(socketName_)) {
            qWarning("single instance guard: cannot listen on %s, continuing without guard", qUtf8Printable(socketName_));
            return true;
        }
    }
    connect(&server_, &QLocalServer::newConnection, this, &SingleInstanceGuard::handlePendingConnections);
    return true;
}

bool SingleInstanceGuard::deliverActivationToPrimary()
{
#if defined(Q_OS_WIN)
    // 二次启动进程通常持有前台资格：先把“可设前台”权限授予其它进程，主实例随后的
    // SetForegroundWindow/requestActivate 才能生效（否则只闪任务栏）。
    ::AllowSetForegroundWindow(ASFW_ANY);
#endif
    QLocalSocket socket;
    bool connected = false;
    // 主实例可能已持锁但尚未 listen 完成：预算内短暂重试后再判定为陈旧残留。
    // Windows 的 connectToServer 为同步阻塞（waitForConnected 超时参数不生效，单次
    // WaitNamedPipe 可达数秒），以总预算封顶避免二次启动卡在重试循环。
    QElapsedTimer connectBudget;
    connectBudget.start();
    for (int attempt = 0; attempt < kConnectAttempts && !connected; ++attempt) {
        if (attempt > 0 && connectBudget.elapsed() >= kConnectBudgetMs) {
            break;
        }
        socket.connectToServer(socketName_);
        connected = socket.waitForConnected(kConnectTimeoutMs);
    }
    if (!connected) {
        return false;
    }
    const QByteArray activationToken = qgetenv("XDG_ACTIVATION_TOKEN");
    if (!activationToken.isEmpty()) {
        // 一次性令牌：读取后立即清除，避免泄漏给之后启动的子进程（xdg-activation 约定）。
        qunsetenv("XDG_ACTIVATION_TOKEN");
    }
    QJsonObject payload;
    payload.insert(QStringLiteral("v"), 1);
    payload.insert(QStringLiteral("activationToken"), QString::fromLatin1(activationToken.toBase64()));
    QByteArray message = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    message.append('\n');
    socket.write(message);
    socket.waitForBytesWritten(kWriteTimeoutMs);
    socket.waitForReadyRead(kAckTimeoutMs);
    return true;
}

void SingleInstanceGuard::handlePendingConnections()
{
    while (QLocalSocket *connection = server_.nextPendingConnection()) {
        connection->setProperty("serionaActivationBuffer", QByteArray{});
        connect(connection, &QLocalSocket::readyRead, this, [this, connection] { readActivationPayload(connection); });
        connect(connection, &QLocalSocket::disconnected, connection, &QObject::deleteLater);
        if (connection->bytesAvailable() > 0) {
            readActivationPayload(connection);
        }
    }
}

void SingleInstanceGuard::readActivationPayload(QLocalSocket *connection)
{
    QByteArray buffer = connection->property("serionaActivationBuffer").toByteArray();
    buffer.append(connection->readAll());
    const qsizetype delimiter = buffer.indexOf('\n');
    if (delimiter < 0) {
        connection->setProperty("serionaActivationBuffer", buffer);
        return;
    }
    connection->setProperty("serionaActivationBuffer", QByteArray{});
    const QJsonDocument document = QJsonDocument::fromJson(buffer.left(delimiter));
    QByteArray activationToken;
    if (document.isObject()) {
        activationToken = QByteArray::fromBase64(document.object().value(QStringLiteral("activationToken")).toString().toLatin1());
    }
    dispatchActivation(activationToken);
    connection->write("ok\n");
    connection->flush();
    connection->disconnectFromServer();
}

void SingleInstanceGuard::dispatchActivation(const QByteArray &activationToken)
{
    emit activationRequested(activationToken);
    if (activationHandler_) {
        activationHandler_(activationToken);
        return;
    }
    pendingActivationToken_ = activationToken;
}

void SingleInstanceGuard::setActivationHandler(std::function<void(const QByteArray &activationToken)> handler)
{
    activationHandler_ = std::move(handler);
    if (!pendingActivationToken_.has_value()) {
        return;
    }
    const QByteArray activationToken = *pendingActivationToken_;
    pendingActivationToken_.reset();
    activationHandler_(activationToken);
}
