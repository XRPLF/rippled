#include <xrpld/overlay/detail/TrafficCount.h>

#include <xrpl/beast/unit_test/suite.h>

#include <xrpl.pb.h>

#include <algorithm>
#include <cstdint>

namespace xrpl::test {

class traffic_count_test : public beast::unit_test::suite
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
        BEAST_EXPECT(known == TrafficCount::category::base);

        // an unknown message type is categorized as unknown
        auto const unknown =
            TrafficCount::categorize(message, static_cast<protocol::MessageType>(99), false);
        BEAST_EXPECT(unknown == TrafficCount::category::unknown);
    }

    void
    testLedgerDataCategorize()
    {
        testcase("ledger-data categorize");

        auto checkCategory = [&](protocol::TMLedgerInfoType type,
                                 bool inbound,
                                 bool requestCookie,
                                 TrafficCount::category category) {
            protocol::TMLedgerData message;
            message.set_type(type);
            if (requestCookie)
                message.set_requestcookie(1);

            BEAST_EXPECT(
                TrafficCount::categorize(message, protocol::mtLEDGER_DATA, inbound) == category);
        };

        checkCategory(protocol::liTS_CANDIDATE, true, false, TrafficCount::category::ld_tsc_get);
        checkCategory(protocol::liTS_CANDIDATE, true, true, TrafficCount::category::ld_tsc_share);
        checkCategory(protocol::liTS_CANDIDATE, false, false, TrafficCount::category::ld_tsc_share);

        checkCategory(protocol::liTX_NODE, true, false, TrafficCount::category::ld_txn_get);
        checkCategory(protocol::liTX_NODE, true, true, TrafficCount::category::ld_txn_share);
        checkCategory(protocol::liTX_NODE, false, false, TrafficCount::category::ld_txn_share);

        checkCategory(protocol::liAS_NODE, true, false, TrafficCount::category::ld_asn_get);
        checkCategory(protocol::liAS_NODE, true, true, TrafficCount::category::ld_asn_share);
        checkCategory(protocol::liAS_NODE, false, false, TrafficCount::category::ld_asn_share);

        checkCategory(protocol::liBASE, true, false, TrafficCount::category::ld_get);
        checkCategory(protocol::liBASE, true, true, TrafficCount::category::ld_share);
        checkCategory(protocol::liBASE, false, false, TrafficCount::category::ld_share);
    }

    void
    testGetLedgerCategorize()
    {
        testcase("get-ledger categorize");

        auto checkCategory = [&](protocol::TMLedgerInfoType type,
                                 bool inbound,
                                 bool requestCookie,
                                 TrafficCount::category category) {
            protocol::TMGetLedger message;
            message.set_itype(type);
            if (requestCookie)
                message.set_requestcookie(1);

            BEAST_EXPECT(
                TrafficCount::categorize(message, protocol::mtGET_LEDGER, inbound) == category);
        };

        checkCategory(protocol::liTS_CANDIDATE, true, false, TrafficCount::category::gl_tsc_share);
        checkCategory(protocol::liTS_CANDIDATE, false, true, TrafficCount::category::gl_tsc_share);
        checkCategory(protocol::liTS_CANDIDATE, false, false, TrafficCount::category::gl_tsc_get);

        checkCategory(protocol::liTX_NODE, true, false, TrafficCount::category::gl_txn_share);
        checkCategory(protocol::liTX_NODE, false, true, TrafficCount::category::gl_txn_share);
        checkCategory(protocol::liTX_NODE, false, false, TrafficCount::category::gl_txn_get);

        checkCategory(protocol::liAS_NODE, true, false, TrafficCount::category::gl_asn_share);
        checkCategory(protocol::liAS_NODE, false, true, TrafficCount::category::gl_asn_share);
        checkCategory(protocol::liAS_NODE, false, false, TrafficCount::category::gl_asn_get);

        checkCategory(protocol::liBASE, true, false, TrafficCount::category::gl_share);
        checkCategory(protocol::liBASE, false, true, TrafficCount::category::gl_share);
        checkCategory(protocol::liBASE, false, false, TrafficCount::category::gl_get);
    }

    void
    testGetObjectByHashCategorize()
    {
        testcase("get-object-by-hash categorize");

        auto checkCategory = [&](protocol::TMGetObjectByHash::ObjectType type,
                                 bool query,
                                 bool inbound,
                                 TrafficCount::category category) {
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
            TrafficCount::category::share_hash_ledger);
        checkCategory(
            protocol::TMGetObjectByHash::otLEDGER,
            true,
            false,
            TrafficCount::category::get_hash_ledger);

        checkCategory(
            protocol::TMGetObjectByHash::otTRANSACTION,
            false,
            false,
            TrafficCount::category::share_hash_tx);
        checkCategory(
            protocol::TMGetObjectByHash::otTRANSACTION,
            false,
            true,
            TrafficCount::category::get_hash_tx);

        checkCategory(
            protocol::TMGetObjectByHash::otTRANSACTION_NODE,
            true,
            true,
            TrafficCount::category::share_hash_txnode);
        checkCategory(
            protocol::TMGetObjectByHash::otTRANSACTION_NODE,
            true,
            false,
            TrafficCount::category::get_hash_txnode);

        checkCategory(
            protocol::TMGetObjectByHash::otSTATE_NODE,
            false,
            false,
            TrafficCount::category::share_hash_asnode);
        checkCategory(
            protocol::TMGetObjectByHash::otSTATE_NODE,
            false,
            true,
            TrafficCount::category::get_hash_asnode);

        checkCategory(
            protocol::TMGetObjectByHash::otCAS_OBJECT,
            true,
            true,
            TrafficCount::category::share_cas_object);
        checkCategory(
            protocol::TMGetObjectByHash::otCAS_OBJECT,
            true,
            false,
            TrafficCount::category::get_cas_object);

        checkCategory(
            protocol::TMGetObjectByHash::otFETCH_PACK,
            false,
            false,
            TrafficCount::category::share_fetch_pack);
        checkCategory(
            protocol::TMGetObjectByHash::otFETCH_PACK,
            false,
            true,
            TrafficCount::category::get_fetch_pack);

        checkCategory(
            protocol::TMGetObjectByHash::otTRANSACTIONS,
            true,
            true,
            TrafficCount::category::get_transactions);

        checkCategory(
            protocol::TMGetObjectByHash::otUNKNOWN, true, true, TrafficCount::category::share_hash);
        checkCategory(
            protocol::TMGetObjectByHash::otUNKNOWN, true, false, TrafficCount::category::get_hash);
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
            TrafficCount m_traffic;

            auto const counts = m_traffic.getCounts();
            std::ranges::for_each(counts, [&](auto const& pair) {
                for (auto i = 0; i < tc.messageCount; ++i)
                    m_traffic.addCount(pair.first, tc.inbound, tc.size);
            });

            auto const counts_new = m_traffic.getCounts();
            std::ranges::for_each(counts_new, [&](auto const& pair) {
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
        BEAST_EXPECT(TrafficCount::to_string(TrafficCount::category::total) == "total");

        // return "unknown" for unknown categories
        BEAST_EXPECT(
            TrafficCount::to_string(static_cast<TrafficCount::category>(1000)) == "unknown");
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
