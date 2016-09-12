/*
    Copyright (c) 2014 Christian Mollekopf <mollekopf@kolabsys.com>

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

#ifndef AKONADI_RELATIONDELETEJOB_H
#define AKONADI_RELATIONDELETEJOB_H

#include "job.h"

namespace Akonadi
{

class Relation;
class RelationDeleteJobPrivate;

/**
 * @short Job that deletes a relation in the Akonadi storage.
 * @since 4.15
 */
class AKONADICORE_EXPORT RelationDeleteJob : public Job
{
    Q_OBJECT

public:
    /**
     * Creates a new relation delete job.
     *
     * @param relation The relation to delete.
     * @param parent The parent object.
     */
    explicit RelationDeleteJob(const Relation &relation, QObject *parent = nullptr);

    /**
     * Returns the relation.
     */
    Relation relation() const;

protected:
    void doStart() Q_DECL_OVERRIDE;
    bool doHandleResponse(qint64 tag, const Protocol::CommandPtr &response) Q_DECL_OVERRIDE;

private:
    Q_DECLARE_PRIVATE(RelationDeleteJob)
};

}

#endif
