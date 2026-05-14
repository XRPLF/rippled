/** @file
 *  Implements `SignerListSet`, the transactor that creates, replaces, or
 *  destroys an account's `ltSIGNER_LIST` ledger entry.
 *
 *  All three operations share a single transaction format; the operation is
 *  decoded from the `sfSignerQuorum` / `sfSignerEntries` combination in
 *  `determineOperation()`, which is called both at preflight and again in
 *  `preCompute()` to cache the parsed results for `doApply()`.
 */
#include <xrpl/tx/transactors/account/SignerListSet.h>

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/SignerEntries.h>
#include <xrpl/tx/Transactor.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <tuple>
#include <utility>
#include <vector>

namespace xrpl {

/** Fixed `sfSignerListID` value written to every `ltSIGNER_LIST` entry.
 *
 *  The data model was designed to support multiple signer lists per account,
 *  but that capability has never been activated. Until it is, all lists are
 *  assigned ID zero.
 */
static std::uint32_t const kDEFAULT_SIGNER_LIST_ID = 0;

std::tuple<NotTEC, std::uint32_t, std::vector<SignerEntries::SignerEntry>, SignerListSet::Operation>
SignerListSet::determineOperation(STTx const& tx, ApplyFlags flags, beast::Journal j)
{
    auto const quorum = tx[sfSignerQuorum];
    std::vector<SignerEntries::SignerEntry> sign;
    Operation op = Operation::Unknown;

    bool const hasSignerEntries(tx.isFieldPresent(sfSignerEntries));
    if ((quorum != 0u) && hasSignerEntries)
    {
        auto signers = SignerEntries::deserialize(tx, j, "transaction");

        if (!signers)
            return std::make_tuple(signers.error(), quorum, sign, op);

        std::sort(signers->begin(), signers->end());

        sign = std::move(*signers);
        op = Operation::Set;
    }
    else if ((quorum == 0) && !hasSignerEntries)
    {
        op = Operation::Destroy;
    }

    return std::make_tuple(tesSUCCESS, quorum, sign, op);
}

std::uint32_t
SignerListSet::getFlagsMask(PreflightContext const& ctx)
{
    return ctx.rules.enabled(fixInvalidTxFlags) ? tfUniversalMask : 0;
}

NotTEC
SignerListSet::preflight(PreflightContext const& ctx)
{
    auto const result = determineOperation(ctx.tx, ctx.flags, ctx.j);

    if (!isTesSuccess(std::get<0>(result)))
        return std::get<0>(result);

    if (std::get<3>(result) == Operation::Unknown)
    {
        JLOG(ctx.j.trace()) << "Malformed transaction: Invalid signer set list format.";
        return temMALFORMED;
    }

    if (std::get<3>(result) == Operation::Set)
    {
        auto const account = ctx.tx.getAccountID(sfAccount);
        NotTEC const ter = validateQuorumAndSignerEntries(
            std::get<1>(result), std::get<2>(result), account, ctx.j, ctx.rules);
        if (!isTesSuccess(ter))
        {
            return ter;
        }
    }

    return tesSUCCESS;
}

TER
SignerListSet::doApply()
{
    switch (do_)
    {
        case Operation::Set:
            return replaceSignerList();

        case Operation::Destroy:
            return destroySignerList();

        default:
            break;
    }
    // LCOV_EXCL_START
    UNREACHABLE("xrpl::SignerListSet::doApply : invalid operation");
    return temMALFORMED;
    // LCOV_EXCL_STOP
}

void
SignerListSet::preCompute()
{
    auto result = determineOperation(ctx_.tx, view().flags(), j_);
    XRPL_ASSERT(
        isTesSuccess(std::get<0>(result)),
        "xrpl::SignerListSet::preCompute : result is tesSUCCESS");
    XRPL_ASSERT(
        std::get<3>(result) != Operation::Unknown,
        "xrpl::SignerListSet::preCompute : result is known operation");

    quorum_ = std::get<1>(result);
    signers_ = std::get<2>(result);
    do_ = std::get<3>(result);

    Transactor::preCompute();
}

/** Computes the pre-`MultiSignReserve` owner-count delta for a signer list.
 *
 *  Under the old model a signer list costs `2 + N` owner-count units, where
 *  N is the number of entries (minimum 3 for a single-entry list). The signed
 *  return type is intentional: callers negate the result when removing a list
 *  so it can be passed directly to `adjustOwnerCount()`.
 *
 *  @param entryCount Number of signers in the list; must be in
 *      `[kMIN_MULTI_SIGNERS, kMAX_MULTI_SIGNERS]` (currently 1–32).
 *  @return The positive owner-count cost (2 + entryCount) for the list.
 */
static int
signerCountBasedOwnerCountDelta(std::size_t entryCount, Rules const& rules)
{
    XRPL_ASSERT(
        entryCount >= STTx::kMIN_MULTI_SIGNERS,
        "xrpl::signerCountBasedOwnerCountDelta : minimum signers");
    XRPL_ASSERT(
        entryCount <= STTx::kMAX_MULTI_SIGNERS,
        "xrpl::signerCountBasedOwnerCountDelta : maximum signers");
    return 2 + static_cast<int>(entryCount);
}

/** Removes a signer list from the ledger and adjusts the owner count.
 *
 *  Returns `tesSUCCESS` immediately if no signer list exists under
 *  `signerListKeylet` (idempotent — safe to call during a create-replace).
 *
 *  Owner-count accounting depends on which amendment model created the list:
 *  if `lsfOneOwnerCount` is set the decrement is 1; otherwise the
 *  pre-`MultiSignReserve` formula `-(2 + N)` is used, where N is the number
 *  of signer entries stored in the existing SLE.
 *
 *  @param registry         Service registry used to obtain the view journal.
 *  @param view             Mutable ledger view.
 *  @param accountKeylet    Keylet of the owning account's `AccountRoot`.
 *  @param ownerDirKeylet   Keylet of the account's owner directory.
 *  @param signerListKeylet Keylet of the `ltSIGNER_LIST` entry to remove.
 *  @param j                Journal for diagnostic logging.
 *  @return `tesSUCCESS` on success; `tefBAD_LEDGER` if the directory entry
 *      cannot be removed (indicates ledger corruption).
 */
static TER
removeSignersFromLedger(
    ServiceRegistry& registry,
    ApplyView& view,
    Keylet const& accountKeylet,
    Keylet const& ownerDirKeylet,
    Keylet const& signerListKeylet,
    beast::Journal j)
{
    SLE::pointer const signers = view.peek(signerListKeylet);

    if (!signers)
        return tesSUCCESS;

    // lsfOneOwnerCount marks post-MultiSignReserve lists (cost = 1 unit).
    // Older lists use the 2+N formula derived from the stored entry count.
    int removeFromOwnerCount = -1;
    if ((signers->getFlags() & lsfOneOwnerCount) == 0)
    {
        STArray const& actualList = signers->getFieldArray(sfSignerEntries);
        removeFromOwnerCount =
            signerCountBasedOwnerCountDelta(actualList.size(), view.rules()) * -1;
    }

    auto const hint = (*signers)[sfOwnerNode];

    if (!view.dirRemove(ownerDirKeylet, hint, signerListKeylet.key, false))
    {
        // LCOV_EXCL_START
        JLOG(j.fatal()) << "Unable to delete SignerList from owner.";
        return tefBAD_LEDGER;
        // LCOV_EXCL_STOP
    }

    adjustOwnerCount(
        view, view.peek(accountKeylet), removeFromOwnerCount, registry.getJournal("View"));

    view.erase(signers);

    return tesSUCCESS;
}

TER
SignerListSet::removeFromLedger(
    ServiceRegistry& registry,
    ApplyView& view,
    AccountID const& account,
    beast::Journal j)
{
    auto const accountKeylet = keylet::account(account);
    auto const ownerDirKeylet = keylet::ownerDir(account);
    auto const signerListKeylet = keylet::signers(account);

    return removeSignersFromLedger(
        registry, view, accountKeylet, ownerDirKeylet, signerListKeylet, j);
}

NotTEC
SignerListSet::validateQuorumAndSignerEntries(
    std::uint32_t quorum,
    std::vector<SignerEntries::SignerEntry> const& signers,
    AccountID const& account,
    beast::Journal j,
    Rules const& rules)
{
    {
        std::size_t const signerCount = signers.size();
        if (signerCount < STTx::kMIN_MULTI_SIGNERS || signerCount > STTx::kMAX_MULTI_SIGNERS)
        {
            JLOG(j.trace()) << "Too many or too few signers in signer list.";
            return temMALFORMED;
        }
    }

    XRPL_ASSERT(
        std::ranges::is_sorted(signers),
        "xrpl::SignerListSet::validateQuorumAndSignerEntries : sorted "
        "signers");
    if (std::ranges::adjacent_find(signers) != signers.end())
    {
        JLOG(j.trace()) << "Duplicate signers in signer list";
        return temBAD_SIGNER;
    }

    std::uint64_t allSignersWeight(0);
    for (auto const& signer : signers)
    {
        std::uint32_t const weight = signer.weight;
        if (weight <= 0)
        {
            JLOG(j.trace()) << "Every signer must have a positive weight.";
            return temBAD_WEIGHT;
        }

        allSignersWeight += signer.weight;

        if (signer.account == account)
        {
            JLOG(j.trace()) << "A signer may not self reference account.";
            return temBAD_SIGNER;
        }
        // Non-existent accounts are permitted as phantom signers.
    }
    if ((quorum <= 0) || (allSignersWeight < quorum))
    {
        JLOG(j.trace()) << "Quorum is unreachable";
        return temBAD_QUORUM;
    }
    return tesSUCCESS;
}

TER
SignerListSet::replaceSignerList()
{
    auto const accountKeylet = keylet::account(account_);
    auto const ownerDirKeylet = keylet::ownerDir(account_);
    auto const signerListKeylet = keylet::signers(account_);

    // Remove any pre-existing list before checking reserve: deletion may
    // lower the owner count and ease the subsequent reserve requirement
    // (consistent with TicketCreate).
    if (TER const ter = removeSignersFromLedger(
            ctx_.registry, view(), accountKeylet, ownerDirKeylet, signerListKeylet, j_))
        return ter;

    auto const sle = view().peek(accountKeylet);
    if (!sle)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    std::uint32_t const oldOwnerCount{(*sle)[sfOwnerCount]};

    constexpr int kADDED_OWNER_COUNT = 1;
    std::uint32_t const flags{lsfOneOwnerCount};

    XRPAmount const newReserve{view().fees().accountReserve(oldOwnerCount + kADDED_OWNER_COUNT)};

    // Check reserve against pre-fee balance to allow dipping into reserve
    // to pay the transaction fee.
    if (preFeeBalance_ < newReserve)
        return tecINSUFFICIENT_RESERVE;

    auto signerList = std::make_shared<SLE>(signerListKeylet);
    view().insert(signerList);
    writeSignersToSLE(signerList, flags);

    auto viewJ = ctx_.registry.get().getJournal("View");
    auto const page =
        ctx_.view().dirInsert(ownerDirKeylet, signerListKeylet, describeOwnerDir(account_));

    JLOG(j_.trace()) << "Create signer list for account " << toBase58(account_) << ": "
                     << (page ? "success" : "failure");

    if (!page)
        return tecDIR_FULL;  // LCOV_EXCL_LINE

    signerList->setFieldU64(sfOwnerNode, *page);

    adjustOwnerCount(view(), sle, kADDED_OWNER_COUNT, viewJ);
    return tesSUCCESS;
}

TER
SignerListSet::destroySignerList()
{
    auto const accountKeylet = keylet::account(account_);
    SLE::pointer const ledgerEntry = view().peek(accountKeylet);
    if (!ledgerEntry)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    if ((ledgerEntry->isFlag(lsfDisableMaster)) && (!ledgerEntry->isFieldPresent(sfRegularKey)))
        return tecNO_ALTERNATIVE_KEY;

    auto const ownerDirKeylet = keylet::ownerDir(account_);
    auto const signerListKeylet = keylet::signers(account_);
    return removeSignersFromLedger(
        ctx_.registry, view(), accountKeylet, ownerDirKeylet, signerListKeylet, j_);
}

void
SignerListSet::writeSignersToSLE(SLE::pointer const& ledgerEntry, std::uint32_t flags) const
{
    if (ctx_.view().rules().enabled(fixIncludeKeyletFields))
    {
        ledgerEntry->setAccountID(sfOwner, account_);
    }
    ledgerEntry->setFieldU32(sfSignerQuorum, quorum_);
    ledgerEntry->setFieldU32(sfSignerListID, kDEFAULT_SIGNER_LIST_ID);
    if (flags != 0u)
        ledgerEntry->setFieldU32(sfFlags, flags);

    STArray toLedger(signers_.size());
    for (auto const& entry : signers_)
    {
        toLedger.pushBack(STObject::makeInnerObject(sfSignerEntry));
        STObject& obj = toLedger.back();
        obj.reserve(2);
        obj[sfAccount] = entry.account;
        obj[sfSignerWeight] = entry.weight;

        // Defensive: only write sfWalletLocator when a tag is actually present
        // so no spurious empty-tag field is ever committed to the ledger.
        if (entry.tag)
            obj.setFieldH256(sfWalletLocator, *(entry.tag));
    }

    ledgerEntry->setFieldArray(sfSignerEntries, toLedger);
}

void
SignerListSet::visitInvariantEntry(
    bool,
    std::shared_ptr<SLE const> const&,
    std::shared_ptr<SLE const> const&)
{
    // No transaction-specific invariants yet (future work).
}

bool
SignerListSet::finalizeInvariants(
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
