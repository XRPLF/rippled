#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/RPCLedgerHelpers.h>
#include <xrpld/rpc/handlers/ledger/LedgerEntryHelpers.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/tx/transactors/proposal/ProposalStatus.h>

#include <expected>
#include <memory>
#include <string>

namespace xrpl {

// "signed" is a C++ keyword, so it cannot be declared through the JSS macro.
static json::StaticString const kJssSigned{"signed"};

// A proposal is addressed either by its ledger-entry index (proposal_id) or
// by what that index is derived from: the proposed transaction's target
// account and TicketSequence. Field types, acceptance and error codes match
// ledger_entry's transaction_proposal addressing (parseTransactionProposal),
// as XLS-0103 §8.1.1 requires.
static std::expected<UInt256, json::Value>
parseProposalID(json::Value const& params)
{
    bool const hasProposalID = params.isMember(jss::proposal_id);
    bool const hasAccount = params.isMember(jss::account);
    bool const hasTicketSeq = params.isMember(jss::ticket_seq);

    if (hasProposalID && !hasAccount && !hasTicketSeq)
        return ledger_entry_helpers::requiredUInt256(params, jss::proposal_id, "malformedRequest");

    if (!hasProposalID && hasAccount && hasTicketSeq)
    {
        auto const target =
            ledger_entry_helpers::requiredAccountID(params, jss::account, "malformedAddress");
        if (!target)
            return std::unexpected(target.error());

        auto const ticketSeq =
            ledger_entry_helpers::requiredUInt32(params, jss::ticket_seq, "malformedRequest");
        if (!ticketSeq)
            return std::unexpected(ticketSeq.error());

        return keylet::txProposal(*target, *ticketSeq).key;
    }

    return std::unexpected(
        rpc::makeParamError(
            "Must specify either 'proposal_id' or both 'account' and 'ticket_seq'."));
}

json::Value
doTransactionProposal(rpc::JsonContext& context)
{
    auto const proposalID = parseProposalID(context.params);
    if (!proposalID)
        return proposalID.error();

    std::shared_ptr<ReadView const> lpLedger;
    auto jvResult = rpc::lookupLedger(lpLedger, context);
    if (!lpLedger)
        return jvResult;

    // The typed keylet makes an index that names a different ledger entry
    // type read as absent rather than as a proposal.
    auto const sleProposal = lpLedger->read(keylet::txProposal(*proposalID));
    if (!sleProposal)
    {
        rpc::injectError(RpcEntryNotFound, jvResult);
        return jvResult;
    }

    auto const status = proposal::evaluateProposal(*lpLedger, *sleProposal, context.j);

    jvResult[jss::proposal_id] = to_string(*proposalID);
    jvResult[jss::proposal] = sleProposal->getJson(JsonOptions::Values::None);
    jvResult[jss::proposal_status] = std::string{proposal::toString(status.state)};

    json::Value& rows = (jvResult[jss::signing_status] = json::ValueType::Array);
    for (auto const& authorization : status.authorizations)
    {
        json::Value row{json::ValueType::Object};
        if (authorization.account)
            row[jss::account] = toBase58(*authorization.account);
        row[kJssSigned] = authorization.satisfied;
        if (authorization.reason)
            row[jss::reason] = std::string{proposal::toString(*authorization.reason)};
        if (authorization.signedWeight)
            row[jss::signed_weight] = *authorization.signedWeight;
        if (authorization.quorum)
            row[jss::quorum] = *authorization.quorum;
        if (authorization.signers)
        {
            json::Value& members = (row[jss::signers] = json::ValueType::Array);
            for (auto const& member : *authorization.signers)
            {
                json::Value entry{json::ValueType::Object};
                entry[jss::account] = toBase58(member.account);
                entry[jss::weight] = member.weight;
                entry[kJssSigned] = member.hasSigned;
                members.append(entry);
            }
        }
        rows.append(row);
    }

    // The stored transaction in submit-ready binary form whenever every
    // authorization is satisfied, whatever the lifecycle state: an expired
    // proposal that reached quorum still holds a submittable transaction
    // (XLS-0103 §8.1.2, §13.4).
    if (status.authorized)
    {
        jvResult[jss::tx_blob] =
            strHex(sleProposal->getFieldObject(sfProposedTransaction).getSerializer().slice());
    }

    return jvResult;
}

}  // namespace xrpl
