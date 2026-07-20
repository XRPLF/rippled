/**
 * GTest unit tests for MetricsRegistry (no-op / telemetry-disabled path).
 *
 *  Tests cover:
 *  - Construction with telemetry disabled (no-op behavior).
 *  - start()/stop() lifecycle when disabled.
 *  - Synchronous instrument recording methods do not crash when disabled.
 *  - Double stop() is safe.
 *  - Destructor handles cleanup without crash.
 *
 *  NOTE: These tests only exercise the no-op path (telemetry disabled).
 *  When XRPL_ENABLE_TELEMETRY is defined, MetricsRegistry.cpp pulls in
 *  xrpld symbols that cannot be linked into this standalone test binary,
 *  so the tests are compiled out.
 */

// When telemetry is globally enabled, MetricsRegistry.cpp requires xrpld
// link dependencies we cannot satisfy in a standalone GTest binary.
#ifndef XRPL_ENABLE_TELEMETRY

#include <xrpld/telemetry/MetricsRegistry.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/core/ServiceRegistry.h>

#include <boost/asio/io_context.hpp>

#include <gtest/gtest.h>

#include <optional>
#include <stdexcept>
#include <string>

using namespace xrpl;

namespace {

/**
 * Minimal mock ServiceRegistry for MetricsRegistry testing.
 *
 *  Only the getMetricsRegistry() call is used in the tests; other methods
 *  are not invoked because the registry is disabled (enabled=false) so no
 *  gauge callbacks execute.
 *
 *  All pure virtual methods throw to catch accidental calls during tests.
 */
class MockServiceRegistry : public ServiceRegistry
{
    [[noreturn]] static void
    throwUnimplemented()
    {
        throw std::logic_error("MockServiceRegistry: method not implemented");
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
    [[nodiscard]] OpenLedger&
    getOpenLedger() override
    {
        throwUnimplemented();
    }
    [[nodiscard]] OpenLedger const&
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
    [[nodiscard]] bool
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
    [[nodiscard]] std::optional<uint256> const&
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

/**
 * Test fixture that provides a MockServiceRegistry and null Journal.
 */
class MetricsRegistryTest : public ::testing::Test
{
protected:
    MockServiceRegistry mockApp_;
    beast::Journal j_{beast::Journal::getNullSink()};
};

}  // namespace

TEST_F(MetricsRegistryTest, disabled_construction)
{
    // Construct with enabled=false; should be a no-op.
    telemetry::MetricsRegistry const registry(false, mockApp_, j_);
    EXPECT_FALSE(registry.isEnabled());
}

TEST_F(MetricsRegistryTest, disabled_start_stop)
{
    telemetry::MetricsRegistry registry(false, mockApp_, j_);

    // start() and stop() should be no-ops when disabled.
    registry.start("http://localhost:4318/v1/metrics");
    registry.stop();

    // Double stop should be safe.
    registry.stop();
}

TEST_F(MetricsRegistryTest, disabled_recording_methods)
{
    telemetry::MetricsRegistry registry(false, mockApp_, j_);
    registry.start("http://localhost:4318/v1/metrics");

    // All recording methods should be no-ops (not crash).
    registry.recordRpcStarted("server_info");
    registry.recordRpcFinished("server_info", 1000);
    registry.recordRpcErrored("ledger", 500);
    registry.recordJobQueued("ledgerData");
    registry.recordJobStarted("ledgerData", 200);
    registry.recordJobFinished("ledgerData", 3000);

    registry.stop();
}

TEST_F(MetricsRegistryTest, destructor_calls_stop)
{
    {
        // Let the destructor handle cleanup.
        telemetry::MetricsRegistry registry(false, mockApp_, j_);
        registry.start("http://localhost:4318/v1/metrics");
    }
    // If we get here without crash, the destructor handled stop.
}

#endif  // !XRPL_ENABLE_TELEMETRY
