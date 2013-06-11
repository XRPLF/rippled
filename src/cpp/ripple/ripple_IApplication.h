#ifndef RIPPLE_IAPPLICATION_H
#define RIPPLE_IAPPLICATION_H

// VFALCO TODO Replace these with beast "unsigned long long" generators
// VFALCO NOTE Apparently these are used elsewhere. Make them constants in the config
//             or in the IApplication
//
#define SYSTEM_CURRENCY_GIFT		1000ull
#define SYSTEM_CURRENCY_USERS		100000000ull
#define SYSTEM_CURRENCY_PARTS		1000000ull		// 10^SYSTEM_CURRENCY_PRECISION
#define SYSTEM_CURRENCY_START		(SYSTEM_CURRENCY_GIFT*SYSTEM_CURRENCY_USERS*SYSTEM_CURRENCY_PARTS)

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
class LedgerAcquireMaster;
class LedgerMaster;
class LoadManager;
class NetworkOPs;
class OrderBookDB;
class PeerDoor;
class SerializedLedgerEntry;
class TransactionMaster;
class TXQueue;
class Wallet;

class DatabaseCon;

typedef TaggedCache <uint256, Blob , UptimeTimerAdapter> NodeCache;
typedef TaggedCache <uint256, SerializedLedgerEntry, UptimeTimerAdapter> SLECache;

class IApplication
{
public:
    static IApplication* New ();

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
	virtual IPeers&                 getPeers () = 0;
	virtual IProofOfWorkFactory&    getProofOfWorkFactory () = 0;
	virtual IUniqueNodeList&        getUNL () = 0;
	virtual IValidations&           getValidations () = 0;

	virtual HashedObjectStore&      getHashedObjectStore () = 0;
	virtual JobQueue&               getJobQueue () = 0;
	virtual LedgerAcquireMaster&    getMasterLedgerAcquire () = 0;
	virtual LedgerMaster&           getLedgerMaster () = 0;
	virtual LoadManager&            getLoadManager () = 0;
	virtual NetworkOPs&             getOPs () = 0;
	virtual OrderBookDB&            getOrderBookDB () = 0;
	virtual PeerDoor&               getPeerDoor () = 0;
	virtual TransactionMaster&      getMasterTransaction () = 0;
	virtual TXQueue&                getTxnQueue () = 0;
    virtual Wallet&                 getWallet () = 0;

	virtual DatabaseCon* getRpcDB () = 0;
	virtual DatabaseCon* getTxnDB () = 0;
	virtual DatabaseCon* getLedgerDB () = 0;
	virtual DatabaseCon* getWalletDB () = 0;
	virtual DatabaseCon* getNetNodeDB () = 0;
	virtual DatabaseCon* getPathFindDB () = 0;
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

extern IApplication* theApp;

#endif
// vim:ts=4
