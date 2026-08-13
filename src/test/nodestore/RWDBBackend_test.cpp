/**
 * @file
 * @brief Tests for the RWDB factory's fetch/store no-op contract.
 */

#include <xrpl/beast/unit_test.h>
#include <xrpl/config/BasicConfig.h>
#include <xrpl/nodestore/DummyScheduler.h>
#include <xrpl/nodestore/Manager.h>
#include <xrpl/nodestore/NodeObject.h>

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
};

BEAST_DEFINE_TESTSUITE(RWDBBackend, nodestore, xrpl);

}  // namespace xrpl::test
