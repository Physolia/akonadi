/*
    SPDX-FileCopyrightText: 2008 Volker Krause <vkrause@kde.org>

    SPDX-License-Identifier: LGPL-2.0-or-later
*/

#include "agentmanager.h"
#include "agentinstance.h"
#include "control.h"
#include "collection.h"
#include "item.h"
#include "itemdeletejob.h"
#include "itemfetchjob.h"
#include "itemfetchscope.h"
#include "itemsync.h"
#include "itemcreatejob.h"
#include "qtest_akonadi.h"
#include "monitor.h"
#include "resourceselectjob_p.h"

#include <KRandom>

#include <QObject>
#include <QSignalSpy>

#include <memory>

using namespace Akonadi;

Q_DECLARE_METATYPE(KJob *)

class ItemsyncTest : public QObject
{
    Q_OBJECT
private:
    Item::List fetchItems(const Collection &col)
    {
        qDebug() << "fetching items from collection" << col.remoteId() << col.name();
        ItemFetchJob *fetch = new ItemFetchJob(col, this);
        fetch->fetchScope().fetchFullPayload();
        fetch->fetchScope().fetchAllAttributes();
        fetch->fetchScope().setCacheOnly(true);   // resources are switched off anyway
        if (!fetch->exec()) {
            []() { QFAIL("Failed to fetch items!"); }();
        }
        return fetch->items();
    }

    static void createItems(const Collection &col, int itemCount)
    {
        for (int i = 0; i < itemCount; ++i) {
            Item item(QStringLiteral("application/octet-stream"));
            item.setRemoteId(QStringLiteral("rid") + QString::number(i));
            item.setGid(QStringLiteral("gid") + QString::number(i));
            item.setPayload<QByteArray>("payload1");
            ItemCreateJob *job = new ItemCreateJob(item, col);
            AKVERIFYEXEC(job);
        }
    }

    static Item duplicateItem(const Item &item, const Collection &col)
    {
        Item duplicate = item;
        duplicate.setId(-1);
        ItemCreateJob *job = new ItemCreateJob(duplicate, col);
        [job]() { AKVERIFYEXEC(job); }();
        return job->item();
    }

    static Item modifyItem(Item item)
    {
        static int counter = 0;
        item.setFlag(QByteArray("\\READ") + QByteArray::number(counter));
        counter++;
        return item;
    }

    std::unique_ptr<Monitor> createCollectionMonitor(const Collection &col)
    {
        auto monitor = std::make_unique<Monitor>();
        monitor->setCollectionMonitored(col);
        if (!AkonadiTest::akWaitForSignal(monitor.get(), &Monitor::monitorReady)) {
            QTest::qFail("Failed to wait for monitor", __FILE__, __LINE__);
            return nullptr;
        }

        return monitor;
    }

private Q_SLOTS:
    void initTestCase()
    {
        AkonadiTest::checkTestIsIsolated();
        Control::start();
        AkonadiTest::setAllResourcesOffline();
        qRegisterMetaType<KJob *>();
    }

    void testFullSync()
    {
        const Collection col = Collection(AkonadiTest::collectionIdFromPath(QStringLiteral("res1/foo")));
        QVERIFY(col.isValid());
        Item::List origItems = fetchItems(col);

        //Since the item sync affects the knut resource we ensure we actually managed to load all items
        //This needs to be adjusted should the testdataset change
        QCOMPARE(origItems.size(), 15);

        Akonadi::Monitor monitor;
        monitor.setCollectionMonitored(col);
        QSignalSpy deletedSpy(&monitor, &Monitor::itemRemoved);
        QVERIFY(deletedSpy.isValid());
        QSignalSpy addedSpy(&monitor, &Monitor::itemAdded);
        QVERIFY(addedSpy.isValid());
        QSignalSpy changedSpy(&monitor, &Monitor::itemChanged);
        QVERIFY(changedSpy.isValid());

        ItemSync *syncer = new ItemSync(col);
        syncer->setFullSyncItems(origItems);
        AKVERIFYEXEC(syncer);

        Item::List resultItems = fetchItems(col);
        QCOMPARE(resultItems.count(), origItems.count());
        QTest::qWait(100);
        QCOMPARE(deletedSpy.count(), 0);
        QCOMPARE(addedSpy.count(), 0);
        QCOMPARE(changedSpy.count(), 0);
    }

    void testFullStreamingSync_data()
    {
        QTest::addColumn<bool>("goToEventLoopAfterAddingItems");

        QTest::newRow("no eventloop") << false;
        QTest::newRow("with eventloop") << true;
    }

    void testFullStreamingSync()
    {
        QFETCH(bool, goToEventLoopAfterAddingItems);

        const Collection col = Collection(AkonadiTest::collectionIdFromPath(QStringLiteral("res1/foo")));
        QVERIFY(col.isValid());
        Item::List origItems = fetchItems(col);
        QCOMPARE(origItems.size(), 15);

        auto monitor = createCollectionMonitor(col);
        QSignalSpy deletedSpy(monitor.get(), &Monitor::itemRemoved);
        QSignalSpy addedSpy(monitor.get(), &Monitor::itemAdded);
        QSignalSpy changedSpy(monitor.get(), &Monitor::itemChanged);

        ItemSync *syncer = new ItemSync(col);
        syncer->setAutoDelete(false);
        syncer->setStreamingEnabled(true);
        QSignalSpy spy(syncer, &KJob::result);
        QVERIFY(spy.isValid());
        syncer->setTotalItems(origItems.count());
        QTest::qWait(0);
        QCOMPARE(spy.count(), 0);

        for (int i = 0; i < origItems.count(); ++i) {
            Item::List l;
            //Modify to trigger a changed signal
            l << modifyItem(origItems[i]);
            syncer->setFullSyncItems(l);
            if (goToEventLoopAfterAddingItems) {
                QTest::qWait(0);
            }
            if (i < origItems.count() - 1) {
                QCOMPARE(spy.count(), 0);
            }
        }
        syncer->deliveryDone();
        QTRY_COMPARE(spy.count(), 1);
        KJob *job = spy.at(0).at(0).value<KJob *>();
        QCOMPARE(job, syncer);
        QCOMPARE(job->error(), 0);

        Item::List resultItems = fetchItems(col);
        QCOMPARE(resultItems.count(), origItems.count());

        delete syncer;
        QTest::qWait(100);
        QTRY_COMPARE(deletedSpy.count(), 0);
        QTRY_COMPARE(addedSpy.count(), 0);
        QTRY_COMPARE(changedSpy.count(), origItems.count());
    }

    void testIncrementalSync()
    {
        {
            ResourceSelectJob *select = new ResourceSelectJob(QStringLiteral("akonadi_knut_resource_0"));
            AKVERIFYEXEC(select);
        }

        const Collection col = Collection(AkonadiTest::collectionIdFromPath(QStringLiteral("res1/foo")));
        QVERIFY(col.isValid());
        Item::List origItems = fetchItems(col);
        QCOMPARE(origItems.size(), 15);

        auto monitor = createCollectionMonitor(col);
        QSignalSpy deletedSpy(monitor.get(), &Monitor::itemRemoved);
        QSignalSpy addedSpy(monitor.get(), &Monitor::itemAdded);
        QSignalSpy changedSpy(monitor.get(), &Monitor::itemChanged);

        {
            ItemSync *syncer = new ItemSync(col);
            syncer->setIncrementalSyncItems(origItems, Item::List());
            AKVERIFYEXEC(syncer);
        }

        QTest::qWait(100);
        QTRY_COMPARE(deletedSpy.count(), 0);
        QCOMPARE(addedSpy.count(), 0);
        QTRY_COMPARE(changedSpy.count(), 0);
        deletedSpy.clear();
        addedSpy.clear();
        changedSpy.clear();

        Item::List resultItems = fetchItems(col);
        QCOMPARE(resultItems.count(), origItems.count());

        Item::List delItems;
        delItems << resultItems.takeFirst();

        Item itemWithOnlyRemoteId;
        itemWithOnlyRemoteId.setRemoteId(resultItems.front().remoteId());
        delItems << itemWithOnlyRemoteId;
        resultItems.takeFirst();

        //This item will not be removed since it isn't existing locally
        Item itemWithRandomRemoteId;
        itemWithRandomRemoteId.setRemoteId(KRandom::randomString(100));
        delItems << itemWithRandomRemoteId;

        {
            ItemSync *syncer = new ItemSync(col);
            syncer->setIncrementalSyncItems(resultItems, delItems);
            AKVERIFYEXEC(syncer);
        }

        Item::List resultItems2 = fetchItems(col);
        QCOMPARE(resultItems2.count(), resultItems.count());

        QTest::qWait(100);
        QTRY_COMPARE(deletedSpy.count(), 2);
        QCOMPARE(addedSpy.count(), 0);
        QTRY_COMPARE(changedSpy.count(), 0);

        {
            ResourceSelectJob *select = new ResourceSelectJob(QStringLiteral(""));
            AKVERIFYEXEC(select);
        }
    }

    void testIncrementalStreamingSync()
    {
        const Collection col = Collection(AkonadiTest::collectionIdFromPath(QStringLiteral("res1/foo")));
        QVERIFY(col.isValid());
        Item::List origItems = fetchItems(col);

        auto monitor = createCollectionMonitor(col);
        QSignalSpy deletedSpy(monitor.get(), &Monitor::itemRemoved);
        QSignalSpy addedSpy(monitor.get(), &Monitor::itemAdded);
        QSignalSpy changedSpy(monitor.get(), &Monitor::itemChanged);

        ItemSync *syncer = new ItemSync(col);
        syncer->setAutoDelete(false);
        QSignalSpy spy(syncer, &KJob::result);
        QVERIFY(spy.isValid());
        syncer->setStreamingEnabled(true);
        QTest::qWait(0);
        QCOMPARE(spy.count(), 0);

        for (int i = 0; i < origItems.count(); ++i) {
            Item::List l;
            //Modify to trigger a changed signal
            l << modifyItem(origItems[i]);
            syncer->setIncrementalSyncItems(l, Item::List());
            if (i < origItems.count() - 1) {
                QTest::qWait(0);   // enter the event loop so itemsync actually can do something
            }
            QCOMPARE(spy.count(), 0);
        }
        syncer->deliveryDone();
        QTRY_COMPARE(spy.count(), 1);
        KJob *job = spy.at(0).at(0).value<KJob *>();
        QCOMPARE(job, syncer);
        QCOMPARE(job->error(), 0);

        Item::List resultItems = fetchItems(col);
        QCOMPARE(resultItems.count(), origItems.count());

        delete syncer;

        QTest::qWait(100);
        QCOMPARE(deletedSpy.count(), 0);
        QCOMPARE(addedSpy.count(), 0);
        QTRY_COMPARE(changedSpy.count(), origItems.size());
    }

    void testEmptyIncrementalSync()
    {
        const Collection col = Collection(AkonadiTest::collectionIdFromPath(QStringLiteral("res1/foo")));
        QVERIFY(col.isValid());
        Item::List origItems = fetchItems(col);

        auto monitor = createCollectionMonitor(col);
        QSignalSpy deletedSpy(monitor.get(), &Monitor::itemRemoved);
        QSignalSpy addedSpy(monitor.get(), &Monitor::itemAdded);
        QSignalSpy changedSpy(monitor.get(), &Monitor::itemChanged);

        ItemSync *syncer = new ItemSync(col);
        syncer->setIncrementalSyncItems(Item::List(), Item::List());
        AKVERIFYEXEC(syncer);

        Item::List resultItems = fetchItems(col);
        QCOMPARE(resultItems.count(), origItems.count());

        QTest::qWait(100);
        QCOMPARE(deletedSpy.count(), 0);
        QCOMPARE(addedSpy.count(), 0);
        QCOMPARE(changedSpy.count(), 0);
    }

    void testGidMerge()
    {
        Collection col(AkonadiTest::collectionIdFromPath(QStringLiteral("res3")));
        {
            Item item(QStringLiteral("application/octet-stream"));
            item.setRemoteId(QStringLiteral("rid1"));
            item.setGid(QStringLiteral("gid1"));
            item.setPayload<QByteArray>("payload1");
            ItemCreateJob *job = new ItemCreateJob(item, col);
            AKVERIFYEXEC(job);
        }
        {
            Item item(QStringLiteral("application/octet-stream"));
            item.setRemoteId(QStringLiteral("rid2"));
            item.setGid(QStringLiteral("gid2"));
            item.setPayload<QByteArray>("payload1");
            ItemCreateJob *job = new ItemCreateJob(item, col);
            AKVERIFYEXEC(job);
        }
        Item modifiedItem(QStringLiteral("application/octet-stream"));
        modifiedItem.setRemoteId(QStringLiteral("rid3"));
        modifiedItem.setGid(QStringLiteral("gid2"));
        modifiedItem.setPayload<QByteArray>("payload2");

        ItemSync *syncer = new ItemSync(col);
        syncer->setIncrementalSyncItems(Item::List() << modifiedItem, Item::List());
        AKVERIFYEXEC(syncer);

        Item::List resultItems = fetchItems(col);
        QCOMPARE(resultItems.count(), 3);

        Item item;
        item.setGid(QStringLiteral("gid2"));
        ItemFetchJob *fetchJob = new ItemFetchJob(item);
        fetchJob->fetchScope().fetchFullPayload();
        AKVERIFYEXEC(fetchJob);
        QCOMPARE(fetchJob->items().size(), 2);
        QCOMPARE(fetchJob->items().first().payload<QByteArray>(), QByteArray("payload2"));
        QCOMPARE(fetchJob->items().first().remoteId(), QString::fromLatin1("rid3"));
        QCOMPARE(fetchJob->items().at(1).payload<QByteArray>(), QByteArray("payload1"));
        QCOMPARE(fetchJob->items().at(1).remoteId(), QStringLiteral("rid2"));
    }

    /*
     * This test verifies that ItemSync doesn't prematurely emit its result if a job inside a transaction fails.
     * ItemSync is supposed to continue the sync but simply ignoring all delivered data.
     */
    void testFailingJob()
    {
        const Collection col = Collection(AkonadiTest::collectionIdFromPath(QStringLiteral("res1/foo")));
        QVERIFY(col.isValid());
        Item::List origItems = fetchItems(col);

        ItemSync *syncer = new ItemSync(col);
        QSignalSpy spy(syncer, &KJob::result);
        QVERIFY(spy.isValid());
        syncer->setStreamingEnabled(true);
        QTest::qWait(0);
        QCOMPARE(spy.count(), 0);

        constexpr int batchSize = 10;
        for (int i = 0; i < batchSize; ++i) {
            Item::List l;
            //Modify to trigger a changed signal
            Item item = modifyItem(origItems[i]);
            // item.setRemoteId(QByteArray("foo"));
            item.setRemoteId(QString());
            item.setId(-1);
            l << item;
            syncer->setIncrementalSyncItems(l, Item::List());
            if (i < (batchSize - 1)) {
                QTest::qWait(0);   // enter the event loop so itemsync actually can do something
            }
            QCOMPARE(spy.count(), 0);
        }
        QTest::qWait(100);
        QTRY_COMPARE(spy.count(), 0);

        for (int i = batchSize; i < origItems.count(); ++i) {
            Item::List l;
            //Modify to trigger a changed signal
            l << modifyItem(origItems[i]);
            syncer->setIncrementalSyncItems(l, Item::List());
            if (i < origItems.count() - 1) {
                QTest::qWait(0);   // enter the event loop so itemsync actually can do something
            }
            QCOMPARE(spy.count(), 0);
        }

        syncer->deliveryDone();
        QTRY_COMPARE(spy.count(), 1);
    }

    /*
     * This test verifies that ItemSync doesn't prematurely emit its result if a job inside a transaction fails, due to a duplicate.
     * This case used to break the TransactionSequence.
     * ItemSync is supposed to continue the sync but simply ignoring all delivered data.
     */
    void testFailingDueToDuplicateItem()
    {
        const Collection col = Collection(AkonadiTest::collectionIdFromPath(QStringLiteral("res1/foo")));
        QVERIFY(col.isValid());
        Item::List origItems = fetchItems(col);

        //Create a duplicate that will trigger an error during the first batch
        Item dupe = duplicateItem(origItems.at(0), col);
        origItems = fetchItems(col);

        ItemSync *syncer = new ItemSync(col);
        QSignalSpy spy(syncer, &KJob::result);
        QVERIFY(spy.isValid());
        syncer->setStreamingEnabled(true);
        QTest::qWait(0);
        QCOMPARE(spy.count(), 0);

        constexpr int batchSize = 10;
        for (int i = 0; i < batchSize; ++i) {
            Item::List l;
            //Modify to trigger a changed signal
            l << modifyItem(origItems[i]);
            syncer->setIncrementalSyncItems(l, Item::List());
            if (i < (batchSize - 1)) {
                QTest::qWait(0);   // enter the event loop so itemsync actually can do something
            }
            QCOMPARE(spy.count(), 0);
        }
        QTest::qWait(100);
        //Ensure the job hasn't finished yet due to the errors
        QTRY_COMPARE(spy.count(), 0);

        for (int i = batchSize; i < origItems.count(); ++i) {
            Item::List l;
            //Modify to trigger a changed signal
            l << modifyItem(origItems[i]);
            syncer->setIncrementalSyncItems(l, Item::List());
            if (i < origItems.count() - 1) {
                QTest::qWait(0);   // enter the event loop so itemsync actually can do something
            }
            QCOMPARE(spy.count(), 0);
        }

        syncer->deliveryDone();
        QTRY_COMPARE(spy.count(), 1);

        // cleanup
        ItemDeleteJob *del = new ItemDeleteJob(dupe, this);
        AKVERIFYEXEC(del);
    }

    void testFullSyncFailingDueToDuplicateItem()
    {
        const Collection col = Collection(AkonadiTest::collectionIdFromPath(QStringLiteral("res1/foo")));
        QVERIFY(col.isValid());
        Item::List origItems = fetchItems(col);
        //Create a duplicate that will trigger an error during the first batch
        Item dupe = duplicateItem(origItems.at(0), col);
        origItems = fetchItems(col);

        auto monitor = createCollectionMonitor(col);
        QSignalSpy deletedSpy(monitor.get(), &Monitor::itemRemoved);
        QSignalSpy addedSpy(monitor.get(), &Monitor::itemAdded);
        QSignalSpy changedSpy(monitor.get(), &Monitor::itemChanged);

        ItemSync *syncer = new ItemSync(col);
        syncer->setFullSyncItems(origItems);
        QVERIFY(!syncer->exec());

        Item::List resultItems = fetchItems(col);
        QCOMPARE(resultItems.count(), origItems.count());
        QTest::qWait(100);
        //QCOMPARE(deletedSpy.count(), 1); // ## is this correct?
        //QCOMPARE(addedSpy.count(), 1); // ## is this correct?
        QCOMPARE(changedSpy.count(), 0);

        // cleanup
        ItemDeleteJob *del = new ItemDeleteJob(dupe, this);
        AKVERIFYEXEC(del);
    }

    void testFullSyncManyItems()
    {
        // Given a collection with 1000 items
        const Collection col = Collection(AkonadiTest::collectionIdFromPath(QStringLiteral("res2/foo2")));
        QVERIFY(col.isValid());

        auto monitor = createCollectionMonitor(col);
        QSignalSpy addedSpy(monitor.get(), &Monitor::itemAdded);

        const int itemCount = 1000;
        createItems(col, itemCount);
        QTRY_COMPARE(addedSpy.count(), itemCount);
        addedSpy.clear();

        const Item::List origItems = fetchItems(col);
        QCOMPARE(origItems.size(), itemCount);

        QSignalSpy deletedSpy(monitor.get(), &Monitor::itemRemoved);
        QSignalSpy changedSpy(monitor.get(), &Monitor::itemChanged);

        QBENCHMARK {
            ItemSync *syncer = new ItemSync(col);
            syncer->setFullSyncItems(origItems);

            AKVERIFYEXEC(syncer);
        }

        const Item::List resultItems = fetchItems(col);
        QCOMPARE(resultItems.count(), origItems.count());
        QTest::qWait(100);
        QCOMPARE(deletedSpy.count(), 0);
        QCOMPARE(addedSpy.count(), 0);
        QCOMPARE(changedSpy.count(), 0);

        // delete all items; QBENCHMARK leads to the whole method being called more than once
        ItemDeleteJob *job = new ItemDeleteJob(resultItems);
        AKVERIFYEXEC(job);
    }

    void testUserCancel()
    {
        // Given a collection with 100 items
        const Collection col = Collection(AkonadiTest::collectionIdFromPath(QStringLiteral("res2/foo2")));
        QVERIFY(col.isValid());

        const Item::List itemsToDelete = fetchItems(col);
        if (!itemsToDelete.isEmpty()) {
            ItemDeleteJob *deleteJob = new ItemDeleteJob(itemsToDelete);
            AKVERIFYEXEC(deleteJob);
        }

        const int itemCount = 100;
        createItems(col, itemCount);
        const Item::List origItems = fetchItems(col);
        QCOMPARE(origItems.size(), itemCount);

        // and an ItemSync running
        ItemSync *syncer = new ItemSync(col);
        syncer->setFullSyncItems(origItems);

        // When the user cancels the ItemSync
        QTimer::singleShot(10, syncer, &ItemSync::rollback);

        // Then the itemsync should finish at some point, and not crash
        QVERIFY(!syncer->exec());
        QCOMPARE(syncer->errorString(), QStringLiteral("User canceled operation."));

        // Cleanup
        ItemDeleteJob *job = new ItemDeleteJob(origItems);
        AKVERIFYEXEC(job);
    }
};

QTEST_AKONADIMAIN(ItemsyncTest)

#include "itemsynctest.moc"
