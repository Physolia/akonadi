/*
    SPDX-FileCopyrightText: 2009 Volker Krause <vkrause@kde.org>

    SPDX-License-Identifier: LGPL-2.0-or-later
*/

#ifndef AKONADI_XMLWRITEJOB_H
#define AKONADI_XMLWRITEJOB_H

#include "akonadi-xml_export.h"
#include "job.h"
#include "collection.h"
namespace Akonadi
{

class Collection;
class XmlWriteJobPrivate;

/**
  Serializes a given Akonadi collection into a XML file.
*/
class AKONADI_XML_EXPORT XmlWriteJob : public Job
{
    Q_OBJECT
public:
    XmlWriteJob(const Collection &root, const QString &fileName, QObject *parent = nullptr);
    XmlWriteJob(const Collection::List &roots, const QString &fileName, QObject *parent = nullptr);
    ~XmlWriteJob() override;

protected:
    /* reimpl. */ void doStart() override;

private:
    friend class XmlWriteJobPrivate;
    XmlWriteJobPrivate *const d;
    void done();
};

}

#endif
