/*
    Copyright (c) 2014 Daniel Vrátil <dvratil@redhat.com>

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

#ifndef AKONADI_SEARCHQUERY_H
#define AKONADI_SEARCHQUERY_H

#include <QSharedPointer>

#include "akonadicore_export.h"

namespace Akonadi
{

/**
 * Search term represents the actual condition within query.
 *
 * SearchTerm can either have multiple subterms, or can be so-called endterm, when
 * there are no more subterms, but instead the actual condition is specified, that
 * is have key, value and relation between them.
 *
 * @since 4.13
 */
class AKONADICORE_EXPORT SearchTerm
{
public:
    enum Relation {
        RelAnd,
        RelOr
    };

    enum Condition {
        CondEqual,
        CondGreaterThan,
        CondGreaterOrEqual,
        CondLessThan,
        CondLessOrEqual,
        CondContains
    };

    enum SearchField {
        Unknown = 0,            ///< Invalid search field
        Collection = 1          ///< Match entities from given Collection (expects Collection ID (qint64))
    };

    /**
     * Constructs a term where all subterms will be in given relation
     */
    explicit SearchTerm(SearchTerm::Relation relation = SearchTerm::RelAnd);

    /**
     * Constructs an end term
     */
    SearchTerm(const QString &key, const QVariant &value, SearchTerm::Condition condition = SearchTerm::CondEqual);
    SearchTerm(SearchField field, const QVariant &value, SearchTerm::Condition condition = SearchTerm::CondEqual);
    SearchTerm(const SearchTerm &other);
    ~SearchTerm();

    SearchTerm &operator=(const SearchTerm &other);
    Q_REQUIRED_RESULT bool operator==(const SearchTerm &other) const;

    Q_REQUIRED_RESULT bool isNull() const;

    /**
     * Returns key of this end term.
     */
    Q_REQUIRED_RESULT QString key() const;

    /**
     * Returns value of this end term.
     */
    Q_REQUIRED_RESULT QVariant value() const;

    /**
     * Returns relation between key and value.
     */
    Q_REQUIRED_RESULT SearchTerm::Condition condition() const;

    /**
     * Adds a new subterm to this term.
     *
     * Subterms will be in relation as specified in SearchTerm constructor.
     *
     * If there are subterms in a term, key, value and condition are ignored.
     */
    void addSubTerm(const SearchTerm &term);

    /**
     * Returns all subterms, or an empty list if this is an end term.
     */
    Q_REQUIRED_RESULT QList<SearchTerm> subTerms() const;

    /**
     * Returns relation in which all subterms are.
     */
    Q_REQUIRED_RESULT SearchTerm::Relation relation() const;

    /**
     * Sets whether the entire term is negated.
     */
    void setIsNegated(bool negated);

    /**
     * Returns whether the entire term is negated.
     */
    Q_REQUIRED_RESULT bool isNegated() const;

    static QString toKey(SearchField field);
    static SearchField fromKey(const QString &key);

    static SearchTerm inCollection(qint64 collection);

private:
    class Private;
    QSharedDataPointer<Private> d;
};

/**
 * @brief A query that can be passed to ItemSearchJob or others.
 *
 * @since 4.13
 */
class AKONADICORE_EXPORT SearchQuery
{
public:
    /**
     * Constructs query where all added terms will be in given relation
     */
    explicit SearchQuery(SearchTerm::Relation rel = SearchTerm::RelAnd);

    ~SearchQuery();
    SearchQuery(const SearchQuery &other);
    SearchQuery &operator=(const SearchQuery &other);
    bool operator==(const SearchQuery &other) const;

    bool isNull() const;

    /**
     * Adds a new term.
     */
    void addTerm(const QString &key, const QVariant &value, SearchTerm::Condition condition = SearchTerm::CondEqual);

    /**
     * Adds a new term with subterms
     */
    void addTerm(const SearchTerm &term);

    /**
     * Sets the root term
     */
    void setTerm(const SearchTerm &term);

    /**
     * Returns the root term.
     */
    SearchTerm term() const;

    /**
     * Sets the maximum number of results.
     *
     * Note that this limit is only evaluated per search backend,
     * so the total number of results retrieved may be larger.
     */
    void setLimit(int limit);

    /**
     * Returns the maximum number of results.
     *
     * The default value is -1, indicating no limit.
     */
    int limit() const;

    QByteArray toJSON() const;
    static SearchQuery fromJSON(const QByteArray &json);

private:
    class Private;
    QSharedDataPointer<Private> d;

};

/**
 * A search term for an email field.
 *
 * This class can be used to create queries that akonadi email search backends understand.
 *
 * @since 4.13
 */
class AKONADICORE_EXPORT EmailSearchTerm : public SearchTerm
{
public:

    /**
     * All fields expect a search string unless noted otherwise.
     */
    enum EmailSearchField {
        Subject = 100,      ///< Search in the subject field (expects QString)
        Body,               ///< Search in the email body (expects QString)
        Headers,            ///< @deprecated Will be removed
        HeaderFrom,         ///< Search in the From header (expects QString)
        HeaderTo,           ///< Search in the To header (expects QString)
        HeaderCC,           ///< Search in the CC header (expects QString)
        HeaderBCC,          ///< Search in the BCC header (expects QString)
        HeaderReplyTo,      ///< Search in the ReplyTo header (expects QString)
        HeaderOrganization, ///< Search in the Organization header (expects QString)
        HeaderListId,       ///< Search in the ListId header (expects QString)
        HeaderResentFrom,   ///< @deprecated Will be removed
        HeaderXLoop,        ///< @deprecated Will be removed
        HeaderXMailingList, ///< @deprecated Will be removed
        HeaderXSpamFlag,    ///< @deprecated Will be removed
        HeaderDate,         ///< Match by the Date header (expects QDateTime)
        HeaderOnlyDate,     ///< Match by the Date header, but only by date (expects QDate)
        MessageStatus,      ///< Match by message flags (expects Akonadi::MessageFlag), boolean filter.
        ByteSize,           ///< Match by message size (expects integer)
        AttachmentName,     ///< Search in attachment names (expects QString)
        Attachment,         ///< Search in bodies of plaintext attachments (expects QString)
        Message,            ///< Search in all the QString-based fields listed above
        MessageTag          ///< @deprecated Will be removed
    };

    /**
     * Constructs an email end term
     */
    EmailSearchTerm(EmailSearchField field, const QVariant &value, SearchTerm::Condition condition = SearchTerm::CondEqual);

    /**
     * Translates field to key
     */
    static QString toKey(EmailSearchField);

    /**
     * Translates key to field
     */
    static EmailSearchField fromKey(const QString &key);

    static SearchTerm from(const QStringList &from, SearchTerm::Relation rel = SearchTerm::RelOr);
    static SearchTerm to(const QStringList &to, SearchTerm::Relation rel = SearchTerm::RelOr);
    static SearchTerm cc(const QStringList &cc, SearchTerm::Relation rel = SearchTerm::RelOr);
    static SearchTerm bcc(const QStringList &bcc, SearchTerm::Relation rel = SearchTerm::RelOr);
    static SearchTerm involves(const QStringList &involves, SearchTerm::Relation rel = SearchTerm::RelOr);
    static SearchTerm isRead(bool isRead = true);
    static SearchTerm isImportant(bool isImportant = true);
    static SearchTerm hasAttachment(bool hasAttachment = true);
    static SearchTerm matches(const QString &match);
    static SearchTerm subjectMatches(const QString &subject);
    static SearchTerm bodyMatches(const QString &bodyMatches);
};

/**
 * A search term for a contact field.
 *
 * This class can be used to create queries that akonadi contact search backends understand.
 *
 * @since 4.13
 */
class AKONADICORE_EXPORT ContactSearchTerm : public SearchTerm
{
public:
    enum ContactSearchField {
        Name = 200,         ///< Search by full name (expects QString)
        Email,              ///< Search by email address (expects QString)
        Nickname,           ///< Search by nickname (expects QString)
        Uid,                ///< Search by vCard UID (expects QString)
        Birthday,           ///< Match by birthday (expects QDate)
        Anniversary,        ///< Match by anniversary (expects QDate)
        All                 ///< Matches all contacts regardless of value
    };

    ContactSearchTerm(ContactSearchField field, const QVariant &value, SearchTerm::Condition condition = SearchTerm::CondEqual);

    /**
     * Translates field to key
     */
    static QString toKey(ContactSearchField);

    /**
     * Translates key to field
     */
    static ContactSearchField fromKey(const QString &key);
};

/**
 * A search term for a incidence field.
 *
 * This class can be used to create queries that akonadi incidence search backends understand.
 *
 * @since 5.0
 */
class AKONADICORE_EXPORT IncidenceSearchTerm : public SearchTerm
{
public:
    enum IncidenceSearchField {
        All = 300,          ///< Matches all events regardless of value
        PartStatus,         ///< Match events based on participant status
        Organizer,          ///< Search by incidence organizer name or email
        Summary,            ///< Search by incidence summary
        Location            ///< Search by incidence location
    };

    IncidenceSearchTerm(IncidenceSearchField field, const QVariant &value, SearchTerm::Condition condition = SearchTerm::CondEqual);

    /**
     * Translates field to key
     */
    static QString toKey(IncidenceSearchField);

    /**
     * Translates key to field
     */
    static IncidenceSearchField fromKey(const QString &key);
};

/**
 * A search term for Collections.
 */
class AKONADICORE_EXPORT CollectionSearchTerm : public SearchTerm
{
public:
    enum CollectionSearchField {
        Name = 400,         ///< Search by collection name
        Namespace,          ///< Search by collection namespace
        Identification,     ///< Search by additional collection identification
        MimeType            ///< Search by collection mimetype
    };

    CollectionSearchTerm(CollectionSearchField field, const QVariant &value,
                         SearchTerm::Condition condition = SearchTerm::CondEqual);

    static QString toKey(CollectionSearchField field);
    static CollectionSearchField fromKey(const QString &key);

    static SearchTerm nameMatches(const QString &name);
    static SearchTerm hasNamespaces(const QStringList &namespaces);
    static SearchTerm hasMimeTypes(const QStringList &mimeTypes);
};

}

#endif // AKONADI_SEARCHQUERY_H
