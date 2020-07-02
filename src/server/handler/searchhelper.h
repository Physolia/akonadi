/***************************************************************************
 *   SPDX-FileCopyrightText: 2006 Tobias Koenig <tokoe@kde.org>            *
 *                                                                         *
 *   SPDX-License-Identifier: LGPL-2.0-or-later                            *
 ***************************************************************************/

#ifndef AKONADISEARCHHELPER_H
#define AKONADISEARCHHELPER_H

#include <QVector>
#include <QStringList>

namespace Akonadi
{
namespace Server
{

class SearchHelper
{
public:
    static QVector<qint64> matchSubcollectionsByMimeType(const QVector<qint64> &ancestors, const QStringList &mimeTypes);
};

} // namespace Server
} // namespace Akonadi

#endif
