/*
    Copyright (c) 2008 Volker Krause <vkrause@kde.org>
    Copyright (C) 2014 Daniel Vrátil <dvraitl@redhat.com>

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

#include "intervalcheck.h"
#include "storage/datastore.h"
#include "storage/itemretrievalmanager.h"

using namespace Akonadi::Server;

static int MINIMUM_AUTOSYNC_INTERVAL = 5; // minutes
static int MINIMUM_COLTREESYNC_INTERVAL = 5; // minutes

IntervalCheck *IntervalCheck::s_instance = 0;

IntervalCheck::IntervalCheck( QObject *parent )
  : CollectionScheduler( parent )
{
  Q_ASSERT( s_instance == 0 );
  s_instance = this;
}

IntervalCheck::~ IntervalCheck()
{
}

IntervalCheck *IntervalCheck::self()
{
  Q_ASSERT( s_instance );
  return s_instance;
}

void IntervalCheck::requestCollectionSync( const Collection &collection )
{
  QMetaObject::invokeMethod( this, "collectionExpired",
                             Qt::QueuedConnection,
                             Q_ARG( Collection, collection ) );
}

int IntervalCheck::collectionScheduleInterval( const Collection &collection )
{
  return collection.cachePolicyCheckInterval();
}

bool IntervalCheck::hasChanged( const Collection &collection, const Collection &changed )
{
  return collection.cachePolicyCheckInterval() != changed.cachePolicyCheckInterval()
        || collection.subscribed() != changed.subscribed();
}

bool IntervalCheck::shouldScheduleCollection( const Collection &collection )
{
  return collection.cachePolicyCheckInterval() > 0
        && collection.subscribed();
}

void IntervalCheck::collectionExpired( const Collection &collection )
{
  const QDateTime now( QDateTime::currentDateTime() );

  if ( collection.parentId() == 0 ) {
    const QString resourceName = collection.resource().name();

    const int interval = qMax( MINIMUM_COLTREESYNC_INTERVAL, collection.cachePolicyCheckInterval() );

    const QDateTime lastExpectedCheck = now.addSecs( interval * -60 );
    if ( !mLastCollectionTreeSyncs.contains( resourceName ) || mLastCollectionTreeSyncs.value( resourceName ) < lastExpectedCheck ) {
      mLastCollectionTreeSyncs.insert( resourceName, now );
      QMetaObject::invokeMethod( ItemRetrievalManager::instance(), "triggerCollectionTreeSync",
                                 Qt::QueuedConnection,
                                 Q_ARG( QString, resourceName ) );
    }
  }

  // now on to the actual collection syncing
  const int interval = qMax( MINIMUM_AUTOSYNC_INTERVAL, collection.cachePolicyCheckInterval() );

  const QDateTime lastExpectedCheck = now.addSecs( interval * -60 );
  if ( mLastChecks.contains( collection.id() ) && mLastChecks.value( collection.id() ) > lastExpectedCheck ) {
    return;
  }
  mLastChecks.insert( collection.id(), now );
  QMetaObject::invokeMethod( ItemRetrievalManager::instance(), "triggerCollectionSync",
                             Qt::QueuedConnection,
                             Q_ARG( QString, collection.resource().name() ),
                             Q_ARG( qint64, collection.id() ) );
}
