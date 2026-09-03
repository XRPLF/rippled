#pragma once

#include <xrpl/basics/Log.h>
#include <xrpl/basics/UnorderedContainers.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/core/HashRouter.h>
#include <xrpl/core/NetworkIDService.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/AmendmentTable.h>
#include <xrpl/ledger/PendingSaves.h>
#include <xrpl/ledger/View.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/STValidation.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/server/LoadFeeTrack.h>

#include <boost/asio/io_context.hpp>

#include <helpers/TestFamily.h>
#include <helpers/TestSink.h>

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace xrpl::test {

/**
 * Logs implementation that creates TestSink instances.
 */
class TestLogs : public Logs
{
public:
    explicit TestLogs(beast::Severity level = beast::Severity::Warning) : Logs(level)
    {
    }

    std::unique_ptr<beast::Journal::Sink>
    makeSink(std::string const&, beast::Severity threshold) override
    {
        return std::make_unique<TestSink>(threshold);
    }
};

/**
 * Minimal AmendmentTable for tests.
 */
class TestAmendmentTable final : public AmendmentTable
{
public:
    [[nodiscard]] uint256
    find(std::string const& name) const override
    {
        return getRegisteredFeature(name).value_or(uint256{});
    }

    bool
    veto(uint256 const&) override
    {
        throw std::logic_error("TestAmendmentTable::veto not implemented");
    }
    bool
    unVeto(uint256 const&) override
    {
        throw std::logic_error("TestAmendmentTable::unVeto not implemented");
    }
    bool
    enable(uint256 const&) override
    {
        throw std::logic_error("TestAmendmentTable::enable not implemented");
    }
    [[nodiscard]] bool
    isEnabled(uint256 const&) const override
    {
        throw std::logic_error("TestAmendmentTable::isEnabled not implemented");
    }
    [[nodiscard]] bool
    isSupported(uint256 const&) const override
    {
        throw std::logic_error("TestAmendmentTable::isSupported not implemented");
    }
    [[nodiscard]] bool
    hasUnsupportedEnabled() const override
    {
        throw std::logic_error("TestAmendmentTable::hasUnsupportedEnabled not implemented");
    }
    [[nodiscard]] std::optional<NetClock::time_point>
    firstUnsupportedExpected() const override
    {
        throw std::logic_error("TestAmendmentTable::firstUnsupportedExpected not implemented");
    }
    [[nodiscard]] json::Value
    getJson(bool) const override
    {
        throw std::logic_error("TestAmendmentTable::getJson not implemented");
    }
    [[nodiscard]] json::Value
    getJson(uint256 const&, bool) const override
    {
        throw std::logic_error("TestAmendmentTable::getJson(amendment) not implemented");
    }
    [[nodiscard]] bool
    needValidatedLedger(LedgerIndex) const override
    {
        throw std::logic_error("TestAmendmentTable::needValidatedLedger not implemented");
    }
    void
    doValidatedLedger(LedgerIndex, std::set<uint256> const&, majorityAmendments_t const&) override
    {
        throw std::logic_error("TestAmendmentTable::doValidatedLedger not implemented");
    }
    void
    trustChanged(hash_set<PublicKey> const&) override
    {
        throw std::logic_error("TestAmendmentTable::trustChanged not implemented");
    }
    std::map<uint256, std::uint32_t>
    doVoting(
        Rules const&,
        NetClock::time_point,
        std::set<uint256> const&,
        majorityAmendments_t const&,
        std::vector<std::shared_ptr<STValidation>> const&) override
    {
        throw std::logic_error("TestAmendmentTable::doVoting not implemented");
    }
    [[nodiscard]] std::vector<uint256>
    doValidation(std::set<uint256> const&) const override
    {
        throw std::logic_error("TestAmendmentTable::doValidation not implemented");
    }
    [[nodiscard]] std::vector<uint256>
    getDesired() const override
    {
        throw std::logic_error("TestAmendmentTable::getDesired not implemented");
    }
};

/**
 * Simple NetworkIDService implementation for tests.
 */
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

/**
 * Test implementation of ServiceRegistry for unit tests.
 *
 * This class provides real implementations for services that can be
 * instantiated from libxrpl (such as Logs, io_context, caches), and
 * throws std::logic_error for services that require the full Application.
 *
 * Tests can subclass this to provide additional services they need.
 */
class TestServiceRegistry : public ServiceRegistry
{
    static Fees
    defaultFees()
    {
        Fees fees{XRPAmount{10}, XRPAmount{10 * kDropsPerXrp}, XRPAmount{2 * kDropsPerXrp}};
        fees.gasLimit = 1'000'000;
        fees.bytecodeSizeLimit = 100'000;
        fees.gasPrice = 1'000'000;
        return fees;
    }

    TestLogs logs_{beast::Severity::Warning};
    boost::asio::io_context ioContext_;
    TestFamily family_{logs_.journal("TestFamily")};
    LoadFeeTrack feeTrack_{logs_.journal("LoadFeeTrack")};
    TestNetworkIDService networkIDService_;
    Fees fees_{defaultFees()};
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
    TestAmendmentTable amendmentTable_;

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
        return amendmentTable_;
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

    resource::Manager&
    getResourceManager() override
    {
        throw std::logic_error("TestServiceRegistry::getResourceManager() not implemented");
    }

    // Storage services
    node_store::Database&
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
        return ioContext_;
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

    Fees
    getFees() const override
    {
        return fees_;
    }

    // Temporary: Get the underlying Application
    Application&
    getApp() override
    {
        throw std::logic_error(
            "TestServiceRegistry::app() not implemented - no Application available in tests");
    }
};

}  // namespace xrpl::test
