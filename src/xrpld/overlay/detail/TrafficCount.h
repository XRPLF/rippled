#pragma once

#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/messages.h>

#include <atomic>
#include <cstdint>

namespace xrpl {

/** Fine-grained per-category accounting of overlay network traffic.
 *
 *  Owned by `OverlayImpl`, which exposes it through thin
 *  `reportInboundTraffic()` / `reportOutboundTraffic()` wrappers.
 *  Counters are harvested by `collectMetrics()` and pushed to the configured
 *  `beast::insight` collector (e.g. Graphite/StatsD).
 *
 *  Typical usage per message:
 *  1. Resolve the category once via `categorize()`.
 *  2. Call `addCount(Category::Total, ...)` for the raw wire size.
 *  3. Call `addCount(resolvedCategory, ...)` for the specific category.
 *  4. Optionally call `addCount(subCategory, ...)` for breakdown categories
 *     such as `TransactionDuplicate` or `ValidationUntrusted`.
 *
 *  @note `Category::Unknown` traffic is never rolled into `Category::Total`.
 *      Both are present in `counts_` from construction and are always safe
 *      to pass to `addCount()`.
 *  @note All counter updates are lock-free (`std::atomic`); this class is
 *      safe to call concurrently from multiple peer strands.
 */
class TrafficCount
{
public:
    enum class Category : std::size_t;

    /** Atomic byte and message counters for a single traffic category.
     *
     *  The four counters use `std::atomic<uint64_t>` so that concurrent peer
     *  strands can increment them without a mutex.
     *
     *  The copy constructor atomically snapshots each counter via `.load()`,
     *  enabling `getCounts()` callers to take a point-in-time copy for
     *  reporting without holding any external lock.
     */
    class TrafficStats
    {
    public:
        /** Monitoring-friendly display name, set once at construction via
         *  `toString(cat)` and never mutated afterwards. */
        std::string name;

        std::atomic<std::uint64_t> bytesIn{0};    /**< Inbound bytes accumulated for this category. */
        std::atomic<std::uint64_t> bytesOut{0};   /**< Outbound bytes accumulated for this category. */
        std::atomic<std::uint64_t> messagesIn{0}; /**< Inbound message count for this category. */
        std::atomic<std::uint64_t> messagesOut{0};/**< Outbound message count for this category. */

        /** Construct and set the display name for @p cat. */
        TrafficStats(TrafficCount::Category cat) : name(TrafficCount::toString(cat))
        {
        }

        /** Snapshot copy: atomically loads each counter so the result
         *  reflects a consistent point-in-time view. */
        TrafficStats(TrafficStats const& ts)
            : name(ts.name)
            , bytesIn(ts.bytesIn.load())
            , bytesOut(ts.bytesOut.load())
            , messagesIn(ts.messagesIn.load())
            , messagesOut(ts.messagesOut.load())
        {
        }

        /** Returns `true` if at least one message has been counted in either
         *  direction; allows monitoring code to skip inactive categories. */
        operator bool() const
        {
            return (messagesIn != 0u) || (messagesOut != 0u);
        }
    };

    /** Taxonomy of overlay protocol traffic for bandwidth telemetry.
     *
     *  Sub-categories (e.g. `TransactionDuplicate`, `ProposalUntrusted`) are
     *  incremented *in addition to* their parent category so that operators
     *  can observe both total volume and breakdown without arithmetic.
     *
     *  `Total` accumulates raw wire bytes for every recognized message exactly
     *  once per send/receive.  `Unknown` catches messages that fall through
     *  all classification logic and is intentionally excluded from `Total`.
     *
     *  @note When adding a new enumerator, also add a corresponding entry to
     *      the `kCATEGORY_MAP` table in `toString()` and the `counts_`
     *      initializer list.
     */
    enum class Category : std::size_t {
        Base,  /**< Ping and status-change overhead; must be first. */

        Cluster,    /**< Cluster peer overhead (mtCLUSTER). */
        Overlay,    /**< Overlay management messages (mtENDPOINTS). */
        Manifests,  /**< Validator manifest exchange (mtMANIFESTS). */

        Transaction,           /**< Transaction messages (mtTRANSACTION). */
        TransactionDuplicate,  /**< Transaction already seen; incremented alongside Transaction. */

        Proposal,           /**< Proposal messages (mtPROPOSE_LEDGER). */
        ProposalUntrusted,  /**< Proposals from untrusted validators; incremented alongside Proposal. */
        ProposalDuplicate,  /**< Proposals seen previously; incremented alongside Proposal. */

        Validation,           /**< Validation messages (mtVALIDATION). */
        ValidationUntrusted,  /**< Validations from untrusted validators; incremented alongside Validation. */
        ValidationDuplicate,  /**< Validations seen previously; incremented alongside Validation. */

        Validatorlist,  /**< Validator list messages (mtVALIDATOR_LIST, mtVALIDATOR_LIST_COLLECTION). */

        Squelch,           /**< Squelch control messages (mtSQUELCH). */
        SquelchSuppressed, /**< Outbound bytes suppressed by squelching; not a real send. */
        SquelchIgnored,    /**< Inbound bytes from peers that are ignoring squelch instructions. */

        // --- TMHaveSet ---
        GetSet,    /**< Transaction sets this node is trying to acquire. */
        ShareSet,  /**< Transaction sets this node is providing to peers. */

        // --- TMLedgerData sub-categories ---
        LdTscGet,   /**< TMLedgerData: transaction set candidate, get direction. */
        LdTscShare, /**< TMLedgerData: transaction set candidate, share direction. */

        LdTxnGet,   /**< TMLedgerData: transaction tree node, get direction. */
        LdTxnShare, /**< TMLedgerData: transaction tree node, share direction. */

        LdAsnGet,   /**< TMLedgerData: account state node, get direction. */
        LdAsnShare, /**< TMLedgerData: account state node, share direction. */

        LdGet,   /**< TMLedgerData: unrecognized itype, get direction. */
        LdShare, /**< TMLedgerData: unrecognized itype, share direction. */

        // --- TMGetLedger sub-categories ---
        GlTscShare, /**< TMGetLedger: transaction set candidate, share direction. */
        GlTscGet,   /**< TMGetLedger: transaction set candidate, get direction. */

        GlTxnShare, /**< TMGetLedger: transaction tree node, share direction. */
        GlTxnGet,   /**< TMGetLedger: transaction tree node, get direction. */

        GlAsnShare, /**< TMGetLedger: account state node, share direction. */
        GlAsnGet,   /**< TMGetLedger: account state node, get direction. */

        GlShare, /**< TMGetLedger: unrecognized itype, share direction. */
        GlGet,   /**< TMGetLedger: unrecognized itype, get direction. */

        // --- TMGetObjectByHash sub-categories ---
        ShareHashLedger, /**< TMGetObjectByHash: otLEDGER, share direction. */
        GetHashLedger,   /**< TMGetObjectByHash: otLEDGER, get direction. */

        ShareHashTx, /**< TMGetObjectByHash: otTRANSACTION, share direction. */
        GetHashTx,   /**< TMGetObjectByHash: otTRANSACTION, get direction. */

        ShareHashTxnode, /**< TMGetObjectByHash: otTRANSACTION_NODE, share direction. */
        GetHashTxnode,   /**< TMGetObjectByHash: otTRANSACTION_NODE, get direction. */

        ShareHashAsnode, /**< TMGetObjectByHash: otSTATE_NODE, share direction. */
        GetHashAsnode,   /**< TMGetObjectByHash: otSTATE_NODE, get direction. */

        ShareCasObject, /**< TMGetObjectByHash: otCAS_OBJECT, share direction. */
        GetCasObject,   /**< TMGetObjectByHash: otCAS_OBJECT, get direction. */

        ShareFetchPack, /**< TMGetObjectByHash: otFETCH_PACK, share direction. */
        GetFetchPack,   /**< TMGetObjectByHash: otFETCH_PACK, get direction. */

        GetTransactions, /**< TMGetObjectByHash: otTRANSACTIONS (always get, direction-independent). */

        ShareHash, /**< TMGetObjectByHash: unrecognized type, share direction. */
        GetHash,   /**< TMGetObjectByHash: unrecognized type, get direction. */

        ProofPathRequest,  /**< TMProofPathRequest messages. */
        ProofPathResponse, /**< TMProofPathResponse messages. */

        ReplayDeltaRequest,  /**< TMReplayDeltaRequest messages. */
        ReplayDeltaResponse, /**< TMReplayDeltaResponse messages. */

        HaveTransactions,      /**< TMHaveTransactions hash-announcement messages. */
        RequestedTransactions, /**< TMTransactions full-transaction responses. */

        Total,   /**< Aggregate raw wire bytes for every recognized message; never includes Unknown. */
        Unknown  /**< Catch-all for message types that fall through all classification; must be last. */
    };

    TrafficCount() = default;

    /** Classify a protocol message into the most specific traffic category.
     *
     *  Uses a two-stage strategy: an O(1) lookup table handles message types
     *  whose category is fully determined by the wire type discriminator alone.
     *  Three `dynamic_cast` branches then handle `TMLedgerData`, `TMGetLedger`,
     *  and `TMGetObjectByHash`, whose sub-category depends on protobuf fields
     *  (`requestcookie`, `itype`, `query`) that are invisible to the wire type.
     *
     *  @param message  The deserialized protobuf message; used for `dynamic_cast`
     *      on the three complex types.
     *  @param type     The wire-level message type discriminator extracted from
     *      the overlay frame header before deserialization.
     *  @param inbound  `true` if the message arrived from a remote peer;
     *      `false` if it is being prepared for outbound transmission.
     *  @return The most specific matching `Category`, or `Category::Unknown`
     *      if no branch matches.
     *  @note Callers should additionally call `addCount(Category::Total, ...)`
     *      once per message to accumulate raw wire bytes separately from the
     *      per-category counts.
     */
    static Category
    categorize(
        ::google::protobuf::Message const& message,
        protocol::MessageType type,
        bool inbound);

    /** Increment the byte and message counters for a traffic category.
     *
     *  Silently returns if @p cat is not present in `counts_` (e.g. a future
     *  enum value added without a matching initializer).  An `XRPL_ASSERT`
     *  guards against out-of-range values in debug builds.
     *
     *  @param cat      The traffic category to update.
     *  @param inbound  `true` to increment `bytesIn`/`messagesIn`; `false`
     *      for `bytesOut`/`messagesOut`.
     *  @param bytes    Wire byte count to add to the appropriate bytes counter.
     */
    void
    addCount(Category cat, bool inbound, int bytes)
    {
        XRPL_ASSERT(
            cat <= Category::Unknown, "xrpl::TrafficCount::addCount : valid category input");

        auto it = counts_.find(cat);

        if (it == counts_.end())
            return;

        if (inbound)
        {
            it->second.bytesIn += bytes;
            ++it->second.messagesIn;
        }
        else
        {
            it->second.bytesOut += bytes;
            ++it->second.messagesOut;
        }
    }

    /** Return a const reference to the live counter map.
     *
     *  The map is keyed by `Category` and pre-populated at construction with
     *  every known category including `Total` and `Unknown`.  Because
     *  `TrafficStats`'s copy constructor performs atomic loads, callers can
     *  snapshot the entire map by copying individual entries without holding
     *  an external lock.
     *
     *  @return Const reference to the internal `unordered_map<Category, TrafficStats>`.
     */
    [[nodiscard]] auto const&
    getCounts() const
    {
        return counts_;
    }

    /** Convert a `Category` value to its monitoring-friendly display name.
     *
     *  Uses a static local map for O(1) lookup.  `Category::Unknown` and any
     *  out-of-range value intentionally fall through to return the literal
     *  string `"unknown"`, so `TrafficStats` objects for unrecognized
     *  categories still receive a valid display name.
     *
     *  @param cat  The category to convert.
     *  @return A stable ASCII string suitable for use as a metric name (e.g.
     *      `"ledger_data_Transaction_Set_candidate_get"`), or `"unknown"`.
     */
    static std::string
    toString(Category cat)
    {
        static std::unordered_map<Category, std::string> const kCATEGORY_MAP = {
            {Category::Base, "overhead"},
            {Category::Cluster, "overhead_cluster"},
            {Category::Overlay, "overhead_overlay"},
            {Category::Manifests, "overhead_manifest"},
            {Category::Transaction, "transactions"},
            {Category::TransactionDuplicate, "transactions_duplicate"},
            {Category::Proposal, "proposals"},
            {Category::ProposalUntrusted, "proposals_untrusted"},
            {Category::ProposalDuplicate, "proposals_duplicate"},
            {Category::Validation, "validations"},
            {Category::ValidationUntrusted, "validations_untrusted"},
            {Category::ValidationDuplicate, "validations_duplicate"},
            {Category::Validatorlist, "validator_lists"},
            {Category::Squelch, "squelch"},
            {Category::SquelchSuppressed, "squelch_suppressed"},
            {Category::SquelchIgnored, "squelch_ignored"},
            {Category::GetSet, "set_get"},
            {Category::ShareSet, "set_share"},
            {Category::LdTscGet, "ledger_data_Transaction_Set_candidate_get"},
            {Category::LdTscShare, "ledger_data_Transaction_Set_candidate_share"},
            {Category::LdTxnGet, "ledger_data_Transaction_Node_get"},
            {Category::LdTxnShare, "ledger_data_Transaction_Node_share"},
            {Category::LdAsnGet, "ledger_data_Account_State_Node_get"},
            {Category::LdAsnShare, "ledger_data_Account_State_Node_share"},
            {Category::LdGet, "ledger_data_get"},
            {Category::LdShare, "ledger_data_share"},
            {Category::GlTscShare, "ledger_Transaction_Set_candidate_share"},
            {Category::GlTscGet, "ledger_Transaction_Set_candidate_get"},
            {Category::GlTxnShare, "ledger_Transaction_node_share"},
            {Category::GlTxnGet, "ledger_Transaction_node_get"},
            {Category::GlAsnShare, "ledger_Account_State_node_share"},
            {Category::GlAsnGet, "ledger_Account_State_node_get"},
            {Category::GlShare, "ledger_share"},
            {Category::GlGet, "ledger_get"},
            {Category::ShareHashLedger, "getobject_Ledger_share"},
            {Category::GetHashLedger, "getobject_Ledger_get"},
            {Category::ShareHashTx, "getobject_Transaction_share"},
            {Category::GetHashTx, "getobject_Transaction_get"},
            {Category::ShareHashTxnode, "getobject_Transaction_node_share"},
            {Category::GetHashTxnode, "getobject_Transaction_node_get"},
            {Category::ShareHashAsnode, "getobject_Account_State_node_share"},
            {Category::GetHashAsnode, "getobject_Account_State_node_get"},
            {Category::ShareCasObject, "getobject_CAS_share"},
            {Category::GetCasObject, "getobject_CAS_get"},
            {Category::ShareFetchPack, "getobject_Fetch_Pack_share"},
            {Category::GetFetchPack, "getobject_Fetch Pack_get"},
            {Category::GetTransactions, "getobject_Transactions_get"},
            {Category::ShareHash, "getobject_share"},
            {Category::GetHash, "getobject_get"},
            {Category::ProofPathRequest, "proof_path_request"},
            {Category::ProofPathResponse, "proof_path_response"},
            {Category::ReplayDeltaRequest, "replay_delta_request"},
            {Category::ReplayDeltaResponse, "replay_delta_response"},
            {Category::HaveTransactions, "have_transactions"},
            {Category::RequestedTransactions, "requested_transactions"},
            {Category::Total, "total"}};

        if (auto it = kCATEGORY_MAP.find(cat); it != kCATEGORY_MAP.end())
            return it->second;

        return "unknown";
    }

protected:
    /** Per-category counter storage, pre-populated at construction.
     *
     *  Every `Category` enumerator (including `Total` and `Unknown`) has an
     *  entry here so that `addCount()` and `getCounts()` callers never need
     *  to check for missing keys.  Populated entirely by the inline
     *  initializer list; do not insert or erase entries at runtime.
     */
    std::unordered_map<Category, TrafficStats> counts_{
        {Category::Base, {Category::Base}},
        {Category::Cluster, {Category::Cluster}},
        {Category::Overlay, {Category::Overlay}},
        {Category::Manifests, {Category::Manifests}},
        {Category::Transaction, {Category::Transaction}},
        {Category::TransactionDuplicate, {Category::TransactionDuplicate}},
        {Category::Proposal, {Category::Proposal}},
        {Category::ProposalUntrusted, {Category::ProposalUntrusted}},
        {Category::ProposalDuplicate, {Category::ProposalDuplicate}},
        {Category::Validation, {Category::Validation}},
        {Category::ValidationUntrusted, {Category::ValidationUntrusted}},
        {Category::ValidationDuplicate, {Category::ValidationDuplicate}},
        {Category::Validatorlist, {Category::Validatorlist}},
        {Category::Squelch, {Category::Squelch}},
        {Category::SquelchSuppressed, {Category::SquelchSuppressed}},
        {Category::SquelchIgnored, {Category::SquelchIgnored}},
        {Category::GetSet, {Category::GetSet}},
        {Category::ShareSet, {Category::ShareSet}},
        {Category::LdTscGet, {Category::LdTscGet}},
        {Category::LdTscShare, {Category::LdTscShare}},
        {Category::LdTxnGet, {Category::LdTxnGet}},
        {Category::LdTxnShare, {Category::LdTxnShare}},
        {Category::LdAsnGet, {Category::LdAsnGet}},
        {Category::LdAsnShare, {Category::LdAsnShare}},
        {Category::LdGet, {Category::LdGet}},
        {Category::LdShare, {Category::LdShare}},
        {Category::GlTscShare, {Category::GlTscShare}},
        {Category::GlTscGet, {Category::GlTscGet}},
        {Category::GlTxnShare, {Category::GlTxnShare}},
        {Category::GlTxnGet, {Category::GlTxnGet}},
        {Category::GlAsnShare, {Category::GlAsnShare}},
        {Category::GlAsnGet, {Category::GlAsnGet}},
        {Category::GlShare, {Category::GlShare}},
        {Category::GlGet, {Category::GlGet}},
        {Category::ShareHashLedger, {Category::ShareHashLedger}},
        {Category::GetHashLedger, {Category::GetHashLedger}},
        {Category::ShareHashTx, {Category::ShareHashTx}},
        {Category::GetHashTx, {Category::GetHashTx}},
        {Category::ShareHashTxnode, {Category::ShareHashTxnode}},
        {Category::GetHashTxnode, {Category::GetHashTxnode}},
        {Category::ShareHashAsnode, {Category::ShareHashAsnode}},
        {Category::GetHashAsnode, {Category::GetHashAsnode}},
        {Category::ShareCasObject, {Category::ShareCasObject}},
        {Category::GetCasObject, {Category::GetCasObject}},
        {Category::ShareFetchPack, {Category::ShareFetchPack}},
        {Category::GetFetchPack, {Category::GetFetchPack}},
        {Category::GetTransactions, {Category::GetTransactions}},
        {Category::ShareHash, {Category::ShareHash}},
        {Category::GetHash, {Category::GetHash}},
        {Category::ProofPathRequest, {Category::ProofPathRequest}},
        {Category::ProofPathResponse, {Category::ProofPathResponse}},
        {Category::ReplayDeltaRequest, {Category::ReplayDeltaRequest}},
        {Category::ReplayDeltaResponse, {Category::ReplayDeltaResponse}},
        {Category::HaveTransactions, {Category::HaveTransactions}},
        {Category::RequestedTransactions, {Category::RequestedTransactions}},
        {Category::Total, {Category::Total}},
        {Category::Unknown, {Category::Unknown}},
    };
};

}  // namespace xrpl
