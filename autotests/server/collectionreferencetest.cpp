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
#include <QObject>
#include <handler/modify.h>
#include <response.h>
#include <storage/entity.h>

#include "fakeakonadiserver.h"
#include "fakedatastore.h"
#include <shared/aktest.h>
#include <shared/akdebug.h>
#include "entities.h"
#include "collectionreferencemanager.h"
#include "dbinitializer.h"

#include <private/scope_p.h>

#include <QtTest/QTest>

using namespace Akonadi;
using namespace Akonadi::Server;

Q_DECLARE_METATYPE(QList<Akonadi::NotificationMessageV3>)
Q_DECLARE_METATYPE(Collection::List)

class CollectionReferenceTest : public QObject
{
    Q_OBJECT

    DbInitializer initializer;

public:
    CollectionReferenceTest()
    {
        qRegisterMetaType<Akonadi::Server::Response>();

        try {
            FakeAkonadiServer::instance()->setPopulateDb(false);
            FakeAkonadiServer::instance()->init();
        } catch (const FakeAkonadiServerException &e) {
            akError() << "Server exception: " << e.what();
            akFatal() << "Fake Akonadi Server failed to start up, aborting test";
        }

        initializer.createResource("testresource");
        initializer.createCollection("col1");
        Collection col2 = initializer.createCollection("col2");
        col2.setEnabled(false);
        col2.update();
    }

    ~CollectionReferenceTest()
    {
        FakeAkonadiServer::instance()->quit();
    }

private Q_SLOTS:
    void testModify_data()
    {
        QTest::addColumn<TestScenario::List>("scenarios");
        QTest::addColumn<QList<Akonadi::NotificationMessageV3> >("expectedNotifications");

        Akonadi::NotificationMessageV3 notificationTemplate;
        notificationTemplate.setType(NotificationMessageV2::Collections);
        notificationTemplate.setOperation(NotificationMessageV2::Modify);
        notificationTemplate.addEntity(initializer.collection("col2").id(), QLatin1String("col2"), QLatin1String(""));
        notificationTemplate.setParentCollection(0);
        notificationTemplate.setResource("testresource");
        notificationTemplate.setSessionId(FakeAkonadiServer::instanceName().toLatin1());
        {
            Protocol::FetchCollectionsCommand cmd;
            cmd.setDepth(Protocol::FetchCollectionsCommand::AllCollections);
            cmd.setResource(QLatin1String("testresource"));
            cmd.setEnabled(true);

            TestScenario::List scenarios;
            scenarios << FakeAkonadiServer::loginScenario()
                      << TestScenario::create(5, TestScenario::ClientCmd, cmd)
                      << TestScenario::create(5, TestScenario::ServerCmd, initializer.listResponse(initializer.collection("col1")))
                      << TestScenario::create(5, TestScenario::ServerCmd, Protocol::FetchCollectionsResponse());
            QTest::newRow("list before referenced first level") << scenarios << QList<Akonadi::NotificationMessageV3>();
        }

        {
            Protocol::ModifyCollectionCommand cmd(initializer.collection("col2").id());
            cmd.setReferenced(true);

            TestScenario::List scenarios;
            scenarios << FakeAkonadiServer::loginScenario()
                      << TestScenario::create(5, TestScenario::ClientCmd, cmd)
                      << TestScenario::create(5, TestScenario::ServerCmd, Protocol::ModifyCollectionResponse());

            Akonadi::NotificationMessageV3 notification = notificationTemplate;
            notification.setItemParts(QSet<QByteArray>() << "REFERENCED");

            QTest::newRow("reference") << scenarios << (QList<Akonadi::NotificationMessageV3>() << notification);
        }

        {
            Protocol::ModifyCollectionCommand cmd(initializer.collection("col2").id());
            cmd.setReferenced(true);

            Protocol::FetchCollectionsCommand listCmd(initializer.collection("col2").id());
            listCmd.setDepth(Protocol::FetchCollectionsCommand::BaseCollection);
            listCmd.setEnabled(true);

            Collection col2 = initializer.collection("col2");
            col2.setReferenced(true);
            TestScenario::List scenarios;
            scenarios << FakeAkonadiServer::loginScenario()
                      << TestScenario::create(5, TestScenario::ClientCmd, cmd)
                      << TestScenario::create(5, TestScenario::ServerCmd, Protocol::ModifyCollectionResponse())
                      << TestScenario::create(6, TestScenario::ClientCmd, listCmd)
                      << TestScenario::create(6, TestScenario::ServerCmd, initializer.listResponse(col2))
                      << TestScenario::create(6, TestScenario::ServerCmd, Protocol::FetchCollectionsResponse());

            Akonadi::NotificationMessageV3 notification = notificationTemplate;
            notification.setItemParts(QSet<QByteArray>() << "REFERENCED");

            QTest::newRow("list referenced base") << scenarios << (QList<Akonadi::NotificationMessageV3>() << notification);
        }
        {
            Protocol::ModifyCollectionCommand cmd(initializer.collection("col2").id());
            cmd.setReferenced(true);

            Protocol::FetchCollectionsCommand listCmd;
            listCmd.setResource(QLatin1String("testresource"));
            listCmd.setEnabled(true);
            listCmd.setDepth(Protocol::FetchCollectionsCommand::ParentCollection);

            Collection col2 = initializer.collection("col2");
            col2.setReferenced(true);
            TestScenario::List scenarios;
            scenarios << FakeAkonadiServer::loginScenario()
                      << TestScenario::create(5, TestScenario::ClientCmd, cmd)
                      << TestScenario::create(5, TestScenario::ServerCmd, Protocol::ModifyCollectionResponse())
                      << TestScenario::create(6, TestScenario::ClientCmd, listCmd)
                      << TestScenario::create(6, TestScenario::ServerCmd, initializer.listResponse(initializer.collection("col1")))
                      << TestScenario::create(6, TestScenario::ServerCmd, initializer.listResponse(col2))
                      << TestScenario::create(6, TestScenario::ServerCmd, Protocol::FetchCollectionsResponse());

            Akonadi::NotificationMessageV3 notification = notificationTemplate;
            notification.setItemParts(QSet<QByteArray>() << "REFERENCED");

            QTest::newRow("list referenced first level") << scenarios << (QList<Akonadi::NotificationMessageV3>() << notification);
        }
        {
            Protocol::ModifyCollectionCommand cmd1(initializer.collection("col2").id());
            cmd1.setReferenced(true);

            Protocol::ModifyCollectionCommand cmd2(initializer.collection("col2").id());
            cmd2.setReferenced(false);

            TestScenario::List scenarios;
            scenarios << FakeAkonadiServer::loginScenario()
                      << TestScenario::create(5, TestScenario::ClientCmd, cmd1)
                      << TestScenario::create(5, TestScenario::ServerCmd, Protocol::ModifyCollectionResponse())
                      << TestScenario::create(6, TestScenario::ClientCmd, cmd2)
                      << TestScenario::create(6, TestScenario::ServerCmd, Protocol::ModifyCollectionResponse());

            Akonadi::NotificationMessageV3 notification = notificationTemplate;
            notification.setItemParts(QSet<QByteArray>() << "REFERENCED");

            QTest::newRow("dereference") << scenarios << (QList<Akonadi::NotificationMessageV3>() << notification << notification);
        }
    }

    void testModify()
    {
        QFETCH(TestScenario::List, scenarios);
        QFETCH(QList<NotificationMessageV3>, expectedNotifications);

        FakeAkonadiServer::instance()->setScenarios(scenarios);
        FakeAkonadiServer::instance()->runTest();

        QSignalSpy *notificationSpy = FakeAkonadiServer::instance()->notificationSpy();
        if (expectedNotifications.isEmpty()) {
            QVERIFY(notificationSpy->isEmpty() || notificationSpy->takeFirst().first().value<NotificationMessageV3::List>().isEmpty());
        } else {
            NotificationMessageV3::List receivedNotifications;
            for (int q = 0; q < notificationSpy->size(); q++) {
                //Only one notify call
                QCOMPARE(notificationSpy->first().count(), 1);
                const NotificationMessageV3::List n = notificationSpy->first().first().value<NotificationMessageV3::List>();
                for (int i = 0; i < n.size(); i++) {
                    receivedNotifications.append(n.at(i));
                }
            }
            QCOMPARE(receivedNotifications.size(), expectedNotifications.count());
            for (int i = 0; i < expectedNotifications.size(); i++) {
                QCOMPARE(receivedNotifications.at(i), expectedNotifications.at(i));
            }
        }
    }

    void testReferenceCollection()
    {
        Collection col = initializer.createCollection("testReferenceCollection");

        CollectionReferenceManager::instance()->referenceCollection("testReferenceCollectionSession", col, true);
        QVERIFY(CollectionReferenceManager::instance()->isReferenced(col.id()));
        QVERIFY(CollectionReferenceManager::instance()->isReferenced(col.id(), "testReferenceCollectionSession"));

        CollectionReferenceManager::instance()->referenceCollection("foobar", col, false);
        QVERIFY(CollectionReferenceManager::instance()->isReferenced(col.id()));
        QVERIFY(CollectionReferenceManager::instance()->isReferenced(col.id(), "testReferenceCollectionSession"));

        CollectionReferenceManager::instance()->referenceCollection("testReferenceCollectionSession", col, false);
        QVERIFY(!CollectionReferenceManager::instance()->isReferenced(col.id()));
        QVERIFY(!CollectionReferenceManager::instance()->isReferenced(col.id(), "testReferenceCollectionSession"));
        QVERIFY(col.remove());
    }

    void testSessionClosed()
    {
        Collection col = initializer.createCollection("testSessionCollection");
        col.setReferenced(true);
        QVERIFY(col.update());
        CollectionReferenceManager::instance()->referenceCollection("testSessionClosedSession", col, true);
        CollectionReferenceManager::instance()->referenceCollection("testSessionClosedSession2", col, true);

        //Remove first session
        CollectionReferenceManager::instance()->removeSession("testSessionClosedSession2");
        QVERIFY(Collection::retrieveById(col.id()).referenced());
        QVERIFY(!CollectionReferenceManager::instance()->isReferenced(col.id(), "testSessionClosedSession2"));
        QVERIFY(CollectionReferenceManager::instance()->isReferenced(col.id(), "testSessionClosedSession"));

        CollectionReferenceManager::instance()->removeSession("testSessionClosedSession");
        QVERIFY(!Collection::retrieveById(col.id()).referenced());
        QVERIFY(!CollectionReferenceManager::instance()->isReferenced(col.id(), "testSessionClosedSession"));

        QVERIFY(col.remove());
    }

    void testCleanup()
    {
        Collection col = initializer.createCollection("testCleanupCollection");
        col.setReferenced(true);
        QVERIFY(col.update());

        CollectionReferenceManager::cleanup();
        QVERIFY(!Collection::retrieveById(col.id()).referenced());

        QVERIFY(col.remove());
    }

};

AKTEST_FAKESERVER_MAIN(CollectionReferenceTest)

#include "collectionreferencetest.moc"
