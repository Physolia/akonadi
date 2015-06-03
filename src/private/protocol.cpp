/*
 *  Copyright (c) 2015 Daniel Vrátil <dvratil@redhat.com>
 *
 *  This library is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU Library General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  This library is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Library General Public
 *  License for more details.
 *
 *  You should have received a copy of the GNU Library General Public License
 *  along with this library; see the file COPYING.LIB.  If not, write to the
 *  Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 *  02110-1301, USA.
 */

#include "protocol_p.h"
#include "scope_p.h"
#include "imapset_p.h"

#include <type_traits>

#include <QDataStream>
#include <QGlobalStatic>
#include <QHash>
#include <QMap>
#include <QDateTime>

using namespace Akonadi;
using namespace Akonadi::Protocol;

#define AKONADI_DECLARE_PRIVATE(Class) \
inline Class##Private* Class::d_func() {\
    return reinterpret_cast<Class##Private*>(d_ptr.data()); \
} \
inline const Class##Private* Class::d_func() const {\
    return reinterpret_cast<const Class##Private*>(d_ptr.constData()); \
}


// Generic code for effective streaming of enums
template<typename T>
typename std::enable_if<std::is_enum<T>::value, QDataStream>::type
&operator<<(QDataStream &stream, T val)
{
    return stream << static_cast<typename std::underlying_type<T>::type>(val);
}

template<typename T>
typename std::enable_if<std::is_enum<T>::value, QDataStream>::type
&operator>>(QDataStream &stream, T &val)
{
    typename std::underlying_type<T>::type tmp;
    stream >> tmp;
    val = static_cast<T>(tmp);
    return stream;
}

template<typename T>
typename std::enable_if<std::is_enum<T>::value, QDataStream>::type
&operator>>(QDataStream &stream, QFlags<T> &flags)
{
    typename std::underlying_type<T>::type t;
    stream >> t;
    flags = QFlags<T>(t);
    return stream;
}

QDataStream &operator>>(QDataStream &stream, Akonadi::Protocol::Command::Type &type)
{
    qint8 t;
    stream >> t;
    type = static_cast<Akonadi::Protocol::Command::Type>(t);
    return stream;
}

QDataStream &operator<<(QDataStream &stream, Akonadi::Protocol::Command::Type type)
{
    return stream << static_cast<qint8>(type);
}


namespace Akonadi
{
namespace Protocol
{

class FactoryPrivate
{
public:
    typedef Command (*CommandFactoryFunc)();
    typedef Response (*ResponseFactoryFunc)();

    FactoryPrivate()
    {
        // Session management
        registerType<Command::Hello, HelloResponse, HelloResponse>();
        registerType<Command::Login, LoginCommand, LoginResponse>();
        registerType<Command::Logout, LogoutCommand, LogoutResponse>();

        // Transactions
        registerType<Command::Transaction, TransactionCommand, TransactionResponse>();

        // Items
        registerType<Command::CreateItem, CreateItemCommand, CreateItemResponse>();
        registerType<Command::CopyItems, CopyItemsCommand, CopyItemsResponse>();
        registerType<Command::DeleteItems, DeleteItemsCommand, DeleteItemsResponse>();
        registerType<Command::FetchItems, FetchItemsCommand, FetchItemsResponse>();
        registerType<Command::LinkItems, LinkItemsCommand, LinkItemsResponse>();
        registerType<Command::ModifyItems, ModifyItemsCommand, ModifyItemsResponse>();
        registerType<Command::MoveItems, MoveItemsCommand, MoveItemsResponse>();

        // Collections
        registerType<Command::CreateCollection, CreateCollectionCommand, CreateCollectionResponse>();
        registerType<Command::CopyCollection, CopyCollectionCommand, CopyCollectionResponse>();
        registerType<Command::DeleteCollection, DeleteCollectionCommand, DeleteCollectionResponse>();
        registerType<Command::FetchCollections, FetchCollectionsCommand, FetchCollectionsResponse>();
        registerType<Command::FetchCollectionStats, FetchCollectionStatsCommand, FetchCollectionStatsResponse>();
        registerType<Command::ModifyCollection, ModifyCollectionCommand, ModifyCollectionResponse>();
        registerType<Command::MoveCollection, MoveCollectionCommand, MoveCollectionResponse>();
        registerType<Command::SelectCollection, SelectCollectionCommand, SelectCollectionResponse>();

        // Search
        registerType<Command::Search, SearchCommand, SearchResponse>();
        registerType<Command::SearchResult, SearchResultCommand, SearchResultResponse>();
        registerType<Command::StoreSearch, StoreSearchCommand, StoreSearchResponse>();

        // Tag
        registerType<Command::CreateTag, CreateTagCommand, CreateTagResponse>();
        registerType<Command::DeleteTag, DeleteTagCommand, DeleteTagResponse>();
        registerType<Command::FetchTags, FetchTagsCommand, FetchTagsResponse>();
        registerType<Command::ModifyTag, ModifyTagCommand, ModifyTagResponse>();

        // Relation
        registerType<Command::FetchRelations, FetchRelationsCommand, FetchRelationsResponse>();
        registerType<Command::ModifyRelation, ModifyRelationCommand, ModifyRelationResponse>();
        registerType<Command::RemoveRelations, RemoveRelationsCommand, RemoveRelationsResponse>();

        // Resources
        registerType<Command::SelectResource, SelectResourceCommand, SelectResourceResponse>();

        // Other...?
        registerType<Command::StreamPayload, StreamPayloadCommand, StreamPayloadResponse>();
    }

    QHash<Command::Type, QPair<CommandFactoryFunc, ResponseFactoryFunc>> registrar;

private:
    template<typename T>
    static Command commandFactoryFunc()
    {
        return T();
    }
    template<typename T>
    static Response responseFactoryFunc()
    {
        return T();
    }

    template<Command::Type T,typename CmdType, typename RespType>
    void registerType() {
        CommandFactoryFunc cmdFunc = &commandFactoryFunc<CmdType>;
        ResponseFactoryFunc respFunc = &responseFactoryFunc<RespType>;
        registrar.insert(T, qMakePair<CommandFactoryFunc, ResponseFactoryFunc>(cmdFunc, respFunc));
    }
};

Q_GLOBAL_STATIC(FactoryPrivate, sFactoryPrivate)

Command Factory::command(Command::Type type)
{
    auto iter = sFactoryPrivate->registrar.constFind(type);
    if (iter == sFactoryPrivate->registrar.constEnd()) {
        Q_ASSERT_X(iter != sFactoryPrivate->registrar.constEnd(),
                    "Aknadi::Protocol::Factory::command()", "Invalid command");
    }
    return iter.value().first();
}

Response Factory::response(Command::Type type)
{
    auto iter = sFactoryPrivate->registrar.constFind(type);
    if (iter == sFactoryPrivate->registrar.constEnd()) {
        Q_ASSERT_X(iter != sFactoryPrivate->registrar.constEnd(),
                    "Akonadi::Protocol::Factory::response()", "Invalid response");
    }
    return iter.value().second();
}

} // namespace Protocol
} // namespace Akonadi



/******************************************************************************/


namespace Akonadi
{
namespace Protocol
{


class CommandPrivate : public QSharedData
{
public:
    CommandPrivate(qint8 type)
        : commandType(type)
    {}


    qint8 commandType;
};

} // namespace Protocol
} // namespace Akonadi

AKONADI_DECLARE_PRIVATE(Command)

Command::Command(CommandPrivate *dd)
    : d_ptr(dd)
{
}

Command::Command(Command &&other)
{
    d_ptr.swap(other.d_ptr);
}

Command::Command(const Command &other)
{
    d_ptr = other.d_ptr;
}

Command::~Command()
{
}

Command& Command::operator=(Command &&other)
{
    d_ptr.swap(other.d_ptr);
    return *this;
}

Command& Command::operator=(const Command &other)
{
    d_ptr = other.d_ptr;
    return *this;
}

Command::Type Command::type() const
{
    return static_cast<Command::Type>(d_func()->commandType & ~_ResponseBit);
}

bool Command::isValid() const
{
    return d_func()->commandType != Invalid;
}

bool Command::isResponse() const
{
    return d_func()->commandType & _ResponseBit;
}

void Command::serialize(QDataStream &stream) const
{
    stream << *this;
}
void Command::deserialize(QDataStream &stream)
{
    stream >> *this;
}

QDataStream &operator<<(QDataStream &stream, const Akonadi::Protocol::Command &command)
{
    return stream << command.d_func()->commandType;
}

QDataStream &operator>>(QDataStream &stream, Akonadi::Protocol::Command &command)
{
    return stream >> command.d_func()->commandType;
}




/******************************************************************************/



namespace Akonadi
{
namespace Protocol
{


class ResponsePrivate : public CommandPrivate
{
public:
    ResponsePrivate(Command::Type type)
    : CommandPrivate(type & Command::_ResponseBit)
    , errorCode(0)
    {}

    QString errorMsg;
    int errorCode;
};

} // namespace Protocol
} // namespace Akonadi

AKONADI_DECLARE_PRIVATE(Response)

Response::Response(ResponsePrivate *dd)
    : Command(dd)
{
}

void Response::setError(int code, const QString &message)
{
    d_func()->errorCode = code;
    d_func()->errorMsg = message;
}

bool Response::isError() const
{
    return d_func()->errorCode;
}

int Response::errorCode() const
{
    return d_func()->errorCode;
}

QString Response::errorMessage() const
{
    return d_func()->errorMsg;
}

void Response::serialize(QDataStream &stream) const
{
    stream << *this;
}
void Response::deserialize(QDataStream &stream)
{
    stream >> *this;
}

QDataStream &operator<<(QDataStream &stream, const Response &command)
{
    return stream << command.d_func()->errorCode
                  << command.d_func()->errorMsg;
}

QDataStream &operator>>(QDataStream &stream, Response &command)
{
    return stream >> command.d_func()->errorCode
                  >> command.d_func()->errorMsg;
}




/******************************************************************************/

namespace Akonadi
{
namespace Protocol
{

class FetchScopePrivate : public QSharedData
{
public:
    FetchScopePrivate()
        : fetchFlags(FetchScope::None)
    {}

    QVector<QByteArray> requestedParts;
    QStringList requestedPayloads;
    QDateTime changedSince;
    QVector<QByteArray> tagFetchScope;
    int ancestorDepth;
    FetchScope::FetchFlags fetchFlags;
};

} // namespace Protocol
} // namespace Akonadi

FetchScope::FetchScope()
    : d(new FetchScopePrivate)
{
}


FetchScope::FetchScope(FetchScope &&other)
{
    d.swap(other.d);
}

FetchScope::FetchScope(const FetchScope &other)
    : d(other.d)
{
}

FetchScope::~FetchScope()
{
}

FetchScope &FetchScope::operator=(FetchScope &&other)
{
    d.swap(other.d);
    return *this;
}

FetchScope &FetchScope::operator=(const FetchScope &other)
{
    d = other.d;
    return *this;
}

void FetchScope::setRequestedParts(const QVector<QByteArray> &requestedParts)
{
    d->requestedParts = requestedParts;
}

QVector<QByteArray> FetchScope::requestedParts() const
{
    return d->requestedParts;
}

void FetchScope::setRequestedPayloads(const QStringList &requestedPayloads)
{
    d->requestedPayloads = requestedPayloads;
}

QStringList FetchScope::requestedPayloads() const
{
    return d->requestedPayloads;
}

void FetchScope::setChangedSince(const QDateTime &changedSince)
{
    d->changedSince = changedSince;
}

QDateTime FetchScope::changedSince() const
{
    return d->changedSince;
}

void FetchScope::setTagFetchScope(const QVector<QByteArray> &tagFetchScope)
{
    d->tagFetchScope = tagFetchScope;
}

QVector<QByteArray> FetchScope::tagFetchScope() const
{
    return d->tagFetchScope;
}

void FetchScope::setAncestorDepth(int depth)
{
    d->ancestorDepth = depth;
}

int FetchScope::ancestorDepth() const
{
    return d->ancestorDepth;
}

bool FetchScope::cacheOnly() const
{
    return d->fetchFlags & CacheOnly;
}

bool FetchScope::checkCachedPayloadPartsOnly() const
{
    return d->fetchFlags & CheckCachedPayloadPartsOnly;
}
bool FetchScope::fullPayload() const
{
    return d->fetchFlags & FullPayload;
}
bool FetchScope::allAttributes() const
{
    return d->fetchFlags & AllAttributes;
}
bool FetchScope::fetchSize() const
{
    return d->fetchFlags & Size;
}
bool FetchScope::fetchMTime() const
{
    return d->fetchFlags & MTime;
}
bool FetchScope::fetchRemoteRevision() const
{
    return d->fetchFlags & RemoteRevision;
}
bool FetchScope::ignoreErrors() const
{
    return d->fetchFlags & IgnoreErrors;
}
bool FetchScope::fetchFlags() const
{
    return d->fetchFlags & Flags;
}
bool FetchScope::fetchRemoteId() const
{
    return d->fetchFlags & RemoteID;
}
bool FetchScope::fetchGID() const
{
    return d->fetchFlags & GID;
}
bool FetchScope::fetchTags() const
{
    return d->fetchFlags & Tags;
}
bool FetchScope::fetchRelations() const
{
    return d->fetchFlags & Relations;
}
bool FetchScope::fetchVirtualReferences() const
{
    return d->fetchFlags & VirtReferences;
}

void FetchScope::setFetch(FetchFlags attributes, bool fetch)
{
    if (fetch) {
        d->fetchFlags |= attributes;
    } else {
        d->fetchFlags &= ~attributes;
    } // namespace Protocol
} // namespace Akonadi

bool FetchScope::fetch(FetchFlags flags) const
{
    return d->fetchFlags & flags;
}

QDataStream &operator<<(QDataStream &stream, const FetchScope &scope)
{
    return stream << scope.d->requestedParts
                  << scope.d->requestedPayloads
                  << scope.d->changedSince
                  << scope.d->tagFetchScope
                  << scope.d->ancestorDepth
                  << scope.d->fetchFlags;
}

QDataStream &operator>>(QDataStream &stream, FetchScope &scope)
{
    return stream >> scope.d->requestedParts
                  >> scope.d->requestedPayloads
                  >> scope.d->changedSince
                  >> scope.d->tagFetchScope
                  >> scope.d->ancestorDepth
                  >> scope.d->fetchFlags;
}



/******************************************************************************/

namespace Akonadi
{
namespace Protocol
{

class PartMetaDataPrivate : public QSharedData
{
public:
    PartMetaDataPrivate()
        : size(0)
        , version(0)
    {}

    QByteArray name;
    qint64 size;
    int version;
};

} // namespace Protocol
} // namespace Akonadi

PartMetaData::PartMetaData()
    : d(new PartMetaDataPrivate)
{
}

PartMetaData::PartMetaData(PartMetaData &&other)
{
    d.swap(other.d);
}

PartMetaData::PartMetaData(const PartMetaData &other)
{
    d = other.d;
}

PartMetaData::~PartMetaData()
{
}

PartMetaData &PartMetaData::operator=(PartMetaData &&other)
{
    d.swap(other.d);
    return *this;
}

PartMetaData &PartMetaData::operator=(const PartMetaData &other)
{
    d = other.d;
    return *this;
}

bool PartMetaData::operator<(const PartMetaData &other) const
{
    return d->name < other.d->name;
}

void PartMetaData::setName(const QByteArray &name)
{
    d->name = name;
}
QByteArray PartMetaData::name() const
{
    return d->name;
}

void PartMetaData::setSize(qint64 size)
{
    d->size = size;
}
qint64 PartMetaData::size() const
{
    return d->size;
}

void PartMetaData::setVersion(int version)
{
    d->version = version;
}
int PartMetaData::version() const
{
    return d->version;
}

QDataStream &operator<<(QDataStream &stream, const PartMetaData &part)
{
    return stream << part.d->name
                  << part.d->size
                  << part.d->version;
}

QDataStream &operator>>(QDataStream &stream, PartMetaData &part)
{
    return stream >> part.d->name
                  >> part.d->size
                  >> part.d->version;
}



/******************************************************************************/


namespace Akonadi
{
namespace Protocol
{

class CachePolicyPrivate : public QSharedData
{
public:
    CachePolicyPrivate()
        : interval(-1)
        , cacheTimeout(-1)
        , syncOnDemand(false)
        , inherit(true)
    {}

    QStringList localParts;
    int interval;
    int cacheTimeout;
    bool syncOnDemand;
    bool inherit;
};

} // namespace Protocol
} // namespace Akonadi

CachePolicy::CachePolicy()
    : d(new CachePolicyPrivate)
{
}

CachePolicy::CachePolicy(CachePolicy &&other)
{
    d.swap(other.d);
}

CachePolicy::CachePolicy(const CachePolicy &other)
    : d(other.d)
{
}

CachePolicy::~CachePolicy()
{
}

CachePolicy &CachePolicy::operator=(CachePolicy &&other)
{
    d.swap(other.d);
    return *this;
}

CachePolicy &CachePolicy::operator=(const CachePolicy &other)
{
    d = other.d;
    return *this;
}

void CachePolicy::setInherit(bool inherit)
{
    d->inherit = inherit;
}
bool CachePolicy::inherit() const
{
    return d->inherit;
}

void CachePolicy::setCheckInterval(int interval)
{
    d->interval = interval;
}
int CachePolicy::checkInterval() const
{
    return d->interval;
}

void CachePolicy::setCacheTimeout(int timeout)
{
    d->cacheTimeout = timeout;
}
int CachePolicy::cacheTimeout() const
{
    return d->cacheTimeout;
}

void CachePolicy::setSyncOnDemand(bool onDemand)
{
    d->syncOnDemand = onDemand;
}
bool CachePolicy::syncOnDemand() const
{
    return d->syncOnDemand;
}

void CachePolicy::setLocalParts(const QStringList &localParts)
{
    d->localParts = localParts;
}
QStringList CachePolicy::localParts() const
{
    return d->localParts;
}


QDataStream &operator<<(QDataStream &stream, const CachePolicy &policy)
{
    return stream << policy.d->inherit
                  << policy.d->interval
                  << policy.d->cacheTimeout
                  << policy.d->syncOnDemand
                  << policy.d->localParts;
}

QDataStream &operator>>(QDataStream &stream, CachePolicy &policy)
{
    return stream >> policy.d->inherit
                  >> policy.d->interval
                  >> policy.d->cacheTimeout
                  >> policy.d->syncOnDemand
                  >> policy.d->localParts;
}



/******************************************************************************/


namespace Akonadi
{
namespace Protocol
{


class AncestorPrivate : public QSharedData
{
public:
    AncestorPrivate()
        : id(-1)
    {}
    AncestorPrivate(qint64 id)
        : id(id)
    {}

    qint64 id;
    QString remoteId;
    Attributes attrs;
};


} // namespace Protocol
} // namespace Akonadi

Ancestor::Ancestor()
    : d(new AncestorPrivate)
{
}

Ancestor::Ancestor(qint64 id)
    : d(new AncestorPrivate(id))
{
}

Ancestor::Ancestor(Ancestor &&other)
{
    d.swap(other.d);
}

Ancestor::Ancestor(const Ancestor &other)
    : d(other.d)
{
}

Ancestor::~Ancestor()
{
}

Ancestor &Ancestor::operator=(Ancestor &&other)
{
    d.swap(other.d);
    return *this;
}

Ancestor &Ancestor::operator=(const Ancestor &other)
{
    d = other.d;
    return *this;
}

void Ancestor::setId(qint64 id)
{
    d->id = id;
}
qint64 Ancestor::id() const
{
    return d->id;
}

void Ancestor::setRemoteId(const QString &remoteId)
{
    d->remoteId = remoteId;
}
QString Ancestor::remoteId() const
{
    return d->remoteId;
}

void Ancestor::setAttributes(const Attributes &attributes)
{
    d->attrs = attributes;
}
Attributes Ancestor::attributes() const
{
    return d->attrs;
}


QDataStream &operator<<(QDataStream &stream, const Ancestor &ancestor)
{
    return stream << ancestor.d->id
                  << ancestor.d->remoteId
                  << ancestor.d->attrs;
}

QDataStream &operator>>(QDataStream &stream, Ancestor &ancestor)
{
    return stream >> ancestor.d->id
                  >> ancestor.d->remoteId
                  >> ancestor.d->attrs;
}




/******************************************************************************/



namespace Akonadi
{
namespace Protocol
{


class HelloResponsePrivate : public ResponsePrivate
{
public:
    HelloResponsePrivate()
        : ResponsePrivate(Command::Hello)
        , protocol(0)
    {}
    HelloResponsePrivate(const QString &server, const QString &message, int protocol)
        : ResponsePrivate(Command::Hello)
        , server(server)
        , message(message)
        , protocol(protocol)
    {}

    QString server;
    QString message;
    int protocol;
};

} // namespace Protocol
} // namespace Akonadi

AKONADI_DECLARE_PRIVATE(HelloResponse)

HelloResponse::HelloResponse(const QString &server, const QString &message, int protocol)
    : Response(new HelloResponsePrivate(server, message, protocol))
{
}

HelloResponse::HelloResponse()
    : Response(new HelloResponsePrivate)
{
}

QString HelloResponse::serverName() const
{
    return d_func()->server;
}

QString HelloResponse::message() const
{
    return d_func()->message;
}

int HelloResponse::protocolVersion() const
{
    return d_func()->protocol;
}

void HelloResponse::serialize(QDataStream &stream) const
{
    stream << *this;
}
void HelloResponse::deserialize(QDataStream &stream)
{
    stream >> *this;
}

QDataStream &operator<<(QDataStream &stream, const HelloResponse &command)
{
    return stream << command.d_func()->server
                  << command.d_func()->message
                  << command.d_func()->protocol;
}

QDataStream &operator>>(QDataStream &stream, HelloResponse &command)
{
    return stream >> command.d_func()->server
                  >> command.d_func()->message
                  >> command.d_func()->protocol;
}




/******************************************************************************/


namespace Akonadi
{
namespace Protocol
{


class LoginCommandPrivate : public CommandPrivate
{
public:
    LoginCommandPrivate()
        : CommandPrivate(Command::Login)
    {}
    LoginCommandPrivate(const QByteArray &sessionId)
        : CommandPrivate(Command::Login)
        , sessionId(sessionId)
    {}

    QByteArray sessionId;
};

} // namespace Protocol
} // namespace Akonadi

AKONADI_DECLARE_PRIVATE(LoginCommand)

LoginCommand::LoginCommand()
    : Command(new LoginCommandPrivate)
{
}

LoginCommand::LoginCommand(const QByteArray &sessionId)
    : Command(new LoginCommandPrivate(sessionId))
{
}

QByteArray LoginCommand::sessionId() const
{
    return d_func()->sessionId;
}

void LoginCommand::serialize(QDataStream &stream) const
{
    stream << *this;
}
void LoginCommand::deserialize(QDataStream &stream)
{
    stream >> *this;
}

QDataStream &operator<<(QDataStream &stream, const LoginCommand &command)
{
    return stream << command.d_func()->sessionId;
}

QDataStream &operator>>(QDataStream &stream, LoginCommand &command)
{
    return stream >> command.d_func()->sessionId;
}




/******************************************************************************/




LoginResponse::LoginResponse()
    : Response(new ResponsePrivate(Command::Login))
{
}




/******************************************************************************/




LogoutCommand::LogoutCommand()
    : Command(new CommandPrivate(Command::Logout))
{
}



/******************************************************************************/



LogoutResponse::LogoutResponse()
    : Response(new ResponsePrivate(Command::Logout))
{
}




/******************************************************************************/



namespace Akonadi
{
namespace Protocol
{

class TransactionCommandPrivate : public CommandPrivate
{
public:
    TransactionCommandPrivate(TransactionCommand::Mode mode = TransactionCommand::Invalid)
        : CommandPrivate(Command::Transaction)
        , mode(mode)
    {}

    TransactionCommand::Mode mode;
};

} // namespace Protocol
} // namespace Akonadi

AKONADI_DECLARE_PRIVATE(TransactionCommand)

TransactionCommand::TransactionCommand()
    : Command(new TransactionCommandPrivate)
{
}

TransactionCommand::TransactionCommand(TransactionCommand::Mode mode)
    : Command(new TransactionCommandPrivate(mode))
{
}

TransactionCommand::Mode TransactionCommand::mode() const
{
    return d_func()->mode;
}

void TransactionCommand::serialize(QDataStream &stream) const
{
    stream << *this;
}
void TransactionCommand::deserialize(QDataStream &stream)
{
    stream >> *this;
}

QDataStream &operator<<(QDataStream &stream, const TransactionCommand &command)
{
    return stream << command.d_func()->mode;
}

QDataStream &operator>>(QDataStream &stream, TransactionCommand &command)
{
    return stream >> command.d_func()->mode;
}



/******************************************************************************/




TransactionResponse::TransactionResponse()
    : Response(new ResponsePrivate(Command::Transaction))
{
}



/******************************************************************************/



namespace Akonadi
{
namespace Protocol
{


class CreateItemCommandPrivate : public CommandPrivate
{
public:
    CreateItemCommandPrivate()
        : CommandPrivate(Command::CreateItem)
        , mergeMode(CreateItemCommand::None)
        , itemSize(0)
    {}

    Scope collection;
    QString mimeType;
    QString gid;
    QString remoteId;
    QString remoteRev;
    QDateTime dateTime;
    Scope tags;
    Scope addedTags;
    Scope removedTags;
    QVector<QByteArray> flags;
    QVector<QByteArray> addedFlags;
    QVector<QByteArray> removedFlags;
    QVector<QByteArray> removedParts;
    QVector<PartMetaData> parts;
    CreateItemCommand::MergeModes mergeMode;
    qint64 itemSize;
};

} // namespace Protocol
} // namespace Akonadi

AKONADI_DECLARE_PRIVATE(CreateItemCommand)

CreateItemCommand::CreateItemCommand()
    : Command(new CreateItemCommandPrivate)
{
}

void CreateItemCommand::setMergeModes(const MergeModes &mode)
{
    d_func()->mergeMode = mode;
}
CreateItemCommand::MergeModes CreateItemCommand::mergeModes() const
{
    return d_func()->mergeMode;
}

void CreateItemCommand::setCollection(const Scope &collection)
{
    d_func()->collection = collection;
}
Scope CreateItemCommand::collection() const
{
    return d_func()->collection;
}

void CreateItemCommand::setItemSize(qint64 size)
{
    d_func()->itemSize = size;
}
qint64 CreateItemCommand::itemSize() const
{
    return d_func()->itemSize;
}

void CreateItemCommand::setMimeType(const QString &mimeType)
{
    d_func()->mimeType = mimeType;
}
QString CreateItemCommand::mimeType() const
{
    return d_func()->mimeType;
}

void CreateItemCommand::setGID(const QString &gid)
{
    d_func()->gid = gid;
}
QString CreateItemCommand::gid() const
{
    return d_func()->gid;
}

void CreateItemCommand::setRemoteId(const QString &remoteId)
{
    d_func()->remoteId = remoteId;
}
QString CreateItemCommand::remoteId() const
{
    return d_func()->remoteId;
}

void CreateItemCommand::setRemoteRevision(const QString &remoteRevision)
{
    d_func()->remoteRev = remoteRevision;
}

QString CreateItemCommand::remoteRevision() const
{
    return d_func()->remoteRev;
}

void CreateItemCommand::setDateTime(const QDateTime &dateTime)
{
    d_func()->dateTime = dateTime;
}
QDateTime CreateItemCommand::dateTime() const
{
    return d_func()->dateTime;
}

void CreateItemCommand::setFlags(const QVector<QByteArray> &flags)
{
    d_func()->flags = flags;
}
QVector<QByteArray> CreateItemCommand::flags() const
{
    return d_func()->flags;
}
void CreateItemCommand::setAddedFlags(const QVector<QByteArray> &flags)
{
    d_func()->addedFlags = flags;
}
QVector<QByteArray> CreateItemCommand::addedFlags() const
{
    return d_func()->addedFlags;
}
void CreateItemCommand::setRemovedFlags(const QVector<QByteArray> &flags)
{
    d_func()->removedFlags = flags;
}
QVector<QByteArray> CreateItemCommand::removedFlags() const
{
    return d_func()->removedFlags;
}

void CreateItemCommand::setTags(const Scope &tags)
{
    d_func()->tags = tags;
}
Scope CreateItemCommand::tags() const
{
    return d_func()->tags;
}
void CreateItemCommand::setAddedTags(const Scope &tags)
{
    d_func()->addedTags = tags;
}
Scope CreateItemCommand::addedTags() const
{
    return d_func()->addedTags;
}
void CreateItemCommand::setRemovedTags(const Scope &tags)
{
    d_func()->removedTags = tags;
}
Scope CreateItemCommand::removedTags() const
{
    return d_func()->removedTags;
}

void CreateItemCommand::setRemovedParts(const QVector<QByteArray> &removedParts)
{
    d_func()->removedParts = removedParts;
}
QVector<QByteArray> CreateItemCommand::removedParts() const
{
    return d_func()->removedParts;
}
void CreateItemCommand::setParts(const QVector<PartMetaData> &parts)
{
    d_func()->parts = parts;
}
QVector<PartMetaData> CreateItemCommand::parts() const
{
    return d_func()->parts;
}

void CreateItemCommand::serialize(QDataStream &stream) const
{
    stream << *this;
}
void CreateItemCommand::deserialize(QDataStream &stream)
{
    stream >> *this;
}

QDataStream &operator<<(QDataStream &stream, const CreateItemCommand &command)
{
    return stream << command.d_func()->mergeMode
                  << command.d_func()->collection
                  << command.d_func()->itemSize
                  << command.d_func()->mimeType
                  << command.d_func()->gid
                  << command.d_func()->remoteId
                  << command.d_func()->remoteRev
                  << command.d_func()->dateTime
                  << command.d_func()->flags
                  << command.d_func()->addedFlags
                  << command.d_func()->removedFlags
                  << command.d_func()->tags
                  << command.d_func()->addedTags
                  << command.d_func()->removedTags
                  << command.d_func()->removedParts
                  << command.d_func()->parts;
}

QDataStream &operator>>(QDataStream &stream, CreateItemCommand &command)
{
    return stream >> command.d_func()->mergeMode
                  >> command.d_func()->collection
                  >> command.d_func()->itemSize
                  >> command.d_func()->mimeType
                  >> command.d_func()->gid
                  >> command.d_func()->remoteId
                  >> command.d_func()->remoteRev
                  >> command.d_func()->dateTime
                  >> command.d_func()->flags
                  >> command.d_func()->addedFlags
                  >> command.d_func()->removedFlags
                  >> command.d_func()->tags
                  >> command.d_func()->addedTags
                  >> command.d_func()->removedTags
                  >> command.d_func()->removedParts
                  >> command.d_func()->parts;
}




/******************************************************************************/




CreateItemResponse::CreateItemResponse()
    : Response(new ResponsePrivate(Command::CreateItem))
{
}




/******************************************************************************/



namespace Akonadi
{
namespace Protocol
{

class CopyItemsCommandPrivate : public CommandPrivate
{
public:
    CopyItemsCommandPrivate()
        : CommandPrivate(Command::CopyItems)
    {}
    CopyItemsCommandPrivate(const Scope &items, const Scope &dest)
        : CommandPrivate(Command::CopyItems)
        , items(items)
        , dest(dest)
    {}

    Scope items;
    Scope dest;
};

} // namespace Protocol
} // namespace Akonadi

AKONADI_DECLARE_PRIVATE(CopyItemsCommand)

CopyItemsCommand::CopyItemsCommand()
    : Command(new CopyItemsCommandPrivate)
{
}

CopyItemsCommand::CopyItemsCommand(const Scope &items, const Scope &dest)
    : Command(new CopyItemsCommandPrivate(items, dest))
{
}

Scope CopyItemsCommand::items() const
{
    return d_func()->items;
}

Scope CopyItemsCommand::destination() const
{
    return d_func()->dest;
}

void CopyItemsCommand::serialize(QDataStream &stream) const
{
    stream << *this;
}
void CopyItemsCommand::deserialize(QDataStream &stream)
{
    stream >> *this;
}

QDataStream &operator<<(QDataStream &stream, const CopyItemsCommand &command)
{
    return stream << command.d_func()->items
                  << command.d_func()->dest;
}

QDataStream &operator>>(QDataStream &stream, CopyItemsCommand &command)
{
    return stream >> command.d_func()->items
                  >> command.d_func()->dest;
}




/******************************************************************************/




CopyItemsResponse::CopyItemsResponse()
    : Response(new ResponsePrivate(Command::CopyItems))
{
}



/******************************************************************************/



namespace Akonadi
{
namespace Protocol
{

class DeleteItemsCommandPrivate : public CommandPrivate
{
public:
    DeleteItemsCommandPrivate()
        : CommandPrivate(Command::DeleteItems)
    {}
    DeleteItemsCommandPrivate(const Scope &items)
        : CommandPrivate(Command::DeleteItems)
        , items(items)
    {}

    Scope items;
};

} // namespace Protocol
} // namespace Akonadi

AKONADI_DECLARE_PRIVATE(DeleteItemsCommand)

DeleteItemsCommand::DeleteItemsCommand()
    : Command(new DeleteItemsCommandPrivate)
{
}

DeleteItemsCommand::DeleteItemsCommand(const Scope &items)
    : Command(new DeleteItemsCommandPrivate(items))
{
}

Scope DeleteItemsCommand::items() const
{
    return d_func()->items;
}

void DeleteItemsCommand::serialize(QDataStream &stream) const
{
    stream << *this;
}
void DeleteItemsCommand::deserialize(QDataStream &stream)
{
    stream >> *this;
}

QDataStream &operator<<(QDataStream &stream, const DeleteItemsCommand &command)
{
    return stream << command.d_func()->items;
}

QDataStream &operator>>(QDataStream &stream, DeleteItemsCommand &command)
{
    return stream >> command.d_func()->items;
}




/******************************************************************************/




DeleteItemsResponse::DeleteItemsResponse()
    : Response(new ResponsePrivate(Command::DeleteItems))
{
}




/******************************************************************************/



namespace Akonadi
{
namespace Protocol
{


class FetchRelationsCommandPrivate : public CommandPrivate
{
public:
    FetchRelationsCommandPrivate()
        : CommandPrivate(Command::FetchRelations)
        , left(-1)
        , right(-1)
        , side(-1)
    {}

    qint64 left;
    qint64 right;
    qint64 side;
    QString type;
    QString resource;
};

} // namespace Protocol
} // namespace Akonadi

AKONADI_DECLARE_PRIVATE(FetchRelationsCommand)

FetchRelationsCommand::FetchRelationsCommand()
    : Command(new FetchRelationsCommandPrivate)
{
}

void FetchRelationsCommand::setLeft(qint64 left)
{
    d_func()->left = left;
}
qint64 FetchRelationsCommand::left() const
{
    return d_func()->left;
}

void FetchRelationsCommand::setRight(qint64 right)
{
    d_func()->right = right;
}
qint64 FetchRelationsCommand::right() const
{
    return d_func()->right;
}

void FetchRelationsCommand::setSide(qint64 side)
{
    d_func()->side = side;
}
qint64 FetchRelationsCommand::side() const
{
    return d_func()->side;
}

void FetchRelationsCommand::setType(const QString &type)
{
    d_func()->type = type;
}
QString FetchRelationsCommand::type() const
{
    return d_func()->type;
}

void FetchRelationsCommand::setResource(const QString &resource)
{
    d_func()->resource = resource;
}
QString FetchRelationsCommand::resource() const
{
    return d_func()->resource;
}

void FetchRelationsCommand::serialize(QDataStream &stream) const
{
    stream << *this;
}
void FetchRelationsCommand::deserialize(QDataStream &stream)
{
    stream >> *this;
}

QDataStream &operator<<(QDataStream &stream, const FetchRelationsCommand &command)
{
    return stream << command.d_func()->left
                  << command.d_func()->right
                  << command.d_func()->side
                  << command.d_func()->type
                  << command.d_func()->resource;
}

QDataStream &operator>>(QDataStream &stream, FetchRelationsCommand &command)
{
    return stream >> command.d_func()->left
                  >> command.d_func()->right
                  >> command.d_func()->side
                  >> command.d_func()->type
                  >> command.d_func()->resource;
}




/*****************************************************************************/



namespace Akonadi
{
namespace Protocol
{

class FetchRelationsResponsePrivate : public ResponsePrivate
{
public:
    FetchRelationsResponsePrivate(qint64 left = -1, qint64 right = -1, const QString &type = QString())
        : ResponsePrivate(Command::FetchRelations)
        , left(left)
        , right(right)
        , type(type)
    {}

    qint64 left;
    qint64 right;
    QString type;
    QString remoteId;
};

} // namespace Protocol
} // namespace Akonadi

AKONADI_DECLARE_PRIVATE(FetchRelationsResponse)

FetchRelationsResponse::FetchRelationsResponse()
    : Response(new FetchRelationsResponsePrivate)
{
}

FetchRelationsResponse::FetchRelationsResponse(qint64 left, qint64 right, const QString &type)
    : Response(new FetchRelationsResponsePrivate(left, right, type))
{
}

qint64 FetchRelationsResponse::left() const
{
    return d_func()->left;
}
qint64 FetchRelationsResponse::right() const
{
    return d_func()->right;
}
QString FetchRelationsResponse::type() const
{
    return d_func()->type;
}
void FetchRelationsResponse::setRemoteId(const QString &remoteId)
{
    d_func()->remoteId = remoteId;
}
QString FetchRelationsResponse::remoteId() const
{
    return d_func()->remoteId;
}

void FetchRelationsResponse::serialize(QDataStream &stream) const
{
    stream << *this;
}
void FetchRelationsResponse::deserialize(QDataStream &stream)
{
    stream >> *this;
}

QDataStream &operator<<(QDataStream &stream, const FetchRelationsResponse &command)
{
    return stream << command.d_func()->left
                  << command.d_func()->right
                  << command.d_func()->type
                  << command.d_func()->remoteId;
}

QDataStream &operator>>(QDataStream &stream, FetchRelationsResponse &command)
{
    return stream >> command.d_func()->left
                  >> command.d_func()->right
                  >> command.d_func()->type
                  >> command.d_func()->remoteId;
}




/******************************************************************************/



namespace Akonadi
{
namespace Protocol
{

class FetchTagsCommandPrivate : public CommandPrivate
{
public:
    FetchTagsCommandPrivate(const Scope &scope = Scope())
        : CommandPrivate(Command::FetchTags)
        , scope(scope)
    {}

    Scope scope;
};

} // namespace Protocol
} // namespace Akonadi

AKONADI_DECLARE_PRIVATE(FetchTagsCommand)

FetchTagsCommand::FetchTagsCommand()
    : Command(new FetchTagsCommandPrivate)
{
}

FetchTagsCommand::FetchTagsCommand(const Scope &scope)
    : Command(new FetchTagsCommandPrivate(scope))
{
}

Scope FetchTagsCommand::scope() const
{
    return d_func()->scope;
}

void FetchTagsCommand::serialize(QDataStream &stream) const
{
    stream << *this;
}
void FetchTagsCommand::deserialize(QDataStream &stream)
{
    stream >> *this;
}

QDataStream &operator<<(QDataStream &stream, const FetchTagsCommand &command)
{
    return stream << command.d_func()->scope;
}

QDataStream &operator>>(QDataStream &stream, FetchTagsCommand &command)
{
    return stream >> command.d_func()->scope;
}




/*****************************************************************************/


namespace Akonadi
{
namespace Protocol
{

class FetchTagsResponsePrivate : public ResponsePrivate
{
public:
    FetchTagsResponsePrivate(qint64 id = -1)
        : ResponsePrivate(Command::FetchTags)
        , id(id)
        , parentId(-1)
    {}

    qint64 id;
    qint64 parentId;
    QString gid;
    QString type;
    QString remoteId;
    Attributes attributes;
};

} // namespace Protocol
} // namespace Akonadi

AKONADI_DECLARE_PRIVATE(FetchTagsResponse)

FetchTagsResponse::FetchTagsResponse()
    : Response(new FetchTagsResponsePrivate)
{
}

FetchTagsResponse::FetchTagsResponse(qint64 id)
    : Response(new FetchTagsResponsePrivate(id))
{
}

qint64 FetchTagsResponse::id() const
{
    return d_func()->id;
}

void FetchTagsResponse::setParentId(qint64 parentId)
{
    d_func()->parentId = parentId;
}
qint64 FetchTagsResponse::parentId() const
{
    return d_func()->parentId;
}

void FetchTagsResponse::setGid(const QString &gid)
{
    d_func()->gid = gid;
}
QString FetchTagsResponse::gid() const
{
    return d_func()->gid;
}

void FetchTagsResponse::setType(const QString &type)
{
    d_func()->type = type;
}
QString FetchTagsResponse::type() const
{
    return d_func()->type;
}

void FetchTagsResponse::setRemoteId(const QString &remoteId)
{
    d_func()->remoteId = remoteId;
}
QString FetchTagsResponse::remoteId() const
{
    return d_func()->remoteId;
}

void FetchTagsResponse::setAttributes(const Attributes &attributes)
{
    d_func()->attributes = attributes;
}
Attributes FetchTagsResponse::attributes() const
{
    return d_func()->attributes;
}

void FetchTagsResponse::serialize(QDataStream &stream) const
{
    stream << *this;
}
void FetchTagsResponse::deserialize(QDataStream &stream)
{
    stream >> *this;
}

QDataStream &operator<<(QDataStream &stream, const FetchTagsResponse &command)
{
    return stream << command.d_func()->id
                  << command.d_func()->parentId
                  << command.d_func()->gid
                  << command.d_func()->type
                  << command.d_func()->remoteId
                  << command.d_func()->attributes;
}

QDataStream &operator>>(QDataStream &stream, FetchTagsResponse &command)
{
    return stream >> command.d_func()->id
                  >> command.d_func()->parentId
                  >> command.d_func()->gid
                  >> command.d_func()->type
                  >> command.d_func()->remoteId
                  >> command.d_func()->attributes;
}




/*****************************************************************************/



namespace Akonadi
{
namespace Protocol
{

class FetchItemsCommandPrivate : public CommandPrivate
{
public:
        FetchItemsCommandPrivate(const Scope &scope = Scope(),
                                 const FetchScope &fetchScope = FetchScope())
            : CommandPrivate(Command::FetchItems)
            , scope(scope)
            , fetchScope(fetchScope)
        {}

        Scope scope;
        FetchScope fetchScope;
};

} // namespace Protocol
} // namespace Akonadi

AKONADI_DECLARE_PRIVATE(FetchItemsCommand)

FetchItemsCommand::FetchItemsCommand()
    : Command(new FetchItemsCommandPrivate)
{
}

FetchItemsCommand::FetchItemsCommand(const Scope &scope, const FetchScope &fetchScope)
    : Command(new FetchItemsCommandPrivate(scope, fetchScope))
{
}

Scope FetchItemsCommand::scope() const
{
    return d_func()->scope;
}

FetchScope FetchItemsCommand::fetchScope() const
{
    return d_func()->fetchScope;
}

void FetchItemsCommand::serialize(QDataStream &stream) const
{
    stream << *this;
}
void FetchItemsCommand::deserialize(QDataStream &stream)
{
    stream >> *this;
}

QDataStream &operator<<(QDataStream &stream, const FetchItemsCommand &command)
{
    return stream << command.d_func()->scope
                  << command.d_func()->fetchScope;
}

QDataStream &operator>>(QDataStream &stream, FetchItemsCommand &command)
{
    return stream >> command.d_func()->scope
                  >> command.d_func()->fetchScope;
}




/****************************************************************************/




namespace Akonadi
{
namespace Protocol
{

class FetchItemsResponsePrivate : public ResponsePrivate
{
public:
    FetchItemsResponsePrivate(qint64 id = -1)
        : ResponsePrivate(Command::FetchItems)
        , id(id)
        , collectionId(-1)
        , size(0)
        , revision(0)
    {}

    QString remoteId;
    QString remoteRev;
    QString gid;
    QString mimeType;
    QDateTime time;
    QVector<QByteArray> flags;
    QVector<FetchTagsResponse> tags;
    QVector<qint64> virtRefs;
    QVector<FetchRelationsResponse> relations;
    QVector<Ancestor> ancestors;
    QMap<PartMetaData, StreamPayloadResponse> parts;
    QVector<QByteArray> cachedParts;
    qint64 id;
    qint64 collectionId;
    qint64 size;
    int revision;
};

} // namespace Protocol
} // namespace Akonadi

AKONADI_DECLARE_PRIVATE(FetchItemsResponse)

FetchItemsResponse::FetchItemsResponse()
    : Response(new FetchItemsResponsePrivate)
{
}

FetchItemsResponse::FetchItemsResponse(qint64 id)
    : Response(new FetchItemsResponsePrivate(id))
{
}

qint64 FetchItemsResponse::id() const
{
    return d_func()->id;
}

void FetchItemsResponse::setRevision(int revision)
{
    d_func()->revision = revision;
}
int FetchItemsResponse::revision() const
{
    return d_func()->revision;
}

void FetchItemsResponse::setParentId(qint64 parentId)
{
    d_func()->collectionId = parentId;
}
qint64 FetchItemsResponse::parentId() const
{
    return d_func()->collectionId;
}

void FetchItemsResponse::setRemoteId(const QString &remoteId)
{
    d_func()->remoteId = remoteId;
}
QString FetchItemsResponse::remoteId() const
{
    return d_func()->remoteId;
}

void FetchItemsResponse::setRemoteRevision(const QString &remoteRevision)
{
    d_func()->remoteRev = remoteRevision;
}
QString FetchItemsResponse::remoteRevision() const
{
    return d_func()->remoteRev;
}

void FetchItemsResponse::setGid(const QString &gid)
{
    d_func()->gid = gid;
}
QString FetchItemsResponse::gid() const
{
    return d_func()->gid;
}

void FetchItemsResponse::setSize(qint64 size)
{
    d_func()->size = size;
}
qint64 FetchItemsResponse::size() const
{
    return d_func()->size;
}

void FetchItemsResponse::setMimeType(const QString &mimeType)
{
    d_func()->mimeType = mimeType;
}
QString FetchItemsResponse::mimeType() const
{
    return d_func()->mimeType;
}

void FetchItemsResponse::setMTime(const QDateTime &mtime)
{
    d_func()->time = mtime;
}
QDateTime FetchItemsResponse::MTime() const
{
    return d_func()->time;
}

void FetchItemsResponse::setFlags(const QVector<QByteArray> &flags)
{
    d_func()->flags = flags;
}
QVector<QByteArray> FetchItemsResponse::flags() const
{
    return d_func()->flags;
}

void FetchItemsResponse::setTags(const QVector<FetchTagsResponse> &tags)
{
    d_func()->tags = tags;
}
QVector<FetchTagsResponse> FetchItemsResponse::tags() const
{
    return d_func()->tags;
}

void FetchItemsResponse::setVirtualReferences(const QVector<qint64> &refs)
{
    d_func()->virtRefs = refs;
}
QVector<qint64> FetchItemsResponse::virtualReferences() const
{
    return d_func()->virtRefs;
}

void FetchItemsResponse::setRelations(const QVector<FetchRelationsResponse> &relations)
{
    d_func()->relations = relations;
}
QVector<FetchRelationsResponse> FetchItemsResponse::relations() const
{
    return d_func()->relations;
}

void FetchItemsResponse::setAncestors(const QVector<Ancestor> &ancestors)
{
    d_func()->ancestors = ancestors;
}
QVector<Ancestor> FetchItemsResponse::ancestors() const
{
    return d_func()->ancestors;
}

void FetchItemsResponse::setParts(const QMap<PartMetaData, StreamPayloadResponse> &parts)
{
    d_func()->parts = parts;
}
QMap<PartMetaData, StreamPayloadResponse> FetchItemsResponse::parts() const
{
    return d_func()->parts;
}

void FetchItemsResponse::setCachedParts(const QVector<QByteArray> &cachedParts)
{
    d_func()->cachedParts = cachedParts;
}
QVector<QByteArray> FetchItemsResponse::cachedParts() const
{
    return d_func()->cachedParts;
}

void FetchItemsResponse::serialize(QDataStream &stream) const
{
    stream << *this;
}
void FetchItemsResponse::deserialize(QDataStream &stream)
{
    stream >> *this;
}

QDataStream &operator<<(QDataStream &stream, const FetchItemsResponse &command)
{
    return stream << command.d_func()->id
                  << command.d_func()->revision
                  << command.d_func()->collectionId
                  << command.d_func()->remoteId
                  << command.d_func()->remoteRev
                  << command.d_func()->gid
                  << command.d_func()->size
                  << command.d_func()->mimeType
                  << command.d_func()->time
                  << command.d_func()->flags
                  << command.d_func()->tags
                  << command.d_func()->virtRefs
                  << command.d_func()->relations
                  << command.d_func()->ancestors
                  << command.d_func()->parts
                  << command.d_func()->cachedParts;
}

QDataStream &operator>>(QDataStream &stream, FetchItemsResponse &command)
{
    return stream >> command.d_func()->id
                  >> command.d_func()->revision
                  >> command.d_func()->collectionId
                  >> command.d_func()->remoteId
                  >> command.d_func()->remoteRev
                  >> command.d_func()->gid
                  >> command.d_func()->size
                  >> command.d_func()->mimeType
                  >> command.d_func()->time
                  >> command.d_func()->flags
                  >> command.d_func()->tags
                  >> command.d_func()->virtRefs
                  >> command.d_func()->relations
                  >> command.d_func()->ancestors
                  >> command.d_func()->parts
                  >> command.d_func()->cachedParts;
}




/*****************************************************************************/



namespace Akonadi
{
namespace Protocol
{

class LinkItemsCommandPrivate : public CommandPrivate
{
public:
    LinkItemsCommandPrivate(LinkItemsCommand::Action action = LinkItemsCommand::Link,
                            const Scope &items = Scope(),
                            const Scope &dest = Scope())
        : CommandPrivate(Command::LinkItems)
        , action(action)
        , items(items)
        , dest(dest)
    {}

    LinkItemsCommand::Action action;
    Scope items;
    Scope dest;
};

} // namespace Protocol
} // namespace Akonadi

AKONADI_DECLARE_PRIVATE(LinkItemsCommand)

LinkItemsCommand::LinkItemsCommand()
    : Command(new LinkItemsCommandPrivate)
{
}

LinkItemsCommand::LinkItemsCommand(Action action, const Scope &items, const Scope &dest)
    : Command(new LinkItemsCommandPrivate(action, items, dest))
{
}

LinkItemsCommand::Action LinkItemsCommand::action() const
{
    return d_func()->action;
}
Scope LinkItemsCommand::items() const
{
    return d_func()->items;
}
Scope LinkItemsCommand::destination() const
{
    return d_func()->dest;
}

void LinkItemsCommand::serialize(QDataStream &stream) const
{
    stream << *this;
}
void LinkItemsCommand::deserialize(QDataStream &stream)
{
    stream >> *this;
}

QDataStream &operator<<(QDataStream &stream, const LinkItemsCommand &command)
{
    return stream << command.d_func()->action
                  << command.d_func()->items
                  << command.d_func()->dest;
}

QDataStream &operator>>(QDataStream &stream, LinkItemsCommand &command)
{
    return stream >> command.d_func()->action
                  >> command.d_func()->items
                  >> command.d_func()->dest;
}




/****************************************************************************/




LinkItemsResponse::LinkItemsResponse()
    : Response(new ResponsePrivate(Command::LinkItems))
{
}




/****************************************************************************/



namespace Akonadi
{
namespace Protocol
{

class ModifyItemsCommandPrivate : public CommandPrivate
{
public:
    ModifyItemsCommandPrivate(const Scope &items = Scope())
        : CommandPrivate(Command::ModifyItems)
        , items(items)
        , size(0)
        , oldRevision(-1)
        , dirty(true)
        , invalidate(false)
        , noResponse(false)
        , notify(true)
        , modifiedParts(ModifyItemsCommand::None)
    {}

    Scope items;
    QVector<QByteArray> flags;
    QVector<QByteArray> addedFlags;
    QVector<QByteArray> removedFlags;
    Scope tags;
    Scope addedTags;
    Scope removedTags;

    QString remoteId;
    QString remoteRev;
    QString gid;
    QVector<QByteArray> removedParts;
    QVector<PartMetaData> parts;
    qint64 size;
    int oldRevision;
    bool dirty;
    bool invalidate;
    bool noResponse;
    bool notify;

    ModifyItemsCommand::ModifiedParts modifiedParts;
};

} // namespace Protocol
} // namespace Akonadi

AKONADI_DECLARE_PRIVATE(ModifyItemsCommand)

ModifyItemsCommand::ModifyItemsCommand()
    : Command(new ModifyItemsCommandPrivate)
{
}

ModifyItemsCommand::ModifyItemsCommand(const Scope &items)
    : Command(new ModifyItemsCommandPrivate(items))
{
}

ModifyItemsCommand::ModifiedParts ModifyItemsCommand::modifiedParts() const
{
    return d_func()->modifiedParts;
}

void ModifyItemsCommand::setItems(const Scope &items)
{
    d_func()->items = items;
}
Scope ModifyItemsCommand::items() const
{
    return d_func()->items;
}

void ModifyItemsCommand::setOldRevision(int oldRevision)
{
    d_func()->oldRevision = oldRevision;
}
int ModifyItemsCommand::oldRevision() const
{
    return d_func()->oldRevision;
}

void ModifyItemsCommand::setFlags(const QVector<QByteArray> &flags)
{
    d_func()->modifiedParts |= Flags;
    d_func()->flags = flags;
}
QVector<QByteArray> ModifyItemsCommand::flags() const
{
    return d_func()->flags;
}

void ModifyItemsCommand::setAddedFlags(const QVector<QByteArray> &addedFlags)
{
    d_func()->modifiedParts |= AddedFlags;
    d_func()->addedFlags = addedFlags;
}
QVector<QByteArray> ModifyItemsCommand::addedFlags() const
{
    return d_func()->addedFlags;
}

void ModifyItemsCommand::setRemovedFlags(const QVector<QByteArray> &removedFlags)
{
    d_func()->modifiedParts |= RemovedFlags;
    d_func()->removedFlags = removedFlags;
}
QVector<QByteArray> ModifyItemsCommand::removedFlags() const
{
    return d_func()->removedFlags;
}

void ModifyItemsCommand::setTags(const Scope &tags)
{
    d_func()->modifiedParts |= Tags;
    d_func()->tags = tags;
}
Scope ModifyItemsCommand::tags() const
{
    return d_func()->tags;
}

void ModifyItemsCommand::setAddedTags(const Scope &addedTags)
{
    d_func()->modifiedParts |= AddedTags;
    d_func()->addedTags = addedTags;
}
Scope ModifyItemsCommand::addedTags() const
{
    return d_func()->addedTags;
}

void ModifyItemsCommand::setRemovedTags(const Scope &removedTags)
{
    d_func()->modifiedParts |= RemovedTags;
    d_func()->removedTags = removedTags;
}
Scope ModifyItemsCommand::removedTags() const
{
    return d_func()->removedTags;
}

void ModifyItemsCommand::setRemoteId(const QString &remoteId)
{
    d_func()->modifiedParts |= RemoteID;
    d_func()->remoteId = remoteId;
}
QString ModifyItemsCommand::remoteId() const
{
    return d_func()->remoteId;
}

void ModifyItemsCommand::setRemoteRevision(const QString &remoteRevision)
{
    d_func()->modifiedParts |= RemoteRevision;
    d_func()->remoteRev = remoteRevision;
}
QString ModifyItemsCommand::remoteRevision() const
{
    return d_func()->remoteRev;
}

void ModifyItemsCommand::setGid(const QString &gid)
{
    d_func()->modifiedParts |= GID;
    d_func()->gid = gid;
}
QString ModifyItemsCommand::gid() const
{
    return d_func()->gid;
}

void ModifyItemsCommand::setDirty(bool dirty)
{
    d_func()->dirty = dirty;
}
bool ModifyItemsCommand::dirty() const
{
    return d_func()->dirty;
}

void ModifyItemsCommand::setInvalidateCache(bool invalidate)
{
    d_func()->invalidate = invalidate;
}
bool ModifyItemsCommand::invalidateCache() const
{
    return d_func()->invalidate;
}

void ModifyItemsCommand::setNoResponse(bool noResponse)
{
    d_func()->noResponse = noResponse;
}
bool ModifyItemsCommand::noResponse() const
{
    return d_func()->noResponse;
}

void ModifyItemsCommand::setNotify(bool notify)
{
    d_func()->notify = notify;
}
bool ModifyItemsCommand::notify() const
{
    return d_func()->notify;
}

void ModifyItemsCommand::setItemSize(qint64 size)
{
    d_func()->modifiedParts |= Size;
    d_func()->size = size;
}
qint64 ModifyItemsCommand::itemSize() const
{
    return d_func()->size;
}

void ModifyItemsCommand::setRemovedParts(const QVector<QByteArray> &removedParts)
{
    d_func()->modifiedParts |= RemovedParts;
    d_func()->removedParts = removedParts;
}
QVector<QByteArray> ModifyItemsCommand::removedParts() const
{
    return d_func()->removedParts;
}

void ModifyItemsCommand::setParts(const QVector<PartMetaData> &parts)
{
    d_func()->modifiedParts |= Parts;
    d_func()->parts = parts;
}
QVector<PartMetaData> ModifyItemsCommand::parts() const
{
    return d_func()->parts;
}

void ModifyItemsCommand::serialize(QDataStream &stream) const
{
    stream << *this;
}
void ModifyItemsCommand::deserialize(QDataStream &stream)
{
    stream >> *this;
}

QDataStream &operator<<(QDataStream &stream, const ModifyItemsCommand &command)
{
    stream << command.d_func()->items
           << command.d_func()->oldRevision
           << command.d_func()->modifiedParts
           << command.d_func()->dirty
           << command.d_func()->invalidate
           << command.d_func()->noResponse
           << command.d_func()->notify;

    if (command.d_func()->modifiedParts & ModifyItemsCommand::Flags) {
        stream << command.d_func()->flags;
    }
    if (command.d_func()->modifiedParts & ModifyItemsCommand::AddedFlags) {
        stream << command.d_func()->addedFlags;
    }
    if (command.d_func()->modifiedParts & ModifyItemsCommand::RemovedFlags) {
        stream << command.d_func()->removedFlags;
    }
    if (command.d_func()->modifiedParts & ModifyItemsCommand::Tags) {
        stream << command.d_func()->tags;
    }
    if (command.d_func()->modifiedParts & ModifyItemsCommand::AddedTags) {
        stream << command.d_func()->addedTags;
    }
    if (command.d_func()->modifiedParts & ModifyItemsCommand::RemovedTags) {
        stream << command.d_func()->removedTags;
    }
    if (command.d_func()->modifiedParts & ModifyItemsCommand::RemoteID) {
        stream << command.d_func()->remoteId;
    }
    if (command.d_func()->modifiedParts & ModifyItemsCommand::RemoteRevision) {
        stream << command.d_func()->remoteRev;
    }
    if (command.d_func()->modifiedParts & ModifyItemsCommand::GID) {
        stream << command.d_func()->gid;
    }
    if (command.d_func()->modifiedParts & ModifyItemsCommand::Size) {
        stream << command.d_func()->size;
    }
    if (command.d_func()->modifiedParts & ModifyItemsCommand::Parts) {
        stream << command.d_func()->parts;
    }
    if (command.d_func()->modifiedParts & ModifyItemsCommand::RemovedParts) {
        stream << command.d_func()->removedParts;
    }
    return stream;
}

QDataStream &operator>>(QDataStream &stream, ModifyItemsCommand &command)
{
    stream >> command.d_func()->items
           >> command.d_func()->oldRevision
           >> command.d_func()->modifiedParts
           >> command.d_func()->dirty
           >> command.d_func()->invalidate
           >> command.d_func()->noResponse
           >> command.d_func()->notify;

    if (command.d_func()->modifiedParts & ModifyItemsCommand::Flags) {
        stream >> command.d_func()->flags;
    }
    if (command.d_func()->modifiedParts & ModifyItemsCommand::AddedFlags) {
        stream >> command.d_func()->addedFlags;
    }
    if (command.d_func()->modifiedParts & ModifyItemsCommand::RemovedFlags) {
        stream >> command.d_func()->removedFlags;
    }
    if (command.d_func()->modifiedParts & ModifyItemsCommand::Tags) {
        stream >> command.d_func()->tags;
    }
    if (command.d_func()->modifiedParts & ModifyItemsCommand::AddedTags) {
        stream >> command.d_func()->addedTags;
    }
    if (command.d_func()->modifiedParts & ModifyItemsCommand::RemovedTags) {
        stream >> command.d_func()->removedTags;
    }
    if (command.d_func()->modifiedParts & ModifyItemsCommand::RemoteID) {
        stream >> command.d_func()->remoteId;
    }
    if (command.d_func()->modifiedParts & ModifyItemsCommand::RemoteRevision) {
        stream >> command.d_func()->remoteRev;
    }
    if (command.d_func()->modifiedParts & ModifyItemsCommand::GID) {
        stream >> command.d_func()->gid;
    }
    if (command.d_func()->modifiedParts & ModifyItemsCommand::Size) {
        stream >> command.d_func()->size;
    }
    if (command.d_func()->modifiedParts & ModifyItemsCommand::Parts) {
        stream >> command.d_func()->parts;
    }
    if (command.d_func()->modifiedParts & ModifyItemsCommand::RemovedParts) {
        stream >> command.d_func()->removedParts;
    }
    return stream;
}



/****************************************************************************/



namespace Akonadi
{
namespace Protocol
{

class ModifyItemsResponsePrivate : public ResponsePrivate
{
public:
    ModifyItemsResponsePrivate(qint64 id = -1, int newRevision = -1)
        : ResponsePrivate(Command::ModifyItems)
        , id(id)
        , newRevision(newRevision)
    {}

    qint64 id;
    int newRevision;
};

} // namespace Protocol
} // namespace Akonadi

AKONADI_DECLARE_PRIVATE(ModifyItemsResponse)

ModifyItemsResponse::ModifyItemsResponse()
    : Response(new ModifyItemsResponsePrivate)
{
}

ModifyItemsResponse::ModifyItemsResponse(qint64 id, int newRevision)
    : Response(new ModifyItemsResponsePrivate(id, newRevision))
{
}

qint64 ModifyItemsResponse::id() const
{
    return d_func()->id;
}
int ModifyItemsResponse::newRevision() const
{
    return d_func()->newRevision;
}

void ModifyItemsResponse::serialize(QDataStream &stream) const
{
    stream << *this;
}
void ModifyItemsResponse::deserialize(QDataStream &stream)
{
    stream >> *this;
}

QDataStream &operator<<(QDataStream &stream, const ModifyItemsResponse &command)
{
    return stream << command.d_func()->id
                  << command.d_func()->newRevision;
}

QDataStream &operator>>(QDataStream &stream, ModifyItemsResponse &command)
{
    return stream >> command.d_func()->id
                  >> command.d_func()->newRevision;
}



/****************************************************************************/


namespace Akonadi
{
namespace Protocol
{

class MoveItemsCommandPrivate : public CommandPrivate
{
public:
    MoveItemsCommandPrivate(const Scope &items = Scope(), const Scope &dest = Scope())
        : CommandPrivate(Command::MoveItems)
        , items(items)
        , dest(dest)
    {}

    Scope items;
    Scope dest;
};

} // namespace Protocol
} // namespace Akonadi

AKONADI_DECLARE_PRIVATE(MoveItemsCommand)

MoveItemsCommand::MoveItemsCommand()
    : Command(new MoveItemsCommandPrivate)
{
}

MoveItemsCommand::MoveItemsCommand(const Scope &items, const Scope &dest)
    : Command(new MoveItemsCommandPrivate(items, dest))
{
}

Scope MoveItemsCommand::items() const
{
    return d_func()->items;
}
Scope MoveItemsCommand::destination() const
{
    return d_func()->dest;
}

void MoveItemsCommand::serialize(QDataStream &stream) const
{
    stream << *this;
}
void MoveItemsCommand::deserialize(QDataStream &stream)
{
    stream >> *this;
}

QDataStream &operator<<(QDataStream &stream, const MoveItemsCommand &command)
{
    return stream << command.d_func()->items
                  << command.d_func()->dest;
}

QDataStream &operator>>(QDataStream &stream, MoveItemsCommand &command)
{
    return stream >> command.d_func()->items
                  >> command.d_func()->dest;
}



/****************************************************************************/



MoveItemsResponse::MoveItemsResponse()
    : Response(new ResponsePrivate(Command::MoveItems))
{
}



/****************************************************************************/



namespace Akonadi
{
namespace Protocol
{

class CreateCollectionCommandPrivate : public CommandPrivate
{
public:
    CreateCollectionCommandPrivate()
        : CommandPrivate(Command::CreateCollection)
        , sync(Tristate::Undefined)
        , display(Tristate::Undefined)
        , index(Tristate::Undefined)
        , enabled(true)
        , isVirtual(false)
    {}

    Scope parent;
    QString name;
    QString remoteId;
    QString remoteRev;
    QStringList mimeTypes;
    CachePolicy cachePolicy;
    Attributes attributes;
    Tristate sync;
    Tristate display;
    Tristate index;
    bool enabled;
    bool isVirtual;
};

} // namespace Protocol
} // namespace Akonadi

AKONADI_DECLARE_PRIVATE(CreateCollectionCommand)

CreateCollectionCommand::CreateCollectionCommand()
    : Command(new CreateCollectionCommandPrivate)
{
}

void CreateCollectionCommand::setParent(const Scope &parent)
{
    d_func()->parent = parent;
}
Scope CreateCollectionCommand::parent() const
{
    return d_func()->parent;
}

void CreateCollectionCommand::setName(const QString &name)
{
    d_func()->name = name;
}
QString CreateCollectionCommand::name() const
{
    return d_func()->name;
}

void CreateCollectionCommand::setRemoteId(const QString &remoteId)
{
    d_func()->remoteId = remoteId;
}
QString CreateCollectionCommand::remoteId() const
{
    return d_func()->remoteId;
}

void CreateCollectionCommand::setRemoteRevision(const QString &remoteRevision)
{
    d_func()->remoteRev = remoteRevision;
}
QString CreateCollectionCommand::remoteRevision() const
{
    return d_func()->remoteRev;
}

void CreateCollectionCommand::setMimeTypes(const QStringList &mimeTypes)
{
    d_func()->mimeTypes = mimeTypes;
}
QStringList CreateCollectionCommand::mimeTypes() const
{
    return d_func()->mimeTypes;
}

void CreateCollectionCommand::setCachePolicy(const CachePolicy &cachePolicy)
{
    d_func()->cachePolicy = cachePolicy;
}
CachePolicy CreateCollectionCommand::cachePolicy() const
{
    return d_func()->cachePolicy;
}

void CreateCollectionCommand::setAttributes(const Attributes &attributes)
{
    d_func()->attributes = attributes;
}
Attributes CreateCollectionCommand::attributes() const
{
    return d_func()->attributes;
}

void CreateCollectionCommand::setIsVirtual(bool isVirtual)
{
    d_func()->isVirtual = isVirtual;
}
bool CreateCollectionCommand::isVirtual() const
{
    return d_func()->isVirtual;
}

void CreateCollectionCommand::setEnabled(bool enabled)
{
    d_func()->enabled = enabled;
}
bool CreateCollectionCommand::enabled() const
{
    return d_func()->enabled;
}

void CreateCollectionCommand::setSyncPref(Tristate sync)
{
    d_func()->sync = sync;
}
Tristate CreateCollectionCommand::syncPref() const
{
    return d_func()->sync;
}

void CreateCollectionCommand::setDisplayPref(Tristate display)
{
    d_func()->display = display;
}
Tristate CreateCollectionCommand::displayPref() const
{
    return d_func()->display;
}

void CreateCollectionCommand::setIndexPref(Tristate index)
{
    d_func()->index = index;
}
Tristate CreateCollectionCommand::indexPref() const
{
    return d_func()->index;
}

void CreateCollectionCommand::serialize(QDataStream &stream) const
{
    stream << *this;
}
void CreateCollectionCommand::deserialize(QDataStream &stream)
{
    stream >> *this;
}

QDataStream &operator<<(QDataStream &stream, const CreateCollectionCommand &command)
{
    return stream << command.d_func()->parent
                  << command.d_func()->name
                  << command.d_func()->remoteId
                  << command.d_func()->remoteRev
                  << command.d_func()->mimeTypes
                  << command.d_func()->cachePolicy
                  << command.d_func()->attributes
                  << command.d_func()->enabled
                  << command.d_func()->sync
                  << command.d_func()->display
                  << command.d_func()->index
                  << command.d_func()->isVirtual;
}

QDataStream &operator>>(QDataStream &stream, CreateCollectionCommand &command)
{
    return stream >> command.d_func()->parent
                  >> command.d_func()->name
                  >> command.d_func()->remoteId
                  >> command.d_func()->remoteRev
                  >> command.d_func()->mimeTypes
                  >> command.d_func()->cachePolicy
                  >> command.d_func()->attributes
                  >> command.d_func()->enabled
                  >> command.d_func()->sync
                  >> command.d_func()->display
                  >> command.d_func()->index
                  >> command.d_func()->isVirtual;
}




/****************************************************************************/




CreateCollectionResponse::CreateCollectionResponse()
    : Response(new ResponsePrivate(Command::CreateCollection))
{
}



/****************************************************************************/



namespace Akonadi
{
namespace Protocol
{

class CopyCollectionCommandPrivate : public CommandPrivate
{
public:
    CopyCollectionCommandPrivate(const Scope &collection = Scope(),
                                 const Scope &dest = Scope())
        : CommandPrivate(Command::CopyCollection)
        , collection(collection)
        , dest(dest)
    {}

    Scope collection;
    Scope dest;
};

} // namespace Protocol
} // namespace Akonadi

AKONADI_DECLARE_PRIVATE(CopyCollectionCommand)

CopyCollectionCommand::CopyCollectionCommand()
    : Command(new CopyCollectionCommandPrivate)
{
}

CopyCollectionCommand::CopyCollectionCommand(const Scope &collection,
                                             const Scope &destination)
    : Command(new CopyCollectionCommandPrivate(collection, destination))
{
}

Scope CopyCollectionCommand::collection() const
{
    return d_func()->collection;
}
Scope CopyCollectionCommand::destination() const
{
    return d_func()->dest;
}

void CopyCollectionCommand::serialize(QDataStream &stream) const
{
    stream << *this;
}
void CopyCollectionCommand::deserialize(QDataStream &stream)
{
    stream >> *this;
}

QDataStream &operator<<(QDataStream &stream, const CopyCollectionCommand &command)
{
    return stream << command.d_func()->collection
                  << command.d_func()->dest;
}

QDataStream &operator>>(QDataStream &stream, CopyCollectionCommand &command)
{
    return stream >> command.d_func()->collection
                  >> command.d_func()->dest;
}



/****************************************************************************/




CopyCollectionResponse::CopyCollectionResponse()
    : Response(new ResponsePrivate(Command::CopyCollection))
{
}



/****************************************************************************/



namespace Akonadi
{
namespace Protocol
{

class DeleteCollectionCommandPrivate : public CommandPrivate
{
public:
    DeleteCollectionCommandPrivate(const Scope &col = Scope())
        : CommandPrivate(Command::DeleteCollection)
        , collection(col)
    {}

    Scope collection;
};

} // namespace Protocol
} // namespace Akonadi

AKONADI_DECLARE_PRIVATE(DeleteCollectionCommand)

DeleteCollectionCommand::DeleteCollectionCommand()
    : Command(new DeleteCollectionCommandPrivate)
{
}

DeleteCollectionCommand::DeleteCollectionCommand(const Scope &collection)
    : Command(new DeleteCollectionCommandPrivate(collection))
{
}

Scope DeleteCollectionCommand::collection() const
{
    return d_func()->collection;
}

void DeleteCollectionCommand::serialize(QDataStream &stream) const
{
    stream << *this;
}
void DeleteCollectionCommand::deserialize(QDataStream &stream)
{
    stream >> *this;
}

QDataStream &operator<<(QDataStream &stream, const DeleteCollectionCommand &command)
{
    return stream << command.d_func()->collection;
}

QDataStream &operator>>(QDataStream &stream, DeleteCollectionCommand &command)
{
    return stream >> command.d_func()->collection;
}



/****************************************************************************/




DeleteCollectionResponse::DeleteCollectionResponse()
    : Response(new ResponsePrivate(Command::DeleteCollection))
{
}



/****************************************************************************/



namespace Akonadi
{
namespace Protocol
{

class FetchCollectionStatsCommandPrivate : public CommandPrivate
{
public:
    FetchCollectionStatsCommandPrivate(const Scope &collection = Scope())
        : CommandPrivate(Command::FetchCollectionStats)
        , collection(collection)
    {}

    Scope collection;
};

} // namespace Protocol
} // namespace Akonadi

AKONADI_DECLARE_PRIVATE(FetchCollectionStatsCommand)

FetchCollectionStatsCommand::FetchCollectionStatsCommand()
    : Command(new FetchCollectionStatsCommandPrivate)
{
}

FetchCollectionStatsCommand::FetchCollectionStatsCommand(const Scope &collection)
    : Command(new FetchCollectionStatsCommandPrivate(collection))
{
}

Scope FetchCollectionStatsCommand::collection() const
{
    return d_func()->collection;
}

void FetchCollectionStatsCommand::serialize(QDataStream &stream) const
{
    stream << *this;
}
void FetchCollectionStatsCommand::deserialize(QDataStream &stream)
{
    stream >> *this;
}

QDataStream &operator<<(QDataStream &stream, const FetchCollectionStatsCommand &command)
{
    return stream << command.d_func()->collection;
}

QDataStream &operator>>(QDataStream &stream, FetchCollectionStatsCommand &command)
{
    return stream >> command.d_func()->collection;
}



/****************************************************************************/



namespace Akonadi
{
namespace Protocol
{

class FetchCollectionStatsResponsePrivate : public ResponsePrivate
{
public:
    FetchCollectionStatsResponsePrivate(qint64 count = -1,
                                        qint64 unseen = -1,
                                        qint64 size = -1)
        : ResponsePrivate(Command::FetchCollectionStats)
        , count(count)
        , unseen(unseen)
        , size(size)
    {}

    qint64 count;
    qint64 unseen;
    qint64 size;
};

} // namespace Protocol
} // namespace Akonadi

AKONADI_DECLARE_PRIVATE(FetchCollectionStatsResponse)

FetchCollectionStatsResponse::FetchCollectionStatsResponse()
    : Response(new FetchCollectionStatsResponsePrivate)
{
}

FetchCollectionStatsResponse::FetchCollectionStatsResponse(qint64 count,
                                                           qint64 unseen,
                                                           qint64 size)
    : Response(new FetchCollectionStatsResponsePrivate(count, unseen, size))
{
}

qint64 FetchCollectionStatsResponse::count() const
{
    return d_func()->count;
}
qint64 FetchCollectionStatsResponse::unseen() const
{
    return d_func()->unseen;
}
qint64 FetchCollectionStatsResponse::size() const
{
    return d_func()->size;
}

void FetchCollectionStatsResponse::serialize(QDataStream &stream) const
{
    stream << *this;
}
void FetchCollectionStatsResponse::deserialize(QDataStream &stream)
{
    stream >> *this;
}

QDataStream &operator<<(QDataStream &stream, const FetchCollectionStatsResponse &command)
{
    return stream << command.d_func()->count
                  << command.d_func()->unseen
                  << command.d_func()->size;
}

QDataStream &operator>>(QDataStream &stream, FetchCollectionStatsResponse &command)
{
    return stream >> command.d_func()->count
                  >> command.d_func()->unseen
                  >> command.d_func()->size;
}




/****************************************************************************/



namespace Akonadi
{
namespace Protocol
{

class FetchCollectionsCommandPrivate : public CommandPrivate
{
public:
    FetchCollectionsCommandPrivate(const Scope &collections = Scope())
        : CommandPrivate(Command::FetchCollections)
        , collections(collections)
        , depth(0)
        , ancestorsDepth(-1)
        , enabled(false)
        , sync(false)
        , display(false)
        , index(false)
        , stats(false)
    {}

    Scope collections;
    QString resource;
    QStringList mimeTypes;
    QVector<QByteArray> ancestorsAttributes;
    int depth;
    int ancestorsDepth;
    bool enabled;
    bool sync;
    bool display;
    bool index;
    bool stats;
};

} // namespace Protocol
} // namespace Akonadi

AKONADI_DECLARE_PRIVATE(FetchCollectionsCommand)

FetchCollectionsCommand::FetchCollectionsCommand()
    : Command(new FetchCollectionsCommandPrivate)
{
}

FetchCollectionsCommand::FetchCollectionsCommand(const Scope &collections)
    : Command(new FetchCollectionsCommandPrivate(collections))
{
}

Scope FetchCollectionsCommand::collections() const
{
    return d_func()->collections;
}

void FetchCollectionsCommand::setDepth(int depth)
{
    d_func()->depth = depth;
}
int FetchCollectionsCommand::depth() const
{
    return d_func()->depth;
}

void FetchCollectionsCommand::setResource(const QString &resourceId)
{
    d_func()->resource = resourceId;
}
QString FetchCollectionsCommand::resource() const
{
    return d_func()->resource;
}

void FetchCollectionsCommand::setMimeTypes(const QStringList &mimeTypes)
{
    d_func()->mimeTypes = mimeTypes;
}
QStringList FetchCollectionsCommand::mimeTypes() const
{
    return d_func()->mimeTypes;
}

void FetchCollectionsCommand::setAncestorsDepth(int depth)
{
    d_func()->ancestorsDepth = depth;
}
int FetchCollectionsCommand::ancestorsDepth() const
{
    return d_func()->ancestorsDepth;
}

void FetchCollectionsCommand::setAncestorsAttributes(const QVector<QByteArray> &attributes)
{
    d_func()->ancestorsAttributes = attributes;
}
QVector<QByteArray> FetchCollectionsCommand::ancestorsAttributes() const
{
    return d_func()->ancestorsAttributes;
}

void FetchCollectionsCommand::setEnabled(bool enabled)
{
    d_func()->enabled = enabled;
}
bool FetchCollectionsCommand::enabled() const
{
    return d_func()->enabled;
}

void FetchCollectionsCommand::setSyncPref(bool sync)
{
    d_func()->sync = sync;
}
bool FetchCollectionsCommand::syncPref() const
{
    return d_func()->sync;
}

void FetchCollectionsCommand::setDisplayPref(bool display)
{
    d_func()->display = display;
}
bool FetchCollectionsCommand::displayPref() const
{
    return d_func()->display;
}

void FetchCollectionsCommand::setIndexPref(bool index)
{
    d_func()->index = index;
}
bool FetchCollectionsCommand::indexPref() const
{
    return d_func()->index;
}

void FetchCollectionsCommand::setFetchStats(bool stats)
{
    d_func()->stats = stats;
}
bool FetchCollectionsCommand::fetchStats() const
{
    return d_func()->stats;
}

void FetchCollectionsCommand::serialize(QDataStream &stream) const
{
    stream << *this;
}
void FetchCollectionsCommand::deserialize(QDataStream &stream)
{
    stream >> *this;
}

QDataStream &operator<<(QDataStream &stream, const FetchCollectionsCommand &command)
{
    return stream << command.d_func()->collections
                  << command.d_func()->resource
                  << command.d_func()->mimeTypes
                  << command.d_func()->depth
                  << command.d_func()->ancestorsDepth
                  << command.d_func()->ancestorsAttributes
                  << command.d_func()->enabled
                  << command.d_func()->sync
                  << command.d_func()->display
                  << command.d_func()->index
                  << command.d_func()->stats;
}

QDataStream &operator>>(QDataStream &stream, FetchCollectionsCommand &command)
{
    return stream >> command.d_func()->collections
                  >> command.d_func()->resource
                  >> command.d_func()->mimeTypes
                  >> command.d_func()->depth
                  >> command.d_func()->ancestorsDepth
                  >> command.d_func()->ancestorsAttributes
                  >> command.d_func()->enabled
                  >> command.d_func()->sync
                  >> command.d_func()->display
                  >> command.d_func()->index
                  >> command.d_func()->stats;
}



/****************************************************************************/


namespace Akonadi
{
namespace Protocol
{

class FetchCollectionsResponsePrivate : public ResponsePrivate
{
public:
    FetchCollectionsResponsePrivate(qint64 id = -1)
        : ResponsePrivate(Command::FetchCollections)
        , id(id)
        , parentId(-1)
        , display(Tristate::Undefined)
        , sync(Tristate::Undefined)
        , index(Tristate::Undefined)
        , isVirtual(false)
        , referenced(false)
        , enabled(true)
    {}

    QString name;
    QString remoteId;
    QString remoteRev;
    QString resource;
    QStringList mimeTypes;
    FetchCollectionStatsResponse stats;
    QString searchQuery;
    QVector<qint64> searchCols;
    QVector<Ancestor> ancestors;
    CachePolicy cachePolicy;
    Attributes attributes;
    qint64 id;
    qint64 parentId;
    Tristate display;
    Tristate sync;
    Tristate index;
    bool isVirtual;
    bool referenced;
    bool enabled;
};

} // namespace Protocol
} // namespace Akonadi

AKONADI_DECLARE_PRIVATE(FetchCollectionsResponse)

FetchCollectionsResponse::FetchCollectionsResponse()
    : Response(new FetchCollectionsResponsePrivate)
{
}

FetchCollectionsResponse::FetchCollectionsResponse(qint64 id)
    : Response(new FetchCollectionsResponsePrivate(id))
{
}

qint64 FetchCollectionsResponse::id() const
{
    return d_func()->id;
}

void FetchCollectionsResponse::setParentId(qint64 parentId)
{
    d_func()->parentId = parentId;
}
qint64 FetchCollectionsResponse::parentId() const
{
    return d_func()->parentId;
}

void FetchCollectionsResponse::setName(const QString &name)
{
    d_func()->name = name;
}
QString FetchCollectionsResponse::name() const
{
    return d_func()->name;
}

void FetchCollectionsResponse::setMimeTypes(const QStringList &mimeTypes)
{
    d_func()->mimeTypes = mimeTypes;
}
QStringList FetchCollectionsResponse::mimeTypes() const
{
    return d_func()->mimeTypes;
}

void FetchCollectionsResponse::setRemoteId(const QString &remoteId)
{
    d_func()->remoteId = remoteId;
}
QString FetchCollectionsResponse::remoteId() const
{
    return d_func()->remoteId;
}

void FetchCollectionsResponse::setRemoteRevision(const QString &remoteRevision)
{
    d_func()->remoteRev = remoteRevision;
}
QString FetchCollectionsResponse::remoteRevision() const
{
    return d_func()->remoteRev;
}

void FetchCollectionsResponse::setResource(const QString &resourceId)
{
    d_func()->resource = resourceId;
}
QString FetchCollectionsResponse::resource() const
{
    return d_func()->resource;
}

void FetchCollectionsResponse::setStatistics(const FetchCollectionStatsResponse &stats)
{
    d_func()->stats = stats;
}
FetchCollectionStatsResponse FetchCollectionsResponse::statistics() const
{
    return d_func()->stats;
}

void FetchCollectionsResponse::setSearchQuery(const QString &query)
{
    d_func()->searchQuery = query;
}
QString FetchCollectionsResponse::searchQuery() const
{
    return d_func()->searchQuery;
}

void FetchCollectionsResponse::setSearchCollections(const QVector<qint64> &searchCols)
{
    d_func()->searchCols = searchCols;
}
QVector<qint64> FetchCollectionsResponse::searchCollections() const
{
    return d_func()->searchCols;
}

void FetchCollectionsResponse::setAncestors(const QVector<Ancestor> &ancestors)
{
    d_func()->ancestors = ancestors;
}
QVector<Ancestor> FetchCollectionsResponse::ancestors() const
{
    return d_func()->ancestors;
}

void FetchCollectionsResponse::setCachePolicy(const CachePolicy &cachePolicy)
{
    d_func()->cachePolicy = cachePolicy;
}
CachePolicy FetchCollectionsResponse::cachePolicy() const
{
    return d_func()->cachePolicy;
}

void FetchCollectionsResponse::setAttributes(const Attributes &attrs)
{
    d_func()->attributes = attrs;
}
Attributes FetchCollectionsResponse::attributes() const
{
    return d_func()->attributes;
}

void FetchCollectionsResponse::setEnabled(bool enabled)
{
    d_func()->enabled = enabled;
}
bool FetchCollectionsResponse::enabled() const
{
    return d_func()->enabled;
}

void FetchCollectionsResponse::setDisplayPref(Tristate display)
{
    d_func()->display = display;
}
Tristate FetchCollectionsResponse::displayPref() const
{
    return d_func()->display;
}

void FetchCollectionsResponse::setSyncPref(Tristate sync)
{
    d_func()->sync = sync;
}
Tristate FetchCollectionsResponse::syncPref() const
{
    return d_func()->sync;
}

void FetchCollectionsResponse::setIndexPref(Tristate index)
{
    d_func()->index = index;
}
Tristate FetchCollectionsResponse::indexPref() const
{
    return d_func()->index;
}

void FetchCollectionsResponse::setReferenced(bool referenced)
{
    d_func()->referenced = referenced;
}
bool FetchCollectionsResponse::referenced() const
{
    return d_func()->referenced;
}

void FetchCollectionsResponse::setIsVirtual(bool isVirtual)
{
    d_func()->isVirtual = isVirtual;
}
bool FetchCollectionsResponse::isVirtual() const
{
    return d_func()->isVirtual;
}

void FetchCollectionsResponse::serialize(QDataStream &stream) const
{
    stream << *this;
}
void FetchCollectionsResponse::deserialize(QDataStream &stream)
{
    stream >> *this;
}

QDataStream &operator<<(QDataStream &stream, const FetchCollectionsResponse &command)
{
    return stream << command.d_func()->id
                  << command.d_func()->parentId
                  << command.d_func()->name
                  << command.d_func()->mimeTypes
                  << command.d_func()->remoteId
                  << command.d_func()->remoteRev
                  << command.d_func()->resource
                  << command.d_func()->stats
                  << command.d_func()->searchQuery
                  << command.d_func()->searchCols
                  << command.d_func()->ancestors
                  << command.d_func()->cachePolicy
                  << command.d_func()->attributes
                  << command.d_func()->display
                  << command.d_func()->sync
                  << command.d_func()->index
                  << command.d_func()->isVirtual
                  << command.d_func()->referenced
                  << command.d_func()->enabled;
}

QDataStream &operator>>(QDataStream &stream, FetchCollectionsResponse &command)
{
    return stream >> command.d_func()->id
                  >> command.d_func()->parentId
                  >> command.d_func()->name
                  >> command.d_func()->mimeTypes
                  >> command.d_func()->remoteId
                  >> command.d_func()->remoteRev
                  >> command.d_func()->resource
                  >> command.d_func()->stats
                  >> command.d_func()->searchQuery
                  >> command.d_func()->searchCols
                  >> command.d_func()->ancestors
                  >> command.d_func()->cachePolicy
                  >> command.d_func()->attributes
                  >> command.d_func()->display
                  >> command.d_func()->sync
                  >> command.d_func()->index
                  >> command.d_func()->isVirtual
                  >> command.d_func()->referenced
                  >> command.d_func()->enabled;
}



/****************************************************************************/



namespace Akonadi
{
namespace Protocol
{

class ModifyCollectionCommandPrivate : public CommandPrivate
{
public:
    ModifyCollectionCommandPrivate(const Scope &collection = Scope())
        : CommandPrivate(Command::ModifyCollection)
        , collection(collection)
        , parentId(-1)
        , sync(Tristate::Undefined)
        , display(Tristate::Undefined)
        , index(Tristate::Undefined)
        , enabled(true)
        , referenced(false)
        , persistentSearchRemote(false)
        , persistentSearchRecursive(false)
        , modifiedParts(ModifyCollectionCommand::None)
    {}

    Scope collection;
    QStringList mimeTypes;
    Protocol::CachePolicy cachePolicy;
    QString name;
    QString remoteId;
    QString remoteRev;
    QString persistentSearchQuery;
    QVector<qint64> persistentSearchCols;
    QSet<QByteArray> removedAttributes;
    Attributes attributes;
    qint64 parentId;
    Tristate sync;
    Tristate display;
    Tristate index;
    bool enabled;
    bool referenced;
    bool persistentSearchRemote;
    bool persistentSearchRecursive;

    ModifyCollectionCommand::ModifiedParts modifiedParts;
};

} // namespace Protocol
} // namespace Akonadi

AKONADI_DECLARE_PRIVATE(ModifyCollectionCommand)

ModifyCollectionCommand::ModifyCollectionCommand()
    : Command(new ModifyCollectionCommandPrivate)
{
}

ModifyCollectionCommand::ModifyCollectionCommand(const Scope &collection)
    : Command(new ModifyCollectionCommandPrivate(collection))
{
}

Scope ModifyCollectionCommand::collection() const
{
    return d_func()->collection;
}

ModifyCollectionCommand::ModifiedParts ModifyCollectionCommand::modifiedParts() const
{
    return d_func()->modifiedParts;
}

void ModifyCollectionCommand::setParentId(qint64 parentId)
{
    d_func()->parentId = parentId;
}
qint64 ModifyCollectionCommand::parentId() const
{
    return d_func()->parentId;
}

void ModifyCollectionCommand::setMimeTypes(const QStringList &mimeTypes)
{
    d_func()->modifiedParts |= MimeTypes;
    d_func()->modifiedParts |= PersistentSearch;
    d_func()->mimeTypes = mimeTypes;
}
QStringList ModifyCollectionCommand::mimeTypes() const
{
    return d_func()->mimeTypes;
}

void ModifyCollectionCommand::setCachePolicy(const Protocol::CachePolicy &cachePolicy)
{
    d_func()->modifiedParts |= CachePolicy;
    d_func()->cachePolicy = cachePolicy;
}
Protocol::CachePolicy ModifyCollectionCommand::cachePolicy() const
{
    return d_func()->cachePolicy;
}

void ModifyCollectionCommand::setName(const QString &name)
{
    d_func()->modifiedParts |= Name;
    d_func()->name = name;
}
QString ModifyCollectionCommand::name() const
{
    return d_func()->name;
}

void ModifyCollectionCommand::setRemoteId(const QString &remoteId)
{
    d_func()->modifiedParts |= RemoteID;
    d_func()->remoteId = remoteId;
}
QString ModifyCollectionCommand::remoteId() const
{
    return d_func()->remoteId;
}

void ModifyCollectionCommand::setRemoteRevision(const QString &remoteRevision)
{
    d_func()->modifiedParts |= RemoteRevision;
    d_func()->remoteRev = remoteRevision;
}
QString ModifyCollectionCommand::remoteRevision() const
{
    return d_func()->remoteRev;
}

void ModifyCollectionCommand::setPersistentSearchQuery(const QString &query)
{
    d_func()->modifiedParts |= PersistentSearch;
    d_func()->persistentSearchQuery = query;
}
QString ModifyCollectionCommand::persistentSearchQuery() const
{
    return d_func()->persistentSearchQuery;
}

void ModifyCollectionCommand::setPersistentSearchCollections(const QVector<qint64> &cols)
{
    d_func()->modifiedParts |= PersistentSearch;
    d_func()->persistentSearchCols = cols;
}
QVector<qint64> ModifyCollectionCommand::persistentSearchCollections() const
{
    return d_func()->persistentSearchCols;
}

void ModifyCollectionCommand::setPersistentSearchRemote(bool remote)
{
    d_func()->modifiedParts |= PersistentSearch;
    d_func()->persistentSearchRemote = remote;
}
bool ModifyCollectionCommand::persistentSearchRemote() const
{
    return d_func()->persistentSearchRemote;
}

void ModifyCollectionCommand::setPersistentSearchRecursive(bool recursive)
{
    d_func()->modifiedParts |= PersistentSearch;
    d_func()->persistentSearchRecursive = recursive;
}
bool ModifyCollectionCommand::persistentSearchRecursive() const
{
    return d_func()->persistentSearchRecursive;
}

void ModifyCollectionCommand::setRemovedAttributes(const QSet<QByteArray> &removedAttrs)
{
    d_func()->modifiedParts |= RemovedAttributes;
    d_func()->removedAttributes = removedAttrs;
}
QSet<QByteArray> ModifyCollectionCommand::removedAttributes() const
{
    return d_func()->removedAttributes;
}

void ModifyCollectionCommand::setAttributes(const Protocol::Attributes &attributes)
{
    d_func()->modifiedParts |= Attributes;
    d_func()->attributes = attributes;
}
Attributes ModifyCollectionCommand::attributes() const
{
    return d_func()->attributes;
}

void ModifyCollectionCommand::setEnabled(bool enabled)
{
    d_func()->modifiedParts |= ListPreferences;
    d_func()->enabled = enabled;
}
bool ModifyCollectionCommand::enabled() const
{
    return d_func()->enabled;
}

void ModifyCollectionCommand::setSyncPref(Tristate sync)
{
    d_func()->modifiedParts |= ListPreferences;
    d_func()->sync = sync;
}
Tristate ModifyCollectionCommand::syncPref() const
{
    return d_func()->sync;
}

void ModifyCollectionCommand::setDisplayPref(Tristate display)
{
    d_func()->modifiedParts |= ListPreferences;
    d_func()->display = display;
}
Tristate ModifyCollectionCommand::displayPref() const
{
    return d_func()->display;
}

void ModifyCollectionCommand::setIndexPref(Tristate index)
{
    d_func()->modifiedParts |= ListPreferences;
    d_func()->index = index;
}
Tristate ModifyCollectionCommand::indexPref() const
{
    return d_func()->index;
}

void ModifyCollectionCommand::setReferenced(bool referenced)
{
    d_func()->modifiedParts |= Referenced;
    d_func()->referenced = referenced;
}
bool ModifyCollectionCommand::referenced() const
{
    return d_func()->referenced;
}

void ModifyCollectionCommand::serialize(QDataStream &stream) const
{
    stream << *this;
}
void ModifyCollectionCommand::deserialize(QDataStream &stream)
{
    stream >> *this;
}

QDataStream &operator<<(QDataStream &stream, const ModifyCollectionCommand &command)
{
    stream << command.d_func()->collection
           << command.d_func()->modifiedParts;

    if (command.d_func()->modifiedParts & ModifyCollectionCommand::Name) {
        stream << command.d_func()->name;
    }
    if (command.d_func()->modifiedParts & ModifyCollectionCommand::RemoteID) {
        stream << command.d_func()->remoteId;
    }
    if (command.d_func()->modifiedParts & ModifyCollectionCommand::RemoteRevision) {
        stream << command.d_func()->remoteRev;
    }
    if (command.d_func()->modifiedParts & ModifyCollectionCommand::ParentID) {
        stream << command.d_func()->parentId;
    }
    if (command.d_func()->modifiedParts & ModifyCollectionCommand::MimeTypes) {
        stream << command.d_func()->mimeTypes;
    }
    if (command.d_func()->modifiedParts & ModifyCollectionCommand::CachePolicy) {
        stream << command.d_func()->cachePolicy;
    }
    if (command.d_func()->modifiedParts & ModifyCollectionCommand::PersistentSearch) {
        stream << command.d_func()->persistentSearchQuery
               << command.d_func()->persistentSearchCols
               << command.d_func()->persistentSearchRemote
               << command.d_func()->persistentSearchRecursive;
    }
    if (command.d_func()->modifiedParts & ModifyCollectionCommand::RemovedAttributes) {
        stream << command.d_func()->removedAttributes;
    }
    if (command.d_func()->modifiedParts & ModifyCollectionCommand::Attributes) {
        stream << command.d_func()->attributes;
    }
    if (command.d_func()->modifiedParts & ModifyCollectionCommand::ListPreferences) {
        stream << command.d_func()->enabled
               << command.d_func()->sync
               << command.d_func()->display
               << command.d_func()->index;
    }
    if (command.d_func()->modifiedParts & ModifyCollectionCommand::Referenced) {
        stream << command.d_func()->referenced;
    }
    return stream;
}

QDataStream &operator>>(QDataStream &stream, ModifyCollectionCommand &command)
{
    stream >> command.d_func()->collection
           >> command.d_func()->modifiedParts;

    if (command.d_func()->modifiedParts & ModifyCollectionCommand::Name) {
        stream >> command.d_func()->name;
    }
    if (command.d_func()->modifiedParts & ModifyCollectionCommand::RemoteID) {
        stream >> command.d_func()->remoteId;
    }
    if (command.d_func()->modifiedParts & ModifyCollectionCommand::RemoteRevision) {
        stream >> command.d_func()->remoteRev;
    }
    if (command.d_func()->modifiedParts & ModifyCollectionCommand::ParentID) {
        stream >> command.d_func()->parentId;
    }
    if (command.d_func()->modifiedParts & ModifyCollectionCommand::MimeTypes) {
        stream >> command.d_func()->mimeTypes;
    }
    if (command.d_func()->modifiedParts & ModifyCollectionCommand::CachePolicy) {
        stream >> command.d_func()->cachePolicy;
    }
    if (command.d_func()->modifiedParts & ModifyCollectionCommand::PersistentSearch) {
        stream >> command.d_func()->persistentSearchQuery
               >> command.d_func()->persistentSearchCols
               >> command.d_func()->persistentSearchRemote
               >> command.d_func()->persistentSearchRecursive;
    }
    if (command.d_func()->modifiedParts & ModifyCollectionCommand::RemovedAttributes) {
        stream >> command.d_func()->removedAttributes;
    }
    if (command.d_func()->modifiedParts & ModifyCollectionCommand::Attributes) {
        stream >> command.d_func()->attributes;
    }
    if (command.d_func()->modifiedParts & ModifyCollectionCommand::ListPreferences) {
        stream >> command.d_func()->enabled
               >> command.d_func()->sync
               >> command.d_func()->display
               >> command.d_func()->index;
    }
    if (command.d_func()->modifiedParts & ModifyCollectionCommand::Referenced) {
        stream >> command.d_func()->referenced;
    }
    return stream;
}




/****************************************************************************/



ModifyCollectionResponse::ModifyCollectionResponse()
    : Response(new ResponsePrivate(Command::ModifyCollection))
{
}



/****************************************************************************/



namespace Akonadi
{
namespace Protocol
{

class MoveCollectionCommandPrivate : public CommandPrivate
{
public:
    MoveCollectionCommandPrivate(const Scope &collection = Scope(),
                                 const Scope &dest = Scope())
        : CommandPrivate(Command::MoveCollection)
        , collection(collection)
        , dest(dest)
    {}

    Scope collection;
    Scope dest;
};

} // namespace Protocol
} // namespace Akonadi

AKONADI_DECLARE_PRIVATE(MoveCollectionCommand)

MoveCollectionCommand::MoveCollectionCommand()
    : Command(new MoveCollectionCommandPrivate)
{
}

MoveCollectionCommand::MoveCollectionCommand(const Scope &collection,
                                             const Scope &destination)
    : Command(new MoveCollectionCommandPrivate(collection, destination))
{
}

Scope MoveCollectionCommand::collection() const
{
    return d_func()->collection;
}
Scope MoveCollectionCommand::destination() const
{
    return d_func()->dest;
}

void MoveCollectionCommand::serialize(QDataStream &stream) const
{
    stream << *this;
}
void MoveCollectionCommand::deserialize(QDataStream &stream)
{
    stream >> *this;
}

QDataStream &operator<<(QDataStream &stream, const MoveCollectionCommand &command)
{
    return stream << command.d_func()->collection
                  << command.d_func()->dest;
}

QDataStream &operator>>(QDataStream &stream, MoveCollectionCommand &command)
{
    return stream >> command.d_func()->collection
                  >> command.d_func()->dest;
}



/****************************************************************************/




MoveCollectionResponse::MoveCollectionResponse()
    : Response(new ResponsePrivate(Command::MoveCollection))
{
}



/****************************************************************************/



namespace Akonadi
{
namespace Protocol
{

class SelectCollectionCommandPrivate : public CommandPrivate
{
public:
    SelectCollectionCommandPrivate(const Scope &collection = Scope())
        : CommandPrivate(Command::SelectCollection)
        , collection(collection)
    {}

    Scope collection;
};

} // namespace Protocol
} // namespace Akonadi

AKONADI_DECLARE_PRIVATE(SelectCollectionCommand)

SelectCollectionCommand::SelectCollectionCommand()
    : Command(new SelectCollectionCommandPrivate)
{
}

SelectCollectionCommand::SelectCollectionCommand(const Scope &collection)
    : Command(new SelectCollectionCommandPrivate(collection))
{
}

Scope SelectCollectionCommand::collection() const
{
    return d_func()->collection;
}

void SelectCollectionCommand::serialize(QDataStream &stream) const
{
    stream << *this;
}
void SelectCollectionCommand::deserialize(QDataStream &stream)
{
    stream >> *this;
}

QDataStream &operator<<(QDataStream &stream, const SelectCollectionCommand &command)
{
    return stream << command.d_func()->collection;
}

QDataStream &operator>>(QDataStream &stream, SelectCollectionCommand &command)
{
    return stream >> command.d_func()->collection;
}



/****************************************************************************/



SelectCollectionResponse::SelectCollectionResponse()
    : Response(new ResponsePrivate(Command::SelectCollection))
{
}



/****************************************************************************/



namespace Akonadi
{
namespace Protocol
{

class SearchCommandPrivate : public CommandPrivate
{
public:
    SearchCommandPrivate()
        : CommandPrivate(Command::Search)
        , recursive(false)
        , remote(false)
    {}

    QStringList mimeTypes;
    QVector<qint64> collections;
    QString query;
    FetchScope fetchScope;
    bool recursive;
    bool remote;
};

} // namespace Protocol
} // namespace Akonadi

AKONADI_DECLARE_PRIVATE(SearchCommand)

SearchCommand::SearchCommand()
    : Command(new SearchCommandPrivate)
{
}

void SearchCommand::setMimeTypes(const QStringList &mimeTypes)
{
    d_func()->mimeTypes = mimeTypes;
}
QStringList SearchCommand::mimeTypes() const
{
    return d_func()->mimeTypes;
}

void SearchCommand::setCollections(const QVector<qint64> &collections)
{
    d_func()->collections = collections;
}
QVector<qint64> SearchCommand::collections() const
{
    return d_func()->collections;
}

void SearchCommand::setQuery(const QString &query)
{
    d_func()->query = query;
}
QString SearchCommand::query() const
{
    return d_func()->query;
}

void SearchCommand::setFetchScope(const FetchScope &fetchScope)
{
    d_func()->fetchScope = fetchScope;
}
FetchScope SearchCommand::fetchScope() const
{
    return d_func()->fetchScope;
}

void SearchCommand::setRecursive(bool recursive)
{
    d_func()->recursive = recursive;
}
bool SearchCommand::recursive() const
{
    return d_func()->recursive;
}

void SearchCommand::setRemote(bool remote)
{
    d_func()->remote = remote;
}
bool SearchCommand::remote() const
{
    return d_func()->remote;
}

void SearchCommand::serialize(QDataStream &stream) const
{
    stream << *this;
}
void SearchCommand::deserialize(QDataStream &stream)
{
    stream >> *this;
}

QDataStream &operator<<(QDataStream &stream, const SearchCommand &command)
{
    return stream << command.d_func()->mimeTypes
                  << command.d_func()->collections
                  << command.d_func()->query
                  << command.d_func()->fetchScope
                  << command.d_func()->recursive
                  << command.d_func()->remote;
}

QDataStream &operator>>(QDataStream &stream, SearchCommand &command)
{
    return stream >> command.d_func()->mimeTypes
                  >> command.d_func()->collections
                  >> command.d_func()->query
                  >> command.d_func()->fetchScope
                  >> command.d_func()->recursive
                  >> command.d_func()->remote;
}



/****************************************************************************/



SearchResponse::SearchResponse()
    : Response(new ResponsePrivate(Command::Search))
{
}



/****************************************************************************/



namespace Akonadi
{
namespace Protocol
{

class SearchResultCommandPrivate : public CommandPrivate
{
public:
    SearchResultCommandPrivate(const QByteArray &searchId = QByteArray(),
                               qint64 collectionId = -1,
                               const Scope &result = Scope())
        : CommandPrivate(Command::SearchResult)
        , searchId(searchId)
        , result(result)
        , collectionId(collectionId)
    {}

    QByteArray searchId;
    Scope result;
    qint64 collectionId;
};

} // namespace Protocol
} // namespace Akonadi

AKONADI_DECLARE_PRIVATE(SearchResultCommand)

SearchResultCommand::SearchResultCommand()
    : Command(new SearchResultCommandPrivate)
{
}

SearchResultCommand::SearchResultCommand(const QByteArray &searchId,
                                         qint64 collectionId,
                                         const Scope &result)
    : Command(new SearchResultCommandPrivate(searchId, collectionId, result))
{
}

QByteArray SearchResultCommand::searchId() const
{
    return d_func()->searchId;
}

qint64 SearchResultCommand::collectionId() const
{
    return d_func()->collectionId;
}

Scope SearchResultCommand::result() const
{
    return d_func()->result;
}

void SearchResultCommand::serialize(QDataStream &stream) const
{
    stream << *this;
}
void SearchResultCommand::deserialize(QDataStream &stream)
{
    stream >> *this;
}

QDataStream &operator<<(QDataStream &stream, const SearchResultCommand &command)
{
    return stream << command.d_func()->searchId
                  << command.d_func()->collectionId
                  << command.d_func()->result;
}

QDataStream &operator>>(QDataStream &stream, SearchResultCommand &command)
{
    return stream >> command.d_func()->searchId
                  >> command.d_func()->collectionId
                  >> command.d_func()->result;
}




/****************************************************************************/




SearchResultResponse::SearchResultResponse()
    : Response(new ResponsePrivate(Command::SearchResult))
{
}



/****************************************************************************/



namespace Akonadi
{
namespace Protocol
{

class StoreSearchCommandPrivate : public CommandPrivate
{
public:
    StoreSearchCommandPrivate()
        : CommandPrivate(Command::StoreSearch)
        , remote(false)
        , recursive(false)
    {}

    QString name;
    QString query;
    QStringList mimeTypes;
    QVector<qint64> queryCols;
    bool remote;
    bool recursive;
};

} // namespace Protocol
} // namespace Akonadi

AKONADI_DECLARE_PRIVATE(StoreSearchCommand)

StoreSearchCommand::StoreSearchCommand()
    : Command(new StoreSearchCommandPrivate)
{
}

void StoreSearchCommand::setName(const QString &name)
{
    d_func()->name = name;
}
QString StoreSearchCommand::name() const
{
    return d_func()->name;
}

void StoreSearchCommand::setQuery(const QString &query)
{
    d_func()->query = query;
}
QString StoreSearchCommand::query() const
{
    return d_func()->query;
}

void StoreSearchCommand::setMimeTypes(const QStringList &mimeTypes)
{
    d_func()->mimeTypes = mimeTypes;
}
QStringList StoreSearchCommand::mimeTypes() const
{
    return d_func()->mimeTypes;
}

void StoreSearchCommand::setQueryCollections(const QVector<qint64> &queryCols)
{
    d_func()->queryCols = queryCols;
}
QVector<qint64> StoreSearchCommand::queryCollections() const
{
    return d_func()->queryCols;
}

void StoreSearchCommand::setRemote(bool remote)
{
    d_func()->remote = remote;
}
bool StoreSearchCommand::remote() const
{
    return d_func()->remote;
}

void StoreSearchCommand::setRecursive(bool recursive)
{
    d_func()->recursive = recursive;
}
bool StoreSearchCommand::recursive() const
{
    return d_func()->recursive;
}

void StoreSearchCommand::serialize(QDataStream &stream) const
{
    stream << *this;
}
void StoreSearchCommand::deserialize(QDataStream &stream)
{
    stream >> *this;
}

QDataStream &operator<<(QDataStream &stream, const StoreSearchCommand &command)
{
    return stream << command.d_func()->name
                  << command.d_func()->query
                  << command.d_func()->mimeTypes
                  << command.d_func()->queryCols
                  << command.d_func()->remote
                  << command.d_func()->recursive;
}

QDataStream &operator>>(QDataStream &stream, StoreSearchCommand &command)
{
    return stream >> command.d_func()->name
                  >> command.d_func()->query
                  >> command.d_func()->mimeTypes
                  >> command.d_func()->queryCols
                  >> command.d_func()->remote
                  >> command.d_func()->recursive;
}



/****************************************************************************/




StoreSearchResponse::StoreSearchResponse()
    : Response(new ResponsePrivate(Command::StoreSearch))
{
}



/****************************************************************************/



namespace Akonadi
{
namespace Protocol
{

class CreateTagCommandPrivate : public CommandPrivate
{
public:
    CreateTagCommandPrivate()
        : CommandPrivate(Command::CreateTag)
        , parentId(-1)
        , merge(false)
    {}

    QString gid;
    QString remoteId;
    QString type;
    Attributes attributes;
    qint64 parentId;
    bool merge;
};

} // namespace Protocol
} // namespace Akonadi

AKONADI_DECLARE_PRIVATE(CreateTagCommand)

CreateTagCommand::CreateTagCommand()
    : Command(new CreateTagCommandPrivate)
{
}

void CreateTagCommand::setGid(const QString &gid)
{
    d_func()->gid = gid;
}
QString CreateTagCommand::gid() const
{
    return d_func()->gid;
}

void CreateTagCommand::setRemoteId(const QString &remoteId)
{
    d_func()->remoteId = remoteId;
}
QString CreateTagCommand::remoteId() const
{
    return d_func()->remoteId;
}

void CreateTagCommand::setType(const QString &type)
{
    d_func()->type = type;
}
QString CreateTagCommand::type() const
{
    return d_func()->type;
}

void CreateTagCommand::setParentId(qint64 parentId)
{
    d_func()->parentId = parentId;
}
qint64 CreateTagCommand::parentId() const
{
    return d_func()->parentId;
}

void CreateTagCommand::setMerge(bool merge)
{
    d_func()->merge = merge;
}
bool CreateTagCommand::merge() const
{
    return d_func()->merge;
}

void CreateTagCommand::setAttributes(const Attributes &attributes)
{
    d_func()->attributes = attributes;
}
Attributes CreateTagCommand::attributes() const
{
    return d_func()->attributes;
}

void CreateTagCommand::serialize(QDataStream &stream) const
{
    stream << *this;
}
void CreateTagCommand::deserialize(QDataStream &stream)
{
    stream >> *this;
}

QDataStream &operator<<(QDataStream &stream, const CreateTagCommand &command)
{
    return stream << command.d_func()->gid
                  << command.d_func()->remoteId
                  << command.d_func()->type
                  << command.d_func()->attributes
                  << command.d_func()->parentId
                  << command.d_func()->merge;
}

QDataStream &operator>>(QDataStream &stream, CreateTagCommand &command)
{
    return stream >> command.d_func()->gid
                  >> command.d_func()->remoteId
                  >> command.d_func()->type
                  >> command.d_func()->attributes
                  >> command.d_func()->parentId
                  >> command.d_func()->merge;
}



/****************************************************************************/




CreateTagResponse::CreateTagResponse()
    : Response(new ResponsePrivate(Command::CreateTag))
{
}




/****************************************************************************/



namespace Akonadi
{
namespace Protocol
{

class DeleteTagCommandPrivate : public CommandPrivate
{
public:
    DeleteTagCommandPrivate(const Scope &tag = Scope())
        : CommandPrivate(Command::DeleteTag)
        , tag(tag)
    {}

    Scope tag;
};

} // namespace Protocol
} // namespace Akonadi

AKONADI_DECLARE_PRIVATE(DeleteTagCommand)

DeleteTagCommand::DeleteTagCommand()
    : Command(new DeleteTagCommandPrivate)
{
}

DeleteTagCommand::DeleteTagCommand(const Scope &tag)
    : Command(new DeleteTagCommandPrivate(tag))
{
}

Scope DeleteTagCommand::tag() const
{
    return d_func()->tag;
}

void DeleteTagCommand::serialize(QDataStream &stream) const
{
    stream << *this;
}
void DeleteTagCommand::deserialize(QDataStream &stream)
{
    stream >> *this;
}

QDataStream &operator<<(QDataStream &stream, const DeleteTagCommand &command)
{
    return stream << command.d_func()->tag;
}

QDataStream &operator>>(QDataStream &stream, DeleteTagCommand &command)
{
    return stream >> command.d_func()->tag;
}



/****************************************************************************/




DeleteTagResponse::DeleteTagResponse()
    : Response(new ResponsePrivate(Command::DeleteTag))
{
}



/****************************************************************************/




namespace Akonadi
{
namespace Protocol
{

class ModifyTagCommandPrivate : public CommandPrivate
{
public:
    ModifyTagCommandPrivate(qint64 tagId = -1)
        : CommandPrivate(Command::ModifyTag)
        , tagId(tagId)
        , parentId(-1)
        , modifiedParts(ModifyTagCommand::None)
    {}

    QString type;
    QString remoteId;
    QSet<QByteArray> removedAttributes;
    Attributes attributes;
    qint64 tagId;
    qint64 parentId;
    ModifyTagCommand::ModifiedParts modifiedParts;
};

} // namespace Protocol
} // namespace Akonadi

AKONADI_DECLARE_PRIVATE(ModifyTagCommand)

ModifyTagCommand::ModifyTagCommand()
    : Command(new ModifyTagCommandPrivate)
{
}

ModifyTagCommand::ModifyTagCommand(qint64 id)
    : Command(new ModifyTagCommandPrivate(id))
{
}

qint64 ModifyTagCommand::tagId() const
{
    return d_func()->tagId;
}

ModifyTagCommand::ModifiedParts ModifyTagCommand::modifiedParts() const
{
    return d_func()->modifiedParts;
}

void ModifyTagCommand::setParentId(qint64 parentId)
{
    d_func()->modifiedParts |= ParentId;
    d_func()->parentId = parentId;
}
qint64 ModifyTagCommand::parentId() const
{
    return d_func()->parentId;
}

void ModifyTagCommand::setType(const QString &type)
{
    d_func()->modifiedParts |= Type;
    d_func()->type = type;
}
QString ModifyTagCommand::type() const
{
    return d_func()->type;
}

void ModifyTagCommand::setRemoteId(const QString &remoteId)
{
    d_func()->modifiedParts |= RemoteId;
    d_func()->remoteId = remoteId;
}
QString ModifyTagCommand::remoteId() const
{
    return d_func()->remoteId;
}

void ModifyTagCommand::setRemovedAttributes(const QSet<QByteArray> &removed)
{
    d_func()->modifiedParts |= RemovedAttributes;
    d_func()->removedAttributes = removed;
}
QSet<QByteArray> ModifyTagCommand::removedAttributes() const
{
    return d_func()->removedAttributes;
}

void ModifyTagCommand::setAttributes(const Protocol::Attributes &attributes)
{
    d_func()->modifiedParts |= Attributes;
    d_func()->attributes = attributes;
}
Attributes ModifyTagCommand::attributes() const
{
    return d_func()->attributes;
}

void ModifyTagCommand::serialize(QDataStream &stream) const
{
    stream << *this;
}
void ModifyTagCommand::deserialize(QDataStream &stream)
{
    stream >> *this;
}

QDataStream &operator<<(QDataStream &stream, const ModifyTagCommand &command)
{
    stream << command.d_func()->tagId
           << command.d_func()->modifiedParts;
    if (command.d_func()->modifiedParts & ModifyTagCommand::ParentId) {
        stream << command.d_func()->parentId;
    }
    if (command.d_func()->modifiedParts & ModifyTagCommand::Type) {
        stream << command.d_func()->type;
    }
    if (command.d_func()->modifiedParts & ModifyTagCommand::RemoteId) {
        stream << command.d_func()->remoteId;
    }
    if (command.d_func()->modifiedParts & ModifyTagCommand::RemovedAttributes) {
        stream << command.d_func()->removedAttributes;
    }
    if (command.d_func()->modifiedParts & ModifyTagCommand::Attributes) {
        stream << command.d_func()->attributes;
    }
    return stream;
}

QDataStream &operator>>(QDataStream &stream, ModifyTagCommand &command)
{
    stream >> command.d_func()->tagId
           >> command.d_func()->modifiedParts;
    if (command.d_func()->modifiedParts & ModifyTagCommand::ParentId) {
        stream >> command.d_func()->parentId;
    }
    if (command.d_func()->modifiedParts & ModifyTagCommand::Type) {
        stream >> command.d_func()->type;
    }
    if (command.d_func()->modifiedParts & ModifyTagCommand::RemoteId) {
        stream >> command.d_func()->remoteId;
    }
    if (command.d_func()->modifiedParts & ModifyTagCommand::RemovedAttributes) {
        stream >> command.d_func()->removedAttributes;
    }
    if (command.d_func()->modifiedParts & ModifyTagCommand::Attributes) {
        stream >> command.d_func()->attributes;
    }
    return stream;
}




/****************************************************************************/




ModifyTagResponse::ModifyTagResponse()
    : Response(new ResponsePrivate(Command::ModifyTag))
{
}




/****************************************************************************/



namespace Akonadi
{
namespace Protocol
{

class ModifyRelationCommandPrivate : public CommandPrivate
{
public:
    ModifyRelationCommandPrivate()
        : CommandPrivate(Command::ModifyRelation)
        , left(-1)
        , right(-1)
    {}

    QString type;
    QString remoteId;
    qint64 left;
    qint64 right;
};

} // namespace Protocol
} // namespace Akonadi

AKONADI_DECLARE_PRIVATE(ModifyRelationCommand)

ModifyRelationCommand::ModifyRelationCommand()
    : Command(new ModifyRelationCommandPrivate)
{
}

void ModifyRelationCommand::setLeft(qint64 left)
{
    d_func()->left = left;
}
qint64 ModifyRelationCommand::left() const
{
    return d_func()->left;
}

void ModifyRelationCommand::setRight(qint64 right)
{
    d_func()->right = right;
}
qint64 ModifyRelationCommand::right() const
{
    return d_func()->right;
}

void ModifyRelationCommand::setType(const QString &type)
{
    d_func()->type = type;
}
QString ModifyRelationCommand::type() const
{
    return d_func()->type;
}

void ModifyRelationCommand::setRemoteId(const QString &remoteId)
{
    d_func()->remoteId = remoteId;
}
QString ModifyRelationCommand::remoteId() const
{
    return d_func()->remoteId;
}

void ModifyRelationCommand::serialize(QDataStream &stream) const
{
    stream << *this;
}
void ModifyRelationCommand::deserialize(QDataStream &stream)
{
    stream >> *this;
}

QDataStream &operator<<(QDataStream &stream, const ModifyRelationCommand &command)
{
    return stream << command.d_func()->left
                  << command.d_func()->right
                  << command.d_func()->type
                  << command.d_func()->remoteId;
}

QDataStream &operator>>(QDataStream &stream, ModifyRelationCommand &command)
{
    return stream >> command.d_func()->left
                  >> command.d_func()->right
                  >> command.d_func()->type
                  >> command.d_func()->remoteId;
}



/****************************************************************************/




ModifyRelationResponse::ModifyRelationResponse()
    : Response(new ResponsePrivate(Command::ModifyRelation))
{
}



/****************************************************************************/



namespace Akonadi
{
namespace Protocol
{

class RemoveRelationsCommandPrivate : public CommandPrivate
{
public:
    RemoveRelationsCommandPrivate()
        : CommandPrivate(Command::RemoveRelations)
        , left(-1)
        , right(-1)
    {}

    qint64 left;
    qint64 right;
    QString type;
};

} // namespace Protocol
} // namespace Akonadi

AKONADI_DECLARE_PRIVATE(RemoveRelationsCommand)

RemoveRelationsCommand::RemoveRelationsCommand()
    : Command(new RemoveRelationsCommandPrivate)
{
}

void RemoveRelationsCommand::setLeft(qint64 left)
{
    d_func()->left = left;
}
qint64 RemoveRelationsCommand::left() const
{
    return d_func()->left;
}

void RemoveRelationsCommand::setRight(qint64 right)
{
    d_func()->right = right;
}
qint64 RemoveRelationsCommand::right() const
{
    return d_func()->right;
}

void RemoveRelationsCommand::setType(const QString &type)
{
    d_func()->type = type;
}
QString RemoveRelationsCommand::type() const
{
    return d_func()->type;
}

void RemoveRelationsCommand::serialize(QDataStream &stream) const
{
    stream << *this;
}
void RemoveRelationsCommand::deserialize(QDataStream &stream)
{
    stream >> *this;
}

QDataStream &operator<<(QDataStream &stream, const RemoveRelationsCommand &command)
{
    return stream << command.d_func()->left
                  << command.d_func()->right
                  << command.d_func()->type;
}

QDataStream &operator>>(QDataStream &stream, RemoveRelationsCommand &command)
{
    return stream >> command.d_func()->left
                  >> command.d_func()->right
                  >> command.d_func()->type;
}



/****************************************************************************/




RemoveRelationsResponse::RemoveRelationsResponse()
    : Response(new ResponsePrivate(Command::RemoveRelations))
{
}



/****************************************************************************/



namespace Akonadi
{
namespace Protocol
{

class SelectResourceCommandPrivate : public CommandPrivate
{
public:
    SelectResourceCommandPrivate(const QString &resourceId = QString())
        : CommandPrivate(Command::SelectResource)
        , resourceId(resourceId)
    {}

    QString resourceId;
};

} // namespace Protocol
} // namespace Akonadi

AKONADI_DECLARE_PRIVATE(SelectResourceCommand)

SelectResourceCommand::SelectResourceCommand()
    : Command(new SelectResourceCommandPrivate)
{
}

SelectResourceCommand::SelectResourceCommand(const QString &resourceId)
    : Command(new SelectResourceCommandPrivate(resourceId))
{
}

QString SelectResourceCommand::resourceId() const
{
    return d_func()->resourceId;
}

void SelectResourceCommand::serialize(QDataStream &stream) const
{
    stream << *this;
}
void SelectResourceCommand::deserialize(QDataStream &stream)
{
    stream >> *this;
}

QDataStream &operator<<(QDataStream &stream, const SelectResourceCommand &command)
{
    return stream << command.d_func()->resourceId;
}

QDataStream &operator>>(QDataStream &stream, SelectResourceCommand &command)
{
    return stream >> command.d_func()->resourceId;
}



/****************************************************************************/




SelectResourceResponse::SelectResourceResponse()
    : Response(new ResponsePrivate(Command::SelectResource))
{
}



/****************************************************************************/



namespace Akonadi
{
namespace Protocol
{

class StreamPayloadCommandPrivate : public CommandPrivate
{
public:
    StreamPayloadCommandPrivate()
        : CommandPrivate(Command::StreamPayload)
        , expectedSize(0)
    {}

    QByteArray payloadName;
    QString externalFile;
    qint64 expectedSize;
};

} // namespace Protocol
} // namespace Akonadi

AKONADI_DECLARE_PRIVATE(StreamPayloadCommand)

StreamPayloadCommand::StreamPayloadCommand()
    : Command(new StreamPayloadCommandPrivate)
{
}

void StreamPayloadCommand::setPayloadName(const QByteArray &name)
{
    d_func()->payloadName = name;
}
QByteArray StreamPayloadCommand::payloadName() const
{
    return d_func()->payloadName;
}

void StreamPayloadCommand::setExpectedSize(qint64 size)
{
    d_func()->expectedSize = size;
}
qint64 StreamPayloadCommand::expectedSize() const
{
    return d_func()->expectedSize;
}

void StreamPayloadCommand::setExternalFile(const QString &externalFile)
{
    d_func()->externalFile = externalFile;
}
QString StreamPayloadCommand::externalFile() const
{
    return d_func()->externalFile;
}

void StreamPayloadCommand::serialize(QDataStream &stream) const
{
    stream << *this;
}
void StreamPayloadCommand::deserialize(QDataStream &stream)
{
    stream >> *this;
}

QDataStream &operator<<(QDataStream &stream, const StreamPayloadCommand &command)
{
    return stream << command.d_func()->payloadName
                  << command.d_func()->expectedSize
                  << command.d_func()->externalFile;
}

QDataStream &operator>>(QDataStream &stream, StreamPayloadCommand &command)
{
    return stream >> command.d_func()->payloadName
                  >> command.d_func()->expectedSize
                  >> command.d_func()->externalFile;
}




/****************************************************************************/



namespace Akonadi
{
namespace Protocol
{

class StreamPayloadResponsePrivate : public ResponsePrivate
{
public:
    StreamPayloadResponsePrivate()
        : ResponsePrivate(Command::StreamPayload)
        , isExternal(false)
    {}

    QByteArray data;
    bool isExternal;
};

} // namespace Protocol
} // namespace Akonadi

AKONADI_DECLARE_PRIVATE(StreamPayloadResponse)

StreamPayloadResponse::StreamPayloadResponse()
    : Response(new StreamPayloadResponsePrivate)
{
}

void StreamPayloadResponse::setIsExternal(bool isExternal)
{
    d_func()->isExternal = isExternal;
}
bool StreamPayloadResponse::isExternal() const
{
    return d_func()->isExternal;
}

void StreamPayloadResponse::setData(const QByteArray &data)
{
    d_func()->data = data;
}

QByteArray StreamPayloadResponse::data() const
{
    return d_func()->data;
}

void StreamPayloadResponse::serialize(QDataStream &stream) const
{
    stream << *this;
}
void StreamPayloadResponse::deserialize(QDataStream &stream)
{
    stream >> *this;
}

QDataStream &operator<<(QDataStream &stream, const StreamPayloadResponse &command)
{
    return stream << command.d_func()->isExternal
                  << command.d_func()->data;
}

QDataStream &operator>>(QDataStream &stream, StreamPayloadResponse &command)
{
    return stream >> command.d_func()->isExternal
                  >> command.d_func()->data;
}
