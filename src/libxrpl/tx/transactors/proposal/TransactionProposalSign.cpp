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
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/SignerEntries.h>
#include <xrpl/tx/Transactor.h>

#include <algorithm>

namespace xrpl {
namespace {

// ProposalSignature.SigningPubKey must currently authorize signerAccount:
// the account's master key (unless disabled), its regular key, or — for a
// phantom multi-signer — the master key of an account that is not in the
// ledger. Mirrors Transactor::checkSign / checkMultiSign key-binding.
TER
checkSignerKey(
    ReadView const& view,
    AccountID const& signerAccount,
    Slice const& signingPubKey,
    bool const permitPhantom,
    beast::Journal j)
{
    if (!publicKeyType(signingPubKey))
    {
        JLOG(j.debug()) << "TransactionProposalSign: unknown key type.";
        return temBAD_SIGNATURE;
    }

    auto const fromKey = calcAccountID(PublicKey(signingPubKey));
    auto const sleSigner = view.read(keylet::account(signerAccount));

    if (fromKey == signerAccount)
    {
        if (!sleSigner)
        {
            if (permitPhantom)
                return tesSUCCESS;
            return tecNO_PERMISSION;
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
    AccountID const& signingFor,
    STObject const& proposalSignature,
    beast::Journal j)
{
    auto const signerAccount = proposalSignature.getAccountID(sfAccount);
    auto const signingPubKey = proposalSignature.getFieldVL(sfSigningPubKey);
    auto const singleSign = signerAccount == signingFor;

    if (singleSign)
    {
        return checkSignerKey(
            view, signingFor, makeSlice(signingPubKey), /*permitPhantom=*/false, j);
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
    if (proposalSignature.getFieldVL(sfSigningPubKey).empty() ||
        proposalSignature.getFieldVL(sfTxnSignature).empty())
    {
        JLOG(ctx.j.debug()) << "TransactionProposalSign: empty key or signature.";
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
    auto const proposalSignature = ctx.tx.getFieldObject(sfProposalSignature);
    auto const signingFor = ctx.tx.getAccountID(sfSigningFor);
    auto const signerAccount = proposalSignature.getAccountID(sfAccount);
    auto const signingPubKey = proposalSignature.getFieldVL(sfSigningPubKey);
    auto const txnSignature = proposalSignature.getFieldVL(sfTxnSignature);

    auto const data =
        proposal::signingData(proposedTx, signingFor, signerAccount, makeSlice(signingPubKey));
    if (!data)
    {
        JLOG(ctx.j.debug()) << "TransactionProposalSign: cannot build signing data.";
        return temMALFORMED;
    }

    if (!publicKeyType(makeSlice(signingPubKey)) ||
        !verify(PublicKey(makeSlice(signingPubKey)), data->slice(), makeSlice(txnSignature)))
    {
        JLOG(ctx.j.debug()) << "TransactionProposalSign: invalid signature "
                               "over the proposed transaction.";
        return temBAD_SIGNATURE;
    }

    // Terminal is checked before authorization: a late Sign both fails and
    // cleans up, regardless of whether this signer would have been allowed
    // to contribute (On-Chain Cosigner spec §6.3.2.2).
    if (proposal::isTerminal(ctx.view, (*sleProposal)[~sfExpiration], proposedTx))
    {
        JLOG(ctx.j.debug()) << "TransactionProposalSign: proposal is terminal.";
        return tesSUCCESS;
    }

    if (!proposal::isRequiredSigningFor(proposedTx, signingFor))
    {
        JLOG(ctx.j.debug()) << "TransactionProposalSign: SigningFor is not "
                               "required by the proposed transaction.";
        return tecNO_PERMISSION;
    }

    if (auto const ret = checkAuthorized(ctx.view, signingFor, proposalSignature, ctx.j);
        !isTesSuccess(ret))
        return ret;

    // Duplicate / mode-conflict / oversize are checked against this copy of
    // ProposedTransaction so a rejected contribution cannot mutate ledger state.
    return proposal::recordContribution(proposedTx, signingFor, proposalSignature);
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
            return ret;
        return tecEXPIRED;
    }

    auto const proposalSignature = ctx_.tx.getFieldObject(sfProposalSignature);
    if (auto const ret = proposal::recordContribution(
            proposedTx, ctx_.tx.getAccountID(sfSigningFor), proposalSignature);
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
