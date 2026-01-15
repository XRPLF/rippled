#include <test/jtx/Env.h>

#include <xrpld/app/main/Application.h>
#include <xrpld/core/ServiceRegistryImpl.h>

#include <xrpl/beast/unit_test.h>
#include <xrpl/core/ServiceRegistry.h>

#include <atomic>
#include <chrono>
#include <thread>

namespace xrpl {
namespace test {

//------------------------------------------------------------------------------

/** Mock Application that tracks which methods are called.

    This class wraps a real Application instance and forwards all calls to it,
    while tracking which methods were invoked. This is useful for testing that
    ServiceRegistryImpl correctly delegates to the Application.
*/
class MockApplication : public Application
{
public:
    explicit MockApplication(Application& app) : app_(app)
    {
    }

    // Tracking flags for methods called by ServiceRegistryImpl
    // They need to be mutable to allow modification from const methods
    mutable std::atomic<bool> getCollectorManagerCalled{false};
    mutable std::atomic<bool> getNodeFamilyCalled{false};
    mutable std::atomic<bool> timeKeeperCalled{false};
    mutable std::atomic<bool> getJobQueueCalled{false};
    mutable std::atomic<bool> getTempNodeCacheCalled{false};
    mutable std::atomic<bool> cachedSLEsCalled{false};
    mutable std::atomic<bool> getAmendmentTableCalled{false};
    mutable std::atomic<bool> getHashRouterCalled{false};
    mutable std::atomic<bool> getFeeTrackCalled{false};
    mutable std::atomic<bool> getLoadManagerCalled{false};
    mutable std::atomic<bool> getValidationsCalled{false};
    mutable std::atomic<bool> validatorsCalled{false};
    mutable std::atomic<bool> validatorSitesCalled{false};
    mutable std::atomic<bool> validatorManifestsCalled{false};
    mutable std::atomic<bool> publisherManifestsCalled{false};
    mutable std::atomic<bool> overlayCalled{false};
    mutable std::atomic<bool> clusterCalled{false};
    mutable std::atomic<bool> peerReservationsCalled{false};
    mutable std::atomic<bool> getResourceManagerCalled{false};
    mutable std::atomic<bool> getNodeStoreCalled{false};
    mutable std::atomic<bool> getSHAMapStoreCalled{false};
    mutable std::atomic<bool> getRelationalDatabaseCalled{false};
    mutable std::atomic<bool> getInboundLedgersCalled{false};
    mutable std::atomic<bool> getInboundTransactionsCalled{false};
    mutable std::atomic<bool> getAcceptedLedgerCacheCalled{false};
    mutable std::atomic<bool> getLedgerMasterCalled{false};
    mutable std::atomic<bool> getLedgerCleanerCalled{false};
    mutable std::atomic<bool> getLedgerReplayerCalled{false};
    mutable std::atomic<bool> pendingSavesCalled{false};
    mutable std::atomic<bool> openLedgerCalled{false};
    mutable std::atomic<bool> openLedgerConstCalled{false};
    mutable std::atomic<bool> getOPsCalled{false};
    mutable std::atomic<bool> getOrderBookDBCalled{false};
    mutable std::atomic<bool> getMasterTransactionCalled{false};
    mutable std::atomic<bool> getTxQCalled{false};
    mutable std::atomic<bool> getPathRequestsCalled{false};
    mutable std::atomic<bool> getServerHandlerCalled{false};
    mutable std::atomic<bool> getPerfLogCalled{false};

    // Forward all Application methods to the real application
    MutexType&
    getMasterMutex() override
    {
        return app_.getMasterMutex();
    }

    bool
    setup(boost::program_options::variables_map const& options) override
    {
        return app_.setup(options);
    }

    void
    start(bool withTimers) override
    {
        app_.start(withTimers);
    }

    void
    run() override
    {
        app_.run();
    }

    void
    signalStop(std::string msg) override
    {
        app_.signalStop(std::move(msg));
    }

    bool
    checkSigs() const override
    {
        return app_.checkSigs();
    }

    void
    checkSigs(bool b) override
    {
        app_.checkSigs(b);
    }

    bool
    isStopping() const override
    {
        return app_.isStopping();
    }

    std::uint64_t
    instanceID() const override
    {
        return app_.instanceID();
    }

    Logs&
    logs() override
    {
        return app_.logs();
    }

    Config&
    config() override
    {
        return app_.config();
    }

    boost::asio::io_context&
    getIOContext() override
    {
        return app_.getIOContext();
    }

    CollectorManager&
    getCollectorManager() override
    {
        getCollectorManagerCalled = true;
        return app_.getCollectorManager();
    }

    Family&
    getNodeFamily() override
    {
        getNodeFamilyCalled = true;
        return app_.getNodeFamily();
    }

    TimeKeeper&
    timeKeeper() override
    {
        timeKeeperCalled = true;
        return app_.timeKeeper();
    }

    JobQueue&
    getJobQueue() override
    {
        getJobQueueCalled = true;
        return app_.getJobQueue();
    }

    NodeCache&
    getTempNodeCache() override
    {
        getTempNodeCacheCalled = true;
        return app_.getTempNodeCache();
    }

    CachedSLEs&
    cachedSLEs() override
    {
        cachedSLEsCalled = true;
        return app_.cachedSLEs();
    }

    AmendmentTable&
    getAmendmentTable() override
    {
        getAmendmentTableCalled = true;
        return app_.getAmendmentTable();
    }

    HashRouter&
    getHashRouter() override
    {
        getHashRouterCalled = true;
        return app_.getHashRouter();
    }

    LoadFeeTrack&
    getFeeTrack() override
    {
        getFeeTrackCalled = true;
        return app_.getFeeTrack();
    }

    LoadManager&
    getLoadManager() override
    {
        getLoadManagerCalled = true;
        return app_.getLoadManager();
    }

    Overlay&
    overlay() override
    {
        overlayCalled = true;
        return app_.overlay();
    }

    TxQ&
    getTxQ() override
    {
        getTxQCalled = true;
        return app_.getTxQ();
    }

    ValidatorList&
    validators() override
    {
        validatorsCalled = true;
        return app_.validators();
    }

    ValidatorSite&
    validatorSites() override
    {
        validatorSitesCalled = true;
        return app_.validatorSites();
    }

    ManifestCache&
    validatorManifests() override
    {
        validatorManifestsCalled = true;
        return app_.validatorManifests();
    }

    ManifestCache&
    publisherManifests() override
    {
        publisherManifestsCalled = true;
        return app_.publisherManifests();
    }

    Cluster&
    cluster() override
    {
        clusterCalled = true;
        return app_.cluster();
    }

    PeerReservationTable&
    peerReservations() override
    {
        peerReservationsCalled = true;
        return app_.peerReservations();
    }

    RCLValidations&
    getValidations() override
    {
        getValidationsCalled = true;
        return app_.getValidations();
    }

    NodeStore::Database&
    getNodeStore() override
    {
        getNodeStoreCalled = true;
        return app_.getNodeStore();
    }

    InboundLedgers&
    getInboundLedgers() override
    {
        getInboundLedgersCalled = true;
        return app_.getInboundLedgers();
    }

    InboundTransactions&
    getInboundTransactions() override
    {
        getInboundTransactionsCalled = true;
        return app_.getInboundTransactions();
    }

    TaggedCache<uint256, AcceptedLedger>&
    getAcceptedLedgerCache() override
    {
        getAcceptedLedgerCacheCalled = true;
        return app_.getAcceptedLedgerCache();
    }

    LedgerMaster&
    getLedgerMaster() override
    {
        getLedgerMasterCalled = true;
        return app_.getLedgerMaster();
    }

    LedgerCleaner&
    getLedgerCleaner() override
    {
        getLedgerCleanerCalled = true;
        return app_.getLedgerCleaner();
    }

    LedgerReplayer&
    getLedgerReplayer() override
    {
        getLedgerReplayerCalled = true;
        return app_.getLedgerReplayer();
    }

    NetworkOPs&
    getOPs() override
    {
        getOPsCalled = true;
        return app_.getOPs();
    }

    OrderBookDB&
    getOrderBookDB() override
    {
        getOrderBookDBCalled = true;
        return app_.getOrderBookDB();
    }

    ServerHandler&
    getServerHandler() override
    {
        getServerHandlerCalled = true;
        return app_.getServerHandler();
    }

    TransactionMaster&
    getMasterTransaction() override
    {
        getMasterTransactionCalled = true;
        return app_.getMasterTransaction();
    }

    perf::PerfLog&
    getPerfLog() override
    {
        getPerfLogCalled = true;
        return app_.getPerfLog();
    }

    std::pair<PublicKey, SecretKey> const&
    nodeIdentity() override
    {
        return app_.nodeIdentity();
    }

    std::optional<PublicKey const>
    getValidationPublicKey() const override
    {
        return app_.getValidationPublicKey();
    }

    Resource::Manager&
    getResourceManager() override
    {
        getResourceManagerCalled = true;
        return app_.getResourceManager();
    }

    PathRequests&
    getPathRequests() override
    {
        getPathRequestsCalled = true;
        return app_.getPathRequests();
    }

    SHAMapStore&
    getSHAMapStore() override
    {
        getSHAMapStoreCalled = true;
        return app_.getSHAMapStore();
    }

    PendingSaves&
    pendingSaves() override
    {
        pendingSavesCalled = true;
        return app_.pendingSaves();
    }

    OpenLedger&
    openLedger() override
    {
        openLedgerCalled = true;
        return app_.openLedger();
    }

    OpenLedger const&
    openLedger() const override
    {
        openLedgerConstCalled = true;
        return app_.openLedger();
    }

    RelationalDatabase&
    getRelationalDatabase() override
    {
        getRelationalDatabaseCalled = true;
        return app_.getRelationalDatabase();
    }

    std::chrono::milliseconds
    getIOLatency() override
    {
        return app_.getIOLatency();
    }

    bool
    serverOkay(std::string& reason) override
    {
        return app_.serverOkay(reason);
    }

    beast::Journal
    journal(std::string const& name) override
    {
        return app_.journal(name);
    }

    int
    fdRequired() const override
    {
        return app_.fdRequired();
    }

    DatabaseCon&
    getWalletDB() override
    {
        return app_.getWalletDB();
    }

    LedgerIndex
    getMaxDisallowedLedger() override
    {
        return app_.getMaxDisallowedLedger();
    }

    std::optional<uint256> const&
    trapTxID() const override
    {
        return app_.trapTxID();
    }

    ServiceRegistry&
    getServiceRegistry() override
    {
        return app_.getServiceRegistry();
    }

    void
    onWrite(beast::PropertyStream::Map& stream) override
    {
        app_.onWrite(stream);
    }

private:
    Application& app_;
};

class ServiceRegistry_test : public beast::unit_test::suite
{
    void
    testGetServices()
    {
        testcase("Get Services");

        jtx::Env env{*this};
        MockApplication mockApp{env.app()};
        ServiceRegistryImpl registry(mockApp);

        // Test core infrastructure services
        registry.getCollectorManager();
        BEAST_EXPECT(mockApp.getCollectorManagerCalled);

        registry.getNodeFamily();
        BEAST_EXPECT(mockApp.getNodeFamilyCalled);

        registry.timeKeeper();
        BEAST_EXPECT(mockApp.timeKeeperCalled);

        registry.getJobQueue();
        BEAST_EXPECT(mockApp.getJobQueueCalled);

        registry.getTempNodeCache();
        BEAST_EXPECT(mockApp.getTempNodeCacheCalled);

        registry.cachedSLEs();
        BEAST_EXPECT(mockApp.cachedSLEsCalled);

        // Test protocol and validation services
        registry.getAmendmentTable();
        BEAST_EXPECT(mockApp.getAmendmentTableCalled);

        registry.getHashRouter();
        BEAST_EXPECT(mockApp.getHashRouterCalled);

        registry.getFeeTrack();
        BEAST_EXPECT(mockApp.getFeeTrackCalled);

        registry.getLoadManager();
        BEAST_EXPECT(mockApp.getLoadManagerCalled);

        registry.getValidations();
        BEAST_EXPECT(mockApp.getValidationsCalled);

        registry.validators();
        BEAST_EXPECT(mockApp.validatorsCalled);

        registry.validatorSites();
        BEAST_EXPECT(mockApp.validatorSitesCalled);

        registry.validatorManifests();
        BEAST_EXPECT(mockApp.validatorManifestsCalled);

        registry.publisherManifests();
        BEAST_EXPECT(mockApp.publisherManifestsCalled);

        // Test network services
        registry.overlay();
        BEAST_EXPECT(mockApp.overlayCalled);

        registry.cluster();
        BEAST_EXPECT(mockApp.clusterCalled);

        registry.peerReservations();
        BEAST_EXPECT(mockApp.peerReservationsCalled);

        registry.getResourceManager();
        BEAST_EXPECT(mockApp.getResourceManagerCalled);

        // Test storage services
        registry.getNodeStore();
        BEAST_EXPECT(mockApp.getNodeStoreCalled);

        registry.getSHAMapStore();
        BEAST_EXPECT(mockApp.getSHAMapStoreCalled);

        registry.getRelationalDatabase();
        BEAST_EXPECT(mockApp.getRelationalDatabaseCalled);

        // Test ledger services
        registry.getInboundLedgers();
        BEAST_EXPECT(mockApp.getInboundLedgersCalled);

        registry.getInboundTransactions();
        BEAST_EXPECT(mockApp.getInboundTransactionsCalled);

        registry.getAcceptedLedgerCache();
        BEAST_EXPECT(mockApp.getAcceptedLedgerCacheCalled);

        registry.getLedgerMaster();
        BEAST_EXPECT(mockApp.getLedgerMasterCalled);

        registry.getLedgerCleaner();
        BEAST_EXPECT(mockApp.getLedgerCleanerCalled);

        registry.getLedgerReplayer();
        BEAST_EXPECT(mockApp.getLedgerReplayerCalled);

        registry.pendingSaves();
        BEAST_EXPECT(mockApp.pendingSavesCalled);

        registry.openLedger();
        BEAST_EXPECT(mockApp.openLedgerCalled);

        // Test const version of openLedger
        ServiceRegistryImpl const& constRegistry = registry;
        constRegistry.openLedger();
        BEAST_EXPECT(mockApp.openLedgerConstCalled);

        // Test transaction and operation services
        registry.getOPs();
        BEAST_EXPECT(mockApp.getOPsCalled);

        registry.getOrderBookDB();
        BEAST_EXPECT(mockApp.getOrderBookDBCalled);

        registry.getMasterTransaction();
        BEAST_EXPECT(mockApp.getMasterTransactionCalled);

        registry.getTxQ();
        BEAST_EXPECT(mockApp.getTxQCalled);

        registry.getPathRequests();
        BEAST_EXPECT(mockApp.getPathRequestsCalled);

        // Test server services
        registry.getServerHandler();
        BEAST_EXPECT(mockApp.getServerHandlerCalled);

        registry.getPerfLog();
        BEAST_EXPECT(mockApp.getPerfLogCalled);
    }

public:
    void
    run() override
    {
        testGetServices();
    }
};

BEAST_DEFINE_TESTSUITE(ServiceRegistry, core, xrpl);

}  // namespace test
}  // namespace xrpl
