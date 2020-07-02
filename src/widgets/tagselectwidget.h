/*
  SPDX-FileCopyrightText: 2015-2020 Laurent Montel <montel@kde.org>

  SPDX-License-Identifier: LGPL-2.0-or-later
  */

#ifndef TAGSELECTWIDGET_H
#define TAGSELECTWIDGET_H

#include <QWidget>
#include "tag.h"
#include "akonadiwidgets_export.h"

namespace Akonadi
{
/**
 * A widget that offers facilities to add/remove tags and provides a way to select tags.
 *
 * @since 4.14.6
 */

class AKONADIWIDGETS_EXPORT TagSelectWidget : public QWidget
{
    Q_OBJECT
public:
    explicit TagSelectWidget(QWidget *parent = nullptr);
    ~TagSelectWidget();

    void setSelection(const Akonadi::Tag::List &tags);
    Q_REQUIRED_RESULT Akonadi::Tag::List selection() const;

    /**
     * @brief tagToStringList
     * @return QStringList from selected tag (List of Url)
     */
    Q_REQUIRED_RESULT QStringList tagToStringList() const;
    /**
     * @brief setSelectionFromStringList, convert a QStringList to Tag (converted from url)
     */
    void setSelectionFromStringList(const QStringList &lst);
private:
    //@cond PRIVATE
    class Private;
    QScopedPointer<Private> const d;
};
}

#endif // TAGSELECTWIDGET_H
