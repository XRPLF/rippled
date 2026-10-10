#include <xrpl/tx/transactors/proposal/ProposalStatus.h>

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/Log.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/ProposalHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/tx/SignerEntries.h>
#include <xrpl/tx/Transactor.h>
#include <xrpl/tx/applySteps.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iterator>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace xrpl::proposal {

std::string_view
toString(UnsignedReason reason)
{
    switch (reason)
    {
        case UnsignedReason::InadequateSignatures:
            return "inadequate_signatures";
        case UnsignedReason::InvalidSignerSet:
            return "invalid_signer_set";
        case UnsignedReason::NoSignerList:
            return "no_signer_list";
        case UnsignedReason::MasterDisabled:
            return "master_disabled";
        case UnsignedReason::NotAuthorized:
            return "not_authorized";
        case UnsignedReason::AccountNotFound:
            return "account_not_found";
        case UnsignedReason::AwaitingSponsorshipSignature:
            return "awaiting_sponsorship_signature";
        case UnsignedReason::InvalidSponsorship:
            return "invalid_sponsorship";
        case UnsignedReason::SponsorshipEntryRequired:
            return "sponsorship_entry_required";
        case UnsignedReason::CounterpartyUnresolvable:
            return "counterparty_unresolvable";
        case UnsignedReason::NoDelegatePermission:
            return "no_delegate_permission";
        case UnsignedReason::SequenceMismatch:
            return "sequence_mismatch";
        case UnsignedReason::TicketReserved:
            return "ticket_reserved";
        case UnsignedReason::Malformed:
            return "malformed";
    }
    // LCOV_EXCL_START
    UNREACHABLE("xrpl::proposal::toString(UnsignedReason) : unknown reason");
    return "malformed";
    // LCOV_EXCL_STOP
}

std::string_view
toString(ProposalState state)
{
    switch (state)
    {
        case ProposalState::Pending:
            return "pending";
        case ProposalState::Complete:
            return "complete";
        case ProposalState::Expired:
            return "expired";
    }
    // LCOV_EXCL_START
    UNREACHABLE("xrpl::proposal::toString(ProposalState) : unknown state");
    return "pending";
    // LCOV_EXCL_STOP
}

namespace {

/**
 * What a signature slot holds.
 */
enum class Material : std::uint8_t {
    /**
     * The canonical unsigned form: an empty SigningPubKey, no TxnSignature,
     * no Signers. Nothing has been collected for this slot.
     */
    None,
    /**
     * A single signature: SigningPubKey and TxnSignature.
     */
    Single,
    /**
     * Multi-signature shares: a non-empty Signers array.
     */
    Multi,
    /**
     * A shape TransactionProposalSign never produces. The transaction path
     * asserts on some of these rather than checking them, so they must be
     * recognized before Transactor::checkSign is reached from a read-only
     * path.
     */
    Malformed,
};

/**
 * An entry for @p account with nothing decided yet. Built by assignment: a
 * designated initializer that names only some fields trips the
 * missing-field-initializers warning, which the build treats as an error.
 */
AuthorizationStatus
entryFor(std::optional<AccountID> const& account)
{
    AuthorizationStatus status;
    status.account = account;
    return status;
}

/**
 * An entry for @p account that nothing collected can satisfy, for @p reason.
 */
AuthorizationStatus
unsatisfied(std::optional<AccountID> const& account, UnsignedReason reason)
{
    AuthorizationStatus status = entryFor(account);
    status.reason = reason;
    return status;
}

/**
 * Classify a signature slot. Field presence is tested before every access, so
 * a bare object built without its template is classified rather than thrown
 * on.
 */
Material
classify(STObject const& sigObject)
{
    bool const hasTxnSignature = sigObject.isFieldPresent(sfTxnSignature);
    Blob const signingPubKey =
        sigObject.isFieldPresent(sfSigningPubKey) ? sigObject.getFieldVL(sfSigningPubKey) : Blob{};

    if (sigObject.isFieldPresent(sfSigners))
    {
        // A multi-signed object carries no single signature of its own.
        if (hasTxnSignature || !signingPubKey.empty())
            return Material::Malformed;

        auto const& signers = sigObject.getFieldArray(sfSigners);
        if (signers.empty())
            return Material::Malformed;

        for (STObject const& signer : signers)
        {
            if (!signer.isFieldPresent(sfAccount) || !signer.isFieldPresent(sfSigningPubKey) ||
                !signer.isFieldPresent(sfTxnSignature))
            {
                return Material::Malformed;
            }

            auto const key = signer.getFieldVL(sfSigningPubKey);
            if (key.empty() || !publicKeyType(makeSlice(key)))
                return Material::Malformed;
        }
        return Material::Multi;
    }

    if (!hasTxnSignature && signingPubKey.empty())
        return Material::None;

    if (!hasTxnSignature || signingPubKey.empty() || !publicKeyType(makeSlice(signingPubKey)))
        return Material::Malformed;

    return Material::Single;
}

/**
 * Whether a multi-sign member's collected key currently authorizes it, by the
 * per-entry rule of Transactor::checkMultiSign: a phantom account (one not in
 * the ledger) signing with its own master key, an enabled master key, or the
 * account's current regular key. Progress detail only; the row's verdict
 * comes from Transactor::checkSign itself.
 *
 * @param view The ledger to evaluate against.
 * @param account The member account.
 * @param signingPubKey The member's collected key; already known to parse.
 */
bool
memberKeyAuthorizes(ReadView const& view, AccountID const& account, Blob const& signingPubKey)
{
    auto const fromKey = calcAccountID(PublicKey(makeSlice(signingPubKey)));
    auto const sleRoot = view.read(keylet::account(account));

    if (fromKey == account)
        return !sleRoot || !sleRoot->isFlag(lsfDisableMaster);

    return sleRoot && sleRoot->isFieldPresent(sfRegularKey) &&
        sleRoot->getAccountID(sfRegularKey) == fromKey;
}

/**
 * The reason behind a failed Transactor::checkSign verdict.
 */
UnsignedReason
reasonFor(NotTEC ter)
{
    if (ter == tefNOT_MULTI_SIGNING)
        return UnsignedReason::NoSignerList;
    if (ter == tefBAD_QUORUM)
        return UnsignedReason::InadequateSignatures;
    if (ter == tefBAD_SIGNATURE)
        return UnsignedReason::InvalidSignerSet;
    if (ter == tefMASTER_DISABLED)
        return UnsignedReason::MasterDisabled;
    if (ter == tefBAD_AUTH)
        return UnsignedReason::NotAuthorized;
    if (ter == terNO_ACCOUNT)
        return UnsignedReason::AccountNotFound;

    // Anything else means the ledger state itself could not be interpreted,
    // such as a SignerList that does not deserialize.
    return UnsignedReason::Malformed;
}

/**
 * Evaluate one required authorization, trusting the ledger state and the
 * signature material to be decodable; see evaluateAuthorization.
 *
 * @param view The ledger to evaluate against.
 * @param account The account whose authorization is required.
 * @param sigObject The signature slot collected for it, or null when the
 *        slot does not exist.
 * @param permitUncreatedAccount Whether an account absent from the ledger may
 *        authorize with its own master key, as a Batch participant that an
 *        earlier inner transaction creates may.
 * @param j Journal for logging.
 */
AuthorizationStatus
evaluateDecodable(
    ReadView const& view,
    AccountID const& account,
    STObject const* sigObject,
    bool permitUncreatedAccount,
    beast::Journal j)
{
    AuthorizationStatus status = entryFor(account);

    // An account that is not in the ledger cannot be authorized by anything
    // collected for it, whatever the material says; a Batch participant is the
    // exception, since an earlier inner transaction may create it.
    if (!permitUncreatedAccount && !view.exists(keylet::account(account)))
    {
        status.reason = UnsignedReason::AccountNotFound;
        return status;
    }

    // The live SignerList: the quorum, and the roster a wallet chases.
    std::optional<std::vector<SignerEntries::SignerEntry>> members;
    if (auto const sleList = view.read(keylet::signerList(account)))
    {
        status.quorum = sleList->getFieldU32(sfSignerQuorum);
        if (auto entries = SignerEntries::deserialize(*sleList, j, "ledger"))
        {
            members = std::move(*entries);
            status.signers.emplace();
            for (auto const& member : *members)
            {
                status.signers->push_back(
                    {.account = member.account, .weight = member.weight, .hasSigned = false});
            }
        }
        // A list that does not deserialize is reported through the verdict
        // below, which fails on it the same way submission would.
    }

    Material const material = sigObject != nullptr ? classify(*sigObject) : Material::None;

    if (material == Material::None)
    {
        status.reason = UnsignedReason::InadequateSignatures;
        return status;
    }
    if (material == Material::Malformed)
    {
        status.reason = UnsignedReason::Malformed;
        return status;
    }

    if (material == Material::Multi)
    {
        // Progress detail: the weight of the collected shares the live list
        // authorizes right now. A share from an account the list no longer
        // names, or whose key no longer authorizes it, adds nothing here and
        // voids the whole set in the verdict below.
        std::uint32_t weight = 0;
        if (members && status.signers)
        {
            for (STObject const& signer : sigObject->getFieldArray(sfSigners))
            {
                AccountID const id = signer.getAccountID(sfAccount);
                auto const it =
                    std::ranges::find(*members, id, &SignerEntries::SignerEntry::account);
                if (it == members->end() ||
                    !memberKeyAuthorizes(view, id, signer.getFieldVL(sfSigningPubKey)))
                {
                    continue;
                }

                weight += it->weight;
                (*status.signers)[std::distance(members->begin(), it)].hasSigned = true;
            }
            status.signedWeight = weight;
        }
    }

    // The verdict: the authorization rule submission applies.
    auto const ter = Transactor::checkSign(
        view, TapNone, std::nullopt, account, *sigObject, j, permitUncreatedAccount);
    if (isTesSuccess(ter))
    {
        status.satisfied = true;
    }
    else
    {
        status.reason = reasonFor(ter);
    }

    return status;
}

/**
 * Evaluate one required authorization.
 *
 * Ledger state or signature material that cannot even be decoded, such as a
 * SignerList whose entries do not deserialize, authorizes nothing. It is
 * reported as malformed rather than propagated, so that a read-only caller is
 * not failed by it.
 *
 * @param view The ledger to evaluate against.
 * @param account The account whose authorization is required.
 * @param sigObject The signature slot collected for it, or null when the
 *        slot does not exist.
 * @param permitUncreatedAccount Whether an account absent from the ledger may
 *        authorize with its own master key, as a Batch participant that an
 *        earlier inner transaction creates may.
 * @param j Journal for logging.
 */
AuthorizationStatus
evaluateAuthorization(
    ReadView const& view,
    AccountID const& account,
    STObject const* sigObject,
    bool permitUncreatedAccount,
    beast::Journal j)
{
    try
    {
        return evaluateDecodable(view, account, sigObject, permitUncreatedAccount, j);
    }
    catch (std::exception const& e)
    {
        JLOG(j.warn()) << "evaluateAuthorization: cannot evaluate " << toBase58(account) << ": "
                       << e.what();
        return unsatisfied(account, UnsignedReason::Malformed);
    }
}

/**
 * Whether the proposed transaction requires a co-signature through
 * CounterpartySignature: it carries a Counterparty, or it is a LoanSet, the
 * only type with an implicit one (LoanSet::checkSign). The implicit rule is
 * keyed on the transaction type, not on sfLoanBrokerID: the LoanBroker*
 * transactions carry that field too but require no counterparty.
 */
bool
requiresCounterparty(STObject const& proposedTx)
{
    return proposedTx.isFieldPresent(sfCounterparty) ||
        proposedTx.getFieldU16(sfTransactionType) == ttLOAN_SET;
}

/**
 * The account that must co-sign through CounterpartySignature: the explicit
 * Counterparty or, for a LoanSet, the owner of the LoanBroker it names,
 * mirroring LoanSet::checkSign. A LoanSet whose LoanBroker no longer exists
 * yields none: the co-signer is still required but can no longer be named,
 * and submission fails the transaction with temBAD_SIGNER.
 */
std::optional<AccountID>
resolveCounterparty(ReadView const& view, STObject const& proposedTx)
{
    if (auto const counterparty = proposedTx[~sfCounterparty])
        return counterparty;

    if (proposedTx.isFieldPresent(sfLoanBrokerID))
    {
        if (auto const broker =
                view.read(keylet::loanBroker(proposedTx.getFieldH256(sfLoanBrokerID))))
        {
            return broker->getAccountID(sfOwner);
        }
    }

    return std::nullopt;
}

/**
 * The sponsorship rule submission applies before it looks at any signature,
 * Transactor::checkSponsor, on the transaction as it would be submitted.
 *
 * @param view The ledger to evaluate against.
 * @param sponsored The transaction carrying sfSponsor: the proposed
 *        transaction, or one of a proposed Batch's inner transactions.
 * @return checkSponsor's verdict, or tefINTERNAL when the stored transaction
 *         cannot be rebuilt as one.
 */
NotTEC
sponsorRule(ReadView const& view, STObject const& sponsored)
{
    try
    {
        STTx const tx{STObject{sponsored}};
        return Transactor::checkSponsor(view, tx);
    }
    catch (std::exception const&)
    {
        return tefINTERNAL;
    }
}

/**
 * Evaluate a Sponsor's authorization, for the proposed transaction itself or
 * for an inner transaction of a proposed Batch that does not co-sign.
 *
 * The verdict starts from Transactor::checkSponsor, the rule submission
 * applies before it looks at any signature: a sponsorship that rule rejects
 * outright (reserve sponsorship on a delegated transaction, temINVALID) can
 * never be satisfied, a sponsor absent from the ledger cannot authorize, and
 * a sponsor with no SponsorSignature collected is pre-authorized only by an
 * on-ledger Sponsorship entry between it and the initiator whose flags do
 * not demand a co-signature for what the transaction sponsors. A collected
 * SponsorSignature is then judged like any other slot; submission validates
 * it unconditionally, so a stale one is not rescued by the entry.
 *
 * @param view The ledger to evaluate against.
 * @param sponsored The transaction carrying sfSponsor: the proposed
 *        transaction, or one of a proposed Batch's inner transactions.
 * @param initiator That transaction's Delegate, or its Account.
 * @param canCollectSignature Whether a SponsorSignature can still be
 *        collected for this slot. False for a Batch inner transaction that
 *        does not co-sign, whose only remedy is a Sponsorship entry.
 * @param j Journal for logging.
 */
AuthorizationStatus
evaluateSponsor(
    ReadView const& view,
    STObject const& sponsored,
    AccountID const& initiator,
    bool canCollectSignature,
    beast::Journal j)
{
    AccountID const sponsor = sponsored.getAccountID(sfSponsor);
    AuthorizationStatus status = entryFor(sponsor);

    auto const rule = sponsorRule(view, sponsored);
    if (rule == tefINTERNAL)
    {
        status.reason = UnsignedReason::Malformed;
        return status;
    }
    if (rule == temINVALID)
    {
        status.reason = UnsignedReason::InvalidSponsorship;
        return status;
    }
    if (rule == terNO_ACCOUNT)
    {
        status.reason = UnsignedReason::AccountNotFound;
        return status;
    }

    if (sponsored.isFieldPresent(sfSponsorSignature))
    {
        STObject const signature = sponsored.getFieldObject(sfSponsorSignature);
        return evaluateAuthorization(view, sponsor, &signature, false, j);
    }

    status = evaluateAuthorization(view, sponsor, nullptr, false, j);
    if (isTesSuccess(rule))
    {
        status.satisfied = true;
        status.reason.reset();
    }
    else if (!canCollectSignature)
    {
        // Nothing can be collected for this sponsor; only the Sponsorship
        // entry can change.
        status.reason = UnsignedReason::SponsorshipEntryRequired;
    }
    else if (view.exists(keylet::sponsorship(sponsor, initiator)))
    {
        // terNO_PERMISSION with an entry present: its flags demand the
        // co-signature. Without one, nothing has pre-authorized the sponsor
        // and the default reason stands.
        status.reason = UnsignedReason::AwaitingSponsorshipSignature;
    }
    return status;
}

/**
 * Whether a transaction's Delegate currently holds permission for it, by the
 * rule submission applies: xrpl::invokeCheckPermission, the type-erased
 * submission hierarchy of transaction-level permission, granular
 * permissions, and that transaction type's own granular semantics.
 *
 * @param view The ledger to evaluate against.
 * @param delegated The transaction carrying sfDelegate: the proposed
 *        transaction, or one of a proposed Batch's inner transactions.
 * @return tesSUCCESS when permitted; terNO_DELEGATE_PERMISSION otherwise,
 *         or tefINTERNAL when the stored transaction cannot be rebuilt. The
 *         STTx is built first so an unknown type is caught here rather than
 *         reaching the dispatch.
 */
NotTEC
delegatePermission(ReadView const& view, STObject const& delegated)
{
    try
    {
        STTx const tx{STObject{delegated}};
        return xrpl::invokeCheckPermission(view, tx);
    }
    catch (std::exception const&)
    {
        return tefINTERNAL;
    }
}

/**
 * Mark the entry an inner transaction's rule fails unsatisfied for
 * @p reason: the participant entry naming @p account, or the outer account's
 * own entry when the inner's Delegate or co-signing Sponsor is the outer
 * account, which is never a participant. An outer Sponsor's entry is never
 * the target, even when the same account is also a participant: that slot
 * is judged on its own.
 *
 * @param result The status being built; the outer account's entry is first.
 * @param participantsBegin Index of the first participant entry.
 * @param account The inner Delegate or co-signing Sponsor the rule failed.
 * @param reason Why.
 */
void
overrideEntry(
    ProposalStatus& result,
    std::size_t participantsBegin,
    AccountID const& account,
    UnsignedReason reason)
{
    auto const sameAccount = [&account](AuthorizationStatus const& status) {
        return status.account == account;
    };
    auto it = std::ranges::find_if(
        result.authorizations.begin() + participantsBegin,
        result.authorizations.end(),
        sameAccount);
    if (it == result.authorizations.end() && sameAccount(result.authorizations.front()))
        it = result.authorizations.begin();

    // A turn gate is the first thing any inner's preclaim applies, so once an
    // entry reports one, no later inner's grant or sponsorship verdict
    // replaces it: the Batch fails on the earlier inner first.
    if (it != result.authorizations.end() &&
        (it->reason == UnsignedReason::SequenceMismatch ||
         it->reason == UnsignedReason::TicketReserved))
    {
        return;
    }

    if (it == result.authorizations.end())
    {
        // LCOV_EXCL_START
        // Unreachable while a Batch is not delegable: the outer account is
        // then the first entry, and every inner Delegate or co-signing
        // Sponsor is either it or a participant. Kept so a future delegable
        // Batch degrades to an unsatisfied entry rather than a lost verdict.
        result.authorizations.push_back(unsatisfied(account, reason));
        return;
        // LCOV_EXCL_STOP
    }
    it->satisfied = false;
    it->reason = reason;
}

/**
 * Whether an inner transaction earlier than the first one that needs
 * @p account's authorization creates that account, on the terms
 * Payment::preclaim applies on the queried ledger: an XRP Payment to it that
 * is not partial, names no DomainID (a payment into a permissioned domain
 * never creates an account), and either meets the account reserve or carries
 * tfSponsorCreatedAccount. Submission lets such an account authorize its
 * BatchSigners entry with its own master key before it exists
 * (Batch::checkBatchSign, permitUncreatedAccount) and sees it created by the
 * time its own inner transaction is judged; any other account absent from
 * the ledger cannot act, and the inner it signs for never applies.
 */
bool
createdByEarlierInner(ReadView const& view, STObject const& batch, AccountID const& account)
{
    bool created = false;
    for (STObject const& inner : batch.getFieldArray(sfRawTransactions))
    {
        AccountID const authorizer = inner.isFieldPresent(sfDelegate)
            ? inner.getAccountID(sfDelegate)
            : inner.getAccountID(sfAccount);
        bool const needed = authorizer == account || inner[~sfCounterparty] == account ||
            (inner.isFieldPresent(sfSponsorSignature) && inner[~sfSponsor] == account);
        if (needed)
            return created;

        if (inner.getFieldU16(sfTransactionType) != ttPAYMENT ||
            !inner.isFieldPresent(sfDestination) || !inner.isFieldPresent(sfAmount) ||
            inner.getAccountID(sfDestination) != account)
        {
            continue;
        }

        STAmount const amount = inner.getFieldAmount(sfAmount);
        std::uint32_t const flags = inner.isFieldPresent(sfFlags) ? inner.getFieldU32(sfFlags) : 0;
        if (amount.native() && (flags & tfPartialPayment) == 0u &&
            !inner.isFieldPresent(sfDomainID) &&
            (amount >= STAmount{view.fees().reserve} || (flags & tfSponsorCreatedAccount) != 0u))
        {
            created = true;
        }
    }
    return created;
}

/**
 * Why inner transaction @p index of a proposed Batch could not take its turn
 * at submission, by the two gates each inner's preclaim applies before any
 * signature rule, Transactor::checkSeqProxy and checkPriorTxAndLastLedger,
 * on the view the inner meets: the outer transaction and the earlier inners
 * have applied by then (Batch applies them in order on one cumulative view).
 * Its Sequence must be the one its account holds once the earlier inners of
 * the same account have consumed theirs, a TicketCreate among them advancing
 * it by its TicketCount as well (TicketCreate::doApply); a TicketSequence
 * must name a Ticket neither the outer transaction nor an earlier inner
 * spends, that an earlier TicketCreate of the same account creates or that is
 * in the ledger, and that no other live proposal reserves
 * (proposal::canConsumeTicket);
 * a LastLedgerSequence must not lie before the earliest ledger the Batch could
 * enter. An AccountTxnID is judged only for the account's first transaction
 * in the Batch: an earlier one sets it to its own hash, which for the outer
 * transaction is not fixed until the Batch is complete (an earlier inner's
 * would be, but is not modelled). An account absent from the queried ledger is
 * not judged either: its own entry says whether an earlier inner creates it,
 * and a new account's first Sequence is fixed only when it is created.
 *
 * @return The reason, or none when the inner can take its turn.
 */
std::optional<UnsignedReason>
innerTurnFailure(ReadView const& view, STObject const& batch, std::size_t index)
{
    STArray const& inners = batch.getFieldArray(sfRawTransactions);
    STObject const& inner = inners[index];
    AccountID const account = inner.getAccountID(sfAccount);

    auto const sleAccount = view.read(keylet::account(account));
    if (!sleAccount)
        return std::nullopt;

    // Replay what this Batch does to the account before the inner runs: the
    // running Sequence, the Tickets spent, and the Tickets created. A
    // TicketCreate first consumes its own Sequence (or Ticket), then creates
    // TicketCount Tickets numbered from the Sequence the account then holds,
    // and advances the Sequence past them.
    bool earlierSameAccount = batch.getAccountID(sfAccount) == account;
    std::uint32_t expectedSequence = sleAccount->getFieldU32(sfSequence);
    std::vector<std::uint32_t> spentTickets;
    std::vector<std::uint32_t> createdTickets;
    if (earlierSameAccount && batch.isFieldPresent(sfTicketSequence))
        spentTickets.push_back(batch.getFieldU32(sfTicketSequence));
    for (std::size_t i = 0; i < index; ++i)
    {
        STObject const& earlier = inners[i];
        if (earlier.getAccountID(sfAccount) != account)
            continue;
        earlierSameAccount = true;
        if (earlier.getFieldU32(sfSequence) != 0)
        {
            ++expectedSequence;
        }
        else if (earlier.isFieldPresent(sfTicketSequence))
        {
            spentTickets.push_back(earlier.getFieldU32(sfTicketSequence));
        }
        if (earlier.getFieldU16(sfTransactionType) == ttTICKET_CREATE &&
            earlier.isFieldPresent(sfTicketCount))
        {
            std::uint32_t const count = earlier.getFieldU32(sfTicketCount);
            for (std::uint32_t t = 0; t < count; ++t)
                createdTickets.push_back(expectedSequence + t);
            expectedSequence += count;
        }
    }

    if (!earlierSameAccount && inner.isFieldPresent(sfAccountTxnID) &&
        sleAccount->getFieldH256(sfAccountTxnID) != inner.getFieldH256(sfAccountTxnID))
    {
        return UnsignedReason::SequenceMismatch;
    }

    if (inner.isFieldPresent(sfLastLedgerSequence))
    {
        std::uint32_t const earliestSeq = view.open() ? view.seq() : view.seq() + 1;
        if (inner.getFieldU32(sfLastLedgerSequence) < earliestSeq)
            return UnsignedReason::SequenceMismatch;
    }

    std::uint32_t const sequence = inner.getFieldU32(sfSequence);
    if (sequence == 0 && inner.isFieldPresent(sfTicketSequence))
    {
        std::uint32_t const ticketSeq = inner.getFieldU32(sfTicketSequence);
        if (std::ranges::find(spentTickets, ticketSeq) != spentTickets.end())
            return UnsignedReason::SequenceMismatch;

        // A Ticket an earlier inner creates exists by the time this one runs,
        // and nothing can have reserved it yet.
        if (std::ranges::find(createdTickets, ticketSeq) != createdTickets.end())
            return std::nullopt;

        if (!view.exists(keylet::ticket(account, SeqProxy::rawTicket(ticketSeq))))
            return UnsignedReason::SequenceMismatch;

        // Reserved while another proposal keyed to it is live: only that
        // proposal's own transaction may spend it.
        if (auto const sleProposal = view.read(keylet::txProposal(account, ticketSeq));
            sleProposal &&
            !payloadMatches(sleProposal->getFieldObject(sfProposedTransaction), inner))
        {
            return UnsignedReason::TicketReserved;
        }
        return std::nullopt;
    }

    if (sequence != expectedSequence)
        return UnsignedReason::SequenceMismatch;
    return std::nullopt;
}

/**
 * The accounts a proposed Batch requires a BatchSigners entry from, sorted
 * and unique, mirroring Batch::preflightSigValidated: each inner
 * transaction's Delegate or Account, each inner Counterparty, and each inner
 * Sponsor that co-signs (the inner carries a SponsorSignature slot), all
 * other than the outer account, which authorizes its inners by signing the
 * Batch itself.
 */
std::vector<AccountID>
requiredBatchSigners(STObject const& batch)
{
    AccountID const outerAccount = batch.getAccountID(sfAccount);

    std::vector<AccountID> required;
    for (STObject const& inner : batch.getFieldArray(sfRawTransactions))
    {
        AccountID const authorizer = inner.isFieldPresent(sfDelegate)
            ? inner.getAccountID(sfDelegate)
            : inner.getAccountID(sfAccount);
        if (authorizer != outerAccount)
            required.push_back(authorizer);

        if (auto const counterparty = inner[~sfCounterparty];
            counterparty && *counterparty != outerAccount)
        {
            required.push_back(*counterparty);
        }

        if (auto const sponsor = inner[~sfSponsor];
            sponsor && inner.isFieldPresent(sfSponsorSignature) && *sponsor != outerAccount)
        {
            required.push_back(*sponsor);
        }
    }

    std::ranges::sort(required);
    auto const duplicates = std::ranges::unique(required);
    required.erase(duplicates.begin(), duplicates.end());
    return required;
}

/**
 * Whether the proposal is terminal on this ledger (XLS-0103 §8.1.3.3): its
 * Expiration has been reached by the parent close time, or the proposed
 * transaction's LastLedgerSequence lies before the earliest ledger it could
 * still enter. That ledger is the queried one itself while it is open, and
 * the next one once it has closed, so a closed ledger whose sequence equals
 * the bound already puts the transaction out of reach.
 */
bool
isExpired(ReadView const& view, SLE const& sleProposal, STObject const& proposedTx)
{
    if (hasExpired(view, sleProposal[~sfExpiration]))
        return true;

    if (!proposedTx.isFieldPresent(sfLastLedgerSequence))
        return false;

    std::uint32_t const earliestSeq = view.open() ? view.seq() : view.seq() + 1;
    return proposedTx.getFieldU32(sfLastLedgerSequence) < earliestSeq;
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

    // A delegated transaction is authorized by its Delegate alone
    // (Transactor::checkSign on a PreclaimContext).
    AccountID const initiator = proposedTx.isFieldPresent(sfDelegate)
        ? proposedTx.getAccountID(sfDelegate)
        : proposedTx.getAccountID(sfAccount);

    // The initiator authorizes through the top-level signature fields. The
    // Sponsor's slot is a separate authorization with its own row, so it is
    // removed first: Transactor::checkSign would otherwise fold its validity
    // into the initiator's verdict.
    {
        STObject topLevel = proposedTx;
        if (topLevel.isFieldPresent(sfSponsorSignature))
            topLevel.makeFieldAbsent(sfSponsorSignature);
        result.authorizations.push_back(
            evaluateAuthorization(view, initiator, &topLevel, false, j));

        // A Delegate acts only while the target's DelegateSet grant covers
        // this transaction; a revoked grant makes the collected material moot,
        // so it takes precedence over the signature verdict.
        if (proposedTx.isFieldPresent(sfDelegate))
        {
            if (auto const permission = delegatePermission(view, proposedTx);
                !isTesSuccess(permission))
            {
                auto& row = result.authorizations.back();
                row.satisfied = false;
                row.reason = permission == tefINTERNAL ? UnsignedReason::Malformed
                                                       : UnsignedReason::NoDelegatePermission;
            }
        }
    }

    if (requiresCounterparty(proposedTx))
    {
        if (auto const counterparty = resolveCounterparty(view, proposedTx))
        {
            std::optional<STObject> const signature =
                proposedTx.isFieldPresent(sfCounterpartySignature)
                ? std::optional{proposedTx.getFieldObject(sfCounterpartySignature)}
                : std::nullopt;
            result.authorizations.push_back(evaluateAuthorization(
                view, *counterparty, signature ? &*signature : nullptr, false, j));
        }
        else
        {
            // Required but unresolvable: the LoanBroker a LoanSet names is
            // gone, so LoanSet::checkSign fails the transaction with
            // temBAD_SIGNER whatever was collected. Reported as an entry
            // nothing can satisfy, with no account to chase.
            result.authorizations.push_back(
                unsatisfied(std::nullopt, UnsignedReason::CounterpartyUnresolvable));
        }
    }

    if (proposedTx.isFieldPresent(sfSponsor))
    {
        result.authorizations.push_back(
            evaluateSponsor(view, proposedTx, initiator, /*canCollectSignature=*/true, j));
    }

    if (proposedTx.getFieldU16(sfTransactionType) == ttBATCH)
    {
        // BatchSigners entries that no required account matches are ignored:
        // TransactionProposalSign never stores one, and completeness cannot
        // come from them.
        STArray const* const batchSigners = proposedTx.isFieldPresent(sfBatchSigners)
            ? &proposedTx.getFieldArray(sfBatchSigners)
            : nullptr;
        auto const slotFor = [batchSigners](AccountID const& id) -> STObject const* {
            if (!batchSigners)
                return nullptr;
            auto const it = std::ranges::find_if(*batchSigners, [&id](STObject const& signer) {
                return signer.isFieldPresent(sfAccount) && signer.getAccountID(sfAccount) == id;
            });
            return it != batchSigners->end() ? &*it : nullptr;
        };

        // A participant an earlier inner transaction creates may authorize
        // with its own master key before it exists (Batch::checkBatchSign);
        // any other participant must be in the ledger.
        auto const required = requiredBatchSigners(proposedTx);
        std::size_t const participantsBegin = result.authorizations.size();
        for (AccountID const& participant : required)
        {
            result.authorizations.push_back(evaluateAuthorization(
                view,
                participant,
                slotFor(participant),
                createdByEarlierInner(view, proposedTx, participant),
                j));
        }

        // Each inner transaction goes through its own preclaim at submission,
        // which applies the same delegation and sponsorship rules as the
        // outer one. A `complete` verdict that skipped them would hand back a
        // transaction whose inner can never apply, so they are applied here
        // in the same order submission uses: sponsorship, then permission.
        //
        // Not modelled: what an earlier inner transaction of the same Batch
        // does to the state a later inner's rules read, other than creating
        // an account. A Sponsorship entry it creates, re-flags or removes, a
        // grant it revokes, a trust line it opens: each is judged here as the
        // queried ledger has it, which can differ from the view the later
        // inner meets at submission.
        STArray const& inners = proposedTx.getFieldArray(sfRawTransactions);
        for (std::size_t index = 0; index < inners.size(); ++index)
        {
            STObject const& inner = inners[index];
            AccountID const innerInitiator = inner.isFieldPresent(sfDelegate)
                ? inner.getAccountID(sfDelegate)
                : inner.getAccountID(sfAccount);

            if (inner.isFieldPresent(sfSponsor))
            {
                if (inner.isFieldPresent(sfSponsorSignature))
                {
                    // A co-signing Sponsor authorizes through its participant
                    // entry above; the sponsorship rule can still reject the
                    // inner outright, whatever that entry holds.
                    auto const rule = sponsorRule(view, inner);
                    AccountID const sponsor = inner.getAccountID(sfSponsor);
                    if (rule == tefINTERNAL)
                    {
                        overrideEntry(
                            result, participantsBegin, sponsor, UnsignedReason::Malformed);
                    }
                    else if (rule == temINVALID)
                    {
                        overrideEntry(
                            result, participantsBegin, sponsor, UnsignedReason::InvalidSponsorship);
                    }
                    else if (
                        rule == terNO_ACCOUNT && !createdByEarlierInner(view, proposedTx, sponsor))
                    {
                        // The sponsor is judged on the view its inner meets,
                        // where an earlier inner may have created it.
                        overrideEntry(
                            result, participantsBegin, sponsor, UnsignedReason::AccountNotFound);
                    }
                }
                else
                {
                    // A Sponsor that does not co-sign has no BatchSigners entry
                    // to collect: only a Sponsorship entry between it and the
                    // inner's initiator authorizes it. Not a slot XLS-0103
                    // §8.1.3.1 lists, so it gets an entry of its own.
                    result.authorizations.push_back(evaluateSponsor(
                        view, inner, innerInitiator, /*canCollectSignature=*/false, j));
                }
            }

            if (inner.isFieldPresent(sfDelegate))
            {
                if (auto const permission = delegatePermission(view, inner);
                    !isTesSuccess(permission))
                {
                    overrideEntry(
                        result,
                        participantsBegin,
                        innerInitiator,
                        permission == tefINTERNAL ? UnsignedReason::Malformed
                                                  : UnsignedReason::NoDelegatePermission);
                }
            }

            // The turn gates are the first thing the inner's preclaim applies,
            // so their verdict takes precedence over the two above: applied
            // last here so it is what the authorizer's entry reports, and
            // overrideEntry keeps it against anything a later inner adds.
            if (auto const failure = innerTurnFailure(view, proposedTx, index))
                overrideEntry(result, participantsBegin, innerInitiator, *failure);
        }

        // Submission requires BatchSigners to be exactly the required set,
        // ascending and unique (Batch::preflightSigValidated): an entry the
        // Batch does not require (the outer account included), a duplicate,
        // or one out of order fails the whole transaction with temBAD_SIGNER.
        // TransactionProposalSign never stores one (XLS-0103 §6.3.2), so each
        // is reported as an entry nothing can satisfy rather than ignored.
        if (batchSigners != nullptr)
        {
            std::optional<AccountID> previous;
            for (STObject const& signer : *batchSigners)
            {
                std::optional<AccountID> const id = signer.isFieldPresent(sfAccount)
                    ? std::optional{signer.getAccountID(sfAccount)}
                    : std::nullopt;
                bool const rejected = !id || !std::ranges::binary_search(required, *id) ||
                    (previous && *id <= *previous);
                if (rejected)
                {
                    result.authorizations.push_back(unsatisfied(id, UnsignedReason::Malformed));
                }
                if (id)
                    previous = id;
            }
        }
    }

    result.authorized = std::ranges::all_of(
        result.authorizations, [](AuthorizationStatus const& status) { return status.satisfied; });

    if (isExpired(view, sleProposal, proposedTx))
    {
        result.state = ProposalState::Expired;
    }
    else if (result.authorized)
    {
        result.state = ProposalState::Complete;
    }
    else
    {
        result.state = ProposalState::Pending;
    }

    return result;
}

}  // namespace xrpl::proposal
