#include <xrpl/server/NetworkOPs.h>

#include <xrpld/app/consensus/RCLConsensus.h>
#include <xrpld/app/consensus/RCLCxPeerPos.h>
#include <xrpld/app/consensus/RCLValidations.h>
#include <xrpld/app/ledger/AcceptedLedger.h>
#include <xrpld/app/ledger/InboundLedger.h>
#include <xrpld/app/ledger/InboundLedgers.h>
#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/ledger/LedgerToJson.h>
#include <xrpld/app/ledger/LocalTxs.h>
#include <xrpld/app/ledger/OpenLedger.h>
#include <xrpld/app/ledger/TransactionMaster.h>
#include <xrpld/app/main/LoadManager.h>
#include <xrpld/app/main/Tuning.h>
#include <xrpld/app/misc/DeliverMax.h>
#include <xrpld/app/misc/FeeVote.h>
#include <xrpld/app/misc/Transaction.h>
#include <xrpld/app/misc/TxQ.h>
#include <xrpld/app/misc/ValidatorKeys.h>
#include <xrpld/app/misc/ValidatorList.h>
#include <xrpld/app/misc/make_NetworkOPs.h>
#include <xrpld/app/rdb/backend/SQLiteDatabase.h>
#include <xrpld/consensus/ConsensusParms.h>
#include <xrpld/consensus/ConsensusTypes.h>
#include <xrpld/core/Config.h>
#include <xrpld/core/ConfigSections.h>
#include <xrpld/overlay/Cluster.h>
#include <xrpld/overlay/ClusterNode.h>
#include <xrpld/overlay/Overlay.h>
#include <xrpld/overlay/predicates.h>
#include <xrpld/rpc/BookChanges.h>
#include <xrpld/rpc/CTID.h>
#include <xrpld/rpc/DeliveredAmount.h>
#include <xrpld/rpc/MPTokenIssuanceID.h>
#include <xrpld/rpc/ServerHandler.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/ToString.h>
#include <xrpl/basics/UnorderedContainers.h>
#include <xrpl/basics/UptimeClock.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/basics/contract.h>
#include <xrpl/basics/mulDiv.h>
#include <xrpl/basics/safe_cast.h>
#include <xrpl/basics/scope.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/clock/abstract_clock.h>
#include <xrpl/beast/insight/Collector.h>
#include <xrpl/beast/insight/Gauge.h>
#include <xrpl/beast/insight/Hook.h>
#include <xrpl/beast/net/IPEndpoint.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/beast/utility/rngfill.h>
#include <xrpl/core/ClosureCounter.h>
#include <xrpl/core/HashRouter.h>
#include <xrpl/core/Job.h>
#include <xrpl/core/NetworkIDService.h>
#include <xrpl/core/PerfLog.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/crypto/RFC1751.h>
#include <xrpl/crypto/csprng.h>
#include <xrpl/git/Git.h>
#include <xrpl/json/json_forwards.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/json_writer.h>
#include <xrpl/ledger/AcceptedLedgerTx.h>
#include <xrpl/ledger/AmendmentTable.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/CanonicalTXSet.h>
#include <xrpl/ledger/Ledger.h>
#include <xrpl/ledger/OpenView.h>
#include <xrpl/ledger/OrderBookDB.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/ApiVersion.h>
#include <xrpl/protocol/Book.h>
#include <xrpl/protocol/BuildInfo.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Fees.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/MultiApiJson.h>
#include <xrpl/protocol/NFTSyntheticSerializer.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/Rate.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/Units.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/protocol/tokens.h>
#include <xrpl/rdb/RelationalDatabase.h>
#include <xrpl/resource/Fees.h>
#include <xrpl/resource/Gossip.h>
#include <xrpl/resource/ResourceManager.h>
#include <xrpl/server/InfoSub.h>
#include <xrpl/server/LoadFeeTrack.h>
#include <xrpl/server/Manifest.h>
#include <xrpl/shamap/SHAMap.h>
#include <xrpl/tx/apply.h>

#include <boost/asio/error.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/host_name.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/system/detail/errc.hpp>
#include <boost/system/detail/error_code.hpp>
#include <boost/system/system_error.hpp>

#include <xrpl.pb.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <climits>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace xrpl {

class NetworkOPsImp final : public NetworkOPs
{
    /**
     * Transaction with input flags and results to be applied in batches.
     */

    class TransactionStatus
    {
    public:
        std::shared_ptr<Transaction> const transaction;
        bool const admin;
        bool const local;
        FailHard const failType;
        bool applied = false;
        TER result;

        TransactionStatus(std::shared_ptr<Transaction> t, bool a, bool l, FailHard f)
            : transaction(std::move(t)), admin(a), local(l), failType(f)
        {
            XRPL_ASSERT(
                local || failType == FailHard::No,
                "xrpl::NetworkOPsImp::TransactionStatus::TransactionStatus : "
                "valid inputs");
        }
    };

    /**
     * Synchronization states for transaction batches.
     */
    enum class DispatchState : unsigned char {
        None,
        Scheduled,
        Running,
    };

    static std::array<char const*, 5> const kStates;

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
            explicit Counters() = default;

            std::uint64_t transitions = 0;
            std::chrono::microseconds dur = std::chrono::microseconds(0);
        };

        OperatingMode mode_ = OperatingMode::DISCONNECTED;
        std::array<Counters, 5> counters_;
        mutable std::mutex mutex_;
        std::chrono::steady_clock::time_point start_ = std::chrono::steady_clock::now();
        std::chrono::steady_clock::time_point const processStart_ = start_;
        std::uint64_t initialSyncUs_{0};
        static std::array<json::StaticString const, 5> const kStates;

    public:
        explicit StateAccounting()
        {
            counters_[static_cast<std::size_t>(OperatingMode::DISCONNECTED)].transitions = 1;
        }

        /**
         * Record state transition. Update duration spent in previous
         * state.
         *
         * @param om New state.
         */
        void
        mode(OperatingMode om);

        /**
         * Output state counters in JSON format.
         *
         * @obj Json object to which to add state accounting data.
         */
        void
        json(json::Value& obj) const;

        struct CounterData
        {
            decltype(counters_) counters;
            decltype(mode_) mode = OperatingMode::DISCONNECTED;
            decltype(start_) start;
            decltype(initialSyncUs_) initialSyncUs{};
        };

        CounterData
        getCounterData() const
        {
            std::scoped_lock const lock(mutex_);
            return {
                .counters = counters_,
                .mode = mode_,
                .start = start_,
                .initialSyncUs = initialSyncUs_};
        }
    };

    //! Server fees published on `server` subscription
    struct ServerFeeSummary
    {
        ServerFeeSummary() = default;

        ServerFeeSummary(
            XRPAmount fee,
            TxQ::Metrics escalationMetrics,  // trivially copyable
            LoadFeeTrack const& loadFeeTrack);
        bool
        operator!=(ServerFeeSummary const& b) const;

        bool
        operator==(ServerFeeSummary const& b) const
        {
            return !(*this != b);
        }

        std::uint32_t loadFactorServer = 256;
        std::uint32_t loadBaseServer = 256;
        XRPAmount baseFee{10};
        std::optional<TxQ::Metrics> em = std::nullopt;
    };

public:
    NetworkOPsImp(
        ServiceRegistry& registry,
        NetworkOPs::clock_type& clock,
        bool standalone,
        std::size_t minPeerCount,
        bool startValid,
        JobQueue& jobQueue,
        LedgerMaster& ledgerMaster,
        ValidatorKeys const& validatorKeys,
        boost::asio::io_context& ioCtx,
        beast::Journal journal,
        beast::insight::Collector::ptr const& collector)
        : registry_(registry)
        , journal_(journal)
        , localTX_(makeLocalTxs())
        , mode_(startValid ? OperatingMode::FULL : OperatingMode::DISCONNECTED)
        , heartbeatTimer_(ioCtx)
        , clusterTimer_(ioCtx)
        , accountHistoryTxTimer_(ioCtx)
        , consensus_(
              registry_.get().getApp(),
              makeFeeVote(
                  setupFeeVote(registry_.get().getApp().config().section("voting")),
                  registry_.get().getJournal("FeeVote")),
              ledgerMaster,
              *localTX_,
              registry.getInboundTransactions(),
              beast::getAbstractClock<std::chrono::steady_clock>(),
              validatorKeys,
              registry_.get().getJournal("LedgerConsensus"))
        , validatorPK_(
              validatorKeys.keys ? validatorKeys.keys->publicKey : decltype(validatorPK_){})
        , validatorMasterPK_(
              validatorKeys.keys ? validatorKeys.keys->masterPublicKey
                                 : decltype(validatorMasterPK_){})
        , ledgerMaster_(ledgerMaster)
        , job_queue_(jobQueue)
        , standalone_(standalone)
        , minPeerCount_(startValid ? 0 : minPeerCount)
        , stats_(std::bind(&NetworkOPsImp::collectMetrics, this), collector)
    {
    }

    ~NetworkOPsImp() override
    {
        // This clear() is necessary to ensure the shared_ptrs in this map get
        // destroyed NOW because the objects in this map invoke methods on this
        // class when they are destroyed
        rpcSubMap_.clear();
    }

public:
    OperatingMode
    getOperatingMode() const override;

    std::string
    strOperatingMode(OperatingMode const mode, bool const admin) const override;

    std::string
    strOperatingMode(bool const admin = false) const override;

    //
    // Transaction operations.
    //

    // Must complete immediately.
    void
    submitTransaction(std::shared_ptr<STTx const> const&) override;

    void
    processTransaction(
        std::shared_ptr<Transaction>& transaction,
        bool bUnlimited,
        bool bLocal,
        FailHard failType) override;

    void
    processTransactionSet(CanonicalTXSet const& set) override;

    /**
     * For transactions submitted directly by a client, apply batch of
     * transactions and wait for this transaction to complete.
     *
     * @param transaction Transaction object.
     * @param bUnlimited Whether a privileged client connection submitted it.
     * @param failType fail_hard setting from transaction submission.
     */
    void
    doTransactionSync(std::shared_ptr<Transaction> transaction, bool bUnlimited, FailHard failType);

    /**
     * For transactions not submitted by a locally connected client, fire and
     * forget. Add to batch and trigger it to be processed if there's no batch
     * currently being applied.
     *
     * @param transaction Transaction object
     * @param bUnlimited Whether a privileged client connection submitted it.
     * @param failType fail_hard setting from transaction submission.
     */
    void
    doTransactionAsync(
        std::shared_ptr<Transaction> transaction,
        bool bUnlimited,
        FailHard failtype);

private:
    bool
    preProcessTransaction(std::shared_ptr<Transaction>& transaction);

    void
    doTransactionSyncBatch(
        std::unique_lock<std::mutex>& lock,
        std::function<bool(std::unique_lock<std::mutex> const&)> retryCallback);

public:
    /**
     * Apply transactions in batches. Continue until none are queued.
     */
    void
    transactionBatch();

    /**
     * Attempt to apply transactions and post-process based on the results.
     *
     * @param Lock that protects the transaction batching
     */
    void
    apply(std::unique_lock<std::mutex>& batchLock);

    //
    // Owner functions.
    //

    json::Value
    getOwnerInfo(std::shared_ptr<ReadView const> lpLedger, AccountID const& account) override;

    //
    // Book functions.
    //

    void
    getBookPage(
        std::shared_ptr<ReadView const>& lpLedger,
        Book const&,
        AccountID const& uTakerID,
        bool const bProof,
        unsigned int iLimit,
        json::Value const& jvMarker,
        json::Value& jvResult) override;

    // Ledger proposal/close functions.
    bool
    processTrustedProposal(RCLCxPeerPos proposal) override;

    bool
    recvValidation(std::shared_ptr<STValidation> const& val, std::string const& source) override;

    void
    mapComplete(std::shared_ptr<SHAMap> const& map, bool fromAcquire) override;

    // Network state machine.

    // Used for the "jump" case.
private:
    void
    switchLastClosedLedger(std::shared_ptr<Ledger const> const& newLCL);
    bool
    checkLastClosedLedger(Overlay::PeerSequence const&, uint256& networkClosed);

public:
    bool
    beginConsensus(uint256 const& networkClosed, std::unique_ptr<std::stringstream> const& clog)
        override;
    void
    endConsensus(std::unique_ptr<std::stringstream> const& clog) override;
    void
    setStandAlone() override;

    /** Called to initially start our timers.
        Not called for stand-alone mode.
    */
    void
    setStateTimer() override;

    void
    setNeedNetworkLedger() override;
    void
    clearNeedNetworkLedger() override;
    bool
    isNeedNetworkLedger() override;
    bool
    isFull() override;

    void
    setMode(OperatingMode om) override;

    bool
    isBlocked() override;
    bool
    isAmendmentBlocked() override;
    void
    setAmendmentBlocked() override;
    bool
    isAmendmentWarned() override;
    void
    setAmendmentWarned() override;
    void
    clearAmendmentWarned() override;
    bool
    isUNLBlocked() override;
    void
    setUNLBlocked() override;
    void
    clearUNLBlocked() override;
    void
    consensusViewChange() override;

    json::Value
    getConsensusInfo() override;
    json::Value
    getServerInfo(bool human, bool admin, bool counters) override;
    void
    clearLedgerFetch() override;
    json::Value
    getLedgerFetchInfo() override;
    std::uint32_t
    acceptLedger(std::optional<std::chrono::milliseconds> consensusDelay) override;
    void
    reportFeeChange() override;
    void
    reportConsensusStateChange(ConsensusPhase phase);

    void
    updateLocalTx(ReadView const& view) override;
    std::size_t
    getLocalTxCount() override;

    //
    // Monitoring: publisher side.
    //
    void
    pubLedger(std::shared_ptr<ReadView const> const& lpAccepted) override;
    void
    pubProposedTransaction(
        std::shared_ptr<ReadView const> const& ledger,
        std::shared_ptr<STTx const> const& transaction,
        TER result) override;
    void
    pubValidation(std::shared_ptr<STValidation> const& val) override;

    //--------------------------------------------------------------------------
    //
    // InfoSub::Source.
    //
    void
    subAccount(InfoSub::ref ispListener, hash_set<AccountID> const& vnaAccountIDs, bool rt)
        override;
    void
    unsubAccount(InfoSub::ref ispListener, hash_set<AccountID> const& vnaAccountIDs, bool rt)
        override;

    // Just remove the subscription from the tracking
    // not from the InfoSub. Needed for InfoSub destruction
    void
    unsubAccountInternal(std::uint64_t seq, hash_set<AccountID> const& vnaAccountIDs, bool rt)
        override;

    ErrorCodeI
    subAccountHistory(InfoSub::ref ispListener, AccountID const& account) override;
    void
    unsubAccountHistory(InfoSub::ref ispListener, AccountID const& account, bool historyOnly)
        override;

    void
    unsubAccountHistoryInternal(std::uint64_t seq, AccountID const& account, bool historyOnly)
        override;

    bool
    subLedger(InfoSub::ref ispListener, json::Value& jvResult) override;
    bool
    unsubLedger(std::uint64_t uListener) override;

    bool
    subBookChanges(InfoSub::ref ispListener) override;
    bool
    unsubBookChanges(std::uint64_t uListener) override;

    bool
    subServer(InfoSub::ref ispListener, json::Value& jvResult, bool admin) override;
    bool
    unsubServer(std::uint64_t uListener) override;

    bool
    subBook(InfoSub::ref ispListener, Book const&) override;
    bool
    unsubBook(std::uint64_t uListener, Book const&) override;

    bool
    subManifests(InfoSub::ref ispListener) override;
    bool
    unsubManifests(std::uint64_t uListener) override;
    void
    pubManifest(Manifest const&) override;

    bool
    subTransactions(InfoSub::ref ispListener) override;
    bool
    unsubTransactions(std::uint64_t uListener) override;

    bool
    subRTTransactions(InfoSub::ref ispListener) override;
    bool
    unsubRTTransactions(std::uint64_t uListener) override;

    bool
    subValidations(InfoSub::ref ispListener) override;
    bool
    unsubValidations(std::uint64_t uListener) override;

    bool
    subPeerStatus(InfoSub::ref ispListener) override;
    bool
    unsubPeerStatus(std::uint64_t uListener) override;
    void
    pubPeerStatus(std::function<json::Value(void)> const&) override;

    bool
    subConsensus(InfoSub::ref ispListener) override;
    bool
    unsubConsensus(std::uint64_t uListener) override;

    InfoSub::pointer
    findRpcSub(std::string const& strUrl) override;
    InfoSub::pointer
    addRpcSub(std::string const& strUrl, InfoSub::ref) override;
    bool
    tryRemoveRpcSub(std::string const& strUrl) override;

    void
    stop() override
    {
        {
            try
            {
                heartbeatTimer_.cancel();
            }
            catch (boost::system::system_error const& e)
            {
                JLOG(journal_.error()) << "NetworkOPs: heartbeatTimer cancel error: " << e.what();
            }

            try
            {
                clusterTimer_.cancel();
            }
            catch (boost::system::system_error const& e)
            {
                JLOG(journal_.error()) << "NetworkOPs: clusterTimer cancel error: " << e.what();
            }

            try
            {
                accountHistoryTxTimer_.cancel();
            }
            catch (boost::system::system_error const& e)
            {
                JLOG(journal_.error())
                    << "NetworkOPs: accountHistoryTxTimer cancel error: " << e.what();
            }
        }
        // Make sure that any waitHandlers pending in our timers are done.
        using namespace std::chrono_literals;
        waitHandlerCounter_.join("NetworkOPs", 1s, journal_);
    }

    void
    stateAccounting(json::Value& obj) override;

private:
    void
    setTimer(
        boost::asio::steady_timer& timer,
        std::chrono::milliseconds const& expiryTime,
        std::function<void()> onExpire,
        std::function<void()> onError);
    void
    setHeartbeatTimer();
    void
    setClusterTimer();
    void
    processHeartbeatTimer();
    void
    processClusterTimer();

    MultiApiJson
    transJson(
        std::shared_ptr<STTx const> const& transaction,
        TER result,
        bool validated,
        std::shared_ptr<ReadView const> const& ledger,
        std::optional<std::reference_wrapper<TxMeta const>> meta);

    void
    pubValidatedTransaction(
        std::shared_ptr<ReadView const> const& ledger,
        AcceptedLedgerTx const& transaction,
        bool last);

    void
    pubAccountTransaction(
        std::shared_ptr<ReadView const> const& ledger,
        AcceptedLedgerTx const& transaction,
        bool last);

    void
    pubProposedAccountTransaction(
        std::shared_ptr<ReadView const> const& ledger,
        std::shared_ptr<STTx const> const& transaction,
        TER result);

    void
    pubServer();
    void
    pubConsensus(ConsensusPhase phase);

    std::string
    getHostId(bool forAdmin);

private:
    using SubMapType = hash_map<std::uint64_t, InfoSub::wptr>;
    using SubInfoMapType = hash_map<AccountID, SubMapType>;
    using subRpcMapType = hash_map<std::string, InfoSub::pointer>;

    /*
     * With a validated ledger to separate history and future, the node
     * streams historical txns with negative indexes starting from -1,
     * and streams future txns starting from index 0.
     * The SubAccountHistoryIndex struct maintains these indexes.
     * It also has a flag stopHistorical_ for stopping streaming
     * the historical txns.
     */
    struct SubAccountHistoryIndex
    {
        AccountID const accountId;
        // forward
        std::uint32_t forwardTxIndex{0};
        // separate backward and forward
        std::uint32_t separationLedgerSeq{0};
        // history, backward
        std::uint32_t historyLastLedgerSeq{0};
        std::int32_t historyTxIndex{-1};
        bool haveHistorical{false};
        std::atomic<bool> stopHistorical{false};

        SubAccountHistoryIndex(AccountID const& accountId) : accountId(accountId)
        {
        }
    };
    struct SubAccountHistoryInfo
    {
        InfoSub::pointer sink;
        std::shared_ptr<SubAccountHistoryIndex> index;
    };
    struct SubAccountHistoryInfoWeak
    {
        InfoSub::wptr sinkWptr;
        std::shared_ptr<SubAccountHistoryIndex> index;
    };
    using SubAccountHistoryMapType =
        hash_map<AccountID, hash_map<std::uint64_t, SubAccountHistoryInfoWeak>>;

    /**
     * @note called while holding subLock_
     */
    void
    subAccountHistoryStart(
        std::shared_ptr<ReadView const> const& ledger,
        SubAccountHistoryInfoWeak& subInfo);
    void
    addAccountHistoryJob(SubAccountHistoryInfoWeak subInfo);
    void
    setAccountHistoryJobTimer(SubAccountHistoryInfoWeak subInfo);

    std::reference_wrapper<ServiceRegistry> registry_;
    beast::Journal journal_;

    std::unique_ptr<LocalTxs> localTX_;

    std::recursive_mutex subLock_;

    std::atomic<OperatingMode> mode_;

    std::atomic<bool> needNetworkLedger_{false};
    std::atomic<bool> amendmentBlocked_{false};
    std::atomic<bool> amendmentWarned_{false};
    std::atomic<bool> unlBlocked_{false};

    ClosureCounter<void, boost::system::error_code const&> waitHandlerCounter_;
    boost::asio::steady_timer heartbeatTimer_;
    boost::asio::steady_timer clusterTimer_;
    boost::asio::steady_timer accountHistoryTxTimer_;

    RCLConsensus consensus_;

    std::optional<PublicKey> const validatorPK_;
    std::optional<PublicKey> const validatorMasterPK_;

    ConsensusPhase lastConsensusPhase_{ConsensusPhase::Open};

    LedgerMaster& ledgerMaster_;

    SubInfoMapType subAccount_;
    SubInfoMapType subRTAccount_;

    subRpcMapType rpcSubMap_;

    SubAccountHistoryMapType subAccountHistory_;

    // Used as array indices; converting to enum class would require casts at ~40 call sites.
    // NOLINTNEXTLINE(cppcoreguidelines-use-enum-class)
    enum SubTypes {
        SLedger,          // Accepted ledgers.
        SManifests,       // Received validator manifests.
        SServer,          // When server changes connectivity state.
        STransactions,    // All accepted transactions.
        SRtTransactions,  // All proposed and accepted transactions.
        SValidations,     // Received validations.
        SPeerStatus,      // Peer status changes.
        SConsensusPhase,  // Consensus phase
        SBookChanges,     // Per-ledger order book changes
        SLastEntry        // Any new entry must be ADDED ABOVE this one
    };

    std::array<SubMapType, SubTypes::SLastEntry> streamMaps_;

    ServerFeeSummary lastFeeSummary_;

    JobQueue& job_queue_;

    // Whether we are in standalone mode.
    bool const standalone_;

    // The number of nodes that we need to consider ourselves connected.
    std::size_t const minPeerCount_;

    // Transaction batching.
    std::condition_variable cond_;
    std::mutex mutex_;
    DispatchState dispatchState_ = DispatchState::None;
    std::vector<TransactionStatus> transactions_;

    StateAccounting accounting_;

    std::set<uint256> pendingValidations_;
    std::mutex validationsMutex_;

private:
    struct Stats
    {
        template <class Handler>
        Stats(Handler const& handler, beast::insight::Collector::ptr const& collector)
            : hook(collector->makeHook(handler))
            , disconnected_duration(
                  collector->makeGauge("State_Accounting", "Disconnected_duration"))
            , connected_duration(collector->makeGauge("State_Accounting", "Connected_duration"))
            , syncing_duration(collector->makeGauge("State_Accounting", "Syncing_duration"))
            , tracking_duration(collector->makeGauge("State_Accounting", "Tracking_duration"))
            , full_duration(collector->makeGauge("State_Accounting", "Full_duration"))
            , disconnected_transitions(
                  collector->makeGauge("State_Accounting", "Disconnected_transitions"))
            , connected_transitions(
                  collector->makeGauge("State_Accounting", "Connected_transitions"))
            , syncing_transitions(collector->makeGauge("State_Accounting", "Syncing_transitions"))
            , tracking_transitions(collector->makeGauge("State_Accounting", "Tracking_transitions"))
            , full_transitions(collector->makeGauge("State_Accounting", "Full_transitions"))
        {
        }

        beast::insight::Hook hook;
        beast::insight::Gauge disconnected_duration;
        beast::insight::Gauge connected_duration;
        beast::insight::Gauge syncing_duration;
        beast::insight::Gauge tracking_duration;
        beast::insight::Gauge full_duration;

        beast::insight::Gauge disconnected_transitions;
        beast::insight::Gauge connected_transitions;
        beast::insight::Gauge syncing_transitions;
        beast::insight::Gauge tracking_transitions;
        beast::insight::Gauge full_transitions;
    };

    std::mutex statsMutex_;  // Mutex to lock stats_
    Stats stats_;

private:
    void
    collectMetrics();
};

//------------------------------------------------------------------------------

static std::array<char const*, 5> const kStateNames{
    {"disconnected", "connected", "syncing", "tracking", "full"}};

std::array<char const*, 5> const NetworkOPsImp::kStates = kStateNames;

std::array<json::StaticString const, 5> const NetworkOPsImp::StateAccounting::kStates = {
    {json::StaticString(kStateNames[0]),
     json::StaticString(kStateNames[1]),
     json::StaticString(kStateNames[2]),
     json::StaticString(kStateNames[3]),
     json::StaticString(kStateNames[4])}};

static auto const kGenesisAccountId =
    calcAccountID(generateKeyPair(KeyType::Secp256k1, generateSeed("masterpassphrase")).first);

//------------------------------------------------------------------------------
inline OperatingMode
NetworkOPsImp::getOperatingMode() const
{
    return mode_;
}

inline std::string
NetworkOPsImp::strOperatingMode(bool const admin /* = false */) const
{
    return strOperatingMode(mode_, admin);
}

inline void
NetworkOPsImp::setStandAlone()
{
    setMode(OperatingMode::FULL);
}

inline void
NetworkOPsImp::setNeedNetworkLedger()
{
    needNetworkLedger_ = true;
}

inline void
NetworkOPsImp::clearNeedNetworkLedger()
{
    needNetworkLedger_ = false;
}

inline bool
NetworkOPsImp::isNeedNetworkLedger()
{
    return needNetworkLedger_;
}

inline bool
NetworkOPsImp::isFull()
{
    return !needNetworkLedger_ && (mode_ == OperatingMode::FULL);
}

std::string
NetworkOPsImp::getHostId(bool forAdmin)
{
    static std::string const kHostname = boost::asio::ip::host_name();

    if (forAdmin)
        return kHostname;

    // For non-admin uses hash the node public key into a
    // single RFC1751 word:
    static std::string const kShroudedHostId = [this]() {
        auto const& id = registry_.get().getApp().nodeIdentity();

        return RFC1751::getWordFromBlob(id.first.data(), id.first.size());
    }();

    return kShroudedHostId;
}

void
NetworkOPsImp::setStateTimer()
{
    setHeartbeatTimer();

    // Only do this work if a cluster is configured
    if (registry_.get().getCluster().size() != 0)
        setClusterTimer();
}

void
NetworkOPsImp::setTimer(
    boost::asio::steady_timer& timer,
    std::chrono::milliseconds const& expiryTime,
    std::function<void()> onExpire,
    std::function<void()> onError)
{
    // Only start the timer if waitHandlerCounter_ is not yet joined.
    if (auto optionalCountedHandler =
            waitHandlerCounter_.wrap([this, onExpire, onError](boost::system::error_code const& e) {
                if ((e.value() == boost::system::errc::success) && (!job_queue_.isStopped()))
                {
                    onExpire();
                }
                // Recover as best we can if an unexpected error occurs.
                if (e.value() != boost::system::errc::success &&
                    e.value() != boost::asio::error::operation_aborted)
                {
                    // Try again later and hope for the best.
                    JLOG(journal_.error())
                        << "Timer got error '" << e.message() << "'.  Restarting timer.";
                    onError();
                }
            }))
    {
        timer.expires_after(expiryTime);
        timer.async_wait(std::move(*optionalCountedHandler));
    }
}

void
NetworkOPsImp::setHeartbeatTimer()
{
    setTimer(
        heartbeatTimer_,
        consensus_.parms().ledgerGRANULARITY,
        [this]() {
            job_queue_.addJob(JtNetopTimer, "NetHeart", [this]() { processHeartbeatTimer(); });
        },
        [this]() { setHeartbeatTimer(); });
}

void
NetworkOPsImp::setClusterTimer()
{
    using namespace std::chrono_literals;

    setTimer(
        clusterTimer_,
        10s,
        [this]() {
            job_queue_.addJob(JtNetopCluster, "NetCluster", [this]() { processClusterTimer(); });
        },
        [this]() { setClusterTimer(); });
}

void
NetworkOPsImp::setAccountHistoryJobTimer(SubAccountHistoryInfoWeak subInfo)
{
    JLOG(journal_.debug()) << "Scheduling AccountHistory job for account "
                           << toBase58(subInfo.index->accountId);
    using namespace std::chrono_literals;
    setTimer(
        accountHistoryTxTimer_,
        4s,
        [this, subInfo]() { addAccountHistoryJob(subInfo); },
        [this, subInfo]() { setAccountHistoryJobTimer(subInfo); });
}

void
NetworkOPsImp::processHeartbeatTimer()
{
    RclConsensusLogger clog("Heartbeat Timer", consensus_.validating(), journal_);
    {
        std::unique_lock lock{registry_.get().getApp().getMasterMutex()};

        // VFALCO NOTE This is for diagnosing a crash on exit
        LoadManager& mgr(registry_.get().getLoadManager());
        mgr.heartbeat();

        std::size_t const numPeers = registry_.get().getOverlay().size();

        // do we have sufficient peers? If not, we are disconnected.
        if (numPeers < minPeerCount_)
        {
            if (mode_ != OperatingMode::DISCONNECTED)
            {
                setMode(OperatingMode::DISCONNECTED);
                std::stringstream ss;
                ss << "Node count (" << numPeers << ") has fallen "
                   << "below required minimum (" << minPeerCount_ << ").";
                JLOG(journal_.warn()) << ss.str();
                CLOG(clog.ss()) << "set mode to DISCONNECTED: " << ss.str();
            }
            else
            {
                CLOG(clog.ss()) << "already DISCONNECTED. too few peers (" << numPeers
                                << "), need at least " << minPeerCount_;
            }

            // MasterMutex lock need not be held to call setHeartbeatTimer()
            lock.unlock();
            // We do not call consensus_.timerEntry until there are enough
            // peers providing meaningful inputs to consensus
            setHeartbeatTimer();

            return;
        }

        if (mode_ == OperatingMode::DISCONNECTED)
        {
            setMode(OperatingMode::CONNECTED);
            JLOG(journal_.info()) << "Node count (" << numPeers << ") is sufficient.";
            CLOG(clog.ss()) << "setting mode to CONNECTED based on " << numPeers << " peers. ";
        }

        // Check if the last validated ledger forces a change between these
        // states.
        auto origMode = mode_.load();
        CLOG(clog.ss()) << "mode: " << strOperatingMode(origMode, true);
        if (mode_ == OperatingMode::SYNCING)
        {
            setMode(OperatingMode::SYNCING);
        }
        else if (mode_ == OperatingMode::CONNECTED)
        {
            setMode(OperatingMode::CONNECTED);
        }
        auto newMode = mode_.load();
        if (origMode != newMode)
        {
            CLOG(clog.ss()) << ", changing to " << strOperatingMode(newMode, true);
        }
        CLOG(clog.ss()) << ". ";
    }

    consensus_.timerEntry(registry_.get().getTimeKeeper().closeTime(), clog.ss());

    CLOG(clog.ss()) << "consensus phase " << to_string(lastConsensusPhase_);
    ConsensusPhase const currPhase = consensus_.phase();
    if (lastConsensusPhase_ != currPhase)
    {
        reportConsensusStateChange(currPhase);
        lastConsensusPhase_ = currPhase;
        CLOG(clog.ss()) << " changed to " << to_string(lastConsensusPhase_);
    }
    CLOG(clog.ss()) << ". ";

    setHeartbeatTimer();
}

void
NetworkOPsImp::processClusterTimer()
{
    if (registry_.get().getCluster().size() == 0)
        return;

    using namespace std::chrono_literals;

    bool const update = registry_.get().getCluster().update(
        registry_.get().getApp().nodeIdentity().first,
        "",
        (ledgerMaster_.getValidatedLedgerAge() <= 4min)
            ? registry_.get().getFeeTrack().getLocalFee()
            : 0,
        registry_.get().getTimeKeeper().now());

    if (!update)
    {
        JLOG(journal_.debug()) << "Too soon to send cluster update";
        setClusterTimer();
        return;
    }

    protocol::TMCluster cluster;
    registry_.get().getCluster().forEach([&cluster](ClusterNode const& node) {
        protocol::TMClusterNode& n = *cluster.add_clusternodes();
        n.set_publickey(toBase58(TokenType::NodePublic, node.identity()));
        n.set_reporttime(node.getReportTime().time_since_epoch().count());
        n.set_nodeload(node.getLoadFee());
        if (!node.name().empty())
            n.set_nodename(node.name());
    });

    Resource::Gossip const gossip = registry_.get().getResourceManager().exportConsumers();
    for (auto& item : gossip.items)
    {
        protocol::TMLoadSource& node = *cluster.add_loadsources();
        node.set_name(to_string(item.address));
        node.set_cost(item.balance);
    }
    registry_.get().getOverlay().foreach(
        sendIf(std::make_shared<Message>(cluster, protocol::mtCLUSTER), PeerInCluster()));
    setClusterTimer();
}

//------------------------------------------------------------------------------

std::string
NetworkOPsImp::strOperatingMode(OperatingMode const mode, bool const admin) const
{
    if (mode == OperatingMode::FULL && admin)
    {
        auto const consensusMode = consensus_.mode();
        if (consensusMode != ConsensusMode::WrongLedger)
        {
            if (consensusMode == ConsensusMode::Proposing)
                return "proposing";

            if (consensus_.validating())
                return "validating";
        }
    }

    return kStates[static_cast<std::size_t>(mode)];
}

void
NetworkOPsImp::submitTransaction(std::shared_ptr<STTx const> const& iTrans)
{
    if (isNeedNetworkLedger())
    {
        // Nothing we can do if we've never been in sync
        return;
    }

    // Enforce Network bar for batch txn
    if (iTrans->isFlag(tfInnerBatchTxn) && ledgerMaster_.getValidatedRules().enabled(featureBatch))
    {
        JLOG(journal_.error()) << "Submitted transaction invalid: tfInnerBatchTxn flag present.";
        return;
    }

    // this is an asynchronous interface
    auto const trans = sterilize(*iTrans);

    auto const txid = trans->getTransactionID();
    auto const flags = registry_.get().getHashRouter().getFlags(txid);

    if ((flags & HashRouterFlags::BAD) != HashRouterFlags::UNDEFINED)
    {
        JLOG(journal_.warn()) << "Submitted transaction cached bad";
        return;
    }

    try
    {
        auto const [validity, reason] = checkValidity(
            registry_.get().getHashRouter(), *trans, ledgerMaster_.getValidatedRules());

        if (validity != Validity::Valid)
        {
            JLOG(journal_.warn()) << "Submitted transaction invalid: " << reason;
            return;
        }
    }
    catch (std::exception const& ex)
    {
        JLOG(journal_.warn()) << "Exception checking transaction " << txid << ": " << ex.what();

        return;
    }

    std::string reason;

    auto tx = std::make_shared<Transaction>(trans, reason, registry_.get().getApp());

    job_queue_.addJob(JtTransaction, "SubmitTxn", [this, tx]() {
        auto t = tx;
        processTransaction(t, false, false, FailHard::No);
    });
}

bool
NetworkOPsImp::preProcessTransaction(std::shared_ptr<Transaction>& transaction)
{
    auto const newFlags = registry_.get().getHashRouter().getFlags(transaction->getID());

    if ((newFlags & HashRouterFlags::BAD) != HashRouterFlags::UNDEFINED)
    {
        // cached bad
        JLOG(journal_.warn()) << transaction->getID() << ": cached bad!\n";
        transaction->setStatus(TransStatus::INVALID);
        transaction->setResult(temBAD_SIGNATURE);
        return false;
    }

    auto const view = ledgerMaster_.getCurrentLedger();

    // This function is called by several different parts of the codebase
    // under no circumstances will we ever accept an inner txn within a batch
    // txn from the network.
    auto const sttx = *transaction->getSTransaction();
    if (sttx.isFlag(tfInnerBatchTxn) && view->rules().enabled(featureBatch))
    {
        transaction->setStatus(TransStatus::INVALID);
        transaction->setResult(temINVALID_FLAG);
        registry_.get().getHashRouter().setFlags(transaction->getID(), HashRouterFlags::BAD);
        return false;
    }

    // NOTE ximinez - I think this check is redundant,
    // but I'm not 100% sure yet.
    // If so, only cost is looking up HashRouter flags.
    auto const [validity, reason] =
        checkValidity(registry_.get().getHashRouter(), sttx, view->rules());
    XRPL_ASSERT(
        validity == Validity::Valid, "xrpl::NetworkOPsImp::processTransaction : valid validity");

    // Not concerned with local checks at this point.
    if (validity == Validity::SigBad)
    {
        JLOG(journal_.info()) << "Transaction has bad signature: " << reason;
        transaction->setStatus(TransStatus::INVALID);
        transaction->setResult(temBAD_SIGNATURE);
        registry_.get().getHashRouter().setFlags(transaction->getID(), HashRouterFlags::BAD);
        return false;
    }

    // canonicalize can change our pointer
    registry_.get().getMasterTransaction().canonicalize(&transaction);

    return true;
}

void
NetworkOPsImp::processTransaction(
    std::shared_ptr<Transaction>& transaction,
    bool bUnlimited,
    bool bLocal,
    FailHard failType)
{
    auto ev = job_queue_.makeLoadEvent(JtTxnProc, "ProcessTXN");

    // preProcessTransaction can change our pointer
    if (!preProcessTransaction(transaction))
        return;

    if (bLocal)
    {
        doTransactionSync(transaction, bUnlimited, failType);
    }
    else
    {
        doTransactionAsync(transaction, bUnlimited, failType);
    }
}

void
NetworkOPsImp::doTransactionAsync(
    std::shared_ptr<Transaction> transaction,
    bool bUnlimited,
    FailHard failType)
{
    std::scoped_lock const lock(mutex_);

    if (transaction->getApplying())
        return;

    transactions_.emplace_back(transaction, bUnlimited, false, failType);
    transaction->setApplying();

    if (dispatchState_ == DispatchState::None)
    {
        if (job_queue_.addJob(JtBatch, "TxBatchAsync", [this]() { transactionBatch(); }))
        {
            dispatchState_ = DispatchState::Scheduled;
        }
    }
}

void
NetworkOPsImp::doTransactionSync(
    std::shared_ptr<Transaction> transaction,
    bool bUnlimited,
    FailHard failType)
{
    std::unique_lock<std::mutex> lock(mutex_);

    if (!transaction->getApplying())
    {
        transactions_.emplace_back(transaction, bUnlimited, true, failType);
        transaction->setApplying();
    }

    doTransactionSyncBatch(lock, [&transaction](std::unique_lock<std::mutex> const&) {
        return transaction->getApplying();
    });
}

void
NetworkOPsImp::doTransactionSyncBatch(
    std::unique_lock<std::mutex>& lock,
    std::function<bool(std::unique_lock<std::mutex> const&)> retryCallback)
{
    do
    {
        if (dispatchState_ == DispatchState::Running)
        {
            // A batch processing job is already running, so wait.
            cond_.wait(lock);
        }
        else
        {
            apply(lock);

            if (!transactions_.empty())
            {
                // More transactions need to be applied, but by another job.
                if (job_queue_.addJob(JtBatch, "TxBatchSync", [this]() { transactionBatch(); }))
                {
                    dispatchState_ = DispatchState::Scheduled;
                }
            }
        }
    } while (retryCallback(lock));
}

void
NetworkOPsImp::processTransactionSet(CanonicalTXSet const& set)
{
    auto ev = job_queue_.makeLoadEvent(JtTxnProc, "ProcessTXNSet");
    std::vector<std::shared_ptr<Transaction>> candidates;
    candidates.reserve(set.size());
    for (auto const& [_, tx] : set)
    {
        std::string reason;
        auto transaction = std::make_shared<Transaction>(tx, reason, registry_.get().getApp());

        if (transaction->getStatus() == TransStatus::INVALID)
        {
            if (!reason.empty())
            {
                JLOG(journal_.trace()) << "Exception checking transaction: " << reason;
            }
            registry_.get().getHashRouter().setFlags(tx->getTransactionID(), HashRouterFlags::BAD);
            continue;
        }

        // preProcessTransaction can change our pointer
        if (!preProcessTransaction(transaction))
            continue;

        candidates.emplace_back(transaction);
    }

    std::vector<TransactionStatus> transactions;
    transactions.reserve(candidates.size());

    std::unique_lock lock(mutex_);

    for (auto& transaction : candidates)
    {
        if (!transaction->getApplying())
        {
            transactions.emplace_back(transaction, false, false, FailHard::No);
            transaction->setApplying();
        }
    }

    if (transactions_.empty())
    {
        transactions_.swap(transactions);
    }
    else
    {
        transactions_.reserve(transactions_.size() + transactions.size());
        for (auto& t : transactions)
            transactions_.push_back(std::move(t));
    }
    if (transactions_.empty())
    {
        JLOG(journal_.debug()) << "No transaction to process!";
        return;
    }

    doTransactionSyncBatch(lock, [&](std::unique_lock<std::mutex> const&) {
        XRPL_ASSERT(lock.owns_lock(), "xrpl::NetworkOPsImp::processTransactionSet has lock");
        return std::ranges::any_of(
            transactions_, [](auto const& t) { return t.transaction->getApplying(); });
    });
}

void
NetworkOPsImp::transactionBatch()
{
    std::unique_lock<std::mutex> lock(mutex_);

    if (dispatchState_ == DispatchState::Running)
        return;

    while (!transactions_.empty())
    {
        apply(lock);
    }
}

void
NetworkOPsImp::apply(std::unique_lock<std::mutex>& batchLock)
{
    std::vector<TransactionStatus> submitHeld;
    std::vector<TransactionStatus> transactions;
    transactions_.swap(transactions);
    XRPL_ASSERT(!transactions.empty(), "xrpl::NetworkOPsImp::apply : non-empty transactions");
    XRPL_ASSERT(
        dispatchState_ != DispatchState::Running, "xrpl::NetworkOPsImp::apply : is not running");

    dispatchState_ = DispatchState::Running;

    batchLock.unlock();

    {
        std::unique_lock masterLock{registry_.get().getApp().getMasterMutex(), std::defer_lock};
        bool changed = false;
        {
            std::unique_lock ledgerLock{ledgerMaster_.peekMutex(), std::defer_lock};
            std::lock(masterLock, ledgerLock);
            registry_.get().getOpenLedger().modify([&](OpenView& view, beast::Journal j) {
                for (TransactionStatus& e : transactions)
                {
                    // we check before adding to the batch
                    ApplyFlags flags = TapNone;
                    if (e.admin)
                        flags |= TapUnlimited;

                    if (e.failType == FailHard::Yes)
                        flags |= TapFailHard;

                    auto const result = registry_.get().getTxQ().apply(
                        registry_.get().getApp(), view, e.transaction->getSTransaction(), flags, j);
                    e.result = result.ter;
                    e.applied = result.applied;
                    changed = changed || result.applied;
                }
                return changed;
            });
        }
        if (changed)
            reportFeeChange();

        std::optional<LedgerIndex> validatedLedgerIndex;
        if (auto const l = ledgerMaster_.getValidatedLedger())
            validatedLedgerIndex = l->header().seq;

        auto newOL = registry_.get().getOpenLedger().current();
        for (TransactionStatus const& e : transactions)
        {
            e.transaction->clearSubmitResult();

            if (e.applied)
            {
                pubProposedTransaction(newOL, e.transaction->getSTransaction(), e.result);
                e.transaction->setApplied();
            }

            e.transaction->setResult(e.result);

            if (isTemMalformed(e.result))
            {
                registry_.get().getHashRouter().setFlags(
                    e.transaction->getID(), HashRouterFlags::BAD);
            }

#ifdef DEBUG
            if (!isTesSuccess(e.result))
            {
                std::string token, human;

                if (transResultInfo(e.result, token, human))
                {
                    JLOG(journal_.info()) << "TransactionResult: " << token << ": " << human;
                }
            }
#endif

            bool const addLocal = e.local;

            if (isTesSuccess(e.result))
            {
                JLOG(journal_.debug()) << "Transaction is now included in open ledger";
                e.transaction->setStatus(TransStatus::INCLUDED);

                // Pop as many "reasonable" transactions for this account as
                // possible. "Reasonable" means they have sequential sequence
                // numbers, or use tickets.
                auto const& txCur = e.transaction->getSTransaction();

                std::size_t count = 0;
                for (auto txNext = ledgerMaster_.popAcctTransaction(txCur);
                     txNext && count < kMaxPoppedTransactions;
                     txNext = ledgerMaster_.popAcctTransaction(txCur), ++count)
                {
                    if (!batchLock.owns_lock())
                        batchLock.lock();
                    std::string reason;
                    auto const trans = sterilize(*txNext);
                    auto t = std::make_shared<Transaction>(trans, reason, registry_.get().getApp());
                    if (t->getApplying())
                        break;
                    submitHeld.emplace_back(t, false, false, FailHard::No);
                    t->setApplying();
                }
                if (batchLock.owns_lock())
                    batchLock.unlock();
            }
            else if (e.result == tefPAST_SEQ)
            {
                // duplicate or conflict
                JLOG(journal_.info()) << "Transaction is obsolete";
                e.transaction->setStatus(TransStatus::OBSOLETE);
            }
            else if (e.result == terQUEUED)
            {
                JLOG(journal_.debug()) << "Transaction is likely to claim a"
                                       << " fee, but is queued until fee drops";

                e.transaction->setStatus(TransStatus::HELD);
                // Add to held transactions, because it could get
                // kicked out of the queue, and this will try to
                // put it back.
                ledgerMaster_.addHeldTransaction(e.transaction);
                e.transaction->setQueued();
                e.transaction->setKept();
            }
            else if (isTerRetry(e.result) || isTelLocal(e.result) || isTefFailure(e.result))
            {
                if (e.failType != FailHard::Yes)
                {
                    auto const lastLedgerSeq =
                        e.transaction->getSTransaction()->at(~sfLastLedgerSequence);
                    auto const ledgersLeft = lastLedgerSeq
                        ? *lastLedgerSeq - ledgerMaster_.getCurrentLedgerIndex()
                        : std::optional<LedgerIndex>{};
                    // If any of these conditions are met, the transaction can
                    // be held:
                    // 1. It was submitted locally. (Note that this flag is only
                    //    true on the initial submission.)
                    // 2. The transaction has a LastLedgerSequence, and the
                    //    LastLedgerSequence is fewer than LocalTxs::kHoldLedgers
                    //    (5) ledgers into the future. (Remember that an
                    //    unseated optional compares as less than all seated
                    //    values, so it has to be checked explicitly first.)
                    // 3. The HashRouterFlags::BAD flag is not set on the txID.
                    // (setFlags
                    //    checks before setting. If the flag is set, it returns
                    //    false, which means it's been held once without one of
                    //    the other conditions, so don't hold it again. Time's
                    //    up!)
                    //
                    if (e.local || (ledgersLeft && ledgersLeft <= LocalTxs::kHoldLedgers) ||
                        registry_.get().getHashRouter().setFlags(
                            e.transaction->getID(), HashRouterFlags::HELD))
                    {
                        // transaction should be held
                        JLOG(journal_.debug()) << "Transaction should be held: " << e.result;
                        e.transaction->setStatus(TransStatus::HELD);
                        ledgerMaster_.addHeldTransaction(e.transaction);
                        e.transaction->setKept();
                    }
                    else
                        JLOG(journal_.debug())
                            << "Not holding transaction " << e.transaction->getID() << ": "
                            << (e.local ? "local" : "network") << ", "
                            << "result: " << e.result << " ledgers left: "
                            << (ledgersLeft ? to_string(*ledgersLeft) : "unspecified");
                }
            }
            else
            {
                JLOG(journal_.debug()) << "Status other than success " << e.result;
                e.transaction->setStatus(TransStatus::INVALID);
            }

            auto const enforceFailHard = e.failType == FailHard::Yes && !isTesSuccess(e.result);

            if (addLocal && !enforceFailHard)
            {
                localTX_->pushBack(
                    ledgerMaster_.getCurrentLedgerIndex(), e.transaction->getSTransaction());
                e.transaction->setKept();
            }

            if ((e.applied ||
                 ((mode_ != OperatingMode::FULL) && (e.failType != FailHard::Yes) && e.local) ||
                 (e.result == terQUEUED)) &&
                !enforceFailHard)
            {
                auto const toSkip =
                    registry_.get().getHashRouter().shouldRelay(e.transaction->getID());
                if (auto const sttx = *(e.transaction->getSTransaction()); toSkip &&
                    // Skip relaying if it's an inner batch txn. The flag should
                    // only be set if the Batch feature is enabled. If Batch is
                    // not enabled, the flag is always invalid, so don't relay
                    // it regardless.
                    !(sttx.isFlag(tfInnerBatchTxn)))
                {
                    protocol::TMTransaction tx;
                    Serializer s;

                    sttx.add(s);
                    tx.set_rawtransaction(s.data(), s.size());
                    tx.set_status(protocol::tsCURRENT);
                    tx.set_receivetimestamp(
                        registry_.get().getTimeKeeper().now().time_since_epoch().count());
                    tx.set_deferred(e.result == terQUEUED);
                    // FIXME: This should be when we received it
                    registry_.get().getOverlay().relay(e.transaction->getID(), tx, *toSkip);
                    e.transaction->setBroadcast();
                }
            }

            if (validatedLedgerIndex)
            {
                auto [fee, accountSeq, availableSeq] =
                    registry_.get().getTxQ().getTxRequiredFeeAndSeq(
                        *newOL, e.transaction->getSTransaction());
                e.transaction->setCurrentLedgerState(
                    *validatedLedgerIndex, fee, accountSeq, availableSeq);
            }
        }
    }

    batchLock.lock();

    for (TransactionStatus const& e : transactions)
        e.transaction->clearApplying();

    if (!submitHeld.empty())
    {
        if (transactions_.empty())
        {
            transactions_.swap(submitHeld);
        }
        else
        {
            transactions_.reserve(transactions_.size() + submitHeld.size());
            for (auto& e : submitHeld)
                transactions_.push_back(std::move(e));
        }
    }

    cond_.notify_all();

    dispatchState_ = DispatchState::None;
}

//
// Owner functions
//

json::Value
NetworkOPsImp::getOwnerInfo(std::shared_ptr<ReadView const> lpLedger, AccountID const& account)
{
    json::Value jvObjects(json::ValueType::Object);
    auto root = keylet::ownerDir(account);
    auto sleNode = lpLedger->read(keylet::page(root));
    if (sleNode)
    {
        std::uint64_t uNodeDir = 0;

        do
        {
            for (auto const& uDirEntry : sleNode->getFieldV256(sfIndexes))
            {
                auto sleCur = lpLedger->read(keylet::child(uDirEntry));
                XRPL_ASSERT(sleCur, "xrpl::NetworkOPsImp::getOwnerInfo : non-null child SLE");

                switch (sleCur->getType())
                {
                    case ltOFFER:
                        if (!jvObjects.isMember(jss::offers))
                            jvObjects[jss::offers] = json::Value(json::ValueType::Array);

                        jvObjects[jss::offers].append(sleCur->getJson(JsonOptions::Values::None));
                        break;

                    case ltRIPPLE_STATE:
                        if (!jvObjects.isMember(jss::ripple_lines))
                        {
                            jvObjects[jss::ripple_lines] = json::Value(json::ValueType::Array);
                        }

                        jvObjects[jss::ripple_lines].append(
                            sleCur->getJson(JsonOptions::Values::None));
                        break;

                    case ltACCOUNT_ROOT:
                    case ltDIR_NODE:
                    // LCOV_EXCL_START
                    default:
                        UNREACHABLE(
                            "xrpl::NetworkOPsImp::getOwnerInfo : invalid "
                            "type");
                        break;
                        // LCOV_EXCL_STOP
                }
            }

            uNodeDir = sleNode->getFieldU64(sfIndexNext);

            if (uNodeDir != 0u)
            {
                sleNode = lpLedger->read(keylet::page(root, uNodeDir));
                XRPL_ASSERT(sleNode, "xrpl::NetworkOPsImp::getOwnerInfo : read next page");
            }
        } while (uNodeDir != 0u);
    }

    return jvObjects;
}

//
// Other
//

inline bool
NetworkOPsImp::isBlocked()
{
    return isAmendmentBlocked() || isUNLBlocked();
}

inline bool
NetworkOPsImp::isAmendmentBlocked()
{
    return amendmentBlocked_;
}

void
NetworkOPsImp::setAmendmentBlocked()
{
    amendmentBlocked_ = true;
    setMode(OperatingMode::CONNECTED);
}

inline bool
NetworkOPsImp::isAmendmentWarned()
{
    return !amendmentBlocked_ && amendmentWarned_;
}

inline void
NetworkOPsImp::setAmendmentWarned()
{
    amendmentWarned_ = true;
}

inline void
NetworkOPsImp::clearAmendmentWarned()
{
    amendmentWarned_ = false;
}

inline bool
NetworkOPsImp::isUNLBlocked()
{
    return unlBlocked_;
}

void
NetworkOPsImp::setUNLBlocked()
{
    unlBlocked_ = true;
    setMode(OperatingMode::CONNECTED);
}

inline void
NetworkOPsImp::clearUNLBlocked()
{
    unlBlocked_ = false;
}

bool
NetworkOPsImp::checkLastClosedLedger(Overlay::PeerSequence const& peerList, uint256& networkClosed)
{
    // Returns true if there's an *abnormal* ledger issue, normal changing in
    // TRACKING mode should return false.  Do we have sufficient validations for
    // our last closed ledger? Or do sufficient nodes agree? And do we have no
    // better ledger available?  If so, we are either tracking or full.

    JLOG(journal_.trace()) << "NetworkOPsImp::checkLastClosedLedger";

    auto const ourClosed = ledgerMaster_.getClosedLedger();

    if (!ourClosed)
        return false;

    uint256 closedLedger = ourClosed->header().hash;
    uint256 const prevClosedLedger = ourClosed->header().parentHash;
    JLOG(journal_.trace()) << "OurClosed:  " << closedLedger;
    JLOG(journal_.trace()) << "PrevClosed: " << prevClosedLedger;

    //-------------------------------------------------------------------------
    // Determine preferred last closed ledger

    auto& validations = registry_.get().getValidations();
    JLOG(journal_.debug()) << "ValidationTrie " << json::Compact(validations.getJsonTrie());

    // Will rely on peer LCL if no trusted validations exist
    hash_map<uint256, std::uint32_t> peerCounts;
    peerCounts[closedLedger] = 0;
    if (mode_ >= OperatingMode::TRACKING)
        peerCounts[closedLedger]++;

    for (auto& peer : peerList)
    {
        uint256 const peerLedger = peer->getClosedLedgerHash();

        if (peerLedger.isNonZero())
            ++peerCounts[peerLedger];
    }

    for (auto const& it : peerCounts)
        JLOG(journal_.debug()) << "L: " << it.first << " n=" << it.second;

    uint256 const preferredLCL = validations.getPreferredLCL(
        RCLValidatedLedger{ourClosed, validations.adaptor().journal()},
        ledgerMaster_.getValidLedgerIndex(),
        peerCounts);

    bool switchLedgers = preferredLCL != closedLedger;
    if (switchLedgers)
        closedLedger = preferredLCL;
    //-------------------------------------------------------------------------
    if (switchLedgers && (closedLedger == prevClosedLedger))
    {
        // don't switch to our own previous ledger
        JLOG(journal_.info()) << "We won't switch to our own previous ledger";
        networkClosed = ourClosed->header().hash;
        switchLedgers = false;
    }
    else
    {
        networkClosed = closedLedger;
    }

    if (!switchLedgers)
        return false;

    auto consensus = ledgerMaster_.getLedgerByHash(closedLedger);

    if (!consensus)
    {
        consensus = registry_.get().getInboundLedgers().acquire(
            closedLedger, 0, InboundLedger::Reason::CONSENSUS);
    }

    if (consensus &&
        (!ledgerMaster_.canBeCurrent(consensus) ||
         !ledgerMaster_.isCompatible(*consensus, journal_.debug(), "Not switching")))
    {
        // Don't switch to a ledger not on the validated chain
        // or with an invalid close time or sequence
        networkClosed = ourClosed->header().hash;
        return false;
    }

    JLOG(journal_.warn()) << "We are not running on the consensus ledger";
    JLOG(journal_.info()) << "Our LCL: " << ourClosed->header().hash << getJson({*ourClosed, {}});
    JLOG(journal_.info()) << "Net LCL " << closedLedger;

    if ((mode_ == OperatingMode::TRACKING) || (mode_ == OperatingMode::FULL))
    {
        setMode(OperatingMode::CONNECTED);
    }

    if (consensus)
    {
        // FIXME: If this rewinds the ledger sequence, or has the same
        // sequence, we should update the status on any stored transactions
        // in the invalidated ledgers.
        switchLastClosedLedger(consensus);
    }

    return true;
}

void
NetworkOPsImp::switchLastClosedLedger(std::shared_ptr<Ledger const> const& newLCL)
{
    // set the newLCL as our last closed ledger -- this is abnormal code
    JLOG(journal_.error()) << "JUMP last closed ledger to " << newLCL->header().hash;

    clearNeedNetworkLedger();

    // Update fee computations.
    registry_.get().getTxQ().processClosedLedger(registry_.get().getApp(), *newLCL, true);

    // Caller must own master lock
    {
        // Apply tx in old open ledger to new
        // open ledger. Then apply local tx.

        auto retries = localTX_->getTxSet();
        auto const lastVal = registry_.get().getLedgerMaster().getValidatedLedger();
        std::optional<Rules> rules;
        if (lastVal)
        {
            rules = makeRulesGivenLedger(*lastVal, registry_.get().getApp().config().features);
        }
        else
        {
            rules.emplace(registry_.get().getApp().config().features);
        }
        registry_.get().getOpenLedger().accept(
            registry_.get().getApp(),
            *rules,
            newLCL,
            OrderedTxs({}),
            false,
            retries,
            TapNone,
            "jump",
            [&](OpenView& view, beast::Journal j) {
                // Stuff the ledger with transactions from the queue.
                return registry_.get().getTxQ().accept(registry_.get().getApp(), view);
            });
    }

    ledgerMaster_.switchLCL(newLCL);

    protocol::TMStatusChange s;
    s.set_newevent(protocol::neSWITCHED_LEDGER);
    s.set_ledgerseq(newLCL->header().seq);
    s.set_networktime(registry_.get().getTimeKeeper().now().time_since_epoch().count());
    s.set_ledgerhashprevious(
        newLCL->header().parentHash.begin(), newLCL->header().parentHash.size());
    s.set_ledgerhash(newLCL->header().hash.begin(), newLCL->header().hash.size());
    registry_.get().getOverlay().foreach(
        SendAlways(std::make_shared<Message>(s, protocol::mtSTATUS_CHANGE)));
}

bool
NetworkOPsImp::beginConsensus(
    uint256 const& networkClosed,
    std::unique_ptr<std::stringstream> const& clog)
{
    XRPL_ASSERT(networkClosed.isNonZero(), "xrpl::NetworkOPsImp::beginConsensus : nonzero input");

    auto closingInfo = ledgerMaster_.getCurrentLedger()->header();

    JLOG(journal_.info()) << "Consensus time for #" << closingInfo.seq << " with LCL "
                          << closingInfo.parentHash;

    auto prevLedger = ledgerMaster_.getLedgerByHash(closingInfo.parentHash);

    if (!prevLedger)
    {
        // this shouldn't happen unless we jump ledgers
        if (mode_ == OperatingMode::FULL)
        {
            JLOG(journal_.warn()) << "Don't have LCL, going to tracking";
            setMode(OperatingMode::TRACKING);
            CLOG(clog) << "beginConsensus Don't have LCL, going to tracking. ";
        }

        CLOG(clog) << "beginConsensus no previous ledger. ";
        return false;
    }

    XRPL_ASSERT(
        prevLedger->header().hash == closingInfo.parentHash,
        "xrpl::NetworkOPsImp::beginConsensus : prevLedger hash matches "
        "parent");
    XRPL_ASSERT(
        closingInfo.parentHash == ledgerMaster_.getClosedLedger()->header().hash,
        "xrpl::NetworkOPsImp::beginConsensus : closedLedger parent matches "
        "hash");

    registry_.get().getValidators().setNegativeUNL(prevLedger->negativeUNL());
    TrustChanges const changes = registry_.get().getValidators().updateTrusted(
        registry_.get().getValidations().getCurrentNodeIDs(),
        closingInfo.parentCloseTime,
        *this,
        registry_.get().getOverlay(),
        registry_.get().getHashRouter());

    if (!changes.added.empty() || !changes.removed.empty())
    {
        registry_.get().getValidations().trustChanged(changes.added, changes.removed);
        // Update the AmendmentTable so it tracks the current validators.
        registry_.get().getAmendmentTable().trustChanged(
            registry_.get().getValidators().getQuorumKeys().second);
    }

    consensus_.startRound(
        registry_.get().getTimeKeeper().closeTime(),
        networkClosed,
        prevLedger,
        changes.removed,
        changes.added,
        clog);

    ConsensusPhase const currPhase = consensus_.phase();
    if (lastConsensusPhase_ != currPhase)
    {
        reportConsensusStateChange(currPhase);
        lastConsensusPhase_ = currPhase;
    }

    JLOG(journal_.debug()) << "Initiating consensus engine";
    return true;
}

bool
NetworkOPsImp::processTrustedProposal(RCLCxPeerPos peerPos)
{
    auto const& peerKey = peerPos.publicKey();
    if (validatorPK_ == peerKey || validatorMasterPK_ == peerKey)
    {
        // Could indicate a operator misconfiguration where two nodes are
        // running with the same validator key configured, so this isn't fatal,
        // and it doesn't necessarily indicate peer misbehavior. But since this
        // is a trusted message, it could be a very big deal. Either way, we
        // don't want to relay the proposal. Note that the byzantine behavior
        // detection in handleNewValidation will notify other peers.
        //
        // Another, innocuous explanation is unusual message routing and delays,
        // causing this node to receive its own messages back.
        JLOG(journal_.error()) << "Received a proposal signed by MY KEY from a peer. This may "
                                  "indicate a misconfiguration where another node has the same "
                                  "validator key, or may be caused by unusual message routing and "
                                  "delays.";
        return false;
    }

    return consensus_.peerProposal(registry_.get().getTimeKeeper().closeTime(), peerPos);
}

void
NetworkOPsImp::mapComplete(std::shared_ptr<SHAMap> const& map, bool fromAcquire)
{
    // We now have an additional transaction set
    // Inform peers we have this set
    protocol::TMHaveTransactionSet msg;
    msg.set_hash(map->getHash().asUInt256().begin(), 256 / 8);
    msg.set_status(protocol::tsHAVE);
    registry_.get().getOverlay().foreach(
        SendAlways(std::make_shared<Message>(msg, protocol::mtHAVE_SET)));

    // We acquired it because consensus asked us to
    if (fromAcquire)
        consensus_.gotTxSet(registry_.get().getTimeKeeper().closeTime(), RCLTxSet{map});
}

void
NetworkOPsImp::endConsensus(std::unique_ptr<std::stringstream> const& clog)
{
    uint256 const deadLedger = ledgerMaster_.getClosedLedger()->header().parentHash;
    for (auto const& it : registry_.get().getOverlay().getActivePeers())
    {
        if (it && (it->getClosedLedgerHash() == deadLedger))
        {
            JLOG(journal_.trace()) << "Killing obsolete peer status";
            it->cycleStatus();
        }
    }

    uint256 networkClosed;
    bool const ledgerChange =
        checkLastClosedLedger(registry_.get().getOverlay().getActivePeers(), networkClosed);

    if (networkClosed.isZero())
    {
        CLOG(clog) << "endConsensus last closed ledger is zero. ";
        return;
    }

    // WRITEME: Unless we are in FULL and in the process of doing a consensus,
    // we must count how many nodes share our LCL, how many nodes disagree with
    // our LCL, and how many validations our LCL has. We also want to check
    // timing to make sure there shouldn't be a newer LCL. We need this
    // information to do the next three tests.

    if (((mode_ == OperatingMode::CONNECTED) || (mode_ == OperatingMode::SYNCING)) && !ledgerChange)
    {
        // Count number of peers that agree with us and UNL nodes whose
        // validations we have for LCL.  If the ledger is good enough, go to
        // TRACKING - TODO
        if (!needNetworkLedger_)
            setMode(OperatingMode::TRACKING);
    }

    if (((mode_ == OperatingMode::CONNECTED) || (mode_ == OperatingMode::TRACKING)) &&
        !ledgerChange)
    {
        // check if the ledger is good enough to go to FULL
        // Note: Do not go to FULL if we don't have the previous ledger
        // check if the ledger is bad enough to go to CONNECTED -- TODO
        auto current = ledgerMaster_.getCurrentLedger();
        if (registry_.get().getTimeKeeper().now() <
            (current->header().parentCloseTime + 2 * current->header().closeTimeResolution))
        {
            setMode(OperatingMode::FULL);
        }
    }

    beginConsensus(networkClosed, clog);
}

void
NetworkOPsImp::consensusViewChange()
{
    if ((mode_ == OperatingMode::FULL) || (mode_ == OperatingMode::TRACKING))
    {
        setMode(OperatingMode::CONNECTED);
    }
}

void
NetworkOPsImp::pubManifest(Manifest const& mo)
{
    // VFALCO consider std::shared_mutex
    std::scoped_lock const sl(subLock_);

    if (!streamMaps_[SManifests].empty())
    {
        json::Value jvObj(json::ValueType::Object);

        jvObj[jss::type] = "manifestReceived";
        jvObj[jss::master_key] = toBase58(TokenType::NodePublic, mo.masterKey);
        if (mo.signingKey)
            jvObj[jss::signing_key] = toBase58(TokenType::NodePublic, *mo.signingKey);
        jvObj[jss::seq] = json::UInt(mo.sequence);
        if (auto sig = mo.getSignature())
            jvObj[jss::signature] = strHex(*sig);
        jvObj[jss::master_signature] = strHex(mo.getMasterSignature());
        if (!mo.domain.empty())
            jvObj[jss::domain] = mo.domain;
        jvObj[jss::manifest] = strHex(mo.serialized);

        for (auto i = streamMaps_[SManifests].begin(); i != streamMaps_[SManifests].end();)
        {
            if (auto p = i->second.lock())
            {
                p->send(jvObj, true);
                ++i;
            }
            else
            {
                i = streamMaps_[SManifests].erase(i);
            }
        }
    }
}

NetworkOPsImp::ServerFeeSummary::ServerFeeSummary(
    XRPAmount fee,
    TxQ::Metrics escalationMetrics,  // trivially copyable
    LoadFeeTrack const& loadFeeTrack)
    : loadFactorServer{loadFeeTrack.getLoadFactor()}
    , loadBaseServer{loadFeeTrack.getLoadBase()}
    , baseFee{fee}
    , em{escalationMetrics}
{
}

bool
NetworkOPsImp::ServerFeeSummary::operator!=(NetworkOPsImp::ServerFeeSummary const& b) const
{
    if (loadFactorServer != b.loadFactorServer || loadBaseServer != b.loadBaseServer ||
        baseFee != b.baseFee || em.has_value() != b.em.has_value())
        return true;

    if (em && b.em)
    {
        return (
            em->minProcessingFeeLevel != b.em->minProcessingFeeLevel ||
            em->openLedgerFeeLevel != b.em->openLedgerFeeLevel ||
            em->referenceFeeLevel != b.em->referenceFeeLevel);
    }

    return false;
}

// Need to cap to uint64 to uint32 due to JSON limitations
static std::uint32_t
trunc32(std::uint64_t v)
{
    constexpr std::uint64_t kMax32 = std::numeric_limits<std::uint32_t>::max();

    return std::min(kMax32, v);
};

void
NetworkOPsImp::pubServer()
{
    // VFALCO TODO Don't hold the lock across calls to send...make a copy of the
    //             list into a local array while holding the lock then release
    //             the lock and call send on everyone.
    //
    std::scoped_lock const sl(subLock_);

    if (!streamMaps_[SServer].empty())
    {
        json::Value jvObj(json::ValueType::Object);

        ServerFeeSummary f{
            registry_.get().getOpenLedger().current()->fees().base,
            registry_.get().getTxQ().getMetrics(*registry_.get().getOpenLedger().current()),
            registry_.get().getFeeTrack()};

        jvObj[jss::type] = "serverStatus";
        jvObj[jss::server_status] = strOperatingMode();
        jvObj[jss::load_base] = f.loadBaseServer;
        jvObj[jss::load_factor_server] = f.loadFactorServer;
        jvObj[jss::base_fee] = f.baseFee.jsonClipped();

        if (f.em)
        {
            auto const loadFactor = std::max(
                safeCast<std::uint64_t>(f.loadFactorServer),
                mulDiv(f.em->openLedgerFeeLevel, f.loadBaseServer, f.em->referenceFeeLevel)
                    .value_or(xrpl::kMuldivMax));

            jvObj[jss::load_factor] = trunc32(loadFactor);
            jvObj[jss::load_factor_fee_escalation] = f.em->openLedgerFeeLevel.jsonClipped();
            jvObj[jss::load_factor_fee_queue] = f.em->minProcessingFeeLevel.jsonClipped();
            jvObj[jss::load_factor_fee_reference] = f.em->referenceFeeLevel.jsonClipped();
        }
        else
        {
            jvObj[jss::load_factor] = f.loadFactorServer;
        }

        lastFeeSummary_ = f;

        for (auto i = streamMaps_[SServer].begin(); i != streamMaps_[SServer].end();)
        {
            InfoSub::pointer const p = i->second.lock();

            // VFALCO TODO research the possibility of using thread queues and
            //             linearizing the deletion of subscribers with the
            //             sending of JSON data.
            if (p)
            {
                p->send(jvObj, true);
                ++i;
            }
            else
            {
                i = streamMaps_[SServer].erase(i);
            }
        }
    }
}

void
NetworkOPsImp::pubConsensus(ConsensusPhase phase)
{
    std::scoped_lock const sl(subLock_);

    auto& streamMap = streamMaps_[SConsensusPhase];
    if (!streamMap.empty())
    {
        json::Value jvObj(json::ValueType::Object);
        jvObj[jss::type] = "consensusPhase";
        jvObj[jss::consensus] = to_string(phase);

        for (auto i = streamMap.begin(); i != streamMap.end();)
        {
            if (auto p = i->second.lock())
            {
                p->send(jvObj, true);
                ++i;
            }
            else
            {
                i = streamMap.erase(i);
            }
        }
    }
}

void
NetworkOPsImp::pubValidation(std::shared_ptr<STValidation> const& val)
{
    // VFALCO consider std::shared_mutex
    std::scoped_lock const sl(subLock_);

    if (!streamMaps_[SValidations].empty())
    {
        json::Value jvObj(json::ValueType::Object);

        auto const signerPublic = val->getSignerPublic();

        jvObj[jss::type] = "validationReceived";
        jvObj[jss::validation_public_key] = toBase58(TokenType::NodePublic, signerPublic);
        jvObj[jss::ledger_hash] = to_string(val->getLedgerHash());
        jvObj[jss::signature] = strHex(val->getSignature());
        jvObj[jss::full] = val->isFull();
        jvObj[jss::flags] = val->getFlags();
        jvObj[jss::signing_time] = *(*val)[~sfSigningTime];
        jvObj[jss::data] = strHex(val->getSerializer().slice());
        jvObj[jss::network_id] = registry_.get().getNetworkIDService().getNetworkID();

        if (auto version = (*val)[~sfServerVersion])
            jvObj[jss::server_version] = std::to_string(*version);

        if (auto cookie = (*val)[~sfCookie])
            jvObj[jss::cookie] = std::to_string(*cookie);

        if (auto hash = (*val)[~sfValidatedHash])
            jvObj[jss::validated_hash] = strHex(*hash);

        auto const masterKey = registry_.get().getValidatorManifests().getMasterKey(signerPublic);

        if (masterKey != signerPublic)
            jvObj[jss::master_key] = toBase58(TokenType::NodePublic, masterKey);

        // NOTE *seq is a number, but old API versions used string. We replace
        // number with a string using MultiApiJson near end of this function
        if (auto const seq = (*val)[~sfLedgerSequence])
            jvObj[jss::ledger_index] = *seq;

        if (val->isFieldPresent(sfAmendments))
        {
            jvObj[jss::amendments] = json::Value(json::ValueType::Array);
            for (auto const& amendment : val->getFieldV256(sfAmendments))
                jvObj[jss::amendments].append(to_string(amendment));
        }

        if (auto const closeTime = (*val)[~sfCloseTime])
            jvObj[jss::close_time] = *closeTime;

        if (auto const loadFee = (*val)[~sfLoadFee])
            jvObj[jss::load_fee] = *loadFee;

        if (auto const baseFee = val->at(~sfBaseFee))
            jvObj[jss::base_fee] = static_cast<double>(*baseFee);

        if (auto const reserveBase = val->at(~sfReserveBase))
            jvObj[jss::reserve_base] = *reserveBase;

        if (auto const reserveInc = val->at(~sfReserveIncrement))
            jvObj[jss::reserve_inc] = *reserveInc;

        // (The ~ operator converts the Proxy to a std::optional, which
        //  simplifies later operations)
        if (auto const baseFeeXRP = ~val->at(~sfBaseFeeDrops); baseFeeXRP && baseFeeXRP->native())
            jvObj[jss::base_fee] = baseFeeXRP->xrp().jsonClipped();

        if (auto const reserveBaseXRP = ~val->at(~sfReserveBaseDrops);
            reserveBaseXRP && reserveBaseXRP->native())
            jvObj[jss::reserve_base] = reserveBaseXRP->xrp().jsonClipped();

        if (auto const reserveIncXRP = ~val->at(~sfReserveIncrementDrops);
            reserveIncXRP && reserveIncXRP->native())
            jvObj[jss::reserve_inc] = reserveIncXRP->xrp().jsonClipped();

        // NOTE Use MultiApiJson to publish two slightly different JSON objects
        // for consumers supporting different API versions
        MultiApiJson multiObj{jvObj};
        multiObj.visit(
            RPC::kApiVersion<1>,  //
            [](json::Value& jvTx) {
                // Type conversion for older API versions to string
                if (jvTx.isMember(jss::ledger_index))
                {
                    jvTx[jss::ledger_index] = std::to_string(jvTx[jss::ledger_index].asUInt());
                }
            });

        for (auto i = streamMaps_[SValidations].begin(); i != streamMaps_[SValidations].end();)
        {
            if (auto p = i->second.lock())
            {
                multiObj.visit(
                    p->getApiVersion(),  //
                    [&](json::Value const& jv) { p->send(jv, true); });
                ++i;
            }
            else
            {
                i = streamMaps_[SValidations].erase(i);
            }
        }
    }
}

void
NetworkOPsImp::pubPeerStatus(std::function<json::Value(void)> const& func)
{
    std::scoped_lock const sl(subLock_);

    if (!streamMaps_[SPeerStatus].empty())
    {
        json::Value jvObj(func());

        jvObj[jss::type] = "peerStatusChange";

        for (auto i = streamMaps_[SPeerStatus].begin(); i != streamMaps_[SPeerStatus].end();)
        {
            InfoSub::pointer const p = i->second.lock();

            if (p)
            {
                p->send(jvObj, true);
                ++i;
            }
            else
            {
                i = streamMaps_[SPeerStatus].erase(i);
            }
        }
    }
}

void
NetworkOPsImp::setMode(OperatingMode om)
{
    using namespace std::chrono_literals;
    if (om == OperatingMode::CONNECTED)
    {
        if (registry_.get().getLedgerMaster().getValidatedLedgerAge() < 1min)
            om = OperatingMode::SYNCING;
    }
    else if (om == OperatingMode::SYNCING)
    {
        if (registry_.get().getLedgerMaster().getValidatedLedgerAge() >= 1min)
            om = OperatingMode::CONNECTED;
    }

    if ((om > OperatingMode::CONNECTED) && isBlocked())
        om = OperatingMode::CONNECTED;

    if (mode_ == om)
        return;

    mode_ = om;

    accounting_.mode(om);

    JLOG(journal_.info()) << "STATE->" << strOperatingMode();
    pubServer();
}

bool
NetworkOPsImp::recvValidation(std::shared_ptr<STValidation> const& val, std::string const& source)
{
    JLOG(journal_.trace()) << "recvValidation " << val->getLedgerHash() << " from " << source;

    std::unique_lock lock(validationsMutex_);
    BypassAccept bypassAccept = BypassAccept::No;
    try
    {
        if (pendingValidations_.contains(val->getLedgerHash()))
        {
            bypassAccept = BypassAccept::Yes;
        }
        else
        {
            pendingValidations_.insert(val->getLedgerHash());
        }
        ScopeUnlock const unlock(lock);
        handleNewValidation(registry_.get().getApp(), val, source, bypassAccept, journal_);
    }
    catch (std::exception const& e)
    {
        JLOG(journal_.warn()) << "Exception thrown for handling new validation "
                              << val->getLedgerHash() << ": " << e.what();
    }
    catch (...)
    {
        JLOG(journal_.warn()) << "Unknown exception thrown for handling new validation "
                              << val->getLedgerHash();
    }
    if (bypassAccept == BypassAccept::No)
    {
        pendingValidations_.erase(val->getLedgerHash());
    }
    lock.unlock();

    pubValidation(val);

    JLOG(journal_.debug()) << [this, &val]() -> auto {
        std::stringstream ss;
        ss << "VALIDATION: " << val->render() << " master_key: ";
        auto master = registry_.get().getValidators().getTrustedKey(val->getSignerPublic());
        if (master)
        {
            ss << toBase58(TokenType::NodePublic, *master);
        }
        else
        {
            ss << "none";
        }
        return ss.str();
    }();

    // We will always relay trusted validations; if configured, we will
    // also relay all untrusted validations.
    return registry_.get().getApp().config().RELAY_UNTRUSTED_VALIDATIONS == 1 || val->isTrusted();
}

json::Value
NetworkOPsImp::getConsensusInfo()
{
    return consensus_.getJson(true);
}

json::Value
NetworkOPsImp::getServerInfo(bool human, bool admin, bool counters)
{
    json::Value info = json::ValueType::Object;

    // System-level warnings
    {
        json::Value warnings{json::ValueType::Array};
        if (isAmendmentBlocked())
        {
            json::Value& w = warnings.append(json::ValueType::Object);
            w[jss::id] = WarnRpcAmendmentBlocked;
            w[jss::message] =
                "This server is amendment blocked, and must be updated to be "
                "able to stay in sync with the network.";
        }
        if (isUNLBlocked())
        {
            json::Value& w = warnings.append(json::ValueType::Object);
            w[jss::id] = WarnRpcExpiredValidatorList;
            w[jss::message] =
                "This server has an expired validator list. validators.txt "
                "may be incorrectly configured or some [validator_list_sites] "
                "may be unreachable.";
        }
        if (admin && isAmendmentWarned())
        {
            json::Value& w = warnings.append(json::ValueType::Object);
            w[jss::id] = WarnRpcUnsupportedMajority;
            w[jss::message] =
                "One or more unsupported amendments have reached majority. "
                "Upgrade to the latest version before they are activated "
                "to avoid being amendment blocked.";
            if (auto const expected =
                    registry_.get().getAmendmentTable().firstUnsupportedExpected())
            {
                auto& d = w[jss::details] = json::ValueType::Object;
                d[jss::expected_date] = expected->time_since_epoch().count();
                d[jss::expected_date_UTC] = to_string(*expected);
            }
        }

        if (warnings.size() != 0u)
            info[jss::warnings] = std::move(warnings);
    }

    // hostid: unique string describing the machine
    if (human)
        info[jss::hostid] = getHostId(admin);

    // domain: if configured with a domain, report it:
    if (!registry_.get().getApp().config().SERVER_DOMAIN.empty())
        info[jss::server_domain] = registry_.get().getApp().config().SERVER_DOMAIN;

    info[jss::build_version] = BuildInfo::getVersionString();

    info[jss::server_state] = strOperatingMode(admin);

    info[jss::time] =
        to_string(std::chrono::floor<std::chrono::microseconds>(std::chrono::system_clock::now()));

    if (needNetworkLedger_)
        info[jss::network_ledger] = "waiting";

    info[jss::validation_quorum] =
        static_cast<json::UInt>(registry_.get().getValidators().quorum());

    if (admin)
    {
        // Note: By default the node size is "tiny". When parsing it's an error if the final
        // NODE_SIZE is over 4 so below code should be safe.
        // NOLINTNEXTLINE(bugprone-switch-missing-default-case)
        switch (registry_.get().getApp().config().NODE_SIZE)
        {
            case 0:
                info[jss::node_size] = "tiny";
                break;
            case 1:
                info[jss::node_size] = "small";
                break;
            case 2:
                info[jss::node_size] = "medium";
                break;
            case 3:
                info[jss::node_size] = "large";
                break;
            case 4:
                info[jss::node_size] = "huge";
                break;
        }

        auto when = registry_.get().getValidators().expires();

        if (!human)
        {
            if (when)
            {
                info[jss::validator_list_expires] =
                    safeCast<json::UInt>(when->time_since_epoch().count());
            }
            else
            {
                info[jss::validator_list_expires] = 0;
            }
        }
        else
        {
            auto& x = (info[jss::validator_list] = json::ValueType::Object);

            x[jss::count] = static_cast<json::UInt>(registry_.get().getValidators().count());

            if (when)
            {
                if (*when == TimeKeeper::time_point::max())
                {
                    x[jss::expiration] = "never";
                    x[jss::status] = "active";
                }
                else
                {
                    x[jss::expiration] = to_string(*when);

                    if (*when > registry_.get().getTimeKeeper().now())
                    {
                        x[jss::status] = "active";
                    }
                    else
                    {
                        x[jss::status] = "expired";
                    }
                }
            }
            else
            {
                x[jss::status] = "unknown";
                x[jss::expiration] = "unknown";
            }
        }

        if (!xrpl::git::getCommitHash().empty() || !xrpl::git::getBuildBranch().empty())
        {
            auto& x = (info[jss::git] = json::ValueType::Object);
            if (!xrpl::git::getCommitHash().empty())
                x[jss::hash] = xrpl::git::getCommitHash();
            if (!xrpl::git::getBuildBranch().empty())
                x[jss::branch] = xrpl::git::getBuildBranch();
        }
    }
    info[jss::io_latency_ms] =
        static_cast<json::UInt>(registry_.get().getApp().getIOLatency().count());

    if (admin)
    {
        if (auto const localPubKey = registry_.get().getValidators().localPublicKey();
            localPubKey && registry_.get().getApp().getValidationPublicKey())
        {
            info[jss::pubkey_validator] = toBase58(TokenType::NodePublic, localPubKey.value());
        }
        else
        {
            info[jss::pubkey_validator] = "none";
        }
    }

    if (counters)
    {
        info[jss::counters] = registry_.get().getPerfLog().countersJson();

        json::Value nodestore(json::ValueType::Object);
        registry_.get().getNodeStore().getCountsJson(nodestore);
        info[jss::counters][jss::nodestore] = nodestore;
        info[jss::current_activities] = registry_.get().getPerfLog().currentJson();
    }

    info[jss::pubkey_node] =
        toBase58(TokenType::NodePublic, registry_.get().getApp().nodeIdentity().first);

    info[jss::complete_ledgers] = registry_.get().getLedgerMaster().getCompleteLedgers();

    if (amendmentBlocked_)
        info[jss::amendment_blocked] = true;

    auto const fp = ledgerMaster_.getFetchPackCacheSize();

    if (fp != 0)
        info[jss::fetch_pack] = json::UInt(fp);

    info[jss::peers] = json::UInt(registry_.get().getOverlay().size());

    json::Value lastClose = json::ValueType::Object;
    lastClose[jss::proposers] = json::UInt(consensus_.prevProposers());

    if (human)
    {
        lastClose[jss::converge_time_s] =
            std::chrono::duration<double>{consensus_.prevRoundTime()}.count();
    }
    else
    {
        lastClose[jss::converge_time] = json::Int(consensus_.prevRoundTime().count());
    }

    info[jss::last_close] = lastClose;

    //  info[jss::consensus] = consensus_.getJson();

    if (admin)
        info[jss::load] = job_queue_.getJson();

    if (auto const netid = registry_.get().getOverlay().networkID())
        info[jss::network_id] = static_cast<json::UInt>(*netid);

    auto const escalationMetrics =
        registry_.get().getTxQ().getMetrics(*registry_.get().getOpenLedger().current());

    auto const loadFactorServer = registry_.get().getFeeTrack().getLoadFactor();
    auto const loadBaseServer = registry_.get().getFeeTrack().getLoadBase();
    /* Scale the escalated fee level to unitless "load factor".
       In practice, this just strips the units, but it will continue
       to work correctly if either base value ever changes. */
    auto const loadFactorFeeEscalation = mulDiv(
                                             escalationMetrics.openLedgerFeeLevel,
                                             loadBaseServer,
                                             escalationMetrics.referenceFeeLevel)
                                             .value_or(xrpl::kMuldivMax);

    auto const loadFactor =
        std::max(safeCast<std::uint64_t>(loadFactorServer), loadFactorFeeEscalation);

    if (!human)
    {
        info[jss::load_base] = loadBaseServer;
        info[jss::load_factor] = trunc32(loadFactor);
        info[jss::load_factor_server] = loadFactorServer;

        /* json::Value doesn't support uint64, so clamp to max
            uint32 value. This is mostly theoretical, since there
            probably isn't enough extant XRP to drive the factor
            that high.
        */
        info[jss::load_factor_fee_escalation] = escalationMetrics.openLedgerFeeLevel.jsonClipped();
        info[jss::load_factor_fee_queue] = escalationMetrics.minProcessingFeeLevel.jsonClipped();
        info[jss::load_factor_fee_reference] = escalationMetrics.referenceFeeLevel.jsonClipped();
    }
    else
    {
        info[jss::load_factor] = static_cast<double>(loadFactor) / loadBaseServer;

        if (loadFactorServer != loadFactor)
            info[jss::load_factor_server] = static_cast<double>(loadFactorServer) / loadBaseServer;

        if (admin)
        {
            std::uint32_t fee = registry_.get().getFeeTrack().getLocalFee();
            if (fee != loadBaseServer)
                info[jss::load_factor_local] = static_cast<double>(fee) / loadBaseServer;
            fee = registry_.get().getFeeTrack().getRemoteFee();
            if (fee != loadBaseServer)
                info[jss::load_factor_net] = static_cast<double>(fee) / loadBaseServer;
            fee = registry_.get().getFeeTrack().getClusterFee();
            if (fee != loadBaseServer)
                info[jss::load_factor_cluster] = static_cast<double>(fee) / loadBaseServer;
        }
        if (escalationMetrics.openLedgerFeeLevel != escalationMetrics.referenceFeeLevel &&
            (admin || loadFactorFeeEscalation != loadFactor))
        {
            info[jss::load_factor_fee_escalation] =
                escalationMetrics.openLedgerFeeLevel.decimalFromReference(
                    escalationMetrics.referenceFeeLevel);
        }
        if (escalationMetrics.minProcessingFeeLevel != escalationMetrics.referenceFeeLevel)
        {
            info[jss::load_factor_fee_queue] =
                escalationMetrics.minProcessingFeeLevel.decimalFromReference(
                    escalationMetrics.referenceFeeLevel);
        }
    }

    bool valid = false;
    auto lpClosed = ledgerMaster_.getValidatedLedger();

    if (lpClosed)
    {
        valid = true;
    }
    else
    {
        lpClosed = ledgerMaster_.getClosedLedger();
    }

    if (lpClosed)
    {
        XRPAmount const baseFee = lpClosed->fees().base;
        json::Value l(json::ValueType::Object);
        l[jss::seq] = json::UInt(lpClosed->header().seq);
        l[jss::hash] = to_string(lpClosed->header().hash);

        if (!human)
        {
            l[jss::base_fee] = baseFee.jsonClipped();
            l[jss::reserve_base] = lpClosed->fees().reserve.jsonClipped();
            l[jss::reserve_inc] = lpClosed->fees().increment.jsonClipped();
            l[jss::close_time] =
                json::Value::UInt(lpClosed->header().closeTime.time_since_epoch().count());
        }
        else
        {
            l[jss::base_fee_xrp] = baseFee.decimalXRP();
            l[jss::reserve_base_xrp] = lpClosed->fees().reserve.decimalXRP();
            l[jss::reserve_inc_xrp] = lpClosed->fees().increment.decimalXRP();

            if (auto const closeOffset = registry_.get().getTimeKeeper().closeOffset();
                std::abs(closeOffset.count()) >= 60)
                l[jss::close_time_offset] = static_cast<std::uint32_t>(closeOffset.count());

            static constexpr std::chrono::seconds kHighAgeThreshold{1000000};
            if (ledgerMaster_.haveValidated())
            {
                auto const age = ledgerMaster_.getValidatedLedgerAge();
                l[jss::age] = json::UInt(age < kHighAgeThreshold ? age.count() : 0);
            }
            else
            {
                auto lCloseTime = lpClosed->header().closeTime;
                auto closeTime = registry_.get().getTimeKeeper().closeTime();
                if (lCloseTime <= closeTime)
                {
                    using namespace std::chrono_literals;
                    auto age = closeTime - lCloseTime;
                    l[jss::age] = json::UInt(age < kHighAgeThreshold ? age.count() : 0);
                }
            }
        }

        if (valid)
        {
            info[jss::validated_ledger] = l;
        }
        else
        {
            info[jss::closed_ledger] = l;
        }

        auto lpPublished = ledgerMaster_.getPublishedLedger();
        if (!lpPublished)
        {
            info[jss::published_ledger] = "none";
        }
        else if (lpPublished->header().seq != lpClosed->header().seq)
        {
            info[jss::published_ledger] = lpPublished->header().seq;
        }
    }

    accounting_.json(info);
    info[jss::uptime] = UptimeClock::now().time_since_epoch().count();
    info[jss::jq_trans_overflow] =
        std::to_string(registry_.get().getOverlay().getJqTransOverflow());
    info[jss::peer_disconnects] = std::to_string(registry_.get().getOverlay().getPeerDisconnect());
    info[jss::peer_disconnects_resources] =
        std::to_string(registry_.get().getOverlay().getPeerDisconnectCharges());

    // This array must be sorted in increasing order.
    static constexpr std::array<std::string_view, 7> kProtocols{
        "http", "https", "peer", "ws", "ws2", "wss", "wss2"};
    static_assert(std::ranges::is_sorted(kProtocols));
    {
        json::Value ports{json::ValueType::Array};
        for (auto const& port : registry_.get().getServerHandler().setup().ports)
        {
            // Don't publish admin ports for non-admin users
            if (!admin &&
                !(port.admin_nets_v4.empty() && port.admin_nets_v6.empty() &&
                  port.admin_user.empty() && port.admin_password.empty()))
                continue;
            std::vector<std::string> proto;
            // NOLINTNEXTLINE(modernize-use-ranges)
            std::set_intersection(
                std::begin(port.protocol),
                std::end(port.protocol),
                std::begin(kProtocols),
                std::end(kProtocols),
                std::back_inserter(proto));
            if (!proto.empty())
            {
                auto& jv = ports.append(json::Value(json::ValueType::Object));
                jv[jss::port] = std::to_string(port.port);
                jv[jss::protocol] = json::Value{json::ValueType::Array};
                for (auto const& p : proto)
                    jv[jss::protocol].append(p);
            }
        }

        if (registry_.get().getApp().config().exists(SECTION_PORT_GRPC))
        {
            auto const& grpcSection = registry_.get().getApp().config().section(SECTION_PORT_GRPC);
            auto const optPort = grpcSection.get("port");
            if (optPort && grpcSection.get("ip"))
            {
                auto& jv = ports.append(json::Value(json::ValueType::Object));
                jv[jss::port] = *optPort;
                jv[jss::protocol] = json::Value{json::ValueType::Array};
                jv[jss::protocol].append("grpc");
            }
        }
        info[jss::ports] = std::move(ports);
    }

    return info;
}

void
NetworkOPsImp::clearLedgerFetch()
{
    registry_.get().getInboundLedgers().clearFailures();
}

json::Value
NetworkOPsImp::getLedgerFetchInfo()
{
    return registry_.get().getInboundLedgers().getInfo();
}

void
NetworkOPsImp::pubProposedTransaction(
    std::shared_ptr<ReadView const> const& ledger,
    std::shared_ptr<STTx const> const& transaction,
    TER result)
{
    // never publish an inner txn inside a batch txn. The flag should
    // only be set if the Batch feature is enabled. If Batch is not
    // enabled, the flag is always invalid, so don't publish it
    // regardless.
    if (transaction->isFlag(tfInnerBatchTxn))
        return;

    MultiApiJson const jvObj = transJson(transaction, result, false, ledger, std::nullopt);

    {
        std::scoped_lock const sl(subLock_);

        auto it = streamMaps_[SRtTransactions].begin();
        while (it != streamMaps_[SRtTransactions].end())
        {
            InfoSub::pointer p = it->second.lock();

            if (p)
            {
                jvObj.visit(
                    p->getApiVersion(),  //
                    [&](json::Value const& jv) { p->send(jv, true); });
                ++it;
            }
            else
            {
                it = streamMaps_[SRtTransactions].erase(it);
            }
        }
    }

    pubProposedAccountTransaction(ledger, transaction, result);
}

void
NetworkOPsImp::pubLedger(std::shared_ptr<ReadView const> const& lpAccepted)
{
    // Ledgers are published only when they acquire sufficient validations
    // Holes are filled across connection loss or other catastrophe

    std::shared_ptr<AcceptedLedger> alpAccepted =
        registry_.get().getAcceptedLedgerCache().fetch(lpAccepted->header().hash);
    if (!alpAccepted)
    {
        alpAccepted = std::make_shared<AcceptedLedger>(lpAccepted);
        registry_.get().getAcceptedLedgerCache().canonicalizeReplaceClient(
            lpAccepted->header().hash, alpAccepted);
    }

    XRPL_ASSERT(
        alpAccepted->getLedger().get() == lpAccepted.get(),
        "xrpl::NetworkOPsImp::pubLedger : accepted input");

    {
        JLOG(journal_.debug()) << "Publishing ledger " << lpAccepted->header().seq << " "
                               << lpAccepted->header().hash;

        std::scoped_lock const sl(subLock_);

        if (!streamMaps_[SLedger].empty())
        {
            json::Value jvObj(json::ValueType::Object);

            jvObj[jss::type] = "ledgerClosed";
            jvObj[jss::ledger_index] = lpAccepted->header().seq;
            jvObj[jss::ledger_hash] = to_string(lpAccepted->header().hash);
            jvObj[jss::ledger_time] =
                json::Value::UInt(lpAccepted->header().closeTime.time_since_epoch().count());

            jvObj[jss::network_id] = registry_.get().getNetworkIDService().getNetworkID();

            if (!lpAccepted->rules().enabled(featureXRPFees))
                jvObj[jss::fee_ref] = kFeeUnitsDeprecated;
            jvObj[jss::fee_base] = lpAccepted->fees().base.jsonClipped();
            jvObj[jss::reserve_base] = lpAccepted->fees().reserve.jsonClipped();
            jvObj[jss::reserve_inc] = lpAccepted->fees().increment.jsonClipped();

            jvObj[jss::txn_count] = json::UInt(alpAccepted->size());

            if (mode_ >= OperatingMode::SYNCING)
            {
                jvObj[jss::validated_ledgers] =
                    registry_.get().getLedgerMaster().getCompleteLedgers();
            }

            auto it = streamMaps_[SLedger].begin();
            while (it != streamMaps_[SLedger].end())
            {
                InfoSub::pointer const p = it->second.lock();
                if (p)
                {
                    p->send(jvObj, true);
                    ++it;
                }
                else
                {
                    it = streamMaps_[SLedger].erase(it);
                }
            }
        }

        if (!streamMaps_[SBookChanges].empty())
        {
            json::Value const jvObj = xrpl::RPC::computeBookChanges(lpAccepted);

            auto it = streamMaps_[SBookChanges].begin();
            while (it != streamMaps_[SBookChanges].end())
            {
                InfoSub::pointer const p = it->second.lock();
                if (p)
                {
                    p->send(jvObj, true);
                    ++it;
                }
                else
                {
                    it = streamMaps_[SBookChanges].erase(it);
                }
            }
        }

        {
            static bool kFirstTime = true;
            if (kFirstTime)
            {
                // First validated ledger, start delayed SubAccountHistory
                kFirstTime = false;
                for (auto& outer : subAccountHistory_)
                {
                    for (auto& inner : outer.second)
                    {
                        auto& subInfo = inner.second;
                        if (subInfo.index->separationLedgerSeq == 0)
                        {
                            subAccountHistoryStart(alpAccepted->getLedger(), subInfo);
                        }
                    }
                }
            }
        }
    }

    // Don't lock since pubAcceptedTransaction is locking.
    for (auto const& accTx : *alpAccepted)
    {
        JLOG(journal_.trace()) << "pubAccepted: " << accTx->getJson();
        pubValidatedTransaction(lpAccepted, *accTx, accTx == *(--alpAccepted->end()));
    }
}

void
NetworkOPsImp::reportFeeChange()
{
    ServerFeeSummary const f{
        registry_.get().getOpenLedger().current()->fees().base,
        registry_.get().getTxQ().getMetrics(*registry_.get().getOpenLedger().current()),
        registry_.get().getFeeTrack()};

    // only schedule the job if something has changed
    if (f != lastFeeSummary_)
    {
        job_queue_.addJob(JtClientFeeChange, "PubFee", [this]() { pubServer(); });
    }
}

void
NetworkOPsImp::reportConsensusStateChange(ConsensusPhase phase)
{
    job_queue_.addJob(JtClientConsensus, "PubCons", [this, phase]() { pubConsensus(phase); });
}

inline void
NetworkOPsImp::updateLocalTx(ReadView const& view)
{
    localTX_->sweep(view);
}
inline std::size_t
NetworkOPsImp::getLocalTxCount()
{
    return localTX_->size();
}

// This routine should only be used to publish accepted or validated
// transactions.
MultiApiJson
NetworkOPsImp::transJson(
    std::shared_ptr<STTx const> const& transaction,
    TER result,
    bool validated,
    std::shared_ptr<ReadView const> const& ledger,
    std::optional<std::reference_wrapper<TxMeta const>> meta)
{
    json::Value jvObj(json::ValueType::Object);
    std::string sToken;
    std::string sHuman;

    transResultInfo(result, sToken, sHuman);

    jvObj[jss::type] = "transaction";
    // NOTE jvObj is not a finished object for either API version. After
    // it's populated, we need to finish it for a specific API version. This is
    // done in a loop, near the end of this function.
    jvObj[jss::transaction] = transaction->getJson(JsonOptions::Values::DisableApiPriorV2, false);

    if (meta)
    {
        jvObj[jss::meta] = meta->get().getJson(JsonOptions::Values::None);
        RPC::insertDeliveredAmount(jvObj[jss::meta], *ledger, transaction, meta->get());
        RPC::insertNFTSyntheticInJson(jvObj, transaction, meta->get());
        RPC::insertMPTokenIssuanceID(jvObj[jss::meta], transaction, meta->get());
    }

    // add CTID where the needed data for it exists
    if (auto const& lookup = ledger->txRead(transaction->getTransactionID());
        lookup.second && lookup.second->isFieldPresent(sfTransactionIndex))
    {
        uint32_t const txnSeq = lookup.second->getFieldU32(sfTransactionIndex);
        uint32_t netID = registry_.get().getNetworkIDService().getNetworkID();
        if (transaction->isFieldPresent(sfNetworkID))
            netID = transaction->getFieldU32(sfNetworkID);

        if (std::optional<std::string> ctid = RPC::encodeCTID(ledger->header().seq, txnSeq, netID);
            ctid)
            jvObj[jss::ctid] = *ctid;
    }
    if (!ledger->open())
        jvObj[jss::ledger_hash] = to_string(ledger->header().hash);

    if (validated)
    {
        jvObj[jss::ledger_index] = ledger->header().seq;
        jvObj[jss::transaction][jss::date] = ledger->header().closeTime.time_since_epoch().count();
        jvObj[jss::validated] = true;
        jvObj[jss::close_time_iso] = toStringIso(ledger->header().closeTime);

        // WRITEME: Put the account next seq here
    }
    else
    {
        jvObj[jss::validated] = false;
        jvObj[jss::ledger_current_index] = ledger->header().seq;
    }

    jvObj[jss::status] = validated ? "closed" : "proposed";
    jvObj[jss::engine_result] = sToken;
    jvObj[jss::engine_result_code] = result;
    jvObj[jss::engine_result_message] = sHuman;

    if (transaction->getTxnType() == ttOFFER_CREATE)
    {
        auto const account = transaction->getAccountID(sfAccount);
        auto const amount = transaction->getFieldAmount(sfTakerGets);

        // If the offer create is not self funded then add the owner balance
        if (account != amount.getIssuer())
        {
            auto const ownerFunds = accountFunds(
                *ledger,
                account,
                amount,
                FreezeHandling::IgnoreFreeze,
                AuthHandling::IgnoreAuth,
                registry_.get().getJournal("View"));
            jvObj[jss::transaction][jss::owner_funds] = ownerFunds.getText();
        }
    }

    std::string const hash = to_string(transaction->getTransactionID());
    MultiApiJson multiObj{jvObj};
    forAllApiVersions(
        multiObj.visit(),  //
        [&]<unsigned Version>(json::Value& jvTx, std::integral_constant<unsigned, Version>) {
            RPC::insertDeliverMax(jvTx[jss::transaction], transaction->getTxnType(), Version);

            if constexpr (Version > 1)
            {
                jvTx[jss::tx_json] = jvTx.removeMember(jss::transaction);
                jvTx[jss::hash] = hash;
            }
            else
            {
                jvTx[jss::transaction][jss::hash] = hash;
            }
        });

    return multiObj;
}

void
NetworkOPsImp::pubValidatedTransaction(
    std::shared_ptr<ReadView const> const& ledger,
    AcceptedLedgerTx const& transaction,
    bool last)
{
    auto const& stTxn = transaction.getTxn();

    // Create two different Json objects, for different API versions
    auto const metaRef = std::ref(transaction.getMeta());
    auto const trResult = transaction.getResult();
    MultiApiJson const jvObj = transJson(stTxn, trResult, true, ledger, metaRef);

    {
        std::scoped_lock const sl(subLock_);

        auto it = streamMaps_[STransactions].begin();
        while (it != streamMaps_[STransactions].end())
        {
            InfoSub::pointer p = it->second.lock();

            if (p)
            {
                jvObj.visit(
                    p->getApiVersion(),  //
                    [&](json::Value const& jv) { p->send(jv, true); });
                ++it;
            }
            else
            {
                it = streamMaps_[STransactions].erase(it);
            }
        }

        it = streamMaps_[SRtTransactions].begin();

        while (it != streamMaps_[SRtTransactions].end())
        {
            InfoSub::pointer p = it->second.lock();

            if (p)
            {
                jvObj.visit(
                    p->getApiVersion(),  //
                    [&](json::Value const& jv) { p->send(jv, true); });
                ++it;
            }
            else
            {
                it = streamMaps_[SRtTransactions].erase(it);
            }
        }
    }

    if (transaction.getResult() == tesSUCCESS)
        registry_.get().getOrderBookDB().processTxn(ledger, transaction, jvObj);

    pubAccountTransaction(ledger, transaction, last);
}

void
NetworkOPsImp::pubAccountTransaction(
    std::shared_ptr<ReadView const> const& ledger,
    AcceptedLedgerTx const& transaction,
    bool last)
{
    hash_set<InfoSub::pointer> notify;
    int iProposed = 0;
    int iAccepted = 0;

    std::vector<SubAccountHistoryInfo> accountHistoryNotify;
    auto const currLedgerSeq = ledger->seq();
    {
        std::scoped_lock const sl(subLock_);

        if (!subAccount_.empty() || !subRTAccount_.empty() || !subAccountHistory_.empty())
        {
            for (auto const& affectedAccount : transaction.getAffected())
            {
                if (auto simiIt = subRTAccount_.find(affectedAccount);
                    simiIt != subRTAccount_.end())
                {
                    auto it = simiIt->second.begin();

                    while (it != simiIt->second.end())
                    {
                        InfoSub::pointer const p = it->second.lock();

                        if (p)
                        {
                            notify.insert(p);
                            ++it;
                            ++iProposed;
                        }
                        else
                        {
                            it = simiIt->second.erase(it);
                        }
                    }
                }

                if (auto simiIt = subAccount_.find(affectedAccount); simiIt != subAccount_.end())
                {
                    auto it = simiIt->second.begin();
                    while (it != simiIt->second.end())
                    {
                        InfoSub::pointer const p = it->second.lock();

                        if (p)
                        {
                            notify.insert(p);
                            ++it;
                            ++iAccepted;
                        }
                        else
                        {
                            it = simiIt->second.erase(it);
                        }
                    }
                }

                if (auto historyIt = subAccountHistory_.find(affectedAccount);
                    historyIt != subAccountHistory_.end())
                {
                    auto& subs = historyIt->second;
                    auto it = subs.begin();
                    while (it != subs.end())
                    {
                        SubAccountHistoryInfoWeak const& info = it->second;
                        if (currLedgerSeq <= info.index->separationLedgerSeq)
                        {
                            ++it;
                            continue;
                        }

                        if (auto isSptr = info.sinkWptr.lock(); isSptr)
                        {
                            accountHistoryNotify.emplace_back(
                                SubAccountHistoryInfo{.sink = isSptr, .index = info.index});
                            ++it;
                        }
                        else
                        {
                            it = subs.erase(it);
                        }
                    }
                    if (subs.empty())
                        subAccountHistory_.erase(historyIt);
                }
            }
        }
    }

    JLOG(journal_.trace()) << "pubAccountTransaction: "
                           << "proposed=" << iProposed << ", accepted=" << iAccepted;

    if (!notify.empty() || !accountHistoryNotify.empty())
    {
        auto const& stTxn = transaction.getTxn();

        // Create two different Json objects, for different API versions
        auto const metaRef = std::ref(transaction.getMeta());
        auto const trResult = transaction.getResult();
        MultiApiJson jvObj = transJson(stTxn, trResult, true, ledger, metaRef);

        for (InfoSub::ref isrListener : notify)
        {
            jvObj.visit(
                isrListener->getApiVersion(),  //
                [&](json::Value const& jv) { isrListener->send(jv, true); });
        }

        if (last)
            jvObj.set(jss::account_history_boundary, true);

        XRPL_ASSERT(
            jvObj.isMember(jss::account_history_tx_stream) == MultiApiJson::IsMemberResult::None,
            "xrpl::NetworkOPsImp::pubAccountTransaction : "
            "account_history_tx_stream not set");
        for (auto& info : accountHistoryNotify)
        {
            auto& index = info.index;
            if (index->forwardTxIndex == 0 && !index->haveHistorical)
                jvObj.set(jss::account_history_tx_first, true);

            jvObj.set(jss::account_history_tx_index, index->forwardTxIndex++);

            jvObj.visit(
                info.sink->getApiVersion(),  //
                [&](json::Value const& jv) { info.sink->send(jv, true); });
        }
    }
}

void
NetworkOPsImp::pubProposedAccountTransaction(
    std::shared_ptr<ReadView const> const& ledger,
    std::shared_ptr<STTx const> const& tx,
    TER result)
{
    hash_set<InfoSub::pointer> notify;
    int iProposed = 0;

    std::vector<SubAccountHistoryInfo> accountHistoryNotify;

    {
        std::scoped_lock const sl(subLock_);

        if (subRTAccount_.empty())
            return;

        if (!subAccount_.empty() || !subRTAccount_.empty() || !subAccountHistory_.empty())
        {
            for (auto const& affectedAccount : tx->getMentionedAccounts())
            {
                if (auto simiIt = subRTAccount_.find(affectedAccount);
                    simiIt != subRTAccount_.end())
                {
                    auto it = simiIt->second.begin();

                    while (it != simiIt->second.end())
                    {
                        InfoSub::pointer const p = it->second.lock();

                        if (p)
                        {
                            notify.insert(p);
                            ++it;
                            ++iProposed;
                        }
                        else
                        {
                            it = simiIt->second.erase(it);
                        }
                    }
                }
            }
        }
    }

    JLOG(journal_.trace()) << "pubProposedAccountTransaction: " << iProposed;

    if (!notify.empty() || !accountHistoryNotify.empty())
    {
        // Create two different Json objects, for different API versions
        MultiApiJson jvObj = transJson(tx, result, false, ledger, std::nullopt);

        for (InfoSub::ref isrListener : notify)
        {
            jvObj.visit(
                isrListener->getApiVersion(),  //
                [&](json::Value const& jv) { isrListener->send(jv, true); });
        }

        XRPL_ASSERT(
            jvObj.isMember(jss::account_history_tx_stream) == MultiApiJson::IsMemberResult::None,
            "xrpl::NetworkOPs::pubProposedAccountTransaction : "
            "account_history_tx_stream not set");
        for (auto& info : accountHistoryNotify)
        {
            auto& index = info.index;
            if (index->forwardTxIndex == 0 && !index->haveHistorical)
                jvObj.set(jss::account_history_tx_first, true);
            jvObj.set(jss::account_history_tx_index, index->forwardTxIndex++);
            jvObj.visit(
                info.sink->getApiVersion(),  //
                [&](json::Value const& jv) { info.sink->send(jv, true); });
        }
    }
}

//
// Monitoring
//

void
NetworkOPsImp::subAccount(
    InfoSub::ref isrListener,
    hash_set<AccountID> const& vnaAccountIDs,
    bool rt)
{
    SubInfoMapType& subMap = rt ? subRTAccount_ : subAccount_;

    for (auto const& naAccountID : vnaAccountIDs)
    {
        JLOG(journal_.trace()) << "subAccount: account: " << toBase58(naAccountID);

        isrListener->insertSubAccountInfo(naAccountID, rt);
    }

    std::scoped_lock const sl(subLock_);

    for (auto const& naAccountID : vnaAccountIDs)
    {
        auto simIterator = subMap.find(naAccountID);
        if (simIterator == subMap.end())
        {
            // Not found, note that account has a new single listener.
            SubMapType usisElement;
            usisElement[isrListener->getSeq()] = isrListener;
            // VFALCO NOTE This is making a needless copy of naAccountID
            subMap.insert(simIterator, make_pair(naAccountID, usisElement));
        }
        else
        {
            // Found, note that the account has another listener.
            simIterator->second[isrListener->getSeq()] = isrListener;
        }
    }
}

void
NetworkOPsImp::unsubAccount(
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
    unsubAccountInternal(isrListener->getSeq(), vnaAccountIDs, rt);
}

void
NetworkOPsImp::unsubAccountInternal(
    std::uint64_t uSeq,
    hash_set<AccountID> const& vnaAccountIDs,
    bool rt)
{
    std::scoped_lock const sl(subLock_);

    SubInfoMapType& subMap = rt ? subRTAccount_ : subAccount_;

    for (auto const& naAccountID : vnaAccountIDs)
    {
        auto simIterator = subMap.find(naAccountID);

        if (simIterator != subMap.end())
        {
            // Found
            simIterator->second.erase(uSeq);

            if (simIterator->second.empty())
            {
                // Don't need hash entry.
                subMap.erase(simIterator);
            }
        }
    }
}

void
NetworkOPsImp::addAccountHistoryJob(SubAccountHistoryInfoWeak subInfo)
{
    registry_.get().getJobQueue().addJob(JtClientAcctHist, "HistTxStream", [this, subInfo]() {
        auto const& accountId = subInfo.index->accountId;
        auto& lastLedgerSeq = subInfo.index->historyLastLedgerSeq;
        auto& txHistoryIndex = subInfo.index->historyTxIndex;

        JLOG(journal_.trace()) << "AccountHistory job for account " << toBase58(accountId)
                               << " started. lastLedgerSeq=" << lastLedgerSeq;

        auto isFirstTx = [&](std::shared_ptr<Transaction> const& tx,
                             std::shared_ptr<TxMeta> const& meta) -> bool {
            /*
             * genesis account: first tx is the one with seq 1
             * other account: first tx is the one created the account
             */
            if (accountId == kGenesisAccountId)
            {
                auto stx = tx->getSTransaction();
                if (stx->getAccountID(sfAccount) == accountId && stx->getSeqValue() == 1)
                    return true;
            }

            for (auto& node : meta->getNodes())
            {
                if (node.getFieldU16(sfLedgerEntryType) != ltACCOUNT_ROOT)
                    continue;

                if (node.isFieldPresent(sfNewFields))
                {
                    if (auto inner = dynamic_cast<STObject const*>(node.peekAtPField(sfNewFields));
                        inner)
                    {
                        if (inner->isFieldPresent(sfAccount) &&
                            inner->getAccountID(sfAccount) == accountId)
                        {
                            return true;
                        }
                    }
                }
            }

            return false;
        };

        auto send = [&](json::Value const& jvObj, bool unsubscribe) -> bool {
            if (auto sptr = subInfo.sinkWptr.lock())
            {
                sptr->send(jvObj, true);
                if (unsubscribe)
                    unsubAccountHistory(sptr, accountId, false);
                return true;
            }

            return false;
        };

        auto sendMultiApiJson = [&](MultiApiJson const& jvObj, bool unsubscribe) -> bool {
            if (auto sptr = subInfo.sinkWptr.lock())
            {
                jvObj.visit(
                    sptr->getApiVersion(),  //
                    [&](json::Value const& jv) { sptr->send(jv, true); });

                if (unsubscribe)
                    unsubAccountHistory(sptr, accountId, false);
                return true;
            }

            return false;
        };

        auto getMoreTxns = [&](std::uint32_t minLedger,
                               std::uint32_t maxLedger,
                               std::optional<RelationalDatabase::AccountTxMarker> marker)
            -> std::pair<
                RelationalDatabase::AccountTxs,
                std::optional<RelationalDatabase::AccountTxMarker>> {
            auto& db = registry_.get().getRelationalDatabase();
            RelationalDatabase::AccountTxPageOptions const options{
                .account = accountId,
                .ledgerRange = {.min = minLedger, .max = maxLedger},
                .marker = marker,
                .limit = 0,
                .bAdmin = true};
            return db.newestAccountTxPage(options);
        };

        /*
         * search backward until the genesis ledger or asked to stop
         */
        while (lastLedgerSeq >= 2 && !subInfo.index->stopHistorical)
        {
            int feeChargeCount = 0;
            if (auto sptr = subInfo.sinkWptr.lock(); sptr)
            {
                sptr->getConsumer().charge(Resource::kFeeMediumBurdenRpc);
                ++feeChargeCount;
            }
            else
            {
                JLOG(journal_.trace())
                    << "AccountHistory job for account " << toBase58(accountId)
                    << " no InfoSub. Fee charged " << feeChargeCount << " times.";
                return;
            }

            // try to search in 1024 ledgers till reaching genesis ledgers
            auto startLedgerSeq = (lastLedgerSeq > 1024 + 2 ? lastLedgerSeq - 1024 : 2);
            JLOG(journal_.trace())
                << "AccountHistory job for account " << toBase58(accountId)
                << ", working on ledger range [" << startLedgerSeq << "," << lastLedgerSeq << "]";

            auto haveRange = [&]() -> bool {
                std::uint32_t validatedMin = UINT_MAX;
                std::uint32_t validatedMax = 0;
                auto haveSomeValidatedLedgers =
                    registry_.get().getLedgerMaster().getValidatedRange(validatedMin, validatedMax);

                return haveSomeValidatedLedgers && validatedMin <= startLedgerSeq &&
                    lastLedgerSeq <= validatedMax;
            }();

            if (!haveRange)
            {
                JLOG(journal_.debug()) << "AccountHistory reschedule job for account "
                                       << toBase58(accountId) << ", incomplete ledger range ["
                                       << startLedgerSeq << "," << lastLedgerSeq << "]";
                setAccountHistoryJobTimer(subInfo);
                return;
            }

            std::optional<RelationalDatabase::AccountTxMarker> marker{};
            while (!subInfo.index->stopHistorical)
            {
                auto dbResult = getMoreTxns(startLedgerSeq, lastLedgerSeq, marker);

                auto const& txns = dbResult.first;
                marker = dbResult.second;
                size_t const numTxns = txns.size();
                for (size_t i = 0; i < numTxns; ++i)
                {
                    auto const& [tx, meta] = txns[i];

                    if (!tx || !meta)
                    {
                        JLOG(journal_.debug()) << "AccountHistory job for account "
                                               << toBase58(accountId) << " empty tx or meta.";
                        send(rpcError(RpcInternal), true);
                        return;
                    }
                    auto curTxLedger =
                        registry_.get().getLedgerMaster().getLedgerBySeq(tx->getLedger());
                    if (!curTxLedger)
                    {
                        // LCOV_EXCL_START
                        UNREACHABLE(
                            "xrpl::NetworkOPsImp::addAccountHistoryJob : "
                            "getLedgerBySeq failed");
                        JLOG(journal_.debug()) << "AccountHistory job for account "
                                               << toBase58(accountId) << " no ledger.";
                        send(rpcError(RpcInternal), true);
                        return;
                        // LCOV_EXCL_STOP
                    }
                    std::shared_ptr<STTx const> const stTxn = tx->getSTransaction();
                    if (!stTxn)
                    {
                        // LCOV_EXCL_START
                        UNREACHABLE(
                            "NetworkOPsImp::addAccountHistoryJob : "
                            "getSTransaction failed");
                        JLOG(journal_.debug()) << "AccountHistory job for account "
                                               << toBase58(accountId) << " getSTransaction failed.";
                        send(rpcError(RpcInternal), true);
                        return;
                        // LCOV_EXCL_STOP
                    }

                    auto const ref = std::ref(*meta);
                    auto const trR = meta->getResultTER();
                    MultiApiJson jvTx = transJson(stTxn, trR, true, curTxLedger, ref);

                    jvTx.set(jss::account_history_tx_index, txHistoryIndex--);
                    if (i + 1 == numTxns || txns[i + 1].first->getLedger() != tx->getLedger())
                        jvTx.set(jss::account_history_boundary, true);

                    if (isFirstTx(tx, meta))
                    {
                        jvTx.set(jss::account_history_tx_first, true);
                        sendMultiApiJson(jvTx, false);

                        JLOG(journal_.trace()) << "AccountHistory job for account "
                                               << toBase58(accountId) << " done, found last tx.";
                        return;
                    }

                    sendMultiApiJson(jvTx, false);
                }

                if (marker)
                {
                    JLOG(journal_.trace())
                        << "AccountHistory job for account " << toBase58(accountId)
                        << " paging, marker=" << marker->ledgerSeq << ":" << marker->txnSeq;
                }
                else
                {
                    break;
                }
            }

            if (!subInfo.index->stopHistorical)
            {
                lastLedgerSeq = startLedgerSeq - 1;
                if (lastLedgerSeq <= 1)
                {
                    JLOG(journal_.trace())
                        << "AccountHistory job for account " << toBase58(accountId)
                        << " done, reached genesis ledger.";
                    return;
                }
            }
        }
    });
}

void
NetworkOPsImp::subAccountHistoryStart(
    std::shared_ptr<ReadView const> const& ledger,
    SubAccountHistoryInfoWeak& subInfo)
{
    subInfo.index->separationLedgerSeq = ledger->seq();
    auto const& accountId = subInfo.index->accountId;
    auto const accountKeylet = keylet::account(accountId);
    if (!ledger->exists(accountKeylet))
    {
        JLOG(journal_.debug()) << "subAccountHistoryStart, no account " << toBase58(accountId)
                               << ", no need to add AccountHistory job.";
        return;
    }
    if (accountId == kGenesisAccountId)
    {
        if (auto const sleAcct = ledger->read(accountKeylet); sleAcct)
        {
            if (sleAcct->getFieldU32(sfSequence) == 1)
            {
                JLOG(journal_.debug())
                    << "subAccountHistoryStart, genesis account " << toBase58(accountId)
                    << " does not have tx, no need to add AccountHistory job.";
                return;
            }
        }
        else
        {
            // LCOV_EXCL_START
            UNREACHABLE(
                "xrpl::NetworkOPsImp::subAccountHistoryStart : failed to "
                "access genesis account");
            return;
            // LCOV_EXCL_STOP
        }
    }
    subInfo.index->historyLastLedgerSeq = ledger->seq();
    subInfo.index->haveHistorical = true;

    JLOG(journal_.debug()) << "subAccountHistoryStart, add AccountHistory job: accountId="
                           << toBase58(accountId) << ", currentLedgerSeq=" << ledger->seq();

    addAccountHistoryJob(subInfo);
}

ErrorCodeI
NetworkOPsImp::subAccountHistory(InfoSub::ref isrListener, AccountID const& accountId)
{
    if (!isrListener->insertSubAccountHistory(accountId))
    {
        JLOG(journal_.debug()) << "subAccountHistory, already subscribed to account "
                               << toBase58(accountId);
        return RpcInvalidParams;
    }

    std::scoped_lock const sl(subLock_);
    SubAccountHistoryInfoWeak ahi{
        .sinkWptr = isrListener, .index = std::make_shared<SubAccountHistoryIndex>(accountId)};
    auto simIterator = subAccountHistory_.find(accountId);
    if (simIterator == subAccountHistory_.end())
    {
        hash_map<std::uint64_t, SubAccountHistoryInfoWeak> inner;
        inner.emplace(isrListener->getSeq(), ahi);
        subAccountHistory_.insert(simIterator, std::make_pair(accountId, inner));
    }
    else
    {
        simIterator->second.emplace(isrListener->getSeq(), ahi);
    }

    auto const ledger = registry_.get().getLedgerMaster().getValidatedLedger();
    if (ledger)
    {
        subAccountHistoryStart(ledger, ahi);
    }
    else
    {
        // The node does not have validated ledgers, so wait for
        // one before start streaming.
        // In this case, the subscription is also considered successful.
        JLOG(journal_.debug()) << "subAccountHistory, no validated ledger yet, delay start";
    }

    return RpcSuccess;
}

void
NetworkOPsImp::unsubAccountHistory(
    InfoSub::ref isrListener,
    AccountID const& account,
    bool historyOnly)
{
    if (!historyOnly)
        isrListener->deleteSubAccountHistory(account);
    unsubAccountHistoryInternal(isrListener->getSeq(), account, historyOnly);
}

void
NetworkOPsImp::unsubAccountHistoryInternal(
    std::uint64_t seq,
    AccountID const& account,
    bool historyOnly)
{
    std::scoped_lock const sl(subLock_);
    auto simIterator = subAccountHistory_.find(account);
    if (simIterator != subAccountHistory_.end())
    {
        auto& subInfoMap = simIterator->second;
        auto subInfoIter = subInfoMap.find(seq);
        if (subInfoIter != subInfoMap.end())
        {
            subInfoIter->second.index->stopHistorical = true;
        }

        if (!historyOnly)
        {
            simIterator->second.erase(seq);
            if (simIterator->second.empty())
            {
                subAccountHistory_.erase(simIterator);
            }
        }
        JLOG(journal_.debug()) << "unsubAccountHistory, account " << toBase58(account)
                               << ", historyOnly = " << (historyOnly ? "true" : "false");
    }
}

bool
NetworkOPsImp::subBook(InfoSub::ref isrListener, Book const& book)
{
    if (auto listeners = registry_.get().getOrderBookDB().makeBookListeners(book))
    {
        listeners->addSubscriber(isrListener);
    }
    else
    {
        // LCOV_EXCL_START
        UNREACHABLE("xrpl::NetworkOPsImp::subBook : null book listeners");
        // LCOV_EXCL_STOP
    }
    return true;
}

bool
NetworkOPsImp::unsubBook(std::uint64_t uSeq, Book const& book)
{
    if (auto listeners = registry_.get().getOrderBookDB().getBookListeners(book))
        listeners->removeSubscriber(uSeq);

    return true;
}

std::uint32_t
NetworkOPsImp::acceptLedger(std::optional<std::chrono::milliseconds> consensusDelay)
{
    // This code-path is exclusively used when the server is in standalone
    // mode via `ledger_accept`
    XRPL_ASSERT(standalone_, "xrpl::NetworkOPsImp::acceptLedger : is standalone");

    if (!standalone_)
        Throw<std::runtime_error>("Operation only possible in STANDALONE mode.");

    // FIXME Could we improve on this and remove the need for a specialized
    // API in Consensus?
    beginConsensus(ledgerMaster_.getClosedLedger()->header().hash, {});
    consensus_.simulate(registry_.get().getTimeKeeper().closeTime(), consensusDelay);
    return ledgerMaster_.getCurrentLedger()->header().seq;
}

// <-- bool: true=added, false=already there
bool
NetworkOPsImp::subLedger(InfoSub::ref isrListener, json::Value& jvResult)
{
    if (auto lpClosed = ledgerMaster_.getValidatedLedger())
    {
        jvResult[jss::ledger_index] = lpClosed->header().seq;
        jvResult[jss::ledger_hash] = to_string(lpClosed->header().hash);
        jvResult[jss::ledger_time] =
            json::Value::UInt(lpClosed->header().closeTime.time_since_epoch().count());
        if (!lpClosed->rules().enabled(featureXRPFees))
            jvResult[jss::fee_ref] = kFeeUnitsDeprecated;
        jvResult[jss::fee_base] = lpClosed->fees().base.jsonClipped();
        jvResult[jss::reserve_base] = lpClosed->fees().reserve.jsonClipped();
        jvResult[jss::reserve_inc] = lpClosed->fees().increment.jsonClipped();
        jvResult[jss::network_id] = registry_.get().getNetworkIDService().getNetworkID();
    }

    if ((mode_ >= OperatingMode::SYNCING) && !isNeedNetworkLedger())
    {
        jvResult[jss::validated_ledgers] = registry_.get().getLedgerMaster().getCompleteLedgers();
    }

    std::scoped_lock const sl(subLock_);
    return streamMaps_[SLedger].emplace(isrListener->getSeq(), isrListener).second;
}

// <-- bool: true=added, false=already there
bool
NetworkOPsImp::subBookChanges(InfoSub::ref isrListener)
{
    std::scoped_lock const sl(subLock_);
    return streamMaps_[SBookChanges].emplace(isrListener->getSeq(), isrListener).second;
}

// <-- bool: true=erased, false=was not there
bool
NetworkOPsImp::unsubLedger(std::uint64_t uSeq)
{
    std::scoped_lock const sl(subLock_);
    return streamMaps_[SLedger].erase(uSeq) != 0u;
}

// <-- bool: true=erased, false=was not there
bool
NetworkOPsImp::unsubBookChanges(std::uint64_t uSeq)
{
    std::scoped_lock const sl(subLock_);
    return streamMaps_[SBookChanges].erase(uSeq) != 0u;
}

// <-- bool: true=added, false=already there
bool
NetworkOPsImp::subManifests(InfoSub::ref isrListener)
{
    std::scoped_lock const sl(subLock_);
    return streamMaps_[SManifests].emplace(isrListener->getSeq(), isrListener).second;
}

// <-- bool: true=erased, false=was not there
bool
NetworkOPsImp::unsubManifests(std::uint64_t uSeq)
{
    std::scoped_lock const sl(subLock_);
    return streamMaps_[SManifests].erase(uSeq) != 0u;
}

// <-- bool: true=added, false=already there
bool
NetworkOPsImp::subServer(InfoSub::ref isrListener, json::Value& jvResult, bool admin)
{
    uint256 uRandom;

    if (standalone_)
        jvResult[jss::stand_alone] = standalone_;

    // CHECKME: is it necessary to provide a random number here?
    beast::rngfill(uRandom.begin(), uRandom.size(), cryptoPrng());

    auto const& feeTrack = registry_.get().getFeeTrack();
    jvResult[jss::random] = to_string(uRandom);
    jvResult[jss::server_status] = strOperatingMode(admin);
    jvResult[jss::load_base] = feeTrack.getLoadBase();
    jvResult[jss::load_factor] = feeTrack.getLoadFactor();
    jvResult[jss::hostid] = getHostId(admin);
    jvResult[jss::pubkey_node] =
        toBase58(TokenType::NodePublic, registry_.get().getApp().nodeIdentity().first);

    std::scoped_lock const sl(subLock_);
    return streamMaps_[SServer].emplace(isrListener->getSeq(), isrListener).second;
}

// <-- bool: true=erased, false=was not there
bool
NetworkOPsImp::unsubServer(std::uint64_t uSeq)
{
    std::scoped_lock const sl(subLock_);
    return streamMaps_[SServer].erase(uSeq) != 0u;
}

// <-- bool: true=added, false=already there
bool
NetworkOPsImp::subTransactions(InfoSub::ref isrListener)
{
    std::scoped_lock const sl(subLock_);
    return streamMaps_[STransactions].emplace(isrListener->getSeq(), isrListener).second;
}

// <-- bool: true=erased, false=was not there
bool
NetworkOPsImp::unsubTransactions(std::uint64_t uSeq)
{
    std::scoped_lock const sl(subLock_);
    return streamMaps_[STransactions].erase(uSeq) != 0u;
}

// <-- bool: true=added, false=already there
bool
NetworkOPsImp::subRTTransactions(InfoSub::ref isrListener)
{
    std::scoped_lock const sl(subLock_);
    return streamMaps_[SRtTransactions].emplace(isrListener->getSeq(), isrListener).second;
}

// <-- bool: true=erased, false=was not there
bool
NetworkOPsImp::unsubRTTransactions(std::uint64_t uSeq)
{
    std::scoped_lock const sl(subLock_);
    return streamMaps_[SRtTransactions].erase(uSeq) != 0u;
}

// <-- bool: true=added, false=already there
bool
NetworkOPsImp::subValidations(InfoSub::ref isrListener)
{
    std::scoped_lock const sl(subLock_);
    return streamMaps_[SValidations].emplace(isrListener->getSeq(), isrListener).second;
}

void
NetworkOPsImp::stateAccounting(json::Value& obj)
{
    accounting_.json(obj);
}

// <-- bool: true=erased, false=was not there
bool
NetworkOPsImp::unsubValidations(std::uint64_t uSeq)
{
    std::scoped_lock const sl(subLock_);
    return streamMaps_[SValidations].erase(uSeq) != 0u;
}

// <-- bool: true=added, false=already there
bool
NetworkOPsImp::subPeerStatus(InfoSub::ref isrListener)
{
    std::scoped_lock const sl(subLock_);
    return streamMaps_[SPeerStatus].emplace(isrListener->getSeq(), isrListener).second;
}

// <-- bool: true=erased, false=was not there
bool
NetworkOPsImp::unsubPeerStatus(std::uint64_t uSeq)
{
    std::scoped_lock const sl(subLock_);
    return streamMaps_[SPeerStatus].erase(uSeq) != 0u;
}

// <-- bool: true=added, false=already there
bool
NetworkOPsImp::subConsensus(InfoSub::ref isrListener)
{
    std::scoped_lock const sl(subLock_);
    return streamMaps_[SConsensusPhase].emplace(isrListener->getSeq(), isrListener).second;
}

// <-- bool: true=erased, false=was not there
bool
NetworkOPsImp::unsubConsensus(std::uint64_t uSeq)
{
    std::scoped_lock const sl(subLock_);
    return streamMaps_[SConsensusPhase].erase(uSeq) != 0u;
}

InfoSub::pointer
NetworkOPsImp::findRpcSub(std::string const& strUrl)
{
    std::scoped_lock const sl(subLock_);

    subRpcMapType::iterator const it = rpcSubMap_.find(strUrl);

    if (it != rpcSubMap_.end())
        return it->second;

    return InfoSub::pointer();
}

InfoSub::pointer
NetworkOPsImp::addRpcSub(std::string const& strUrl, InfoSub::ref rspEntry)
{
    std::scoped_lock const sl(subLock_);

    rpcSubMap_.emplace(strUrl, rspEntry);

    return rspEntry;
}

bool
NetworkOPsImp::tryRemoveRpcSub(std::string const& strUrl)
{
    std::scoped_lock const sl(subLock_);
    auto pInfo = findRpcSub(strUrl);

    if (!pInfo)
        return false;

    // check to see if any of the stream maps still hold a weak reference to
    // this entry before removing
    for (SubMapType const& map : streamMaps_)
    {
        if (map.contains(pInfo->getSeq()))
            return false;
    }
    rpcSubMap_.erase(strUrl);
    return true;
}

#ifndef USE_NEW_BOOK_PAGE

// NIKB FIXME this should be looked at. There's no reason why this shouldn't
//            work, but it demonstrated poor performance.
//
void
NetworkOPsImp::getBookPage(
    std::shared_ptr<ReadView const>& lpLedger,
    Book const& book,
    AccountID const& uTakerID,
    bool const bProof,
    unsigned int iLimit,
    json::Value const& jvMarker,
    json::Value& jvResult)
{  // CAUTION: This is the old get book page logic
    json::Value& jvOffers = (jvResult[jss::offers] = json::Value(json::ValueType::Array));

    std::unordered_map<AccountID, STAmount> umBalance;
    uint256 const uBookBase = getBookBase(book);
    uint256 const uBookEnd = getQualityNext(uBookBase);
    uint256 uTipIndex = uBookBase;

    if (auto stream = journal_.trace())
    {
        stream << "getBookPage:" << book;
        stream << "getBookPage: uBookBase=" << uBookBase;
        stream << "getBookPage: uBookEnd=" << uBookEnd;
        stream << "getBookPage: uTipIndex=" << uTipIndex;
    }

    ReadView const& view = *lpLedger;

    bool const bGlobalFreeze =
        isGlobalFrozen(view, book.out.getIssuer()) || isGlobalFrozen(view, book.in.getIssuer());

    bool bDone = false;
    bool bDirectAdvance = true;

    std::shared_ptr<SLE const> sleOfferDir;
    uint256 offerIndex;
    unsigned int uBookEntry = 0;
    STAmount saDirRate;

    auto const rate = transferRate(view, book.out.getIssuer());
    auto viewJ = registry_.get().getJournal("View");

    while (!bDone && iLimit-- > 0)
    {
        if (bDirectAdvance)
        {
            bDirectAdvance = false;

            JLOG(journal_.trace()) << "getBookPage: bDirectAdvance";

            auto const ledgerIndex = view.succ(uTipIndex, uBookEnd);
            if (ledgerIndex)
            {
                sleOfferDir = view.read(keylet::page(*ledgerIndex));
            }
            else
            {
                sleOfferDir.reset();
            }

            if (!sleOfferDir)
            {
                JLOG(journal_.trace()) << "getBookPage: bDone";
                bDone = true;
            }
            else
            {
                uTipIndex = sleOfferDir->key();
                saDirRate = amountFromQuality(getQuality(uTipIndex));

                cdirFirst(view, uTipIndex, sleOfferDir, uBookEntry, offerIndex);

                JLOG(journal_.trace()) << "getBookPage:   uTipIndex=" << uTipIndex;
                JLOG(journal_.trace()) << "getBookPage: offerIndex=" << offerIndex;
            }
        }

        if (!bDone)
        {
            auto sleOffer = view.read(keylet::offer(offerIndex));

            if (sleOffer)
            {
                auto const uOfferOwnerID = sleOffer->getAccountID(sfAccount);
                auto const& saTakerGets = sleOffer->getFieldAmount(sfTakerGets);
                auto const& saTakerPays = sleOffer->getFieldAmount(sfTakerPays);
                STAmount saOwnerFunds;
                bool firstOwnerOffer(true);

                if (book.out.getIssuer() == uOfferOwnerID)
                {
                    // If an offer is selling issuer's own IOUs, it is fully
                    // funded.
                    saOwnerFunds = saTakerGets;
                }
                else if (bGlobalFreeze)
                {
                    // If either asset is globally frozen, consider all offers
                    // that aren't ours to be totally unfunded
                    saOwnerFunds.clear(book.out);
                }
                else
                {
                    auto umBalanceEntry = umBalance.find(uOfferOwnerID);
                    if (umBalanceEntry != umBalance.end())
                    {
                        // Found in running balance table.

                        saOwnerFunds = umBalanceEntry->second;
                        firstOwnerOffer = false;
                    }
                    else
                    {
                        // Did not find balance in table.

                        saOwnerFunds = accountHolds(
                            view,
                            uOfferOwnerID,
                            book.out,
                            FreezeHandling::ZeroIfFrozen,
                            AuthHandling::ZeroIfUnauthorized,
                            viewJ);

                        if (saOwnerFunds < beast::kZero)
                        {
                            // Treat negative funds as zero.

                            saOwnerFunds.clear();
                        }
                    }
                }

                json::Value jvOffer = sleOffer->getJson(JsonOptions::Values::None);

                STAmount saTakerGetsFunded;
                STAmount saOwnerFundsLimit = saOwnerFunds;
                Rate offerRate = kParityRate;

                if (rate != kParityRate
                    // Have a transfer fee.
                    && uTakerID != book.out.getIssuer()
                    // Not taking offers of own IOUs.
                    && book.out.getIssuer() != uOfferOwnerID)
                // Offer owner not issuing ownfunds
                {
                    // Need to charge a transfer fee to offer owner.
                    offerRate = rate;
                    saOwnerFundsLimit = divide(saOwnerFunds, offerRate);
                }

                if (saOwnerFundsLimit >= saTakerGets)
                {
                    // Sufficient funds no shenanigans.
                    saTakerGetsFunded = saTakerGets;
                }
                else
                {
                    // Only provide, if not fully funded.

                    saTakerGetsFunded = saOwnerFundsLimit;

                    saTakerGetsFunded.setJson(jvOffer[jss::taker_gets_funded]);
                    std::min(
                        saTakerPays, multiply(saTakerGetsFunded, saDirRate, saTakerPays.asset()))
                        .setJson(jvOffer[jss::taker_pays_funded]);
                }

                STAmount const saOwnerPays = (kParityRate == offerRate)
                    ? saTakerGetsFunded
                    : std::min(saOwnerFunds, multiply(saTakerGetsFunded, offerRate));

                umBalance[uOfferOwnerID] = saOwnerFunds - saOwnerPays;

                // Include all offers funded and unfunded
                json::Value& jvOf = jvOffers.append(jvOffer);
                jvOf[jss::quality] = saDirRate.getText();

                if (firstOwnerOffer)
                    jvOf[jss::owner_funds] = saOwnerFunds.getText();
            }
            else
            {
                JLOG(journal_.warn()) << "Missing offer";
            }

            if (!cdirNext(view, uTipIndex, sleOfferDir, uBookEntry, offerIndex))
            {
                bDirectAdvance = true;
            }
            else
            {
                JLOG(journal_.trace()) << "getBookPage: offerIndex=" << offerIndex;
            }
        }
    }

    //  jvResult[jss::marker]  = json::Value(json::ValueType::Array);
    //  jvResult[jss::nodes]   = json::Value(json::ValueType::Array);
}

#else

// This is the new code that uses the book iterators
// It has temporarily been disabled

void
NetworkOPsImp::getBookPage(
    std::shared_ptr<ReadView const> lpLedger,
    Book const& book,
    AccountID const& uTakerID,
    bool const bProof,
    unsigned int iLimit,
    json::Value const& jvMarker,
    json::Value& jvResult)
{
    auto& jvOffers = (jvResult[jss::offers] = json::Value(json::ValueType::Array));

    std::map<AccountID, STAmount> umBalance;

    MetaView lesActive(lpLedger, tapNONE, true);
    OrderBookIterator obIterator(lesActive, book);

    auto const rate = transferRate(lesActive, book.out.account);

    bool const bGlobalFreeze =
        lesActive.isGlobalFrozen(book.out.account) || lesActive.isGlobalFrozen(book.in.account);

    while (iLimit-- > 0 && obIterator.nextOffer())
    {
        SLE::pointer sleOffer = obIterator.getCurrentOffer();
        if (sleOffer)
        {
            auto const uOfferOwnerID = sleOffer->getAccountID(sfAccount);
            auto const& saTakerGets = sleOffer->getFieldAmount(sfTakerGets);
            auto const& saTakerPays = sleOffer->getFieldAmount(sfTakerPays);
            STAmount saDirRate = obIterator.getCurrentRate();
            STAmount saOwnerFunds;

            if (book.out.account == uOfferOwnerID)
            {
                // If offer is selling issuer's own IOUs, it is fully funded.
                saOwnerFunds = saTakerGets;
            }
            else if (bGlobalFreeze)
            {
                // If either asset is globally frozen, consider all offers
                // that aren't ours to be totally unfunded
                saOwnerFunds.clear(book.out);
            }
            else
            {
                auto umBalanceEntry = umBalance.find(uOfferOwnerID);

                if (umBalanceEntry != umBalance.end())
                {
                    // Found in running balance table.

                    saOwnerFunds = umBalanceEntry->second;
                }
                else
                {
                    // Did not find balance in table.

                    saOwnerFunds = lesActive.accountHolds(
                        uOfferOwnerID,
                        book.out.currency,
                        book.out.account,
                        FreezeHandling::fhZERO_IF_FROZEN);

                    if (saOwnerFunds.isNegative())
                    {
                        // Treat negative funds as zero.

                        saOwnerFunds.zero();
                    }
                }
            }

            json::Value jvOffer = sleOffer->getJson(JsonOptions::Values::None);

            STAmount saTakerGetsFunded;
            STAmount saOwnerFundsLimit = saOwnerFunds;
            Rate offerRate = parityRate;

            if (rate != parityRate
                // Have a transfer fee.
                && uTakerID != book.out.account
                // Not taking offers of own IOUs.
                && book.out.account != uOfferOwnerID)
            // Offer owner not issuing ownfunds
            {
                // Need to charge a transfer fee to offer owner.
                offerRate = rate;
                saOwnerFundsLimit = divide(saOwnerFunds, offerRate);
            }

            if (saOwnerFundsLimit >= saTakerGets)
            {
                // Sufficient funds no shenanigans.
                saTakerGetsFunded = saTakerGets;
            }
            else
            {
                // Only provide, if not fully funded.
                saTakerGetsFunded = saOwnerFundsLimit;

                saTakerGetsFunded.setJson(jvOffer[jss::taker_gets_funded]);

                // TODO(tom): The result of this expression is not used - what's
                // going on here?
                std::min(saTakerPays, multiply(saTakerGetsFunded, saDirRate, saTakerPays.asset()))
                    .setJson(jvOffer[jss::taker_pays_funded]);
            }

            STAmount saOwnerPays = (parityRate == offerRate)
                ? saTakerGetsFunded
                : std::min(saOwnerFunds, multiply(saTakerGetsFunded, offerRate));

            umBalance[uOfferOwnerID] = saOwnerFunds - saOwnerPays;

            if (!saOwnerFunds.isZero() || uOfferOwnerID == uTakerID)
            {
                // Only provide funded offers and offers of the taker.
                json::Value& jvOf = jvOffers.append(jvOffer);
                jvOf[jss::quality] = saDirRate.getText();
            }
        }
    }

    //  jvResult[jss::marker]  = json::Value(json::ValueType::Array);
    //  jvResult[jss::nodes]   = json::Value(json::ValueType::Array);
}

#endif

inline void
NetworkOPsImp::collectMetrics()
{
    auto [counters, mode, start, initialSync] = accounting_.getCounterData();
    auto const current = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start);
    counters[static_cast<std::size_t>(mode)].dur += current;

    std::scoped_lock const lock(statsMutex_);
    stats_.disconnected_duration.set(
        counters[static_cast<std::size_t>(OperatingMode::DISCONNECTED)].dur.count());
    stats_.connected_duration.set(
        counters[static_cast<std::size_t>(OperatingMode::CONNECTED)].dur.count());
    stats_.syncing_duration.set(
        counters[static_cast<std::size_t>(OperatingMode::SYNCING)].dur.count());
    stats_.tracking_duration.set(
        counters[static_cast<std::size_t>(OperatingMode::TRACKING)].dur.count());
    stats_.full_duration.set(counters[static_cast<std::size_t>(OperatingMode::FULL)].dur.count());

    stats_.disconnected_transitions.set(
        counters[static_cast<std::size_t>(OperatingMode::DISCONNECTED)].transitions);
    stats_.connected_transitions.set(
        counters[static_cast<std::size_t>(OperatingMode::CONNECTED)].transitions);
    stats_.syncing_transitions.set(
        counters[static_cast<std::size_t>(OperatingMode::SYNCING)].transitions);
    stats_.tracking_transitions.set(
        counters[static_cast<std::size_t>(OperatingMode::TRACKING)].transitions);
    stats_.full_transitions.set(
        counters[static_cast<std::size_t>(OperatingMode::FULL)].transitions);
}

void
NetworkOPsImp::StateAccounting::mode(OperatingMode om)
{
    auto now = std::chrono::steady_clock::now();

    std::scoped_lock const lock(mutex_);
    ++counters_[static_cast<std::size_t>(om)].transitions;
    if (om == OperatingMode::FULL && counters_[static_cast<std::size_t>(om)].transitions == 1)
    {
        initialSyncUs_ =
            std::chrono::duration_cast<std::chrono::microseconds>(now - processStart_).count();
    }
    counters_[static_cast<std::size_t>(mode_)].dur +=
        std::chrono::duration_cast<std::chrono::microseconds>(now - start_);

    mode_ = om;
    start_ = now;
}

void
NetworkOPsImp::StateAccounting::json(json::Value& obj) const
{
    auto [counters, mode, start, initialSync] = getCounterData();
    auto const current = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start);
    counters[static_cast<std::size_t>(mode)].dur += current;

    obj[jss::state_accounting] = json::ValueType::Object;
    for (std::size_t i = static_cast<std::size_t>(OperatingMode::DISCONNECTED);
         i <= static_cast<std::size_t>(OperatingMode::FULL);
         ++i)
    {
        obj[jss::state_accounting][kStates[i]] = json::ValueType::Object;
        auto& state = obj[jss::state_accounting][kStates[i]];
        state[jss::transitions] = std::to_string(counters[i].transitions);
        state[jss::duration_us] = std::to_string(counters[i].dur.count());
    }
    obj[jss::server_state_duration_us] = std::to_string(current.count());
    if (initialSync != 0u)
        obj[jss::initial_sync_duration_us] = std::to_string(initialSync);
}

//------------------------------------------------------------------------------

std::unique_ptr<NetworkOPs>
makeNetworkOPs(
    ServiceRegistry& registry,
    NetworkOPs::clock_type& clock,
    bool standalone,
    std::size_t minPeerCount,
    bool startValid,
    JobQueue& jobQueue,
    LedgerMaster& ledgerMaster,
    ValidatorKeys const& validatorKeys,
    boost::asio::io_context& ioCtx,
    beast::Journal journal,
    beast::insight::Collector::ptr const& collector)
{
    return std::make_unique<NetworkOPsImp>(
        registry,
        clock,
        standalone,
        minPeerCount,
        startValid,
        jobQueue,
        ledgerMaster,
        validatorKeys,
        ioCtx,
        journal,
        collector);
}

}  // namespace xrpl
