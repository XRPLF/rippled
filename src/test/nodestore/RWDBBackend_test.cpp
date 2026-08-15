/**
 * @file
 * @brief Tests for the RWDB factory's fetch/store no-op contract.
 */

#include <xrpl/basics/ByteUtilities.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/config/BasicConfig.h>
#include <xrpl/nodestore/Database.h>
#include <xrpl/nodestore/DummyScheduler.h>
#include <xrpl/nodestore/Manager.h>
#include <xrpl/nodestore/NodeObject.h>
#include <xrpl/nodestore/Types.h>

#include <memory>
#include <utility>

namespace xrpl::test {

/**
 * Test the RWDB factory's fetch/store no-op contract.
 */
class RWDBBackend_test : public beast::unit_test::Suite
{
public:
    void
    run() override
    {
        testFactoryIsNullBackend();
        testSingleZeroCacheKeyStillCreatesCache();
        testBothZeroCacheKeysDisableCache();
    }

    void
    testFactoryIsNullBackend()
    {
        testcase("RWDB factory fetch/store are no-ops");

        Section section;
        section.set("type", "RWDB");
        section.set("path", "rwdb-test");

        node_store::DummyScheduler scheduler;
        auto backend = node_store::Manager::instance().makeBackend(
            section, 0, scheduler, beast::Journal{beast::Journal::getNullSink()});
        if (!BEAST_EXPECT(backend))
            return;

        backend->open();
        BEAST_EXPECT(backend->isOpen());
        BEAST_EXPECT(backend->getName() == "rwdb-test");

        auto object = NodeObject::createObject(NodeObjectType::Ledger, Blob{1, 2, 3, 4}, uint256{});
        backend->store(object);

        std::shared_ptr<NodeObject> fetched;
        BEAST_EXPECT(backend->fetch(object->getHash(), &fetched) == node_store::Status::NotFound);
        BEAST_EXPECT(!fetched);

        backend->close();
    }

    static std::unique_ptr<node_store::Database>
    makeRWDBDatabase(Section section, node_store::DummyScheduler& scheduler)
    {
        return node_store::Manager::instance().makeDatabase(
            megabytes(4), scheduler, 0, section, beast::Journal{beast::Journal::getNullSink()});
    }

    void
    testSingleZeroCacheKeyStillCreatesCache()
    {
        testcase("a single cache_size=0 still creates the object cache");

        Section section;
        section.set("type", "RWDB");
        section.set("path", "rwdb-one-zero");
        section.set("cache_size", "0");

        node_store::DummyScheduler scheduler;
        auto db = makeRWDBDatabase(std::move(section), scheduler);
        if (!BEAST_EXPECT(db))
            return;

        uint256 const hash{1};
        Blob data{1, 2, 3, 4};
        db->store(NodeObjectType::Ledger, std::move(data), hash, 1);
        auto fetched = db->fetchNodeObject(hash, 1);
        // Null backend cannot reload; a hit means the object cache exists.
        BEAST_EXPECT(fetched);
    }

    void
    testBothZeroCacheKeysDisableCache()
    {
        testcase("cache_size=0 and cache_age=0 disable the object cache");

        Section section;
        section.set("type", "RWDB");
        section.set("path", "rwdb-both-zero");
        section.set("cache_size", "0");
        section.set("cache_age", "0");

        node_store::DummyScheduler scheduler;
        auto db = makeRWDBDatabase(std::move(section), scheduler);
        if (!BEAST_EXPECT(db))
            return;

        uint256 const hash{2};
        Blob data{1, 2, 3, 4};
        db->store(NodeObjectType::Ledger, std::move(data), hash, 1);
        auto fetched = db->fetchNodeObject(hash, 1);
        BEAST_EXPECT(!fetched);
    }
};

BEAST_DEFINE_TESTSUITE(RWDBBackend, nodestore, xrpl);

}  // namespace xrpl::test
