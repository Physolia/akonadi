/*
    Copyright (c) 2011 Volker Krause <vkrause@kde.org>

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

#include "akstandarddirs.h"
#include "akapplication.h"

#include <libs/xdgbasedirs_p.h>

#include <QFile>

using namespace Akonadi;

// FIXME: this is largely copied from XdgBaseDirs
QString AkStandardDirs::configFile(const QString& configFile, Akonadi::XdgBaseDirs::FileAccessMode openMode)
{
  QString akonadiDir = QLatin1String( "akonadi" );
  if ( !AkApplication::instanceIdentifier().isEmpty() )
    akonadiDir += QLatin1Char('/') + AkApplication::instanceIdentifier();

  const QString savePath = XdgBaseDirs::saveDir( "config", akonadiDir ) + QLatin1Char( '/' ) + configFile;

  if ( openMode == XdgBaseDirs::WriteOnly )
    return savePath;

  const QString path = XdgBaseDirs::findResourceFile( "config", akonadiDir + QLatin1Char( '/' ) + configFile );

  if ( path.isEmpty() ) {
    return savePath;
  } else if ( openMode == XdgBaseDirs::ReadOnly || path == savePath ) {
    return path;
  }

  // file found in system paths and mode is ReadWrite, thus
  // we copy to the home path location and return this path
  QFile systemFile( path );

  systemFile.copy( savePath );

  return savePath;
}

QString AkStandardDirs::serverConfigFile(XdgBaseDirs::FileAccessMode openMode)
{
  return configFile( QLatin1String("akonadiserverrc"), openMode );
}

QString AkStandardDirs::connectionConfigFile(XdgBaseDirs::FileAccessMode openMode)
{
  return configFile( QLatin1String("akonadiconnectionrc"), openMode );
}

QString AkStandardDirs::agentConfigFile(XdgBaseDirs::FileAccessMode openMode)
{
  return configFile( QLatin1String("agentsrc"), openMode );
}

