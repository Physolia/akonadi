/***************************************************************************
 *   Copyright (C) 2006 by Till Adam <adam@kde.org>                        *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU Library General Public License as       *
 *   published by the Free Software Foundation; either version 2 of the    *
 *   License, or (at your option) any later version.                       *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this program; if not, write to the                 *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.             *
 ***************************************************************************/

#include "akonadi.h"
#include "connection.h"
#include "serveradaptor.h"
#include "akonadiserver_debug.h"

#include "cachecleaner.h"
#include "intervalcheck.h"
#include "storagejanitor.h"
#include "storage/dbconfig.h"
#include "storage/datastore.h"
#include "notificationmanager.h"
#include "resourcemanager.h"
#include "tracer.h"
#include "utils.h"
#include "debuginterface.h"
#include "storage/itemretrievalmanager.h"
#include "storage/collectionstatistics.h"
#include "preprocessormanager.h"
#include "search/searchmanager.h"
#include "search/searchtaskmanager.h"
#include "aklocalserver.h"

#include "collectionreferencemanager.h"

#include <private/xdgbasedirs_p.h>
#include <private/standarddirs_p.h>
#include <private/protocol_p.h>
#include <private/dbus_p.h>
#include <private/instance_p.h>

#include <QtSql/QSqlQuery>
#include <QtSql/QSqlError>

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QSettings>
#include <QtCore/QTimer>
#include <QtDBus/QDBusServiceWatcher>
#include <QtNetwork/QLocalServer>

#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif
#include <stdlib.h>

#ifdef Q_OS_WIN
#include <windows.h>
#include <Sddl.h>
#endif

using namespace Akonadi;
using namespace Akonadi::Server;

AkonadiServer *AkonadiServer::s_instance = 0;

AkonadiServer::AkonadiServer(QObject *parent)
    : QObject(parent)
    , mCmdServer(Q_NULLPTR)
    , mNtfServer(Q_NULLPTR)
    , mNotificationManager(Q_NULLPTR)
    , mCacheCleaner(Q_NULLPTR)
    , mIntervalCheck(Q_NULLPTR)
    , mStorageJanitor(Q_NULLPTR)
    , mItemRetrieval(Q_NULLPTR)
    , mAgentSearchManager(Q_NULLPTR)
    , mDatabaseProcess(0)
    , mSearchManager(0)
    , mAlreadyShutdown(false)
{
    // Register bunch of useful types
    qRegisterMetaType<Protocol::Command>();
    qRegisterMetaType<Protocol::ChangeNotification>();
    qRegisterMetaType<Protocol::ChangeNotification::List>();

    qRegisterMetaType<Protocol::ChangeNotification::Type>();
    qDBusRegisterMetaType<Protocol::ChangeNotification::Type>();
}

bool AkonadiServer::init()
{
    const QString serverConfigFile = StandardDirs::serverConfigFile(XdgBaseDirs::ReadWrite);
    QSettings settings(serverConfigFile, QSettings::IniFormat);
    // Restrict permission to 600, as the file might contain database password in plaintext
    QFile::setPermissions(serverConfigFile, QFile::ReadOwner | QFile::WriteOwner);

    if (!DbConfig::configuredDatabase()) {
        quit();
        return false;
    }

    if (DbConfig::configuredDatabase()->useInternalServer()) {
        if (!startDatabaseProcess()) {
            quit();
            return false;
        }
    } else {
        if (!createDatabase()) {
            quit();
            return false;
        }
    }

    DbConfig::configuredDatabase()->setup();

    s_instance = this;

    mCmdServer = new AkLocalServer(this);
    connect(mCmdServer, static_cast<void(AkLocalServer::*)(quintptr)>(&AkLocalServer::newConnection),
            this, &AkonadiServer::newCmdConnection);

    mNotificationManager = new NotificationManager();
    mNtfServer = new AkLocalServer(this);
    // Note: this is a queued connection, as NotificationManager lives in its
    // own thread
    connect(mNtfServer, static_cast<void(AkLocalServer::*)(quintptr)>(&AkLocalServer::newConnection),
            mNotificationManager, &NotificationManager::registerConnection);



    const QString connectionSettingsFile = StandardDirs::connectionConfigFile(XdgBaseDirs::WriteOnly);
    QSettings connectionSettings(connectionSettingsFile, QSettings::IniFormat);

    QString pipeName;
#ifdef Q_OS_WIN
    HANDLE hToken = NULL;
    PSID sid;
    QString userID;

    OpenProcessToken(GetCurrentProcess(), TOKEN_READ, &hToken);
    if (hToken) {
        DWORD size;
        PTOKEN_USER userStruct;

        GetTokenInformation(hToken, TokenUser, NULL, 0, &size);
        if (ERROR_INSUFFICIENT_BUFFER == GetLastError()) {
            userStruct = reinterpret_cast<PTOKEN_USER>(new BYTE[size]);
            GetTokenInformation(hToken, TokenUser, userStruct, size, &size);

            int sidLength = GetLengthSid(userStruct->User.Sid);
            sid = (PSID) malloc(sidLength);
            CopySid(sidLength, sid, userStruct->User.Sid);
            CloseHandle(hToken);
            delete [] userStruct;
        }

        LPWSTR s;
        if (!ConvertSidToStringSidW(sid, &s)) {
            qCCritical(AKONADISERVER_LOG) << "Could not determine user id for current process.";
            userID = QString();
        } else {
            userID = QString::fromUtf16(reinterpret_cast<ushort *>(s));
            LocalFree(s);
        }
        free(sid);
    }

    pipeName = QStringLiteral("Akonadi-%1").arg(userID);
#else
    uid_t uid = getuid();
    pipeName = QStringLiteral("Akonadi-%1").arg(uid);
#endif

    if (Instance::hasIdentifier()) {
        pipeName += QStringLiteral("-") % Instance::identifier();
    }

    const QString cmdPipeName = pipeName + QStringLiteral("-Cmd");
    if (!mCmdServer->listen(cmdPipeName)) {
        qCCritical(AKONADISERVER_LOG) << "Unable to listen on named pipe" << cmdPipeName;
        quit();
        return false;
    }

    const QString ntfPipeName = pipeName + QStringLiteral("-Ntf");
    if (!mNtfServer->listen(ntfPipeName)) {
        qCCritical(AKONADISERVER_LOG) << "Unable to listen on named pipe" << ntfPipeName;
        quit();
        return false;
    }

    connectionSettings.setValue(QStringLiteral("Data/Method"), QStringLiteral("NamedPipe"));
    connectionSettings.setValue(QStringLiteral("Data/NamedPipe"), cmdPipeName);

    connectionSettings.setValue(QStringLiteral("Notifications/Method"), QStringLiteral("NamedPipe"));
    connectionSettings.setValue(QStringLiteral("Nottifications/NamedPipe"), ntfPipeName);


    // initialize the database
    DataStore *db = DataStore::self();
    if (!db->database().isOpen()) {
        qCCritical(AKONADISERVER_LOG) << "Unable to open database" << db->database().lastError().text();
        quit();
        return false;
    }
    if (!db->init()) {
        qCCritical(AKONADISERVER_LOG) << "Unable to initialize database.";
        quit();
        return false;
    }

    Tracer::self();
    new DebugInterface(this);
    ResourceManager::self();

    CollectionStatistics::self();

    // Initialize the preprocessor manager
    PreprocessorManager::init();

    // Forcibly disable it if configuration says so
    if (settings.value(QStringLiteral("General/DisablePreprocessing"), false).toBool()) {
        PreprocessorManager::instance()->setEnabled(false);
    }

    if (settings.value(QStringLiteral("Cache/EnableCleaner"), true).toBool()) {
        mCacheCleaner = new CacheCleaner();
    }

    mIntervalCheck = new IntervalCheck();
    mStorageJanitor = new StorageJanitor();
    mItemRetrieval = new ItemRetrievalManager();
    mAgentSearchManager = new SearchTaskManager();

    const QStringList searchManagers = settings.value(QStringLiteral("Search/Manager"),
                                                      QStringList() << QStringLiteral("Agent")).toStringList();
    mSearchManager = new SearchManager(searchManagers);

    new ServerAdaptor(this);
    QDBusConnection::sessionBus().registerObject(QStringLiteral("/Server"), this);

    const QByteArray dbusAddress = qgetenv("DBUS_SESSION_BUS_ADDRESS");
    if (!dbusAddress.isEmpty()) {
        connectionSettings.setValue(QStringLiteral("DBUS/Address"), QLatin1String(dbusAddress));
    }

    QDBusServiceWatcher *watcher = new QDBusServiceWatcher(DBus::serviceName(DBus::Control),
                                                           QDBusConnection::sessionBus(),
                                                           QDBusServiceWatcher::WatchForOwnerChange, this);

    connect(watcher, &QDBusServiceWatcher::serviceOwnerChanged,
            this, &AkonadiServer::serviceOwnerChanged);

    // Unhide all the items that are actually hidden.
    // The hidden flag was probably left out after an (abrupt)
    // server quit. We don't attempt to resume preprocessing
    // for the items as we don't actually know at which stage the
    // operation was interrupted...
    db->unhideAllPimItems();

    // Cleanup referenced collections from the last run
    CollectionReferenceManager::cleanup();

    // We are ready, now register org.freedesktop.Akonadi service to DBus and
    // the fun can begin
    if (!QDBusConnection::sessionBus().registerService(DBus::serviceName(DBus::Server))) {
        qCCritical(AKONADISERVER_LOG) << "Unable to connect to dbus service: " << QDBusConnection::sessionBus().lastError().message();
        quit();
        return false;
    }

    return true;
}

AkonadiServer::~AkonadiServer()
{
}

template <typename T> static void quitThread(T &thread)
{
    if (!thread) {
        return;
    }
    thread->quit();
    thread->wait();
    delete thread;
    thread = 0;
}

bool AkonadiServer::quit()
{
    if (mAlreadyShutdown) {
        return true;
    }
    mAlreadyShutdown = true;

    qCDebug(AKONADISERVER_LOG) << "terminating connection threads";
    for (int i = 0; i < mConnections.count(); ++i) {
        delete mConnections[i];
    }
    mConnections.clear();

    qCDebug(AKONADISERVER_LOG) << "terminating service threads";
    delete mCacheCleaner;
    delete mIntervalCheck;
    delete mStorageJanitor;
    delete mItemRetrieval;
    delete mAgentSearchManager;
    delete mSearchManager;
    delete mNotificationManager;

    // Terminate the preprocessor manager before the database but after all connections are gone
    PreprocessorManager::done();

    if (DbConfig::isConfigured()) {
        if (DataStore::hasDataStore()) {
            DataStore::self()->close();
        }
        qCDebug(AKONADISERVER_LOG) << "stopping db process";
        stopDatabaseProcess();
    }

    QSettings settings(StandardDirs::serverConfigFile(), QSettings::IniFormat);
    const QString connectionSettingsFile = StandardDirs::connectionConfigFile(XdgBaseDirs::WriteOnly);

#ifndef Q_OS_WIN
    const QString socketDir = Utils::preferredSocketDirectory(StandardDirs::saveDir("data"));

    if (!QDir::home().remove(socketDir + QLatin1String("/akonadiserver.socket"))) {
        qCCritical(AKONADISERVER_LOG) << "Failed to remove Unix socket";
    }
#endif
    if (!QDir::home().remove(connectionSettingsFile)) {
        qCCritical(AKONADISERVER_LOG) << "Failed to remove runtime connection config file";
    }

    QTimer::singleShot(0, this, &AkonadiServer::doQuit);

    return true;
}

void AkonadiServer::doQuit()
{
    QCoreApplication::exit();
}

void AkonadiServer::newCmdConnection(quintptr socketDescriptor)
{
    if (mAlreadyShutdown) {
        return;
    }

    QPointer<Connection> connection = new Connection(socketDescriptor);
    connect(connection.data(), &Connection::disconnected,
            this, [connection]() {
                delete connection.data();
            }, Qt::QueuedConnection);
    mConnections.append(connection);
}

AkonadiServer *AkonadiServer::instance()
{
    if (!s_instance) {
        s_instance = new AkonadiServer();
    }
    return s_instance;
}

bool AkonadiServer::startDatabaseProcess()
{
    if (!DbConfig::configuredDatabase()->useInternalServer()) {
        qCCritical(AKONADISERVER_LOG) << "Trying to start external database!";
    }

    // create the database directories if they don't exists
    StandardDirs::saveDir("data");
    StandardDirs::saveDir("data", QStringLiteral("file_db_data"));

    return DbConfig::configuredDatabase()->startInternalServer();
}

bool AkonadiServer::createDatabase()
{
    bool success = true;
    const QLatin1String initCon("initConnection");
    QSqlDatabase db = QSqlDatabase::addDatabase(DbConfig::configuredDatabase()->driverName(), initCon);
    DbConfig::configuredDatabase()->apply(db);
    db.setDatabaseName(DbConfig::configuredDatabase()->databaseName());
    if (!db.isValid()) {
        qCCritical(AKONADISERVER_LOG) << "Invalid database object during initial database connection";
        return false;
    }

    if (db.open()) {
        db.close();
    } else {
        qCCritical(AKONADISERVER_LOG) << "Failed to use database" << DbConfig::configuredDatabase()->databaseName();
        qCCritical(AKONADISERVER_LOG) << "Database error:" << db.lastError().text();
        qCDebug(AKONADISERVER_LOG) << "Trying to create database now...";

        db.close();
        db.setDatabaseName(QString());
        if (db.open()) {
            {
                QSqlQuery query(db);
                if (!query.exec(QStringLiteral("CREATE DATABASE %1").arg(DbConfig::configuredDatabase()->databaseName()))) {
                    qCCritical(AKONADISERVER_LOG) << "Failed to create database";
                    qCCritical(AKONADISERVER_LOG) << "Query error:" << query.lastError().text();
                    qCCritical(AKONADISERVER_LOG) << "Database error:" << db.lastError().text();
                    success = false;
                }
            } // make sure query is destroyed before we close the db
            db.close();
        } else {
            qCCritical(AKONADISERVER_LOG) << "Failed to connect to database!";
            qCCritical(AKONADISERVER_LOG) << "Database error:" << db.lastError().text();
            success = false;
        }
    }
    QSqlDatabase::removeDatabase(initCon);
    return success;
}

void AkonadiServer::stopDatabaseProcess()
{
    if (!DbConfig::configuredDatabase()->useInternalServer()) {
        return;
    }

    DbConfig::configuredDatabase()->stopInternalServer();
}

void AkonadiServer::serviceOwnerChanged(const QString &name, const QString &oldOwner, const QString &newOwner)
{
    Q_UNUSED(name);
    Q_UNUSED(oldOwner);
    if (newOwner.isEmpty()) {
        qCCritical(AKONADISERVER_LOG) << "Control process died, committing suicide!";
        quit();
    }
}

CacheCleaner *AkonadiServer::cacheCleaner()
{
    return mCacheCleaner;
}

IntervalCheck *AkonadiServer::intervalChecker()
{
    return mIntervalCheck;
}

NotificationManager *AkonadiServer::notificationManager()
{
    return mNotificationManager;
}

QString AkonadiServer::serverPath() const
{
    return XdgBaseDirs::homePath("config");
}

#include "akonadi.moc"
