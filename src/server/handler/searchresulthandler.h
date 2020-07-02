/*
 * SPDX-FileCopyrightText: 2013 Daniel Vrátil <dvratil@redhat.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#ifndef AKONADI_SEARCHRESULTHANDLER_H_
#define AKONADI_SEARCHRESULTHANDLER_H_

#include "handler.h"

namespace Akonadi
{
namespace Server
{

/**
  @ingroup akonadi_server_handler

  Handler for the search_result command
*/
class SearchResultHandler: public Handler
{
public:
    SearchResultHandler(AkonadiServer &akonadi);
    ~SearchResultHandler() override = default;

    bool parseStream() override;

private:
    bool fail(const QByteArray &searchId, const QString &error);
};

} // namespace Server
} // namespace Akonadi

#endif // AKONADI_SEARCHRESULTHANDLER_H_
