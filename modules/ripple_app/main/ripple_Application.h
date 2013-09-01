//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_IAPPLICATION_H
#define RIPPLE_IAPPLICATION_H

// VFALCO TODO Fix forward declares required for header dependency loops
class IFeatures;
class IFeeVote;
class IHashRouter;
class ILoadFeeTrack;
class Peers;
class UniqueNodeList;
class IValidations;
class Validators;

class NodeStore;
class JobQueue;
class InboundLedgers;
class LedgerMaster;
class LoadManager;
class NetworkOPs;
class OrderBookDB;
class ProofOfWorkFactory;
class SerializedLedgerEntry;
class TransactionMaster;
class TXQueue;
class LocalCredentials;

class DatabaseCon;

typedef TaggedCacheType <uint256, Blob , UptimeTimerAdapter> NodeCache;
typedef TaggedCacheType <uint256, SerializedLedgerEntry, UptimeTimerAdapter> SLECache;

class Application
{
public:
    /* VFALCO NOTE

        The master lock protects:

        - The open ledger
        - Server global state
            * What the last closed ledger is
            * State of the consensus engine

        other things
    */
    typedef RippleRecursiveMutex LockType;
    typedef LockType::ScopedLockType ScopedLockType;

    virtual LockType& getMasterLock () = 0;

public:
    struct State
    {
        // Stuff in here is accessed concurrently and requires a WriteAccess
    };
    
    typedef SharedData <State> SharedState;

    SharedState& getSharedState () noexcept { return m_sharedState; }

    SharedState const& getSharedState () const noexcept { return m_sharedState; }

private:
    SharedState m_sharedState;

public:
    virtual ~Application () { }

    virtual boost::asio::io_service& getIOService () = 0;

    virtual NodeCache&              getTempNodeCache () = 0;
    virtual SLECache&               getSLECache () = 0;

    virtual Validators&             getValidators () = 0;
    virtual IFeatures&              getFeatureTable () = 0;
    virtual IFeeVote&               getFeeVote () = 0;
    virtual IHashRouter&            getHashRouter () = 0;
    virtual ILoadFeeTrack&          getFeeTrack () = 0;
    virtual LoadManager&            getLoadManager () = 0;
    virtual Peers&                  getPeers () = 0;
    virtual ProofOfWorkFactory&     getProofOfWorkFactory () = 0;
    virtual UniqueNodeList&         getUNL () = 0;
    virtual IValidations&           getValidations () = 0;

    virtual NodeStore&              getNodeStore () = 0;
    virtual JobQueue&               getJobQueue () = 0;
    virtual InboundLedgers&         getInboundLedgers () = 0;
    virtual LedgerMaster&           getLedgerMaster () = 0;
    virtual NetworkOPs&             getOPs () = 0;
    virtual OrderBookDB&            getOrderBookDB () = 0;
    virtual TransactionMaster&      getMasterTransaction () = 0;
    virtual TXQueue&                getTxnQueue () = 0;
    virtual LocalCredentials&       getLocalCredentials () = 0;

    virtual DatabaseCon* getRpcDB () = 0;
    virtual DatabaseCon* getTxnDB () = 0;
    virtual DatabaseCon* getLedgerDB () = 0;

    /** Retrieve the "wallet database"

        It looks like this is used to store the unique node list.
    */
    // VFALCO TODO Rename, document this
    //        NOTE This will be replaced by class Validators
    //
    virtual DatabaseCon* getWalletDB () = 0;

    virtual bool getSystemTimeOffset (int& offset) = 0;
    virtual bool isShutdown () = 0;
    virtual bool running () = 0;
    virtual void setup () = 0;
    virtual void run () = 0;
    virtual void stop () = 0;
    virtual void sweep () = 0;
};

extern Application& getApp ();

#endif
