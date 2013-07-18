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
class UniqueNodeList;
class IValidations;

class NodeStore;
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
#if 1
    class ScopedLockType;

    class MasterLockType
    {
    public:
        MasterLockType ()
            : m_fileName ("")
            , m_lineNumber (0)
        {
        }

        // Note that these are not exactly thread safe.

        char const* getFileName () const noexcept
        {
            return m_fileName;
        }

        int getLineNumber () const noexcept
        {
            return m_lineNumber;
        }

    private:
        friend class ScopedLockType;

        void setOwner (char const* fileName, int lineNumber)
        {
            m_fileName = fileName;
            m_lineNumber = lineNumber;
        }

        void resetOwner ()
        {
            m_fileName = "";
            m_lineNumber = 0;
        }

        boost::recursive_mutex m_mutex;
        char const* m_fileName;
        int m_lineNumber;
    };

    class ScopedLockType
    {
    public:
        explicit ScopedLockType (MasterLockType& mutex,
                                 char const* fileName,
                                 int lineNumber)
            : m_mutex (mutex)
            , m_lock (mutex.m_mutex)
        {
            mutex.setOwner (fileName, lineNumber);
        }

        ~ScopedLockType ()
        {
            if (m_lock.owns_lock ())
                m_mutex.resetOwner ();
        }

        void unlock ()
        {
            if (m_lock.owns_lock ())
                m_mutex.resetOwner ();

            m_lock.unlock ();
        }

    private:
        MasterLockType& m_mutex;
        boost::recursive_mutex::scoped_lock m_lock;
    };

#else
    typedef boost::recursive_mutex MasterLockType;

    typedef boost::recursive_mutex::scoped_lock ScopedLockType;

#endif

    virtual MasterLockType& getMasterLock () = 0;




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
    virtual ILoadManager&           getLoadManager () = 0;
    virtual IPeers&                 getPeers () = 0;
    virtual IProofOfWorkFactory&    getProofOfWorkFactory () = 0;
    virtual UniqueNodeList&        getUNL () = 0;
    virtual IValidations&           getValidations () = 0;

    virtual NodeStore&      getNodeStore () = 0;
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
