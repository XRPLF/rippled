#include <xrpld/app/main/Application.h>
#include <xrpld/core/ServiceRegistryImpl.h>

namespace xrpl {

ServiceRegistryImpl::ServiceRegistryImpl(Application& app) : app_(app)
{
}

// Core infrastructure services
CollectorManager&
ServiceRegistryImpl::getCollectorManager()
{
    return app_.getCollectorManager();
}

Family&
ServiceRegistryImpl::getNodeFamily()
{
    return app_.getNodeFamily();
}

TimeKeeper&
ServiceRegistryImpl::timeKeeper()
{
    return app_.timeKeeper();
}

JobQueue&
ServiceRegistryImpl::getJobQueue()
{
    return app_.getJobQueue();
}

NodeCache&
ServiceRegistryImpl::getTempNodeCache()
{
    return app_.getTempNodeCache();
}

CachedSLEs&
ServiceRegistryImpl::cachedSLEs()
{
    return app_.cachedSLEs();
}

// Protocol and validation services
AmendmentTable&
ServiceRegistryImpl::getAmendmentTable()
{
    return app_.getAmendmentTable();
}

HashRouter&
ServiceRegistryImpl::getHashRouter()
{
    return app_.getHashRouter();
}

LoadFeeTrack&
ServiceRegistryImpl::getFeeTrack()
{
    return app_.getFeeTrack();
}

LoadManager&
ServiceRegistryImpl::getLoadManager()
{
    return app_.getLoadManager();
}

RCLValidations&
ServiceRegistryImpl::getValidations()
{
    return app_.getValidations();
}

ValidatorList&
ServiceRegistryImpl::validators()
{
    return app_.validators();
}

ValidatorSite&
ServiceRegistryImpl::validatorSites()
{
    return app_.validatorSites();
}

ManifestCache&
ServiceRegistryImpl::validatorManifests()
{
    return app_.validatorManifests();
}

ManifestCache&
ServiceRegistryImpl::publisherManifests()
{
    return app_.publisherManifests();
}

// Network services
Overlay&
ServiceRegistryImpl::overlay()
{
    return app_.overlay();
}

Cluster&
ServiceRegistryImpl::cluster()
{
    return app_.cluster();
}

PeerReservationTable&
ServiceRegistryImpl::peerReservations()
{
    return app_.peerReservations();
}

Resource::Manager&
ServiceRegistryImpl::getResourceManager()
{
    return app_.getResourceManager();
}

// Storage services
NodeStore::Database&
ServiceRegistryImpl::getNodeStore()
{
    return app_.getNodeStore();
}

SHAMapStore&
ServiceRegistryImpl::getSHAMapStore()
{
    return app_.getSHAMapStore();
}

RelationalDatabase&
ServiceRegistryImpl::getRelationalDatabase()
{
    return app_.getRelationalDatabase();
}

// Ledger services
InboundLedgers&
ServiceRegistryImpl::getInboundLedgers()
{
    return app_.getInboundLedgers();
}

InboundTransactions&
ServiceRegistryImpl::getInboundTransactions()
{
    return app_.getInboundTransactions();
}

TaggedCache<uint256, AcceptedLedger>&
ServiceRegistryImpl::getAcceptedLedgerCache()
{
    return app_.getAcceptedLedgerCache();
}

LedgerMaster&
ServiceRegistryImpl::getLedgerMaster()
{
    return app_.getLedgerMaster();
}

LedgerCleaner&
ServiceRegistryImpl::getLedgerCleaner()
{
    return app_.getLedgerCleaner();
}

LedgerReplayer&
ServiceRegistryImpl::getLedgerReplayer()
{
    return app_.getLedgerReplayer();
}

PendingSaves&
ServiceRegistryImpl::pendingSaves()
{
    return app_.pendingSaves();
}

OpenLedger&
ServiceRegistryImpl::openLedger()
{
    return app_.openLedger();
}

OpenLedger const&
ServiceRegistryImpl::openLedger() const
{
    return app_.openLedger();
}

// Transaction and operation services
NetworkOPs&
ServiceRegistryImpl::getOPs()
{
    return app_.getOPs();
}

OrderBookDB&
ServiceRegistryImpl::getOrderBookDB()
{
    return app_.getOrderBookDB();
}

TransactionMaster&
ServiceRegistryImpl::getMasterTransaction()
{
    return app_.getMasterTransaction();
}

TxQ&
ServiceRegistryImpl::getTxQ()
{
    return app_.getTxQ();
}

PathRequests&
ServiceRegistryImpl::getPathRequests()
{
    return app_.getPathRequests();
}

// Server services
ServerHandler&
ServiceRegistryImpl::getServerHandler()
{
    return app_.getServerHandler();
}

perf::PerfLog&
ServiceRegistryImpl::getPerfLog()
{
    return app_.getPerfLog();
}

// Configuration and state
bool
ServiceRegistryImpl::isStopping() const
{
    return app_.isStopping();
}

beast::Journal
ServiceRegistryImpl::journal(std::string const& name)
{
    return app_.journal(name);
}

boost::asio::io_context&
ServiceRegistryImpl::getIOContext()
{
    return app_.getIOContext();
}

Logs&
ServiceRegistryImpl::logs()
{
    return app_.logs();
}

Application&
ServiceRegistryImpl::app()
{
    return app_;
}

}  // namespace xrpl
