#pragma once

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/SHAMapHash.h>
#include <xrpl/basics/TaggedCache.h>
#include <xrpl/ledger/CachedSLEs.h>

#include <boost/asio.hpp>

namespace xrpl {

// Forward declarations
namespace NodeStore {
class Database;
}
namespace Resource {
class Manager;
}
namespace perf {
class PerfLog;
}

// This is temporary until we migrate all code to use ServiceRegistry.
class Application;

// Forward declarations
class AcceptedLedger;
class AmendmentTable;
class Cluster;
class CollectorManager;
class DatabaseCon;
class Family;
class HashRouter;
class InboundLedgers;
class InboundTransactions;
class JobQueue;
class LedgerCleaner;
class LedgerMaster;
class LedgerReplayer;
class LoadFeeTrack;
class LoadManager;
class ManifestCache;
class NetworkOPs;
class OpenLedger;
class OrderBookDB;
class Overlay;
class PathRequests;
class PeerReservationTable;
class PendingSaves;
class RelationalDatabase;
class ServerHandler;
class SHAMapStore;
class TimeKeeper;
class TransactionMaster;
class TxQ;
class ValidatorList;
class ValidatorSite;

template <class Adaptor>
class Validations;
class RCLValidationsAdaptor;
using RCLValidations = Validations<RCLValidationsAdaptor>;

using NodeCache = TaggedCache<SHAMapHash, Blob>;

/** Service registry for dependency injection.

    This abstract interface provides access to various services and components
    used throughout the application. It separates the service locator pattern
    from the Application lifecycle management.

    Components that need access to services can hold a reference to
    ServiceRegistry rather than Application when they only need service
    access and not lifecycle management.

*/
class ServiceRegistry
{
public:
    ServiceRegistry() = default;
    virtual ~ServiceRegistry() = default;

    // Core infrastructure services
    virtual CollectorManager&
    getCollectorManager() = 0;

    virtual Family&
    getNodeFamily() = 0;

    virtual TimeKeeper&
    timeKeeper() = 0;

    virtual JobQueue&
    getJobQueue() = 0;

    virtual NodeCache&
    getTempNodeCache() = 0;

    virtual CachedSLEs&
    cachedSLEs() = 0;

    // Protocol and validation services
    virtual AmendmentTable&
    getAmendmentTable() = 0;

    virtual HashRouter&
    getHashRouter() = 0;

    virtual LoadFeeTrack&
    getFeeTrack() = 0;

    virtual LoadManager&
    getLoadManager() = 0;

    virtual RCLValidations&
    getValidations() = 0;

    virtual ValidatorList&
    validators() = 0;

    virtual ValidatorSite&
    validatorSites() = 0;

    virtual ManifestCache&
    validatorManifests() = 0;

    virtual ManifestCache&
    publisherManifests() = 0;

    // Network services
    virtual Overlay&
    overlay() = 0;

    virtual Cluster&
    cluster() = 0;

    virtual PeerReservationTable&
    peerReservations() = 0;

    virtual Resource::Manager&
    getResourceManager() = 0;

    // Storage services
    virtual NodeStore::Database&
    getNodeStore() = 0;

    virtual SHAMapStore&
    getSHAMapStore() = 0;

    virtual RelationalDatabase&
    getRelationalDatabase() = 0;

    // Ledger services
    virtual InboundLedgers&
    getInboundLedgers() = 0;

    virtual InboundTransactions&
    getInboundTransactions() = 0;

    virtual TaggedCache<uint256, AcceptedLedger>&
    getAcceptedLedgerCache() = 0;

    virtual LedgerMaster&
    getLedgerMaster() = 0;

    virtual LedgerCleaner&
    getLedgerCleaner() = 0;

    virtual LedgerReplayer&
    getLedgerReplayer() = 0;

    virtual PendingSaves&
    pendingSaves() = 0;

    virtual OpenLedger&
    openLedger() = 0;

    virtual OpenLedger const&
    openLedger() const = 0;

    // Transaction and operation services
    virtual NetworkOPs&
    getOPs() = 0;

    virtual OrderBookDB&
    getOrderBookDB() = 0;

    virtual TransactionMaster&
    getMasterTransaction() = 0;

    virtual TxQ&
    getTxQ() = 0;

    virtual PathRequests&
    getPathRequests() = 0;

    // Server services
    virtual ServerHandler&
    getServerHandler() = 0;

    virtual perf::PerfLog&
    getPerfLog() = 0;

    // Configuration and state
    virtual bool
    isStopping() const = 0;

    virtual beast::Journal
    journal(std::string const& name) = 0;

    virtual boost::asio::io_context&
    getIOContext() = 0;

    virtual Logs&
    logs() = 0;

    virtual std::optional<uint256> const&
    trapTxID() const = 0;

    /** Retrieve the "wallet database" */
    virtual DatabaseCon&
    getWalletDB() = 0;

    // Temporary: Get the underlying Application for functions that haven't
    // been migrated yet. This should be removed once all code is migrated.
    virtual Application&
    app() = 0;
};

}  // namespace xrpl
