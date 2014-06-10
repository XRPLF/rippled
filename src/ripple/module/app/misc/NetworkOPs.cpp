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

#include <ripple/basics/utility/Time.h>
#include <ripple/overlay/predicates.h>
#include <ripple/common/jsonrpc_fields.h>
#include <beast/module/core/thread/DeadlineTimer.h>
#include <beast/module/core/system/SystemStats.h>
#include <tuple>

namespace ripple {

class FeeVoteLog;
template <>
char const*
LogPartition::getPartitionName <FeeVoteLog>() { return "FeeVote"; }

class NetworkOPsImp
    : public NetworkOPs
    , public beast::DeadlineTimer::Listener
    , public beast::LeakChecked <NetworkOPsImp>
{
public:
    enum Fault
    {
        // exceptions these functions can throw
        IO_ERROR    = 1,
        NO_NETWORK  = 2,
    };

public:
    // VFALCO TODO Make LedgerMaster a SharedPtr or a reference.
    //
    NetworkOPsImp (clock_type& clock, LedgerMaster& ledgerMaster,
        Stoppable& parent, beast::Journal journal)
        : NetworkOPs (parent)
        , m_clock (clock)
        , m_journal (journal)
        , m_localTX (LocalTxs::New ())
        , m_feeVote (make_FeeVote(10, 20 * SYSTEM_CURRENCY_PARTS,
            5 * SYSTEM_CURRENCY_PARTS, LogPartition::getJournal <FeeVoteLog>()))
        , mMode (omDISCONNECTED)
        , mNeedNetworkLedger (false)
        , mProposing (false)
        , mValidating (false)
        , m_amendmentBlocked (false)
        , m_heartbeatTimer (this)
        , m_clusterTimer (this)
        , m_ledgerMaster (ledgerMaster)
        , mCloseTimeOffset (0)
        , mLastCloseProposers (0)
        , mLastCloseConvergeTime (1000 * LEDGER_IDLE_INTERVAL)
        , mLastCloseTime (0)
        , mLastValidationTime (0)
        , mFetchPack ("FetchPack", 65536, 45, clock,
            LogPartition::getJournal <TaggedCacheLog> ())
        , mFetchSeq (0)
        , mLastLoadBase (256)
        , mLastLoadFactor (256)
    {
    }

    ~NetworkOPsImp ()
    {
    }

    // network information
    std::uint32_t getNetworkTimeNC ();                 // Our best estimate of wall time in seconds from 1/1/2000
    std::uint32_t getCloseTimeNC ();                   // Our best estimate of current ledger close time
    std::uint32_t getValidationTimeNC ();              // Use *only* to timestamp our own validation
    void closeTimeOffset (int);
    boost::posix_time::ptime getNetworkTimePT ();
    std::uint32_t getLedgerID (uint256 const& hash);
    std::uint32_t getCurrentLedgerID ();
    OperatingMode getOperatingMode ()
    {
        return mMode;
    }
    std::string strOperatingMode ();

    Ledger::pointer     getClosedLedger ()
    {
        return m_ledgerMaster.getClosedLedger ();
    }
    Ledger::pointer     getValidatedLedger ()
    {
        return m_ledgerMaster.getValidatedLedger ();
    }
    Ledger::pointer     getPublishedLedger ()
    {
        return m_ledgerMaster.getPublishedLedger ();
    }
    Ledger::pointer     getCurrentLedger ()
    {
        return m_ledgerMaster.getCurrentLedger ();
    }
    Ledger::pointer getLedgerByHash (uint256 const& hash)
    {
        return m_ledgerMaster.getLedgerByHash (hash);
    }
    Ledger::pointer getLedgerBySeq (const std::uint32_t seq);
    void            missingNodeInLedger (const std::uint32_t seq);

    uint256         getClosedLedgerHash ()
    {
        return m_ledgerMaster.getClosedLedger ()->getHash ();
    }

    // Do we have this inclusive range of ledgers in our database
    bool haveLedgerRange (std::uint32_t from, std::uint32_t to);
    bool haveLedger (std::uint32_t seq);
    std::uint32_t getValidatedSeq ();
    bool isValidated (std::uint32_t seq);
    bool isValidated (std::uint32_t seq, uint256 const& hash);
    bool isValidated (Ledger::ref l)
    {
        return isValidated (l->getLedgerSeq (), l->getHash ());
    }
    bool getValidatedRange (std::uint32_t& minVal, std::uint32_t& maxVal)
    {
        return m_ledgerMaster.getValidatedRange (minVal, maxVal);
    }
    bool getFullValidatedRange (std::uint32_t& minVal, std::uint32_t& maxVal)
    {
        return m_ledgerMaster.getFullValidatedRange (minVal, maxVal);
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
    typedef std::function<void (Transaction::pointer, TER)> stCallback; // must complete immediately
    void submitTransaction (Job&, SerializedTransaction::pointer, stCallback callback = stCallback ());
    Transaction::pointer submitTransactionSync (Transaction::ref tpTrans, bool bAdmin, bool bLocal, bool bFailHard, bool bSubmit);

    void runTransactionQueue ();
    Transaction::pointer processTransactionCb (Transaction::pointer, bool bAdmin, bool bLocal, bool bFailHard, stCallback);
    Transaction::pointer processTransaction (Transaction::pointer transaction, bool bAdmin, bool bLocal, bool bFailHard)
    {
        return processTransactionCb (transaction, bAdmin, bLocal, bFailHard, stCallback ());
    }

    // VFALCO Workaround for MSVC std::function which doesn't swallow return types.
    //
    void processTransactionCbVoid (Transaction::pointer p, bool bAdmin, bool bLocal, bool bFailHard, stCallback cb)
    {
        processTransactionCb (p, bAdmin, bLocal, bFailHard, cb);
    }

    Transaction::pointer findTransactionByID (uint256 const& transactionID);
#if 0
    int findTransactionsBySource (uint256 const& uLedger, std::list<Transaction::pointer>&, const RippleAddress& sourceAccount,
                                  std::uint32_t minSeq, std::uint32_t maxSeq);
#endif
    int findTransactionsByDestination (std::list<Transaction::pointer>&, const RippleAddress& destinationAccount,
                                       std::uint32_t startLedgerSeq, std::uint32_t endLedgerSeq, int maxTransactions);

    //
    // Account functions
    //

    AccountState::pointer   getAccountState (Ledger::ref lrLedger, const RippleAddress& accountID);
    SLE::pointer            getGenerator (Ledger::ref lrLedger, const uint160& uGeneratorID);

    //
    // Directory functions
    //

    STVector256             getDirNodeInfo (Ledger::ref lrLedger, uint256 const& uRootIndex,
                                            std::uint64_t& uNodePrevious, std::uint64_t& uNodeNext);

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

    // ledger proposal/close functions
    void processTrustedProposal (LedgerProposal::pointer proposal, std::shared_ptr<protocol::TMProposeSet> set,
                                 RippleAddress nodePublic, uint256 checkLedger, bool sigGood);
    SHAMapAddNode gotTXData (const std::shared_ptr<Peer>& peer, uint256 const& hash,
                             const std::list<SHAMapNode>& nodeIDs, const std::list< Blob >& nodeData);
    bool recvValidation (SerializedValidation::ref val, const std::string& source);
    void takePosition (int seq, SHAMap::ref position);
    SHAMap::pointer getTXMap (uint256 const& hash);
    bool hasTXSet (const std::shared_ptr<Peer>& peer, uint256 const& set, protocol::TxSetStatus status);
    void mapComplete (uint256 const& hash, SHAMap::ref map);
    bool stillNeedTXSet (uint256 const& hash);
    void makeFetchPack (Job&, std::weak_ptr<Peer> peer, std::shared_ptr<protocol::TMGetObjectByHash> request,
                        uint256 haveLedger, std::uint32_t uUptime);
    bool shouldFetchPack (std::uint32_t seq);
    void gotFetchPack (bool progress, std::uint32_t seq);
    void addFetchPack (uint256 const& hash, std::shared_ptr< Blob >& data);
    bool getFetchPack (uint256 const& hash, Blob& data);
    int getFetchSize ();
    void sweepFetchPack ();

    // network state machine

    // VFALCO TODO Try to make all these private since they seem to be...private
    //
    void switchLastClosedLedger (Ledger::pointer newLedger, bool duringConsensus); // Used for the "jump" case
    bool checkLastClosedLedger (const Overlay::PeerSequence&, uint256& networkClosed);
    int beginConsensus (uint256 const& networkClosed, Ledger::pointer closingLedger);
    void tryStartConsensus ();
    void endConsensus (bool correctLCL);
    void setStandAlone ()
    {
        setMode (omFULL);
    }

    /** Called to initially start our timers.
        Not called for stand-alone mode.
    */
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
    bool isAmendmentBlocked ()
    {
        return m_amendmentBlocked;
    }
    void setAmendmentBlocked ();
    void consensusViewChange ();
    int getPreviousProposers ()
    {
        return mLastCloseProposers;
    }
    int getPreviousConvergeTime ()
    {
        return mLastCloseConvergeTime;
    }
    std::uint32_t getLastCloseTime ()
    {
        return mLastCloseTime;
    }
    void setLastCloseTime (std::uint32_t t)
    {
        mLastCloseTime = t;
    }
    Json::Value getConsensusInfo ();
    Json::Value getServerInfo (bool human, bool admin);
    void clearLedgerFetch ();
    Json::Value getLedgerFetchInfo ();
    std::uint32_t acceptLedger ();
    ripple::unordered_map < uint160,
          std::list<LedgerProposal::pointer> > & peekStoredProposals ()
    {
        return mStoredProposals;
    }
    void storeProposal (LedgerProposal::ref proposal,    const RippleAddress& peerPublic);
    uint256 getConsensusLCL ();
    void reportFeeChange ();

    void updateLocalTx (Ledger::ref newValidLedger) override
    {
        m_localTX->sweep (newValidLedger);
    }
    void addLocalTx (Ledger::ref openLedger, SerializedTransaction::ref txn) override
    {
        m_localTX->push_back (openLedger->getLedgerSeq(), txn);
    }
    std::size_t getLocalTxCount () override
    {
        return m_localTX->size ();
    }

    //Helper function to generate SQL query to get transactions
    std::string transactionsSQL (std::string selection, const RippleAddress& account,
                                 std::int32_t minLedger, std::int32_t maxLedger,
                                 bool descending, std::uint32_t offset, int limit,
                                 bool binary, bool count, bool bAdmin);


    // client information retrieval functions
    std::vector< std::pair<Transaction::pointer, TransactionMetaSet::pointer> >
    getAccountTxs (const RippleAddress& account, std::int32_t minLedger, std::int32_t maxLedger,
                   bool descending, std::uint32_t offset, int limit, bool bAdmin);

    std::vector< std::pair<Transaction::pointer, TransactionMetaSet::pointer> >
    getTxsAccount (const RippleAddress& account, std::int32_t minLedger, std::int32_t maxLedger,
                   bool forward, Json::Value& token, int limit, bool bAdmin);

    typedef std::tuple<std::string, std::string, std::uint32_t> txnMetaLedgerType;

    std::vector<txnMetaLedgerType>
    getAccountTxsB (const RippleAddress& account, std::int32_t minLedger,
                    std::int32_t maxLedger,  bool descending, std::uint32_t offset,
                    int limit, bool bAdmin);

    std::vector<txnMetaLedgerType>
    getTxsAccountB (const RippleAddress& account, std::int32_t minLedger,
                    std::int32_t maxLedger,  bool forward, Json::Value& token, int limit, bool bAdmin);

    std::vector<RippleAddress> getLedgerAffectedAccounts (std::uint32_t ledgerSeq);

    //
    // Monitoring: publisher side
    //
    void pubLedger (Ledger::ref lpAccepted);
    void pubProposedTransaction (Ledger::ref lpCurrent, SerializedTransaction::ref stTxn, TER terResult);

    //--------------------------------------------------------------------------
    //
    // InfoSub::Source
    //
    void subAccount (InfoSub::ref ispListener,
                     const boost::unordered_set<RippleAddress>& vnaAccountIDs,
                     std::uint32_t uLedgerIndex, bool rt);
    void unsubAccount (std::uint64_t uListener,
                       const boost::unordered_set<RippleAddress>& vnaAccountIDs,
                       bool rt);

    bool subLedger (InfoSub::ref ispListener, Json::Value& jvResult);
    bool unsubLedger (std::uint64_t uListener);

    bool subServer (InfoSub::ref ispListener, Json::Value& jvResult);
    bool unsubServer (std::uint64_t uListener);

    bool subBook (InfoSub::ref ispListener, RippleCurrency const& currencyPays, RippleCurrency const& currencyGets,
                  RippleIssuer const& issuerPays, RippleIssuer const& issuerGets);
    bool unsubBook (std::uint64_t uListener, RippleCurrency const& currencyPays,
                    RippleCurrency const& currencyGets,
                    RippleIssuer const& issuerPays, RippleIssuer const& issuerGets);

    bool subTransactions (InfoSub::ref ispListener);
    bool unsubTransactions (std::uint64_t uListener);

    bool subRTTransactions (InfoSub::ref ispListener);
    bool unsubRTTransactions (std::uint64_t uListener);

    InfoSub::pointer    findRpcSub (const std::string& strUrl);
    InfoSub::pointer    addRpcSub (const std::string& strUrl, InfoSub::ref rspEntry);

    //--------------------------------------------------------------------------
    //
    // Stoppable

    void onStop ()
    {
        m_heartbeatTimer.cancel();
        m_clusterTimer.cancel();

        stopped ();
    }

private:
    void setHeartbeatTimer ();
    void setClusterTimer ();
    void onDeadlineTimer (beast::DeadlineTimer& timer);
    void processHeartbeatTimer ();
    void processClusterTimer ();

    void setMode (OperatingMode);

    Json::Value transJson (const SerializedTransaction& stTxn, TER terResult, bool bValidated, Ledger::ref lpCurrent);
    bool haveConsensusObject ();

    Json::Value pubBootstrapAccountInfo (Ledger::ref lpAccepted, const RippleAddress& naAccountID);

    void pubValidatedTransaction (Ledger::ref alAccepted, const AcceptedLedgerTx& alTransaction);
    void pubAccountTransaction (Ledger::ref lpCurrent, const AcceptedLedgerTx& alTransaction, bool isAccepted);

    void pubServer ();

private:
    clock_type& m_clock;

    typedef ripple::unordered_map <uint160, SubMapType>               SubInfoMapType;
    typedef ripple::unordered_map <uint160, SubMapType>::iterator     SubInfoMapIterator;

    typedef ripple::unordered_map<std::string, InfoSub::pointer>     subRpcMapType;

    // XXX Split into more locks.
    typedef RippleRecursiveMutex LockType;
    typedef std::lock_guard <LockType> ScopedLockType;

    beast::Journal m_journal;

    std::unique_ptr <LocalTxs> m_localTX;

    std::unique_ptr <FeeVote> m_feeVote;

    LockType mLock;

    OperatingMode                       mMode;
    bool                                mNeedNetworkLedger;
    bool                                mProposing, mValidating;
    bool                                m_amendmentBlocked;
    boost::posix_time::ptime            mConnectTime;
    beast::DeadlineTimer                m_heartbeatTimer;
    beast::DeadlineTimer                m_clusterTimer;
    std::shared_ptr<LedgerConsensus>  mConsensus;
    ripple::unordered_map < uint160,
          std::list<LedgerProposal::pointer> > mStoredProposals;

    LedgerMaster&                       m_ledgerMaster;
    InboundLedger::pointer              mAcquiringLedger;

    int                                 mCloseTimeOffset;

    // last ledger close
    int                                 mLastCloseProposers, mLastCloseConvergeTime;
    uint256                             mLastCloseHash;
    std::uint32_t                       mLastCloseTime;
    std::uint32_t                       mLastValidationTime;
    SerializedValidation::pointer       mLastValidation;

    // Recent positions taken
    std::map<uint256, std::pair<int, SHAMap::pointer> > mRecentPositions;

    SubInfoMapType                                      mSubAccount;
    SubInfoMapType                                      mSubRTAccount;

    subRpcMapType                                       mRpcSubMap;

    SubMapType                                          mSubLedger;             // accepted ledgers
    SubMapType                                          mSubServer;             // when server changes connectivity state
    SubMapType                                          mSubTransactions;       // all accepted transactions
    SubMapType                                          mSubRTTransactions;     // all proposed and accepted transactions

    TaggedCache< uint256, Blob>                         mFetchPack;
    std::uint32_t                                       mFetchSeq;

    std::uint32_t                                       mLastLoadBase;
    std::uint32_t                                       mLastLoadFactor;
};

//------------------------------------------------------------------------------

void NetworkOPsImp::setStateTimer ()
{
    setHeartbeatTimer ();
    setClusterTimer ();
}

void NetworkOPsImp::setHeartbeatTimer ()
{
    m_heartbeatTimer.setExpiration (LEDGER_GRANULARITY / 1000.0);
}

void NetworkOPsImp::setClusterTimer ()
{
    m_clusterTimer.setExpiration (10.0);
}

void NetworkOPsImp::onDeadlineTimer (beast::DeadlineTimer& timer)
{
    if (timer == m_heartbeatTimer)
    {
        getApp().getJobQueue ().addJob (jtNETOP_TIMER, "NetOPs.heartbeat",
            std::bind (&NetworkOPsImp::processHeartbeatTimer, this));
    }
    else if (timer == m_clusterTimer)
    {
        getApp().getJobQueue ().addJob (jtNETOP_CLUSTER, "NetOPs.cluster",
            std::bind (&NetworkOPsImp::processClusterTimer, this));
    }
}

void NetworkOPsImp::processHeartbeatTimer ()
{
    {
        Application::ScopedLockType lock (getApp().getMasterLock ());

        // VFALCO NOTE This is for diagnosing a crash on exit
        Application& app (getApp ());
        LoadManager& mgr (app.getLoadManager ());
        mgr.resetDeadlockDetector ();

        std::size_t const numPeers = getApp().overlay ().size ();

        // do we have sufficient peers? If not, we are disconnected.
        if (numPeers < getConfig ().NETWORK_QUORUM)
        {
            if (mMode != omDISCONNECTED)
            {
                setMode (omDISCONNECTED);
                m_journal.warning
                    << "Node count (" << numPeers << ") "
                    << "has fallen below quorum (" << getConfig ().NETWORK_QUORUM << ").";
            }

            setHeartbeatTimer ();

            return;
        }

        if (mMode == omDISCONNECTED)
        {
            setMode (omCONNECTED);
            m_journal.info << "Node count (" << numPeers << ") is sufficient.";
        }

        // Check if the last validated ledger forces a change between these states
        if (mMode == omSYNCING)
        {
            setMode (omSYNCING);
        }
        else if (mMode == omCONNECTED)
        {
            setMode (omCONNECTED);
        }

        if (!mConsensus)
            tryStartConsensus ();

        if (mConsensus)
            mConsensus->timerEntry ();
    }

    setHeartbeatTimer ();
}

void NetworkOPsImp::processClusterTimer ()
{
    bool synced = (m_ledgerMaster.getValidatedLedgerAge() <= 240);
    ClusterNodeStatus us("", synced ? getApp().getFeeTrack().getLocalFee() : 0, getNetworkTimeNC());
    if (!getApp().getUNL().nodeUpdate(getApp().getLocalCredentials().getNodePublic(), us))
    {
        m_journal.debug << "To soon to send cluster update";
        return;
    }

    std::map<RippleAddress, ClusterNodeStatus> nodes = getApp().getUNL().getClusterStatus();

    protocol::TMCluster cluster;
    for (std::map<RippleAddress, ClusterNodeStatus>::iterator it = nodes.begin(),
            end = nodes.end(); it != end; ++it)
    {
        protocol::TMClusterNode& node = *cluster.add_clusternodes();
        node.set_publickey(it->first.humanNodePublic());
        node.set_reporttime(it->second.getReportTime());
        node.set_nodeload(it->second.getLoadFee());
        if (!it->second.getName().empty())
            node.set_nodename(it->second.getName());
    }

    Resource::Gossip gossip = getApp().getResourceManager().exportConsumers();
    BOOST_FOREACH (Resource::Gossip::Item const& item, gossip.items)
    {
        protocol::TMLoadSource& node = *cluster.add_loadsources();
        node.set_name (to_string (item.address));
        node.set_cost (item.balance);
    }
    getApp ().overlay ().foreach (send_if (
        std::make_shared<Message>(cluster, protocol::mtCLUSTER),
        peer_in_cluster ()));
    setClusterTimer ();
}

//------------------------------------------------------------------------------


std::string NetworkOPsImp::strOperatingMode ()
{
    static const char* paStatusToken [] =
    {
        "disconnected",
        "connected",
        "syncing",
        "tracking",
        "full"
    };

    if (mMode == omFULL)
    {
        if (mProposing)
            return "proposing";

        if (mValidating)
            return "validating";
    }

    return paStatusToken[mMode];
}

boost::posix_time::ptime NetworkOPsImp::getNetworkTimePT ()
{
    int offset = 0;

    getApp().getSystemTimeOffset (offset);

    // VFALCO TODO Replace this with a beast call
    return boost::posix_time::microsec_clock::universal_time () + boost::posix_time::seconds (offset);
}

std::uint32_t NetworkOPsImp::getNetworkTimeNC ()
{
    return iToSeconds (getNetworkTimePT ());
}

std::uint32_t NetworkOPsImp::getCloseTimeNC ()
{
    return iToSeconds (getNetworkTimePT () + boost::posix_time::seconds (mCloseTimeOffset));
}

std::uint32_t NetworkOPsImp::getValidationTimeNC ()
{
    std::uint32_t vt = getNetworkTimeNC ();

    if (vt <= mLastValidationTime)
        vt = mLastValidationTime + 1;

    mLastValidationTime = vt;
    return vt;
}

void NetworkOPsImp::closeTimeOffset (int offset)
{
    // take large offsets, ignore small offsets, push towards our wall time
    if (offset > 1)
        mCloseTimeOffset += (offset + 3) / 4;
    else if (offset < -1)
        mCloseTimeOffset += (offset - 3) / 4;
    else
        mCloseTimeOffset = (mCloseTimeOffset * 3) / 4;

    if (mCloseTimeOffset != 0)
        m_journal.info << "Close time offset now " << mCloseTimeOffset;
}

std::uint32_t NetworkOPsImp::getLedgerID (uint256 const& hash)
{
    Ledger::pointer  lrLedger   = m_ledgerMaster.getLedgerByHash (hash);

    return lrLedger ? lrLedger->getLedgerSeq () : 0;
}

Ledger::pointer NetworkOPsImp::getLedgerBySeq (const std::uint32_t seq)
{
    return m_ledgerMaster.getLedgerBySeq (seq);
}

std::uint32_t NetworkOPsImp::getCurrentLedgerID ()
{
    return m_ledgerMaster.getCurrentLedger ()->getLedgerSeq ();
}

bool NetworkOPsImp::haveLedgerRange (std::uint32_t from, std::uint32_t to)
{
    return m_ledgerMaster.haveLedgerRange (from, to);
}

bool NetworkOPsImp::haveLedger (std::uint32_t seq)
{
    return m_ledgerMaster.haveLedger (seq);
}

std::uint32_t NetworkOPsImp::getValidatedSeq ()
{
    return m_ledgerMaster.getValidatedLedger ()->getLedgerSeq ();
}

bool NetworkOPsImp::isValidated (std::uint32_t seq, uint256 const& hash)
{
    if (!isValidated (seq))
        return false;

    return m_ledgerMaster.getHashBySeq (seq) == hash;
}

bool NetworkOPsImp::isValidated (std::uint32_t seq)
{
    // use when ledger was retrieved by seq
    return haveLedger (seq) && (seq <= m_ledgerMaster.getValidatedLedger ()->getLedgerSeq ());
}

void NetworkOPsImp::submitTransaction (Job&, SerializedTransaction::pointer iTrans, stCallback callback)
{
    // this is an asynchronous interface
    Serializer s;
    iTrans->add (s);

    SerializerIterator sit (s);
    SerializedTransaction::pointer trans = std::make_shared<SerializedTransaction> (std::ref (sit));

    uint256 suppress = trans->getTransactionID ();
    int flags;

    if (getApp().getHashRouter ().addSuppressionPeer (suppress, 0, flags) && ((flags & SF_RETRY) != 0))
    {
        m_journal.warning << "Redundant transactions submitted";
        return;
    }

    if ((flags & SF_BAD) != 0)
    {
        m_journal.warning << "Submitted transaction cached bad";
        return;
    }

    if ((flags & SF_SIGGOOD) == 0)
    {
        try
        {
            if (!passesLocalChecks (*trans) || !trans->checkSign ())
            {
                m_journal.warning << "Submitted transaction has bad signature";
                getApp().getHashRouter ().setFlag (suppress, SF_BAD);
                return;
            }

            getApp().getHashRouter ().setFlag (suppress, SF_SIGGOOD);
        }
        catch (...)
        {
            m_journal.warning << "Exception checking transaction " << suppress;
            return;
        }
    }

    getApp().getJobQueue().addJob (jtTRANSACTION, "submitTxn",
        std::bind (&NetworkOPsImp::processTransactionCbVoid, this,
            std::make_shared<Transaction> (trans, false), false, false, false, callback));
}

// Sterilize transaction through serialization.
// This is fully synchronous and deprecated
Transaction::pointer NetworkOPsImp::submitTransactionSync (Transaction::ref tpTrans, bool bAdmin, bool bLocal, bool bFailHard, bool bSubmit)
{
    Serializer s;
    tpTrans->getSTransaction ()->add (s);

    Transaction::pointer tpTransNew = Transaction::sharedTransaction (s.getData (), true);

    if (!tpTransNew)
    {
        // Could not construct transaction.
        return tpTransNew;
    }

    if (tpTransNew->getSTransaction ()->isEquivalent (*tpTrans->getSTransaction ()))
    {
        if (bSubmit)
            (void) NetworkOPsImp::processTransaction (tpTransNew, bAdmin, bLocal, bFailHard);
    }
    else
    {
        m_journal.fatal << "Transaction reconstruction failure";
        m_journal.fatal << tpTransNew->getSTransaction ()->getJson (0);
        m_journal.fatal << tpTrans->getSTransaction ()->getJson (0);

        // assert (false); "1e-95" as amount can trigger this

        tpTransNew.reset ();
    }

    return tpTransNew;
}

void NetworkOPsImp::runTransactionQueue ()
{
    TxQueueEntry::pointer txn;

    for (int i = 0; i < 10; ++i)
    {
        getApp().getTxQueue ().getJob (txn);

        if (!txn)
            return;

        {
            LoadEvent::autoptr ev = getApp().getJobQueue ().getLoadEventAP (jtTXN_PROC, "runTxnQ");

            {
                Application::ScopedLockType lock (getApp().getMasterLock ());

                Transaction::pointer dbtx = getApp().getMasterTransaction ().fetch (txn->getID (), true);
                assert (dbtx);

                bool didApply;
                TER r = m_ledgerMaster.doTransaction (dbtx->getSTransaction (),
                                                      tapOPEN_LEDGER | tapNO_CHECK_SIGN, didApply);
                dbtx->setResult (r);

                if (isTemMalformed (r)) // malformed, cache bad
                    getApp().getHashRouter ().setFlag (txn->getID (), SF_BAD);
    //            else if (isTelLocal (r) || isTerRetry (r)) // can be retried
    //                getApp().getHashRouter ().setFlag (txn->getID (), SF_RETRY);


                if (isTerRetry (r))
                {
                    // transaction should be held
                    m_journal.debug << "QTransaction should be held: " << r;
                    dbtx->setStatus (HELD);
                    getApp().getMasterTransaction ().canonicalize (&dbtx);
                    m_ledgerMaster.addHeldTransaction (dbtx);
                }
                else if (r == tefPAST_SEQ)
                {
                    // duplicate or conflict
                    m_journal.info << "QTransaction is obsolete";
                    dbtx->setStatus (OBSOLETE);
                }
                else if (r == tesSUCCESS)
                {
                    m_journal.info << "QTransaction is now included in open ledger";
                    dbtx->setStatus (INCLUDED);
                    getApp().getMasterTransaction ().canonicalize (&dbtx);
                }
                else
                {
                    m_journal.debug << "QStatus other than success " << r;
                    dbtx->setStatus (INVALID);
                }

                if (didApply /*|| (mMode != omFULL)*/ )
                {
                    std::set <Peer::ShortId> peers;

                    if (getApp().getHashRouter ().swapSet (txn->getID (), peers, SF_RELAYED))
                    {
                        m_journal.debug << "relaying";
                        protocol::TMTransaction tx;
                        Serializer s;
                        dbtx->getSTransaction ()->add (s);
                        tx.set_rawtransaction (&s.getData ().front (), s.getLength ());
                        tx.set_status (protocol::tsCURRENT);
                        tx.set_receivetimestamp (getNetworkTimeNC ()); // FIXME: This should be when we received it
                        getApp ().overlay ().foreach (send_if_not (
                            std::make_shared<Message> (tx, protocol::mtTRANSACTION),
                            peer_in_set(peers)));
                    }
                    else
                        m_journal.debug << "recently relayed";
                }

                txn->doCallbacks (r);
            }
        }
    }

    if (getApp().getTxQueue ().stopProcessing (txn))
        getApp().getIOService ().post (std::bind (&NetworkOPsImp::runTransactionQueue, this));
}

Transaction::pointer NetworkOPsImp::processTransactionCb (
    Transaction::pointer trans, bool bAdmin, bool bLocal, bool bFailHard, stCallback callback)
{
    LoadEvent::autoptr ev = getApp().getJobQueue ().getLoadEventAP (jtTXN_PROC, "ProcessTXN");

    int newFlags = getApp().getHashRouter ().getFlags (trans->getID ());

    if ((newFlags & SF_BAD) != 0)
    {
        // cached bad
        trans->setStatus (INVALID);
        trans->setResult (temBAD_SIGNATURE);
        return trans;
    }

    if ((newFlags & SF_SIGGOOD) == 0)
    {
        // signature not checked
        if (!trans->checkSign ())
        {
            m_journal.info << "Transaction has bad signature";
            trans->setStatus (INVALID);
            trans->setResult (temBAD_SIGNATURE);
            getApp().getHashRouter ().setFlag (trans->getID (), SF_BAD);
            return trans;
        }

        getApp().getHashRouter ().setFlag (trans->getID (), SF_SIGGOOD);
    }

    {
        Application::ScopedLockType lock (getApp().getMasterLock ());

        bool didApply;
        TER r = m_ledgerMaster.doTransaction (trans->getSTransaction (),
                                              bAdmin ? (tapOPEN_LEDGER | tapNO_CHECK_SIGN | tapADMIN) : (tapOPEN_LEDGER | tapNO_CHECK_SIGN), didApply);
        trans->setResult (r);

        if (isTemMalformed (r)) // malformed, cache bad
            getApp().getHashRouter ().setFlag (trans->getID (), SF_BAD);
    //    else if (isTelLocal (r) || isTerRetry (r)) // can be retried
    //        getApp().getHashRouter ().setFlag (trans->getID (), SF_RETRY);

#ifdef BEAST_DEBUG
        if (r != tesSUCCESS)
        {
            std::string token, human;
            if (transResultInfo (r, token, human))
                m_journal.info << "TransactionResult: " << token << ": " << human;
        }

#endif

        if (callback)
            callback (trans, r);


        if (r == tefFAILURE)
        {
            // VFALCO TODO All callers use a try block so this should be changed to
            //             a return value.
            throw Fault (IO_ERROR);
        }

        bool addLocal = bLocal;

        if (r == tesSUCCESS)
        {
            m_journal.info << "Transaction is now included in open ledger";
            trans->setStatus (INCLUDED);

            // VFALCO NOTE The value of trans can be changed here!!
            getApp().getMasterTransaction ().canonicalize (&trans);
        }
        else if (r == tefPAST_SEQ)
        {
            // duplicate or conflict
            m_journal.info << "Transaction is obsolete";
            trans->setStatus (OBSOLETE);
        }
        else if (isTerRetry (r))
        {
            if (bFailHard)
                addLocal = false;
            else
            {
                // transaction should be held
                m_journal.debug << "Transaction should be held: " << r;
                trans->setStatus (HELD);
                getApp().getMasterTransaction ().canonicalize (&trans);
                m_ledgerMaster.addHeldTransaction (trans);
            }
        }
        else
        {
            m_journal.debug << "Status other than success " << r;
            trans->setStatus (INVALID);
        }

        if (addLocal)
            addLocalTx (m_ledgerMaster.getCurrentLedger (), trans->getSTransaction ());

        if (didApply || ((mMode != omFULL) && !bFailHard && bLocal))
        {
            std::set<Peer::ShortId> peers;

            if (getApp().getHashRouter ().swapSet (trans->getID (), peers, SF_RELAYED))
            {
                protocol::TMTransaction tx;
                Serializer s;
                trans->getSTransaction ()->add (s);
                tx.set_rawtransaction (&s.getData ().front (), s.getLength ());
                tx.set_status (protocol::tsCURRENT);
                tx.set_receivetimestamp (getNetworkTimeNC ()); // FIXME: This should be when we received it
                getApp ().overlay ().foreach (send_if_not (
                    std::make_shared<Message> (tx, protocol::mtTRANSACTION),
                    peer_in_set(peers)));
            }
        }
    }

    return trans;
}

Transaction::pointer NetworkOPsImp::findTransactionByID (uint256 const& transactionID)
{
    return Transaction::load (transactionID);
}

int NetworkOPsImp::findTransactionsByDestination (std::list<Transaction::pointer>& txns,
        const RippleAddress& destinationAccount, std::uint32_t startLedgerSeq,
        std::uint32_t endLedgerSeq, int maxTransactions)
{
    // WRITEME
    return 0;
}

//
// Account functions
//

AccountState::pointer NetworkOPsImp::getAccountState (Ledger::ref lrLedger, const RippleAddress& accountID)
{
    return lrLedger->getAccountState (accountID);
}

SLE::pointer NetworkOPsImp::getGenerator (Ledger::ref lrLedger, const uint160& uGeneratorID)
{
    if (!lrLedger)
        return SLE::pointer ();

    return lrLedger->getGenerator (uGeneratorID);
}

//
// Directory functions
//

// <-- false : no entrieS
STVector256 NetworkOPsImp::getDirNodeInfo (
    Ledger::ref         lrLedger,
    uint256 const&      uNodeIndex,
    std::uint64_t&      uNodePrevious,
    std::uint64_t&      uNodeNext)
{
    STVector256         svIndexes;
    SLE::pointer        sleNode     = lrLedger->getDirNode (uNodeIndex);

    if (sleNode)
    {
        m_journal.debug << "getDirNodeInfo: node index: " << to_string (uNodeIndex);

        m_journal.trace << "getDirNodeInfo: first: " << strHex (sleNode->getFieldU64 (sfIndexPrevious));
        m_journal.trace << "getDirNodeInfo:  last: " << strHex (sleNode->getFieldU64 (sfIndexNext));

        uNodePrevious   = sleNode->getFieldU64 (sfIndexPrevious);
        uNodeNext       = sleNode->getFieldU64 (sfIndexNext);
        svIndexes       = sleNode->getFieldV256 (sfIndexes);

        m_journal.trace << "getDirNodeInfo: first: " << strHex (uNodePrevious);
        m_journal.trace << "getDirNodeInfo:  last: " << strHex (uNodeNext);
    }
    else
    {
        m_journal.info << "getDirNodeInfo: node index: NOT FOUND: " << to_string (uNodeIndex);

        uNodePrevious   = 0;
        uNodeNext       = 0;
    }

    return svIndexes;
}

#if 0
//
// Nickname functions
//

NicknameState::pointer NetworkOPsImp::getNicknameState (uint256 const& uLedger, const std::string& strNickname)
{
    return m_ledgerMaster.getLedgerByHash (uLedger)->getNicknameState (strNickname);
}
#endif

//
// Owner functions
//

Json::Value NetworkOPsImp::getOwnerInfo (Ledger::pointer lpLedger, const RippleAddress& naAccount)
{
    Json::Value jvObjects (Json::objectValue);

    uint256             uRootIndex  = lpLedger->getOwnerDirIndex (naAccount.getAccountID ());

    SLE::pointer        sleNode     = lpLedger->getDirNode (uRootIndex);

    if (sleNode)
    {
        std::uint64_t  uNodeDir;

        do
        {
            STVector256                 svIndexes   = sleNode->getFieldV256 (sfIndexes);
            const std::vector<uint256>& vuiIndexes  = svIndexes.peekValue ();

            BOOST_FOREACH (uint256 const & uDirEntry, vuiIndexes)
            {
                SLE::pointer        sleCur      = lpLedger->getSLEi (uDirEntry);

                switch (sleCur->getType ())
                {
                case ltOFFER:
                    if (!jvObjects.isMember (jss::offers))
                        jvObjects[jss::offers]         = Json::Value (Json::arrayValue);

                    jvObjects[jss::offers].append (sleCur->getJson (0));
                    break;

                case ltRIPPLE_STATE:
                    if (!jvObjects.isMember (jss::ripple_lines))
                        jvObjects[jss::ripple_lines]   = Json::Value (Json::arrayValue);

                    jvObjects[jss::ripple_lines].append (sleCur->getJson (0));
                    break;

                case ltACCOUNT_ROOT:
                case ltDIR_NODE:
                case ltGENERATOR_MAP:
                case ltNICKNAME:
                default:
                    assert (false);
                    break;
                }
            }

            uNodeDir        = sleNode->getFieldU64 (sfIndexNext);

            if (uNodeDir)
            {
                sleNode = lpLedger->getDirNode (Ledger::getDirNodeIndex (uRootIndex, uNodeDir));
                assert (sleNode);
            }
        }
        while (uNodeDir);
    }

    return jvObjects;
}

//
// Other
//

void NetworkOPsImp::setAmendmentBlocked ()
{
    m_amendmentBlocked = true;
    setMode (omTRACKING);
}

class ValidationCount
{
public:
    int trustedValidations, nodesUsing;
    uint160 highNodeUsing, highValidation;

    ValidationCount () : trustedValidations (0), nodesUsing (0)
    {
    }

    bool operator> (const ValidationCount& v)
    {
        if (trustedValidations > v.trustedValidations)
            return true;

        if (trustedValidations < v.trustedValidations)
            return false;

        if (trustedValidations == 0)
        {
            if (nodesUsing > v.nodesUsing)
                return true;

            if (nodesUsing < v.nodesUsing) return
                false;

            return highNodeUsing > v.highNodeUsing;
        }

        return highValidation > v.highValidation;
    }
};

void NetworkOPsImp::tryStartConsensus ()
{
    uint256 networkClosed;
    bool ledgerChange = checkLastClosedLedger (getApp().overlay ().getActivePeers (), networkClosed);

    if (networkClosed.isZero ())
        return;

    // WRITEME: Unless we are in omFULL and in the process of doing a consensus,
    // we must count how many nodes share our LCL, how many nodes disagree with our LCL,
    // and how many validations our LCL has. We also want to check timing to make sure
    // there shouldn't be a newer LCL. We need this information to do the next three
    // tests.

    if (((mMode == omCONNECTED) || (mMode == omSYNCING)) && !ledgerChange)
    {
        // count number of peers that agree with us and UNL nodes whose validations we have for LCL
        // if the ledger is good enough, go to omTRACKING - TODO
        if (!mNeedNetworkLedger)
            setMode (omTRACKING);
    }

    if (((mMode == omCONNECTED) || (mMode == omTRACKING)) && !ledgerChange)
    {
        // check if the ledger is good enough to go to omFULL
        // Note: Do not go to omFULL if we don't have the previous ledger
        // check if the ledger is bad enough to go to omCONNECTED -- TODO
        if (getApp().getOPs ().getNetworkTimeNC () < m_ledgerMaster.getCurrentLedger ()->getCloseTimeNC ())
            setMode (omFULL);
    }

    if ((!mConsensus) && (mMode != omDISCONNECTED))
        beginConsensus (networkClosed, m_ledgerMaster.getCurrentLedger ());
}

bool NetworkOPsImp::checkLastClosedLedger (const Overlay::PeerSequence& peerList, uint256& networkClosed)
{
    // Returns true if there's an *abnormal* ledger issue, normal changing in TRACKING mode should return false
    // Do we have sufficient validations for our last closed ledger? Or do sufficient nodes
    // agree? And do we have no better ledger available?
    // If so, we are either tracking or full.

    m_journal.trace << "NetworkOPsImp::checkLastClosedLedger";

    Ledger::pointer ourClosed = m_ledgerMaster.getClosedLedger ();

    if (!ourClosed)
        return false;

    uint256 closedLedger = ourClosed->getHash ();
    uint256 prevClosedLedger = ourClosed->getParentHash ();
    m_journal.trace << "OurClosed:  " << closedLedger;
    m_journal.trace << "PrevClosed: " << prevClosedLedger;

    ripple::unordered_map<uint256, ValidationCount> ledgers;
    {
        ripple::unordered_map<uint256, currentValidationCount> current =
            getApp().getValidations ().getCurrentValidations (closedLedger, prevClosedLedger);
        typedef std::map<uint256, currentValidationCount>::value_type u256_cvc_pair;
        BOOST_FOREACH (const u256_cvc_pair & it, current)
        {
            ValidationCount& vc = ledgers[it.first];
            vc.trustedValidations += it.second.first;

            if (it.second.second > vc.highValidation)
                vc.highValidation = it.second.second;
        }
    }

    ValidationCount& ourVC = ledgers[closedLedger];

    if (mMode >= omTRACKING)
    {
        ++ourVC.nodesUsing;
        uint160 ourAddress = getApp().getLocalCredentials ().getNodePublic ().getNodeID ();

        if (ourAddress > ourVC.highNodeUsing)
            ourVC.highNodeUsing = ourAddress;
    }

    BOOST_FOREACH (Peer::ptr const& peer, peerList)
    {
        uint256 peerLedger = peer->getClosedLedgerHash ();

        if (peerLedger.isNonZero ())
        {
            try
            {
                ValidationCount& vc = ledgers[peerLedger];

                if ((vc.nodesUsing == 0) || (peer->getNodePublic ().getNodeID () > vc.highNodeUsing))
                    vc.highNodeUsing = peer->getNodePublic ().getNodeID ();

                ++vc.nodesUsing;
	    }
	    catch (...)
	    {
	        // Peer is likely not connected anymore
	    }
        }
    }

    ValidationCount bestVC = ledgers[closedLedger];

    // 3) Is there a network ledger we'd like to switch to? If so, do we have it?
    bool switchLedgers = false;

    for (ripple::unordered_map<uint256, ValidationCount>::iterator it = ledgers.begin (), end = ledgers.end ();
            it != end; ++it)
    {
        m_journal.debug << "L: " << it->first << " t=" << it->second.trustedValidations <<
                                       ", n=" << it->second.nodesUsing;

        // Temporary logging to make sure tiebreaking isn't broken
        if (it->second.trustedValidations > 0)
            m_journal.trace << "  TieBreakTV: " << it->second.highValidation;
        else
        {
            if (it->second.nodesUsing > 0)
                m_journal.trace << "  TieBreakNU: " << it->second.highNodeUsing;
        }

        if (it->second > bestVC)
        {
            bestVC = it->second;
            closedLedger = it->first;
            switchLedgers = true;
        }
    }

    if (switchLedgers && (closedLedger == prevClosedLedger))
    {
        // don't switch to our own previous ledger
        m_journal.info << "We won't switch to our own previous ledger";
        networkClosed = ourClosed->getHash ();
        switchLedgers = false;
    }
    else
        networkClosed = closedLedger;

    if (!switchLedgers)
    {
        if (mAcquiringLedger)
        {
            mAcquiringLedger->abort ();
            getApp().getInboundLedgers ().dropLedger (mAcquiringLedger->getHash ());
            mAcquiringLedger.reset ();
        }

        return false;
    }

    m_journal.warning << "We are not running on the consensus ledger";
    m_journal.info << "Our LCL: " << ourClosed->getJson (0);
    m_journal.info << "Net LCL " << closedLedger;

    if ((mMode == omTRACKING) || (mMode == omFULL))
        setMode (omCONNECTED);

    Ledger::pointer consensus = m_ledgerMaster.getLedgerByHash (closedLedger);

    if (!consensus)
    {
        m_journal.info << "Acquiring consensus ledger " << closedLedger;

        if (!mAcquiringLedger || (mAcquiringLedger->getHash () != closedLedger))
            mAcquiringLedger = getApp().getInboundLedgers ().findCreate (closedLedger, 0, InboundLedger::fcCONSENSUS);

        if (!mAcquiringLedger || mAcquiringLedger->isFailed ())
        {
            getApp().getInboundLedgers ().dropLedger (closedLedger);
            m_journal.error << "Network ledger cannot be acquired";
            return true;
        }

        if (!mAcquiringLedger->isComplete ())
            return true;

        clearNeedNetworkLedger ();
        consensus = mAcquiringLedger->getLedger ();
    }

    // FIXME: If this rewinds the ledger sequence, or has the same sequence, we should update the status on
    // any stored transactions in the invalidated ledgers.
    switchLastClosedLedger (consensus, false);

    return true;
}

void NetworkOPsImp::switchLastClosedLedger (Ledger::pointer newLedger, bool duringConsensus)
{
    // set the newledger as our last closed ledger -- this is abnormal code

    if (duringConsensus)
        m_journal.error << "JUMPdc last closed ledger to " << newLedger->getHash ();
    else
        m_journal.error << "JUMP last closed ledger to " << newLedger->getHash ();

    clearNeedNetworkLedger ();
    newLedger->setClosed ();
    Ledger::pointer openLedger = std::make_shared<Ledger> (false, std::ref (*newLedger));
    m_ledgerMaster.switchLedgers (newLedger, openLedger);

    protocol::TMStatusChange s;
    s.set_newevent (protocol::neSWITCHED_LEDGER);
    s.set_ledgerseq (newLedger->getLedgerSeq ());
    s.set_networktime (getApp().getOPs ().getNetworkTimeNC ());
    uint256 hash = newLedger->getParentHash ();
    s.set_ledgerhashprevious (hash.begin (), hash.size ());
    hash = newLedger->getHash ();
    s.set_ledgerhash (hash.begin (), hash.size ());

    getApp ().overlay ().foreach (send_always (
        std::make_shared<Message> (s, protocol::mtSTATUS_CHANGE)));
}

int NetworkOPsImp::beginConsensus (uint256 const& networkClosed, Ledger::pointer closingLedger)
{
    m_journal.info << "Consensus time for ledger " << closingLedger->getLedgerSeq ();
    m_journal.info << " LCL is " << closingLedger->getParentHash ();

    Ledger::pointer prevLedger = m_ledgerMaster.getLedgerByHash (closingLedger->getParentHash ());

    if (!prevLedger)
    {
        // this shouldn't happen unless we jump ledgers
        if (mMode == omFULL)
        {
            m_journal.warning << "Don't have LCL, going to tracking";
            setMode (omTRACKING);
        }

        return 3;
    }

    assert (prevLedger->getHash () == closingLedger->getParentHash ());
    assert (closingLedger->getParentHash () == m_ledgerMaster.getClosedLedger ()->getHash ());

    // Create a consensus object to get consensus on this ledger
    assert (!mConsensus);
    prevLedger->setImmutable ();

    mConsensus = make_LedgerConsensus (m_clock, *m_localTX, networkClosed,
        prevLedger, m_ledgerMaster.getCurrentLedger ()->getCloseTimeNC (),
            *m_feeVote);

    m_journal.debug << "Initiating consensus engine";
    return mConsensus->startup ();
}

bool NetworkOPsImp::haveConsensusObject ()
{
    if (mConsensus != nullptr)
        return true;

    if ((mMode == omFULL) || (mMode == omTRACKING))
    {
        tryStartConsensus ();
    }
    else
    {
        // we need to get into the consensus process
        uint256 networkClosed;
        Overlay::PeerSequence peerList = getApp().overlay ().getActivePeers ();
        bool ledgerChange = checkLastClosedLedger (peerList, networkClosed);

        if (!ledgerChange)
        {
            m_journal.info << "Beginning consensus due to peer action";
            if ( ((mMode == omTRACKING) || (mMode == omSYNCING)) &&
                 (getPreviousProposers() >= m_ledgerMaster.getMinValidations()) )
                setMode (omFULL);
            beginConsensus (networkClosed, m_ledgerMaster.getCurrentLedger ());
        }
    }

    return mConsensus != nullptr;
}

uint256 NetworkOPsImp::getConsensusLCL ()
{
    if (!haveConsensusObject ())
        return uint256 ();

    return mConsensus->getLCL ();
}

void NetworkOPsImp::processTrustedProposal (LedgerProposal::pointer proposal,
        std::shared_ptr<protocol::TMProposeSet> set, RippleAddress nodePublic, uint256 checkLedger, bool sigGood)
{
    {
        Application::ScopedLockType lock (getApp().getMasterLock ());

        bool relay = true;

        if (!haveConsensusObject ())
        {
            m_journal.info << "Received proposal outside consensus window";

            if (mMode == omFULL)
                relay = false;
        }
        else
        {
            storeProposal (proposal, nodePublic);

            uint256 consensusLCL = mConsensus->getLCL ();

            if (!set->has_previousledger () && (checkLedger != consensusLCL))
            {
                m_journal.warning << "Have to re-check proposal signature due to consensus view change";
                assert (proposal->hasSignature ());
                proposal->setPrevLedger (consensusLCL);

                if (proposal->checkSign ())
                    sigGood = true;
            }

            if (sigGood && (consensusLCL == proposal->getPrevLedger ()))
            {
                relay = mConsensus->peerPosition (proposal);
                m_journal.trace << "Proposal processing finished, relay=" << relay;
            }
        }

        if (relay)
        {
            std::set<Peer::ShortId> peers;
            if (getApp().getHashRouter ().swapSet (
                proposal->getSuppressionID (), peers, SF_RELAYED))
	    {
                getApp ().overlay ().foreach (send_if_not (
                    std::make_shared<Message> (*set, protocol::mtPROPOSE_LEDGER),
                    peer_in_set(peers)));
	    }
        }
        else
        {
            m_journal.info << "Not relaying trusted proposal";
        }
    }
}

// Must be called while holding the master lock
SHAMap::pointer NetworkOPsImp::getTXMap (uint256 const& hash)
{
    std::map<uint256, std::pair<int, SHAMap::pointer> >::iterator it = mRecentPositions.find (hash);

    if (it != mRecentPositions.end ())
        return it->second.second;

    if (!haveConsensusObject ())
        return SHAMap::pointer ();

    return mConsensus->getTransactionTree (hash, false);
}

// Must be called while holding the master lock
void NetworkOPsImp::takePosition (int seq, SHAMap::ref position)
{
    mRecentPositions[position->getHash ()] = std::make_pair (seq, position);

    if (mRecentPositions.size () > 4)
    {
        std::map<uint256, std::pair<int, SHAMap::pointer> >::iterator it = mRecentPositions.begin ();

        while (it != mRecentPositions.end ())
        {
            if (it->second.first < (seq - 2))
            {
                mRecentPositions.erase (it);
                return;
            }

            ++it;
        }
    }
}

// Call with the master lock for now
SHAMapAddNode NetworkOPsImp::gotTXData (const std::shared_ptr<Peer>& peer, uint256 const& hash,
                                     const std::list<SHAMapNode>& nodeIDs, const std::list< Blob >& nodeData)
{

    if (!mConsensus)
    {
        m_journal.warning << "Got TX data with no consensus object";
        return SHAMapAddNode ();
    }

    return mConsensus->peerGaveNodes (peer, hash, nodeIDs, nodeData);
}

bool NetworkOPsImp::hasTXSet (const std::shared_ptr<Peer>& peer, uint256 const& set, protocol::TxSetStatus status)
{
    if (mConsensus == nullptr)
    {
        m_journal.info << "Peer has TX set, not during consensus";
        return false;
    }

    return mConsensus->peerHasSet (peer, set, status);
}

bool NetworkOPsImp::stillNeedTXSet (uint256 const& hash)
{
    if (!mConsensus)
        return false;

    return mConsensus->stillNeedTXSet (hash);
}

void NetworkOPsImp::mapComplete (uint256 const& hash, SHAMap::ref map)
{
    if (haveConsensusObject ())
        mConsensus->mapComplete (hash, map, true);
}

void NetworkOPsImp::endConsensus (bool correctLCL)
{
    uint256 deadLedger = m_ledgerMaster.getClosedLedger ()->getParentHash ();

    std::vector <Peer::ptr> peerList = getApp().overlay ().getActivePeers ();

    BOOST_FOREACH (Peer::ptr const& it, peerList)
    {
        if (it && (it->getClosedLedgerHash () == deadLedger))
        {
            m_journal.trace << "Killing obsolete peer status";
            it->cycleStatus ();
        }
    }

    mConsensus = std::shared_ptr<LedgerConsensus> ();
}

void NetworkOPsImp::consensusViewChange ()
{
    if ((mMode == omFULL) || (mMode == omTRACKING))
        setMode (omCONNECTED);
}

void NetworkOPsImp::pubServer ()
{
    // VFALCO TODO Don't hold the lock across calls to send...make a copy of the
    //             list into a local array while holding the lock then release the
    //             lock and call send on everyone.
    //
    ScopedLockType sl (mLock);

    if (!mSubServer.empty ())
    {
        Json::Value jvObj (Json::objectValue);

        jvObj [jss::type]          = "serverStatus";
        jvObj [jss::server_status] = strOperatingMode ();
        jvObj [jss::load_base]     = (mLastLoadBase = getApp().getFeeTrack ().getLoadBase ());
        jvObj [jss::load_factor]   = (mLastLoadFactor = getApp().getFeeTrack ().getLoadFactor ());

        Json::FastWriter w;
        std::string sObj = w.write (jvObj);

        NetworkOPsImp::SubMapType::const_iterator it = mSubServer.begin ();

        while (it != mSubServer.end ())
        {
            InfoSub::pointer p = it->second.lock ();

            // VFALCO TODO research the possibility of using thread queues and linearizing
            //             the deletion of subscribers with the sending of JSON data.
            if (p)
            {
                p->send (jvObj, sObj, true);

                ++it;
            }
            else
            {
                it = mSubServer.erase (it);
            }
        }
    }
}

void NetworkOPsImp::setMode (OperatingMode om)
{

    if (om == omCONNECTED)
    {
        if (getApp().getLedgerMaster ().getValidatedLedgerAge () < 60)
            om = omSYNCING;
    }
    else if (om == omSYNCING)
    {
        if (getApp().getLedgerMaster ().getValidatedLedgerAge () >= 60)
            om = omCONNECTED;
    }

    if ((om > omTRACKING) && m_amendmentBlocked)
        om = omTRACKING;

    if (mMode == om)
        return;

    if ((om >= omCONNECTED) && (mMode == omDISCONNECTED))
        mConnectTime = boost::posix_time::second_clock::universal_time ();

    mMode = om;

    Log ((om < mMode) ? lsWARNING : lsINFO) << "STATE->" << strOperatingMode ();
    pubServer ();
}


std::string
NetworkOPsImp::transactionsSQL (std::string selection, const RippleAddress& account,
                             std::int32_t minLedger, std::int32_t maxLedger, bool descending,
                             std::uint32_t offset, int limit,
                             bool binary, bool count, bool bAdmin)
{
    std::uint32_t NONBINARY_PAGE_LENGTH = 200;
    std::uint32_t BINARY_PAGE_LENGTH = 500;

    std::uint32_t numberOfResults;

    if (count)
        numberOfResults = 1000000000;
    else if (limit < 0)
        numberOfResults = binary ? BINARY_PAGE_LENGTH : NONBINARY_PAGE_LENGTH;
    else if (!bAdmin)
        numberOfResults = std::min (binary ? BINARY_PAGE_LENGTH
                                           : NONBINARY_PAGE_LENGTH, static_cast<std::uint32_t> (limit));
    else
        numberOfResults = limit;

    std::string maxClause = "";
    std::string minClause = "";

    if (maxLedger != -1)
        maxClause = boost::str (boost::format ("AND AccountTransactions.LedgerSeq <= '%u'") % maxLedger);

    if (minLedger != -1)
        minClause = boost::str (boost::format ("AND AccountTransactions.LedgerSeq >= '%u'") % minLedger);

    std::string sql;

    if (count)
        sql =
            boost::str (boost::format ("SELECT %s FROM AccountTransactions WHERE Account = '%s' %s %s LIMIT %u, %u;")
		    % selection
		    % account.humanAccountID ()
		    % maxClause
		    % minClause
		    % beast::lexicalCastThrow <std::string> (offset)
		    % beast::lexicalCastThrow <std::string> (numberOfResults)
		);
    else
        sql =
            boost::str (boost::format ("SELECT %s FROM "
                                       "AccountTransactions INNER JOIN Transactions ON Transactions.TransID = AccountTransactions.TransID "
                                       "WHERE Account = '%s' %s %s "
                                       "ORDER BY AccountTransactions.LedgerSeq %s, AccountTransactions.TxnSeq %s, AccountTransactions.TransID %s "
                                       "LIMIT %u, %u;")
                    % selection
                    % account.humanAccountID ()
                    % maxClause
                    % minClause
                    % (descending ? "DESC" : "ASC")
                    % (descending ? "DESC" : "ASC")
                    % (descending ? "DESC" : "ASC")
                    % beast::lexicalCastThrow <std::string> (offset)
                    % beast::lexicalCastThrow <std::string> (numberOfResults)
                   );
    m_journal.trace << "txSQL query: " << sql;
    return sql;
}

std::vector< std::pair<Transaction::pointer, TransactionMetaSet::pointer> >
NetworkOPsImp::getAccountTxs (const RippleAddress& account, std::int32_t minLedger,
                              std::int32_t maxLedger, bool descending, std::uint32_t offset,
                              int limit, bool bAdmin)
{
    // can be called with no locks
    std::vector< std::pair<Transaction::pointer, TransactionMetaSet::pointer> > ret;

    std::string sql = NetworkOPsImp::transactionsSQL ("AccountTransactions.LedgerSeq,Status,RawTxn,TxnMeta", account,
                      minLedger, maxLedger, descending, offset, limit, false, false, bAdmin);

    {
        Database* db = getApp().getTxnDB ()->getDB ();
        DeprecatedScopedLock sl (getApp().getTxnDB ()->getDBLock ());

        SQL_FOREACH (db, sql)
        {
            Transaction::pointer txn = Transaction::transactionFromSQL (db, false);

            Serializer rawMeta;
            int metaSize = 2048;
            rawMeta.resize (metaSize);
            metaSize = db->getBinary ("TxnMeta", &*rawMeta.begin (), rawMeta.getLength ());

            if (metaSize > rawMeta.getLength ())
            {
                rawMeta.resize (metaSize);
                db->getBinary ("TxnMeta", &*rawMeta.begin (), rawMeta.getLength ());
            }
            else
                rawMeta.resize (metaSize);

            if (rawMeta.getLength() == 0)
            { // Work around a bug that could leave the metadata missing
                std::uint32_t seq = static_cast<std::uint32_t>(db->getBigInt("LedgerSeq"));
                m_journal.warning << "Recovering ledger " << seq << ", txn " << txn->getID();
                Ledger::pointer ledger = getLedgerBySeq(seq);
                if (ledger)
                    ledger->pendSaveValidated(false, false);
            }

            TransactionMetaSet::pointer meta = std::make_shared<TransactionMetaSet> (txn->getID (), txn->getLedger (), rawMeta.getData ());

            // VFALCO NOTE Use emplace instead.
            ret.push_back (std::pair<Transaction::ref, TransactionMetaSet::ref> (txn, meta));
        }
    }

    return ret;
}

std::vector<NetworkOPsImp::txnMetaLedgerType> NetworkOPsImp::getAccountTxsB (
    const RippleAddress& account, std::int32_t minLedger, std::int32_t maxLedger, bool descending,
    std::uint32_t offset, int limit, bool bAdmin)
{
    // can be called with no locks
    std::vector< txnMetaLedgerType> ret;

    std::string sql = NetworkOPsImp::transactionsSQL ("AccountTransactions.LedgerSeq,Status,RawTxn,TxnMeta", account,
                      minLedger, maxLedger, descending, offset, limit, true/*binary*/, false, bAdmin);

    {
        Database* db = getApp().getTxnDB ()->getDB ();
        DeprecatedScopedLock sl (getApp().getTxnDB ()->getDBLock ());

        SQL_FOREACH (db, sql)
        {
            int txnSize = 2048;
            Blob rawTxn (txnSize);
            txnSize = db->getBinary ("RawTxn", &rawTxn[0], rawTxn.size ());

            if (txnSize > rawTxn.size ())
            {
                rawTxn.resize (txnSize);
                db->getBinary ("RawTxn", &*rawTxn.begin (), rawTxn.size ());
            }
            else
                rawTxn.resize (txnSize);

            int metaSize = 2048;
            Blob rawMeta (metaSize);
            metaSize = db->getBinary ("TxnMeta", &rawMeta[0], rawMeta.size ());

            if (metaSize > rawMeta.size ())
            {
                rawMeta.resize (metaSize);
                db->getBinary ("TxnMeta", &*rawMeta.begin (), rawMeta.size ());
            }
            else
                rawMeta.resize (metaSize);

            // VFALCO TODO Change the container's type to be std::tuple so
            //             we can use std::forward_as_tuple here
            //
            ret.push_back (std::make_tuple (
                strHex (rawTxn), strHex (rawMeta), db->getInt ("LedgerSeq")));
        }
    }

    return ret;
}


std::vector< std::pair<Transaction::pointer, TransactionMetaSet::pointer> >
NetworkOPsImp::getTxsAccount (const RippleAddress& account, std::int32_t minLedger,
                              std::int32_t maxLedger, bool forward, Json::Value& token,
                              int limit, bool bAdmin)
{
    std::vector< std::pair<Transaction::pointer, TransactionMetaSet::pointer> > ret;

    std::uint32_t NONBINARY_PAGE_LENGTH = 200;
    std::uint32_t EXTRA_LENGTH = 20;

    bool foundResume = token.isNull() || !token.isObject();

    std::uint32_t numberOfResults, queryLimit;
    if (limit <= 0)
        numberOfResults = NONBINARY_PAGE_LENGTH;
    else if (!bAdmin && (limit > NONBINARY_PAGE_LENGTH))
        numberOfResults = NONBINARY_PAGE_LENGTH;
    else
        numberOfResults = limit;
    queryLimit = numberOfResults + 1 + (foundResume ? 0 : EXTRA_LENGTH);

    std::uint32_t findLedger = 0, findSeq = 0;
    if (!foundResume)
    {
        try
        {
            if (!token.isMember(jss::ledger) || !token.isMember(jss::seq))
                return ret;
            findLedger = token[jss::ledger].asInt();
            findSeq = token[jss::seq].asInt();
        }
        catch (...)
        {
            return ret;
        }
    }

    // ST NOTE We're using the token reference both for passing inputs and
    //         outputs, so we need to clear it in between.
    token = Json::nullValue;

    std::string sql = boost::str (boost::format
        ("SELECT AccountTransactions.LedgerSeq,AccountTransactions.TxnSeq,Status,RawTxn,TxnMeta "
         "FROM AccountTransactions INNER JOIN Transactions ON Transactions.TransID = AccountTransactions.TransID "
         "WHERE AccountTransactions.Account = '%s' AND AccountTransactions.LedgerSeq BETWEEN '%u' AND '%u' "
         "ORDER BY AccountTransactions.LedgerSeq %s, AccountTransactions.TxnSeq %s, AccountTransactions.TransID %s "
         "LIMIT %u;")
             % account.humanAccountID()
             % ((forward && (findLedger != 0)) ? findLedger : minLedger)
             % ((!forward && (findLedger != 0)) ? findLedger: maxLedger)
             % (forward ? "ASC" : "DESC")
             % (forward ? "ASC" : "DESC")
             % (forward ? "ASC" : "DESC")
             % queryLimit);
    {
        Database* db = getApp().getTxnDB ()->getDB ();
        DeprecatedScopedLock sl (getApp().getTxnDB ()->getDBLock ());

        SQL_FOREACH (db, sql)
        {
            if (!foundResume)
            {
                if ((findLedger == db->getInt("LedgerSeq")) && (findSeq == db->getInt("TxnSeq")))
                    foundResume = true;
            }
            else if (numberOfResults == 0)
            {
                token = Json::objectValue;
                token[jss::ledger] = db->getInt("LedgerSeq");
                token[jss::seq] = db->getInt("TxnSeq");
                break;
            }

            if (foundResume)
            {
                Transaction::pointer txn = Transaction::transactionFromSQL (db, false);

                Serializer rawMeta;
                int metaSize = 2048;
                rawMeta.resize (metaSize);
                metaSize = db->getBinary ("TxnMeta", &*rawMeta.begin (), rawMeta.getLength ());

                if (metaSize > rawMeta.getLength ())
                {
                    rawMeta.resize (metaSize);
                    db->getBinary ("TxnMeta", &*rawMeta.begin (), rawMeta.getLength ());
                }
                else
                    rawMeta.resize (metaSize);

                if (rawMeta.getLength() == 0)
                { // Work around a bug that could leave the metadata missing
                    std::uint32_t seq = static_cast<std::uint32_t>(db->getBigInt("LedgerSeq"));
                    m_journal.warning << "Recovering ledger " << seq << ", txn " << txn->getID();
                    Ledger::pointer ledger = getLedgerBySeq(seq);
                    if (ledger)
                        ledger->pendSaveValidated(false, false);
                }

                --numberOfResults;
                TransactionMetaSet::pointer meta = std::make_shared<TransactionMetaSet> (txn->getID (), txn->getLedger (), rawMeta.getData ());

                ret.push_back (std::pair<Transaction::ref, TransactionMetaSet::ref> (txn, meta));
            }
        }
    }

    return ret;
}

std::vector<NetworkOPsImp::txnMetaLedgerType>
NetworkOPsImp::getTxsAccountB (const RippleAddress& account, std::int32_t minLedger,
                               std::int32_t maxLedger,  bool forward, Json::Value& token,
                               int limit, bool bAdmin)
{
    std::vector<txnMetaLedgerType> ret;

    std::uint32_t BINARY_PAGE_LENGTH = 500;
    std::uint32_t EXTRA_LENGTH = 20;

    bool foundResume = token.isNull() || !token.isObject();

    std::uint32_t numberOfResults, queryLimit;
    if (limit <= 0)
        numberOfResults = BINARY_PAGE_LENGTH;
    else if (!bAdmin && (limit > BINARY_PAGE_LENGTH))
        numberOfResults = BINARY_PAGE_LENGTH;
    else
        numberOfResults = limit;
    queryLimit = numberOfResults + 1 + (foundResume ? 0 : EXTRA_LENGTH);

    std::uint32_t findLedger = 0, findSeq = 0;
    if (!foundResume)
    {
        try
        {
            if (!token.isMember(jss::ledger) || !token.isMember(jss::seq))
                return ret;
            findLedger = token[jss::ledger].asInt();
            findSeq = token[jss::seq].asInt();
        }
        catch (...)
        {
            return ret;
        }
    }

    token = Json::nullValue;

    std::string sql = boost::str (boost::format
        ("SELECT AccountTransactions.LedgerSeq,AccountTransactions.TxnSeq,Status,RawTxn,TxnMeta "
         "FROM AccountTransactions INNER JOIN Transactions ON Transactions.TransID = AccountTransactions.TransID "
         "WHERE AccountTransactions.Account = '%s' AND AccountTransactions.LedgerSeq BETWEEN '%u' AND '%u' "
         "ORDER BY AccountTransactions.LedgerSeq %s, AccountTransactions.TxnSeq %s, AccountTransactions.TransID %s "
         "LIMIT %u;")
             % account.humanAccountID()
             % ((forward && (findLedger != 0)) ? findLedger : minLedger)
             % ((!forward && (findLedger != 0)) ? findLedger: maxLedger)
             % (forward ? "ASC" : "DESC")
             % (forward ? "ASC" : "DESC")
             % (forward ? "ASC" : "DESC")
             % queryLimit);
    {
        Database* db = getApp().getTxnDB ()->getDB ();
        DeprecatedScopedLock sl (getApp().getTxnDB ()->getDBLock ());

        SQL_FOREACH (db, sql)
        {
            if (!foundResume)
            {
                if ((findLedger == db->getInt("LedgerSeq")) && (findSeq == db->getInt("TxnSeq")))
                    foundResume = true;
            }
            else if (numberOfResults == 0)
            {
                token = Json::objectValue;
                token[jss::ledger] = db->getInt("LedgerSeq");
                token[jss::seq] = db->getInt("TxnSeq");
                break;
            }

            if (foundResume)
            {
                int txnSize = 2048;
                Blob rawTxn (txnSize);
                txnSize = db->getBinary ("RawTxn", &rawTxn[0], rawTxn.size ());

                if (txnSize > rawTxn.size ())
                {
                    rawTxn.resize (txnSize);
                    db->getBinary ("RawTxn", &*rawTxn.begin (), rawTxn.size ());
                }
                else
                    rawTxn.resize (txnSize);

                int metaSize = 2048;
                Blob rawMeta (metaSize);
                metaSize = db->getBinary ("TxnMeta", &rawMeta[0], rawMeta.size ());

                if (metaSize > rawMeta.size ())
                {
                    rawMeta.resize (metaSize);
                    db->getBinary ("TxnMeta", &*rawMeta.begin (), rawMeta.size ());
                }
                else
                    rawMeta.resize (metaSize);

                ret.push_back (std::make_tuple (
                    strHex (rawTxn), strHex (rawMeta), db->getInt ("LedgerSeq")));
                --numberOfResults;
            }
        }
    }

    return ret;
}


std::vector<RippleAddress>
NetworkOPsImp::getLedgerAffectedAccounts (std::uint32_t ledgerSeq)
{
    std::vector<RippleAddress> accounts;
    std::string sql = str (boost::format
                           ("SELECT DISTINCT Account FROM AccountTransactions INDEXED BY AcctLgrIndex WHERE LedgerSeq = '%u';")
                           % ledgerSeq);
    RippleAddress acct;
    {
        Database* db = getApp().getTxnDB ()->getDB ();
        DeprecatedScopedLock sl (getApp().getTxnDB ()->getDBLock ());
        SQL_FOREACH (db, sql)
        {
            if (acct.setAccountID (db->getStrBinary ("Account")))
                accounts.push_back (acct);
        }
    }
    return accounts;
}

bool NetworkOPsImp::recvValidation (SerializedValidation::ref val, const std::string& source)
{
    m_journal.debug << "recvValidation " << val->getLedgerHash () << " from " << source;
    return getApp().getValidations ().addValidation (val, source);
}

Json::Value NetworkOPsImp::getConsensusInfo ()
{
    if (mConsensus)
        return mConsensus->getJson (true);

    Json::Value info = Json::objectValue;
    info[jss::consensus] = "none";
    return info;
}


Json::Value NetworkOPsImp::getServerInfo (bool human, bool admin)
{
    Json::Value info = Json::objectValue;

    // hostid: unique string describing the machine
    if (human)
    {
        if (! admin)
        {
            // For a non admin connection, hash the node ID into a single RFC1751 word
            Blob const& addr (getApp().getLocalCredentials ().getNodePublic ().getNodePublic ());
            info [jss::hostid] = RFC1751::getWordFromBlob (addr.data (), addr.size ());
        }
        else
        {
            // Only admins get the hostname for security reasons
            info [jss::hostid] = beast::SystemStats::getComputerName();
        }
    }

    info [jss::build_version] = BuildInfo::getVersionString ();

    info [jss::server_state] = strOperatingMode ();

    if (mNeedNetworkLedger)
        info[jss::network_ledger] = jss::waiting;

    info[jss::validation_quorum] = m_ledgerMaster.getMinValidations ();

    info["io_latency_ms"] = static_cast<Json::UInt> (getApp().getIOLatency().count());

    if (admin)
    {
        if (getConfig ().VALIDATION_PUB.isValid ())
            info[jss::pubkey_validator] = getConfig ().VALIDATION_PUB.humanNodePublic ();
        else
            info[jss::pubkey_validator] = jss::none;
    }

    info[jss::pubkey_node] = getApp().getLocalCredentials ().getNodePublic ().humanNodePublic ();


    info[jss::complete_ledgers] = getApp().getLedgerMaster ().getCompleteLedgers ();

    if (m_amendmentBlocked)
        info[jss::amendment_blocked] = true;

    size_t fp = mFetchPack.getCacheSize ();

    if (fp != 0)
        info[jss::fetch_pack] = Json::UInt (fp);

    info[jss::peers] = Json::UInt (getApp ().overlay ().size ());

    Json::Value lastClose = Json::objectValue;
    lastClose[jss::proposers] = getApp().getOPs ().getPreviousProposers ();

    if (human)
        lastClose[jss::converge_time_s] = static_cast<double> (getApp().getOPs ().getPreviousConvergeTime ()) / 1000.0;
    else
        lastClose[jss::converge_time] = Json::Int (getApp().getOPs ().getPreviousConvergeTime ());

    info[jss::last_close] = lastClose;

    //  if (mConsensus)
    //      info[jss::consensus] = mConsensus->getJson();

    if (admin)
        info[jss::load] = getApp().getJobQueue ().getJson ();

    if (!human)
    {
        info[jss::load_base] = getApp().getFeeTrack ().getLoadBase ();
        info[jss::load_factor] = getApp().getFeeTrack ().getLoadFactor ();
    }
    else
    {
        info[jss::load_factor] =
            static_cast<double> (getApp().getFeeTrack ().getLoadFactor ()) / getApp().getFeeTrack ().getLoadBase ();
        if (admin)
        {
            std::uint32_t base = getApp().getFeeTrack().getLoadBase();
            std::uint32_t fee = getApp().getFeeTrack().getLocalFee();
            if (fee != base)
                info[jss::load_factor_local] =
                    static_cast<double> (fee) / base;
            fee = getApp().getFeeTrack ().getRemoteFee();
            if (fee != base)
                info[jss::load_factor_net] =
                    static_cast<double> (fee) / base;
            fee = getApp().getFeeTrack().getClusterFee();
            if (fee != base)
                info[jss::load_factor_cluster] =
                    static_cast<double> (fee) / base;
        }
    }

    bool valid = false;
    Ledger::pointer lpClosed    = getValidatedLedger ();

    if (lpClosed)
        valid = true;
    else
        lpClosed                = getClosedLedger ();

    if (lpClosed)
    {
        std::uint64_t baseFee = lpClosed->getBaseFee ();
        std::uint64_t baseRef = lpClosed->getReferenceFeeUnits ();
        Json::Value l (Json::objectValue);
        l[jss::seq]                = Json::UInt (lpClosed->getLedgerSeq ());
        l[jss::hash]               = to_string (lpClosed->getHash ());

        if (!human)
        {
            l[jss::base_fee]       = Json::Value::UInt (baseFee);
            l[jss::reserve_base]   = Json::Value::UInt (lpClosed->getReserve (0));
            l[jss::reserve_inc]    = Json::Value::UInt (lpClosed->getReserveInc ());
            l[jss::close_time]     = Json::Value::UInt (lpClosed->getCloseTimeNC ());
        }
        else
        {
            l[jss::base_fee_xrp]       = static_cast<double> (baseFee) / SYSTEM_CURRENCY_PARTS;
            l[jss::reserve_base_xrp]   =
                static_cast<double> (Json::UInt (lpClosed->getReserve (0) * baseFee / baseRef)) / SYSTEM_CURRENCY_PARTS;
            l[jss::reserve_inc_xrp]    =
                static_cast<double> (Json::UInt (lpClosed->getReserveInc () * baseFee / baseRef)) / SYSTEM_CURRENCY_PARTS;

            std::uint32_t closeTime = getCloseTimeNC ();
            std::uint32_t lCloseTime = lpClosed->getCloseTimeNC ();

            if (lCloseTime <= closeTime)
            {
                std::uint32_t age = closeTime - lCloseTime;

                if (age < 1000000)
                    l[jss::age]            = Json::UInt (age);
            }
        }

        if (valid)
            info[jss::validated_ledger] = l;
        else
            info[jss::closed_ledger] = l;

        Ledger::pointer lpPublished = getPublishedLedger ();
        if (!lpPublished)
            info[jss::published_ledger] = jss::none;
        else if (lpPublished->getLedgerSeq() != lpClosed->getLedgerSeq())
            info[jss::published_ledger] = lpPublished->getLedgerSeq();
    }

    return info;
}

void NetworkOPsImp::clearLedgerFetch ()
{
    getApp().getInboundLedgers().clearFailures();
}

Json::Value NetworkOPsImp::getLedgerFetchInfo ()
{
    return getApp().getInboundLedgers().getInfo();
}

//
// Monitoring: publisher side
//

Json::Value NetworkOPsImp::pubBootstrapAccountInfo (Ledger::ref lpAccepted, const RippleAddress& naAccountID)
{
    Json::Value         jvObj (Json::objectValue);

    jvObj["type"]           = "accountInfoBootstrap";
    jvObj["account"]        = naAccountID.humanAccountID ();
    jvObj["owner"]          = getOwnerInfo (lpAccepted, naAccountID);
    jvObj["ledger_index"]   = lpAccepted->getLedgerSeq ();
    jvObj["ledger_hash"]    = to_string (lpAccepted->getHash ());
    jvObj["ledger_time"]    = Json::Value::UInt (utFromSeconds (lpAccepted->getCloseTimeNC ()));

    return jvObj;
}

void NetworkOPsImp::pubProposedTransaction (Ledger::ref lpCurrent, SerializedTransaction::ref stTxn, TER terResult)
{
    Json::Value jvObj   = transJson (*stTxn, terResult, false, lpCurrent);

    {
        ScopedLockType sl (mLock);
        NetworkOPsImp::SubMapType::const_iterator it = mSubRTTransactions.begin ();

        while (it != mSubRTTransactions.end ())
        {
            InfoSub::pointer p = it->second.lock ();

            if (p)
            {
                p->send (jvObj, true);
                ++it;
            }
            else
                it = mSubRTTransactions.erase (it);
        }
    }
    AcceptedLedgerTx alt (stTxn, terResult);
    m_journal.trace << "pubProposed: " << alt.getJson ();
    pubAccountTransaction (lpCurrent, alt, false);
}

void NetworkOPsImp::pubLedger (Ledger::ref accepted)
{
    // Ledgers are published only when they acquire sufficient validations
    // Holes are filled across connection loss or other catastrophe

    AcceptedLedger::pointer alpAccepted = AcceptedLedger::makeAcceptedLedger (accepted);
    Ledger::ref lpAccepted = alpAccepted->getLedger ();

    {
        ScopedLockType sl (mLock);

        if (!mSubLedger.empty ())
        {
            Json::Value jvObj (Json::objectValue);

            jvObj[jss::type]           = jss::ledgerClosed;
            jvObj[jss::ledger_index]   = lpAccepted->getLedgerSeq ();
            jvObj[jss::ledger_hash]    = to_string (lpAccepted->getHash ());
            jvObj[jss::ledger_time]    = Json::Value::UInt (lpAccepted->getCloseTimeNC ());

            jvObj[jss::fee_ref]        = Json::UInt (lpAccepted->getReferenceFeeUnits ());
            jvObj[jss::fee_base]       = Json::UInt (lpAccepted->getBaseFee ());
            jvObj[jss::reserve_base]   = Json::UInt (lpAccepted->getReserve (0));
            jvObj[jss::reserve_inc]    = Json::UInt (lpAccepted->getReserveInc ());

            jvObj[jss::txn_count]      = Json::UInt (alpAccepted->getTxnCount ());

            if (mMode >= omSYNCING)
                jvObj[jss::validated_ledgers]  = getApp().getLedgerMaster ().getCompleteLedgers ();

            NetworkOPsImp::SubMapType::const_iterator it = mSubLedger.begin ();

            while (it != mSubLedger.end ())
            {
                InfoSub::pointer p = it->second.lock ();

                if (p)
                {
                    p->send (jvObj, true);
                    ++it;
                }
                else
                    it = mSubLedger.erase (it);
            }
        }
    }

    // Don't lock since pubAcceptedTransaction is locking.
    BOOST_FOREACH (const AcceptedLedger::value_type & vt, alpAccepted->getMap ())
    {
        m_journal.trace << "pubAccepted: " << vt.second->getJson ();
        pubValidatedTransaction (lpAccepted, *vt.second);
    }
}

void NetworkOPsImp::reportFeeChange ()
{
    if ((getApp().getFeeTrack ().getLoadBase () == mLastLoadBase) &&
            (getApp().getFeeTrack ().getLoadFactor () == mLastLoadFactor))
        return;

    getApp().getJobQueue ().addJob (jtCLIENT, "reportFeeChange->pubServer", std::bind (&NetworkOPsImp::pubServer, this));
}

Json::Value NetworkOPsImp::transJson (const SerializedTransaction& stTxn, TER terResult, bool bValidated,
                                   Ledger::ref lpCurrent)
{
    // This routine should only be used to publish accepted or validated transactions
    Json::Value jvObj (Json::objectValue);
    std::string sToken;
    std::string sHuman;

    transResultInfo (terResult, sToken, sHuman);

    jvObj[jss::type]           = jss::transaction;
    jvObj[jss::transaction]    = stTxn.getJson (0);

    if (bValidated)
    {
        jvObj[jss::ledger_index]           = lpCurrent->getLedgerSeq ();
        jvObj[jss::ledger_hash]            = to_string (lpCurrent->getHash ());
        jvObj[jss::transaction][jss::date]  = lpCurrent->getCloseTimeNC ();
        jvObj[jss::validated]              = true;

        // WRITEME: Put the account next seq here

    }
    else
    {
        jvObj[jss::validated]              = false;
        jvObj[jss::ledger_current_index]   = lpCurrent->getLedgerSeq ();
    }

    jvObj[jss::status]                 = bValidated ? jss::closed : jss::proposed;
    jvObj[jss::engine_result]          = sToken;
    jvObj[jss::engine_result_code]     = terResult;
    jvObj[jss::engine_result_message]  = sHuman;

    return jvObj;
}

void NetworkOPsImp::pubValidatedTransaction (Ledger::ref alAccepted, const AcceptedLedgerTx& alTx)
{
    Json::Value jvObj   = transJson (*alTx.getTxn (), alTx.getResult (), true, alAccepted);
    jvObj[jss::meta] = alTx.getMeta ()->getJson (0);

    Json::FastWriter w;
    std::string sObj = w.write (jvObj);

    {
        ScopedLockType sl (mLock);

        NetworkOPsImp::SubMapType::const_iterator it = mSubTransactions.begin ();

        while (it != mSubTransactions.end ())
        {
            InfoSub::pointer p = it->second.lock ();

            if (p)
            {
                p->send (jvObj, sObj, true);
                ++it;
            }
            else
                it = mSubTransactions.erase (it);
        }

        it = mSubRTTransactions.begin ();

        while (it != mSubRTTransactions.end ())
        {
            InfoSub::pointer p = it->second.lock ();

            if (p)
            {
                p->send (jvObj, sObj, true);
                ++it;
            }
            else
                it = mSubRTTransactions.erase (it);
        }
    }
    getApp().getOrderBookDB ().processTxn (alAccepted, alTx, jvObj);
    pubAccountTransaction (alAccepted, alTx, true);
}

void NetworkOPsImp::pubAccountTransaction (Ledger::ref lpCurrent, const AcceptedLedgerTx& alTx, bool bAccepted)
{
    std::unordered_set<InfoSub::pointer>  notify;
    int                             iProposed   = 0;
    int                             iAccepted   = 0;

    {
        ScopedLockType sl (mLock);

        if (!bAccepted && mSubRTAccount.empty ()) return;

        if (!mSubAccount.empty () || (!mSubRTAccount.empty ()) )
        {
            BOOST_FOREACH (const RippleAddress & affectedAccount, alTx.getAffected ())
            {
                SubInfoMapIterator  simiIt  = mSubRTAccount.find (affectedAccount.getAccountID ());

                if (simiIt != mSubRTAccount.end ())
                {
                    NetworkOPsImp::SubMapType::const_iterator it = simiIt->second.begin ();

                    while (it != simiIt->second.end ())
                    {
                        InfoSub::pointer p = it->second.lock ();

                        if (p)
                        {
                            notify.insert (p);
                            ++it;
                            ++iProposed;
                        }
                        else
                            it = simiIt->second.erase (it);
                    }
                }

                if (bAccepted)
                {
                    simiIt  = mSubAccount.find (affectedAccount.getAccountID ());

                    if (simiIt != mSubAccount.end ())
                    {
                        NetworkOPsImp::SubMapType::const_iterator it = simiIt->second.begin ();

                        while (it != simiIt->second.end ())
                        {
                            InfoSub::pointer p = it->second.lock ();

                            if (p)
                            {
                                notify.insert (p);
                                ++it;
                                ++iAccepted;
                            }
                            else
                                it = simiIt->second.erase (it);
                        }
                    }
                }
            }
        }
    }
    m_journal.info << "pubAccountTransaction:" <<
        " iProposed=" << iProposed <<
        " iAccepted=" << iAccepted;

    if (!notify.empty ())
    {
        Json::Value jvObj = transJson (
            *alTx.getTxn (), alTx.getResult (), bAccepted, lpCurrent);

        if (alTx.isApplied ())
            jvObj[jss::meta] = alTx.getMeta ()->getJson (0);

        Json::FastWriter w;
        std::string sObj = w.write (jvObj);

        BOOST_FOREACH (InfoSub::ref isrListener, notify)
        {
            isrListener->send (jvObj, sObj, true);
        }
    }
}

//
// Monitoring
//

void NetworkOPsImp::subAccount (InfoSub::ref isrListener,
    const boost::unordered_set<RippleAddress>& vnaAccountIDs,
    std::uint32_t uLedgerIndex, bool rt)
{
    SubInfoMapType& subMap = rt ? mSubRTAccount : mSubAccount;

    // For the connection, monitor each account.
    BOOST_FOREACH (const RippleAddress & naAccountID, vnaAccountIDs)
    {
        m_journal.trace << "subAccount:"
            " account: " << naAccountID.humanAccountID ();

        isrListener->insertSubAccountInfo (naAccountID, uLedgerIndex);
    }

    ScopedLockType sl (mLock);

    BOOST_FOREACH (const RippleAddress & naAccountID, vnaAccountIDs)
    {
        SubInfoMapType::iterator    simIterator = subMap.find (naAccountID.getAccountID ());

        if (simIterator == subMap.end ())
        {
            // Not found, note that account has a new single listner.
            SubMapType  usisElement;
            usisElement[isrListener->getSeq ()] = isrListener;
            subMap.insert (simIterator, make_pair (naAccountID.getAccountID (), usisElement));
        }
        else
        {
            // Found, note that the account has another listener.
            simIterator->second[isrListener->getSeq ()] = isrListener;
        }
    }
}

void NetworkOPsImp::unsubAccount (std::uint64_t uSeq,
                                  const boost::unordered_set<RippleAddress>& vnaAccountIDs,
                                  bool rt)
{
    SubInfoMapType& subMap = rt ? mSubRTAccount : mSubAccount;

    // For the connection, unmonitor each account.
    // FIXME: Don't we need to unsub?
    // BOOST_FOREACH(const RippleAddress& naAccountID, vnaAccountIDs)
    // {
    //  isrListener->deleteSubAccountInfo(naAccountID);
    // }

    ScopedLockType sl (mLock);

    for (auto const& naAccountID : vnaAccountIDs)
    {
        auto simIterator = subMap.find (naAccountID.getAccountID ());

        if (simIterator != subMap.end ())
        {
            // Found
            simIterator->second.erase (uSeq);

            if (simIterator->second.empty ())
            {
                // Don't need hash entry.
                subMap.erase (simIterator);
            }
        }
    }
}

bool NetworkOPsImp::subBook (InfoSub::ref isrListener, RippleCurrency const& currencyPays, RippleCurrency const& currencyGets,
                          RippleIssuer const& issuerPays, RippleIssuer const& issuerGets)
{
    BookListeners::pointer listeners =
        getApp().getOrderBookDB ().makeBookListeners (currencyPays, currencyGets, issuerPays, issuerGets);

    assert (listeners);
    if (listeners)
        listeners->addSubscriber (isrListener);

    return true;
}

bool NetworkOPsImp::unsubBook (std::uint64_t uSeq,
                            RippleCurrency const& currencyPays, RippleCurrency const& currencyGets, RippleIssuer const& issuerPays, RippleIssuer const& issuerGets)
{
    BookListeners::pointer listeners =
        getApp().getOrderBookDB ().getBookListeners (currencyPays, currencyGets, issuerPays, issuerGets);

    if (listeners)
        listeners->removeSubscriber (uSeq);

    return true;
}

void NetworkOPsImp::newLCL (int proposers, int convergeTime, uint256 const& ledgerHash)
{
    assert (convergeTime);
    mLastCloseProposers = proposers;
    mLastCloseConvergeTime = convergeTime;
    mLastCloseHash = ledgerHash;
}

std::uint32_t NetworkOPsImp::acceptLedger ()
{
    // accept the current transaction tree, return the new ledger's sequence
    beginConsensus (m_ledgerMaster.getClosedLedger ()->getHash (), m_ledgerMaster.getCurrentLedger ());
    mConsensus->simulate ();
    return m_ledgerMaster.getCurrentLedger ()->getLedgerSeq ();
}

void NetworkOPsImp::storeProposal (LedgerProposal::ref proposal, const RippleAddress& peerPublic)
{
    std::list<LedgerProposal::pointer>& props = mStoredProposals[peerPublic.getNodeID ()];

    if (props.size () >= (unsigned) (mLastCloseProposers + 10))
        props.pop_front ();

    props.push_back (proposal);
}

#if 0
void NetworkOPsImp::subAccountChanges (InfoSub* isrListener, const uint256 uLedgerHash)
{
}

void NetworkOPsImp::unsubAccountChanges (InfoSub* isrListener)
{
}
#endif

// <-- bool: true=added, false=already there
bool NetworkOPsImp::subLedger (InfoSub::ref isrListener, Json::Value& jvResult)
{
    Ledger::pointer lpClosed    = getValidatedLedger ();

    if (lpClosed)
    {
        jvResult[jss::ledger_index]    = lpClosed->getLedgerSeq ();
        jvResult[jss::ledger_hash]     = to_string (lpClosed->getHash ());
        jvResult[jss::ledger_time]     = Json::Value::UInt (lpClosed->getCloseTimeNC ());
        jvResult[jss::fee_ref]         = Json::UInt (lpClosed->getReferenceFeeUnits ());
        jvResult[jss::fee_base]        = Json::UInt (lpClosed->getBaseFee ());
        jvResult[jss::reserve_base]    = Json::UInt (lpClosed->getReserve (0));
        jvResult[jss::reserve_inc]     = Json::UInt (lpClosed->getReserveInc ());
    }

    if ((mMode >= omSYNCING) && !isNeedNetworkLedger ())
        jvResult[jss::validated_ledgers]   = getApp().getLedgerMaster ().getCompleteLedgers ();

    ScopedLockType sl (mLock);
    return mSubLedger.emplace (isrListener->getSeq (), isrListener).second;
}

// <-- bool: true=erased, false=was not there
bool NetworkOPsImp::unsubLedger (std::uint64_t uSeq)
{
    ScopedLockType sl (mLock);
    return !!mSubLedger.erase (uSeq);
}

// <-- bool: true=added, false=already there
bool NetworkOPsImp::subServer (InfoSub::ref isrListener, Json::Value& jvResult)
{
    uint256         uRandom;

    if (getConfig ().RUN_STANDALONE)
        jvResult[jss::stand_alone] = getConfig ().RUN_STANDALONE;

    RandomNumbers::getInstance ().fillBytes (uRandom.begin (), uRandom.size ());

    jvResult[jss::random]          = to_string (uRandom);
    jvResult[jss::server_status]   = strOperatingMode ();
    jvResult[jss::load_base]       = getApp().getFeeTrack ().getLoadBase ();
    jvResult[jss::load_factor]     = getApp().getFeeTrack ().getLoadFactor ();

    ScopedLockType sl (mLock);
    return mSubServer.emplace (isrListener->getSeq (), isrListener).second;
}

// <-- bool: true=erased, false=was not there
bool NetworkOPsImp::unsubServer (std::uint64_t uSeq)
{
    ScopedLockType sl (mLock);
    return !!mSubServer.erase (uSeq);
}

// <-- bool: true=added, false=already there
bool NetworkOPsImp::subTransactions (InfoSub::ref isrListener)
{
    ScopedLockType sl (mLock);
    return mSubTransactions.emplace (isrListener->getSeq (), isrListener).second;
}

// <-- bool: true=erased, false=was not there
bool NetworkOPsImp::unsubTransactions (std::uint64_t uSeq)
{
    ScopedLockType sl (mLock);
    return !!mSubTransactions.erase (uSeq);
}

// <-- bool: true=added, false=already there
bool NetworkOPsImp::subRTTransactions (InfoSub::ref isrListener)
{
    ScopedLockType sl (mLock);
    return mSubRTTransactions.emplace (isrListener->getSeq (), isrListener).second;
}

// <-- bool: true=erased, false=was not there
bool NetworkOPsImp::unsubRTTransactions (std::uint64_t uSeq)
{
    ScopedLockType sl (mLock);
    return !!mSubRTTransactions.erase (uSeq);
}

InfoSub::pointer NetworkOPsImp::findRpcSub (const std::string& strUrl)
{
    ScopedLockType sl (mLock);

    subRpcMapType::iterator it = mRpcSubMap.find (strUrl);

    if (it != mRpcSubMap.end ())
        return it->second;

    return InfoSub::pointer ();
}

InfoSub::pointer NetworkOPsImp::addRpcSub (const std::string& strUrl, InfoSub::ref rspEntry)
{
    ScopedLockType sl (mLock);

    mRpcSubMap.emplace (strUrl, rspEntry);

    return rspEntry;
}

#ifndef USE_NEW_BOOK_PAGE
// NIKB FIXME this should be looked at. There's no reason why this shouldn't
//            work, but it demonstrated poor performance.
//
// FIXME : support iLimit.
void NetworkOPsImp::getBookPage (Ledger::pointer lpLedger, const uint160& uTakerPaysCurrencyID, const uint160& uTakerPaysIssuerID, const uint160& uTakerGetsCurrencyID, const uint160& uTakerGetsIssuerID, const uint160& uTakerID, const bool bProof, const unsigned int iLimit, const Json::Value& jvMarker, Json::Value& jvResult)
{ // CAUTION: This is the old get book page logic
    Json::Value&    jvOffers    = (jvResult[jss::offers] = Json::Value (Json::arrayValue));

    std::map<uint160, STAmount> umBalance;
    const uint256   uBookBase   = Ledger::getBookBase (uTakerPaysCurrencyID, uTakerPaysIssuerID, uTakerGetsCurrencyID, uTakerGetsIssuerID);
    const uint256   uBookEnd    = Ledger::getQualityNext (uBookBase);
    uint256         uTipIndex   = uBookBase;

    if (m_journal.trace)
    {
        m_journal.trace << "getBookPage:" <<
            " uTakerPaysCurrencyID=" <<
                STAmount::createHumanCurrency (uTakerPaysCurrencyID) <<
            " uTakerPaysIssuerID=" <<
                RippleAddress::createHumanAccountID (uTakerPaysIssuerID);
        m_journal.trace << "getBookPage:" <<
            " uTakerGetsCurrencyID=" <<
                STAmount::createHumanCurrency (uTakerGetsCurrencyID) <<
            " uTakerGetsIssuerID=" <<
                RippleAddress::createHumanAccountID (uTakerGetsIssuerID);
        m_journal.trace << "getBookPage: uBookBase=" << uBookBase;
        m_journal.trace << "getBookPage: uBookEnd=" << uBookEnd;
        m_journal.trace << "getBookPage: uTipIndex=" << uTipIndex;
    }

    LedgerEntrySet  lesActive (lpLedger, tapNONE, true);

    bool            bDone           = false;
    bool            bDirectAdvance  = true;

    SLE::pointer    sleOfferDir;
    uint256         offerIndex;
    unsigned int    uBookEntry;
    STAmount        saDirRate;

    unsigned int    iLeft           = iLimit;

    if ((iLeft == 0) || (iLeft > 300))
        iLeft = 300;

    std::uint32_t  uTransferRate   = lesActive.rippleTransferRate (uTakerGetsIssuerID);

    while (!bDone && (--iLeft > 0))
    {
        if (bDirectAdvance)
        {
            bDirectAdvance  = false;

            m_journal.trace << "getBookPage: bDirectAdvance";

            sleOfferDir     = lesActive.entryCache (ltDIR_NODE, lpLedger->getNextLedgerIndex (uTipIndex, uBookEnd));

            if (!sleOfferDir)
            {
                m_journal.trace << "getBookPage: bDone";
                bDone           = true;
            }
            else
            {
                uTipIndex       = sleOfferDir->getIndex ();
                saDirRate       = STAmount::setRate (Ledger::getQuality (uTipIndex));

                lesActive.dirFirst (uTipIndex, sleOfferDir, uBookEntry, offerIndex);

                m_journal.trace << "getBookPage:   uTipIndex=" << uTipIndex;
                m_journal.trace << "getBookPage: offerIndex=" << offerIndex;
            }
        }

        if (!bDone)
        {
            SLE::pointer    sleOffer        = lesActive.entryCache (ltOFFER, offerIndex);

            if (sleOffer)
            {
                const uint160   uOfferOwnerID   = sleOffer->getFieldAccount160 (sfAccount);
                const STAmount& saTakerGets     = sleOffer->getFieldAmount (sfTakerGets);
                const STAmount& saTakerPays     = sleOffer->getFieldAmount (sfTakerPays);
                STAmount        saOwnerFunds;

                if (uTakerGetsIssuerID == uOfferOwnerID)
                {
                    // If offer is selling issuer's own IOUs, it is fully funded.
                    saOwnerFunds    = saTakerGets;
                }
                else
                {
                    std::map<uint160, STAmount>::const_iterator umBalanceEntry  = umBalance.find (uOfferOwnerID);

                    if (umBalanceEntry != umBalance.end ())
                    {
                        // Found in running balance table.

                        saOwnerFunds    = umBalanceEntry->second;
                        // m_journal.info << boost::str(boost::format("getBookPage: saOwnerFunds=%s (cached)") % saOwnerFunds.getFullText());
                    }
                    else
                    {
                        // Did not find balance in table.

                        saOwnerFunds    = lesActive.accountHolds (uOfferOwnerID, uTakerGetsCurrencyID, uTakerGetsIssuerID);

                        // m_journal.info << boost::str(boost::format("getBookPage: saOwnerFunds=%s (new)") % saOwnerFunds.getFullText());
                        if (saOwnerFunds < zero)
                        {
                            // Treat negative funds as zero.

                            saOwnerFunds.clear ();
                        }
                    }
                }

                Json::Value jvOffer = sleOffer->getJson (0);

                STAmount    saTakerGetsFunded;
                STAmount    saOwnerFundsLimit;
                std::uint32_t uOfferRate;


                if (uTransferRate != QUALITY_ONE                // Have a tranfer fee.
                        && uTakerID != uTakerGetsIssuerID           // Not taking offers of own IOUs.
                        && uTakerGetsIssuerID != uOfferOwnerID)     // Offer owner not issuing ownfunds
                {
                    // Need to charge a transfer fee to offer owner.
                    uOfferRate          = uTransferRate;
                    saOwnerFundsLimit   = STAmount::divide (saOwnerFunds, STAmount (CURRENCY_ONE, ACCOUNT_ONE, uOfferRate, -9));
                }
                else
                {
                    uOfferRate          = QUALITY_ONE;
                    saOwnerFundsLimit   = saOwnerFunds;
                }

                if (saOwnerFundsLimit >= saTakerGets)
                {
                    // Sufficient funds no shenanigans.
                    saTakerGetsFunded   = saTakerGets;
                }
                else
                {
                    // m_journal.info << boost::str(boost::format("getBookPage:  saTakerGets=%s") % saTakerGets.getFullText());
                    // m_journal.info << boost::str(boost::format("getBookPage:  saTakerPays=%s") % saTakerPays.getFullText());
                    // m_journal.info << boost::str(boost::format("getBookPage: saOwnerFunds=%s") % saOwnerFunds.getFullText());
                    // m_journal.info << boost::str(boost::format("getBookPage:    saDirRate=%s") % saDirRate.getText());
                    // m_journal.info << boost::str(boost::format("getBookPage:     multiply=%s") % STAmount::multiply(saTakerGetsFunded, saDirRate).getFullText());
                    // m_journal.info << boost::str(boost::format("getBookPage:     multiply=%s") % STAmount::multiply(saTakerGetsFunded, saDirRate, saTakerPays).getFullText());

                    // Only provide, if not fully funded.

                    saTakerGetsFunded   = saOwnerFundsLimit;

                    saTakerGetsFunded.setJson (jvOffer[jss::taker_gets_funded]);
                    std::min (saTakerPays,
                        STAmount::multiply (saTakerGetsFunded, saDirRate, saTakerPays)).setJson
                            (jvOffer[jss::taker_pays_funded]);
                }

                STAmount saOwnerPays = (QUALITY_ONE == uOfferRate)
                    ? saTakerGetsFunded
                    : std::min (
                        saOwnerFunds,
                        STAmount::multiply (
                            saTakerGetsFunded,
                            STAmount (CURRENCY_ONE, ACCOUNT_ONE, uOfferRate, -9)));

                umBalance[uOfferOwnerID]    = saOwnerFunds - saOwnerPays;

                if (saOwnerFunds != zero || uOfferOwnerID == uTakerID)
                {
                    // Only provide funded offers and offers of the taker.
                    Json::Value& jvOf   = jvOffers.append (jvOffer);
                    jvOf[jss::quality]     = saDirRate.getText ();
                }
            }
            else
            {
                m_journal.warning << "Missing offer";
        }

            if (!lesActive.dirNext (uTipIndex, sleOfferDir, uBookEntry, offerIndex))
            {
                bDirectAdvance  = true;
            }
            else
            {
                m_journal.trace << "getBookPage: offerIndex=" << offerIndex;
            }
        }
    }

    //  jvResult["marker"]  = Json::Value(Json::arrayValue);
    //  jvResult["nodes"]   = Json::Value(Json::arrayValue);
}


#else

// This is the new code that uses the book iterators
// It has temporarily been disabled

// FIXME : support iLimit.
void NetworkOPsImp::getBookPage (Ledger::pointer lpLedger, const uint160& uTakerPaysCurrencyID, const uint160& uTakerPaysIssuerID, const uint160& uTakerGetsCurrencyID, const uint160& uTakerGetsIssuerID, const uint160& uTakerID, const bool bProof, const unsigned int iLimit, const Json::Value& jvMarker, Json::Value& jvResult)
{
    Json::Value&    jvOffers    = (jvResult[jss::offers] = Json::Value (Json::arrayValue));

    std::map<uint160, STAmount> umBalance;

    LedgerEntrySet  lesActive (lpLedger, tapNONE, true);
    OrderBookIterator obIterator (lesActive, uTakerPaysCurrencyID, uTakerPaysIssuerID, uTakerGetsCurrencyID, uTakerGetsIssuerID);

    unsigned int    iLeft           = iLimit;

    if ((iLeft == 0) || (iLeft > 300))
        iLeft = 300;

    std::uint32_t  uTransferRate   = lesActive.rippleTransferRate (uTakerGetsIssuerID);

    while ((--iLeft > 0) && obIterator.nextOffer ())
    {

        SLE::pointer    sleOffer        = obIterator.getCurrentOffer();
        if (sleOffer)
        {
            const uint160   uOfferOwnerID   = sleOffer->getFieldAccount160 (sfAccount);
            const STAmount& saTakerGets     = sleOffer->getFieldAmount (sfTakerGets);
            const STAmount& saTakerPays     = sleOffer->getFieldAmount (sfTakerPays);
            STAmount        saDirRate       = obIterator.getCurrentRate ();
            STAmount        saOwnerFunds;

            if (uTakerGetsIssuerID == uOfferOwnerID)
            {
                // If offer is selling issuer's own IOUs, it is fully funded.
                saOwnerFunds    = saTakerGets;
            }
            else
            {
                std::map<uint160, STAmount>::const_iterator umBalanceEntry  = umBalance.find (uOfferOwnerID);

                if (umBalanceEntry != umBalance.end ())
                {
                    // Found in running balance table.

                    saOwnerFunds    = umBalanceEntry->second;
                    // m_journal.info << boost::str(boost::format("getBookPage: saOwnerFunds=%s (cached)") % saOwnerFunds.getFullText());
                }
                else
                {
                    // Did not find balance in table.

                    saOwnerFunds    = lesActive.accountHolds (uOfferOwnerID, uTakerGetsCurrencyID, uTakerGetsIssuerID);

                    // m_journal.info << boost::str(boost::format("getBookPage: saOwnerFunds=%s (new)") % saOwnerFunds.getFullText());
                    if (saOwnerFunds.isNegative ())
                    {
                        // Treat negative funds as zero.

                        saOwnerFunds.zero ();
                    }
                }
            }

            Json::Value jvOffer = sleOffer->getJson (0);

            STAmount    saTakerGetsFunded;
            STAmount    saOwnerFundsLimit;
            std::uint32_t uOfferRate;


            if (uTransferRate != QUALITY_ONE                // Have a tranfer fee.
                    && uTakerID != uTakerGetsIssuerID           // Not taking offers of own IOUs.
                    && uTakerGetsIssuerID != uOfferOwnerID)     // Offer owner not issuing ownfunds
            {
                // Need to charge a transfer fee to offer owner.
                uOfferRate          = uTransferRate;
                saOwnerFundsLimit   = STAmount::divide (saOwnerFunds, STAmount (CURRENCY_ONE, ACCOUNT_ONE, uOfferRate, -9));
            }
            else
            {
                uOfferRate          = QUALITY_ONE;
                saOwnerFundsLimit   = saOwnerFunds;
            }

            if (saOwnerFundsLimit >= saTakerGets)
            {
                // Sufficient funds no shenanigans.
                saTakerGetsFunded   = saTakerGets;
            }
            else
            {
                // m_journal.info << boost::str(boost::format("getBookPage:  saTakerGets=%s") % saTakerGets.getFullText());
                // m_journal.info << boost::str(boost::format("getBookPage:  saTakerPays=%s") % saTakerPays.getFullText());
                // m_journal.info << boost::str(boost::format("getBookPage: saOwnerFunds=%s") % saOwnerFunds.getFullText());
                // m_journal.info << boost::str(boost::format("getBookPage:    saDirRate=%s") % saDirRate.getText());
                // m_journal.info << boost::str(boost::format("getBookPage:     multiply=%s") % STAmount::multiply(saTakerGetsFunded, saDirRate).getFullText());
                // m_journal.info << boost::str(boost::format("getBookPage:     multiply=%s") % STAmount::multiply(saTakerGetsFunded, saDirRate, saTakerPays).getFullText());

                // Only provide, if not fully funded.

                saTakerGetsFunded   = saOwnerFundsLimit;

                saTakerGetsFunded.setJson (jvOffer[jss::taker_gets_funded]);
                std::min (saTakerPays, STAmount::multiply (saTakerGetsFunded, saDirRate, saTakerPays)).setJson (jvOffer[jss::taker_pays_funded]);
            }

            STAmount    saOwnerPays     = (QUALITY_ONE == uOfferRate)
                                          ? saTakerGetsFunded
                                          : std::min (saOwnerFunds, STAmount::multiply (saTakerGetsFunded, STAmount (CURRENCY_ONE, ACCOUNT_ONE, uOfferRate, -9)));

            umBalance[uOfferOwnerID]    = saOwnerFunds - saOwnerPays;

            if (!saOwnerFunds.isZero () || uOfferOwnerID == uTakerID)
            {
                // Only provide funded offers and offers of the taker.
                Json::Value& jvOf   = jvOffers.append (jvOffer);
                jvOf[jss::quality]     = saDirRate.getText ();
            }

        }
    }

    //  jvResult["marker"]  = Json::Value(Json::arrayValue);
    //  jvResult["nodes"]   = Json::Value(Json::arrayValue);
}

#endif

static void fpAppender (protocol::TMGetObjectByHash* reply, std::uint32_t ledgerSeq,
                        const uint256& hash, const Blob& blob)
{
    protocol::TMIndexedObject& newObj = * (reply->add_objects ());
    newObj.set_ledgerseq (ledgerSeq);
    newObj.set_hash (hash.begin (), 256 / 8);
    newObj.set_data (&blob[0], blob.size ());
}

void NetworkOPsImp::makeFetchPack (Job&, std::weak_ptr<Peer> wPeer,
                                std::shared_ptr<protocol::TMGetObjectByHash> request,
                                uint256 haveLedgerHash, std::uint32_t uUptime)
{
    if (UptimeTimer::getInstance ().getElapsedSeconds () > (uUptime + 1))
    {
        m_journal.info << "Fetch pack request got stale";
        return;
    }

    if (getApp().getFeeTrack ().isLoadedLocal () ||
        (m_ledgerMaster.getValidatedLedgerAge() > 40))
    {
        m_journal.info << "Too busy to make fetch pack";
        return;
    }

    Peer::ptr peer = wPeer.lock ();

    if (!peer)
        return;

    Ledger::pointer haveLedger = getLedgerByHash (haveLedgerHash);

    if (!haveLedger)
    {
        m_journal.info << "Peer requests fetch pack for ledger we don't have: " << haveLedger;
        peer->charge (Resource::feeRequestNoReply);
        return;
    }

    if (!haveLedger->isClosed ())
    {
        m_journal.warning << "Peer requests fetch pack from open ledger: " << haveLedger;
        peer->charge (Resource::feeInvalidRequest);
        return;
    }

    if (haveLedger->getLedgerSeq() < m_ledgerMaster.getEarliestFetch())
    {
        m_journal.debug << "Peer requests fetch pack that is too early";
        peer->charge (Resource::feeInvalidRequest);
        return;
    }

    Ledger::pointer wantLedger = getLedgerByHash (haveLedger->getParentHash ());

    if (!wantLedger)
    {
        m_journal.info
            << "Peer requests fetch pack for ledger whose predecessor we don't have: "
            << haveLedger;
        peer->charge (Resource::feeRequestNoReply);
        return;
    }

    try
    {

        protocol::TMGetObjectByHash reply;
        reply.set_query (false);

        if (request->has_seq ())
            reply.set_seq (request->seq ());

        reply.set_ledgerhash (request->ledgerhash ());
        reply.set_type (protocol::TMGetObjectByHash::otFETCH_PACK);

        do
        {
            std::uint32_t lSeq = wantLedger->getLedgerSeq ();

            protocol::TMIndexedObject& newObj = *reply.add_objects ();
            newObj.set_hash (wantLedger->getHash ().begin (), 256 / 8);
            Serializer s (256);
            s.add32 (HashPrefix::ledgerMaster);
            wantLedger->addRaw (s);
            newObj.set_data (s.getDataPtr (), s.getLength ());
            newObj.set_ledgerseq (lSeq);

            wantLedger->peekAccountStateMap ()->getFetchPack
                (haveLedger->peekAccountStateMap ().get (), true, 1024,
                    std::bind (fpAppender, &reply, lSeq, std::placeholders::_1,
                               std::placeholders::_2));

            if (wantLedger->getTransHash ().isNonZero ())
                wantLedger->peekTransactionMap ()->getFetchPack (
                    nullptr, true, 256,
                    std::bind (fpAppender, &reply, lSeq, std::placeholders::_1,
                               std::placeholders::_2));

            if (reply.objects ().size () >= 256)
                break;

            // move may save a ref/unref
            haveLedger = std::move (wantLedger);
            wantLedger = getLedgerByHash (haveLedger->getParentHash ());
        }
        while (wantLedger && (UptimeTimer::getInstance ().getElapsedSeconds () <= (uUptime + 1)));

        m_journal.info << "Built fetch pack with " << reply.objects ().size () << " nodes";
        Message::pointer msg = std::make_shared<Message> (reply, protocol::mtGET_OBJECTS);
        peer->send (msg);
    }
    catch (...)
    {
        m_journal.warning << "Exception building fetch pach";
    }
}

void NetworkOPsImp::sweepFetchPack ()
{
    mFetchPack.sweep ();
}

void NetworkOPsImp::addFetchPack (uint256 const& hash, std::shared_ptr< Blob >& data)
{
    mFetchPack.canonicalize (hash, data);
}

bool NetworkOPsImp::getFetchPack (uint256 const& hash, Blob& data)
{
    bool ret = mFetchPack.retrieve (hash, data);

    if (!ret)
        return false;

    mFetchPack.del (hash, false);

    if (hash != Serializer::getSHA512Half (data))
    {
        m_journal.warning << "Bad entry in fetch pack";
        return false;
    }

    return true;
}

bool NetworkOPsImp::shouldFetchPack (std::uint32_t seq)
{
    if (mFetchSeq == seq)
        return false;
    mFetchSeq = seq;
    return true;
}

int NetworkOPsImp::getFetchSize ()
{
    return mFetchPack.getCacheSize ();
}

void NetworkOPsImp::gotFetchPack (bool progress, std::uint32_t seq)
{

    // FIXME: Calling this function more than once will result in
    // InboundLedgers::gotFetchPack being called more than once
    // which is expensive. A flag should track whether we've already dispatched

    getApp().getJobQueue ().addJob (jtLEDGER_DATA, "gotFetchPack",
                                   std::bind (&InboundLedgers::gotFetchPack,
                                              &getApp().getInboundLedgers (), std::placeholders::_1));
}

void NetworkOPsImp::missingNodeInLedger (std::uint32_t seq)
{
    uint256 hash = getApp().getLedgerMaster ().getHashBySeq (seq);
    if (hash.isZero())
    {
        m_journal.warning << "Missing a node in ledger " << seq << " cannot fetch";
    }
    else
    {
        m_journal.warning << "Missing a node in ledger " << seq << " fetching";
        getApp().getInboundLedgers ().findCreate (hash, seq, InboundLedger::fcGENERIC);
    }
}

//------------------------------------------------------------------------------

NetworkOPs::NetworkOPs (Stoppable& parent)
    : InfoSub::Source ("NetworkOPs", parent)
{
}

NetworkOPs::~NetworkOPs ()
{
}

//------------------------------------------------------------------------------

NetworkOPs* NetworkOPs::New (clock_type& clock, LedgerMaster& ledgerMaster,
    Stoppable& parent, beast::Journal journal)
{
    return new NetworkOPsImp (clock, ledgerMaster, parent, journal);
}

} // ripple
