/*
    Copyright (c) 2007 Tobias Koenig <tokoe@kde.org>

    This library is free software; you can redistribute it and/or modify it
    under the terms of the GNU Library General Public License as published by
    the Free Software Foundation; either version 2 of the License, or (at your
    option) any later version.

    This library is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Library General Public
    License for more details.

    You should have received a copy of the GNU Library General Public License
    along with this library; see the file COPYING.LIB.  If not, write to the
    Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
    02110-1301, USA.
*/

// @cond PRIVATE

#include "monitor_p.h"

#include "collectionfetchjob.h"
#include "collectionstatistics.h"
#include "dbusconnectionpool.h"
#include "itemfetchjob.h"
#include "notificationmessage_p.h"
#include "notificationmanagerinterface.h"
#include "session.h"

#include <kdebug.h>
#include <kcomponentdata.h>

using namespace Akonadi;

static const int PipelineSize = 5;

MonitorPrivate::MonitorPrivate(Monitor * parent) :
  q_ptr( parent ),
  notificationSource( 0 ),
  monitorAll( false ),
  session( Session::defaultSession() ),
  collectionCache( 3*PipelineSize, session ), // needs to be at least 3x pipeline size for the collection move case
  itemCache( PipelineSize, session ), // needs to be at least 1x pipeline size
  fetchCollection( false ),
  fetchCollectionStatistics( false ),
  useRefCounting( false )
{
}

void MonitorPrivate::init()
{
  QObject::connect( &collectionCache, SIGNAL( dataAvailable() ), q_ptr, SLOT( dataAvailable() ) );
  QObject::connect( &itemCache, SIGNAL( dataAvailable() ), q_ptr, SLOT( dataAvailable() ) );
  QObject::connect( ServerManager::self(), SIGNAL(stateChanged(Akonadi::ServerManager::State)),
                    q_ptr, SLOT(serverStateChanged(Akonadi::ServerManager::State)) );

  NotificationMessage::registerDBusTypes();
}

bool MonitorPrivate::connectToNotificationManager()
{
  delete notificationSource;
  notificationSource = 0;

  org::freedesktop::Akonadi::NotificationManager manager(
          QLatin1String( "org.freedesktop.Akonadi" ),
          QLatin1String( "/notifications" ),
          DBusConnectionPool::threadConnection() );

  QDBusObjectPath p = manager.subscribe( KGlobal::mainComponent().componentName() );
  if ( manager.lastError().isValid() ) {
    // :TODO: What to do?
    return false;
  }

  notificationSource = new org::freedesktop::Akonadi::NotificationSource(
              QLatin1String( "org.freedesktop.Akonadi" ),
              p.path(),
              DBusConnectionPool::threadConnection(), q_ptr );

  if ( !notificationSource ) {
    // :TODO: error handling
    return false;
  }

  QObject::connect( notificationSource, SIGNAL( notify( Akonadi::NotificationMessage::List ) ),
                    q_ptr, SLOT( slotNotify( Akonadi::NotificationMessage::List ) ) );

  return true;
}

void MonitorPrivate::serverStateChanged(ServerManager::State state)
{
  if ( state == ServerManager::Running )
    connectToNotificationManager();
}

int MonitorPrivate::pipelineSize() const
{
  return PipelineSize;
}

bool MonitorPrivate::isLazilyIgnored( const NotificationMessage & msg ) const
{
  NotificationMessage::Operation op = msg.operation();

  if ( !fetchCollectionStatistics &&
       ( msg.type() == NotificationMessage::Item ) && ( ( op == NotificationMessage::Add && q_ptr->receivers( SIGNAL( itemAdded( const Akonadi::Item&, const Akonadi::Collection& ) ) ) == 0 )
    || ( op == NotificationMessage::Remove && q_ptr->receivers( SIGNAL( itemRemoved( const Akonadi::Item& ) ) ) == 0 )
    || ( op == NotificationMessage::Modify && q_ptr->receivers( SIGNAL( itemChanged( const Akonadi::Item&, const QSet<QByteArray>& ) ) ) == 0 )
    || ( op == NotificationMessage::Move && q_ptr->receivers( SIGNAL( itemMoved( const Akonadi::Item&, const Akonadi::Collection&, const Akonadi::Collection& ) ) ) == 0 )
    || ( op == NotificationMessage::Link && q_ptr->receivers( SIGNAL( itemLinked( const Akonadi::Item&, const Akonadi::Collection& ) ) ) == 0 )
    || ( op == NotificationMessage::Unlink && q_ptr->receivers( SIGNAL( itemUnlinked( const Akonadi::Item&, const Akonadi::Collection& ) ) ) == 0 ) ) )
  {
    return true;
  }

  if ( !useRefCounting )
    return false;

  if ( msg.type() == NotificationMessage::Collection )
    // Lazy fetching can only affects items.
    return false;

  Collection::Id parentCollectionId = msg.parentCollection();

  if ( ( op == NotificationMessage::Add )
    || ( op == NotificationMessage::Remove )
    || ( op == NotificationMessage::Modify )
    || ( op == NotificationMessage::Link )
    || ( op == NotificationMessage::Unlink ) )
  {
    if ( refCountMap.contains( parentCollectionId ) || m_buffer.isBuffered( parentCollectionId ) )
      return false;
  }


  if ( op == NotificationMessage::Move )
  {
    if ( !refCountMap.contains( parentCollectionId ) && !m_buffer.isBuffered( parentCollectionId ) )
      if ( !refCountMap.contains( msg.parentDestCollection() ) && !m_buffer.isBuffered( msg.parentDestCollection() ) )
        return true;
    // We can't ignore the move. It must be transformed later into a removal or insertion.
    return false;
  }
  return true;
}

bool MonitorPrivate::acceptNotification( const NotificationMessage & msg )
{
  // session is ignored
  if ( sessions.contains( msg.sessionId() ) )
    return false;

  // corresponding signal is not connected
  if ( isLazilyIgnored( msg ) )
    return false;

  // user requested everything
  if ( monitorAll )
    return true;

  switch ( msg.type() ) {
    case NotificationMessage::InvalidType:
      kWarning() << "Received invalid change notification!";
      return false;

    case NotificationMessage::Item:
      // we have a resource or mimetype filter
      if ( !resources.isEmpty() || !mimetypes.isEmpty() ) {
        if ( isMimeTypeMonitored( msg.mimeType() ) || resources.contains( msg.resource() ) || isMoveDestinationResourceMonitored( msg ) )
          return true;
        return false;
      }

      // we explicitly monitor that item or the collections it's in
      return items.contains( msg.uid() )
          || isCollectionMonitored( msg.parentCollection() )
          || isCollectionMonitored( msg.parentDestCollection() );

    case NotificationMessage::Collection:
      // we have a resource filter
      if ( !resources.isEmpty() ) {
        const bool resourceMatches = resources.contains( msg.resource() ) || isMoveDestinationResourceMonitored( msg );
        // a bit hacky, but match the behaviour from the item case,
        // if resource is the only thing we are filtering on, stop here, and if the resource filter matched, of course
        if ( mimetypes.isEmpty() || resourceMatches )
          return resourceMatches;
        // else continue
      }

      // we explicitly monitor that colleciton, or all of them
      return isCollectionMonitored( msg.uid() )
          || isCollectionMonitored( msg.parentCollection() )
          || isCollectionMonitored( msg.parentDestCollection() );
  }
  Q_ASSERT( false );
  return false;
}

void MonitorPrivate::dispatchNotifications()
{
  while ( pipeline.size() < pipelineSize() && !pendingNotifications.isEmpty() ) {
    const NotificationMessage msg = pendingNotifications.dequeue();
    if ( ensureDataAvailable( msg ) && pipeline.isEmpty() )
      emitNotification( msg );
    else
      pipeline.enqueue( msg );
  }
}

void MonitorPrivate::cleanOldNotifications()
{
  foreach ( const NotificationMessage &msg, pipeline ) {
    if ( !acceptNotification( msg ) ) {
      pipeline.removeOne( msg );
    }
  }

  foreach ( const NotificationMessage &msg, pendingNotifications ) {
    if ( !acceptNotification( msg ) ) {
      pendingNotifications.removeOne( msg );
    }
  }
}

bool MonitorPrivate::ensureDataAvailable( const NotificationMessage &msg )
{
  bool allCached = true;
  if ( fetchCollection ) {
    if ( !collectionCache.ensureCached( msg.parentCollection(), mCollectionFetchScope ) )
      allCached = false;
    if ( msg.operation() == NotificationMessage::Move && !collectionCache.ensureCached( msg.parentDestCollection(), mCollectionFetchScope ) )
      allCached = false;
  }
  if ( msg.operation() == NotificationMessage::Remove )
    return allCached; // the actual object is gone already, nothing to fetch there

  if ( msg.type() == NotificationMessage::Item && !mItemFetchScope.isEmpty() ) {
    if ( !itemCache.ensureCached( msg.uid(), mItemFetchScope ) )
      allCached = false;
  } else if ( msg.type() == NotificationMessage::Collection && fetchCollection ) {
    if ( !collectionCache.ensureCached( msg.uid(), mCollectionFetchScope ) )
      allCached = false;
  }
  return allCached;
}

void MonitorPrivate::emitNotification( const NotificationMessage &msg )
{
  const Collection parent = collectionCache.retrieve( msg.parentCollection() );
  Collection destParent;
  if ( msg.operation() == NotificationMessage::Move )
    destParent = collectionCache.retrieve( msg.parentDestCollection() );

  if ( msg.type() == NotificationMessage::Collection ) {
    const Collection col = collectionCache.retrieve( msg.uid() );
    emitCollectionNotification( msg, col, parent, destParent );
  } else if ( msg.type() == NotificationMessage::Item ) {
    const Item item = itemCache.retrieve( msg.uid() );
    emitItemNotification( msg, item, parent, destParent );
  }
}

void MonitorPrivate::dataAvailable()
{
  while ( !pipeline.isEmpty() ) {
    const NotificationMessage msg = pipeline.head();
    if ( ensureDataAvailable( msg ) ) {
      // dequeue should be before emit, otherwise stuff might happen (like dataAvailable
      // being called again) and we end up dequeuing an empty pipeline
      pipeline.dequeue();
      emitNotification( msg );
    } else {
      break;
    }
  }
  dispatchNotifications();
}

void MonitorPrivate::updatePendingStatistics( const NotificationMessage &msg )
{
  if ( msg.type() == NotificationMessage::Item ) {
    notifyCollectionStatisticsWatchers( msg.parentCollection(), msg.resource() );
    // FIXME use the proper resource of the target collection, for cross resource moves
    notifyCollectionStatisticsWatchers( msg.parentDestCollection(), msg.resource() );
  } else if ( msg.type() == NotificationMessage::Collection && msg.operation() == NotificationMessage::Remove ) {
    // no need for statistics updates anymore
    recentlyChangedCollections.remove( msg.uid() );
  }
}

void MonitorPrivate::slotSessionDestroyed( QObject * object )
{
  Session* objectSession = qobject_cast<Session*>( object );
  if ( objectSession )
    sessions.removeAll( objectSession->sessionId() );
}

void MonitorPrivate::slotStatisticsChangedFinished( KJob* job )
{
  if ( job->error() ) {
    kWarning() << "Error on fetching collection statistics: " << job->errorText();
  } else {
    CollectionStatisticsJob *statisticsJob = static_cast<CollectionStatisticsJob*>( job );
    Q_ASSERT( statisticsJob->collection().isValid() );
    emit q_ptr->collectionStatisticsChanged( statisticsJob->collection().id(),
                                             statisticsJob->statistics() );
  }
}

void MonitorPrivate::slotFlushRecentlyChangedCollections()
{
  foreach ( Collection::Id collection, recentlyChangedCollections ) {
    Q_ASSERT( collection >= 0 );
    if ( fetchCollectionStatistics ) {
      fetchStatistics( collection );
    } else {
      static const CollectionStatistics dummyStatistics;
      emit q_ptr->collectionStatisticsChanged( collection, dummyStatistics );
    }
  }
  recentlyChangedCollections.clear();
}

void MonitorPrivate::appendAndCompress( const NotificationMessage &msg  )
{
  if ( !useRefCounting || msg.operation() != NotificationMessage::Move || msg.type() != NotificationMessage::Item )
    return NotificationMessage::appendAndCompress( pendingNotifications, msg );

  bool sourceWatched = refCountMap.contains( msg.parentCollection() ) || m_buffer.isBuffered( msg.parentCollection() );
  bool destWatched = refCountMap.contains( msg.parentDestCollection() ) || m_buffer.isBuffered( msg.parentDestCollection() );

  if ( sourceWatched && destWatched )
    return NotificationMessage::appendAndCompress( pendingNotifications, msg );

  if ( sourceWatched )
  {
    // Transform into a removal
    NotificationMessage removalMessage = msg;
    removalMessage.setOperation( NotificationMessage::Remove );
    removalMessage.setParentDestCollection( -1 );
    return NotificationMessage::appendAndCompress( pendingNotifications, removalMessage );
  }

  // Transform into an insertion
  NotificationMessage insertionMessage = msg;
  insertionMessage.setOperation( NotificationMessage::Add );
  insertionMessage.setParentCollection( msg.parentDestCollection() );
  insertionMessage.setParentDestCollection( -1 );
  NotificationMessage::appendAndCompress( pendingNotifications, insertionMessage );
}

void MonitorPrivate::slotNotify( const NotificationMessage::List &msgs )
{
  foreach ( const NotificationMessage &msg, msgs ) {
    invalidateCaches( msg );
    updatePendingStatistics( msg );
    if ( acceptNotification( msg ) ) {
      appendAndCompress( msg );
    }
  }

  dispatchNotifications();
}

void MonitorPrivate::emitItemNotification( const NotificationMessage &msg, const Item &item,
                                             const Collection &collection, const Collection &collectionDest  )
{
  Q_ASSERT( msg.type() == NotificationMessage::Item );
  Collection col = collection;
  Collection colDest = collectionDest;
  if ( !col.isValid() ) {
    col = Collection( msg.parentCollection() );
    col.setResource( QString::fromUtf8( msg.resource() ) );
  }
  if ( !colDest.isValid() ) {
    colDest = Collection( msg.parentDestCollection() );
    // HACK: destination resource is delivered in the parts field...
    if ( !msg.itemParts().isEmpty() )
      colDest.setResource( QString::fromLatin1( *(msg.itemParts().begin()) ) );
  }

  Item it = item;
  if ( !it.isValid() || msg.operation() == NotificationMessage::Remove ) {
    it = Item( msg.uid() );
    it.setRemoteId( msg.remoteId() );
    it.setMimeType( msg.mimeType() );
  } else if ( it.remoteId().isEmpty() && !msg.remoteId().isEmpty() ) {
    // recover RID, in case of inter-resource moves the source RID is only in the
    // notification but not in the item loaded from Akonadi
    it.setRemoteId( msg.remoteId() );
  } else if ( msg.operation() == NotificationMessage::Move && col.resource() != colDest.resource() ) {
    // recover RID in case of inter-resource moves (part 2), if the destination has already
    // changed the RID we need to reset to the one belonging to the source resource
    it.setRemoteId( msg.remoteId() );
  }

  if ( !it.parentCollection().isValid() ) {
    if ( msg.operation() == NotificationMessage::Move )
      it.setParentCollection( colDest );
    else
      it.setParentCollection( col );
  }

  // HACK: We have the remoteRevision stored in the itemParts set
  //       for delete operations to avoid protocol breakage
  if ( msg.operation() == NotificationMessage::Remove ) {
    if ( !msg.itemParts().isEmpty() ) { //TODO: investigate why it could be empty
      const QString remoteRevision = QString::fromUtf8( msg.itemParts().toList().first() );
      it.setRemoteRevision( remoteRevision );
    }
  }

  switch ( msg.operation() ) {
    case NotificationMessage::Add:
      emit q_ptr->itemAdded( it, col );
      break;
    case NotificationMessage::Modify:
      emit q_ptr->itemChanged( it, msg.itemParts() );
      break;
    case NotificationMessage::Move:
      emit q_ptr->itemMoved( it, col, colDest );
      break;
    case NotificationMessage::Remove:
      emit q_ptr->itemRemoved( it );
      break;
    case NotificationMessage::Link:
      emit q_ptr->itemLinked( it, col );
      break;
    case NotificationMessage::Unlink:
      emit q_ptr->itemUnlinked( it, col );
      break;
    default:
      kDebug() << "Unknown operation type" << msg.operation() << "in item change notification";
      break;
  }
}

void MonitorPrivate::emitCollectionNotification( const NotificationMessage &msg, const Collection &col,
                                                   const Collection &par, const Collection &dest )
{
  Q_ASSERT( msg.type() == NotificationMessage::Collection );
  Collection parent = par;
  if ( !parent.isValid() )
    parent = Collection( msg.parentCollection() );
  Collection destination = dest;
  if ( !destination.isValid() )
    destination = Collection( msg.parentDestCollection() );

  Collection collection = col;
  if ( !collection.isValid() || msg.operation() == NotificationMessage::Remove ) {
    collection = Collection( msg.uid() );
    collection.setResource( QString::fromUtf8( msg.resource() ) );
    collection.setRemoteId( msg.remoteId() );
  } else if ( collection.remoteId().isEmpty() && !msg.remoteId().isEmpty() ) {
    // recover RID, in case of inter-resource moves the source RID is only in the
    // notification but not in the item loaded from Akonadi
    collection.setRemoteId( msg.remoteId() );
  } else if ( msg.operation() == NotificationMessage::Move && parent.resource() != destination.resource() ) {
    // recover RID in case of inter-resource moves (part 2), if the destination has already
    // changed the RID we need to reset to the one belonging to the source resource
    collection.setRemoteId( msg.remoteId() );
  }

  if ( !collection.parentCollection().isValid() ) {
    if ( msg.operation() == NotificationMessage::Move )
      collection.setParentCollection( destination );
    else
      collection.setParentCollection( parent );
  }

  // HACK: We have the remoteRevision stored in the itemParts set
  //       for delete operations to avoid protocol breakage
  if ( msg.operation() == NotificationMessage::Remove ) {
    if ( !msg.itemParts().isEmpty() ) { //TODO: investigate why it could be empty
      const QString remoteRevision = QString::fromUtf8( msg.itemParts().toList().first() );
      collection.setRemoteRevision( remoteRevision );
    }
  }

  switch ( msg.operation() ) {
    case NotificationMessage::Add:
      emit q_ptr->collectionAdded( collection, parent );
      break;
    case NotificationMessage::Modify:
      emit q_ptr->collectionChanged( collection );
      emit q_ptr->collectionChanged( collection, msg.itemParts() );
      break;
    case NotificationMessage::Move:
      emit q_ptr->collectionMoved( collection, parent, destination );
      break;
    case NotificationMessage::Remove:
      emit q_ptr->collectionRemoved( collection );
      break;
    case NotificationMessage::Subscribe:
      if ( !monitorAll )
        emit q_ptr->collectionSubscribed( collection, parent );
      break;
    case NotificationMessage::Unsubscribe:
      if ( !monitorAll )
        emit q_ptr->collectionUnsubscribed( collection );
      break;
    default:
      kDebug() << "Unknown operation type" << msg.operation() << "in collection change notification";
  }
}

void MonitorPrivate::invalidateCaches( const NotificationMessage &msg )
{
  // remove invalidates
  if ( msg.operation() == NotificationMessage::Remove ) {
    if ( msg.type() == NotificationMessage::Collection ) {
      collectionCache.invalidate( msg.uid() );
    } else if ( msg.type() == NotificationMessage::Item ) {
      itemCache.invalidate( msg.uid() );
    }
  }

  // modify removes the cache entry, as we need to re-fetch
  // And subscription modify the visibility of the collection by the collectionFetchScope.
  if ( msg.operation() == NotificationMessage::Modify
        || msg.operation() == NotificationMessage::Move
        || msg.operation() == NotificationMessage::Subscribe ) {
    if ( msg.type() == NotificationMessage::Collection ) {
      collectionCache.update( msg.uid(), mCollectionFetchScope );
    } else if ( msg.type() == NotificationMessage::Item ) {
      itemCache.update( msg.uid(), mItemFetchScope );
    }
  }
}

void MonitorPrivate::invalidateCache( const Collection &col )
{
  collectionCache.update( col.id(), mCollectionFetchScope );
}

void MonitorPrivate::ref( Collection::Id id )
{
  if ( !refCountMap.contains( id ) )
  {
    refCountMap.insert( id, 0 );
  }
  ++refCountMap[ id ];

  if ( m_buffer.isBuffered( id ) )
    m_buffer.purge( id );
}

Collection::Id MonitorPrivate::deref( Collection::Id id )
{
  Q_ASSERT( refCountMap.contains( id ) );
  if ( --refCountMap[ id ] == 0 )
  {
    refCountMap.remove( id );
  }
  return m_buffer.buffer( id );
}

void MonitorPrivate::PurgeBuffer::purge( Collection::Id id )
{
  int idx = m_buffer.indexOf( id, 0 );
  while ( idx <= m_index )
  {
    if ( idx < 0 )
      break;
    m_buffer.remove( idx );
    if ( m_index > 0 )
      --m_index;
    idx = m_buffer.indexOf( id, idx );
  }
  while ( int idx = m_buffer.indexOf( id, m_index ) > -1 )
  {
    m_buffer.remove( idx );
  }
}

Collection::Id MonitorPrivate::PurgeBuffer::buffer( Collection::Id id )
{
  if ( m_index == MAXBUFFERSIZE )
  {
    m_index = 0;
  }
  Collection::Id bumpedId = -1;
  if ( m_buffer.size() == MAXBUFFERSIZE )
  {
    bumpedId = m_buffer[ m_index ];
    m_buffer.remove( m_index );
  }

  // Ensure that we don't put a duplicate @p id into the buffer.
  purge( id );

  m_buffer.insert( m_index, id );
  ++m_index;

  return bumpedId;
}

// @endcond
