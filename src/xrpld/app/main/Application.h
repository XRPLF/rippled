//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef RIPPLE_APP_MAIN_APPLICATION_H_INCLUDED
#define RIPPLE_APP_MAIN_APPLICATION_H_INCLUDED

#include <xrpld/core/Config.h>
#include <xrpld/overlay/PeerReservationTable.h>
#include <xrpld/shamap/TreeNodeCache.h>

#include <xrpl/basics/TaggedCache.h>
#include <xrpl/beast/utility/PropertyStream.h>
#include <xrpl/protocol/Protocol.h>

#include <boost/asio.hpp>
#include <boost/program_options.hpp>

#include <mutex>

namespace ripple {

namespace unl {
class Manager;
}
namespace Resource {
class Manager;
}
namespace NodeStore {
class Database;
}  // namespace NodeStore
namespace perf {
class PerfLog;
}

// VFALCO TODO Fix forward declares required for header dependency loops
class AmendmentTable;

template <
    class Key,
    class T,
    bool IsKeyCache,
    class SharedWeakUnionPointer,
    class SharedPointerType,
    class Hash,
    class KeyEqual,
    class Mutex>
class TaggedCache;
class STLedgerEntry;
using SLE = STLedgerEntry;
using CachedSLEs = TaggedCache<uint256, SLE const>;

class CollectorManager;
class Family;
class HashRouter;
class Logs;
class LoadFeeTrack;
class JobQueue;
class InboundLedgers;
class InboundTransactions;
class AcceptedLedger;
class Ledger;
class LedgerMaster;
class LedgerCleaner;
class LedgerReplayer;
class LoadManager;
class ManifestCache;
class ValidatorKeys;
class NetworkOPs;
class OpenLedger;
class OrderBookDB;
class Overlay;
class PathRequests;
class PendingSaves;
class PublicKey;
class ServerHandler;
class SecretKey;
class STLedgerEntry;
class TimeKeeper;
class TransactionMaster;
class TxQ;

class ValidatorList;
class ValidatorSite;
class Cluster;

class RelationalDatabase;
class DatabaseCon;
class SHAMapStore;

using NodeCache = TaggedCache<SHAMapHash, Blob>;

template <class Adaptor>
class Validations;
class RCLValidationsAdaptor;
using RCLValidations = Validations<RCLValidationsAdaptor>;

class Application : public beast::PropertyStream::Source
{
public:
    /* VFALCO NOTE

        The master mutex protects:

        - The open ledger
        - Server global state
            * What the last closed ledger is
            * State of the consensus engine

        other things
    */
    using MutexType = std::recursive_mutex;
    virtual MutexType&
    getMasterMutex() = 0;

public:
    Application();

    virtual ~Application() = default;

    virtual bool
    setup(boost::program_options::variables_map const& options) = 0;

    virtual void
    start(bool withTimers) = 0;
    virtual void
    run() = 0;
    virtual void
    signalStop(std::string msg) = 0;
    virtual bool
    checkSigs() const = 0;
    virtual void
    checkSigs(bool) = 0;
    virtual bool
    isStopping() const = 0;

    //
    // ---
    //

    /** Returns a 64-bit instance identifier, generated at startup */
    virtual std::uint64_t
    instanceID() const = 0;

    virtual Logs&
    logs() = 0;
    virtual Config&
    config() = 0;

    virtual boost::asio::io_service&
    getIOService() = 0;

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
    virtual AmendmentTable&
    getAmendmentTable() = 0;
    virtual HashRouter&
    getHashRouter() = 0;
    virtual LoadFeeTrack&
    getFeeTrack() = 0;
    virtual LoadManager&
    getLoadManager() = 0;
    virtual Overlay&
    overlay() = 0;
    virtual TxQ&
    getTxQ() = 0;
    virtual ValidatorList&
    validators() = 0;
    virtual ValidatorSite&
    validatorSites() = 0;
    virtual ManifestCache&
    validatorManifests() = 0;
    virtual ManifestCache&
    publisherManifests() = 0;
    virtual Cluster&
    cluster() = 0;
    virtual PeerReservationTable&
    peerReservations() = 0;
    virtual RCLValidations&
    getValidations() = 0;
    virtual NodeStore::Database&
    getNodeStore() = 0;
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
    virtual NetworkOPs&
    getOPs() = 0;
    virtual OrderBookDB&
    getOrderBookDB() = 0;
    virtual ServerHandler&
    getServerHandler() = 0;
    virtual TransactionMaster&
    getMasterTransaction() = 0;
    virtual perf::PerfLog&
    getPerfLog() = 0;

    virtual std::pair<PublicKey, SecretKey> const&
    nodeIdentity() = 0;

    virtual std::optional<PublicKey const>
    getValidationPublicKey() const = 0;

    virtual Resource::Manager&
    getResourceManager() = 0;
    virtual PathRequests&
    getPathRequests() = 0;
    virtual SHAMapStore&
    getSHAMapStore() = 0;
    virtual PendingSaves&
    pendingSaves() = 0;
    virtual OpenLedger&
    openLedger() = 0;
    virtual OpenLedger const&
    openLedger() const = 0;
    virtual RelationalDatabase&
    getRelationalDatabase() = 0;

    virtual std::chrono::milliseconds
    getIOLatency() = 0;

    virtual bool
    serverOkay(std::string& reason) = 0;

    virtual beast::Journal
    journal(std::string const& name) = 0;

    /* Returns the number of file descriptors the application needs */
    virtual int
    fdRequired() const = 0;

    /** Retrieve the "wallet database" */
    virtual DatabaseCon&
    getWalletDB() = 0;

    /** Ensure that a newly-started validator does not sign proposals older
     * than the last ledger it persisted. */
    virtual LedgerIndex
    getMaxDisallowedLedger() = 0;

    virtual std::optional<uint256> const&
    trapTxID() const = 0;
};

std::unique_ptr<Application>
make_Application(
    std::unique_ptr<Config> config,
    std::unique_ptr<Logs> logs,
    std::unique_ptr<TimeKeeper> timeKeeper);

}  // namespace ripple

#endif
