#pragma once

#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/messages.h>

#include <atomic>
#include <cstdint>

namespace xrpl {

/**
    TrafficCount is used to count ingress and egress wire bytes and number of
   messages. The general intended usage is as follows:
        1. Determine the message category by callin TrafficCount::categorize
        2. Increment the counters for incoming or outgoing traffic by calling
   TrafficCount::addCount
        3. Optionally, TrafficCount::addCount can be called at any time to
   increment additional traffic categories, not captured by
   TrafficCount::categorize.

   There are two special categories:
        1. category::total - this category is used to report the total traffic
   amount. It should be incremented once just after receiving a new message, and
   once just before sending a message to a peer. Messages whose category is not
   in TrafficCount::categorize are not included in the total.
        2. category::unknown - this category is used to report traffic for
   messages of unknown type.
*/
class TrafficCount
{
public:
    enum class category : std::size_t;

    class TrafficStats
    {
    public:
        std::string name;

        std::atomic<std::uint64_t> bytesIn{0};
        std::atomic<std::uint64_t> bytesOut{0};
        std::atomic<std::uint64_t> messagesIn{0};
        std::atomic<std::uint64_t> messagesOut{0};

        TrafficStats(TrafficCount::category cat) : name(TrafficCount::to_string(cat))
        {
        }

        TrafficStats(TrafficStats const& ts)
            : name(ts.name)
            , bytesIn(ts.bytesIn.load())
            , bytesOut(ts.bytesOut.load())
            , messagesIn(ts.messagesIn.load())
            , messagesOut(ts.messagesOut.load())
        {
        }

        operator bool() const
        {
            return (messagesIn != 0u) || (messagesOut != 0u);
        }
    };

    // If you add entries to this enum, you need to update the initialization
    // of the arrays at the bottom of this file which map array numbers to
    // human-readable, monitoring-tool friendly names.
    enum class category : std::size_t {
        base,  // basic peer overhead, must be first

        cluster,    // cluster overhead
        overlay,    // overlay management
        manifests,  // manifest management

        transaction,  // transaction messages
        // The following categories breakdown transaction message type
        transaction_duplicate,  // duplicate transaction messages

        proposal,  // proposal messages
        // The following categories breakdown proposal message type
        proposal_untrusted,  // proposals from untrusted validators
        proposal_duplicate,  // proposals seen previously

        validation,  // validation messages
        // The following categories breakdown validation message type
        validation_untrusted,  // validations from untrusted validators
        validation_duplicate,  // validations seen previously

        validatorlist,

        squelch,
        squelch_suppressed,  // egress traffic amount suppressed by squelching
        squelch_ignored,     // the traffic amount that came from peers ignoring
                             // squelch messages

        // TMHaveSet message:
        get_set,    // transaction sets we try to get
        share_set,  // transaction sets we get

        // TMLedgerData: transaction set candidate
        ld_tsc_get,
        ld_tsc_share,

        // TMLedgerData: transaction node
        ld_txn_get,
        ld_txn_share,

        // TMLedgerData: account state node
        ld_asn_get,
        ld_asn_share,

        // TMLedgerData: generic
        ld_get,
        ld_share,

        // TMGetLedger: transaction set candidate
        gl_tsc_share,
        gl_tsc_get,

        // TMGetLedger: transaction node
        gl_txn_share,
        gl_txn_get,

        // TMGetLedger: account state node
        gl_asn_share,
        gl_asn_get,

        // TMGetLedger: generic
        gl_share,
        gl_get,

        // TMGetObjectByHash:
        share_hash_ledger,
        get_hash_ledger,

        // TMGetObjectByHash:
        share_hash_tx,
        get_hash_tx,

        // TMGetObjectByHash: transaction node
        share_hash_txnode,
        get_hash_txnode,

        // TMGetObjectByHash: account state node
        share_hash_asnode,
        get_hash_asnode,

        // TMGetObjectByHash: CAS
        share_cas_object,
        get_cas_object,

        // TMGetObjectByHash: fetch packs
        share_fetch_pack,
        get_fetch_pack,

        // TMGetObjectByHash: transactions
        get_transactions,

        // TMGetObjectByHash: generic
        share_hash,
        get_hash,

        // TMProofPathRequest and TMProofPathResponse
        proof_path_request,
        proof_path_response,

        // TMReplayDeltaRequest and TMReplayDeltaResponse
        replay_delta_request,
        replay_delta_response,

        // TMHaveTransactions
        have_transactions,

        // TMTransactions
        requested_transactions,

        // The total p2p bytes sent and received on the wire
        total,

        unknown  // must be last
    };

    TrafficCount() = default;

    /** Given a protocol message, determine which traffic category it belongs to
     */
    static category
    categorize(
        ::google::protobuf::Message const& message,
        protocol::MessageType type,
        bool inbound);

    /** Account for traffic associated with the given category */
    void
    addCount(category cat, bool inbound, int bytes)
    {
        XRPL_ASSERT(
            static_cast<std::size_t>(cat) <= static_cast<std::size_t>(category::unknown),
            "xrpl::TrafficCount::addCount : valid category input");

        auto it = counts_.find(cat);

        // nothing to do, the category does not exist
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

    /** An up-to-date copy of all the counters

        @return an object which satisfies the requirements of Container
     */
    [[nodiscard]] auto const&
    getCounts() const
    {
        return counts_;
    }

    static std::string
    to_string(category cat)
    {
        static std::unordered_map<category, std::string> const category_map = {
            {category::base, "overhead"},
            {category::cluster, "overhead_cluster"},
            {category::overlay, "overhead_overlay"},
            {category::manifests, "overhead_manifest"},
            {category::transaction, "transactions"},
            {category::transaction_duplicate, "transactions_duplicate"},
            {category::proposal, "proposals"},
            {category::proposal_untrusted, "proposals_untrusted"},
            {category::proposal_duplicate, "proposals_duplicate"},
            {category::validation, "validations"},
            {category::validation_untrusted, "validations_untrusted"},
            {category::validation_duplicate, "validations_duplicate"},
            {category::validatorlist, "validator_lists"},
            {category::squelch, "squelch"},
            {category::squelch_suppressed, "squelch_suppressed"},
            {category::squelch_ignored, "squelch_ignored"},
            {category::get_set, "set_get"},
            {category::share_set, "set_share"},
            {category::ld_tsc_get, "ledger_data_Transaction_Set_candidate_get"},
            {category::ld_tsc_share, "ledger_data_Transaction_Set_candidate_share"},
            {category::ld_txn_get, "ledger_data_Transaction_Node_get"},
            {category::ld_txn_share, "ledger_data_Transaction_Node_share"},
            {category::ld_asn_get, "ledger_data_Account_State_Node_get"},
            {category::ld_asn_share, "ledger_data_Account_State_Node_share"},
            {category::ld_get, "ledger_data_get"},
            {category::ld_share, "ledger_data_share"},
            {category::gl_tsc_share, "ledger_Transaction_Set_candidate_share"},
            {category::gl_tsc_get, "ledger_Transaction_Set_candidate_get"},
            {category::gl_txn_share, "ledger_Transaction_node_share"},
            {category::gl_txn_get, "ledger_Transaction_node_get"},
            {category::gl_asn_share, "ledger_Account_State_node_share"},
            {category::gl_asn_get, "ledger_Account_State_node_get"},
            {category::gl_share, "ledger_share"},
            {category::gl_get, "ledger_get"},
            {category::share_hash_ledger, "getobject_Ledger_share"},
            {category::get_hash_ledger, "getobject_Ledger_get"},
            {category::share_hash_tx, "getobject_Transaction_share"},
            {category::get_hash_tx, "getobject_Transaction_get"},
            {category::share_hash_txnode, "getobject_Transaction_node_share"},
            {category::get_hash_txnode, "getobject_Transaction_node_get"},
            {category::share_hash_asnode, "getobject_Account_State_node_share"},
            {category::get_hash_asnode, "getobject_Account_State_node_get"},
            {category::share_cas_object, "getobject_CAS_share"},
            {category::get_cas_object, "getobject_CAS_get"},
            {category::share_fetch_pack, "getobject_Fetch_Pack_share"},
            {category::get_fetch_pack, "getobject_Fetch Pack_get"},
            {category::get_transactions, "getobject_Transactions_get"},
            {category::share_hash, "getobject_share"},
            {category::get_hash, "getobject_get"},
            {category::proof_path_request, "proof_path_request"},
            {category::proof_path_response, "proof_path_response"},
            {category::replay_delta_request, "replay_delta_request"},
            {category::replay_delta_response, "replay_delta_response"},
            {category::have_transactions, "have_transactions"},
            {category::requested_transactions, "requested_transactions"},
            {category::total, "total"}};

        if (auto it = category_map.find(cat); it != category_map.end())
            return it->second;

        return "unknown";
    }

protected:
    std::unordered_map<category, TrafficStats> counts_{
        {category::base, {category::base}},
        {category::cluster, {category::cluster}},
        {category::overlay, {category::overlay}},
        {category::manifests, {category::manifests}},
        {category::transaction, {category::transaction}},
        {category::transaction_duplicate, {category::transaction_duplicate}},
        {category::proposal, {category::proposal}},
        {category::proposal_untrusted, {category::proposal_untrusted}},
        {category::proposal_duplicate, {category::proposal_duplicate}},
        {category::validation, {category::validation}},
        {category::validation_untrusted, {category::validation_untrusted}},
        {category::validation_duplicate, {category::validation_duplicate}},
        {category::validatorlist, {category::validatorlist}},
        {category::squelch, {category::squelch}},
        {category::squelch_suppressed, {category::squelch_suppressed}},
        {category::squelch_ignored, {category::squelch_ignored}},
        {category::get_set, {category::get_set}},
        {category::share_set, {category::share_set}},
        {category::ld_tsc_get, {category::ld_tsc_get}},
        {category::ld_tsc_share, {category::ld_tsc_share}},
        {category::ld_txn_get, {category::ld_txn_get}},
        {category::ld_txn_share, {category::ld_txn_share}},
        {category::ld_asn_get, {category::ld_asn_get}},
        {category::ld_asn_share, {category::ld_asn_share}},
        {category::ld_get, {category::ld_get}},
        {category::ld_share, {category::ld_share}},
        {category::gl_tsc_share, {category::gl_tsc_share}},
        {category::gl_tsc_get, {category::gl_tsc_get}},
        {category::gl_txn_share, {category::gl_txn_share}},
        {category::gl_txn_get, {category::gl_txn_get}},
        {category::gl_asn_share, {category::gl_asn_share}},
        {category::gl_asn_get, {category::gl_asn_get}},
        {category::gl_share, {category::gl_share}},
        {category::gl_get, {category::gl_get}},
        {category::share_hash_ledger, {category::share_hash_ledger}},
        {category::get_hash_ledger, {category::get_hash_ledger}},
        {category::share_hash_tx, {category::share_hash_tx}},
        {category::get_hash_tx, {category::get_hash_tx}},
        {category::share_hash_txnode, {category::share_hash_txnode}},
        {category::get_hash_txnode, {category::get_hash_txnode}},
        {category::share_hash_asnode, {category::share_hash_asnode}},
        {category::get_hash_asnode, {category::get_hash_asnode}},
        {category::share_cas_object, {category::share_cas_object}},
        {category::get_cas_object, {category::get_cas_object}},
        {category::share_fetch_pack, {category::share_fetch_pack}},
        {category::get_fetch_pack, {category::get_fetch_pack}},
        {category::get_transactions, {category::get_transactions}},
        {category::share_hash, {category::share_hash}},
        {category::get_hash, {category::get_hash}},
        {category::proof_path_request, {category::proof_path_request}},
        {category::proof_path_response, {category::proof_path_response}},
        {category::replay_delta_request, {category::replay_delta_request}},
        {category::replay_delta_response, {category::replay_delta_response}},
        {category::have_transactions, {category::have_transactions}},
        {category::requested_transactions, {category::requested_transactions}},
        {category::total, {category::total}},
        {category::unknown, {category::unknown}},
    };
};

}  // namespace xrpl
