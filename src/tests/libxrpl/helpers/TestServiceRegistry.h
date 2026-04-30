#pragma once

#include <xrpl/basics/Log.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/core/HashRouter.h>
#include <xrpl/core/NetworkIDService.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/PendingSaves.h>
#include <xrpl/server/LoadFeeTrack.h>

#include <boost/asio/io_context.hpp>

#include <helpers/TestFamily.h>
#include <helpers/TestSink.h>

#include <optional>
#include <stdexcept>

namespace xrpl {
namespace test {

/** Logs implementation that creates TestSink instances. */
class TestLogs : public Logs
{
public:
    explicit TestLogs(beast::severities::Severity level = beast::severities::kWarning) : Logs(level)
    {
    }

    std::unique_ptr<beast::Journal::Sink>
    makeSink(std::string const&, beast::severities::Severity threshold) override
    {
        return std::make_unique<TestSink>(threshold);
    }
};

/** Simple NetworkIDService implementation for tests. */
class TestNetworkIDService final : public NetworkIDService
{
public:
    explicit TestNetworkIDService(std::uint32_t networkID = 0) : networkID_(networkID)
    {
    }

    [[nodiscard]] std::uint32_t
    getNetworkID() const noexcept override
    {
        return networkID_;
    }

private:
    std::uint32_t networkID_;
};

/** Test implementation of ServiceRegistry for unit tests.

    This class provides real implementations for services that can be
    instantiated from libxrpl (such as Logs, io_context, caches), and
    throws std::logic_error for services that require the full Application.

    Tests can subclass this to provide additional services they need.
*/
class TestServiceRegistry : public ServiceRegistry
{
    TestLogs logs_{beast::severities::kWarning};
    boost::asio::io_context io_context_;
    TestFamily family_{logs_.journal("TestFamily")};
    LoadFeeTrack feeTrack_{logs_.journal("LoadFeeTrack")};
    TestNetworkIDService networkIDService_;
    HashRouter hashRouter_{HashRouter::Setup{}, stopwatch()};
    NodeCache tempNodeCache_{
        "TempNodeCache",
        16384,
        std::chrono::minutes{1},
        stopwatch(),
        logs_.journal("TaggedCache")};
    CachedSLEs cachedSLEs_{
        "CachedSLEs",
        16384,
        std::chrono::minutes{1},
        stopwatch(),
        logs_.journal("TaggedCache")};
    PendingSaves pendingSaves_;
    std::optional<uint256> trapTxID_;

public:
    TestServiceRegistry() = default;
    ~TestServiceRegistry() override = default;

    // Core infrastructure services
    CollectorManager&
    getCollectorManager() override
    {
        throw std::logic_error("TestServiceRegistry::getCollectorManager() not implemented");
    }

    Family&
    getNodeFamily() override
    {
        return family_;
    }

    TimeKeeper&
    getTimeKeeper() override
    {
        throw std::logic_error("TestServiceRegistry::timeKeeper() not implemented");
    }

    JobQueue&
    getJobQueue() override
    {
        throw std::logic_error("TestServiceRegistry::getJobQueue() not implemented");
    }

    NodeCache&
    getTempNodeCache() override
    {
        return tempNodeCache_;
    }

    CachedSLEs&
    getCachedSLEs() override
    {
        return cachedSLEs_;
    }

    NetworkIDService&
    getNetworkIDService() override
    {
        return networkIDService_;
    }

    // Protocol and validation services
    AmendmentTable&
    getAmendmentTable() override
    {
        throw std::logic_error("TestServiceRegistry::getAmendmentTable() not implemented");
    }

    HashRouter&
    getHashRouter() override
    {
        return hashRouter_;
    }

    LoadFeeTrack&
    getFeeTrack() override
    {
        return feeTrack_;
    }

    LoadManager&
    getLoadManager() override
    {
        throw std::logic_error("TestServiceRegistry::getLoadManager() not implemented");
    }

    RCLValidations&
    getValidations() override
    {
        throw std::logic_error("TestServiceRegistry::getValidations() not implemented");
    }

    ValidatorList&
    getValidators() override
    {
        throw std::logic_error("TestServiceRegistry::validators() not implemented");
    }

    ValidatorSite&
    getValidatorSites() override
    {
        throw std::logic_error("TestServiceRegistry::validatorSites() not implemented");
    }

    ManifestCache&
    getValidatorManifests() override
    {
        throw std::logic_error("TestServiceRegistry::validatorManifests() not implemented");
    }

    ManifestCache&
    getPublisherManifests() override
    {
        throw std::logic_error("TestServiceRegistry::publisherManifests() not implemented");
    }

    // Network services
    Overlay&
    getOverlay() override
    {
        throw std::logic_error("TestServiceRegistry::overlay() not implemented");
    }

    Cluster&
    getCluster() override
    {
        throw std::logic_error("TestServiceRegistry::cluster() not implemented");
    }

    PeerReservationTable&
    getPeerReservations() override
    {
        throw std::logic_error("TestServiceRegistry::peerReservations() not implemented");
    }

    Resource::Manager&
    getResourceManager() override
    {
        throw std::logic_error("TestServiceRegistry::getResourceManager() not implemented");
    }

    // Storage services
    NodeStore::Database&
    getNodeStore() override
    {
        throw std::logic_error("TestServiceRegistry::getNodeStore() not implemented");
    }

    SHAMapStore&
    getSHAMapStore() override
    {
        throw std::logic_error("TestServiceRegistry::getSHAMapStore() not implemented");
    }

    RelationalDatabase&
    getRelationalDatabase() override
    {
        throw std::logic_error("TestServiceRegistry::getRelationalDatabase() not implemented");
    }

    // Ledger services
    InboundLedgers&
    getInboundLedgers() override
    {
        throw std::logic_error("TestServiceRegistry::getInboundLedgers() not implemented");
    }

    InboundTransactions&
    getInboundTransactions() override
    {
        throw std::logic_error("TestServiceRegistry::getInboundTransactions() not implemented");
    }

    TaggedCache<uint256, AcceptedLedger>&
    getAcceptedLedgerCache() override
    {
        throw std::logic_error("TestServiceRegistry::getAcceptedLedgerCache() not implemented");
    }

    LedgerMaster&
    getLedgerMaster() override
    {
        throw std::logic_error("TestServiceRegistry::getLedgerMaster() not implemented");
    }

    LedgerCleaner&
    getLedgerCleaner() override
    {
        throw std::logic_error("TestServiceRegistry::getLedgerCleaner() not implemented");
    }

    LedgerReplayer&
    getLedgerReplayer() override
    {
        throw std::logic_error("TestServiceRegistry::getLedgerReplayer() not implemented");
    }

    PendingSaves&
    getPendingSaves() override
    {
        return pendingSaves_;
    }

    OpenLedger&
    getOpenLedger() override
    {
        throw std::logic_error("TestServiceRegistry::openLedger() not implemented");
    }

    OpenLedger const&
    getOpenLedger() const override
    {
        throw std::logic_error("TestServiceRegistry::openLedger() const not implemented");
    }

    // Transaction and operation services
    NetworkOPs&
    getOPs() override
    {
        throw std::logic_error("TestServiceRegistry::getOPs() not implemented");
    }

    OrderBookDB&
    getOrderBookDB() override
    {
        throw std::logic_error("TestServiceRegistry::getOrderBookDB() not implemented");
    }

    TransactionMaster&
    getMasterTransaction() override
    {
        throw std::logic_error("TestServiceRegistry::getMasterTransaction() not implemented");
    }

    TxQ&
    getTxQ() override
    {
        throw std::logic_error("TestServiceRegistry::getTxQ() not implemented");
    }

    PathRequestManager&
    getPathRequestManager() override
    {
        throw std::logic_error("TestServiceRegistry::getPathRequestManager() not implemented");
    }

    // Server services
    ServerHandler&
    getServerHandler() override
    {
        throw std::logic_error("TestServiceRegistry::getServerHandler() not implemented");
    }

    perf::PerfLog&
    getPerfLog() override
    {
        throw std::logic_error("TestServiceRegistry::getPerfLog() not implemented");
    }

    // Configuration and state
    bool
    isStopping() const override
    {
        return false;
    }

    beast::Journal
    getJournal(std::string const& name) override
    {
        return logs_.journal(name);
    }

    boost::asio::io_context&
    getIOContext() override
    {
        return io_context_;
    }

    Logs&
    getLogs() override
    {
        return logs_;
    }

    std::optional<uint256> const&
    getTrapTxID() const override
    {
        return trapTxID_;
    }

    DatabaseCon&
    getWalletDB() override
    {
        throw std::logic_error("TestServiceRegistry::getWalletDB() not implemented");
    }

    // Temporary: Get the underlying Application
    Application&
    getApp() override
    {
        throw std::logic_error(
            "TestServiceRegistry::app() not implemented - no Application available in tests");
    }
};

}  // namespace test
}  // namespace xrpl
