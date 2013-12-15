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

#ifndef RIPPLE_APP_APPLICATION_H_INCLUDED
#define RIPPLE_APP_APPLICATION_H_INCLUDED

namespace SiteFiles { class Manager; }
namespace Validators { class Manager; }
namespace Resource { class Manager; }
namespace NodeStore { class Database; }
namespace RPC { class Manager; }

// VFALCO TODO Fix forward declares required for header dependency loops
class CollectorManager;
class IFeatures;
class IFeeVote;
class IHashRouter;
class LoadFeeTrack;
class Peers;
class UniqueNodeList;
class JobQueue;
class InboundLedgers;
class LedgerMaster;
class LoadManager;
class NetworkOPs;
class OrderBookDB;
class ProofOfWorkFactory;
class SerializedLedgerEntry;
class TransactionMaster;
class TxQueue;
class LocalCredentials;

class DatabaseCon;

typedef TaggedCacheType <uint256, Blob , UptimeTimerAdapter> NodeCache;
typedef TaggedCacheType <uint256, SerializedLedgerEntry, UptimeTimerAdapter> SLECache;

class Application : public PropertyStream::Source
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
    static Application* New ();

    Application ();

    virtual ~Application () { }

    virtual boost::asio::io_service& getIOService () = 0;
    virtual CollectorManager&       getCollectorManager () = 0;
    virtual RPC::Manager&           getRPCServiceManager() = 0;
    virtual JobQueue&               getJobQueue () = 0;
    virtual SiteFiles::Manager&     getSiteFiles () = 0;
    virtual NodeCache&              getTempNodeCache () = 0;
    virtual SLECache&               getSLECache () = 0;
    virtual Validators::Manager&    getValidators () = 0;
    virtual IFeatures&              getFeatureTable () = 0;
    virtual IFeeVote&               getFeeVote () = 0;
    virtual IHashRouter&            getHashRouter () = 0;
    virtual LoadFeeTrack&           getFeeTrack () = 0;
    virtual LoadManager&            getLoadManager () = 0;
    virtual Peers&                  getPeers () = 0;
    virtual ProofOfWorkFactory&     getProofOfWorkFactory () = 0;
    virtual UniqueNodeList&         getUNL () = 0;
    virtual Validations&            getValidations () = 0;
    virtual NodeStore::Database&    getNodeStore () = 0;
    virtual InboundLedgers&         getInboundLedgers () = 0;
    virtual LedgerMaster&           getLedgerMaster () = 0;
    virtual NetworkOPs&             getOPs () = 0;
    virtual OrderBookDB&            getOrderBookDB () = 0;
    virtual TransactionMaster&      getMasterTransaction () = 0;
    virtual TxQueue&                getTxQueue () = 0;
    virtual LocalCredentials&       getLocalCredentials () = 0;
    virtual Resource::Manager&      getResourceManager () = 0;

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
    virtual void signalStop () = 0;
};

extern Application& getApp ();

#endif
