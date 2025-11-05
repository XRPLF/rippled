#ifndef XRPL_OVERLAY_TRAFFIC_H_INCLUDED
#define XRPL_OVERLAY_TRAFFIC_H_INCLUDED

#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/messages.h>

#include <atomic>
#include <cstdint>

namespace ripple {

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
    enum category : std::size_t;

    class TrafficStats
    {
    public:
        std::string name;

        std::atomic<std::uint64_t> bytesIn{0};
        std::atomic<std::uint64_t> bytesOut{0};
        std::atomic<std::uint64_t> messagesIn{0};
        std::atomic<std::uint64_t> messagesOut{0};

        TrafficStats(TrafficCount::category cat)
            : name(TrafficCount::to_string(cat))
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
            return messagesIn || messagesOut;
        }
    };

    // If you add entries to this enum, you need to update the initialization
    // of the arrays at the bottom of this file which map array numbers to
    // human-readable, monitoring-tool friendly names.
    enum category : std::size_t {
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
            cat <= category::unknown,
            "ripple::TrafficCount::addCount : valid category input");

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
    auto const&
    getCounts() const
    {
        return counts_;
    }

    static std::string
    to_string(category cat)
    {
        static std::unordered_map<category, std::string> const category_map = {
            {base, "overhead"},
            {cluster, "overhead_cluster"},
            {overlay, "overhead_overlay"},
            {manifests, "overhead_manifest"},
            {transaction, "transactions"},
            {transaction_duplicate, "transactions_duplicate"},
            {proposal, "proposals"},
            {proposal_untrusted, "proposals_untrusted"},
            {proposal_duplicate, "proposals_duplicate"},
            {validation, "validations"},
            {validation_untrusted, "validations_untrusted"},
            {validation_duplicate, "validations_duplicate"},
            {validatorlist, "validator_lists"},
            {squelch, "squelch"},
            {squelch_suppressed, "squelch_suppressed"},
            {squelch_ignored, "squelch_ignored"},
            {get_set, "set_get"},
            {share_set, "set_share"},
            {ld_tsc_get, "ledger_data_Transaction_Set_candidate_get"},
            {ld_tsc_share, "ledger_data_Transaction_Set_candidate_share"},
            {ld_txn_get, "ledger_data_Transaction_Node_get"},
            {ld_txn_share, "ledger_data_Transaction_Node_share"},
            {ld_asn_get, "ledger_data_Account_State_Node_get"},
            {ld_asn_share, "ledger_data_Account_State_Node_share"},
            {ld_get, "ledger_data_get"},
            {ld_share, "ledger_data_share"},
            {gl_tsc_share, "ledger_Transaction_Set_candidate_share"},
            {gl_tsc_get, "ledger_Transaction_Set_candidate_get"},
            {gl_txn_share, "ledger_Transaction_node_share"},
            {gl_txn_get, "ledger_Transaction_node_get"},
            {gl_asn_share, "ledger_Account_State_node_share"},
            {gl_asn_get, "ledger_Account_State_node_get"},
            {gl_share, "ledger_share"},
            {gl_get, "ledger_get"},
            {share_hash_ledger, "getobject_Ledger_share"},
            {get_hash_ledger, "getobject_Ledger_get"},
            {share_hash_tx, "getobject_Transaction_share"},
            {get_hash_tx, "getobject_Transaction_get"},
            {share_hash_txnode, "getobject_Transaction_node_share"},
            {get_hash_txnode, "getobject_Transaction_node_get"},
            {share_hash_asnode, "getobject_Account_State_node_share"},
            {get_hash_asnode, "getobject_Account_State_node_get"},
            {share_cas_object, "getobject_CAS_share"},
            {get_cas_object, "getobject_CAS_get"},
            {share_fetch_pack, "getobject_Fetch_Pack_share"},
            {get_fetch_pack, "getobject_Fetch Pack_get"},
            {get_transactions, "getobject_Transactions_get"},
            {share_hash, "getobject_share"},
            {get_hash, "getobject_get"},
            {proof_path_request, "proof_path_request"},
            {proof_path_response, "proof_path_response"},
            {replay_delta_request, "replay_delta_request"},
            {replay_delta_response, "replay_delta_response"},
            {have_transactions, "have_transactions"},
            {requested_transactions, "requested_transactions"},
            {total, "total"}};

        if (auto it = category_map.find(cat); it != category_map.end())
            return it->second;

        return "unknown";
    }

protected:
    std::unordered_map<category, TrafficStats> counts_{
        {base, {base}},
        {cluster, {cluster}},
        {overlay, {overlay}},
        {manifests, {manifests}},
        {transaction, {transaction}},
        {transaction_duplicate, {transaction_duplicate}},
        {proposal, {proposal}},
        {proposal_untrusted, {proposal_untrusted}},
        {proposal_duplicate, {proposal_duplicate}},
        {validation, {validation}},
        {validation_untrusted, {validation_untrusted}},
        {validation_duplicate, {validation_duplicate}},
        {validatorlist, {validatorlist}},
        {squelch, {squelch}},
        {squelch_suppressed, {squelch_suppressed}},
        {squelch_ignored, {squelch_ignored}},
        {get_set, {get_set}},
        {share_set, {share_set}},
        {ld_tsc_get, {ld_tsc_get}},
        {ld_tsc_share, {ld_tsc_share}},
        {ld_txn_get, {ld_txn_get}},
        {ld_txn_share, {ld_txn_share}},
        {ld_asn_get, {ld_asn_get}},
        {ld_asn_share, {ld_asn_share}},
        {ld_get, {ld_get}},
        {ld_share, {ld_share}},
        {gl_tsc_share, {gl_tsc_share}},
        {gl_tsc_get, {gl_tsc_get}},
        {gl_txn_share, {gl_txn_share}},
        {gl_txn_get, {gl_txn_get}},
        {gl_asn_share, {gl_asn_share}},
        {gl_asn_get, {gl_asn_get}},
        {gl_share, {gl_share}},
        {gl_get, {gl_get}},
        {share_hash_ledger, {share_hash_ledger}},
        {get_hash_ledger, {get_hash_ledger}},
        {share_hash_tx, {share_hash_tx}},
        {get_hash_tx, {get_hash_tx}},
        {share_hash_txnode, {share_hash_txnode}},
        {get_hash_txnode, {get_hash_txnode}},
        {share_hash_asnode, {share_hash_asnode}},
        {get_hash_asnode, {get_hash_asnode}},
        {share_cas_object, {share_cas_object}},
        {get_cas_object, {get_cas_object}},
        {share_fetch_pack, {share_fetch_pack}},
        {get_fetch_pack, {get_fetch_pack}},
        {get_transactions, {get_transactions}},
        {share_hash, {share_hash}},
        {get_hash, {get_hash}},
        {proof_path_request, {proof_path_request}},
        {proof_path_response, {proof_path_response}},
        {replay_delta_request, {replay_delta_request}},
        {replay_delta_response, {replay_delta_response}},
        {have_transactions, {have_transactions}},
        {requested_transactions, {requested_transactions}},
        {total, {total}},
        {unknown, {unknown}},
    };
};

}  // namespace ripple
#endif
