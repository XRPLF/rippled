#include <xrpld/overlay/detail/TxMetrics.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/protocol/jss.h>

#include <xrpl.pb.h>

#include <chrono>
#include <cstdint>
#include <string>

namespace xrpl::test {

class tx_metrics_test : public beast::unit_test::suite
{
    static void
    rewind(metrics::SingleMetrics& metrics)
    {
        using namespace std::chrono_literals;
        metrics.intervalStart = metrics::SingleMetrics::clock_type::now() - 1s;
    }

    static void
    rewind(metrics::MultipleMetrics& metrics)
    {
        rewind(metrics.m1);
        rewind(metrics.m2);
    }

    void
    expectJson(Json::Value const& json, Json::StaticString const& key, std::uint64_t value)
    {
        BEAST_EXPECT(json[key].asString() == std::to_string(value));
    }

public:
    void
    testProtocolMetrics()
    {
        testcase("protocol metrics");

        metrics::TxMetrics metrics;

        rewind(metrics.tx);
        metrics.addMetrics(protocol::MessageType::mtTRANSACTION, 3000);

        rewind(metrics.haveTx);
        metrics.addMetrics(protocol::MessageType::mtHAVE_TRANSACTIONS, 6000);

        rewind(metrics.getLedger);
        metrics.addMetrics(protocol::MessageType::mtGET_LEDGER, 9000);

        rewind(metrics.ledgerData);
        metrics.addMetrics(protocol::MessageType::mtLEDGER_DATA, 12000);

        rewind(metrics.transactions);
        metrics.addMetrics(protocol::MessageType::mtTRANSACTIONS, 15000);

        metrics.addMetrics(static_cast<protocol::MessageType>(99), 3000);

        auto const json = metrics.json();
        expectJson(json, jss::txr_tx_sz, 100);
        expectJson(json, jss::txr_have_txs_sz, 200);
        expectJson(json, jss::txr_get_ledger_sz, 300);
        expectJson(json, jss::txr_ledger_data_sz, 400);
        expectJson(json, jss::txr_transactions_sz, 500);
    }

    void
    testPeerAndMissingTxMetrics()
    {
        testcase("peer and missing transaction metrics");

        metrics::TxMetrics metrics;

        rewind(metrics.selectedPeers);
        rewind(metrics.suppressedPeers);
        rewind(metrics.notEnabled);
        metrics.addMetrics(90, 60, 30);

        rewind(metrics.missingTx);
        metrics.addMetrics(3000);

        auto const json = metrics.json();
        expectJson(json, jss::txr_selected_cnt, 3);
        expectJson(json, jss::txr_suppressed_cnt, 2);
        expectJson(json, jss::txr_not_enabled_cnt, 1);
        expectJson(json, jss::txr_missing_tx_freq, 100);
    }

    void
    run() override
    {
        testProtocolMetrics();
        testPeerAndMissingTxMetrics();
    }
};

BEAST_DEFINE_TESTSUITE(tx_metrics, overlay, xrpl);

}  // namespace xrpl::test
