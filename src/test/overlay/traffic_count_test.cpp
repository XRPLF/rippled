#include <xrpld/overlay/detail/TrafficCount.h>

#include <xrpl/beast/unit_test/suite.h>

#include <xrpl.pb.h>

#include <algorithm>
#include <cstdint>

namespace xrpl::test {

class traffic_count_test : public beast::unit_test::Suite
{
public:
    traffic_count_test() = default;

    void
    testCategorize()
    {
        testcase("categorize");
        protocol::TMPing message;
        message.set_type(protocol::TMPing::ptPING);

        // a known message is categorized to a proper category
        auto const known = TrafficCount::categorize(message, protocol::mtPING, false);
        BEAST_EXPECT(known == TrafficCount::Category::Base);

        // an unknown message type is categorized as unknown
        auto const unknown =
            TrafficCount::categorize(message, static_cast<protocol::MessageType>(99), false);
        BEAST_EXPECT(unknown == TrafficCount::Category::Unknown);
    }

    void
    testLedgerDataCategorize()
    {
        testcase("ledger-data categorize");

        auto checkCategory = [&](protocol::TMLedgerInfoType type,
                                 bool inbound,
                                 bool requestCookie,
                                 TrafficCount::Category category) {
            protocol::TMLedgerData message;
            message.set_type(type);
            if (requestCookie)
                message.set_requestcookie(1);

            BEAST_EXPECT(
                TrafficCount::categorize(message, protocol::mtLEDGER_DATA, inbound) == category);
        };

        checkCategory(protocol::liTS_CANDIDATE, true, false, TrafficCount::Category::LdTscGet);
        checkCategory(protocol::liTS_CANDIDATE, true, true, TrafficCount::Category::LdTscShare);
        checkCategory(protocol::liTS_CANDIDATE, false, false, TrafficCount::Category::LdTscShare);

        checkCategory(protocol::liTX_NODE, true, false, TrafficCount::Category::LdTxnGet);
        checkCategory(protocol::liTX_NODE, true, true, TrafficCount::Category::LdTxnShare);
        checkCategory(protocol::liTX_NODE, false, false, TrafficCount::Category::LdTxnShare);

        checkCategory(protocol::liAS_NODE, true, false, TrafficCount::Category::LdAsnGet);
        checkCategory(protocol::liAS_NODE, true, true, TrafficCount::Category::LdAsnShare);
        checkCategory(protocol::liAS_NODE, false, false, TrafficCount::Category::LdAsnShare);

        checkCategory(protocol::liBASE, true, false, TrafficCount::Category::LdGet);
        checkCategory(protocol::liBASE, true, true, TrafficCount::Category::LdShare);
        checkCategory(protocol::liBASE, false, false, TrafficCount::Category::LdShare);
    }

    void
    testGetLedgerCategorize()
    {
        testcase("get-ledger categorize");

        auto checkCategory = [&](protocol::TMLedgerInfoType type,
                                 bool inbound,
                                 bool requestCookie,
                                 TrafficCount::Category category) {
            protocol::TMGetLedger message;
            message.set_itype(type);
            if (requestCookie)
                message.set_requestcookie(1);

            BEAST_EXPECT(
                TrafficCount::categorize(message, protocol::mtGET_LEDGER, inbound) == category);
        };

        checkCategory(protocol::liTS_CANDIDATE, true, false, TrafficCount::Category::GlTscShare);
        checkCategory(protocol::liTS_CANDIDATE, false, true, TrafficCount::Category::GlTscShare);
        checkCategory(protocol::liTS_CANDIDATE, false, false, TrafficCount::Category::GlTscGet);

        checkCategory(protocol::liTX_NODE, true, false, TrafficCount::Category::GlTxnShare);
        checkCategory(protocol::liTX_NODE, false, true, TrafficCount::Category::GlTxnShare);
        checkCategory(protocol::liTX_NODE, false, false, TrafficCount::Category::GlTxnGet);

        checkCategory(protocol::liAS_NODE, true, false, TrafficCount::Category::GlAsnShare);
        checkCategory(protocol::liAS_NODE, false, true, TrafficCount::Category::GlAsnShare);
        checkCategory(protocol::liAS_NODE, false, false, TrafficCount::Category::GlAsnGet);

        checkCategory(protocol::liBASE, true, false, TrafficCount::Category::GlShare);
        checkCategory(protocol::liBASE, false, true, TrafficCount::Category::GlShare);
        checkCategory(protocol::liBASE, false, false, TrafficCount::Category::GlGet);
    }

    void
    testGetObjectByHashCategorize()
    {
        testcase("get-object-by-hash categorize");

        auto checkCategory = [&](protocol::TMGetObjectByHash::ObjectType type,
                                 bool query,
                                 bool inbound,
                                 TrafficCount::Category category) {
            protocol::TMGetObjectByHash message;
            message.set_type(type);
            message.set_query(query);

            BEAST_EXPECT(
                TrafficCount::categorize(message, protocol::mtGET_OBJECTS, inbound) == category);
        };

        checkCategory(
            protocol::TMGetObjectByHash::otLEDGER,
            true,
            true,
            TrafficCount::Category::ShareHashLedger);
        checkCategory(
            protocol::TMGetObjectByHash::otLEDGER,
            true,
            false,
            TrafficCount::Category::GetHashLedger);

        checkCategory(
            protocol::TMGetObjectByHash::otTRANSACTION,
            false,
            false,
            TrafficCount::Category::ShareHashTx);
        checkCategory(
            protocol::TMGetObjectByHash::otTRANSACTION,
            false,
            true,
            TrafficCount::Category::GetHashTx);

        checkCategory(
            protocol::TMGetObjectByHash::otTRANSACTION_NODE,
            true,
            true,
            TrafficCount::Category::ShareHashTxnode);
        checkCategory(
            protocol::TMGetObjectByHash::otTRANSACTION_NODE,
            true,
            false,
            TrafficCount::Category::GetHashTxnode);

        checkCategory(
            protocol::TMGetObjectByHash::otSTATE_NODE,
            false,
            false,
            TrafficCount::Category::ShareHashAsnode);
        checkCategory(
            protocol::TMGetObjectByHash::otSTATE_NODE,
            false,
            true,
            TrafficCount::Category::GetHashAsnode);

        checkCategory(
            protocol::TMGetObjectByHash::otCAS_OBJECT,
            true,
            true,
            TrafficCount::Category::ShareCasObject);
        checkCategory(
            protocol::TMGetObjectByHash::otCAS_OBJECT,
            true,
            false,
            TrafficCount::Category::GetCasObject);

        checkCategory(
            protocol::TMGetObjectByHash::otFETCH_PACK,
            false,
            false,
            TrafficCount::Category::ShareFetchPack);
        checkCategory(
            protocol::TMGetObjectByHash::otFETCH_PACK,
            false,
            true,
            TrafficCount::Category::GetFetchPack);

        checkCategory(
            protocol::TMGetObjectByHash::otTRANSACTIONS,
            true,
            true,
            TrafficCount::Category::GetTransactions);

        checkCategory(
            protocol::TMGetObjectByHash::otUNKNOWN, true, true, TrafficCount::Category::ShareHash);
        checkCategory(
            protocol::TMGetObjectByHash::otUNKNOWN, true, false, TrafficCount::Category::GetHash);
    }

    struct TestCase
    {
        std::string name;
        int size;
        bool inbound;
        int messageCount;
        std::uint64_t expectedBytesIn;
        std::uint64_t expectedBytesOut;
        std::uint64_t expectedMessagesIn;
        std::uint64_t expectedMessagesOut;
    };

    void
    testAddCount()
    {
        auto run = [&](TestCase const& tc) {
            testcase(tc.name);
            TrafficCount traffic;

            auto const counts = traffic.getCounts();
            std::ranges::for_each(counts, [&](auto const& pair) {
                for (auto i = 0; i < tc.messageCount; ++i)
                    traffic.addCount(pair.first, tc.inbound, tc.size);
            });

            auto const countsNew = traffic.getCounts();
            std::ranges::for_each(countsNew, [&](auto const& pair) {
                BEAST_EXPECT(pair.second.bytesIn.load() == tc.expectedBytesIn);
                BEAST_EXPECT(pair.second.bytesOut.load() == tc.expectedBytesOut);
                BEAST_EXPECT(pair.second.messagesIn.load() == tc.expectedMessagesIn);
                BEAST_EXPECT(pair.second.messagesOut.load() == tc.expectedMessagesOut);
            });
        };

        auto const testcases = {
            TestCase{
                .name = "zero-counts",
                .size = 0,
                .inbound = false,
                .messageCount = 0,
                .expectedBytesIn = 0,
                .expectedBytesOut = 0,
                .expectedMessagesIn = 0,
                .expectedMessagesOut = 0,
            },
            TestCase{
                .name = "inbound-counts",
                .size = 10,
                .inbound = true,
                .messageCount = 10,
                .expectedBytesIn = 100,
                .expectedBytesOut = 0,
                .expectedMessagesIn = 10,
                .expectedMessagesOut = 0,
            },
            TestCase{
                .name = "outbound-counts",
                .size = 10,
                .inbound = false,
                .messageCount = 10,
                .expectedBytesIn = 0,
                .expectedBytesOut = 100,
                .expectedMessagesIn = 0,
                .expectedMessagesOut = 10,
            },
        };

        for (auto const& tc : testcases)
            run(tc);
    }

    void
    testToString()
    {
        testcase("category-to-string");

        // known category returns known string value
        BEAST_EXPECT(TrafficCount::toString(TrafficCount::Category::Total) == "total");

        // return "unknown" for unknown categories
        BEAST_EXPECT(
            TrafficCount::toString(static_cast<TrafficCount::Category>(1000)) == "unknown");
    }

    void
    run() override
    {
        testCategorize();
        testLedgerDataCategorize();
        testGetLedgerCategorize();
        testGetObjectByHashCategorize();
        testAddCount();
        testToString();
    }
};

BEAST_DEFINE_TESTSUITE(traffic_count, overlay, xrpl);

}  // namespace xrpl::test
