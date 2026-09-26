#pragma once

#include <QMouseEvent>
#include <QObject>
#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>
#include <QtQml/qqmlregistration.h>

namespace Seriona::App {

// 原生 popup 的转发事件不能再次投递给另一个窗口中的独占抓取者。
class PopupInputGuard : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QQuickWindow *window READ window WRITE setWindow NOTIFY windowChanged FINAL)

public:
    explicit PopupInputGuard(QObject *parent = nullptr) : QObject(parent) {}

    QQuickWindow *window() const { return m_window; }

    void setWindow(QQuickWindow *window)
    {
        if (m_window == window)
            return;
        if (m_window)
            m_window->removeEventFilter(this);
        m_window = window;
        if (m_window)
            m_window->installEventFilter(this);
        emit windowChanged();
    }

signals:
    void windowChanged();

protected:
    bool eventFilter(QObject *object, QEvent *event) override
    {
        if (object != m_window || m_window->type() != Qt::Popup)
            return false;
        switch (event->type()) {
        case QEvent::MouseMove:
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonRelease: {
            auto *mouse = static_cast<QMouseEvent *>(event);
            auto *grabber = qobject_cast<QQuickItem *>(mouse->exclusiveGrabber(mouse->point(0)));
            if (grabber && grabber->window() && grabber->window() != m_window) {
                // ignore + filter 只拒绝 popup 的副本；Qt 继续向原窗口投递原事件。
                event->ignore();
                return true;
            }
            break;
        }
        default:
            break;
        }
        return false;
    }

private:
    QPointer<QQuickWindow> m_window;
};

} // namespace Seriona::App
