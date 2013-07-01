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
class IPeers;
class IProofOfWorkFactory;
class IUniqueNodeList;
class IValidations;

class HashedObjectStore;
class JobQueue;
class InboundLedgers;
class LedgerMaster;
class LoadManager;
class NetworkOPs;
class OrderBookDB;
class PeerDoor;
class SerializedLedgerEntry;
class TransactionMaster;
class TXQueue;
class LocalCredentials;

class DatabaseCon;

typedef TaggedCache <uint256, Blob , UptimeTimerAdapter> NodeCache;
typedef TaggedCache <uint256, SerializedLedgerEntry, UptimeTimerAdapter> SLECache;

class IApplication
{
public:
    virtual ~IApplication () { }

    /* VFALCO NOTE

        The master lock protects:

        - The open ledger
        - Server global state
            * What the last closed ledger is
            * State of the consensus engine

        other things
    */
    virtual boost::recursive_mutex&  getMasterLock () = 0;

    virtual boost::asio::io_service& getIOService () = 0;
    virtual boost::asio::io_service& getAuxService () = 0;

    virtual NodeCache&              getTempNodeCache () = 0;
    virtual SLECache&               getSLECache () = 0;

    virtual IFeatures&              getFeatureTable () = 0;
    virtual IFeeVote&               getFeeVote () = 0;
    virtual IHashRouter&            getHashRouter () = 0;
    virtual ILoadFeeTrack&          getFeeTrack () = 0;
    virtual ILoadManager&           getLoadManager () = 0;
    virtual IPeers&                 getPeers () = 0;
    virtual IProofOfWorkFactory&    getProofOfWorkFactory () = 0;
    virtual IUniqueNodeList&        getUNL () = 0;
    virtual IValidations&           getValidations () = 0;

    virtual HashedObjectStore&      getHashedObjectStore () = 0;
    virtual JobQueue&               getJobQueue () = 0;
    virtual InboundLedgers&         getInboundLedgers () = 0;
    virtual LedgerMaster&           getLedgerMaster () = 0;
    virtual NetworkOPs&             getOPs () = 0;
    virtual OrderBookDB&            getOrderBookDB () = 0;
    virtual PeerDoor&               getPeerDoor () = 0;
    virtual TransactionMaster&      getMasterTransaction () = 0;
    virtual TXQueue&                getTxnQueue () = 0;
    virtual LocalCredentials&       getLocalCredentials () = 0;

    virtual DatabaseCon* getRpcDB () = 0;
    virtual DatabaseCon* getTxnDB () = 0;
    virtual DatabaseCon* getLedgerDB () = 0;
    virtual DatabaseCon* getWalletDB () = 0;
    // VFALCO NOTE It looks like this isn't used...
    //virtual DatabaseCon* getNetNodeDB () = 0;
    // VFALCO NOTE It looks like this isn't used...
    //virtual DatabaseCon* getPathFindDB () = 0;
    virtual DatabaseCon* getHashNodeDB () = 0;

    virtual leveldb::DB* getHashNodeLDB () = 0;
    virtual leveldb::DB* getEphemeralLDB () = 0;

    virtual bool getSystemTimeOffset (int& offset) = 0;
    virtual bool isShutdown () = 0;
    virtual bool running () = 0;
    virtual void setup () = 0;
    virtual void run () = 0;
    virtual void stop () = 0;
    virtual void sweep () = 0;
};

extern IApplication& getApp ();

#endif
