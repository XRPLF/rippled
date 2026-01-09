#ifndef XRPLD_CORE_SERVICEREGISTRYIMPL_H_INCLUDED
#define XRPLD_CORE_SERVICEREGISTRYIMPL_H_INCLUDED

#include <xrpl/core/ServiceRegistry.h>

namespace xrpl {

// Forward declaration
class Application;

/** Implementation of ServiceRegistry that delegates to Application.

    This class provides a ServiceRegistry interface that wraps an Application
    reference. It allows components to depend on ServiceRegistry instead of
    Application, enabling gradual migration and better separation of concerns.

    Usage:
        Application& app = ...;
        ServiceRegistry& registry = app.getServiceRegistry();
        // or
        ServiceRegistryImpl registry(app);
*/
class ServiceRegistryImpl : public ServiceRegistry
{
public:
    explicit ServiceRegistryImpl(Application& app);

    ~ServiceRegistryImpl() override = default;

    // Core infrastructure services
    CollectorManager&
    getCollectorManager() override;

    Family&
    getNodeFamily() override;

    TimeKeeper&
    timeKeeper() override;

    JobQueue&
    getJobQueue() override;

    NodeCache&
    getTempNodeCache() override;

    CachedSLEs&
    cachedSLEs() override;

    NetworkIDService&
    getNetworkIDService() override;

    // Protocol and validation services
    AmendmentTable&
    getAmendmentTable() override;

    HashRouter&
    getHashRouter() override;

    LoadFeeTrack&
    getFeeTrack() override;

    LoadManager&
    getLoadManager() override;

    RCLValidations&
    getValidations() override;

    ValidatorList&
    validators() override;

    ValidatorSite&
    validatorSites() override;

    ManifestCache&
    validatorManifests() override;

    ManifestCache&
    publisherManifests() override;

    // Network services
    Overlay&
    overlay() override;

    Cluster&
    cluster() override;

    PeerReservationTable&
    peerReservations() override;

    Resource::Manager&
    getResourceManager() override;

    // Storage services
    NodeStore::Database&
    getNodeStore() override;

    SHAMapStore&
    getSHAMapStore() override;

    RelationalDatabase&
    getRelationalDatabase() override;

    // Ledger services
    InboundLedgers&
    getInboundLedgers() override;

    InboundTransactions&
    getInboundTransactions() override;

    TaggedCache<uint256, AcceptedLedger>&
    getAcceptedLedgerCache() override;

    LedgerMaster&
    getLedgerMaster() override;

    LedgerCleaner&
    getLedgerCleaner() override;

    LedgerReplayer&
    getLedgerReplayer() override;

    PendingSaves&
    pendingSaves() override;

    OpenLedger&
    openLedger() override;

    OpenLedger const&
    openLedger() const override;

    // Transaction and operation services
    NetworkOPs&
    getOPs() override;

    OrderBookDB&
    getOrderBookDB() override;

    TransactionMaster&
    getMasterTransaction() override;

    TxQ&
    getTxQ() override;

    PathRequests&
    getPathRequests() override;

    // Server services
    ServerHandler&
    getServerHandler() override;

    perf::PerfLog&
    getPerfLog() override;

    // Configuration and state
    bool
    isStopping() const override;

    beast::Journal
    journal(std::string const& name) override;

    boost::asio::io_context&
    getIOContext() override;

    Logs&
    logs() override;

    std::optional<uint256> const&
    trapTxID() const override;

    DatabaseCon&
    getWalletDB() override;

    // Temporary: Get the underlying Application
    Application&
    app() override;

private:
    Application& app_;
};

}  // namespace xrpl

#endif
