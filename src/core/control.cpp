/*
    SPDX-FileCopyrightText: 2007 Volker Krause <vkrause@kde.org>

    SPDX-License-Identifier: LGPL-2.0-or-later
*/

#include "control.h"
#include "akonadicore_debug.h"
#include "servermanager.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QPointer>

using namespace Akonadi;

namespace Akonadi
{
namespace Internal
{
class StaticControl : public Control
{
    Q_OBJECT
};

}

Q_GLOBAL_STATIC(Internal::StaticControl, s_instance) // NOLINT(readability-redundant-member-init)

/**
 * @internal
 */
class ControlPrivate
{
public:
    explicit ControlPrivate(Control *parent)
        : mParent(parent)
    {
    }

    void cleanup()
    {
    }

    bool exec();
    void serverStateChanged(ServerManager::State state);

    QPointer<Control> mParent;
    QEventLoop *mEventLoop = nullptr;
    bool mSuccess = false;

    bool mStarting = false;
    bool mStopping = false;
};

bool ControlPrivate::exec()
{
    qCDebug(AKONADICORE_LOG) << "Starting/Stopping Akonadi (using an event loop).";
    mEventLoop = new QEventLoop(mParent);
    mEventLoop->exec();
    mEventLoop->deleteLater();
    mEventLoop = nullptr;

    if (!mSuccess) {
        qCWarning(AKONADICORE_LOG) << "Could not start/stop Akonadi!";
    }

    mStarting = false;
    mStopping = false;

    const bool rv = mSuccess;
    mSuccess = false;
    return rv;
}

void ControlPrivate::serverStateChanged(ServerManager::State state)
{
    qCDebug(AKONADICORE_LOG) << "Server state changed to" << state;
    if (mEventLoop && mEventLoop->isRunning()) {
        // ignore transient states going into the right direction
        if ((mStarting && (state == ServerManager::Starting || state == ServerManager::Upgrading)) || (mStopping && state == ServerManager::Stopping)) {
            return;
        }
        mEventLoop->quit();
        mSuccess = (mStarting && state == ServerManager::Running) || (mStopping && state == ServerManager::NotRunning);
    }
}

Control::Control()
    : d(new ControlPrivate(this))
{
    connect(ServerManager::self(), &ServerManager::stateChanged, this, [this](Akonadi::ServerManager::State state) {
        d->serverStateChanged(state);
    });
    // mProgressIndicator is a widget, so it better be deleted before the QApplication is deleted
    // Otherwise we get a crash in QCursor code with Qt-4.5
    if (QCoreApplication::instance()) {
        connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, [this]() {
            d->cleanup();
        });
    }
}

Control::~Control() = default;

bool Control::start()
{
    switch (ServerManager::state()) {
    case ServerManager::Stopping:
        qCDebug(AKONADICORE_LOG) << "Server is currently being stopped, won't try to start it now";
        return false;
    case ServerManager::Broken:
        qCDebug(AKONADICORE_LOG) << "Server is already broken: " << ServerManager::brokenReason();
        return false;
    case ServerManager::Starting:
    case ServerManager::NotRunning:
    case ServerManager::Running:
    case ServerManager::Upgrading:
        break;
    }
    if (ServerManager::isRunning() || s_instance->d->mEventLoop) {
        qCDebug(AKONADICORE_LOG) << "Server is already running";
        return true;
    }
    s_instance->d->mStarting = true;
    if (!ServerManager::start()) {
        qCDebug(AKONADICORE_LOG) << "ServerManager::start failed -> return false";
        return false;
    }
    return s_instance->d->exec();
}

bool Control::stop()
{
    if (ServerManager::state() == ServerManager::Starting) {
        return false;
    }
    if (!ServerManager::isRunning() || s_instance->d->mEventLoop) {
        return true;
    }
    s_instance->d->mStopping = true;
    if (!ServerManager::stop()) {
        return false;
    }
    return s_instance->d->exec();
}

bool Control::restart()
{
    if (ServerManager::isRunning()) {
        if (!stop()) {
            return false;
        }
    }
    return start();
}

} // namespace Akonadi

#include "control.moc"

#include "moc_control.cpp"
