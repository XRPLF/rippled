#include <xrpl/ledger/helpers/ProposalHelpers.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Batch.h>
#include <xrpl/protocol/HashPrefix.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/Sign.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/TxFormats.h>

#include <algorithm>
#include <cstdint>
#include <exception>
#include <optional>
#include <utility>

namespace xrpl::proposal {
namespace {

STObject
innerTxn(STObject const& wrapper)
{
    if (wrapper.isFieldPresent(sfTransactionType))
        return wrapper;
    return wrapper.getFieldObject(sfRawTransaction);
}

// The account authorized to sign for `tx` on the ordinary submit path: its
// Delegate if permission delegation is in use, otherwise its Account. Mirrors
// STTx::getInitiator so that a contribution recorded here matches the
// signature Transactor::checkSign will later look for.
AccountID
initiator(STObject const& tx)
{
    if (tx.isFieldPresent(sfDelegate))
        return tx.getAccountID(sfDelegate);
    return tx.getAccountID(sfAccount);
}

STObject*
findBatchSigner(STArray& batchSigners, AccountID const& signingFor)
{
    auto const it = std::ranges::find_if(batchSigners, [&](auto const& entry) {
        return entry.getAccountID(sfAccount) == signingFor;
    });
    return it == batchSigners.end() ? nullptr : &*it;
}

void
sortByAccount(STArray& entries)
{
    std::ranges::sort(entries, [](auto const& lhs, auto const& rhs) {
        return lhs.getAccountID(sfAccount) < rhs.getAccountID(sfAccount);
    });
}

bool
accountPresent(STArray const& signers, AccountID const& account)
{
    return std::ranges::any_of(
        signers, [&](auto const& entry) { return entry.getAccountID(sfAccount) == account; });
}

STObject
makeSignerEntry(STObject const& proposalSignature)
{
    auto entry = STObject::makeInnerObject(sfSigner);
    entry.setAccountID(sfAccount, proposalSignature.getAccountID(sfAccount));
    entry.setFieldVL(sfSigningPubKey, proposalSignature.getFieldVL(sfSigningPubKey));
    entry.setFieldVL(sfTxnSignature, proposalSignature.getFieldVL(sfTxnSignature));
    return entry;
}

TER
recordIntoSigners(STObject& slot, STObject const& proposalSignature, bool const singleSign)
{
    auto const hasSigners = slot.isFieldPresent(sfSigners);
    auto const existingKey =
        slot.isFieldPresent(sfSigningPubKey) ? slot.getFieldVL(sfSigningPubKey) : Blob{};
    auto const hasSingle = !existingKey.empty() && slot.isFieldPresent(sfTxnSignature);

    if (singleSign)
    {
        if (hasSigners)
            return tecNO_PERMISSION;
        if (hasSingle)
            return tecDUPLICATE;
        slot.setFieldVL(sfSigningPubKey, proposalSignature.getFieldVL(sfSigningPubKey));
        slot.setFieldVL(sfTxnSignature, proposalSignature.getFieldVL(sfTxnSignature));
        return tesSUCCESS;
    }

    if (hasSingle)
        return tecNO_PERMISSION;

    auto signers = hasSigners ? slot.getFieldArray(sfSigners) : STArray{sfSigners};
    auto const signerID = proposalSignature.getAccountID(sfAccount);
    if (accountPresent(signers, signerID))
        return tecDUPLICATE;
    // Defensive: SignerListSet caps SignerList at kMaxMultiSigners, so
    // collecting more than that many distinct authorized signers is not
    // reachable through normal contribution flow.
    if (signers.size() >= STTx::kMaxMultiSigners)
        return tecOVERSIZE;  // LCOV_EXCL_LINE

    signers.push_back(makeSignerEntry(proposalSignature));
    sortByAccount(signers);
    slot.setFieldArray(sfSigners, signers);
    if (!slot.isFieldPresent(sfSigningPubKey))
        slot.setFieldVL(sfSigningPubKey, Slice{});
    return tesSUCCESS;
}

}  // namespace

bool
isValidProposal(STObject const& proposedTx)
{
    if (isProposalTx(proposedTx))
        return false;

    if (isPseudoTx(proposedTx))
        return false;

    if (proposedTx.isFieldPresent(sfFlags) &&
        (proposedTx.getFieldU32(sfFlags) & tfInnerBatchTxn) != 0u)
        return false;

    if (proposedTx.getFieldU16(sfTransactionType) == ttBATCH &&
        proposedTx.isFieldPresent(sfRawTransactions))
    {
        auto const& innerTxns = proposedTx.getFieldArray(sfRawTransactions);
        for (auto const& inner : innerTxns)
        {
            auto const tx = innerTxn(inner);
            if (isProposalTx(tx) || isPseudoTx(tx))
                return false;
        }
    }

    return true;
}

bool
isTerminal(
    ReadView const& view,
    std::optional<std::uint32_t> expiration,
    STObject const& proposedTx)
{
    if (hasExpired(view, expiration))
        return true;

    return proposedTx.isFieldPresent(sfLastLedgerSequence) &&
        view.seq() > proposedTx.getFieldU32(sfLastLedgerSequence);
}

TER
deleteProposal(ApplyView& view, SLE::pointer const& sleProposal, beast::Journal j)
{
    XRPL_ASSERT(
        sleProposal && sleProposal->getType() == ltTRANSACTION_PROPOSAL &&
            view.exists(Keylet{ltTRANSACTION_PROPOSAL, sleProposal->key()}),
        "xrpl::proposal::deleteProposal : valid proposal sle of this view");

    auto const owner = sleProposal->getAccountID(sfOwner);

    auto const page = sleProposal->getFieldU64(sfOwnerNode);
    if (!view.dirRemove(keylet::ownerDir(owner), page, sleProposal->key(), true))
    {
        // LCOV_EXCL_START
        JLOG(j.fatal()) << "Unable to delete TransactionProposal from owner.";
        return tefBAD_LEDGER;
        // LCOV_EXCL_STOP
    }

    auto const sleOwner = view.peek(keylet::account(owner));
    if (!sleOwner)
    {
        // LCOV_EXCL_START
        JLOG(j.fatal()) << "Could not find TransactionProposal owner account root.";
        return tefBAD_LEDGER;
        // LCOV_EXCL_STOP
    }

    decreaseOwnerCountForObject(
        view,
        sleOwner,
        sleProposal,
        proposalOwnerCount(sleProposal->getFieldObject(sfProposedTransaction)),
        j);

    view.erase(sleProposal);
    return tesSUCCESS;
}

std::optional<SignatureRole>
auxiliaryRole(
    STObject const& proposedTx,
    AccountID const& signingFor,
    std::optional<AccountID> const& implicitCounterparty)
{
    // The initiator's contribution is a Transaction role, which is expressed
    // by "no auxiliary role" so the two never both hold.
    if (signingFor == initiator(proposedTx))
        return std::nullopt;

    // A LoanSet without sfCounterparty defaults to LoanBroker.Owner
    // (XLS-66 §3.8). Callers resolve the fallback account against the ledger
    // and pass it in as implicitCounterparty; the helper itself is stateless.
    auto const explicitCounter = proposedTx[~sfCounterparty];
    auto const effectiveCounter = explicitCounter ? explicitCounter : implicitCounterparty;
    if (effectiveCounter && *effectiveCounter == signingFor)
        return SignatureRole::Counterparty;

    if (auto const sponsor = proposedTx[~sfSponsor]; sponsor && *sponsor == signingFor)
        return SignatureRole::Sponsor;

    return std::nullopt;
}

bool
isOuterSigningFor(
    STObject const& proposedTx,
    AccountID const& signingFor,
    std::optional<AccountID> const& implicitCounterparty)
{
    // A contribution for the proposed transaction itself: its initiator
    // (Account or Delegate), or an outer Counterparty / Sponsor co-signature.
    // Not an inner Batch participant — those go in sfBatchSigners.
    return signingFor == initiator(proposedTx) ||
        auxiliaryRole(proposedTx, signingFor, implicitCounterparty).has_value();
}

bool
hasAmbiguousOuterRole(
    STObject const& proposedTx,
    AccountID const& signingFor,
    std::optional<AccountID> const& implicitCounterparty)
{
    std::size_t roles = 0;
    if (signingFor == initiator(proposedTx))
        ++roles;

    auto const explicitCounter = proposedTx[~sfCounterparty];
    auto const effectiveCounter = explicitCounter ? explicitCounter : implicitCounterparty;
    if (effectiveCounter && *effectiveCounter == signingFor)
        ++roles;

    if (auto const sponsor = proposedTx[~sfSponsor]; sponsor && *sponsor == signingFor)
        ++roles;

    return roles > 1;
}

bool
isRequiredSigningFor(
    STObject const& proposedTx,
    AccountID const& signingFor,
    std::optional<AccountID> const& implicitCounterparty)
{
    if (isOuterSigningFor(proposedTx, signingFor, implicitCounterparty))
        return true;

    if (proposedTx.getFieldU16(sfTransactionType) != ttBATCH ||
        !proposedTx.isFieldPresent(sfRawTransactions))
        return false;

    // A batch inner participant is any account whose signature the batch's
    // own submit-time check requires, minus the outer account (which never
    // appears in BatchSigners). Mirrors Batch::preflightSigValidated's
    // requiredSigners assembly:
    //   - each inner's initiator (Delegate if permission delegation is in
    //     use, otherwise Account) — a delegated inner is signed by the
    //     Delegate, not the Account holder;
    //   - each inner's sfCounterparty when the transaction type has one.
    //
    // The only in-tree tx type that carries sfCounterparty is LoanSet, and
    // it is currently listed in Batch::kDisabledTxTypes, so the inner
    // counterparty branch below is unreachable through today's amendment
    // set. It is retained to stay in lockstep with Batch's own gate: if a
    // future amendment permits inner LoanSet (or introduces another
    // counterparty-carrying inner tx type), cosign routes those signatures
    // to BatchSigners the same way Batch requires them at submit.
    //
    // Inner sfSponsor is deliberately not recognized here. Batch's own
    // requiredSigners assembly gates the sponsor on
    // rb.isFieldPresent(sfSponsorSignature); a proposal inner is stored in
    // unsigned canonical form and never carries that field, so the batch's
    // own submit-time check would not require the sponsor as a batch
    // signer either. In v1 cosign therefore handles inner reserve
    // sponsorship the pre-funded way: the sponsor establishes the
    // sponsorship SLE ahead of time via SponsorshipTransfer, and no
    // TransactionProposalSign is collected for the sponsor role at the
    // inner level. Co-signed inner sponsorship (a sponsor who signs both
    // the inner Sponsor payload and the batch payload) would require a
    // second signature slot per contribution and is deferred to a
    // follow-up amendment.
    auto const outer = proposedTx.getAccountID(sfAccount);
    return std::ranges::any_of(proposedTx.getFieldArray(sfRawTransactions), [&](auto const& inner) {
        auto const tx = innerTxn(inner);
        if (signingFor == outer)
            return false;
        if (signingFor == initiator(tx))
            return true;
        if (auto const counter = tx[~sfCounterparty]; counter && *counter == signingFor)
            return true;
        return false;
    });
}

std::optional<Serializer>
signingData(
    STObject const& proposedTx,
    AccountID const& signingFor,
    AccountID const& signerAccount,
    Slice const& signingPubKey,
    Rules const& rules,
    std::optional<AccountID> const& implicitCounterparty)
{
    auto const singleSign = signerAccount == signingFor;
    auto const forOuter = isOuterSigningFor(proposedTx, signingFor, implicitCounterparty);
    auto const auxRole = auxiliaryRole(proposedTx, signingFor, implicitCounterparty);

    // Hash the same canonical STTx the ordinary submit path will verify, not
    // the untyped nested object stored on the proposal.
    try
    {
        STTx stx{STObject{proposedTx}};

        // Batch inner: build the XLS-56 BatchSigner payload. The outer
        // account of a Batch signs the standard multi-sign payload and
        // falls through to the branch below.
        if (stx.getTxnType() == ttBATCH && !forOuter)
        {
            Serializer msg;
            serializeBatch(
                msg,
                stx.getAccountID(sfAccount),
                stx.getSeqProxy().value(),
                stx.getFlags(),
                stx.getBatchTransactionIDs());
            if (singleSign)
            {
                finishMultiSigningData(signingFor, msg);
            }
            else
            {
                msg.addBitString(signingFor);
                finishMultiSigningData(signerAccount, msg);
            }
            return msg;
        }

        // Outer level: initiator, or Counterparty / Sponsor co-signer. The
        // role's HashPrefix binds the signature to the slot it is being
        // collected for once fixCleanup3_4_0 is enabled — before it, every
        // role signs the same bytes, which is the pre-fix behavior of every
        // multi-role transaction on the ordinary submit path.
        auto const role = auxRole.value_or(SignatureRole::Transaction);
        auto const prefix = signingPrefix(role, !singleSign, rules);

        if (singleSign)
        {
            // Only the initiator's single-sign publishes its key into the
            // proposed transaction's top-level sfSigningPubKey. A
            // Counterparty / Sponsor single-sign's key lives inside its own
            // signature slot; the outer sfSigningPubKey stays empty (as
            // unsigned canonical form leaves it) and the aux slot itself is
            // excluded by addWithoutSigningFields because sfCounterpartySignature
            // and sfSponsorSignature are both SField::kNotSigning.
            if (role == SignatureRole::Transaction)
                stx.setFieldVL(sfSigningPubKey, signingPubKey);
            Serializer s;
            s.add32(prefix);
            stx.addWithoutSigningFields(s);
            return s;
        }

        // Multi-sign of the outer transaction (initiator, or a Counterparty /
        // Sponsor whose slot itself carries a Signers array). Payload is the
        // same across roles apart from the role prefix — see checkMultiSign
        // for the Transaction path and checkSign's aux-signature branch for
        // the role-slot paths.
        return buildMultiSigningData(stx, signerAccount, prefix);
    }
    catch (std::exception const&)
    {
        // Defensive: proposedTx was validated at TransactionProposalCreate
        // preflight, so an exception building STTx from it is unexpected
        // ledger state, not a normal preclaim path.
        return std::nullopt;  // LCOV_EXCL_LINE
    }
}

TER
recordContribution(
    STObject& proposedTx,
    AccountID const& signingFor,
    STObject const& proposalSignature,
    std::optional<AccountID> const& implicitCounterparty)
{
    auto const singleSign = proposalSignature.getAccountID(sfAccount) == signingFor;
    auto const forOuter = isOuterSigningFor(proposedTx, signingFor, implicitCounterparty);

    if (forOuter)
    {
        // Counterparty and Sponsor contributions land in their own top-level
        // slots (sfCounterpartySignature / sfSponsorSignature), which share
        // the sfBatchSigner inner-object schema: SigningPubKey / TxnSignature
        // / Signers all optional. recordIntoSigners is agnostic to which
        // slot object it is writing into, so the same duplicate / mode-
        // conflict / oversize checks apply to every role.
        auto const auxRole = auxiliaryRole(proposedTx, signingFor, implicitCounterparty);
        if (auxRole)
        {
            SField const* slotField = signatureField(*auxRole);
            XRPL_ASSERT(
                slotField != nullptr,
                "xrpl::proposal::recordContribution : aux role has signature field");
            auto slot = proposedTx.isFieldPresent(*slotField)
                ? proposedTx.getFieldObject(*slotField)
                : STObject::makeInnerObject(*slotField);
            if (auto const ret = recordIntoSigners(slot, proposalSignature, singleSign);
                !isTesSuccess(ret))
                return ret;
            proposedTx.setFieldObject(*slotField, slot);
            return tesSUCCESS;
        }
        // Initiator (Account / Delegate) contribution: written into the
        // outer Signers array or top-level SigningPubKey / TxnSignature.
        return recordIntoSigners(proposedTx, proposalSignature, singleSign);
    }

    auto batchSigners = proposedTx.isFieldPresent(sfBatchSigners)
        ? proposedTx.getFieldArray(sfBatchSigners)
        : STArray{sfBatchSigners};

    if (auto* existing = findBatchSigner(batchSigners, signingFor))
    {
        if (auto const ret = recordIntoSigners(*existing, proposalSignature, singleSign);
            !isTesSuccess(ret))
            return ret;
        sortByAccount(batchSigners);
        proposedTx.setFieldArray(sfBatchSigners, batchSigners);
        return tesSUCCESS;
    }

    // Defensive: kMaxBatchSigners (24) is larger than the ceiling on distinct
    // batch participants a Batch can produce — kMaxBatchTxCount (8) inners,
    // each contributing at most one BatchSigner entry — so the array can never
    // grow large enough through normal contribution flow.
    if (batchSigners.size() >= kMaxBatchSigners)
        return tecOVERSIZE;  // LCOV_EXCL_LINE

    auto entry = STObject::makeInnerObject(sfBatchSigner);
    entry.setAccountID(sfAccount, signingFor);
    if (auto const ret = recordIntoSigners(entry, proposalSignature, singleSign);
        !isTesSuccess(ret))
        return ret;  // LCOV_EXCL_LINE — fresh entry: no existing signers or single-sign, so
                     // recordIntoSigners cannot fail here.
    batchSigners.push_back(std::move(entry));
    sortByAccount(batchSigners);
    proposedTx.setFieldArray(sfBatchSigners, batchSigners);
    return tesSUCCESS;
}

}  // namespace xrpl::proposal
