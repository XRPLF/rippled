//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_NETWORKOPS_H
#define RIPPLE_NETWORKOPS_H

// Operations that clients may wish to perform against the network
// Master operational handler, server sequencer, network tracker

class Peer;
class LedgerConsensus;

class NetworkOPs
    : public DeadlineTimer::Listener
    , LeakChecked <NetworkOPs>
{
public:
    enum Fault
    {
        // exceptions these functions can throw
        IO_ERROR    = 1,
        NO_NETWORK  = 2,
    };

    enum OperatingMode
    {
        // how we process transactions or account balance requests
        omDISCONNECTED  = 0,    // not ready to process requests
        omCONNECTED     = 1,    // convinced we are talking to the network
        omSYNCING       = 2,    // fallen slightly behind
        omTRACKING      = 3,    // convinced we agree with the network
        omFULL          = 4     // we have the ledger and can even validate
    };

#if 0
    // VFALCO TODO Make this happen
    /** Subscription data interface.
    */
    class Subscriber
    {
    public:
        typedef boost::weak_ptr <Subscriber> WeakPtr;

        /** Called every time new JSON data is available.
        */
        virtual void onSubscriberReceiveJSON (Json::Value const& json) { }
    };
    typedef boost::unordered_map <uint64, Subscriber::WeakPtr> SubMapType;
#endif

    typedef boost::unordered_map <uint64, InfoSub::wptr> SubMapType;

public:
    // VFALCO TODO Make LedgerMaster a SharedObjectPtr or a reference.
    //
    explicit NetworkOPs (LedgerMaster* pLedgerMaster);

    // network information
    uint32 getNetworkTimeNC ();                 // Our best estimate of wall time in seconds from 1/1/2000
    uint32 getCloseTimeNC ();                   // Our best estimate of current ledger close time
    uint32 getValidationTimeNC ();              // Use *only* to timestamp our own validation
    void closeTimeOffset (int);
    boost::posix_time::ptime getNetworkTimePT ();
    uint32 getLedgerID (uint256 const& hash);
    uint32 getCurrentLedgerID ();
    OperatingMode getOperatingMode ()
    {
        return mMode;
    }
    std::string strOperatingMode ();

    Ledger::ref     getClosedLedger ()
    {
        return mLedgerMaster->getClosedLedger ();
    }
    Ledger::ref     getValidatedLedger ()
    {
        return mLedgerMaster->getValidatedLedger ();
    }
    Ledger::ref     getPublishedLedger ()
    {
        return mLedgerMaster->getPublishedLedger ();
    }
    Ledger::ref     getCurrentLedger ()
    {
        return mLedgerMaster->getCurrentLedger ();
    }
    Ledger::ref     getCurrentSnapshot ()
    {
        return mLedgerMaster->getCurrentSnapshot ();
    }
    Ledger::pointer getLedgerByHash (uint256 const& hash)
    {
        return mLedgerMaster->getLedgerByHash (hash);
    }
    Ledger::pointer getLedgerBySeq (const uint32 seq);
    void            missingNodeInLedger (const uint32 seq);

    uint256         getClosedLedgerHash ()
    {
        return mLedgerMaster->getClosedLedger ()->getHash ();
    }

    // Do we have this inclusive range of ledgers in our database
    bool haveLedgerRange (uint32 from, uint32 to);
    bool haveLedger (uint32 seq);
    uint32 getValidatedSeq ();
    bool isValidated (uint32 seq);
    bool isValidated (uint32 seq, uint256 const& hash);
    bool isValidated (Ledger::ref l)
    {
        return isValidated (l->getLedgerSeq (), l->getHash ());
    }
    bool getValidatedRange (uint32& minVal, uint32& maxVal)
    {
        return mLedgerMaster->getValidatedRange (minVal, maxVal);
    }

    SerializedValidation::ref getLastValidation ()
    {
        return mLastValidation;
    }
    void setLastValidation (SerializedValidation::ref v)
    {
        mLastValidation = v;
    }

    SLE::pointer getSLE (Ledger::pointer lpLedger, uint256 const& uHash)
    {
        return lpLedger->getSLE (uHash);
    }
    SLE::pointer getSLEi (Ledger::pointer lpLedger, uint256 const& uHash)
    {
        return lpLedger->getSLEi (uHash);
    }

    //
    // Transaction operations
    //
    typedef FUNCTION_TYPE<void (Transaction::pointer, TER)> stCallback; // must complete immediately
    void submitTransaction (Job&, SerializedTransaction::pointer, stCallback callback = stCallback ());
    Transaction::pointer submitTransactionSync (Transaction::ref tpTrans, bool bAdmin, bool bFailHard, bool bSubmit);

    void runTransactionQueue ();
    Transaction::pointer processTransaction (Transaction::pointer, bool bAdmin, bool bFailHard, stCallback);
    Transaction::pointer processTransaction (Transaction::pointer transaction, bool bAdmin, bool bFailHard)
    {
        return processTransaction (transaction, bAdmin, bFailHard, stCallback ());
    }

    Transaction::pointer findTransactionByID (uint256 const& transactionID);
#if 0
    int findTransactionsBySource (uint256 const& uLedger, std::list<Transaction::pointer>&, const RippleAddress& sourceAccount,
                                  uint32 minSeq, uint32 maxSeq);
#endif
    int findTransactionsByDestination (std::list<Transaction::pointer>&, const RippleAddress& destinationAccount,
                                       uint32 startLedgerSeq, uint32 endLedgerSeq, int maxTransactions);

    //
    // Account functions
    //

    AccountState::pointer   getAccountState (Ledger::ref lrLedger, const RippleAddress& accountID);
    SLE::pointer            getGenerator (Ledger::ref lrLedger, const uint160& uGeneratorID);

    //
    // Directory functions
    //

    STVector256             getDirNodeInfo (Ledger::ref lrLedger, uint256 const& uRootIndex,
                                            uint64& uNodePrevious, uint64& uNodeNext);

#if 0
    //
    // Nickname functions
    //

    NicknameState::pointer  getNicknameState (uint256 const& uLedger, const std::string& strNickname);
#endif

    //
    // Owner functions
    //

    Json::Value getOwnerInfo (Ledger::pointer lpLedger, const RippleAddress& naAccount);

    //
    // Book functions
    //

    void getBookPage (Ledger::pointer lpLedger,
                      const uint160& uTakerPaysCurrencyID,
                      const uint160& uTakerPaysIssuerID,
                      const uint160& uTakerGetsCurrencyID,
                      const uint160& uTakerGetsIssuerID,
                      const uint160& uTakerID,
                      const bool bProof,
                      const unsigned int iLimit,
                      const Json::Value& jvMarker,
                      Json::Value& jvResult);

    // raw object operations
    bool findRawLedger (uint256 const& ledgerHash, Blob& rawLedger);
    bool findRawTransaction (uint256 const& transactionHash, Blob& rawTransaction);
    bool findAccountNode (uint256 const& nodeHash, Blob& rawAccountNode);
    bool findTransactionNode (uint256 const& nodeHash, Blob& rawTransactionNode);

    // tree synchronization operations
    bool getTransactionTreeNodes (uint32 ledgerSeq, uint256 const& myNodeID,
                                  Blob const& myNode, std::list< Blob >& newNodes);
    bool getAccountStateNodes (uint32 ledgerSeq, uint256 const& myNodeId,
                               Blob const& myNode, std::list< Blob >& newNodes);

    // ledger proposal/close functions
    void processTrustedProposal (LedgerProposal::pointer proposal, boost::shared_ptr<protocol::TMProposeSet> set,
                                 RippleAddress nodePublic, uint256 checkLedger, bool sigGood);
    SHAMapAddNode gotTXData (const boost::shared_ptr<Peer>& peer, uint256 const& hash,
                             const std::list<SHAMapNode>& nodeIDs, const std::list< Blob >& nodeData);
    bool recvValidation (SerializedValidation::ref val, const std::string& source);
    void takePosition (int seq, SHAMap::ref position);
    SHAMap::pointer getTXMap (uint256 const& hash);
    bool hasTXSet (const boost::shared_ptr<Peer>& peer, uint256 const& set, protocol::TxSetStatus status);
    void mapComplete (uint256 const& hash, SHAMap::ref map);
    bool stillNeedTXSet (uint256 const& hash);
    void makeFetchPack (Job&, boost::weak_ptr<Peer> peer, boost::shared_ptr<protocol::TMGetObjectByHash> request,
                        Ledger::pointer wantLedger, Ledger::pointer haveLedger, uint32 uUptime);
    bool shouldFetchPack (uint32 seq);
    void gotFetchPack (bool progress, uint32 seq);
    void addFetchPack (uint256 const& hash, boost::shared_ptr< Blob >& data);
    bool getFetchPack (uint256 const& hash, Blob& data);
    int getFetchSize ();
    void sweepFetchPack ();

    // network state machine

    // VFALCO TODO Try to make all these private since they seem to be...private
    //
    void switchLastClosedLedger (Ledger::pointer newLedger, bool duringConsensus); // Used for the "jump" case
    bool checkLastClosedLedger (const std::vector<Peer::pointer>&, uint256& networkClosed);
    int beginConsensus (uint256 const& networkClosed, Ledger::pointer closingLedger);
    void tryStartConsensus ();
    void endConsensus (bool correctLCL);
    void setStandAlone ()
    {
        setMode (omFULL);
    }
    void setStateTimer ();
    void newLCL (int proposers, int convergeTime, uint256 const& ledgerHash);
    void needNetworkLedger ()
    {
        mNeedNetworkLedger = true;
    }
    void clearNeedNetworkLedger ()
    {
        mNeedNetworkLedger = false;
    }
    bool isNeedNetworkLedger ()
    {
        return mNeedNetworkLedger;
    }
    bool isFull ()
    {
        return !mNeedNetworkLedger && (mMode == omFULL);
    }
    void setProposing (bool p, bool v)
    {
        mProposing = p;
        mValidating = v;
    }
    bool isProposing ()
    {
        return mProposing;
    }
    bool isValidating ()
    {
        return mValidating;
    }
    bool isFeatureBlocked ()
    {
        return mFeatureBlocked;
    }
    void setFeatureBlocked ();
    void consensusViewChange ();
    int getPreviousProposers ()
    {
        return mLastCloseProposers;
    }
    int getPreviousConvergeTime ()
    {
        return mLastCloseConvergeTime;
    }
    uint32 getLastCloseTime ()
    {
        return mLastCloseTime;
    }
    void setLastCloseTime (uint32 t)
    {
        mLastCloseTime = t;
    }
    Json::Value getConsensusInfo ();
    Json::Value getServerInfo (bool human, bool admin);
    uint32 acceptLedger ();
    boost::unordered_map < uint160,
          std::list<LedgerProposal::pointer> > & peekStoredProposals ()
    {
        return mStoredProposals;
    }
    void storeProposal (LedgerProposal::ref proposal,    const RippleAddress& peerPublic);
    uint256 getConsensusLCL ();
    void reportFeeChange ();

    void doClusterReport ();

    //Helper function to generate SQL query to get transactions
    std::string transactionsSQL (std::string selection, const RippleAddress& account,
                                 int32 minLedger, int32 maxLedger, bool descending, uint32 offset, int limit,
                                 bool binary, bool count, bool bAdmin);


    // client information retrieval functions
    std::vector< std::pair<Transaction::pointer, TransactionMetaSet::pointer> >
    getAccountTxs (const RippleAddress& account, int32 minLedger, int32 maxLedger,  bool descending, uint32 offset, int limit, bool bAdmin);

    typedef boost::tuple<std::string, std::string, uint32> txnMetaLedgerType;
    std::vector<txnMetaLedgerType>
    getAccountTxsB (const RippleAddress& account, int32 minLedger, int32 maxLedger,  bool descending, uint32 offset, int limit, bool bAdmin);

    std::vector<RippleAddress> getLedgerAffectedAccounts (uint32 ledgerSeq);
    std::vector<SerializedTransaction> getLedgerTransactions (uint32 ledgerSeq);
    uint32 countAccountTxs (const RippleAddress& account, int32 minLedger, int32 maxLedger);
    //
    // Monitoring: publisher side
    //
    void pubLedger (Ledger::ref lpAccepted);
    void pubProposedTransaction (Ledger::ref lpCurrent, SerializedTransaction::ref stTxn, TER terResult);


    //
    // Monitoring: subscriber side
    //
    void subAccount (InfoSub::ref ispListener, const boost::unordered_set<RippleAddress>& vnaAccountIDs, uint32 uLedgerIndex, bool rt);
    void unsubAccount (uint64 uListener, const boost::unordered_set<RippleAddress>& vnaAccountIDs, bool rt);

    bool subLedger (InfoSub::ref ispListener, Json::Value& jvResult);
    bool unsubLedger (uint64 uListener);

    bool subServer (InfoSub::ref ispListener, Json::Value& jvResult);
    bool unsubServer (uint64 uListener);

    bool subBook (InfoSub::ref ispListener, const uint160& currencyPays, const uint160& currencyGets,
                  const uint160& issuerPays, const uint160& issuerGets);
    bool unsubBook (uint64 uListener, const uint160& currencyPays, const uint160& currencyGets,
                    const uint160& issuerPays, const uint160& issuerGets);

    bool subTransactions (InfoSub::ref ispListener);
    bool unsubTransactions (uint64 uListener);

    bool subRTTransactions (InfoSub::ref ispListener);
    bool unsubRTTransactions (uint64 uListener);

    InfoSub::pointer    findRpcSub (const std::string& strUrl);
    InfoSub::pointer    addRpcSub (const std::string& strUrl, InfoSub::ref rspEntry);

private:
    void processNetTimer ();
    void onDeadlineTimer (DeadlineTimer& timer);

    void setMode (OperatingMode);

    Json::Value transJson (const SerializedTransaction& stTxn, TER terResult, bool bValidated, Ledger::ref lpCurrent);
    bool haveConsensusObject ();

    Json::Value pubBootstrapAccountInfo (Ledger::ref lpAccepted, const RippleAddress& naAccountID);

    void pubValidatedTransaction (Ledger::ref alAccepted, const AcceptedLedgerTx& alTransaction);
    void pubAccountTransaction (Ledger::ref lpCurrent, const AcceptedLedgerTx& alTransaction, bool isAccepted);

    void pubServer ();

private:
    typedef boost::unordered_map <uint160, SubMapType>               SubInfoMapType;
    typedef boost::unordered_map <uint160, SubMapType>::iterator     SubInfoMapIterator;

    typedef boost::unordered_map<std::string, InfoSub::pointer>     subRpcMapType;

    OperatingMode                       mMode;
    bool                                mNeedNetworkLedger;
    bool                                mProposing, mValidating;
    bool                                mFeatureBlocked;
    boost::posix_time::ptime            mConnectTime;
    DeadlineTimer                       m_netTimer;
    DeadlineTimer                       m_clusterTimer;
    boost::shared_ptr<LedgerConsensus>  mConsensus;
    boost::unordered_map < uint160,
          std::list<LedgerProposal::pointer> > mStoredProposals;

    LedgerMaster*                       mLedgerMaster;
    InboundLedger::pointer              mAcquiringLedger;

    int                                 mCloseTimeOffset;

    // last ledger close
    int                                 mLastCloseProposers, mLastCloseConvergeTime;
    uint256                             mLastCloseHash;
    uint32                              mLastCloseTime;
    uint32                              mLastValidationTime;
    SerializedValidation::pointer       mLastValidation;

    // Recent positions taken
    std::map<uint256, std::pair<int, SHAMap::pointer> > mRecentPositions;

    // XXX Split into more locks.
    boost::recursive_mutex                              mMonitorLock;
    SubInfoMapType                                      mSubAccount;
    SubInfoMapType                                      mSubRTAccount;

    subRpcMapType                                       mRpcSubMap;

    SubMapType                                          mSubLedger;             // accepted ledgers
    SubMapType                                          mSubServer;             // when server changes connectivity state
    SubMapType                                          mSubTransactions;       // all accepted transactions
    SubMapType                                          mSubRTTransactions;     // all proposed and accepted transactions

    TaggedCache< uint256, Blob , UptimeTimerAdapter >   mFetchPack;
    uint32                                              mLastFetchPack;

    // VFALCO TODO Document the special value uint32(-1) for this member
    //             and replace uint32(-1) with a constant. It is initialized
    //             in the ctor-initializer list to this constant.
    //
    uint32                                              mFetchSeq;

    uint32                                              mLastLoadBase;
    uint32                                              mLastLoadFactor;
};

#endif
