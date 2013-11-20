/***************************************************************************
 *   Copyright (C) 2006 by Tobias Koenig <tokoe@kde.org>                   *
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
 *   51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.         *
 ***************************************************************************/

#include "store.h"
#include <akdebug.h>

#include "akonadi.h"
#include "akonadiconnection.h"
#include "handlerhelper.h"
#include "response.h"
#include "storage/datastore.h"
#include "storage/transaction.h"
#include "storage/itemqueryhelper.h"
#include "storage/selectquerybuilder.h"
#include "storage/parthelper.h"
#include "storage/dbconfig.h"
#include "storage/itemretriever.h"
#include <storage/parttypehelper.h>

#include "libs/imapparser_p.h"
#include "imapstreamparser.h"

#include <libs/protocol_p.h>

#include <QtCore/QStringList>
#include <QLocale>
#include <QDebug>
#include <QFile>

#include <algorithm>
#include <functional>

using namespace Akonadi;

static bool payloadChanged( const QSet<QByteArray> &changes )
{
  Q_FOREACH ( const QByteArray &change, changes ) {
    if ( change.startsWith( AKONADI_PARAM_PLD ) ) {
      return true;
    }
  }
  return false;
}

Store::Store( Scope::SelectionScope scope )
  : Handler()
  , mScope( scope )
  , mPos( 0 )
  , mPreviousRevision( -1 )
  , mSize( 0 )
  , mCheckRevision( false )
{
}

bool Store::replaceFlags( const PimItem::List &item, const QList<QByteArray> &flags )
{
  Flag::List flagList = HandlerHelper::resolveFlags( flags );
  DataStore *store = connection()->storageBackend();

  if ( !store->setItemsFlags( item, flagList ) ) {
    throw HandlerException( "Store::replaceFlags: Unable to set new item flags" );
  }

  return true;
}

bool Store::addFlags( const PimItem::List &items, const QList<QByteArray> &flags, bool &flagsChanged )
{
  const Flag::List flagList = HandlerHelper::resolveFlags( flags );
  DataStore *store = connection()->storageBackend();

  if ( !store->appendItemsFlags( items, flagList, flagsChanged ) ) {
    akDebug() << "Store::addFlags: Unable to add new item flags";
    return false;
  }
  return true;
}

bool Store::deleteFlags( const PimItem::List &items, const QList<QByteArray> &flags )
{
  DataStore *store = connection()->storageBackend();

  QVector<Flag> flagList;
  flagList.reserve( flags.size() );
  for ( int i = 0; i < flags.count(); ++i ) {
    Flag flag = Flag::retrieveByName( QString::fromUtf8( flags[i] ) );
    if ( !flag.isValid() ) {
      continue;
    }

    flagList.append( flag );
  }

  if ( !store->removeItemsFlags( items, flagList ) ) {
    akDebug() << "Store::deleteFlags: Unable to remove item flags";
    return false;
  }
  return true;
}

bool Store::parseStream()
{
  parseCommand();
  DataStore *store = connection()->storageBackend();
  Transaction transaction( store );
   // Set the same modification time for each item.
  const QDateTime modificationtime = QDateTime::currentDateTime().toUTC();

  // retrieve selected items
  SelectQueryBuilder<PimItem> qb;
  ItemQueryHelper::scopeToQuery( mScope, connection(), qb );
  if ( !qb.exec() ) {
    return failureResponse( "Unable to retrieve items" );
  }
  PimItem::List pimItems = qb.result();
  if ( pimItems.isEmpty() ) {
    return failureResponse( "No items found" );
  }

  for ( int i = 0; i < pimItems.size(); ++i ) {
    if ( mCheckRevision ) {
      // check for conflicts if a resources tries to overwrite an item with dirty payload
      const PimItem &pimItem = pimItems.at( i );
      if ( connection()->isOwnerResource( pimItem ) ) {
        if ( pimItem.dirty() ) {
          const QString error = QString::fromLatin1( "[LRCONFLICT] Resource %1 tries to modify item %2 (%3) (in collection %4) with dirty payload, aborting STORE." );
          throw HandlerException( error.arg( pimItem.collection().resource().name() ).arg( pimItem.id() )
                                       .arg( pimItem.remoteId() ).arg( pimItem.collectionId() ) );
        }
      }

      // check and update revisions
      if ( pimItems.at( i ).rev() != (int)mPreviousRevision ) {
        throw HandlerException( "[LLCONFLICT] Item was modified elsewhere, aborting STORE." );
      }
    }
  }

  QSet<QByteArray> changes;
  qint64 partSizes = 0;
  bool invalidateCache = false;
  bool undirty = false;
  bool silent = false;

  // apply modifications
  m_streamParser->beginList();
  while ( !m_streamParser->atListEnd() ) {
    // parse the command
    QByteArray command = m_streamParser->readString();
    if ( command.isEmpty() ) {
      throw HandlerException( "Syntax error" );
    }
    Operation op = Replace;
    if ( command.startsWith( '+' ) ) {
      op = Add;
      command = command.mid( 1 );
    } else if ( command.startsWith( '-' ) ) {
      op = Delete;
      command = command.mid( 1 );
    }
    if ( command.endsWith( AKONADI_PARAM_DOT_SILENT ) ) {
      command.chop( 7 );
      silent = true;
    }
//     akDebug() << "STORE: handling command: " << command;

    // handle commands that can be applied to more than one item
    if ( command == AKONADI_PARAM_FLAGS ) {
      bool flagsChanged = true;
      const QList<QByteArray> flags = m_streamParser->readParenthesizedList();
      if ( op == Replace ) {
        flagsChanged = replaceFlags( pimItems, flags );
      } else if ( op == Add ) {
        if ( !addFlags( pimItems, flags, flagsChanged ) ) {
          return failureResponse( "Unable to add item flags." );
        }
      } else if ( op == Delete ) {
        if ( !( flagsChanged = deleteFlags( pimItems, flags ) ) ) {
          return failureResponse( "Unable to remove item flags." );
        }
      }

      if ( flagsChanged && !changes.contains( AKONADI_PARAM_FLAGS ) ) {
        changes << AKONADI_PARAM_FLAGS;
      }
      continue;
    }

    // handle commands that can only be applied to one item
    if ( pimItems.size() > 1 ) {
      throw HandlerException( "This Modification can only be applied to a single item" );
    }
    PimItem &item = pimItems.first();
    if ( !item.isValid() ) {
      throw HandlerException( "Invalid item in query result!?" );
    }

    if ( command == AKONADI_PARAM_REMOTEID ) {
      const QString rid = m_streamParser->readUtf8String();
      if ( item.remoteId() != rid ) {
        if ( !connection()->isOwnerResource( item ) ) {
          throw HandlerException( "Only resources can modify remote identifiers" );
        }
        item.setRemoteId( rid );
        changes << AKONADI_PARAM_REMOTEID;
      }
    } else if ( command == AKONADI_PARAM_GID ) {
      const QString gid = m_streamParser->readUtf8String();
      if ( item.gid() != gid ) {
        item.setGid( gid );
      }
      changes << AKONADI_PARAM_GID;
    } else if ( command == AKONADI_PARAM_REMOTEREVISION ) {
      const QString remoteRevision = m_streamParser->readUtf8String();
      if ( item.remoteRevision() != remoteRevision ) {
        if ( !connection()->isOwnerResource( item ) ) {
          throw HandlerException( "Only resources can modify remote revisions" );
        }
        item.setRemoteRevision( remoteRevision );
        changes << AKONADI_PARAM_REMOTEREVISION;
      }
    } else if ( command == AKONADI_PARAM_UNDIRTY ) {
      m_streamParser->readString(); // read the 'false' string
      item.setDirty( false );
      undirty = true;
    } else if ( command == AKONADI_PARAM_INVALIDATECACHE ) {
      invalidateCache = true;
    } else if ( command == AKONADI_PARAM_SIZE ) {
      mSize = m_streamParser->readNumber();
      changes << AKONADI_PARAM_SIZE;
    } else if ( command == "PARTS" ) {
      const QList<QByteArray> parts = m_streamParser->readParenthesizedList();
      if ( op == Delete ) {
        if ( !store->removeItemParts( item, parts ) ) {
          return failureResponse( "Unable to remove item parts." );
        }
        changes += QSet<QByteArray>::fromList( parts );
      }
    } else if ( command == AKONADI_CMD_COLLECTION ) {
      throw HandlerException( "Item moving via STORE is deprecated, update your Akonadi client" );
    } else { // parts/attributes
      // obtain and configure the part object
      int partVersion = 0;
      QByteArray partName;
      ImapParser::splitVersionedKey( command, partName, partVersion );

      const PartType partType = PartTypeHelper::fromFqName( partName );

      SelectQueryBuilder<Part> qb;
      qb.addValueCondition( Part::pimItemIdColumn(), Query::Equals, item.id() );
      qb.addValueCondition( Part::partTypeIdColumn(), Query::Equals, partType.id() );
      if ( !qb.exec() ) {
        return failureResponse( "Unable to check item part existence" );
      }
      Part::List result = qb.result();
      Part part;
      if ( !result.isEmpty() ) {
        part = result.first();
      }
      part.setPartType( partType );
      part.setVersion( partVersion );
      part.setPimItemId( item.id() );

      QByteArray value;
      if ( m_streamParser->hasLiteral() ) {
        const qint64 dataSize = m_streamParser->remainingLiteralSize();
        if ( partName.startsWith( AKONADI_PARAM_PLD ) ) {
          partSizes += dataSize;
        }
        const bool storeInFile = dataSize > DbConfig::configuredDatabase()->sizeThreshold();
        //actual case when streaming storage is used: external payload is enabled, data is big enough in a literal
        if ( storeInFile ) {
          // use first part as value for the initial insert into / update to the database.
          // this will give us a proper filename to stream the rest of the parts contents into
          // NOTE: we have to set the correct size (== dataSize) directly
          value = m_streamParser->readLiteralPart();
         // akDebug() << Q_FUNC_INFO << "VALUE in STORE: " << value << value.size() << dataSize;

          if ( part.isValid() ) {
            PartHelper::update( &part, value, dataSize );
          } else {
//             akDebug() << "insert from Store::handleLine";
            part.setData( value );
            part.setDatasize( dataSize );
            if ( !PartHelper::insert( &part ) ) {
              return failureResponse( "Unable to add item part" );
            }
          }

          //the actual streaming code for the remaining parts:
          // reads from the parser, writes immediately to the file
          // ### move this entire block to part helper? should be useful for append as well
          const QString fileName = PartHelper::resolveAbsolutePath( part.data() );
          QFile file( fileName );
          if ( file.open( QIODevice::WriteOnly | QIODevice::Append ) ) {
            while ( !m_streamParser->atLiteralEnd() ) {
              value = m_streamParser->readLiteralPart();
              file.write( value ); // ### error handling?
            }
            file.close();
          } else {
            return failureResponse( "Unable to update item part" );
          }

          changes << partName;
          continue;
        } else { // not store in file
          //don't write in streaming way as the data goes to the database
          while ( !m_streamParser->atLiteralEnd() ) {
            value += m_streamParser->readLiteralPart();
          }
        }
      } else { //not a literal
        value = m_streamParser->readString();
        if ( partName.startsWith( AKONADI_PARAM_PLD ) ) {
          partSizes += value.size();
        }
      }

      // only relevant for non-literals or non-external literals
      const QByteArray origData = PartHelper::translateData( part );
      if ( origData != value ) {
        if ( part.isValid() ) {
          PartHelper::update( &part, value, value.size() );
        } else {
//           akDebug() << "insert from Store::handleLine: " << value.left(100);
          part.setData( value );
          part.setDatasize( value.size() );
          if ( !PartHelper::insert( &part ) ) {
            return failureResponse( "Unable to add item part" );
          }
        }
        changes << partName;
      }

    } // parts/attribute modification
  }

  QString datetime;
  if ( !changes.isEmpty() || invalidateCache || undirty ) {

    // update item size
    if ( pimItems.size() == 1 && ( mSize > 0 || partSizes > 0 ) ) {
      pimItems.first().setSize( qMax( mSize, partSizes ) );
    }

    const bool onlyRemoteIdChanged = ( changes.size() == 1 && changes.contains( AKONADI_PARAM_REMOTEID ) );
    const bool onlyRemoteRevisionChanged = ( changes.size() == 1 && changes.contains( AKONADI_PARAM_REMOTEREVISION ) );
    const bool onlyRemoteIdAndRevisionChanged = ( changes.size() == 2 && changes.contains( AKONADI_PARAM_REMOTEID )
                                                                     && changes.contains( AKONADI_PARAM_REMOTEREVISION ) );
    const bool onlyFlagsChanged = ( changes.size() == 1 && changes.contains( AKONADI_PARAM_FLAGS ) );
    const bool onlyGIDChanged = ( changes.size() == 1 && changes.contains( AKONADI_PARAM_GID ) );
    // If only the remote id and/or the remote revision changed, we don't have to increase the REV,
    // because these updates do not change the payload and can only be done by the owning resource -> no conflicts possible
    const bool revisionNeedsUpdate = ( !changes.isEmpty() && !onlyRemoteIdChanged && !onlyRemoteRevisionChanged && !onlyRemoteIdAndRevisionChanged && !onlyGIDChanged );

    // run update query and prepare change notifications
    for ( int i = 0; i < pimItems.count(); ++i ) {

      if ( revisionNeedsUpdate ) {
        pimItems[i].setRev( pimItems[i].rev() + 1 );
      }

      PimItem &item = pimItems[i];
      item.setDatetime( modificationtime );
      item.setAtime( modificationtime );
      if ( !connection()->isOwnerResource( item ) && payloadChanged( changes ) ) {
        item.setDirty( true );
      }
      if ( !item.update() ) {
        throw HandlerException( "Unable to write item changes into the database" );
      }

      if ( invalidateCache ) {
        if ( !store->invalidateItemCache( item ) ) {
          throw HandlerException( "Unable to invalidate item cache in the database" );
        }
      }

      // flags change notification went separatly during command parsing
      // GID-only changes are ignored to prevent resources from updating their storage when no actual change happened
      if ( !changes.isEmpty() && !onlyFlagsChanged && !onlyGIDChanged ) {
        store->notificationCollector()->itemChanged( item, changes );
      }

      if ( !silent ) {
        sendPimItemResponse( item );
      }
    }

    if ( !transaction.commit() ) {
      return failureResponse( "Cannot commit transaction." );
    }

    datetime = QLocale::c().toString( modificationtime, QLatin1String( "dd-MMM-yyyy hh:mm:ss +0000" ) );
  } else {
    datetime = QLocale::c().toString( pimItems.first().datetime(), QLatin1String( "dd-MMM-yyyy hh:mm:ss +0000" ) );
  }

  // TODO: When implementing support for modifying multiple items at once, the revisions of the items should be in the responses.
  // or only modified items should appear in the repsponse.
  Response response;
  response.setTag( tag() );
  response.setSuccess();
  response.setString( "DATETIME " + ImapParser::quote( datetime.toUtf8() ) + " STORE completed" );

  Q_EMIT responseAvailable( response );
  return true;
}

void Store::parseCommand()
{
  mScope.parseScope( m_streamParser );

  // parse the stuff before the modification list
  while ( !m_streamParser->hasList() ) {
    const QByteArray command = m_streamParser->readString();
    if ( command.isEmpty() ) { // ie. we are at command end
      throw HandlerException( "No modification list provided in STORE command" );
    } else if ( command == AKONADI_PARAM_REVISION ) {
      mPreviousRevision = m_streamParser->readNumber();
      mCheckRevision = true;
    } else if ( command == AKONADI_PARAM_SIZE ) {
      mSize = m_streamParser->readNumber();
    }
  }
}

void Store::sendPimItemResponse( const PimItem &pimItem )
{
  QList<QByteArray> attrs;
  attrs.push_back( "REV" );
  attrs.push_back( QByteArray::number( pimItem.rev() ) );

  QByteArray result;
  result += QByteArray::number( pimItem.id() );
  result += " FETCH (";
  result += ImapParser::join( attrs, " " );
  result += ')';

  Response response;
  response.setUntagged();
  response.setString( result );
  Q_EMIT responseAvailable( response );
}
