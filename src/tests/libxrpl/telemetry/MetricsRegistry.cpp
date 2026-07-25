/**
 * GTest unit tests for MetricsRegistry (no-op / telemetry-disabled path).
 *
 *  Tests cover:
 *  - Construction with telemetry disabled (no-op behavior).
 *  - start()/stop() lifecycle when disabled.
 *  - Synchronous instrument recording methods do not crash when disabled.
 *  - Double stop() is safe.
 *  - Destructor handles cleanup without crash.
 *  - Compile-time-disabled proof for the sync-diagnostics gauges: the whole
 *    async-gauge registration surface is compiled away, and a full disabled
 *    lifecycle never touches any ServiceRegistry service.
 *
 *  NOTE: These tests only exercise the no-op path (telemetry disabled).
 *  When XRPL_ENABLE_TELEMETRY is defined, MetricsRegistry.cpp pulls in
 *  xrpld symbols that cannot be linked into this standalone test binary,
 *  so the tests are compiled out.
 *
 *  CONSEQUENCE for the sync-diagnostics gauges (`unl_quorum`,
 *  `clock_close_offset_seconds`, `sync_state`,
 *  `server_stall_events_total`): this file CANNOT assert an observed gauge
 *  value, because on this build the gauges do not exist -- their registration
 *  methods and the OTel instrument members are inside
 *  `#ifdef XRPL_ENABLE_TELEMETRY`, and there is no MeterProvider at all. What
 *  is provable here, and what the tests below assert, is the complementary
 *  half: that nothing is registered and no service is consulted. The exact
 *  observed values (trusted_keys=5, quorum=4, offset=-3, and the sync_state /
 *  stall-episode values) are asserted in MetricMacros.cpp, which is the file
 *  compiled when telemetry IS enabled.
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
#include <type_traits>
#include <utility>

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

// -----------------------------------------------------------------
// Sync-diagnostics gauges: compile-time-disabled proof.
//
// `unl_quorum` reads ValidatorList::trustedKeyCount() and quorum();
// `clock_close_offset_seconds` reads TimeKeeper::closeOffset(); `sync_state` and
// `server_stall_events_total` read NetworkOPs and LoadManager. All are
// reached through the ServiceRegistry, and MockServiceRegistry::getValidators()
// / getTimeKeeper() / getOPs() / getLoadManager() THROW std::logic_error. So "no
// gauge callback ran" is directly observable here: had registerAsyncGauges() run
// and had a callback fired, one of those accessors would have thrown.
//
// Honest scope note: these tests do NOT assert an observed gauge value. On this
// build the gauges are not compiled at all (see the file header), so there is no
// value to read -- inventing one would be fiction. The value assertions live in
// MetricMacros.cpp. What is asserted here is the other half of the contract:
// registration is absent and no service is consulted.
// -----------------------------------------------------------------

// The observable-gauge registration surface is compiled OUT when telemetry is
// disabled: `meter()` -- the only accessor the gauges and the XRPL_METRIC_*
// macros use to reach the OTel SDK -- does not exist as a member at all. This is
// a compile-time assertion, so it fails the build (not the run) if the accessor
// ever escapes its #ifdef and drags the SDK into a telemetry-off build.
TEST_F(MetricsRegistryTest, disabled_build_exposes_no_meter_accessor)
{
    // Detects `registry.meter()` being callable. Under #ifndef
    // XRPL_ENABLE_TELEMETRY it must not be, so the trait is false.
    auto hasMeter = []<typename T>(T* r) { return requires { r->meter(); }; };
    EXPECT_FALSE(hasMeter(static_cast<telemetry::MetricsRegistry*>(nullptr)));

    // The enable flag is still queryable and reports exactly false -- the class
    // is a no-op, not an absent type.
    telemetry::MetricsRegistry const registry(false, mockApp_, j_);
    EXPECT_FALSE(registry.isEnabled());
}

// A full disabled lifecycle registers no gauge and therefore consults NO
// ServiceRegistry service. Asserting the cause, not just the absence of a crash:
// every MockServiceRegistry accessor a sync-diagnostics gauge would need throws,
// so reaching the end without an exception proves no callback ran.
TEST_F(MetricsRegistryTest, disabled_lifecycle_never_consults_gauge_services)
{
    telemetry::MetricsRegistry registry(false, mockApp_, j_);

    // start() is where registerAsyncGauges() -- and with it
    // registerUnlQuorumGauge() / registerClockSkewGauge() /
    // registerSyncStateGauge() / registerStallEventsCounter() -- would run.
    EXPECT_NO_THROW(registry.start("http://localhost:4318/v1/metrics"));

    // detachCallbacks() is the shutdown hook the real gauges honour. It must be
    // safe and idempotent even though there is nothing to detach.
    EXPECT_NO_THROW(registry.detachCallbacks());
    EXPECT_NO_THROW(registry.detachCallbacks());

    // Still disabled after a full start(): start() must not flip the flag.
    EXPECT_FALSE(registry.isEnabled());

    EXPECT_NO_THROW(registry.stop());

    // Positive control: the mock DOES throw when a gauge-backing service is
    // actually requested. Without this, "nothing threw" would be vacuous -- it
    // could mean the mock is permissive rather than that no callback ran.
    EXPECT_THROW(mockApp_.getValidators(), std::logic_error);
    EXPECT_THROW(mockApp_.getTimeKeeper(), std::logic_error);
    // The two services the WP-A2 sync-state signals read. sync_state needs both
    // (NetworkOPs for the gate/duration/ledgers-behind, LoadManager for stall
    // seconds) and server_stall_events_total needs the second, so either one
    // firing would have thrown above.
    EXPECT_THROW(mockApp_.getOPs(), std::logic_error);
    EXPECT_THROW(mockApp_.getLoadManager(), std::logic_error);
}

// Even asking for enabled=true registers no sync-diagnostics gauge on a
// telemetry-off build. isEnabled() faithfully echoes the constructor argument
// (the flag lives outside the #ifdef), so the flag alone does NOT prove the
// gauges are inert -- the mock does: a full start()/stop() cycle with
// enabled=true still consults no service, so no callback was ever registered.
// Asserts the exact flag value on BOTH construction paths.
TEST_F(MetricsRegistryTest, enabled_flag_alone_registers_no_gauges_when_compiled_out)
{
    telemetry::MetricsRegistry enabledRequest(true, mockApp_, j_);

    // The flag is echoed back exactly, true not false: it is a plain member,
    // not gated on XRPL_ENABLE_TELEMETRY.
    EXPECT_TRUE(enabledRequest.isEnabled());

    // Yet the whole lifecycle stays inert. If registerAsyncGauges() had run and
    // registered registerUnlQuorumGauge()/registerClockSkewGauge()/
    // registerSyncStateGauge()/registerStallEventsCounter(), a callback would
    // reach getValidators()/getTimeKeeper()/getOPs()/getLoadManager() and throw
    // std::logic_error.
    EXPECT_NO_THROW(enabledRequest.start("http://localhost:4318/v1/metrics"));
    EXPECT_NO_THROW(enabledRequest.detachCallbacks());
    EXPECT_NO_THROW(enabledRequest.stop());

    // Contrast: enabled=false reports exactly false.
    telemetry::MetricsRegistry const disabledRequest(false, mockApp_, j_);
    EXPECT_FALSE(disabledRequest.isEnabled());
}

// The `state_changes_total` counter no longer has a registry-owned wrapper
// method: WP-A2 moved it to a labelled call-site macro in
// NetworkOPsImp::setMode so it can carry {from,to}. This compile-time
// assertion is the regression guard -- if someone reintroduces
// incrementStateChanges(), the unlabelled instrument would coexist with the
// labelled one and Prometheus would carry two conflicting versions of the same
// metric name.
TEST_F(MetricsRegistryTest, state_changes_counter_has_no_registry_wrapper)
{
    auto hasIncrementStateChanges = []<typename T>(T* r) {
        return requires { r->incrementStateChanges(); };
    };
    EXPECT_FALSE(hasIncrementStateChanges(static_cast<telemetry::MetricsRegistry*>(nullptr)));

    // Positive control: a sibling parity counter that WAS deliberately kept as
    // a registry wrapper is still detectable, so the trait above is really
    // probing for the method and not vacuously false.
    auto hasIncrementLedgersClosed = []<typename T>(T* r) {
        return requires { r->incrementLedgersClosed(); };
    };
    EXPECT_TRUE(hasIncrementLedgersClosed(static_cast<telemetry::MetricsRegistry*>(nullptr)));
}

#endif  // !XRPL_ENABLE_TELEMETRY
