/*
    Copyright (c) 2009 Volker Krause <vkrause@kde.org>

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

#ifndef AKONADI_MOVE_H
#define AKONADI_MOVE_H

#include "handler.h"

namespace Akonadi {
namespace Server {

/**
  @ingroup akonadi_server_handler

  Handler for the item move command.

  <h4>Semantics</h4>
  Moves the selected items. Item selection can happen within the usual three scopes:
  - based on a uid set relative to the currently selected collection
  - based on a global uid set (UID)
  - based on a list of remote identifiers within the currently selected collection (RID)

  Destination is a collection id.
*/
class  Move : public Handler
{
    Q_OBJECT

public:
    bool parseStream() Q_DECL_OVERRIDE;

private Q_SLOTS:
    void itemsRetrieved(const QList<qint64> &ids);

private:
    Collection mDestination;
};

} // namespace Server
} // namespace Akonadi

#endif
