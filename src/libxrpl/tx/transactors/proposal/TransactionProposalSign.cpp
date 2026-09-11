#include <xrpl/tx/transactors/proposal/TransactionProposalSign.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/ProposalHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/Sign.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/SignerEntries.h>
#include <xrpl/tx/Transactor.h>

#include <algorithm>

namespace xrpl {
namespace {

// ProposalSignature.SigningPubKey must currently authorize signerAccount:
// the account's master key (unless disabled), its regular key, or — for a
// phantom signer — the master key of an account that is not in the ledger.
// Mirrors Transactor::checkSign / checkMultiSign key-binding.
TER
checkSignerKey(
    ReadView const& view,
    AccountID const& signerAccount,
    Slice const& signingPubKey,
    bool const permitPhantom,
    beast::Journal j)
{
    // Defensive: preflight already rejected an unparseable key
    // (temMALFORMED), so a caller from that path never reaches this branch.
    // LCOV_EXCL_START
    if (!publicKeyType(signingPubKey))
    {
        JLOG(j.debug()) << "TransactionProposalSign: unknown key type.";
        return tecNO_PERMISSION;
    }
    // LCOV_EXCL_STOP

    auto const fromKey = calcAccountID(PublicKey(signingPubKey));
    auto const sleSigner = view.read(keylet::account(signerAccount));

    if (fromKey == signerAccount)
    {
        if (!sleSigner)
        {
            // A batch inner may originate from an account an earlier inner
            // creates; that phantom account can only be authorized by its
            // own master key. Mirrors Batch::checkBatchSign's
            // permitUncreatedAccount=true call into Transactor::checkSign.
            if (permitPhantom)
                return tesSUCCESS;
            // Outer / non-batch single-sign path: signerAccount is
            // SigningFor, which is the proposed transaction's target and
            // was verified to exist at TransactionProposalCreate time.
            return tecNO_PERMISSION;  // LCOV_EXCL_LINE
        }
        if (sleSigner->isFlag(lsfDisableMaster))
        {
            JLOG(j.debug()) << "TransactionProposalSign: master key disabled.";
            return tecNO_PERMISSION;
        }
        return tesSUCCESS;
    }

    if (!sleSigner || !sleSigner->isFieldPresent(sfRegularKey) ||
        fromKey != sleSigner->getAccountID(sfRegularKey))
    {
        JLOG(j.debug()) << "TransactionProposalSign: key does not match "
                           "master or regular key.";
        return tecNO_PERMISSION;
    }
    return tesSUCCESS;
}

TER
checkAuthorized(
    ReadView const& view,
    STObject const& proposedTx,
    AccountID const& signingFor,
    STObject const& proposalSignature,
    std::optional<AccountID> const& implicitCounterparty,
    beast::Journal j)
{
    auto const signerAccount = proposalSignature.getAccountID(sfAccount);
    auto const signingPubKey = proposalSignature.getFieldVL(sfSigningPubKey);
    auto const singleSign = signerAccount == signingFor;

    if (singleSign)
    {
        // Every outer role (initiator / Counterparty / Sponsor) targets an
        // account the proposed transaction requires to exist on the ledger,
        // so a phantom signer is only ever allowed for a batch inner
        // participant. Mirrors Batch::checkBatchSign's
        // permitUncreatedAccount=true call into Transactor::checkSign.
        auto const permitPhantom =
            !proposal::isOuterSigningFor(proposedTx, signingFor, implicitCounterparty);
        return checkSignerKey(view, signingFor, makeSlice(signingPubKey), permitPhantom, j);
    }

    auto const sleList = view.read(keylet::signerList(signingFor));
    if (!sleList)
    {
        JLOG(j.debug()) << "TransactionProposalSign: SigningFor has no SignerList.";
        return tecNO_PERMISSION;
    }

    auto const entries = SignerEntries::deserialize(*sleList, j, "ledger");
    if (!entries)
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE

    if (std::ranges::none_of(
            *entries, [&](auto const& entry) { return entry.account == signerAccount; }))
    {
        JLOG(j.debug()) << "TransactionProposalSign: signer is not on "
                           "SigningFor's SignerList.";
        return tecNO_PERMISSION;
    }

    return checkSignerKey(view, signerAccount, makeSlice(signingPubKey), /*permitPhantom=*/true, j);
}

// Resolve any Counterparty this proposed transaction infers from the ledger
// rather than carrying explicitly. A LoanSet without sfCounterparty defaults
// to LoanBroker.Owner (XLS-66 §3.8); all other types have no implicit
// counterparty. Returns nullopt when no inference applies, or when the
// LoanBroker referenced by a LoanSet no longer exists on the ledger (in
// which case the proposal has no recognizable Counterparty signer until —
// or unless — its broker returns).
std::optional<AccountID>
resolveImplicitCounterparty(ReadView const& view, STObject const& proposedTx)
{
    if (proposedTx.getFieldU16(sfTransactionType) != ttLOAN_SET)
        return std::nullopt;
    if (proposedTx.isFieldPresent(sfCounterparty))
        return std::nullopt;
    if (!proposedTx.isFieldPresent(sfLoanBrokerID))
        return std::nullopt;  // LCOV_EXCL_LINE — SoeRequired on LoanSet.

    auto const brokerSle = view.read(keylet::loanBroker(proposedTx.getFieldH256(sfLoanBrokerID)));
    if (!brokerSle)
        return std::nullopt;

    return brokerSle->getAccountID(sfOwner);
}

}  // namespace

NotTEC
TransactionProposalSign::preflight(PreflightContext const& ctx)
{
    if (ctx.tx[sfProposalID] == beast::kZero)
    {
        JLOG(ctx.j.debug()) << "TransactionProposalSign: zero ProposalID.";
        return temMALFORMED;
    }

    auto const proposalSignature = ctx.tx.getFieldObject(sfProposalSignature);
    auto const signingPubKey = proposalSignature.getFieldVL(sfSigningPubKey);
    if (signingPubKey.empty() || proposalSignature.getFieldVL(sfTxnSignature).empty())
    {
        JLOG(ctx.j.debug()) << "TransactionProposalSign: empty key or signature.";
        return temMALFORMED;
    }

    // Stateless parseability check on the signing key: preclaim's verify()
    // requires a well-formed key anyway, and doing it here spares any
    // ledger fetch when the contribution is obviously malformed.
    if (!publicKeyType(makeSlice(signingPubKey)))
    {
        JLOG(ctx.j.debug()) << "TransactionProposalSign: unknown key type.";
        return temMALFORMED;
    }

    return tesSUCCESS;
}

TER
TransactionProposalSign::preclaim(PreclaimContext const& ctx)
{
    auto const proposalID = ctx.tx[sfProposalID];
    auto const sleProposal = ctx.view.read(keylet::txProposal(proposalID));
    if (!sleProposal)
    {
        JLOG(ctx.j.debug()) << "TransactionProposalSign: no such proposal.";
        return tecNO_ENTRY;
    }

    auto proposedTx = sleProposal->getFieldObject(sfProposedTransaction);

    // Terminal proposals are cleaned up in doApply regardless of signer or
    // signature validity, so short-circuit before doing any signing-data or
    // authorization work (On-Chain Cosigner spec §6.3.2.2).
    if (proposal::isTerminal(ctx.view, (*sleProposal)[~sfExpiration], proposedTx))
    {
        JLOG(ctx.j.debug()) << "TransactionProposalSign: proposal is terminal.";
        return tesSUCCESS;
    }

    auto const proposalSignature = ctx.tx.getFieldObject(sfProposalSignature);
    auto const signingFor = ctx.tx.getAccountID(sfSigningFor);
    auto const signerAccount = proposalSignature.getAccountID(sfAccount);
    auto const signingPubKey = proposalSignature.getFieldVL(sfSigningPubKey);
    auto const txnSignature = proposalSignature.getFieldVL(sfTxnSignature);
    auto const implicitCounterparty = resolveImplicitCounterparty(ctx.view, proposedTx);

    auto const data = proposal::signingData(
        proposedTx,
        signingFor,
        signerAccount,
        makeSlice(signingPubKey),
        ctx.view.rules(),
        implicitCounterparty);
    if (!data)
    {
        // LCOV_EXCL_START
        // Defensive: proposedTx was validated at TransactionProposalCreate,
        // so failing to interpret it here means the stored ledger entry is
        // malformed — an internal invariant violation, not a caller error.
        JLOG(ctx.j.debug()) << "TransactionProposalSign: cannot build signing data.";
        return tefINTERNAL;
        // LCOV_EXCL_STOP
    }

    // publicKeyType() was validated in preflight; verify() rejects a bad
    // signature. A caller cannot make it here with an invalid signature.
    if (!verify(PublicKey(makeSlice(signingPubKey)), data->slice(), makeSlice(txnSignature)))
    {
        // The submitted TransactionProposalSign is well-formed and the
        // proposal exists; the contribution just isn't authorized to be
        // recorded, so this is a claimed-fee protocol-level failure rather
        // than a temMALFORMED / temBAD_SIGNATURE that would prevent relay
        // of the outer transaction itself (whose own signature is valid).
        JLOG(ctx.j.debug()) << "TransactionProposalSign: invalid signature "
                               "over the proposed transaction.";
        return tecNO_PERMISSION;
    }

    if (!proposal::isRequiredSigningFor(proposedTx, signingFor, implicitCounterparty))
    {
        JLOG(ctx.j.debug()) << "TransactionProposalSign: SigningFor is not "
                               "required by the proposed transaction.";
        return tecNO_PERMISSION;
    }

    // Reject a SigningFor that plays more than one outer role at once
    // (e.g. both Counterparty and Sponsor). Under fixCleanup3_4_0 each
    // role's payload has a distinct HashPrefix, so one contribution cannot
    // satisfy two slots, and silently routing to a single slot would leave
    // the proposal stuck waiting for the slot no signature will ever land
    // in. Spec §6.1.1 originally called for recording the same
    // contribution in every matching slot; reconciling that with the
    // role-prefix fix is pending, and until then this is a claimed-fee
    // failure with an explicit diagnostic (see hasAmbiguousOuterRole).
    if (proposal::hasAmbiguousOuterRole(proposedTx, signingFor, implicitCounterparty))
    {
        JLOG(ctx.j.debug()) << "TransactionProposalSign: SigningFor plays "
                               "more than one outer role for the proposed "
                               "transaction; role disambiguation is not yet "
                               "specified.";
        return tecNO_PERMISSION;
    }

    if (auto const ret = checkAuthorized(
            ctx.view, proposedTx, signingFor, proposalSignature, implicitCounterparty, ctx.j);
        !isTesSuccess(ret))
        return ret;

    // Duplicate / mode-conflict / oversize are checked against this copy of
    // ProposedTransaction so a rejected contribution cannot mutate ledger state.
    return proposal::recordContribution(
        proposedTx, signingFor, proposalSignature, implicitCounterparty);
}

TER
TransactionProposalSign::doApply()
{
    auto const sleProposal = view().peek(keylet::txProposal(ctx_.tx[sfProposalID]));
    if (!sleProposal)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    auto proposedTx = sleProposal->getFieldObject(sfProposedTransaction);
    if (proposal::isTerminal(view(), (*sleProposal)[~sfExpiration], proposedTx))
    {
        if (auto const ret = proposal::deleteProposal(
                view(), sleProposal, ctx_.registry.get().getJournal("View"));
            !isTesSuccess(ret))
            return ret;  // LCOV_EXCL_LINE — deleteProposal's failure paths are themselves
                         // LCOV_EXCL.
        return tecEXPIRED;
    }

    auto const proposalSignature = ctx_.tx.getFieldObject(sfProposalSignature);
    auto const implicitCounterparty = resolveImplicitCounterparty(view(), proposedTx);
    if (auto const ret = proposal::recordContribution(
            proposedTx,
            ctx_.tx.getAccountID(sfSigningFor),
            proposalSignature,
            implicitCounterparty);
        !isTesSuccess(ret))
        return tefINTERNAL;  // LCOV_EXCL_LINE

    sleProposal->setFieldObject(sfProposedTransaction, proposedTx);
    view().update(sleProposal);
    return tesSUCCESS;
}

void
TransactionProposalSign::visitInvariantEntry(bool, SLE::const_ref, SLE::const_ref)
{
    // No transaction-specific invariants yet (future work).
}

bool
TransactionProposalSign::finalizeInvariants(
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
