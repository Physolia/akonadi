/*
    Copyright (c) 2010 Tobias Koenig <tokoe@kde.org>

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

#include "dbconfigpostgresql.h"
#include "utils.h"

#include <libs/xdgbasedirs_p.h>
#include <akdebug.h>
#include <akstandarddirs.h>

#include <QtCore/QDir>
#include <QtCore/QProcess>
#include <QtSql/QSqlDriver>
#include <QtSql/QSqlError>
#include <QtSql/QSqlQuery>

#include <unistd.h>

using namespace Akonadi;

DbConfigPostgresql::DbConfigPostgresql()
{
}

QString DbConfigPostgresql::driverName() const
{
  return QLatin1String( "QPSQL" );
}

QString DbConfigPostgresql::databaseName() const
{
  return mDatabaseName;
}

bool DbConfigPostgresql::init( QSettings &settings )
{
  // determine default settings depending on the driver
  QString defaultHostName;
  QString defaultOptions;
  QString defaultServerPath;
  QString defaultInitDbPath;
  QString defaultPgData;

#ifndef Q_WS_WIN // We assume that PostgreSQL is running as service on Windows
  const bool defaultInternalServer = true;
#else
  const bool defaultInternalServer = false;
#endif

  mInternalServer = settings.value( QLatin1String( "QPSQL/StartServer" ), defaultInternalServer ).toBool();
  if ( mInternalServer ) {
    QStringList postgresSearchPath;

#ifdef POSTGRES_PATH
    const QString dir( QLatin1String( POSTGRES_PATH ) );
    if ( QDir( dir ).exists() ) {
      postgresSearchPath << QLatin1String( POSTGRES_PATH );
    }
#endif
    postgresSearchPath << QLatin1String( "/usr/sbin" )
                       << QLatin1String( "/usr/local/sbin" )
                       << QLatin1String( "/usr/lib/postgresql/8.4/bin" )
                       << QLatin1String( "/usr/lib/postgresql/9.0/bin" )
                       << QLatin1String( "/usr/lib/postgresql/9.1/bin" )
                       << QLatin1String( "/usr/lib/postgresql/9.2/bin" )
                       << QLatin1String( "/usr/lib/postgresql/9.3/bin" );

    defaultServerPath = XdgBaseDirs::findExecutableFile( QLatin1String( "pg_ctl" ), postgresSearchPath );
    defaultInitDbPath = XdgBaseDirs::findExecutableFile( QLatin1String( "initdb" ), postgresSearchPath );
    defaultHostName = Utils::preferredSocketDirectory( AkStandardDirs::saveDir( "data", QLatin1String( "db_misc" ) ) );
    defaultPgData = AkStandardDirs::saveDir( "data", QLatin1String( "db_data" ) );
  }

  // read settings for current driver
  settings.beginGroup( driverName() );
  mDatabaseName = settings.value( QLatin1String( "Name" ), defaultDatabaseName() ).toString();
  mHostName = settings.value( QLatin1String( "Host" ), defaultHostName ).toString();
  mUserName = settings.value( QLatin1String( "User" ) ).toString();
  mPassword = settings.value( QLatin1String( "Password" ) ).toString();
  mConnectionOptions = settings.value( QLatin1String( "Options" ), defaultOptions ).toString();
  mServerPath = settings.value( QLatin1String( "ServerPath" ), defaultServerPath ).toString();
  mInitDbPath = settings.value( QLatin1String( "InitDbPath" ), defaultInitDbPath ).toString();
  mPgData = settings.value( QLatin1String( "PgData" ), defaultPgData ).toString();
  settings.endGroup();

  // store back the default values
  settings.beginGroup( driverName() );
  settings.setValue( QLatin1String( "Name" ), mDatabaseName );
  settings.setValue( QLatin1String( "Host" ), mHostName );
  settings.setValue( QLatin1String( "Options" ), mConnectionOptions );
  if ( !mServerPath.isEmpty() ) {
    settings.setValue( QLatin1String( "ServerPath" ), mServerPath );
  }
  if ( !mInitDbPath.isEmpty() ) {
    settings.setValue( QLatin1String( "InitDbPath" ), mInitDbPath );
  }
  settings.setValue( QLatin1String( "StartServer" ), mInternalServer );
  settings.endGroup();
  settings.sync();

  return true;
}

void DbConfigPostgresql::apply( QSqlDatabase &database )
{
  if ( !mDatabaseName.isEmpty() ) {
    database.setDatabaseName( mDatabaseName );
  }
  if ( !mHostName.isEmpty() ) {
    database.setHostName( mHostName );
  }
  if ( !mUserName.isEmpty() ) {
    database.setUserName( mUserName );
  }
  if ( !mPassword.isEmpty() ) {
    database.setPassword( mPassword );
  }

  database.setConnectOptions( mConnectionOptions );

  // can we check that during init() already?
  Q_ASSERT( database.driver()->hasFeature( QSqlDriver::LastInsertId ) );
}

bool DbConfigPostgresql::useInternalServer() const
{
  return mInternalServer;
}

void DbConfigPostgresql::startInternalServer()
{
  // We defined the mHostName to the socket directory, during init
  const QString socketDir = mHostName;

  if ( !QFile::exists( QString::fromLatin1( "%1/PG_VERSION" ).arg( mPgData ) ) ) {
    // postgres data directory not initialized yet, so call initdb on it

    // call 'initdb --pgdata=/home/user/.local/share/akonadi/data_db'
    const QString command = QString::fromLatin1( "%1" ).arg( mInitDbPath );
    QStringList arguments;
    arguments << QString::fromLatin1( "--pgdata=%2" ).arg( mPgData )
              // TODO check locale
              << QString::fromLatin1( "--locale=en_US.UTF-8" );
    QProcess::execute( command, arguments );
  }

  // synthesize the postgres command
  QStringList arguments;
  arguments << QString::fromLatin1( "start")
            << QString::fromLatin1( "-w" )
            << QString::fromLatin1( "--timeout=10" ) // default is 60 seconds.
            << QString::fromLatin1( "--pgdata=%1" ).arg( mPgData )
            // set the directory for unix domain socket communication
            // -o will pass the switch to postgres
            << QString::fromLatin1( "-o \"-k %1\"" ).arg( socketDir );

  QProcess pgCtl;
  pgCtl.start( mServerPath, arguments );
  if ( !pgCtl.waitForStarted() ) {
    akError() << "Could not start database server!";
    akError() << "executable:" << mServerPath;
    akError() << "arguments:" << arguments;
    akFatal() << "process error:" << pgCtl.errorString();
  }

  const QLatin1String initCon( "initConnection" );
  {
    QSqlDatabase db = QSqlDatabase::addDatabase( QLatin1String( "QPSQL" ), initCon );
    apply( db );

    // use the default database that is always available
    db.setDatabaseName( QLatin1String( "postgres" ) );

    if ( !db.isValid() ) {
      akFatal() << "Invalid database object during database server startup";
    }

    bool opened = false;
    for ( int i = 0; i < 120; ++i ) {
      opened = db.open();
      if ( opened ) {
        break;
      }

      if ( pgCtl.waitForFinished( 500 ) ) {
        akError() << "Database process exited unexpectedly during initial connection!";
        akError() << "executable:" << mServerPath;
        akError() << "arguments:" << arguments;
        akError() << "stdout:" << pgCtl.readAllStandardOutput();
        akError() << "stderr:" << pgCtl.readAllStandardError();
        akError() << "exit code:" << pgCtl.exitCode();
        akFatal() << "process error:" << pgCtl.errorString();
      }
    }

    if ( opened ) {
      {
        QSqlQuery query( db );

        // check if the 'akonadi' database already exists
        query.exec( QString::fromLatin1( "SELECT 1 FROM pg_catalog.pg_database WHERE datname = '%1'" ).arg( mDatabaseName ) );

        // if not, create it
        if ( !query.first() ) {
          if ( !query.exec( QString::fromLatin1( "CREATE DATABASE %1" ).arg( mDatabaseName ) ) ) {
            akError() << "Failed to create database";
            akError() << "Query error:" << query.lastError().text();
            akFatal() << "Database error:" << db.lastError().text();
          }
        }
      } // make sure query is destroyed before we close the db
      db.close();
    }
  }

  QSqlDatabase::removeDatabase( initCon );
}

void DbConfigPostgresql::stopInternalServer()
{
  if ( !checkServerIsRunning() ) {
    akDebug() << "Database is no longer running";
    return;
  }

  const QString command = QString::fromLatin1( "%1" ).arg( mServerPath );

  // first, try a FAST shutdown
  QStringList arguments;
  arguments << QString::fromLatin1( "stop" )
            << QString::fromLatin1( "--pgdata=%1" ).arg( mPgData )
            << QString::fromLatin1( "--mode=fast" );
  QProcess::execute( command, arguments );
  sleep( 3 );
  if ( !checkServerIsRunning() ) {
    return;
  }

  // second, try an IMMEDIATE shutdown
  arguments.clear();
  arguments << QString::fromLatin1( "stop" )
            << QString::fromLatin1( "--pgdata=%1" ).arg( mPgData )
            << QString::fromLatin1( "--mode=immediate" );
  QProcess::execute( command, arguments );
  sleep( 3 );
  if ( !checkServerIsRunning() ) {
    return;
  }

  // third, pg_ctl couldn't terminate all the postgres processes, we have to
  // kill the master one. We don't want to do that, but we've passed the last
  // call. pg_ctl is used to send the kill signal (safe when kill is not
  // supported by OS)
  const QString pidFileName = QString::fromLatin1( "%1/postmaster.pid" ).arg( mPgData );
  QFile pidFile( pidFileName );
  if ( pidFile.open( QIODevice::ReadOnly ) ) {
    QString postmasterPid = QString::fromUtf8( pidFile.readLine( 0 ).trimmed() );
    akError() << "The postmaster is still running. Killing it.";

    arguments.clear();
    arguments << QString::fromLatin1( "kill" )
              << QString::fromLatin1( "ABRT" )
              << QString::fromLatin1( "%1" ).arg( postmasterPid );
    QProcess::execute( command, arguments );
  }
}

bool DbConfigPostgresql::checkServerIsRunning()
{
  const QString command = QString::fromLatin1( "%1" ).arg( mServerPath );
  QStringList arguments;
  arguments << QString::fromLatin1( "status" )
            << QString::fromLatin1( "--pgdata=%1" ).arg( mPgData );

  QProcess pgCtl;
  pgCtl.start( command, arguments, QIODevice::ReadOnly );
  if ( !pgCtl.waitForFinished( 3000 ) ) {
    // Error?
    return false;
  }

  const QByteArray output = pgCtl.readAllStandardOutput();
  return output.startsWith( "pg_ctl: server is running" );
}
