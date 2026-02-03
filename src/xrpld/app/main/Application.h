#pragma once

#include <xrpld/core/Config.h>
#include <xrpld/overlay/PeerReservationTable.h>

#include <xrpl/basics/TaggedCache.h>
#include <xrpl/beast/utility/PropertyStream.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/shamap/TreeNodeCache.h>

#include <boost/asio.hpp>
#include <boost/program_options.hpp>

#include <mutex>

namespace xrpl {

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

class Application : public ServiceRegistry, public beast::PropertyStream::Source
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

    virtual boost::asio::io_context&
    getIOContext() = 0;

    virtual std::pair<PublicKey, SecretKey> const&
    nodeIdentity() = 0;

    virtual std::optional<PublicKey const>
    getValidationPublicKey() const = 0;

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
make_Application(std::unique_ptr<Config> config, std::unique_ptr<Logs> logs, std::unique_ptr<TimeKeeper> timeKeeper);

}  // namespace xrpl
