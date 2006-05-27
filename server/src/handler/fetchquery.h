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

#ifndef AKONADIFETCHQUERY_H
#define AKONADIFETCHQUERY_H

#include <QByteArray>
#include <QList>

namespace Akonadi {

/**
 * An tool class which does the parsing of a fetch request for us.
 */
class FetchQuery
{
  public:

    enum Type
    {
      AllType,
      FullType,
      FastType,
      AttributeType,
      AttributeListType
    };

    class Attribute
    {
      public:
        enum Type
        {
          Envelope,
          Flags,
          InternalDate,
          RFC822,
          RFC822_Header,
          RFC822_Size,
          RFC822_Text,
          Body,
          Body_Structure,
          Uid
        };

        bool parse( const QByteArray &attribute );
        void dump();

        Type mType;
    };

    bool parse( const QByteArray &query );
    QList<QByteArray> normalizedSequences( const QList<QByteArray> &sequences );
    bool hasAttributeType( Attribute::Type type ) const;

    void dump();

    QList<QByteArray> mSequences;
    QList<Attribute> mAttributes;
    Type mType;
    bool mIsUidFetch;
};

}

#endif
