/** Unit tests for MetricsRegistry.

    Tests cover:
    - Construction with telemetry disabled (no-op behavior).
    - start()/stop() lifecycle when disabled.
    - Synchronous instrument recording methods do not crash when disabled.
    - Double stop() is safe.

    NOTE: Tests that exercise the OTel SDK path require XRPL_ENABLE_TELEMETRY
    to be defined at build time (telemetry=ON). The no-op path tests run
    unconditionally.
*/

#include <xrpld/telemetry/MetricsRegistry.h>

#include <xrpl/beast/unit_test.h>
#include <xrpl/core/ServiceRegistry.h>

namespace xrpl {
namespace test {

/** Minimal mock ServiceRegistry for MetricsRegistry testing.

    Only the getMetricsRegistry() call is used in the tests; other methods
    are not invoked because the registry is disabled (enabled=false) so no
    gauge callbacks execute.

    All pure virtual methods throw to catch accidental calls during tests.
*/
class MockServiceRegistry : public ServiceRegistry
{
    [[noreturn]] static void
    throwUnimplemented()
    {
        Throw<std::logic_error>("MockServiceRegistry: method not implemented");
    }

public:
    // ServiceRegistry interface — stubs that should never be called.
    CollectorManager&
    getCollectorManager() override
    {
        throwUnimplemented();
    }
    Family&
    getNodeFamily() override
    {
        throwUnimplemented();
    }
    TimeKeeper&
    getTimeKeeper() override
    {
        throwUnimplemented();
    }
    JobQueue&
    getJobQueue() override
    {
        throwUnimplemented();
    }
    NodeCache&
    getTempNodeCache() override
    {
        throwUnimplemented();
    }
    CachedSLEs&
    getCachedSLEs() override
    {
        throwUnimplemented();
    }
    NetworkIDService&
    getNetworkIDService() override
    {
        throwUnimplemented();
    }
    AmendmentTable&
    getAmendmentTable() override
    {
        throwUnimplemented();
    }
    HashRouter&
    getHashRouter() override
    {
        throwUnimplemented();
    }
    LoadFeeTrack&
    getFeeTrack() override
    {
        throwUnimplemented();
    }
    LoadManager&
    getLoadManager() override
    {
        throwUnimplemented();
    }
    RCLValidations&
    getValidations() override
    {
        throwUnimplemented();
    }
    ValidatorList&
    getValidators() override
    {
        throwUnimplemented();
    }
    ValidatorSite&
    getValidatorSites() override
    {
        throwUnimplemented();
    }
    ManifestCache&
    getValidatorManifests() override
    {
        throwUnimplemented();
    }
    ManifestCache&
    getPublisherManifests() override
    {
        throwUnimplemented();
    }
    Overlay&
    getOverlay() override
    {
        throwUnimplemented();
    }
    Cluster&
    getCluster() override
    {
        throwUnimplemented();
    }
    PeerReservationTable&
    getPeerReservations() override
    {
        throwUnimplemented();
    }
    Resource::Manager&
    getResourceManager() override
    {
        throwUnimplemented();
    }
    NodeStore::Database&
    getNodeStore() override
    {
        throwUnimplemented();
    }
    SHAMapStore&
    getSHAMapStore() override
    {
        throwUnimplemented();
    }
    RelationalDatabase&
    getRelationalDatabase() override
    {
        throwUnimplemented();
    }
    InboundLedgers&
    getInboundLedgers() override
    {
        throwUnimplemented();
    }
    InboundTransactions&
    getInboundTransactions() override
    {
        throwUnimplemented();
    }
    TaggedCache<uint256, AcceptedLedger>&
    getAcceptedLedgerCache() override
    {
        throwUnimplemented();
    }
    LedgerMaster&
    getLedgerMaster() override
    {
        throwUnimplemented();
    }
    LedgerCleaner&
    getLedgerCleaner() override
    {
        throwUnimplemented();
    }
    LedgerReplayer&
    getLedgerReplayer() override
    {
        throwUnimplemented();
    }
    PendingSaves&
    getPendingSaves() override
    {
        throwUnimplemented();
    }
    OpenLedger&
    getOpenLedger() override
    {
        throwUnimplemented();
    }
    OpenLedger const&
    getOpenLedger() const override
    {
        throwUnimplemented();
    }
    NetworkOPs&
    getOPs() override
    {
        throwUnimplemented();
    }
    OrderBookDB&
    getOrderBookDB() override
    {
        throwUnimplemented();
    }
    TransactionMaster&
    getMasterTransaction() override
    {
        throwUnimplemented();
    }
    TxQ&
    getTxQ() override
    {
        throwUnimplemented();
    }
    PathRequestManager&
    getPathRequestManager() override
    {
        throwUnimplemented();
    }
    ServerHandler&
    getServerHandler() override
    {
        throwUnimplemented();
    }
    perf::PerfLog&
    getPerfLog() override
    {
        throwUnimplemented();
    }
    telemetry::Telemetry&
    getTelemetry() override
    {
        throwUnimplemented();
    }
    telemetry::MetricsRegistry*
    getMetricsRegistry() override
    {
        return nullptr;
    }
    bool
    isStopping() const override
    {
        return false;
    }
    beast::Journal
    getJournal(std::string const&) override
    {
        return beast::Journal(beast::Journal::getNullSink());
    }
    boost::asio::io_context&
    getIOContext() override
    {
        throwUnimplemented();
    }
    Logs&
    getLogs() override
    {
        throwUnimplemented();
    }
    std::optional<uint256> const&
    getTrapTxID() const override
    {
        static std::optional<uint256> const empty;
        return empty;
    }
    DatabaseCon&
    getWalletDB() override
    {
        throwUnimplemented();
    }
    Application&
    getApp() override
    {
        throwUnimplemented();
    }
};

class MetricsRegistry_test : public beast::unit_test::suite
{
    void
    testDisabledConstruction()
    {
        testcase("Disabled construction");

        MockServiceRegistry mockApp;
        beast::Journal j(beast::Journal::getNullSink());

        // Construct with enabled=false; should be a no-op.
        telemetry::MetricsRegistry registry(false, mockApp, j);
        BEAST_EXPECT(!registry.isEnabled());
    }

    void
    testDisabledStartStop()
    {
        testcase("Disabled start/stop");

        MockServiceRegistry mockApp;
        beast::Journal j(beast::Journal::getNullSink());

        telemetry::MetricsRegistry registry(false, mockApp, j);

        // start() and stop() should be no-ops when disabled.
        registry.start("http://localhost:4318/v1/metrics");
        registry.stop();

        // Double stop should be safe.
        registry.stop();

        pass();
    }

    void
    testDisabledRecording()
    {
        testcase("Disabled recording methods");

        MockServiceRegistry mockApp;
        beast::Journal j(beast::Journal::getNullSink());

        telemetry::MetricsRegistry registry(false, mockApp, j);
        registry.start("http://localhost:4318/v1/metrics");

        // All recording methods should be no-ops (not crash).
        registry.recordRpcStarted("server_info");
        registry.recordRpcFinished("server_info", 1000);
        registry.recordRpcErrored("ledger", 500);
        registry.recordJobQueued("ledgerData");
        registry.recordJobStarted("ledgerData", 200);
        registry.recordJobFinished("ledgerData", 3000);

        registry.stop();

        pass();
    }

    void
    testDestructorStops()
    {
        testcase("Destructor calls stop");

        MockServiceRegistry mockApp;
        beast::Journal j(beast::Journal::getNullSink());

        {
            // Let the destructor handle cleanup.
            telemetry::MetricsRegistry registry(false, mockApp, j);
            registry.start("http://localhost:4318/v1/metrics");
        }

        // If we get here without crash, the destructor handled stop.
        pass();
    }

public:
    void
    run() override
    {
        testDisabledConstruction();
        testDisabledStartStop();
        testDisabledRecording();
        testDestructorStops();
    }
};

BEAST_DEFINE_TESTSUITE(MetricsRegistry, telemetry, xrpl);

}  // namespace test
}  // namespace xrpl
