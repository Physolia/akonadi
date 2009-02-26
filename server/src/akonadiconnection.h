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
#ifndef AKONADICONNECTION_H
#define AKONADICONNECTION_H

#include <QtCore/QPointer>
#include <QtCore/QThread>
#include <QtNetwork/QLocalSocket>

#include "akonadiprivate_export.h"
#include "entities.h"
#include "global.h"

namespace Akonadi {
    class Handler;
    class Response;
    class DataStore;
    class ImapParser;
    class Collection;
    class ImapStreamParser;

/**
    An AkonadiConnection represents one connection of a client to the server.
*/
class AKONADIPRIVATE_EXPORT AkonadiConnection : public QThread
{
    Q_OBJECT
public:
    AkonadiConnection( quintptr socketDescriptor, QObject *parent );
    virtual ~AkonadiConnection();
    void run();

    virtual DataStore* storageBackend();
    qint64 selectedCollectionId() const;
    void setSelectedCollection( qint64 collection );

    const Collection selectedCollection();

    void addStatusMessage( const QByteArray& msg );
    void flushStatusMessageQueue();

    void setSessionId( const QByteArray &id );
    QByteArray sessionId() const;

protected Q_SLOTS:
    void slotDisconnected();
    /**
     * New data arrived from the client. Creates a handler for it and passes the data to the handler.
     */
    void slotNewData();
    void slotResponseAvailable( const Response& );
    void slotConnectionStateChange( ConnectionState );

protected:
    AkonadiConnection() {} // used for testing
    void writeOut( const QByteArray &data );
    Handler* findHandlerForCommand( const QByteArray& command );


private:
    quintptr m_socketDescriptor;
    QLocalSocket *m_socket;
    QPointer<Handler> m_currentHandler;
    ConnectionState m_connectionState;
    mutable DataStore *m_backend;
    qint64 m_selectedConnection;
    QList<QByteArray> m_statusMessageQueue;
    QString m_identifier;
    QByteArray m_sessionId;
    ImapParser *m_parser;
    ImapStreamParser *m_streamParser;
};

}

#endif
