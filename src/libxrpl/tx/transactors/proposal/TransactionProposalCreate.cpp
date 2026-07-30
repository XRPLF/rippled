#include <xrpl/tx/transactors/proposal/TransactionProposalCreate.h>

#include <xrpl/basics/Log.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/ledger/helpers/SponsorHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>
#include <xrpl/tx/applySteps.h>
#include <xrpl/tx/transactors/proposal/ProposalHelpers.h>

#include <cstdint>
#include <exception>
#include <memory>

namespace xrpl {

NotTEC
TransactionProposalCreate::preflight(PreflightContext const& ctx)
{
    if (ctx.tx[sfExpiration] == 0)
    {
        JLOG(ctx.j.debug()) << "TransactionProposalCreate: zero expiration.";
        return temBAD_EXPIRATION;
    }

    STObject const proposedTx = ctx.tx.getFieldObject(sfProposedTransaction);

    if (!proposedTx.isFieldPresent(sfTransactionType) || !proposedTx.isFieldPresent(sfAccount))
    {
        JLOG(ctx.j.debug()) << "TransactionProposalCreate: proposed txn "
                               "lacks TransactionType or Account.";
        return temMALFORMED;
    }

    // The proposed transaction must be independently submittable through the
    // ordinary multi-sign path: no nested proposals, no pseudo-transactions,
    // no batch inner transactions.
    if (isProposalTx(proposedTx))
    {
        JLOG(ctx.j.debug()) << "TransactionProposalCreate: nested proposal.";
        return temINVALID;
    }

    if (isPseudoTx(proposedTx))
    {
        JLOG(ctx.j.debug()) << "TransactionProposalCreate: proposed txn is a "
                               "pseudo-transaction.";
        return temINVALID;
    }

    if (proposedTx.isFieldPresent(sfFlags) &&
        ((proposedTx.getFieldU32(sfFlags) & tfInnerBatchTxn) != 0u))
    {
        JLOG(ctx.j.debug()) << "TransactionProposalCreate: proposed txn "
                               "carries tfInnerBatchTxn.";
        return temINVALID;
    }

    // The proposed transaction is stored in its unsigned canonical form; the
    // ledger populates its signature fields as contributions arrive.
    if (hasSignatureField(proposedTx))
    {
        JLOG(ctx.j.debug()) << "TransactionProposalCreate: proposed txn "
                               "carries signature fields.";
        return temBAD_SIGNER;
    }

    if (!hasEmptySigningPubKey(proposedTx))
    {
        JLOG(ctx.j.debug()) << "TransactionProposalCreate: proposed txn "
                               "SigningPubKey must be present and empty.";
        return temBAD_SIGNER;
    }

    // The proposed transaction's fee is charged to the target account when
    // the completed transaction is submitted, so it must be fixed now.
    if (!proposedTx.isFieldPresent(sfFee) || !proposedTx.isFieldPresent(sfSequence))
    {
        JLOG(ctx.j.debug()) << "TransactionProposalCreate: proposed txn "
                               "lacks Fee or Sequence.";
        return temMALFORMED;
    }

    // The proposed transaction must be ticket-based: it must carry a
    // TicketSequence and must not use a live Sequence. Sequence is a required
    // common field, so "no Sequence" is expressed as a Sequence of 0 rather
    // than an absent field. A ticket decouples the proposal from the target
    // account's live sequence, so unrelated target-account activity cannot
    // invalidate it while signatures are collected (spec §4.2.1).
    if (!proposedTx.isFieldPresent(sfTicketSequence) || proposedTx.getFieldU32(sfSequence) != 0)
        return temSEQ_AND_TICKET;

    // The proposed transaction must pass its own static checks under the
    // current rules, so no statically-dead proposal can be stored. TapDryRun
    // accepts the unsigned canonical form without a signature check;
    // TapProposal additionally skips signature-presence checks (e.g. Batch
    // signer matching), which are deferred to submission time (spec §5.3.1.2).
    try
    {
        STTx const stx{STObject{proposedTx}};
        auto const inner =
            xrpl::preflight(ctx.registry, ctx.rules, stx, TapDryRun | TapProposal, ctx.j);
        if (!isTesSuccess(inner.ter))
        {
            JLOG(ctx.j.debug()) << "TransactionProposalCreate: proposed txn "
                                   "failed preflight: "
                                << transHuman(inner.ter);
            // Surface the proposed transaction type's own preflight code
            // rather than collapsing it to a generic error (spec §5.3.1).
            return inner.ter;
        }
    }
    catch (std::exception const& e)
    {
        JLOG(ctx.j.debug()) << "TransactionProposalCreate: proposed txn is "
                               "malformed: "
                            << e.what();
        return temMALFORMED;
    }

    return tesSUCCESS;
}

TER
TransactionProposalCreate::preclaim(PreclaimContext const& ctx)
{
    if (hasExpired(ctx.view, ctx.tx[~sfExpiration]))
    {
        JLOG(ctx.j.debug()) << "TransactionProposalCreate: already expired.";
        return tecEXPIRED;
    }

    auto const proposedTx = ctx.tx.getFieldObject(sfProposedTransaction);

    // Once the proposed transaction's own ledger bound has passed it can never
    // be applied, so the proposal is dead on arrival. The bound is the one the
    // ordinary path uses for tefMAX_LEDGER: the last ledger in which the
    // proposed transaction may still be submitted (spec §4.5).
    if (proposedTx.isFieldPresent(sfLastLedgerSequence) &&
        proposedTx.getFieldU32(sfLastLedgerSequence) < ctx.view.seq())
    {
        JLOG(ctx.j.debug()) << "TransactionProposalCreate: proposed txn "
                               "LastLedgerSequence has passed.";
        return tecEXPIRED;
    }

    AccountID const target = proposedTx.getAccountID(sfAccount);
    auto const sleTarget = ctx.view.read(keylet::account(target));
    if (!sleTarget)
    {
        JLOG(ctx.j.debug()) << "TransactionProposalCreate: target account "
                               "does not exist.";
        return tecNO_TARGET;
    }

    // A pseudo-account cannot authorize a transaction through a SignerList.
    if (isPseudoAccount(sleTarget))
        return tecNO_PERMISSION;

    std::uint32_t const ticketSequence = proposedTx.getFieldU32(sfTicketSequence);

    if (ctx.view.exists(keylet::txProposal(target, ticketSequence)))
    {
        JLOG(ctx.j.debug()) << "TransactionProposalCreate: duplicate proposal.";
        return tecDUPLICATE;
    }

    return tesSUCCESS;
}

TER
TransactionProposalCreate::doApply()
{
    auto const sle = view().peek(keylet::account(accountID_));
    if (!sle)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    auto const proposedTx = ctx_.tx.getFieldObject(sfProposedTransaction);
    std::uint32_t const ownerCount = proposalOwnerCount(proposedTx);

    // The proposal holds a full transaction plus its collected signatures, so
    // it reserves more than a typical ledger entry (5 increments; 10 for a
    // proposed Batch).
    if (auto const ret = checkReserve(
            ctx_.getApplyViewContext(),
            sle,
            preFeeBalance_,
            {.ownerCountDelta = static_cast<int>(ownerCount)},
            ctx_.journal);
        !isTesSuccess(ret))
        return ret;

    AccountID const target = proposedTx.getAccountID(sfAccount);
    std::uint32_t const ticketSequence = proposedTx.getFieldU32(sfTicketSequence);

    Keylet const proposalKeylet = keylet::txProposal(target, ticketSequence);
    auto sleProposal = std::make_shared<SLE>(proposalKeylet);
    sleProposal->setAccountID(sfOwner, accountID_);
    sleProposal->setFieldObject(sfProposedTransaction, proposedTx);
    sleProposal->setFieldU32(sfExpiration, ctx_.tx[sfExpiration]);

    view().insert(sleProposal);

    auto viewJ = ctx_.registry.get().getJournal("View");
    {
        auto const page = view().dirInsert(
            keylet::ownerDir(accountID_), proposalKeylet, describeOwnerDir(accountID_));
        if (!page)
            return tecDIR_FULL;  // LCOV_EXCL_LINE
        sleProposal->setFieldU64(sfOwnerNode, *page);
    }

    increaseOwnerCount(ctx_.getApplyViewContext(), sle, ownerCount, viewJ);
    addSponsorToLedgerEntry(ctx_.getApplyViewContext(), sleProposal);
    return tesSUCCESS;
}

void
TransactionProposalCreate::visitInvariantEntry(bool, SLE::const_ref, SLE::const_ref)
{
    // No transaction-specific invariants yet (future work). Object-level
    // invariants for the TransactionProposal ledger entry (unsigned canonical
    // form, non-zero Expiration, correct ProposalID key, sorted/unique signer
    // arrays) belong in a protocol-level ValidTransactionProposal check.
}

bool
TransactionProposalCreate::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    // No transaction-specific invariants yet (future work).
    return true;
}

}  // namespace xrpl
