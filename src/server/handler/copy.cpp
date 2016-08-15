/*
    Copyright (c) 2008 Volker Krause <vkrause@kde.org>

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

#include "copy.h"

#include "connection.h"
#include "handlerhelper.h"
#include "cachecleaner.h"
#include "storage/datastore.h"
#include "storage/itemqueryhelper.h"
#include "storage/itemretriever.h"
#include "storage/selectquerybuilder.h"
#include "storage/transaction.h"
#include "storage/parthelper.h"

#include <private/imapset_p.h>

using namespace Akonadi;
using namespace Akonadi::Server;

bool Copy::copyItem(const PimItem &item, const Collection &target)
{
    PimItem newItem = item;
    newItem.setId(-1);
    newItem.setRev(0);
    newItem.setDatetime(QDateTime::currentDateTime());
    newItem.setAtime(QDateTime::currentDateTime());
    newItem.setRemoteId(QString());
    newItem.setRemoteRevision(QString());
    newItem.setCollectionId(target.id());
    Part::List parts;
    parts.reserve(item.parts().count());
    Q_FOREACH (const Part &part, item.parts()) {
        Part newPart(part);
        newPart.setData(PartHelper::translateData(newPart.data(), part.external()));
        newPart.setPimItemId(-1);
        parts << newPart;
    }

    DataStore *store = connection()->storageBackend();
    if (!store->appendPimItem(parts, item.mimeType(), target, QDateTime::currentDateTime(), QString(), QString(), item.gid(), newItem)) {
        return false;
    }
    Q_FOREACH (const Flag &flag, item.flags()) {
        if (!newItem.addFlag(flag)) {
            return false;
        }
    }
    return true;
}

void Copy::itemsRetrieved(const QList<qint64>& ids)
{
    SelectQueryBuilder<PimItem> qb;
    ItemQueryHelper::itemSetToQuery(ImapSet(ids), qb);
    if (!qb.exec()) {
        failureResponse("Unable to retrieve items");
        return;
    }
    PimItem::List items = qb.result();
    qb.query().finish();

    DataStore *store = connection()->storageBackend();
    Transaction transaction(store);

    Q_FOREACH (const PimItem &item, items) {
        if (!copyItem(item, mTargetCollection)) {
            failureResponse("Unable to copy item");
            return;
        }
    }

    if (!transaction.commit()) {
        failureResponse("Cannot commit transaction.");
        return;
    }
}


bool Copy::parseStream()
{
    Protocol::CopyItemsCommand cmd(m_command);

    if (!checkScopeConstraints(cmd.items(), Scope::Uid)) {
        return failureResponse("Only UID copy is allowed");
    }

    if (cmd.items().isEmpty()) {
        return failureResponse("No items specified");
    }

    mTargetCollection = HandlerHelper::collectionFromScope(cmd.destination(), connection());
    if (!mTargetCollection.isValid()) {
        return failureResponse("No valid target specified");
    }
    if (mTargetCollection.isVirtual()) {
        return failureResponse("Copying items into virtual collections is not allowed");
    }

    CacheCleanerInhibitor inhibitor;

    ItemRetriever retriever(connection());
    retriever.setItemSet(cmd.items().uidSet());
    retriever.setRetrieveFullPayload(true);
    connect(&retriever, &ItemRetriever::itemsRetrieved,
            this, &Copy::itemsRetrieved);
    if (!retriever.exec()) {
        return failureResponse(retriever.lastError());
    }

    return successResponse<Protocol::CopyItemsResponse>();
    return true;
}
