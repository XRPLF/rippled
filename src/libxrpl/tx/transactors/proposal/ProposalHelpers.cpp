#include <xrpl/tx/transactors/proposal/ProposalHelpers.h>

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/tx/SignerEntries.h>
#include <xrpl/tx/Transactor.h>

#include <algorithm>
#include <cstdint>
#include <exception>
#include <optional>
#include <utility>
#include <vector>

namespace xrpl::proposal {

TER
deleteProposal(ApplyView& view, SLE::pointer const& sleProposal, beast::Journal j)
{
    // view carries no null contract (a reference), but the two parameters are
    // bound to each other: sleProposal must be a live entry of this same
    // view, since the directory removal, owner-root peek, and erase below all
    // mutate that view assuming they see the entry's state.
    XRPL_ASSERT(
        sleProposal && sleProposal->getType() == ltTRANSACTION_PROPOSAL &&
            view.exists(Keylet{ltTRANSACTION_PROPOSAL, sleProposal->key()}),
        "xrpl::proposal::deleteProposal : valid proposal sle of this view");

    AccountID const owner = sleProposal->getAccountID(sfOwner);

    std::uint64_t const page{(*sleProposal)[sfOwnerNode]};
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

    // Release the reserve against the Owner or, if the entry carries a
    // reserve sponsor, against that sponsor.
    decreaseOwnerCountForObject(
        view,
        sleOwner,
        sleProposal,
        proposalOwnerCount(sleProposal->getFieldObject(sfProposedTransaction)),
        j);

    view.erase(sleProposal);
    return tesSUCCESS;
}

namespace {

/**
 * Whether the signature object is well-formed enough to hand to
 * Transactor::checkSign. Bare or partially-filled objects (a proposal
 * accumulates signatures over time) are simply "not signed yet". Shapes the
 * transaction path asserts rather than checks — a single signature without a
 * SigningPubKey, a Signers entry with an empty one, TxnSignature alongside
 * Signers — must be treated as unauthorized instead of reaching those
 * assertions from a read-only path.
 */
bool
authorizationCheckable(STObject const& sigObject)
{
    bool const hasTxnSignature = sigObject.isFieldPresent(sfTxnSignature);
    if (sigObject.isFieldPresent(sfSigners))
    {
        if (hasTxnSignature)
            return false;
        auto const& signers = sigObject.getFieldArray(sfSigners);
        return !signers.empty() && std::ranges::all_of(signers, [](STObject const& signer) {
            return signer.isFieldPresent(sfAccount) && signer.isFieldPresent(sfSigningPubKey) &&
                !signer.getFieldVL(sfSigningPubKey).empty();
        });
    }
    return hasTxnSignature && sigObject.isFieldPresent(sfSigningPubKey) &&
        !sigObject.getFieldVL(sfSigningPubKey).empty();
}

/**
 * Report multi-signature progress: the account's live SignerQuorum, and the
 * weight the collected Signers entries hold against that live list. Entries
 * that are no longer on the list contribute nothing, matching how they would
 * fare at submission time.
 */
void
reportMultiSignProgress(
    ReadView const& view,
    AccountID const& account,
    STObject const* sigObject,
    SignerStatus& status,
    beast::Journal j)
{
    auto const sleList = view.read(keylet::signerList(account));
    if (sleList)
        status.quorum = sleList->getFieldU32(sfSignerQuorum);

    if (!sigObject || !sigObject->isFieldPresent(sfSigners))
        return;

    std::uint32_t weight = 0;
    if (sleList)
    {
        if (auto const entries = SignerEntries::deserialize(*sleList, j, "ledger"))
        {
            for (STObject const& signer : sigObject->getFieldArray(sfSigners))
            {
                AccountID const id = signer.getAccountID(sfAccount);
                auto const it = std::ranges::find_if(
                    *entries, [&id](auto const& entry) { return entry.account == id; });
                if (it != entries->end())
                    weight += it->weight;
            }
        }
    }
    status.signedWeight = weight;
}

/**
 * Whether the signature material collected for one account currently
 * authorizes it, plus its multi-signature progress. sigObject is null when no
 * signature has been collected for the account yet.
 */
SignerStatus
evaluateAuthorization(
    ReadView const& view,
    AccountID const& account,
    SignerRole role,
    STObject const* sigObject,
    bool permitUncreatedAccount,
    beast::Journal j)
{
    SignerStatus status{.account = account, .role = role};

    // Malformed on-ledger signature material must degrade to "not satisfied"
    // rather than fail the caller (an RPC), so every field access on
    // sigObject stays inside this try.
    try
    {
        reportMultiSignProgress(view, account, sigObject, status, j);

        if (sigObject && authorizationCheckable(*sigObject))
        {
            // The same authorization rule the transaction path applies at
            // preclaim; crypto validity was already checked when the
            // signature was appended to the proposal.
            status.satisfied = isTesSuccess(Transactor::checkSign(
                view, TapNone, std::nullopt, account, *sigObject, j, permitUncreatedAccount));
        }
    }
    catch (std::exception const& e)
    {
        JLOG(j.warn()) << "evaluateAuthorization: signature material for " << toBase58(account)
                       << " is malformed: " << e.what();
        status.satisfied = false;
    }

    return status;
}

}  // namespace

ProposalStatus
evaluateProposal(ReadView const& view, SLE const& sleProposal, beast::Journal j)
{
    XRPL_ASSERT(
        sleProposal.getType() == ltTRANSACTION_PROPOSAL,
        "xrpl::proposal::evaluateProposal : a TransactionProposal entry");

    ProposalStatus result;
    STObject const proposedTx = sleProposal.getFieldObject(sfProposedTransaction);

    AccountID const initiator = proposedTx.isFieldPresent(sfDelegate)
        ? proposedTx.getAccountID(sfDelegate)
        : proposedTx.getAccountID(sfAccount);

    // The initiator's authorization lives in the top-level signature fields.
    // Evaluate them without the sponsor's signature: that is a separate
    // authorization reported on its own row, and Transactor::checkSign would
    // otherwise fold its validity into the initiator's.
    {
        STObject topLevel = proposedTx;
        if (topLevel.isFieldPresent(sfSponsorSignature))
            topLevel.makeFieldAbsent(sfSponsorSignature);
        result.signers.push_back(evaluateAuthorization(
            view, initiator, SignerRole::account, &topLevel, /*permitUncreatedAccount=*/false, j));
    }

    // The required auxiliary co-signer, who signs through
    // CounterpartySignature: the explicit Counterparty or — for a LoanSet,
    // the only type with an implicit one — the owner of its LoanBroker
    // (mirrors LoanSet::checkSign). The implicit rule is keyed on the
    // transaction type, not on sfLoanBrokerID: the LoanBroker* types carry
    // that field too but require no counterparty.
    std::optional<AccountID> counterparty = proposedTx[~sfCounterparty];
    if (!counterparty && proposedTx.getFieldU16(sfTransactionType) == ttLOAN_SET &&
        proposedTx.isFieldPresent(sfLoanBrokerID))
    {
        if (auto const broker =
                view.read(keylet::loanBroker(proposedTx.getFieldH256(sfLoanBrokerID))))
            counterparty = broker->at(sfOwner);
    }
    if (counterparty)
    {
        std::optional<STObject> const sig = proposedTx.isFieldPresent(sfCounterpartySignature)
            ? std::optional<STObject>(proposedTx.getFieldObject(sfCounterpartySignature))
            : std::nullopt;
        result.signers.push_back(evaluateAuthorization(
            view,
            *counterparty,
            SignerRole::counterparty,
            sig ? &*sig : nullptr,
            /*permitUncreatedAccount=*/false,
            j));
    }

    // A Sponsor either co-signs through SponsorSignature or is pre-authorized
    // by an on-ledger Sponsorship entry whose flags do not demand a signature
    // for what this transaction sponsors (mirrors Transactor::checkSponsor).
    if (proposedTx.isFieldPresent(sfSponsor))
    {
        AccountID const sponsor = proposedTx.getAccountID(sfSponsor);
        std::optional<STObject> const sig = proposedTx.isFieldPresent(sfSponsorSignature)
            ? std::optional<STObject>(proposedTx.getFieldObject(sfSponsorSignature))
            : std::nullopt;
        auto status = evaluateAuthorization(
            view, sponsor, SignerRole::sponsor, sig ? &*sig : nullptr, false, j);
        // The pre-authorization fallback applies only while no
        // SponsorSignature has been collected: once the field exists,
        // Transactor::checkSign validates it unconditionally at submission,
        // so a failing (e.g. stale-key) signature must not be rescued here.
        if (!status.satisfied && !sig)
        {
            std::uint32_t const sponsorFlags =
                proposedTx.isFieldPresent(sfSponsorFlags) ? proposedTx.getFieldU32(sfSponsorFlags) : 0;
            if (auto const sleSponsorship = view.read(keylet::sponsorship(sponsor, initiator)))
            {
                bool const feeNeedsSig = ((sponsorFlags & spfSponsorFee) != 0u) &&
                    sleSponsorship->isFlag(lsfSponsorshipRequireSignForFee);
                bool const reserveNeedsSig = ((sponsorFlags & spfSponsorReserve) != 0u) &&
                    sleSponsorship->isFlag(lsfSponsorshipRequireSignForReserve);
                status.satisfied = !feeNeedsSig && !reserveNeedsSig;
            }
        }
        result.signers.push_back(std::move(status));
    }

    // A proposed Batch needs each inner initiator, counterparty, and
    // co-signing sponsor other than the outer account to authorize through a
    // BatchSigners entry (mirrors Batch::preflightSigValidated). BatchSigners
    // entries that no required account matches are ignored here: the Sign
    // transaction never stores one, and completeness cannot come from them.
    if (proposedTx.getFieldU16(sfTransactionType) == ttBATCH)
    {
        AccountID const outerAccount = proposedTx.getAccountID(sfAccount);

        std::vector<std::pair<AccountID, SignerRole>> required;
        auto const addRequired = [&outerAccount, &required](AccountID const& id, SignerRole role) {
            if (id == outerAccount)
                return;
            if (std::ranges::none_of(
                    required, [&id](auto const& entry) { return entry.first == id; }))
                required.emplace_back(id, role);
        };
        for (STObject const& rb : proposedTx.getFieldArray(sfRawTransactions))
        {
            addRequired(
                rb.isFieldPresent(sfDelegate) ? rb.getAccountID(sfDelegate)
                                              : rb.getAccountID(sfAccount),
                SignerRole::batchParticipant);
            if (auto const counterparty = rb[~sfCounterparty])
                addRequired(*counterparty, SignerRole::counterparty);
            if (rb.isFieldPresent(sfSponsor) && rb.isFieldPresent(sfSponsorSignature))
                addRequired(rb.getAccountID(sfSponsor), SignerRole::sponsor);
        }

        STArray const* const batchSigners = proposedTx.isFieldPresent(sfBatchSigners)
            ? &proposedTx.getFieldArray(sfBatchSigners)
            : nullptr;
        auto const findBatchSigner = [batchSigners](AccountID const& id) -> STObject const* {
            if (!batchSigners)
                return nullptr;
            auto const it = std::ranges::find_if(*batchSigners, [&id](STObject const& signer) {
                return signer.getAccountID(sfAccount) == id;
            });
            return it != batchSigners->end() ? &*it : nullptr;
        };

        for (auto const& [id, role] : required)
        {
            // permitUncreatedAccount: an earlier inner transaction may create
            // the signer's account, so authorization by its own master key
            // must count (mirrors Batch::checkBatchSign).
            result.signers.push_back(evaluateAuthorization(
                view, id, role, findBatchSigner(id), /*permitUncreatedAccount=*/true, j));
        }
    }

    // Terminal-first (XLS-0103 §8.1.2): a proposal past its Expiration or its
    // transaction's LastLedgerSequence reports expired even if fully signed.
    // An open view can still include the transaction itself; a closed one
    // only in a successor, matching the tefMAX_LEDGER rule (seq > LLS fails).
    std::uint32_t const earliestSeq = view.seq() + (view.open() ? 0 : 1);
    bool const expired = hasExpired(view, sleProposal[~sfExpiration]) ||
        (proposedTx.isFieldPresent(sfLastLedgerSequence) &&
         proposedTx.getFieldU32(sfLastLedgerSequence) < earliestSeq);

    if (expired)
        result.state = ProposalState::expired;
    else if (std::ranges::all_of(result.signers, [](auto const& s) { return s.satisfied; }))
        result.state = ProposalState::complete;
    else
        result.state = ProposalState::pending;

    return result;
}

}  // namespace xrpl::proposal
