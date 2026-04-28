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
    enum Category : std::size_t;

    class TrafficStats
    {
    public:
        std::string name;

        std::atomic<std::uint64_t> bytesIn{0};
        std::atomic<std::uint64_t> bytesOut{0};
        std::atomic<std::uint64_t> messagesIn{0};
        std::atomic<std::uint64_t> messagesOut{0};

        TrafficStats(TrafficCount::Category cat) : name(TrafficCount::to_string(cat))
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
    enum Category : std::size_t {
        Base,  // basic peer overhead, must be first

        Cluster,    // cluster overhead
        Overlay,    // overlay management
        Manifests,  // manifest management

        Transaction,  // transaction messages
        // The following categories breakdown transaction message type
        TransactionDuplicate,  // duplicate transaction messages

        Proposal,  // proposal messages
        // The following categories breakdown proposal message type
        ProposalUntrusted,  // proposals from untrusted validators
        ProposalDuplicate,  // proposals seen previously

        Validation,  // validation messages
        // The following categories breakdown validation message type
        ValidationUntrusted,  // validations from untrusted validators
        ValidationDuplicate,  // validations seen previously

        Validatorlist,

        Squelch,
        SquelchSuppressed,  // egress traffic amount suppressed by squelching
        SquelchIgnored,     // the traffic amount that came from peers ignoring
                            // squelch messages

        // TMHaveSet message:
        GetSet,    // transaction sets we try to get
        ShareSet,  // transaction sets we get

        // TMLedgerData: transaction set candidate
        LdTscGet,
        LdTscShare,

        // TMLedgerData: transaction node
        LdTxnGet,
        LdTxnShare,

        // TMLedgerData: account state node
        LdAsnGet,
        LdAsnShare,

        // TMLedgerData: generic
        LdGet,
        LdShare,

        // TMGetLedger: transaction set candidate
        GlTscShare,
        GlTscGet,

        // TMGetLedger: transaction node
        GlTxnShare,
        GlTxnGet,

        // TMGetLedger: account state node
        GlAsnShare,
        GlAsnGet,

        // TMGetLedger: generic
        GlShare,
        GlGet,

        // TMGetObjectByHash:
        ShareHashLedger,
        GetHashLedger,

        // TMGetObjectByHash:
        ShareHashTx,
        GetHashTx,

        // TMGetObjectByHash: transaction node
        ShareHashTxnode,
        GetHashTxnode,

        // TMGetObjectByHash: account state node
        ShareHashAsnode,
        GetHashAsnode,

        // TMGetObjectByHash: CAS
        ShareCasObject,
        GetCasObject,

        // TMGetObjectByHash: fetch packs
        ShareFetchPack,
        GetFetchPack,

        // TMGetObjectByHash: transactions
        GetTransactions,

        // TMGetObjectByHash: generic
        ShareHash,
        GetHash,

        // TMProofPathRequest and TMProofPathResponse
        ProofPathRequest,
        ProofPathResponse,

        // TMReplayDeltaRequest and TMReplayDeltaResponse
        ReplayDeltaRequest,
        ReplayDeltaResponse,

        // TMHaveTransactions
        HaveTransactions,

        // TMTransactions
        RequestedTransactions,

        // The total p2p bytes sent and received on the wire
        Total,

        Unknown  // must be last
    };

    TrafficCount() = default;

    /** Given a protocol message, determine which traffic category it belongs to
     */
    static Category
    categorize(
        ::google::protobuf::Message const& message,
        protocol::MessageType type,
        bool inbound);

    /** Account for traffic associated with the given category */
    void
    addCount(Category cat, bool inbound, int bytes)
    {
        XRPL_ASSERT(
            cat <= category::Unknown, "xrpl::TrafficCount::addCount : valid category input");

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
    to_string(Category cat)
    {
        static std::unordered_map<Category, std::string> const kCATEGORY_MAP = {
            {Base, "overhead"},
            {Cluster, "overhead_cluster"},
            {Overlay, "overhead_overlay"},
            {Manifests, "overhead_manifest"},
            {Transaction, "transactions"},
            {TransactionDuplicate, "transactions_duplicate"},
            {Proposal, "proposals"},
            {ProposalUntrusted, "proposals_untrusted"},
            {ProposalDuplicate, "proposals_duplicate"},
            {Validation, "validations"},
            {ValidationUntrusted, "validations_untrusted"},
            {ValidationDuplicate, "validations_duplicate"},
            {Validatorlist, "validator_lists"},
            {Squelch, "squelch"},
            {SquelchSuppressed, "squelch_suppressed"},
            {SquelchIgnored, "squelch_ignored"},
            {GetSet, "set_get"},
            {ShareSet, "set_share"},
            {LdTscGet, "ledger_data_Transaction_Set_candidate_get"},
            {LdTscShare, "ledger_data_Transaction_Set_candidate_share"},
            {LdTxnGet, "ledger_data_Transaction_Node_get"},
            {LdTxnShare, "ledger_data_Transaction_Node_share"},
            {LdAsnGet, "ledger_data_Account_State_Node_get"},
            {LdAsnShare, "ledger_data_Account_State_Node_share"},
            {LdGet, "ledger_data_get"},
            {LdShare, "ledger_data_share"},
            {GlTscShare, "ledger_Transaction_Set_candidate_share"},
            {GlTscGet, "ledger_Transaction_Set_candidate_get"},
            {GlTxnShare, "ledger_Transaction_node_share"},
            {GlTxnGet, "ledger_Transaction_node_get"},
            {GlAsnShare, "ledger_Account_State_node_share"},
            {GlAsnGet, "ledger_Account_State_node_get"},
            {GlShare, "ledger_share"},
            {GlGet, "ledger_get"},
            {ShareHashLedger, "getobject_Ledger_share"},
            {GetHashLedger, "getobject_Ledger_get"},
            {ShareHashTx, "getobject_Transaction_share"},
            {GetHashTx, "getobject_Transaction_get"},
            {ShareHashTxnode, "getobject_Transaction_node_share"},
            {GetHashTxnode, "getobject_Transaction_node_get"},
            {ShareHashAsnode, "getobject_Account_State_node_share"},
            {GetHashAsnode, "getobject_Account_State_node_get"},
            {ShareCasObject, "getobject_CAS_share"},
            {GetCasObject, "getobject_CAS_get"},
            {ShareFetchPack, "getobject_Fetch_Pack_share"},
            {GetFetchPack, "getobject_Fetch Pack_get"},
            {GetTransactions, "getobject_Transactions_get"},
            {ShareHash, "getobject_share"},
            {GetHash, "getobject_get"},
            {ProofPathRequest, "proof_path_request"},
            {ProofPathResponse, "proof_path_response"},
            {ReplayDeltaRequest, "replay_delta_request"},
            {ReplayDeltaResponse, "replay_delta_response"},
            {HaveTransactions, "have_transactions"},
            {RequestedTransactions, "requested_transactions"},
            {Total, "total"}};

        if (auto it = kCATEGORY_MAP.find(cat); it != kCATEGORY_MAP.end())
            return it->second;

        return "unknown";
    }

protected:
    std::unordered_map<Category, TrafficStats> counts_{
        {Base, {Base}},
        {Cluster, {Cluster}},
        {Overlay, {Overlay}},
        {Manifests, {Manifests}},
        {Transaction, {Transaction}},
        {TransactionDuplicate, {TransactionDuplicate}},
        {Proposal, {Proposal}},
        {ProposalUntrusted, {ProposalUntrusted}},
        {ProposalDuplicate, {ProposalDuplicate}},
        {Validation, {Validation}},
        {ValidationUntrusted, {ValidationUntrusted}},
        {ValidationDuplicate, {ValidationDuplicate}},
        {Validatorlist, {Validatorlist}},
        {Squelch, {Squelch}},
        {SquelchSuppressed, {SquelchSuppressed}},
        {SquelchIgnored, {SquelchIgnored}},
        {GetSet, {GetSet}},
        {ShareSet, {ShareSet}},
        {LdTscGet, {LdTscGet}},
        {LdTscShare, {LdTscShare}},
        {LdTxnGet, {LdTxnGet}},
        {LdTxnShare, {LdTxnShare}},
        {LdAsnGet, {LdAsnGet}},
        {LdAsnShare, {LdAsnShare}},
        {LdGet, {LdGet}},
        {LdShare, {LdShare}},
        {GlTscShare, {GlTscShare}},
        {GlTscGet, {GlTscGet}},
        {GlTxnShare, {GlTxnShare}},
        {GlTxnGet, {GlTxnGet}},
        {GlAsnShare, {GlAsnShare}},
        {GlAsnGet, {GlAsnGet}},
        {GlShare, {GlShare}},
        {GlGet, {GlGet}},
        {ShareHashLedger, {ShareHashLedger}},
        {GetHashLedger, {GetHashLedger}},
        {ShareHashTx, {ShareHashTx}},
        {GetHashTx, {GetHashTx}},
        {ShareHashTxnode, {ShareHashTxnode}},
        {GetHashTxnode, {GetHashTxnode}},
        {ShareHashAsnode, {ShareHashAsnode}},
        {GetHashAsnode, {GetHashAsnode}},
        {ShareCasObject, {ShareCasObject}},
        {GetCasObject, {GetCasObject}},
        {ShareFetchPack, {ShareFetchPack}},
        {GetFetchPack, {GetFetchPack}},
        {GetTransactions, {GetTransactions}},
        {ShareHash, {ShareHash}},
        {GetHash, {GetHash}},
        {ProofPathRequest, {ProofPathRequest}},
        {ProofPathResponse, {ProofPathResponse}},
        {ReplayDeltaRequest, {ReplayDeltaRequest}},
        {ReplayDeltaResponse, {ReplayDeltaResponse}},
        {HaveTransactions, {HaveTransactions}},
        {RequestedTransactions, {RequestedTransactions}},
        {Total, {Total}},
        {Unknown, {Unknown}},
    };
};

}  // namespace xrpl
