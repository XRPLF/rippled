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

#include <BeastConfig.h>
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/consensus/Consensus.h>
#include <ripple/app/consensus/RCLConsensus.h>
#include <ripple/app/consensus/RCLValidations.h>
#include <ripple/app/ledger/AcceptedLedger.h>
#include <ripple/app/ledger/InboundLedgers.h>
#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/consensus/ConsensusParms.h>
#include <ripple/app/ledger/LedgerToJson.h>
#include <ripple/app/ledger/LocalTxs.h>
#include <ripple/app/ledger/OpenLedger.h>
#include <ripple/app/ledger/OrderBookDB.h>
#include <ripple/app/ledger/TransactionMaster.h>
#include <ripple/app/main/LoadManager.h>
#include <ripple/app/misc/HashRouter.h>
#include <ripple/app/misc/LoadFeeTrack.h>
#include <ripple/app/misc/Transaction.h>
#include <ripple/app/misc/TxQ.h>
#include <ripple/app/misc/ValidatorKeys.h>
#include <ripple/app/misc/ValidatorList.h>
#include <ripple/app/misc/impl/AccountTxPaging.h>
#include <ripple/app/tx/apply.h>
#include <ripple/basics/mulDiv.h>
#include <ripple/basics/UptimeTimer.h>
#include <ripple/core/ConfigSections.h>
#include <ripple/crypto/csprng.h>
#include <ripple/crypto/RFC1751.h>
#include <ripple/json/to_string.h>
#include <ripple/overlay/Cluster.h>
#include <ripple/overlay/Overlay.h>
#include <ripple/overlay/predicates.h>
#include <ripple/protocol/BuildInfo.h>
#include <ripple/resource/ResourceManager.h>
#include <ripple/beast/rfc2616.h>
#include <ripple/beast/core/LexicalCast.h>
#include <ripple/beast/core/SystemStats.h>
#include <ripple/beast/utility/rngfill.h>
#include <ripple/basics/make_lock.h>
#include <beast/core/detail/base64.hpp>
#include <boost/asio/steady_timer.hpp>

namespace ripple {

class NetworkOPsImp final
    : public NetworkOPs
{
    /**
     * Transaction with input flags and results to be applied in batches.
     */

    class TransactionStatus
    {
    public:
        std::shared_ptr<Transaction> transaction;
        bool admin;
        bool local;
        FailHard failType;
        bool applied;
        TER result;

        TransactionStatus (
                std::shared_ptr<Transaction> t,
                bool a,
                bool l,
                FailHard f)
            : transaction (t)
            , admin (a)
            , local (l)
            , failType (f)
        {}
    };

    /**
     * Synchronization states for transaction batches.
     */
    enum class DispatchState : unsigned char
    {
        none,
        scheduled,
        running,
    };

    static std::array<char const*, 5> const states_;

    /**
     * State accounting records two attributes for each possible server state:
     * 1) Amount of time spent in each state (in microseconds). This value is
     *    updated upon each state transition.
     * 2) Number of transitions to each state.
     *
     * This data can be polled through server_info and represented by
     * monitoring systems similarly to how bandwidth, CPU, and other
     * counter-based metrics are managed.
     *
     * State accounting is more accurate than periodic sampling of server
     * state. With periodic sampling, it is very likely that state transitions
     * are missed, and accuracy of time spent in each state is very rough.
     */
    class StateAccounting
    {
        struct Counters
        {
            std::uint32_t transitions = 0;
            std::chrono::microseconds dur = std::chrono::microseconds (0);
        };

        OperatingMode mode_ = omDISCONNECTED;
        std::array<Counters, 5> counters_;
        mutable std::mutex mutex_;
        std::chrono::system_clock::time_point start_ =
            std::chrono::system_clock::now();
        static std::array<Json::StaticString const, 5> const states_;
        static Json::StaticString const transitions_;
        static Json::StaticString const dur_;

    public:
        explicit StateAccounting ()
        {
            counters_[omDISCONNECTED].transitions = 1;
        }

        /**
         * Record state transition. Update duration spent in previous
         * state.
         *
         * @param om New state.
         */
        void mode (OperatingMode om);

        /**
         * Output state counters in JSON format.
         *
         * @return JSON object.
         */
        Json::Value json() const;
    };

    //! Server fees published on `server` subscription
    struct ServerFeeSummary
    {
        ServerFeeSummary() = default;

        ServerFeeSummary(std::uint64_t fee,
                         boost::optional<TxQ::Metrics>&& escalationMetrics,
                         LoadFeeTrack const & loadFeeTrack);
        bool
        operator !=(ServerFeeSummary const & b) const;

        bool
        operator ==(ServerFeeSummary const & b) const
        {
            return !(*this != b);
        }

        std::uint32_t loadFactorServer = 256;
        std::uint32_t loadBaseServer = 256;
        std::uint64_t baseFee = 10;
        boost::optional<TxQ::Metrics> em = boost::none;
    };


public:
    // VFALCO TODO Make LedgerMaster a SharedPtr or a reference.
    //
    NetworkOPsImp (Application& app, NetworkOPs::clock_type& clock,
        bool standalone, std::size_t network_quorum, bool start_valid,
        JobQueue& job_queue, LedgerMaster& ledgerMaster, Stoppable& parent,
        ValidatorKeys const & validatorKeys, boost::asio::io_service& io_svc,
        beast::Journal journal)
        : NetworkOPs (parent)
        , app_ (app)
        , m_clock (clock)
        , m_journal (journal)
        , m_localTX (make_LocalTxs ())
        , mMode (start_valid ? omFULL : omDISCONNECTED)
        , mNeedNetworkLedger (false)
        , m_amendmentBlocked (false)
        , heartbeatTimer_ (io_svc)
        , clusterTimer_ (io_svc)
        , mConsensus (app,
            make_FeeVote(setup_FeeVote (app_.config().section ("voting")),
                                        app_.logs().journal("FeeVote")),
            ledgerMaster,
            *m_localTX,
            app.getInboundTransactions(),
            stopwatch(),
            validatorKeys,
            app_.logs().journal("LedgerConsensus"))
        , m_ledgerMaster (ledgerMaster)
        , m_job_queue (job_queue)
        , m_standalone (standalone)
        , m_network_quorum (start_valid ? 0 : network_quorum)
        , accounting_ ()
    {
    }

    ~NetworkOPsImp() override
    {
        // This clear() is necessary to ensure the shared_ptrs in this map get
        // destroyed NOW because the objects in this map invoke methods on this
        // class when they are destroyed
        mRpcSubMap.clear();
    }

public:
    OperatingMode getOperatingMode () const override
    {
        return mMode;
    }
    std::string strOperatingMode () const override;

    //
    // Transaction operations.
    //

    // Must complete immediately.
    void submitTransaction (std::shared_ptr<STTx const> const&) override;

    void processTransaction (
        std::shared_ptr<Transaction>& transaction,
        bool bUnlimited, bool bLocal, FailHard failType) override;

    /**
     * For transactions submitted directly by a client, apply batch of
     * transactions and wait for this transaction to complete.
     *
     * @param transaction Transaction object.
     * @param bUnliimited Whether a privileged client connection submitted it.
     * @param failType fail_hard setting from transaction submission.
     */
    void doTransactionSync (std::shared_ptr<Transaction> transaction,
        bool bUnlimited, FailHard failType);

    /**
     * For transactions not submitted by a locally connected client, fire and
     * forget. Add to batch and trigger it to be processed if there's no batch
     * currently being applied.
     *
     * @param transaction Transaction object
     * @param bUnlimited Whether a privileged client connection submitted it.
     * @param failType fail_hard setting from transaction submission.
     */
    void doTransactionAsync (std::shared_ptr<Transaction> transaction,
        bool bUnlimited, FailHard failtype);

    /**
     * Apply transactions in batches. Continue until none are queued.
     */
    void transactionBatch();

    /**
     * Attempt to apply transactions and post-process based on the results.
     *
     * @param Lock that protects the transaction batching
     */
    void apply (std::unique_lock<std::mutex>& batchLock);

    //
    // Owner functions.
    //

    Json::Value getOwnerInfo (
        std::shared_ptr<ReadView const> lpLedger,
        AccountID const& account) override;

    //
    // Book functions.
    //

    void getBookPage (std::shared_ptr<ReadView const>& lpLedger,
                      Book const&, AccountID const& uTakerID, const bool bProof,
                      unsigned int iLimit,
                      Json::Value const& jvMarker, Json::Value& jvResult)
            override;

    // Ledger proposal/close functions.
    void processTrustedProposal (
        RCLCxPeerPos proposal,
        std::shared_ptr<protocol::TMProposeSet> set,
        NodeID const &node) override;

    bool recvValidation (
        STValidation::ref val, std::string const& source) override;

    std::shared_ptr<SHAMap> getTXMap (uint256 const& hash);
    bool hasTXSet (
        const std::shared_ptr<Peer>& peer, uint256 const& set,
        protocol::TxSetStatus status);

    void mapComplete (
        std::shared_ptr<SHAMap> const& map,
        bool fromAcquire) override;

    // Network state machine.

    // VFALCO TODO Try to make all these private since they seem to be...private
    //

    // Used for the "jump" case.
private:
    void switchLastClosedLedger (
        std::shared_ptr<Ledger const> const& newLCL);
    bool checkLastClosedLedger (
        const Overlay::PeerSequence&, uint256& networkClosed);

public:
    bool beginConsensus (uint256 const& networkClosed) override;
    void endConsensus () override;
    void setStandAlone () override
    {
        setMode (omFULL);
    }

    /** Called to initially start our timers.
        Not called for stand-alone mode.
    */
    void setStateTimer () override;

    void needNetworkLedger () override
    {
        mNeedNetworkLedger = true;
    }
    void clearNeedNetworkLedger () override
    {
        mNeedNetworkLedger = false;
    }
    bool isNeedNetworkLedger () override
    {
        return mNeedNetworkLedger;
    }
    bool isFull () override
    {
        return !mNeedNetworkLedger && (mMode == omFULL);
    }
    bool isAmendmentBlocked () override
    {
        return m_amendmentBlocked;
    }
    void setAmendmentBlocked () override;
    void consensusViewChange () override;

    Json::Value getConsensusInfo () override;
    Json::Value getServerInfo (bool human, bool admin) override;
    void clearLedgerFetch () override;
    Json::Value getLedgerFetchInfo () override;
    std::uint32_t acceptLedger (
        boost::optional<std::chrono::milliseconds> consensusDelay) override;
    uint256 getConsensusLCL () override;
    void reportFeeChange () override;

    void updateLocalTx (ReadView const& view) override
    {
        m_localTX->sweep (view);
    }
    std::size_t getLocalTxCount () override
    {
        return m_localTX->size ();
    }

    //Helper function to generate SQL query to get transactions.
    std::string transactionsSQL (
        std::string selection, AccountID const& account,
        std::int32_t minLedger, std::int32_t maxLedger,
        bool descending, std::uint32_t offset, int limit,
        bool binary, bool count, bool bUnlimited);

    // Client information retrieval functions.
    using NetworkOPs::AccountTxs;
    AccountTxs getAccountTxs (
        AccountID const& account,
        std::int32_t minLedger, std::int32_t maxLedger, bool descending,
        std::uint32_t offset, int limit, bool bUnlimited) override;

    AccountTxs getTxsAccount (
        AccountID const& account, std::int32_t minLedger,
        std::int32_t maxLedger, bool forward, Json::Value& token, int limit,
        bool bUnlimited) override;

    using NetworkOPs::txnMetaLedgerType;
    using NetworkOPs::MetaTxsList;

    MetaTxsList
    getAccountTxsB (
        AccountID const& account, std::int32_t minLedger,
        std::int32_t maxLedger,  bool descending, std::uint32_t offset,
        int limit, bool bUnlimited) override;

    MetaTxsList
    getTxsAccountB (
        AccountID const& account, std::int32_t minLedger,
        std::int32_t maxLedger,  bool forward, Json::Value& token,
        int limit, bool bUnlimited) override;

    //
    // Monitoring: publisher side.
    //
    void pubLedger (
        std::shared_ptr<ReadView const> const& lpAccepted) override;
    void pubProposedTransaction (
        std::shared_ptr<ReadView const> const& lpCurrent,
        std::shared_ptr<STTx const> const& stTxn, TER terResult) override;
    void pubValidation (
        STValidation::ref val) override;

    //--------------------------------------------------------------------------
    //
    // InfoSub::Source.
    //
    void subAccount (
        InfoSub::ref ispListener,
        hash_set<AccountID> const& vnaAccountIDs, bool rt) override;
    void unsubAccount (
        InfoSub::ref ispListener,
        hash_set<AccountID> const& vnaAccountIDs,
        bool rt) override;

    // Just remove the subscription from the tracking
    // not from the InfoSub. Needed for InfoSub destruction
    void unsubAccountInternal (
        std::uint64_t seq,
        hash_set<AccountID> const& vnaAccountIDs,
        bool rt) override;

    bool subLedger (InfoSub::ref ispListener, Json::Value& jvResult) override;
    bool unsubLedger (std::uint64_t uListener) override;

    bool subServer (
        InfoSub::ref ispListener, Json::Value& jvResult, bool admin) override;
    bool unsubServer (std::uint64_t uListener) override;

    bool subBook (InfoSub::ref ispListener, Book const&) override;
    bool unsubBook (std::uint64_t uListener, Book const&) override;

    bool subManifests (InfoSub::ref ispListener) override;
    bool unsubManifests (std::uint64_t uListener) override;
    void pubManifest (Manifest const&) override;

    bool subTransactions (InfoSub::ref ispListener) override;
    bool unsubTransactions (std::uint64_t uListener) override;

    bool subRTTransactions (InfoSub::ref ispListener) override;
    bool unsubRTTransactions (std::uint64_t uListener) override;

    bool subValidations (InfoSub::ref ispListener) override;
    bool unsubValidations (std::uint64_t uListener) override;

    bool subPeerStatus (InfoSub::ref ispListener) override;
    bool unsubPeerStatus (std::uint64_t uListener) override;
    void pubPeerStatus (std::function<Json::Value(void)> const&) override;

    InfoSub::pointer findRpcSub (std::string const& strUrl) override;
    InfoSub::pointer addRpcSub (
        std::string const& strUrl, InfoSub::ref) override;
    bool tryRemoveRpcSub (std::string const& strUrl) override;

    //--------------------------------------------------------------------------
    //
    // Stoppable.

    void onStop () override
    {
        mAcquiringLedger.reset();
        {
            boost::system::error_code ec;
            heartbeatTimer_.cancel (ec);
            if (ec)
            {
                JLOG (m_journal.error())
                    << "NetworkOPs: heartbeatTimer cancel error: "
                    << ec.message();
            }

            ec.clear();
            clusterTimer_.cancel (ec);
            if (ec)
            {
                JLOG (m_journal.error())
                    << "NetworkOPs: clusterTimer cancel error: "
                    << ec.message();
            }
        }
        // Make sure that any waitHandlers pending in our timers are done
        // before we declare ourselves stopped.
        using namespace std::chrono_literals;
        waitHandlerCounter_.join("NetworkOPs", 1s, m_journal);
        stopped ();
    }

private:
    void setHeartbeatTimer ();
    void setClusterTimer ();
    void processHeartbeatTimer ();
    void processClusterTimer ();

    void setMode (OperatingMode);

    Json::Value transJson (
        const STTx& stTxn, TER terResult, bool bValidated,
        std::shared_ptr<ReadView const> const& lpCurrent);

    void pubValidatedTransaction (
        std::shared_ptr<ReadView const> const& alAccepted,
        const AcceptedLedgerTx& alTransaction);
    void pubAccountTransaction (
        std::shared_ptr<ReadView const> const& lpCurrent,
        const AcceptedLedgerTx& alTransaction,
        bool isAccepted);

    void pubServer ();

    std::string getHostId (bool forAdmin);

private:
    using SubMapType = hash_map <std::uint64_t, InfoSub::wptr>;
    using SubInfoMapType = hash_map <AccountID, SubMapType>;
    using subRpcMapType = hash_map<std::string, InfoSub::pointer>;

    // XXX Split into more locks.
    using ScopedLockType = std::lock_guard <std::recursive_mutex>;

    Application& app_;
    clock_type& m_clock;
    beast::Journal m_journal;

    std::unique_ptr <LocalTxs> m_localTX;

    std::recursive_mutex mSubLock;

    std::atomic<OperatingMode> mMode;

    std::atomic <bool> mNeedNetworkLedger;
    bool m_amendmentBlocked;

    ClosureCounter<void, boost::system::error_code const&> waitHandlerCounter_;
    boost::asio::steady_timer heartbeatTimer_;
    boost::asio::steady_timer clusterTimer_;

    RCLConsensus mConsensus;

    LedgerMaster& m_ledgerMaster;
    std::shared_ptr<InboundLedger> mAcquiringLedger;

    SubInfoMapType mSubAccount;
    SubInfoMapType mSubRTAccount;

    subRpcMapType mRpcSubMap;

    enum SubTypes
    {
        sLedger,                    // Accepted ledgers.
        sManifests,                 // Received validator manifests.
        sServer,                    // When server changes connectivity state.
        sTransactions,              // All accepted transactions.
        sRTTransactions,            // All proposed and accepted transactions.
        sValidations,               // Received validations.
        sPeerStatus,                // Peer status changes.

        sLastEntry = sPeerStatus    // as this name implies, any new entry must
                                    // be ADDED ABOVE this one
    };
    std::array<SubMapType, SubTypes::sLastEntry+1> mStreamMaps;

    ServerFeeSummary mLastFeeSummary;


    JobQueue& m_job_queue;

    // Whether we are in standalone mode.
    bool const m_standalone;

    // The number of nodes that we need to consider ourselves connected.
    std::size_t const m_network_quorum;

    // Transaction batching.
    std::condition_variable mCond;
    std::mutex mMutex;
    DispatchState mDispatchState = DispatchState::none;
    std::vector <TransactionStatus> mTransactions;

    StateAccounting accounting_;
};

//------------------------------------------------------------------------------

static std::array<char const*, 5> const stateNames {{
    "disconnected",
    "connected",
    "syncing",
    "tracking",
    "full"}};

#ifndef __INTELLISENSE__
static_assert (NetworkOPs::omDISCONNECTED == 0, "");
static_assert (NetworkOPs::omCONNECTED == 1, "");
static_assert (NetworkOPs::omSYNCING == 2, "");
static_assert (NetworkOPs::omTRACKING == 3, "");
static_assert (NetworkOPs::omFULL == 4, "");
#endif

std::array<char const*, 5> const NetworkOPsImp::states_ = stateNames;

std::array<Json::StaticString const, 5> const
NetworkOPsImp::StateAccounting::states_ = {{
    Json::StaticString(stateNames[0]),
    Json::StaticString(stateNames[1]),
    Json::StaticString(stateNames[2]),
    Json::StaticString(stateNames[3]),
    Json::StaticString(stateNames[4])}};

//------------------------------------------------------------------------------
std::string
NetworkOPsImp::getHostId (bool forAdmin)
{
    if (forAdmin)
        return beast::getComputerName ();

    // For non-admin uses hash the node public key into a
    // single RFC1751 word:
    static std::string const shroudedHostId =
        [this]()
        {
            auto const& id = app_.nodeIdentity();

            return RFC1751::getWordFromBlob (
                id.first.data (),
                id.first.size ());
        }();

    return shroudedHostId;
}

void NetworkOPsImp::setStateTimer ()
{
    setHeartbeatTimer ();
    setClusterTimer ();
}

void NetworkOPsImp::setHeartbeatTimer ()
{
    // Only start the timer if waitHandlerCounter_ is not yet joined.
    if (auto optionalCountedHandler = waitHandlerCounter_.wrap (
        [this] (boost::system::error_code const& e)
        {
            if ((e.value() == boost::system::errc::success) &&
                (! m_job_queue.isStopped()))
            {
                m_job_queue.addJob (jtNETOP_TIMER, "NetOPs.heartbeat",
                    [this] (Job&) { processHeartbeatTimer(); });
            }
            // Recover as best we can if an unexpected error occurs.
            if (e.value() != boost::system::errc::success &&
                e.value() != boost::asio::error::operation_aborted)
            {
                // Try again later and hope for the best.
                JLOG (m_journal.error())
                   << "Heartbeat timer got error '" << e.message()
                   << "'.  Restarting timer.";
                setHeartbeatTimer();
            }
        }))
    {
        heartbeatTimer_.expires_from_now (
            mConsensus.parms().ledgerGRANULARITY);
        heartbeatTimer_.async_wait (std::move (*optionalCountedHandler));
    }
}

void NetworkOPsImp::setClusterTimer ()
{
    // Only start the timer if waitHandlerCounter_ is not yet joined.
    if (auto optionalCountedHandler = waitHandlerCounter_.wrap (
        [this] (boost::system::error_code const& e)
        {
            if ((e.value() == boost::system::errc::success) &&
                (! m_job_queue.isStopped()))
            {
                m_job_queue.addJob (jtNETOP_CLUSTER, "NetOPs.cluster",
                    [this] (Job&) { processClusterTimer(); });
            }
            // Recover as best we can if an unexpected error occurs.
            if (e.value() != boost::system::errc::success &&
                e.value() != boost::asio::error::operation_aborted)
            {
                // Try again later and hope for the best.
                JLOG (m_journal.error())
                   << "Cluster timer got error '" << e.message()
                   << "'.  Restarting timer.";
                setClusterTimer();
            }
        }))
    {
        using namespace std::chrono_literals;
        clusterTimer_.expires_from_now (10s);
        clusterTimer_.async_wait (std::move (*optionalCountedHandler));
    }
}

void NetworkOPsImp::processHeartbeatTimer ()
{
    {
        auto lock = make_lock(app_.getMasterMutex());

        // VFALCO NOTE This is for diagnosing a crash on exit
        LoadManager& mgr (app_.getLoadManager ());
        mgr.resetDeadlockDetector ();

        std::size_t const numPeers = app_.overlay ().size ();

        // do we have sufficient peers? If not, we are disconnected.
        if (numPeers < m_network_quorum)
        {
            if (mMode != omDISCONNECTED)
            {
                setMode (omDISCONNECTED);
                JLOG(m_journal.warn())
                    << "Node count (" << numPeers << ") "
                    << "has fallen below quorum (" << m_network_quorum << ").";
            }
            // We do not call mConsensus.timerEntry until there
            // are enough peers providing meaningful inputs to consensus
            setHeartbeatTimer ();

            return;
        }

        if (mMode == omDISCONNECTED)
        {
            setMode (omCONNECTED);
            JLOG(m_journal.info())
                << "Node count (" << numPeers << ") is sufficient.";
        }

        // Check if the last validated ledger forces a change between these
        // states.
        if (mMode == omSYNCING)
            setMode (omSYNCING);
        else if (mMode == omCONNECTED)
            setMode (omCONNECTED);

    }

    mConsensus.timerEntry (app_.timeKeeper().closeTime());

    setHeartbeatTimer ();
}

void NetworkOPsImp::processClusterTimer ()
{
    bool const update = app_.cluster().update(
        app_.nodeIdentity().first,
        "",
        (m_ledgerMaster.getValidatedLedgerAge() <= 4min)
            ? app_.getFeeTrack().getLocalFee()
            : 0,
        app_.timeKeeper().now());

    if (!update)
    {
        JLOG(m_journal.debug()) << "Too soon to send cluster update";
        setClusterTimer ();
        return;
    }

    protocol::TMCluster cluster;
    app_.cluster().for_each(
        [&cluster](ClusterNode const& node)
        {
            protocol::TMClusterNode& n = *cluster.add_clusternodes();
            n.set_publickey(toBase58 (
                TokenType::TOKEN_NODE_PUBLIC,
                node.identity()));
            n.set_reporttime(
                node.getReportTime().time_since_epoch().count());
            n.set_nodeload(node.getLoadFee());
            if (!node.name().empty())
                n.set_nodename(node.name());
        });

    Resource::Gossip gossip = app_.getResourceManager().exportConsumers();
    for (auto& item: gossip.items)
    {
        protocol::TMLoadSource& node = *cluster.add_loadsources();
        node.set_name (to_string (item.address));
        node.set_cost (item.balance);
    }
    app_.overlay ().foreach (send_if (
        std::make_shared<Message>(cluster, protocol::mtCLUSTER),
        peer_in_cluster ()));
    setClusterTimer ();
}

//------------------------------------------------------------------------------


std::string NetworkOPsImp::strOperatingMode () const
{
    if (mMode == omFULL)
    {
        auto const mode = mConsensus.mode();
        if (mode != ConsensusMode::wrongLedger)
        {
            if (mode == ConsensusMode::proposing)
                return "proposing";

            if (mConsensus.validating())
                return "validating";
        }
    }

    return states_[mMode];
}

void NetworkOPsImp::submitTransaction (std::shared_ptr<STTx const> const& iTrans)
{
    if (isNeedNetworkLedger ())
    {
        // Nothing we can do if we've never been in sync
        return;
    }

    // this is an asynchronous interface
    auto const trans = sterilize(*iTrans);

    auto const txid = trans->getTransactionID ();
    auto const flags = app_.getHashRouter().getFlags(txid);

    if ((flags & SF_RETRY) != 0)
    {
        JLOG(m_journal.warn()) << "Redundant transactions submitted";
        return;
    }

    if ((flags & SF_BAD) != 0)
    {
        JLOG(m_journal.warn()) << "Submitted transaction cached bad";
        return;
    }

    try
    {
        auto const validity = checkValidity(
            app_.getHashRouter(), *trans,
                m_ledgerMaster.getValidatedRules(),
                    app_.config());

        if (validity.first != Validity::Valid)
        {
            JLOG(m_journal.warn()) <<
                "Submitted transaction invalid: " <<
                validity.second;
            return;
        }
    }
    catch (std::exception const&)
    {
        JLOG(m_journal.warn()) << "Exception checking transaction" << txid;

        return;
    }

    std::string reason;

    auto tx = std::make_shared<Transaction> (
        trans, reason, app_);

    m_job_queue.addJob (
        jtTRANSACTION, "submitTxn",
        [this, tx] (Job&) {
            auto t = tx;
            processTransaction(t, false, false, FailHard::no);
        });
}

void NetworkOPsImp::processTransaction (std::shared_ptr<Transaction>& transaction,
        bool bUnlimited, bool bLocal, FailHard failType)
{
    auto ev = m_job_queue.makeLoadEvent (jtTXN_PROC, "ProcessTXN");
    auto const newFlags = app_.getHashRouter ().getFlags (transaction->getID ());

    if ((newFlags & SF_BAD) != 0)
    {
        // cached bad
        transaction->setStatus (INVALID);
        transaction->setResult (temBAD_SIGNATURE);
        return;
    }

    // NOTE eahennis - I think this check is redundant,
    // but I'm not 100% sure yet.
    // If so, only cost is looking up HashRouter flags.
    auto const view = m_ledgerMaster.getCurrentLedger();
    auto const validity = checkValidity(
        app_.getHashRouter(),
            *transaction->getSTransaction(),
                view->rules(), app_.config());
    assert(validity.first == Validity::Valid);

    // Not concerned with local checks at this point.
    if (validity.first == Validity::SigBad)
    {
        JLOG(m_journal.info()) << "Transaction has bad signature: " <<
            validity.second;
        transaction->setStatus(INVALID);
        transaction->setResult(temBAD_SIGNATURE);
        app_.getHashRouter().setFlags(transaction->getID(),
            SF_BAD);
        return;
    }

    // canonicalize can change our pointer
    app_.getMasterTransaction ().canonicalize (&transaction);

    if (bLocal)
        doTransactionSync (transaction, bUnlimited, failType);
    else
        doTransactionAsync (transaction, bUnlimited, failType);
}

void NetworkOPsImp::doTransactionAsync (std::shared_ptr<Transaction> transaction,
        bool bUnlimited, FailHard failType)
{
    std::lock_guard<std::mutex> lock (mMutex);

    if (transaction->getApplying())
        return;

    mTransactions.push_back (TransactionStatus (transaction, bUnlimited, false,
        failType));
    transaction->setApplying();

    if (mDispatchState == DispatchState::none)
    {
        if (m_job_queue.addJob (
            jtBATCH, "transactionBatch",
            [this] (Job&) { transactionBatch(); }))
        {
            mDispatchState = DispatchState::scheduled;
        }
    }
}

void NetworkOPsImp::doTransactionSync (std::shared_ptr<Transaction> transaction,
        bool bUnlimited, FailHard failType)
{
    std::unique_lock<std::mutex> lock (mMutex);

    if (! transaction->getApplying())
    {
        mTransactions.push_back (TransactionStatus (transaction, bUnlimited,
            true, failType));
        transaction->setApplying();
    }

    do
    {
        if (mDispatchState == DispatchState::running)
        {
            // A batch processing job is already running, so wait.
            mCond.wait (lock);
        }
        else
        {
            apply (lock);

            if (mTransactions.size())
            {
                // More transactions need to be applied, but by another job.
                if (m_job_queue.addJob (
                    jtBATCH, "transactionBatch",
                    [this] (Job&) { transactionBatch(); }))
                {
                    mDispatchState = DispatchState::scheduled;
                }
            }
        }
    }
    while (transaction->getApplying());
}

void NetworkOPsImp::transactionBatch()
{
    std::unique_lock<std::mutex> lock (mMutex);

    if (mDispatchState == DispatchState::running)
        return;

    while (mTransactions.size())
    {
        apply (lock);
    }
}

void NetworkOPsImp::apply (std::unique_lock<std::mutex>& batchLock)
{
    std::vector<TransactionStatus> submit_held;
    std::vector<TransactionStatus> transactions;
    mTransactions.swap (transactions);
    assert (! transactions.empty());

    assert (mDispatchState != DispatchState::running);
    mDispatchState = DispatchState::running;

    batchLock.unlock();

    {
        auto lock = make_lock(app_.getMasterMutex());
        bool changed = false;
        {
            std::lock_guard <std::recursive_mutex> lock (
                m_ledgerMaster.peekMutex());

            app_.openLedger().modify(
                [&](OpenView& view, beast::Journal j)
            {
                for (TransactionStatus& e : transactions)
                {
                    // we check before addingto the batch
                    ApplyFlags flags = tapNO_CHECK_SIGN;
                    if (e.admin)
                        flags = flags | tapUNLIMITED;

                    auto const result = app_.getTxQ().apply(
                        app_, view, e.transaction->getSTransaction(),
                        flags, j);
                    e.result = result.first;
                    e.applied = result.second;
                    changed = changed || result.second;
                }
                return changed;
            });
        }
        if (changed)
            reportFeeChange();

        auto newOL = app_.openLedger().current();
        for (TransactionStatus& e : transactions)
        {
            if (e.applied)
            {
                pubProposedTransaction (newOL,
                    e.transaction->getSTransaction(), e.result);
            }

            e.transaction->setResult (e.result);

            if (isTemMalformed (e.result))
                app_.getHashRouter().setFlags (e.transaction->getID(), SF_BAD);

    #ifdef BEAST_DEBUG
            if (e.result != tesSUCCESS)
            {
                std::string token, human;

                if (transResultInfo (e.result, token, human))
                {
                    JLOG(m_journal.info()) << "TransactionResult: "
                            << token << ": " << human;
                }
            }
    #endif

            bool addLocal = e.local;

            if (e.result == tesSUCCESS)
            {
                JLOG(m_journal.debug())
                    << "Transaction is now included in open ledger";
                e.transaction->setStatus (INCLUDED);

                auto txCur = e.transaction->getSTransaction();
                for (auto const& tx : m_ledgerMaster.pruneHeldTransactions(
                    txCur->getAccountID(sfAccount), txCur->getSequence() + 1))
                {
                    std::string reason;
                    auto const trans = sterilize(*tx);
                    auto t = std::make_shared<Transaction>(
                        trans, reason, app_);
                    submit_held.emplace_back(
                        t, false, false, FailHard::no);
                    t->setApplying();
                }
            }
            else if (e.result == tefPAST_SEQ)
            {
                // duplicate or conflict
                JLOG(m_journal.info()) << "Transaction is obsolete";
                e.transaction->setStatus (OBSOLETE);
            }
            else if (e.result == terQUEUED)
            {
                JLOG(m_journal.info()) << "Transaction is likely to claim a " <<
                    "fee, but is queued until fee drops";
                e.transaction->setStatus(HELD);
                // Add to held transactions, because it could get
                // kicked out of the queue, and this will try to
                // put it back.
                m_ledgerMaster.addHeldTransaction(e.transaction);
            }
            else if (isTerRetry (e.result))
            {
                if (e.failType == FailHard::yes)
                {
                    addLocal = false;
                }
                else
                {
                    // transaction should be held
                    JLOG(m_journal.debug())
                        << "Transaction should be held: " << e.result;
                    e.transaction->setStatus (HELD);
                    m_ledgerMaster.addHeldTransaction (e.transaction);
                }
            }
            else
            {
                JLOG(m_journal.debug())
                    << "Status other than success " << e.result;
                e.transaction->setStatus (INVALID);
            }

            if (addLocal)
            {
                m_localTX->push_back (
                    m_ledgerMaster.getCurrentLedgerIndex(),
                    e.transaction->getSTransaction());
            }

            if (e.applied || ((mMode != omFULL) &&
                (e.failType != FailHard::yes) && e.local) ||
                    (e.result == terQUEUED))
            {
                auto const toSkip = app_.getHashRouter().shouldRelay(
                    e.transaction->getID());

                if (toSkip)
                {
                    protocol::TMTransaction tx;
                    Serializer s;

                    e.transaction->getSTransaction()->add (s);
                    tx.set_rawtransaction (&s.getData().front(), s.getLength());
                    tx.set_status (protocol::tsCURRENT);
                    tx.set_receivetimestamp (app_.timeKeeper().now().time_since_epoch().count());
                    tx.set_deferred(e.result == terQUEUED);
                    // FIXME: This should be when we received it
                    app_.overlay().foreach (send_if_not (
                        std::make_shared<Message> (tx, protocol::mtTRANSACTION),
                        peer_in_set(*toSkip)));
                }
            }
        }
    }

    batchLock.lock();

    for (TransactionStatus& e : transactions)
        e.transaction->clearApplying();

    if (! submit_held.empty())
    {
        if (mTransactions.empty())
            mTransactions.swap(submit_held);
        else
            for (auto& e : submit_held)
                mTransactions.push_back(std::move(e));
    }

    mCond.notify_all();

    mDispatchState = DispatchState::none;
}

//
// Owner functions
//

Json::Value NetworkOPsImp::getOwnerInfo (
    std::shared_ptr<ReadView const> lpLedger, AccountID const& account)
{
    Json::Value jvObjects (Json::objectValue);
    auto uRootIndex = getOwnerDirIndex (account);
    auto sleNode = lpLedger->read (keylet::page (uRootIndex));
    if (sleNode)
    {
        std::uint64_t  uNodeDir;

        do
        {
            for (auto const& uDirEntry : sleNode->getFieldV256 (sfIndexes))
            {
                auto sleCur = lpLedger->read (keylet::child (uDirEntry));
                assert (sleCur);

                switch (sleCur->getType ())
                {
                case ltOFFER:
                    if (!jvObjects.isMember (jss::offers))
                        jvObjects[jss::offers] = Json::Value (Json::arrayValue);

                    jvObjects[jss::offers].append (sleCur->getJson (0));
                    break;

                case ltRIPPLE_STATE:
                    if (!jvObjects.isMember (jss::ripple_lines))
                    {
                        jvObjects[jss::ripple_lines] =
                                Json::Value (Json::arrayValue);
                    }

                    jvObjects[jss::ripple_lines].append (sleCur->getJson (0));
                    break;

                case ltACCOUNT_ROOT:
                case ltDIR_NODE:
                default:
                    assert (false);
                    break;
                }
            }

            uNodeDir = sleNode->getFieldU64 (sfIndexNext);

            if (uNodeDir)
            {
                sleNode = lpLedger->read (keylet::page (uRootIndex, uNodeDir));
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

bool NetworkOPsImp::checkLastClosedLedger (
    const Overlay::PeerSequence& peerList, uint256& networkClosed)
{
    // Returns true if there's an *abnormal* ledger issue, normal changing in
    // TRACKING mode should return false.  Do we have sufficient validations for
    // our last closed ledger? Or do sufficient nodes agree? And do we have no
    // better ledger available?  If so, we are either tracking or full.

    JLOG(m_journal.trace()) << "NetworkOPsImp::checkLastClosedLedger";

    auto const ourClosed = m_ledgerMaster.getClosedLedger ();

    if (!ourClosed)
        return false;

    uint256 closedLedger = ourClosed->info().hash;
    uint256 prevClosedLedger = ourClosed->info().parentHash;
    JLOG(m_journal.trace()) << "OurClosed:  " << closedLedger;
    JLOG(m_journal.trace()) << "PrevClosed: " << prevClosedLedger;

    struct ValidationCount
    {
        int trustedValidations, nodesUsing;

        ValidationCount() : trustedValidations(0), nodesUsing(0)
        {
        }

        auto
        asTie() const
        {
            return std::tie(trustedValidations, nodesUsing);
        }

        bool
        operator>(const ValidationCount& v) const
        {
            return asTie() > v.asTie();
        }

        bool
        operator==(const ValidationCount& v) const
        {
            return asTie() == v.asTie();
        }
    };

    hash_map<uint256, ValidationCount> ledgers;
    {
        hash_map<uint256, std::uint32_t> current =
            app_.getValidations().currentTrustedDistribution(
                closedLedger,
                prevClosedLedger,
                m_ledgerMaster.getValidLedgerIndex());

        for (auto& it: current)
            ledgers[it.first].trustedValidations += it.second;
    }

    auto& ourVC = ledgers[closedLedger];

    if (mMode >= omTRACKING)
    {
        ++ourVC.nodesUsing;
    }

    for (auto& peer: peerList)
    {
        uint256 peerLedger = peer->getClosedLedgerHash ();

        if (peerLedger.isNonZero ())
            ++ledgers[peerLedger].nodesUsing;
    }

    auto bestVC = ledgers[closedLedger];

    // 3) Is there a network ledger we'd like to switch to? If so, do we have
    // it?
    bool switchLedgers = false;

    for (auto const& it: ledgers)
    {
        JLOG(m_journal.debug()) << "L: " << it.first
                              << " t=" << it.second.trustedValidations
                              << ", n=" << it.second.nodesUsing;

        // Temporary logging to make sure tiebreaking isn't broken
        if (it.second.trustedValidations > 0)
            JLOG(m_journal.trace())
                << "  TieBreakTV: " << it.first;
        else
        {
            if (it.second.nodesUsing > 0)
            {
                JLOG(m_journal.trace())
                    << "  TieBreakNU: " << it.first;
            }
        }

        // Switch to a ledger with more support
        // or the one with higher hash if they have the same support
        if (it.second > bestVC ||
            (it.second == bestVC && it.first > closedLedger))
        {
            bestVC = it.second;
            closedLedger = it.first;
            switchLedgers = true;
        }
    }

    if (switchLedgers && (closedLedger == prevClosedLedger))
    {
        // don't switch to our own previous ledger
        JLOG(m_journal.info()) << "We won't switch to our own previous ledger";
        networkClosed = ourClosed->info().hash;
        switchLedgers = false;
    }
    else
        networkClosed = closedLedger;

    if (!switchLedgers)
        return false;

    auto consensus = m_ledgerMaster.getLedgerByHash (closedLedger);

    if (!consensus)
        consensus = app_.getInboundLedgers().acquire (
            closedLedger, 0, InboundLedger::fcCONSENSUS);

    if (consensus &&
        ! m_ledgerMaster.isCompatible (*consensus, m_journal.debug(),
            "Not switching"))
    {
        // Don't switch to a ledger not on the validated chain
        networkClosed = ourClosed->info().hash;
        return false;
    }

    JLOG(m_journal.warn()) << "We are not running on the consensus ledger";
    JLOG(m_journal.info()) << "Our LCL: " << getJson (*ourClosed);
    JLOG(m_journal.info()) << "Net LCL " << closedLedger;

    if ((mMode == omTRACKING) || (mMode == omFULL))
        setMode (omCONNECTED);

    if (consensus)
    {
        // FIXME: If this rewinds the ledger sequence, or has the same sequence, we
        // should update the status on any stored transactions in the invalidated
        // ledgers.
        switchLastClosedLedger (consensus);
    }

    return true;
}

void NetworkOPsImp::switchLastClosedLedger (
    std::shared_ptr<Ledger const> const& newLCL)
{
    // set the newLCL as our last closed ledger -- this is abnormal code
    JLOG(m_journal.error()) <<
        "JUMP last closed ledger to " << newLCL->info().hash;

    clearNeedNetworkLedger ();

    // Update fee computations.
    // TODO: Needs an open ledger
    //app_.getTxQ().processClosedLedger(app_, *newLCL, true);

    // Caller must own master lock
    {
        // Apply tx in old open ledger to new
        // open ledger. Then apply local tx.

        auto retries = m_localTX->getTxSet();
        auto const lastVal =
            app_.getLedgerMaster().getValidatedLedger();
        boost::optional<Rules> rules;
        if (lastVal)
            rules.emplace(*lastVal, app_.config().features);
        else
            rules.emplace(app_.config().features);
        app_.openLedger().accept(app_, *rules,
            newLCL, OrderedTxs({}), false, retries,
                tapNONE, "jump",
                    [&](OpenView& view, beast::Journal j)
                    {
                        // Stuff the ledger with transactions from the queue.
                        return app_.getTxQ().accept(app_, view);
                    });
    }

    m_ledgerMaster.switchLCL (newLCL);

    protocol::TMStatusChange s;
    s.set_newevent (protocol::neSWITCHED_LEDGER);
    s.set_ledgerseq (newLCL->info().seq);
    s.set_networktime (app_.timeKeeper().now().time_since_epoch().count());
    s.set_ledgerhashprevious (
        newLCL->info().parentHash.begin (),
        newLCL->info().parentHash.size ());
    s.set_ledgerhash (
        newLCL->info().hash.begin (),
        newLCL->info().hash.size ());

    app_.overlay ().foreach (send_always (
        std::make_shared<Message> (s, protocol::mtSTATUS_CHANGE)));
}

bool NetworkOPsImp::beginConsensus (uint256 const& networkClosed)
{
    assert (networkClosed.isNonZero ());

    auto closingInfo = m_ledgerMaster.getCurrentLedger()->info();

    JLOG(m_journal.info()) <<
        "Consensus time for #" << closingInfo.seq <<
        " with LCL " << closingInfo.parentHash;

    auto prevLedger = m_ledgerMaster.getLedgerByHash(
        closingInfo.parentHash);

    if(! prevLedger)
    {
        // this shouldn't happen unless we jump ledgers
        if (mMode == omFULL)
        {
            JLOG(m_journal.warn()) << "Don't have LCL, going to tracking";
            setMode (omTRACKING);
        }

        return false;
    }

    assert (prevLedger->info().hash == closingInfo.parentHash);
    assert (closingInfo.parentHash ==
            m_ledgerMaster.getClosedLedger()->info().hash);

    app_.validators().onConsensusStart (
        app_.getValidations().getCurrentPublicKeys ());

    mConsensus.startRound (
        app_.timeKeeper().closeTime(),
        networkClosed,
        prevLedger);

    JLOG(m_journal.debug()) << "Initiating consensus engine";
    return true;
}

uint256 NetworkOPsImp::getConsensusLCL ()
{
    return mConsensus.prevLedgerID ();
}

void NetworkOPsImp::processTrustedProposal (
    RCLCxPeerPos peerPos,
    std::shared_ptr<protocol::TMProposeSet> set,
    NodeID const& node)
{
    if (mConsensus.peerProposal(
            app_.timeKeeper().closeTime(), peerPos))
    {
        app_.overlay().relay(*set, peerPos.suppressionID());
    }
    else
        JLOG(m_journal.info()) << "Not relaying trusted proposal";
}

void
NetworkOPsImp::mapComplete (
    std::shared_ptr<SHAMap> const& map, bool fromAcquire)
{
    // We now have an additional transaction set
    // either created locally during the consensus process
    // or acquired from a peer

    // Inform peers we have this set
    protocol::TMHaveTransactionSet msg;
    msg.set_hash (map->getHash().as_uint256().begin(), 256 / 8);
    msg.set_status (protocol::tsHAVE);
    app_.overlay().foreach (send_always (
        std::make_shared<Message> (
            msg, protocol::mtHAVE_SET)));

    // We acquired it because consensus asked us to
    if (fromAcquire)
        mConsensus.gotTxSet (
            app_.timeKeeper().closeTime(),
            RCLTxSet{map});
}

void NetworkOPsImp::endConsensus ()
{
    uint256 deadLedger = m_ledgerMaster.getClosedLedger ()->info().parentHash;

    for (auto const& it : app_.overlay ().getActivePeers ())
    {
        if (it && (it->getClosedLedgerHash () == deadLedger))
        {
            JLOG(m_journal.trace()) << "Killing obsolete peer status";
            it->cycleStatus ();
        }
    }

    uint256 networkClosed;
    bool ledgerChange = checkLastClosedLedger (
        app_.overlay ().getActivePeers (), networkClosed);

    if (networkClosed.isZero ())
        return;

    // WRITEME: Unless we are in omFULL and in the process of doing a consensus,
    // we must count how many nodes share our LCL, how many nodes disagree with
    // our LCL, and how many validations our LCL has. We also want to check
    // timing to make sure there shouldn't be a newer LCL. We need this
    // information to do the next three tests.

    if (((mMode == omCONNECTED) || (mMode == omSYNCING)) && !ledgerChange)
    {
        // Count number of peers that agree with us and UNL nodes whose
        // validations we have for LCL.  If the ledger is good enough, go to
        // omTRACKING - TODO
        if (!mNeedNetworkLedger)
            setMode (omTRACKING);
    }

    if (((mMode == omCONNECTED) || (mMode == omTRACKING)) && !ledgerChange)
    {
        // check if the ledger is good enough to go to omFULL
        // Note: Do not go to omFULL if we don't have the previous ledger
        // check if the ledger is bad enough to go to omCONNECTED -- TODO
        auto current = m_ledgerMaster.getCurrentLedger();
        if (app_.timeKeeper().now() <
            (current->info().parentCloseTime + 2* current->info().closeTimeResolution))
        {
            setMode (omFULL);
        }
    }

    beginConsensus (networkClosed);
}

void NetworkOPsImp::consensusViewChange ()
{
    if ((mMode == omFULL) || (mMode == omTRACKING))
        setMode (omCONNECTED);
}

void NetworkOPsImp::pubManifest (Manifest const& mo)
{
    // VFALCO consider std::shared_mutex
    ScopedLockType sl (mSubLock);

    if (!mStreamMaps[sManifests].empty ())
    {
        Json::Value jvObj (Json::objectValue);

        jvObj [jss::type]             = "manifestReceived";
        jvObj [jss::master_key]       = toBase58(
            TokenType::TOKEN_NODE_PUBLIC, mo.masterKey);
        jvObj [jss::signing_key]      = toBase58(
            TokenType::TOKEN_NODE_PUBLIC, mo.signingKey);
        jvObj [jss::seq]              = Json::UInt (mo.sequence);
        jvObj [jss::signature]        = strHex (mo.getSignature ());
        jvObj [jss::master_signature] = strHex (mo.getMasterSignature ());

        for (auto i = mStreamMaps[sManifests].begin ();
            i != mStreamMaps[sManifests].end (); )
        {
            if (auto p = i->second.lock())
            {
                p->send (jvObj, true);
                ++i;
            }
            else
            {
                i = mStreamMaps[sManifests].erase (i);
            }
        }
    }
}

NetworkOPsImp::ServerFeeSummary::ServerFeeSummary(
        std::uint64_t fee,
        boost::optional<TxQ::Metrics>&& escalationMetrics,
        LoadFeeTrack const & loadFeeTrack)
    : loadFactorServer{loadFeeTrack.getLoadFactor()}
    , loadBaseServer{loadFeeTrack.getLoadBase()}
    , baseFee{fee}
    , em{std::move(escalationMetrics)}
{

}


bool
NetworkOPsImp::ServerFeeSummary::operator !=(NetworkOPsImp::ServerFeeSummary const & b) const
{
    if(loadFactorServer != b.loadFactorServer ||
       loadBaseServer != b.loadBaseServer ||
       baseFee != b.baseFee ||
       em.is_initialized() != b.em.is_initialized())
            return true;

    if(em && b.em)
    {
        return (em->minFeeLevel != b.em->minFeeLevel ||
                em->expFeeLevel != b.em->expFeeLevel ||
                em->referenceFeeLevel != b.em->referenceFeeLevel);
    }

    return false;
}

void NetworkOPsImp::pubServer ()
{
    // VFALCO TODO Don't hold the lock across calls to send...make a copy of the
    //             list into a local array while holding the lock then release the
    //             lock and call send on everyone.
    //
    ScopedLockType sl (mSubLock);

    if (!mStreamMaps[sServer].empty ())
    {
        Json::Value jvObj (Json::objectValue);

        ServerFeeSummary f{app_.openLedger().current()->fees().base,
            app_.getTxQ().getMetrics(*app_.openLedger().current()),
            app_.getFeeTrack()};

        // Need to cap to uint64 to uint32 due to JSON limitations
        auto clamp = [](std::uint64_t v)
        {
            constexpr std::uint64_t max32 =
                std::numeric_limits<std::uint32_t>::max();

            return static_cast<std::uint32_t>(std::min(max32, v));
        };


        jvObj [jss::type] = "serverStatus";
        jvObj [jss::server_status] = strOperatingMode ();
        jvObj [jss::load_base] = f.loadBaseServer;
        jvObj [jss::load_factor_server] = f.loadFactorServer;
        jvObj [jss::base_fee] = clamp(f.baseFee);

        if(f.em)
        {
            auto const loadFactor =
                std::max(static_cast<std::uint64_t>(f.loadFactorServer),
                    mulDiv(f.em->expFeeLevel, f.loadBaseServer,
                        f.em->referenceFeeLevel).second);

            jvObj [jss::load_factor]   = clamp(loadFactor);
            jvObj [jss::load_factor_fee_escalation] = clamp(f.em->expFeeLevel);
            jvObj [jss::load_factor_fee_queue] = clamp(f.em->minFeeLevel);
            jvObj [jss::load_factor_fee_reference]
                = clamp(f.em->referenceFeeLevel);

        }
        else
            jvObj [jss::load_factor] = f.loadFactorServer;

        mLastFeeSummary = f;

        for (auto i = mStreamMaps[sServer].begin ();
            i != mStreamMaps[sServer].end (); )
        {
            InfoSub::pointer p = i->second.lock ();

            // VFALCO TODO research the possibility of using thread queues and
            //             linearizing the deletion of subscribers with the
            //             sending of JSON data.
            if (p)
            {
                p->send (jvObj, true);
                ++i;
            }
            else
            {
                i = mStreamMaps[sServer].erase (i);
            }
        }
    }
}


void NetworkOPsImp::pubValidation (STValidation::ref val)
{
    // VFALCO consider std::shared_mutex
    ScopedLockType sl (mSubLock);

    if (!mStreamMaps[sValidations].empty ())
    {
        Json::Value jvObj (Json::objectValue);

        jvObj [jss::type]                  = "validationReceived";
        jvObj [jss::validation_public_key] = toBase58(
            TokenType::TOKEN_NODE_PUBLIC,
            val->getSignerPublic());
        jvObj [jss::ledger_hash]           = to_string (val->getLedgerHash ());
        jvObj [jss::signature]             = strHex (val->getSignature ());
        jvObj [jss::full]                  = val->isFull();
        jvObj [jss::flags]                 = val->getFlags();
        jvObj [jss::signing_time]          = *(*val)[~sfSigningTime];

        if (auto const seq = (*val)[~sfLedgerSequence])
            jvObj [jss::ledger_index] = to_string (*seq);

        if (val->isFieldPresent (sfAmendments))
        {
            jvObj[jss::amendments] = Json::Value (Json::arrayValue);
            for (auto const& amendment : val->getFieldV256(sfAmendments))
                jvObj [jss::amendments].append (to_string (amendment));
        }

        if (auto const closeTime = (*val)[~sfCloseTime])
            jvObj [jss::close_time] = *closeTime;

        if (auto const loadFee = (*val)[~sfLoadFee])
            jvObj [jss::load_fee] = *loadFee;

        if (auto const baseFee = (*val)[~sfBaseFee])
            jvObj [jss::base_fee] = static_cast<double> (*baseFee);

        if (auto const reserveBase = (*val)[~sfReserveBase])
            jvObj [jss::reserve_base] = *reserveBase;

        if (auto const reserveInc = (*val)[~sfReserveIncrement])
            jvObj [jss::reserve_inc] = *reserveInc;

        for (auto i = mStreamMaps[sValidations].begin ();
            i != mStreamMaps[sValidations].end (); )
        {
            if (auto p = i->second.lock())
            {
                p->send (jvObj, true);
                ++i;
            }
            else
            {
                i = mStreamMaps[sValidations].erase (i);
            }
        }
    }
}

void NetworkOPsImp::pubPeerStatus (
    std::function<Json::Value(void)> const& func)
{
    ScopedLockType sl (mSubLock);

    if (!mStreamMaps[sPeerStatus].empty ())
    {
        Json::Value jvObj (func());

        jvObj [jss::type]                  = "peerStatusChange";

        for (auto i = mStreamMaps[sPeerStatus].begin ();
            i != mStreamMaps[sPeerStatus].end (); )
        {
            InfoSub::pointer p = i->second.lock ();

            if (p)
            {
                p->send (jvObj, true);
                ++i;
            }
            else
            {
                i = mStreamMaps[sPeerStatus].erase (i);
            }
        }
    }
}

void NetworkOPsImp::setMode (OperatingMode om)
{
    if (om == omCONNECTED)
    {
        if (app_.getLedgerMaster ().getValidatedLedgerAge () < 1min)
            om = omSYNCING;
    }
    else if (om == omSYNCING)
    {
        if (app_.getLedgerMaster ().getValidatedLedgerAge () >= 1min)
            om = omCONNECTED;
    }

    if ((om > omTRACKING) && m_amendmentBlocked)
        om = omTRACKING;

    if (mMode == om)
        return;

    mMode = om;

    accounting_.mode (om);

    JLOG(m_journal.info()) << "STATE->" << strOperatingMode ();
    pubServer ();
}


std::string
NetworkOPsImp::transactionsSQL (
    std::string selection, AccountID const& account,
    std::int32_t minLedger, std::int32_t maxLedger, bool descending,
    std::uint32_t offset, int limit,
    bool binary, bool count, bool bUnlimited)
{
    std::uint32_t NONBINARY_PAGE_LENGTH = 200;
    std::uint32_t BINARY_PAGE_LENGTH = 500;

    std::uint32_t numberOfResults;

    if (count)
    {
        numberOfResults = 1000000000;
    }
    else if (limit < 0)
    {
        numberOfResults = binary ? BINARY_PAGE_LENGTH : NONBINARY_PAGE_LENGTH;
    }
    else if (!bUnlimited)
    {
        numberOfResults = std::min (
            binary ? BINARY_PAGE_LENGTH : NONBINARY_PAGE_LENGTH,
            static_cast<std::uint32_t> (limit));
    }
    else
    {
        numberOfResults = limit;
    }

    std::string maxClause = "";
    std::string minClause = "";

    if (maxLedger != -1)
    {
        maxClause = boost::str (boost::format (
            "AND AccountTransactions.LedgerSeq <= '%u'") % maxLedger);
    }

    if (minLedger != -1)
    {
        minClause = boost::str (boost::format (
            "AND AccountTransactions.LedgerSeq >= '%u'") % minLedger);
    }

    std::string sql;

    if (count)
        sql =
            boost::str (boost::format (
                "SELECT %s FROM AccountTransactions "
                "WHERE Account = '%s' %s %s LIMIT %u, %u;")
            % selection
            % app_.accountIDCache().toBase58(account)
            % maxClause
            % minClause
            % beast::lexicalCastThrow <std::string> (offset)
            % beast::lexicalCastThrow <std::string> (numberOfResults)
        );
    else
        sql =
            boost::str (boost::format (
                "SELECT %s FROM "
                "AccountTransactions INNER JOIN Transactions "
                "ON Transactions.TransID = AccountTransactions.TransID "
                "WHERE Account = '%s' %s %s "
                "ORDER BY AccountTransactions.LedgerSeq %s, "
                "AccountTransactions.TxnSeq %s, AccountTransactions.TransID %s "
                "LIMIT %u, %u;")
                    % selection
                    % app_.accountIDCache().toBase58(account)
                    % maxClause
                    % minClause
                    % (descending ? "DESC" : "ASC")
                    % (descending ? "DESC" : "ASC")
                    % (descending ? "DESC" : "ASC")
                    % beast::lexicalCastThrow <std::string> (offset)
                    % beast::lexicalCastThrow <std::string> (numberOfResults)
                   );
    JLOG(m_journal.trace()) << "txSQL query: " << sql;
    return sql;
}

NetworkOPs::AccountTxs NetworkOPsImp::getAccountTxs (
    AccountID const& account,
    std::int32_t minLedger, std::int32_t maxLedger, bool descending,
    std::uint32_t offset, int limit, bool bUnlimited)
{
    // can be called with no locks
    AccountTxs ret;

    std::string sql = transactionsSQL (
        "AccountTransactions.LedgerSeq,Status,RawTxn,TxnMeta", account,
        minLedger, maxLedger, descending, offset, limit, false, false,
        bUnlimited);

    {
        auto db = app_.getTxnDB ().checkoutDb ();

        boost::optional<std::uint64_t> ledgerSeq;
        boost::optional<std::string> status;
        soci::blob sociTxnBlob (*db), sociTxnMetaBlob (*db);
        soci::indicator rti, tmi;
        Blob rawTxn, txnMeta;

        soci::statement st =
                (db->prepare << sql,
                 soci::into(ledgerSeq),
                 soci::into(status),
                 soci::into(sociTxnBlob, rti),
                 soci::into(sociTxnMetaBlob, tmi));

        st.execute ();
        while (st.fetch ())
        {
            if (soci::i_ok == rti)
                convert(sociTxnBlob, rawTxn);
            else
                rawTxn.clear ();

            if (soci::i_ok == tmi)
                convert (sociTxnMetaBlob, txnMeta);
            else
                txnMeta.clear ();

            auto txn = Transaction::transactionFromSQL (
                ledgerSeq, status, rawTxn, app_);

            if (txnMeta.empty ())
            { // Work around a bug that could leave the metadata missing
                auto const seq = rangeCheckedCast<std::uint32_t>(
                    ledgerSeq.value_or (0));

                JLOG(m_journal.warn()) <<
                    "Recovering ledger " << seq <<
                    ", txn " << txn->getID();

                if (auto l = m_ledgerMaster.getLedgerBySeq(seq))
                    pendSaveValidated(app_, l, false, false);
            }

            if (txn)
                ret.emplace_back (txn, std::make_shared<TxMeta> (
                    txn->getID (), txn->getLedger (), txnMeta,
                        app_.journal("TxMeta")));
        }
    }

    return ret;
}

std::vector<NetworkOPsImp::txnMetaLedgerType> NetworkOPsImp::getAccountTxsB (
    AccountID const& account,
    std::int32_t minLedger, std::int32_t maxLedger, bool descending,
    std::uint32_t offset, int limit, bool bUnlimited)
{
    // can be called with no locks
    std::vector<txnMetaLedgerType> ret;

    std::string sql = transactionsSQL (
        "AccountTransactions.LedgerSeq,Status,RawTxn,TxnMeta", account,
        minLedger, maxLedger, descending, offset, limit, true/*binary*/, false,
        bUnlimited);

    {
        auto db = app_.getTxnDB ().checkoutDb ();

        boost::optional<std::uint64_t> ledgerSeq;
        boost::optional<std::string> status;
        soci::blob sociTxnBlob (*db), sociTxnMetaBlob (*db);
        soci::indicator rti, tmi;

        soci::statement st =
                (db->prepare << sql,
                 soci::into(ledgerSeq),
                 soci::into(status),
                 soci::into(sociTxnBlob, rti),
                 soci::into(sociTxnMetaBlob, tmi));

        st.execute ();
        while (st.fetch ())
        {
            Blob rawTxn;
            if (soci::i_ok == rti)
                convert (sociTxnBlob, rawTxn);
            Blob txnMeta;
            if (soci::i_ok == tmi)
                convert (sociTxnMetaBlob, txnMeta);

            auto const seq =
                rangeCheckedCast<std::uint32_t>(ledgerSeq.value_or (0));

            ret.emplace_back (
                strHex (rawTxn), strHex (txnMeta), seq);
        }
    }

    return ret;
}

NetworkOPsImp::AccountTxs
NetworkOPsImp::getTxsAccount (
    AccountID const& account, std::int32_t minLedger,
    std::int32_t maxLedger, bool forward, Json::Value& token,
    int limit, bool bUnlimited)
{
    static std::uint32_t const page_length (200);

    Application& app = app_;
    NetworkOPsImp::AccountTxs ret;

    auto bound = [&ret, &app](
        std::uint32_t ledger_index,
        std::string const& status,
        Blob const& rawTxn,
        Blob const& rawMeta)
    {
        convertBlobsToTxResult (
            ret, ledger_index, status, rawTxn, rawMeta, app);
    };

    accountTxPage(app_.getTxnDB (), app_.accountIDCache(),
        std::bind(saveLedgerAsync, std::ref(app_),
            std::placeholders::_1), bound, account, minLedger,
                maxLedger, forward, token, limit, bUnlimited,
                    page_length);

    return ret;
}

NetworkOPsImp::MetaTxsList
NetworkOPsImp::getTxsAccountB (
    AccountID const& account, std::int32_t minLedger,
    std::int32_t maxLedger,  bool forward, Json::Value& token,
    int limit, bool bUnlimited)
{
    static const std::uint32_t page_length (500);

    MetaTxsList ret;

    auto bound = [&ret](
        std::uint32_t ledgerIndex,
        std::string const& status,
        Blob const& rawTxn,
        Blob const& rawMeta)
    {
        ret.emplace_back (strHex(rawTxn), strHex (rawMeta), ledgerIndex);
    };

    accountTxPage(app_.getTxnDB (), app_.accountIDCache(),
        std::bind(saveLedgerAsync, std::ref(app_),
            std::placeholders::_1), bound, account, minLedger,
                maxLedger, forward, token, limit, bUnlimited,
                    page_length);
    return ret;
}

bool NetworkOPsImp::recvValidation (
    STValidation::ref val, std::string const& source)
{
    JLOG(m_journal.debug()) << "recvValidation " << val->getLedgerHash ()
                          << " from " << source;
    pubValidation (val);
    return handleNewValidation(app_, val, source);
}

Json::Value NetworkOPsImp::getConsensusInfo ()
{
    return mConsensus.getJson (true);
}

Json::Value NetworkOPsImp::getServerInfo (bool human, bool admin)
{
    Json::Value info = Json::objectValue;

    // hostid: unique string describing the machine
    if (human)
        info [jss::hostid] = getHostId (admin);

    info [jss::build_version] = BuildInfo::getVersionString ();

    info [jss::server_state] = strOperatingMode ();

    if (mNeedNetworkLedger)
        info[jss::network_ledger] = "waiting";

    info[jss::validation_quorum] = static_cast<Json::UInt>(
        app_.validators ().quorum ());

    info[jss::io_latency_ms] = static_cast<Json::UInt> (
        app_.getIOLatency().count());

    if (admin)
    {
        if (!app_.getValidationPublicKey().empty())
        {
            info[jss::pubkey_validator] = toBase58 (
                TokenType::TOKEN_NODE_PUBLIC,
                app_.validators().localPublicKey());
        }
        else
        {
            info[jss::pubkey_validator] = "none";
        }
    }

    info[jss::pubkey_node] = toBase58 (
        TokenType::TOKEN_NODE_PUBLIC,
        app_.nodeIdentity().first);

    info[jss::complete_ledgers] =
            app_.getLedgerMaster ().getCompleteLedgers ();

    if (m_amendmentBlocked)
        info[jss::amendment_blocked] = true;

    auto const fp = m_ledgerMaster.getFetchPackCacheSize ();

    if (fp != 0)
        info[jss::fetch_pack] = Json::UInt (fp);

    info[jss::peers] = Json::UInt (app_.overlay ().size ());

    Json::Value lastClose = Json::objectValue;
    lastClose[jss::proposers] = Json::UInt(mConsensus.prevProposers());

    if (human)
    {
        lastClose[jss::converge_time_s] =
            std::chrono::duration<double>{
                mConsensus.prevRoundTime()}.count();
    }
    else
    {
        lastClose[jss::converge_time] =
                Json::Int (mConsensus.prevRoundTime().count());
    }

    info[jss::last_close] = lastClose;

    //  info[jss::consensus] = mConsensus.getJson();

    if (admin)
        info[jss::load] = m_job_queue.getJson ();

    auto const escalationMetrics = app_.getTxQ().getMetrics(
        *app_.openLedger().current());

    constexpr std::uint64_t max32 =
        std::numeric_limits<std::uint32_t>::max();

    auto const loadFactorServer = app_.getFeeTrack().getLoadFactor();
    auto const loadBaseServer = app_.getFeeTrack().getLoadBase();
    auto const loadFactorFeeEscalation = escalationMetrics ?
        escalationMetrics->expFeeLevel : 1;
    auto const loadBaseFeeEscalation = escalationMetrics ?
        escalationMetrics->referenceFeeLevel : 1;

    auto const loadFactor = std::max(static_cast<std::uint64_t>(loadFactorServer),
        mulDiv(loadFactorFeeEscalation, loadBaseServer, loadBaseFeeEscalation).second);

    if (!human)
    {
        info[jss::load_base] = loadBaseServer;
        info[jss::load_factor] = static_cast<std::uint32_t>(
            std::min(max32, loadFactor));
        if (escalationMetrics)
        {
            info[jss::load_factor_server] = loadFactorServer;

            /* Json::Value doesn't support uint64, so clamp to max
                uint32 value. This is mostly theoretical, since there
                probably isn't enough extant XRP to drive the factor
                that high.
            */
            info[jss::load_factor_fee_escalation] =
                static_cast<std::uint32_t> (std::min(
                    max32, loadFactorFeeEscalation));
            info[jss::load_factor_fee_queue] =
                static_cast<std::uint32_t> (std::min(
                    max32, escalationMetrics->minFeeLevel));
            info[jss::load_factor_fee_reference] =
                static_cast<std::uint32_t> (std::min(
                    max32, loadBaseFeeEscalation));
        }
    }
    else
    {
        info[jss::load_factor] = static_cast<double> (loadFactor) / loadBaseServer;

        if (loadFactorServer != loadFactor)
            info[jss::load_factor_server] =
                static_cast<double> (loadFactorServer) / loadBaseServer;

        if (admin)
        {
            std::uint32_t fee = app_.getFeeTrack().getLocalFee();
            if (fee != loadBaseServer)
                info[jss::load_factor_local] =
                    static_cast<double> (fee) / loadBaseServer;
            fee = app_.getFeeTrack ().getRemoteFee();
            if (fee != loadBaseServer)
                info[jss::load_factor_net] =
                    static_cast<double> (fee) / loadBaseServer;
            fee = app_.getFeeTrack().getClusterFee();
            if (fee != loadBaseServer)
                info[jss::load_factor_cluster] =
                    static_cast<double> (fee) / loadBaseServer;
        }
        if (escalationMetrics)
        {
            if (loadFactorFeeEscalation !=
                    escalationMetrics->referenceFeeLevel &&
                        (admin || loadFactorFeeEscalation != loadFactor))
                info[jss::load_factor_fee_escalation] =
                    static_cast<double> (loadFactorFeeEscalation) /
                        escalationMetrics->referenceFeeLevel;
            if (escalationMetrics->minFeeLevel !=
                    escalationMetrics->referenceFeeLevel)
                info[jss::load_factor_fee_queue] =
                    static_cast<double> (escalationMetrics->minFeeLevel) /
                        escalationMetrics->referenceFeeLevel;
        }
    }

    bool valid = false;
    auto lpClosed = m_ledgerMaster.getValidatedLedger ();

    if (lpClosed)
        valid = true;
    else
        lpClosed = m_ledgerMaster.getClosedLedger ();

    if (lpClosed)
    {
        std::uint64_t baseFee = lpClosed->fees().base;
        std::uint64_t baseRef = lpClosed->fees().units;
        Json::Value l (Json::objectValue);
        l[jss::seq] = Json::UInt (lpClosed->info().seq);
        l[jss::hash] = to_string (lpClosed->info().hash);

        if (!human)
        {
            l[jss::base_fee] = Json::Value::UInt (baseFee);
            l[jss::reserve_base] = Json::Value::UInt (lpClosed->fees().accountReserve(0).drops());
            l[jss::reserve_inc] =
                    Json::Value::UInt (lpClosed->fees().increment);
            l[jss::close_time] =
                    Json::Value::UInt (lpClosed->info().closeTime.time_since_epoch().count());
        }
        else
        {
            l[jss::base_fee_xrp] = static_cast<double> (baseFee) /
                    SYSTEM_CURRENCY_PARTS;
            l[jss::reserve_base_xrp]   =
                static_cast<double> (Json::UInt (
                    lpClosed->fees().accountReserve(0).drops() * baseFee / baseRef))
                    / SYSTEM_CURRENCY_PARTS;
            l[jss::reserve_inc_xrp]    =
                static_cast<double> (Json::UInt (
                    lpClosed->fees().increment * baseFee / baseRef))
                    / SYSTEM_CURRENCY_PARTS;

            auto const nowOffset = app_.timeKeeper().nowOffset();
            if (std::abs (nowOffset.count()) >= 60)
                l[jss::system_time_offset] = nowOffset.count();

            auto const closeOffset = app_.timeKeeper().closeOffset();
            if (std::abs (closeOffset.count()) >= 60)
                l[jss::close_time_offset] = closeOffset.count();

            auto lCloseTime = lpClosed->info().closeTime;
            auto closeTime = app_.timeKeeper().closeTime();
            if (lCloseTime <= closeTime)
            {
                auto age = closeTime - lCloseTime;
                if (age < 1000000s)
                    l[jss::age] = Json::UInt (age.count());
                else
                    l[jss::age] = 0;
            }
        }

        if (valid)
            info[jss::validated_ledger] = l;
        else
            info[jss::closed_ledger] = l;

        auto lpPublished = m_ledgerMaster.getPublishedLedger ();
        if (! lpPublished)
            info[jss::published_ledger] = "none";
        else if (lpPublished->info().seq != lpClosed->info().seq)
            info[jss::published_ledger] = lpPublished->info().seq;
    }

    info[jss::state_accounting] = accounting_.json();
    info[jss::uptime] = UptimeTimer::getInstance ().getElapsedSeconds ();

    return info;
}

void NetworkOPsImp::clearLedgerFetch ()
{
    app_.getInboundLedgers().clearFailures();
}

Json::Value NetworkOPsImp::getLedgerFetchInfo ()
{
    return app_.getInboundLedgers().getInfo();
}

void NetworkOPsImp::pubProposedTransaction (
    std::shared_ptr<ReadView const> const& lpCurrent,
    std::shared_ptr<STTx const> const& stTxn, TER terResult)
{
    Json::Value jvObj   = transJson (*stTxn, terResult, false, lpCurrent);

    {
        ScopedLockType sl (mSubLock);

        auto it = mStreamMaps[sRTTransactions].begin ();
        while (it != mStreamMaps[sRTTransactions].end ())
        {
            InfoSub::pointer p = it->second.lock ();

            if (p)
            {
                p->send (jvObj, true);
                ++it;
            }
            else
            {
                it = mStreamMaps[sRTTransactions].erase (it);
            }
        }
    }
    AcceptedLedgerTx alt (lpCurrent, stTxn, terResult,
        app_.accountIDCache(), app_.logs());
    JLOG(m_journal.trace()) << "pubProposed: " << alt.getJson ();
    pubAccountTransaction (lpCurrent, alt, false);
}

void NetworkOPsImp::pubLedger (
    std::shared_ptr<ReadView const> const& lpAccepted)
{
    // Ledgers are published only when they acquire sufficient validations
    // Holes are filled across connection loss or other catastrophe

    std::shared_ptr<AcceptedLedger> alpAccepted =
        app_.getAcceptedLedgerCache().fetch (lpAccepted->info().hash);
    if (! alpAccepted)
    {
        alpAccepted = std::make_shared<AcceptedLedger> (
            lpAccepted, app_.accountIDCache(), app_.logs());
        app_.getAcceptedLedgerCache().canonicalize (
            lpAccepted->info().hash, alpAccepted);
    }

    {
        ScopedLockType sl (mSubLock);

        if (!mStreamMaps[sLedger].empty ())
        {
            Json::Value jvObj (Json::objectValue);

            jvObj[jss::type] = "ledgerClosed";
            jvObj[jss::ledger_index] = lpAccepted->info().seq;
            jvObj[jss::ledger_hash] = to_string (lpAccepted->info().hash);
            jvObj[jss::ledger_time]
                    = Json::Value::UInt (lpAccepted->info().closeTime.time_since_epoch().count());

            jvObj[jss::fee_ref]
                    = Json::UInt (lpAccepted->fees().units);
            jvObj[jss::fee_base] = Json::UInt (lpAccepted->fees().base);
            jvObj[jss::reserve_base] = Json::UInt (lpAccepted->fees().accountReserve(0).drops());
            jvObj[jss::reserve_inc] = Json::UInt (lpAccepted->fees().increment);

            jvObj[jss::txn_count] = Json::UInt (alpAccepted->getTxnCount ());

            if (mMode >= omSYNCING)
            {
                jvObj[jss::validated_ledgers]
                        = app_.getLedgerMaster ().getCompleteLedgers ();
            }

            auto it = mStreamMaps[sLedger].begin ();
            while (it != mStreamMaps[sLedger].end ())
            {
                InfoSub::pointer p = it->second.lock ();
                if (p)
                {
                    p->send (jvObj, true);
                    ++it;
                }
                else
                    it = mStreamMaps[sLedger].erase (it);
            }
        }
    }

    // Don't lock since pubAcceptedTransaction is locking.
    for (auto const& vt : alpAccepted->getMap ())
    {
        JLOG(m_journal.trace()) << "pubAccepted: " << vt.second->getJson ();
        pubValidatedTransaction (lpAccepted, *vt.second);
    }
}

void NetworkOPsImp::reportFeeChange ()
{
    ServerFeeSummary f{app_.openLedger().current()->fees().base,
        app_.getTxQ().getMetrics(*app_.openLedger().current()),
        app_.getFeeTrack()};

    // only schedule the job if something has changed
    if (f != mLastFeeSummary)
    {
        m_job_queue.addJob (
            jtCLIENT, "reportFeeChange->pubServer",
            [this] (Job&) { pubServer(); });
    }
}

// This routine should only be used to publish accepted or validated
// transactions.
Json::Value NetworkOPsImp::transJson(
    const STTx& stTxn, TER terResult, bool bValidated,
    std::shared_ptr<ReadView const> const& lpCurrent)
{
    Json::Value jvObj (Json::objectValue);
    std::string sToken;
    std::string sHuman;

    transResultInfo (terResult, sToken, sHuman);

    jvObj[jss::type]           = "transaction";
    jvObj[jss::transaction]    = stTxn.getJson (0);

    if (bValidated)
    {
        jvObj[jss::ledger_index]           = lpCurrent->info().seq;
        jvObj[jss::ledger_hash]            = to_string (lpCurrent->info().hash);
        jvObj[jss::transaction][jss::date] =
            lpCurrent->info().closeTime.time_since_epoch().count();
        jvObj[jss::validated]              = true;

        // WRITEME: Put the account next seq here

    }
    else
    {
        jvObj[jss::validated]              = false;
        jvObj[jss::ledger_current_index]   = lpCurrent->info().seq;
    }

    jvObj[jss::status]                 = bValidated ? "closed" : "proposed";
    jvObj[jss::engine_result]          = sToken;
    jvObj[jss::engine_result_code]     = terResult;
    jvObj[jss::engine_result_message]  = sHuman;

    if (stTxn.getTxnType() == ttOFFER_CREATE)
    {
        auto const account = stTxn.getAccountID(sfAccount);
        auto const amount = stTxn.getFieldAmount (sfTakerGets);

        // If the offer create is not self funded then add the owner balance
        if (account != amount.issue ().account)
        {
            auto const ownerFunds = accountFunds(*lpCurrent,
                account, amount, fhIGNORE_FREEZE, app_.journal ("View"));
            jvObj[jss::transaction][jss::owner_funds] = ownerFunds.getText ();
        }
    }

    return jvObj;
}

void NetworkOPsImp::pubValidatedTransaction (
    std::shared_ptr<ReadView const> const& alAccepted,
    const AcceptedLedgerTx& alTx)
{
    Json::Value jvObj = transJson (
        *alTx.getTxn (), alTx.getResult (), true, alAccepted);
    jvObj[jss::meta] = alTx.getMeta ()->getJson (0);

    {
        ScopedLockType sl (mSubLock);

        auto it = mStreamMaps[sTransactions].begin ();
        while (it != mStreamMaps[sTransactions].end ())
        {
            InfoSub::pointer p = it->second.lock ();

            if (p)
            {
                p->send (jvObj, true);
                ++it;
            }
            else
                it = mStreamMaps[sTransactions].erase (it);
        }

        it = mStreamMaps[sRTTransactions].begin ();

        while (it != mStreamMaps[sRTTransactions].end ())
        {
            InfoSub::pointer p = it->second.lock ();

            if (p)
            {
                p->send (jvObj, true);
                ++it;
            }
            else
                it = mStreamMaps[sRTTransactions].erase (it);
        }
    }
    app_.getOrderBookDB ().processTxn (alAccepted, alTx, jvObj);
    pubAccountTransaction (alAccepted, alTx, true);
}

void NetworkOPsImp::pubAccountTransaction (
    std::shared_ptr<ReadView const> const& lpCurrent,
    const AcceptedLedgerTx& alTx,
    bool bAccepted)
{
    hash_set<InfoSub::pointer>  notify;
    int                             iProposed   = 0;
    int                             iAccepted   = 0;

    {
        ScopedLockType sl (mSubLock);

        if (!bAccepted && mSubRTAccount.empty ()) return;

        if (!mSubAccount.empty () || (!mSubRTAccount.empty ()) )
        {
            for (auto const& affectedAccount: alTx.getAffected ())
            {
                auto simiIt
                        = mSubRTAccount.find (affectedAccount);
                if (simiIt != mSubRTAccount.end ())
                {
                    auto it = simiIt->second.begin ();

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
                    simiIt  = mSubAccount.find (affectedAccount);

                    if (simiIt != mSubAccount.end ())
                    {
                        auto it = simiIt->second.begin ();
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
    JLOG(m_journal.trace()) << "pubAccountTransaction:" <<
        " iProposed=" << iProposed <<
        " iAccepted=" << iAccepted;

    if (!notify.empty ())
    {
        Json::Value jvObj = transJson (
            *alTx.getTxn (), alTx.getResult (), bAccepted, lpCurrent);

        if (alTx.isApplied ())
            jvObj[jss::meta] = alTx.getMeta ()->getJson (0);

        for (InfoSub::ref isrListener : notify)
            isrListener->send (jvObj, true);
    }
}

//
// Monitoring
//

void NetworkOPsImp::subAccount (
    InfoSub::ref isrListener,
    hash_set<AccountID> const& vnaAccountIDs, bool rt)
{
    SubInfoMapType& subMap = rt ? mSubRTAccount : mSubAccount;

    for (auto const& naAccountID : vnaAccountIDs)
    {
        JLOG(m_journal.trace()) <<
            "subAccount: account: " << toBase58(naAccountID);

        isrListener->insertSubAccountInfo (naAccountID, rt);
    }

    ScopedLockType sl (mSubLock);

    for (auto const& naAccountID : vnaAccountIDs)
    {
        auto simIterator = subMap.find (naAccountID);
        if (simIterator == subMap.end ())
        {
            // Not found, note that account has a new single listner.
            SubMapType  usisElement;
            usisElement[isrListener->getSeq ()] = isrListener;
            // VFALCO NOTE This is making a needless copy of naAccountID
            subMap.insert (simIterator,
                make_pair(naAccountID, usisElement));
        }
        else
        {
            // Found, note that the account has another listener.
            simIterator->second[isrListener->getSeq ()] = isrListener;
        }
    }
}

void NetworkOPsImp::unsubAccount (
    InfoSub::ref isrListener,
    hash_set<AccountID> const& vnaAccountIDs,
    bool rt)
{
    for (auto const& naAccountID : vnaAccountIDs)
    {
        // Remove from the InfoSub
        isrListener->deleteSubAccountInfo(naAccountID, rt);
    }

    // Remove from the server
    unsubAccountInternal (isrListener->getSeq(), vnaAccountIDs, rt);
}

void NetworkOPsImp::unsubAccountInternal (
    std::uint64_t uSeq,
    hash_set<AccountID> const& vnaAccountIDs,
    bool rt)
{
    ScopedLockType sl (mSubLock);

    SubInfoMapType& subMap = rt ? mSubRTAccount : mSubAccount;

    for (auto const& naAccountID : vnaAccountIDs)
    {
        auto simIterator = subMap.find (naAccountID);

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

bool NetworkOPsImp::subBook (InfoSub::ref isrListener, Book const& book)
{
    if (auto listeners = app_.getOrderBookDB ().makeBookListeners (book))
        listeners->addSubscriber (isrListener);
    else
        assert (false);
    return true;
}

bool NetworkOPsImp::unsubBook (std::uint64_t uSeq, Book const& book)
{
    if (auto listeners = app_.getOrderBookDB ().getBookListeners (book))
        listeners->removeSubscriber (uSeq);

    return true;
}

std::uint32_t NetworkOPsImp::acceptLedger (
    boost::optional<std::chrono::milliseconds> consensusDelay)
{
    // This code-path is exclusively used when the server is in standalone
    // mode via `ledger_accept`
    assert (m_standalone);

    if (!m_standalone)
        Throw<std::runtime_error> ("Operation only possible in STANDALONE mode.");

    // FIXME Could we improve on this and remove the need for a specialized
    // API in Consensus?
    beginConsensus (m_ledgerMaster.getClosedLedger()->info().hash);
    mConsensus.simulate (app_.timeKeeper().closeTime(), consensusDelay);
    return m_ledgerMaster.getCurrentLedger ()->info().seq;
}

// <-- bool: true=added, false=already there
bool NetworkOPsImp::subLedger (InfoSub::ref isrListener, Json::Value& jvResult)
{
    if (auto lpClosed = m_ledgerMaster.getValidatedLedger ())
    {
        jvResult[jss::ledger_index]    = lpClosed->info().seq;
        jvResult[jss::ledger_hash]     = to_string (lpClosed->info().hash);
        jvResult[jss::ledger_time]
            = Json::Value::UInt(lpClosed->info().closeTime.time_since_epoch().count());
        jvResult[jss::fee_ref]
                = Json::UInt (lpClosed->fees().units);
        jvResult[jss::fee_base]        = Json::UInt (lpClosed->fees().base);
        jvResult[jss::reserve_base]    = Json::UInt (lpClosed->fees().accountReserve(0).drops());
        jvResult[jss::reserve_inc]     = Json::UInt (lpClosed->fees().increment);
    }

    if ((mMode >= omSYNCING) && !isNeedNetworkLedger ())
    {
        jvResult[jss::validated_ledgers]
                = app_.getLedgerMaster ().getCompleteLedgers ();
    }

    ScopedLockType sl (mSubLock);
    return mStreamMaps[sLedger].emplace (
        isrListener->getSeq (), isrListener).second;
}

// <-- bool: true=erased, false=was not there
bool NetworkOPsImp::unsubLedger (std::uint64_t uSeq)
{
    ScopedLockType sl (mSubLock);
    return mStreamMaps[sLedger].erase (uSeq);
}

// <-- bool: true=added, false=already there
bool NetworkOPsImp::subManifests (InfoSub::ref isrListener)
{
    ScopedLockType sl (mSubLock);
    return mStreamMaps[sManifests].emplace (
        isrListener->getSeq (), isrListener).second;
}

// <-- bool: true=erased, false=was not there
bool NetworkOPsImp::unsubManifests (std::uint64_t uSeq)
{
    ScopedLockType sl (mSubLock);
    return mStreamMaps[sManifests].erase (uSeq);
}

// <-- bool: true=added, false=already there
bool NetworkOPsImp::subServer (InfoSub::ref isrListener, Json::Value& jvResult,
    bool admin)
{
    uint256 uRandom;

    if (m_standalone)
        jvResult[jss::stand_alone] = m_standalone;

    // CHECKME: is it necessary to provide a random number here?
    beast::rngfill (
        uRandom.begin(),
        uRandom.size(),
        crypto_prng());

    auto const& feeTrack = app_.getFeeTrack();
    jvResult[jss::random]          = to_string (uRandom);
    jvResult[jss::server_status]   = strOperatingMode ();
    jvResult[jss::load_base]       = feeTrack.getLoadBase ();
    jvResult[jss::load_factor]     = feeTrack.getLoadFactor ();
    jvResult [jss::hostid]         = getHostId (admin);
    jvResult[jss::pubkey_node]     = toBase58 (
        TokenType::TOKEN_NODE_PUBLIC,
        app_.nodeIdentity().first);

    ScopedLockType sl (mSubLock);
    return mStreamMaps[sServer].emplace (
        isrListener->getSeq (), isrListener).second;
}

// <-- bool: true=erased, false=was not there
bool NetworkOPsImp::unsubServer (std::uint64_t uSeq)
{
    ScopedLockType sl (mSubLock);
    return mStreamMaps[sServer].erase (uSeq);
}

// <-- bool: true=added, false=already there
bool NetworkOPsImp::subTransactions (InfoSub::ref isrListener)
{
    ScopedLockType sl (mSubLock);
    return mStreamMaps[sTransactions].emplace (
        isrListener->getSeq (), isrListener).second;
}

// <-- bool: true=erased, false=was not there
bool NetworkOPsImp::unsubTransactions (std::uint64_t uSeq)
{
    ScopedLockType sl (mSubLock);
    return mStreamMaps[sTransactions].erase (uSeq);
}

// <-- bool: true=added, false=already there
bool NetworkOPsImp::subRTTransactions (InfoSub::ref isrListener)
{
    ScopedLockType sl (mSubLock);
    return mStreamMaps[sRTTransactions].emplace (
        isrListener->getSeq (), isrListener).second;
}

// <-- bool: true=erased, false=was not there
bool NetworkOPsImp::unsubRTTransactions (std::uint64_t uSeq)
{
    ScopedLockType sl (mSubLock);
    return mStreamMaps[sRTTransactions].erase (uSeq);
}

// <-- bool: true=added, false=already there
bool NetworkOPsImp::subValidations (InfoSub::ref isrListener)
{
    ScopedLockType sl (mSubLock);
    return mStreamMaps[sValidations].emplace (
        isrListener->getSeq (), isrListener).second;
}

// <-- bool: true=erased, false=was not there
bool NetworkOPsImp::unsubValidations (std::uint64_t uSeq)
{
    ScopedLockType sl (mSubLock);
    return mStreamMaps[sValidations].erase (uSeq);
}

// <-- bool: true=added, false=already there
bool NetworkOPsImp::subPeerStatus (InfoSub::ref isrListener)
{
    ScopedLockType sl (mSubLock);
    return mStreamMaps[sPeerStatus].emplace (
        isrListener->getSeq (), isrListener).second;
}

// <-- bool: true=erased, false=was not there
bool NetworkOPsImp::unsubPeerStatus (std::uint64_t uSeq)
{
    ScopedLockType sl (mSubLock);
    return mStreamMaps[sPeerStatus].erase (uSeq);
}

InfoSub::pointer NetworkOPsImp::findRpcSub (std::string const& strUrl)
{
    ScopedLockType sl (mSubLock);

    subRpcMapType::iterator it = mRpcSubMap.find (strUrl);

    if (it != mRpcSubMap.end ())
        return it->second;

    return InfoSub::pointer ();
}

InfoSub::pointer NetworkOPsImp::addRpcSub (
    std::string const& strUrl, InfoSub::ref rspEntry)
{
    ScopedLockType sl (mSubLock);

    mRpcSubMap.emplace (strUrl, rspEntry);

    return rspEntry;
}

bool NetworkOPsImp::tryRemoveRpcSub (std::string const& strUrl)
{
    ScopedLockType sl (mSubLock);
    auto pInfo = findRpcSub(strUrl);

    if (!pInfo)
        return false;

    // check to see if any of the stream maps still hold a weak reference to
    // this entry before removing
    for (SubMapType const& map : mStreamMaps)
    {
        if (map.find(pInfo->getSeq()) != map.end())
            return false;
    }
    mRpcSubMap.erase(strUrl);
    return true;
}

#ifndef USE_NEW_BOOK_PAGE

// NIKB FIXME this should be looked at. There's no reason why this shouldn't
//            work, but it demonstrated poor performance.
//
void NetworkOPsImp::getBookPage (
    std::shared_ptr<ReadView const>& lpLedger,
    Book const& book,
    AccountID const& uTakerID,
    bool const bProof,
    unsigned int iLimit,
    Json::Value const& jvMarker,
    Json::Value& jvResult)
{ // CAUTION: This is the old get book page logic
    Json::Value& jvOffers =
            (jvResult[jss::offers] = Json::Value (Json::arrayValue));

    std::map<AccountID, STAmount> umBalance;
    const uint256   uBookBase   = getBookBase (book);
    const uint256   uBookEnd    = getQualityNext (uBookBase);
    uint256         uTipIndex   = uBookBase;

    if (auto stream = m_journal.trace())
    {
        stream << "getBookPage:" << book;
        stream << "getBookPage: uBookBase=" << uBookBase;
        stream << "getBookPage: uBookEnd=" << uBookEnd;
        stream << "getBookPage: uTipIndex=" << uTipIndex;
    }

    ReadView const& view = *lpLedger;

    bool const bGlobalFreeze =
        isGlobalFrozen(view, book.out.account) ||
            isGlobalFrozen(view, book.in.account);

    bool            bDone           = false;
    bool            bDirectAdvance  = true;

    std::shared_ptr<SLE const> sleOfferDir;
    uint256         offerIndex;
    unsigned int    uBookEntry;
    STAmount        saDirRate;

    auto const rate = transferRate(view, book.out.account);
    auto viewJ = app_.journal ("View");

    while (! bDone && iLimit-- > 0)
    {
        if (bDirectAdvance)
        {
            bDirectAdvance  = false;

            JLOG(m_journal.trace()) << "getBookPage: bDirectAdvance";

            auto const ledgerIndex = view.succ(uTipIndex, uBookEnd);
            if (ledgerIndex)
                sleOfferDir = view.read(keylet::page(*ledgerIndex));
            else
                sleOfferDir.reset();

            if (!sleOfferDir)
            {
                JLOG(m_journal.trace()) << "getBookPage: bDone";
                bDone           = true;
            }
            else
            {
                uTipIndex = sleOfferDir->key();
                saDirRate = amountFromQuality (getQuality (uTipIndex));

                cdirFirst (view,
                    uTipIndex, sleOfferDir, uBookEntry, offerIndex, viewJ);

                JLOG(m_journal.trace())
                    << "getBookPage:   uTipIndex=" << uTipIndex;
                JLOG(m_journal.trace())
                    << "getBookPage: offerIndex=" << offerIndex;
            }
        }

        if (!bDone)
        {
            auto sleOffer = view.read(keylet::offer(offerIndex));

            if (sleOffer)
            {
                auto const uOfferOwnerID =
                        sleOffer->getAccountID (sfAccount);
                auto const& saTakerGets =
                        sleOffer->getFieldAmount (sfTakerGets);
                auto const& saTakerPays =
                        sleOffer->getFieldAmount (sfTakerPays);
                STAmount saOwnerFunds;
                bool firstOwnerOffer (true);

                if (book.out.account == uOfferOwnerID)
                {
                    // If an offer is selling issuer's own IOUs, it is fully
                    // funded.
                    saOwnerFunds    = saTakerGets;
                }
                else if (bGlobalFreeze)
                {
                    // If either asset is globally frozen, consider all offers
                    // that aren't ours to be totally unfunded
                    saOwnerFunds.clear (book.out);
                }
                else
                {
                    auto umBalanceEntry  = umBalance.find (uOfferOwnerID);
                    if (umBalanceEntry != umBalance.end ())
                    {
                        // Found in running balance table.

                        saOwnerFunds    = umBalanceEntry->second;
                        firstOwnerOffer = false;
                    }
                    else
                    {
                        // Did not find balance in table.

                        saOwnerFunds = accountHolds (view,
                            uOfferOwnerID, book.out.currency,
                                book.out.account, fhZERO_IF_FROZEN, viewJ);

                        if (saOwnerFunds < zero)
                        {
                            // Treat negative funds as zero.

                            saOwnerFunds.clear ();
                        }
                    }
                }

                Json::Value jvOffer = sleOffer->getJson (0);

                STAmount saTakerGetsFunded;
                STAmount saOwnerFundsLimit = saOwnerFunds;
                Rate offerRate = parityRate;

                if (rate != parityRate
                    // Have a tranfer fee.
                    && uTakerID != book.out.account
                    // Not taking offers of own IOUs.
                    && book.out.account != uOfferOwnerID)
                    // Offer owner not issuing ownfunds
                {
                    // Need to charge a transfer fee to offer owner.
                    offerRate = rate;
                    saOwnerFundsLimit = divide (
                        saOwnerFunds, offerRate);
                }

                if (saOwnerFundsLimit >= saTakerGets)
                {
                    // Sufficient funds no shenanigans.
                    saTakerGetsFunded   = saTakerGets;
                }
                else
                {
                    // Only provide, if not fully funded.

                    saTakerGetsFunded = saOwnerFundsLimit;

                    saTakerGetsFunded.setJson (jvOffer[jss::taker_gets_funded]);
                    std::min (
                        saTakerPays, multiply (
                            saTakerGetsFunded, saDirRate, saTakerPays.issue ())).setJson
                            (jvOffer[jss::taker_pays_funded]);
                }

                STAmount saOwnerPays = (parityRate == offerRate)
                    ? saTakerGetsFunded
                    : std::min (
                        saOwnerFunds,
                        multiply (saTakerGetsFunded, offerRate));

                umBalance[uOfferOwnerID]    = saOwnerFunds - saOwnerPays;

                // Include all offers funded and unfunded
                Json::Value& jvOf = jvOffers.append (jvOffer);
                jvOf[jss::quality] = saDirRate.getText ();

                if (firstOwnerOffer)
                    jvOf[jss::owner_funds] = saOwnerFunds.getText ();
            }
            else
            {
                JLOG(m_journal.warn()) << "Missing offer";
            }

            if (! cdirNext(view,
                    uTipIndex, sleOfferDir, uBookEntry, offerIndex, viewJ))
            {
                bDirectAdvance  = true;
            }
            else
            {
                JLOG(m_journal.trace())
                    << "getBookPage: offerIndex=" << offerIndex;
            }
        }
    }

    //  jvResult[jss::marker]  = Json::Value(Json::arrayValue);
    //  jvResult[jss::nodes]   = Json::Value(Json::arrayValue);
}


#else

// This is the new code that uses the book iterators
// It has temporarily been disabled

void NetworkOPsImp::getBookPage (
    std::shared_ptr<ReadView const> lpLedger,
    Book const& book,
    AccountID const& uTakerID,
    bool const bProof,
    unsigned int iLimit,
    Json::Value const& jvMarker,
    Json::Value& jvResult)
{
    auto& jvOffers = (jvResult[jss::offers] = Json::Value (Json::arrayValue));

    std::map<AccountID, STAmount> umBalance;

    MetaView  lesActive (lpLedger, tapNONE, true);
    OrderBookIterator obIterator (lesActive, book);

    auto const rate = transferRate(lesActive, book.out.account);

    const bool bGlobalFreeze = lesActive.isGlobalFrozen (book.out.account) ||
                               lesActive.isGlobalFrozen (book.in.account);

    while (iLimit-- > 0 && obIterator.nextOffer ())
    {

        SLE::pointer    sleOffer        = obIterator.getCurrentOffer();
        if (sleOffer)
        {
            auto const uOfferOwnerID = sleOffer->getAccountID (sfAccount);
            auto const& saTakerGets = sleOffer->getFieldAmount (sfTakerGets);
            auto const& saTakerPays = sleOffer->getFieldAmount (sfTakerPays);
            STAmount saDirRate = obIterator.getCurrentRate ();
            STAmount saOwnerFunds;

            if (book.out.account == uOfferOwnerID)
            {
                // If offer is selling issuer's own IOUs, it is fully funded.
                saOwnerFunds    = saTakerGets;
            }
            else if (bGlobalFreeze)
            {
                // If either asset is globally frozen, consider all offers
                // that aren't ours to be totally unfunded
                saOwnerFunds.clear (book.out);
            }
            else
            {
                auto umBalanceEntry = umBalance.find (uOfferOwnerID);

                if (umBalanceEntry != umBalance.end ())
                {
                    // Found in running balance table.

                    saOwnerFunds    = umBalanceEntry->second;
                }
                else
                {
                    // Did not find balance in table.

                    saOwnerFunds = lesActive.accountHolds (
                        uOfferOwnerID, book.out.currency, book.out.account, fhZERO_IF_FROZEN);

                    if (saOwnerFunds.isNegative ())
                    {
                        // Treat negative funds as zero.

                        saOwnerFunds.zero ();
                    }
                }
            }

            Json::Value jvOffer = sleOffer->getJson (0);

            STAmount saTakerGetsFunded;
            STAmount saOwnerFundsLimit = saOwnerFunds;
            Rate offerRate = parityRate;

            if (rate != parityRate
                // Have a tranfer fee.
                && uTakerID != book.out.account
                // Not taking offers of own IOUs.
                && book.out.account != uOfferOwnerID)
                // Offer owner not issuing ownfunds
            {
                // Need to charge a transfer fee to offer owner.
                offerRate = rate;
                saOwnerFundsLimit = divide (saOwnerFunds, offerRate);
            }

            if (saOwnerFundsLimit >= saTakerGets)
            {
                // Sufficient funds no shenanigans.
                saTakerGetsFunded   = saTakerGets;
            }
            else
            {
                // Only provide, if not fully funded.
                saTakerGetsFunded   = saOwnerFundsLimit;

                saTakerGetsFunded.setJson (jvOffer[jss::taker_gets_funded]);

                // TOOD(tom): The result of this expression is not used - what's
                // going on here?
                std::min (saTakerPays, multiply (
                    saTakerGetsFunded, saDirRate, saTakerPays.issue ())).setJson (
                        jvOffer[jss::taker_pays_funded]);
            }

            STAmount saOwnerPays = (parityRate == offerRate)
                ? saTakerGetsFunded
                : std::min (
                    saOwnerFunds,
                    multiply (saTakerGetsFunded, offerRate));

            umBalance[uOfferOwnerID]    = saOwnerFunds - saOwnerPays;

            if (!saOwnerFunds.isZero () || uOfferOwnerID == uTakerID)
            {
                // Only provide funded offers and offers of the taker.
                Json::Value& jvOf   = jvOffers.append (jvOffer);
                jvOf[jss::quality]     = saDirRate.getText ();
            }

        }
    }

    //  jvResult[jss::marker]  = Json::Value(Json::arrayValue);
    //  jvResult[jss::nodes]   = Json::Value(Json::arrayValue);
}

#endif

//------------------------------------------------------------------------------

NetworkOPs::NetworkOPs (Stoppable& parent)
    : InfoSub::Source ("NetworkOPs", parent)
{
}

//------------------------------------------------------------------------------


void NetworkOPsImp::StateAccounting::mode (OperatingMode om)
{
    auto now = std::chrono::system_clock::now();

    std::lock_guard<std::mutex> lock (mutex_);
    ++counters_[om].transitions;
    counters_[mode_].dur += std::chrono::duration_cast<
        std::chrono::microseconds>(now - start_);

    mode_ = om;
    start_ = now;
}

Json::Value NetworkOPsImp::StateAccounting::json() const
{
    std::unique_lock<std::mutex> lock (mutex_);

    auto counters = counters_;
    auto const start = start_;
    auto const mode = mode_;

    lock.unlock();

    counters[mode].dur += std::chrono::duration_cast<
        std::chrono::microseconds>(std::chrono::system_clock::now() - start);

    Json::Value ret = Json::objectValue;

    for (std::underlying_type_t<OperatingMode> i = omDISCONNECTED;
        i <= omFULL; ++i)
    {
        ret[states_[i]] = Json::objectValue;
        auto& state = ret[states_[i]];
        state[jss::transitions] = counters[i].transitions;
        state[jss::duration_us] = std::to_string (counters[i].dur.count());
    }

    return ret;
}

//------------------------------------------------------------------------------

std::unique_ptr<NetworkOPs>
make_NetworkOPs (Application& app, NetworkOPs::clock_type& clock,
    bool standalone, std::size_t network_quorum, bool startvalid,
    JobQueue& job_queue, LedgerMaster& ledgerMaster, Stoppable& parent,
    ValidatorKeys const & validatorKeys, boost::asio::io_service& io_svc,
    beast::Journal journal)
{
    return std::make_unique<NetworkOPsImp> (app, clock, standalone,
        network_quorum, startvalid, job_queue, ledgerMaster, parent,
        validatorKeys, io_svc, journal);
}

} // ripple
