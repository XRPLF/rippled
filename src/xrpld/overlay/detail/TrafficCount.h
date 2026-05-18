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
    enum class Category : std::size_t;

    class TrafficStats
    {
    public:
        std::string name;

        std::atomic<std::uint64_t> bytesIn{0};
        std::atomic<std::uint64_t> bytesOut{0};
        std::atomic<std::uint64_t> messagesIn{0};
        std::atomic<std::uint64_t> messagesOut{0};

        TrafficStats(TrafficCount::Category cat) : name(TrafficCount::toString(cat))
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
    enum class Category : std::size_t {
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
            cat <= Category::Unknown, "xrpl::TrafficCount::addCount : valid category input");

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
    toString(Category cat)
    {
        static std::unordered_map<Category, std::string> const kCategoryMap = {
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

        if (auto it = kCategoryMap.find(cat); it != kCategoryMap.end())
            return it->second;

        return "unknown";
    }

protected:
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
