#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/RPCLedgerHelpers.h>
#include <xrpld/rpc/handlers/ledger/LedgerEntryHelpers.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/json/json_errors.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/tx/transactors/proposal/ProposalHelpers.h>

#include <expected>
#include <memory>

namespace xrpl {

// "signed" is a C++ keyword, so it cannot be declared through the JSS macro.
static json::StaticString const kJssSigned{"signed"};

// A proposal is addressed either by its ledger-entry index (proposal_id) or
// by what that index is derived from: the proposed transaction's target
// account and TicketSequence. Field types, acceptance and error codes match
// ledger_entry's transaction_proposal addressing (parseTransactionProposal).
static std::expected<uint256, json::Value>
parseProposalID(json::Value const& params)
{
    bool const hasProposalID = params.isMember(jss::proposal_id);
    bool const hasAccount = params.isMember(jss::account);
    bool const hasTicketSeq = params.isMember(jss::ticket_seq);

    if (hasProposalID && !hasAccount && !hasTicketSeq)
    {
        return LedgerEntryHelpers::requiredUInt256(params, jss::proposal_id, "malformedRequest");
    }

    if (!hasProposalID && hasAccount && hasTicketSeq)
    {
        auto const target =
            LedgerEntryHelpers::requiredAccountID(params, jss::account, "malformedAddress");
        if (!target)
            return std::unexpected(target.error());

        auto const ticketSeq =
            LedgerEntryHelpers::requiredUInt32(params, jss::ticket_seq, "malformedRequest");
        if (!ticketSeq)
            return std::unexpected(ticketSeq.error());

        return keylet::txProposal(*target, *ticketSeq).key;
    }

    return std::unexpected(
        RPC::makeParamError("Specify either proposal_id or account with ticket_seq."));
}

static char const*
signerRoleLabel(proposal::SignerRole role)
{
    switch (role)
    {
        case proposal::SignerRole::account:
            return "account";
        case proposal::SignerRole::batchParticipant:
            return "batch_participant";
        case proposal::SignerRole::counterparty:
            return "counterparty";
        case proposal::SignerRole::sponsor:
            return "sponsor";
    }
    return "unknown";  // LCOV_EXCL_LINE
}

static char const*
proposalStateLabel(proposal::ProposalState state)
{
    switch (state)
    {
        case proposal::ProposalState::pending:
            return "pending";
        case proposal::ProposalState::complete:
            return "complete";
        case proposal::ProposalState::expired:
            return "expired";
    }
    return "unknown";  // LCOV_EXCL_LINE
}

json::Value
doTransactionProposal(RPC::JsonContext& context)
{
    std::shared_ptr<ReadView const> lpLedger;
    auto jvResult = RPC::lookupLedger(lpLedger, context);

    if (!lpLedger)
        return jvResult;

    uint256 uNodeIndex;
    try
    {
        auto const parsed = parseProposalID(context.params);
        if (!parsed)
            return parsed.error();
        uNodeIndex = *parsed;
    }
    catch (json::Error const&)
    {
        // A wrongly-typed parameter (e.g. an array where a scalar belongs)
        // is the caller's error, not an internal one.
        return RPC::makeError(RpcInvalidParams);
    }

    // The typed keylet makes an index that names a different ledger entry
    // type read as absent rather than as a proposal.
    auto const sleProposal = lpLedger->read(keylet::txProposal(uNodeIndex));
    if (!sleProposal)
    {
        RPC::injectError(RpcEntryNotFound, jvResult);
        return jvResult;
    }

    auto const status = proposal::evaluateProposal(*lpLedger, *sleProposal, context.j);

    jvResult[jss::proposal_id] = to_string(uNodeIndex);
    jvResult[jss::proposal] = sleProposal->getJson(JsonOptions::Values::None);
    jvResult[jss::proposal_status] = proposalStateLabel(status.state);

    json::Value& signers = (jvResult[jss::signing_status] = json::ValueType::Array);
    for (auto const& signer : status.signers)
    {
        json::Value entry{json::ValueType::Object};
        entry[jss::account] = toBase58(signer.account);
        entry[jss::role] = signerRoleLabel(signer.role);
        entry[kJssSigned] = signer.satisfied;
        if (signer.signedWeight)
            entry[jss::signed_weight] = *signer.signedWeight;
        if (signer.quorum)
            entry[jss::quorum] = *signer.quorum;
        signers.append(entry);
    }

    return jvResult;
}

}  // namespace xrpl
