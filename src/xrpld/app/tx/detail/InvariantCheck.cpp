#include <xrpld/app/misc/AMMHelpers.h>
#include <xrpld/app/misc/AMMUtils.h>
#include <xrpld/app/tx/detail/InvariantCheck.h>
#include <xrpld/app/tx/detail/NFTokenUtils.h>
#include <xrpld/app/tx/detail/PermissionedDomainSet.h>

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/CredentialHelpers.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/View.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/SystemParameters.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/Units.h>
#include <xrpl/protocol/nftPageMask.h>

#include <cstdint>
#include <optional>

namespace ripple {

/*
assert(enforce)

There are several asserts (or XRPL_ASSERTs) in this file that check a variable
named `enforce` when an invariant fails. At first glance, those asserts may look
incorrect, but they are not.

Those asserts take advantage of two facts:
1. `asserts` are not (normally) executed in release builds.
2. Invariants should *never* fail, except in tests that specifically modify
   the open ledger to break them.

This makes `assert(enforce)` sort of a second-layer of invariant enforcement
aimed at _developers_. It's designed to fire if a developer writes code that
violates an invariant, and runs it in unit tests or a develop build that _does
not have the relevant amendments enabled_. It's intentionally a pain in the neck
so that bad code gets caught and fixed as early as possible.
*/

enum Privilege {
    noPriv =
        0x0000,  // The transaction can not do any of the enumerated operations
    createAcct =
        0x0001,  // The transaction can create a new ACCOUNT_ROOT object.
    createPseudoAcct = 0x0002,  // The transaction can create a pseudo account,
                                // which implies createAcct
    mustDeleteAcct =
        0x0004,  // The transaction must delete an ACCOUNT_ROOT object
    mayDeleteAcct = 0x0008,    // The transaction may delete an ACCOUNT_ROOT
                               // object, but does not have to
    overrideFreeze = 0x0010,   // The transaction can override some freeze rules
    changeNFTCounts = 0x0020,  // The transaction can mint or burn an NFT
    createMPTIssuance =
        0x0040,  // The transaction can create a new MPT issuance
    destroyMPTIssuance = 0x0080,  // The transaction can destroy an MPT issuance
    mustAuthorizeMPT = 0x0100,  // The transaction MUST create or delete an MPT
                                // object (except by issuer)
    mayAuthorizeMPT = 0x0200,   // The transaction MAY create or delete an MPT
                                // object (except by issuer)
    mayDeleteMPT =
        0x0400,  // The transaction MAY delete an MPT object. May not create.
    mustModifyVault =
        0x0800,  // The transaction must modify, delete or create, a vault
};
constexpr Privilege
operator|(Privilege lhs, Privilege rhs)
{
    return safe_cast<Privilege>(
        safe_cast<std::underlying_type_t<Privilege>>(lhs) |
        safe_cast<std::underlying_type_t<Privilege>>(rhs));
}

#pragma push_macro("TRANSACTION")
#undef TRANSACTION

#define TRANSACTION(tag, value, name, delegatable, amendment, privileges, ...) \
    case tag: {                                                                \
        return (privileges) & priv;                                            \
    }

bool
hasPrivilege(STTx const& tx, Privilege priv)
{
    switch (tx.getTxnType())
    {
#include <xrpl/protocol/detail/transactions.macro>
        // Deprecated types
        default:
            return false;
    }
};

#undef TRANSACTION
#pragma pop_macro("TRANSACTION")

void
TransactionFeeCheck::visitEntry(
    bool,
    std::shared_ptr<SLE const> const&,
    std::shared_ptr<SLE const> const&)
{
    // nothing to do
}

bool
TransactionFeeCheck::finalize(
    STTx const& tx,
    TER const,
    XRPAmount const fee,
    ReadView const&,
    beast::Journal const& j)
{
    // We should never charge a negative fee
    if (fee.drops() < 0)
    {
        JLOG(j.fatal()) << "Invariant failed: fee paid was negative: "
                        << fee.drops();
        return false;
    }

    // We should never charge a fee that's greater than or equal to the
    // entire XRP supply.
    if (fee >= INITIAL_XRP)
    {
        JLOG(j.fatal()) << "Invariant failed: fee paid exceeds system limit: "
                        << fee.drops();
        return false;
    }

    // We should never charge more for a transaction than the transaction
    // authorizes. It's possible to charge less in some circumstances.
    if (fee > tx.getFieldAmount(sfFee).xrp())
    {
        JLOG(j.fatal()) << "Invariant failed: fee paid is " << fee.drops()
                        << " exceeds fee specified in transaction.";
        return false;
    }

    return true;
}

//------------------------------------------------------------------------------

void
XRPNotCreated::visitEntry(
    bool isDelete,
    std::shared_ptr<SLE const> const& before,
    std::shared_ptr<SLE const> const& after)
{
    /* We go through all modified ledger entries, looking only at account roots,
     * escrow payments, and payment channels. We remove from the total any
     * previous XRP values and add to the total any new XRP values. The net
     * balance of a payment channel is computed from two fields (amount and
     * balance) and deletions are ignored for paychan and escrow because the
     * amount fields have not been adjusted for those in the case of deletion.
     */
    if (before)
    {
        switch (before->getType())
        {
            case ltACCOUNT_ROOT:
                drops_ -= (*before)[sfBalance].xrp().drops();
                break;
            case ltPAYCHAN:
                drops_ -=
                    ((*before)[sfAmount] - (*before)[sfBalance]).xrp().drops();
                break;
            case ltESCROW:
                if (isXRP((*before)[sfAmount]))
                    drops_ -= (*before)[sfAmount].xrp().drops();
                break;
            default:
                break;
        }
    }

    if (after)
    {
        switch (after->getType())
        {
            case ltACCOUNT_ROOT:
                drops_ += (*after)[sfBalance].xrp().drops();
                break;
            case ltPAYCHAN:
                if (!isDelete)
                    drops_ += ((*after)[sfAmount] - (*after)[sfBalance])
                                  .xrp()
                                  .drops();
                break;
            case ltESCROW:
                if (!isDelete && isXRP((*after)[sfAmount]))
                    drops_ += (*after)[sfAmount].xrp().drops();
                break;
            default:
                break;
        }
    }
}

bool
XRPNotCreated::finalize(
    STTx const& tx,
    TER const,
    XRPAmount const fee,
    ReadView const&,
    beast::Journal const& j)
{
    // The net change should never be positive, as this would mean that the
    // transaction created XRP out of thin air. That's not possible.
    if (drops_ > 0)
    {
        JLOG(j.fatal()) << "Invariant failed: XRP net change was positive: "
                        << drops_;
        return false;
    }

    // The negative of the net change should be equal to actual fee charged.
    if (-drops_ != fee.drops())
    {
        JLOG(j.fatal()) << "Invariant failed: XRP net change of " << drops_
                        << " doesn't match fee " << fee.drops();
        return false;
    }

    return true;
}

//------------------------------------------------------------------------------

void
XRPBalanceChecks::visitEntry(
    bool,
    std::shared_ptr<SLE const> const& before,
    std::shared_ptr<SLE const> const& after)
{
    auto isBad = [](STAmount const& balance) {
        if (!balance.native())
            return true;

        auto const drops = balance.xrp();

        // Can't have more than the number of drops instantiated
        // in the genesis ledger.
        if (drops > INITIAL_XRP)
            return true;

        // Can't have a negative balance (0 is OK)
        if (drops < XRPAmount{0})
            return true;

        return false;
    };

    if (before && before->getType() == ltACCOUNT_ROOT)
        bad_ |= isBad((*before)[sfBalance]);

    if (after && after->getType() == ltACCOUNT_ROOT)
        bad_ |= isBad((*after)[sfBalance]);
}

bool
XRPBalanceChecks::finalize(
    STTx const&,
    TER const,
    XRPAmount const,
    ReadView const&,
    beast::Journal const& j)
{
    if (bad_)
    {
        JLOG(j.fatal()) << "Invariant failed: incorrect account XRP balance";
        return false;
    }

    return true;
}

//------------------------------------------------------------------------------

void
NoBadOffers::visitEntry(
    bool isDelete,
    std::shared_ptr<SLE const> const& before,
    std::shared_ptr<SLE const> const& after)
{
    auto isBad = [](STAmount const& pays, STAmount const& gets) {
        // An offer should never be negative
        if (pays < beast::zero)
            return true;

        if (gets < beast::zero)
            return true;

        // Can't have an XRP to XRP offer:
        return pays.native() && gets.native();
    };

    if (before && before->getType() == ltOFFER)
        bad_ |= isBad((*before)[sfTakerPays], (*before)[sfTakerGets]);

    if (after && after->getType() == ltOFFER)
        bad_ |= isBad((*after)[sfTakerPays], (*after)[sfTakerGets]);
}

bool
NoBadOffers::finalize(
    STTx const&,
    TER const,
    XRPAmount const,
    ReadView const&,
    beast::Journal const& j)
{
    if (bad_)
    {
        JLOG(j.fatal()) << "Invariant failed: offer with a bad amount";
        return false;
    }

    return true;
}

//------------------------------------------------------------------------------

void
NoZeroEscrow::visitEntry(
    bool isDelete,
    std::shared_ptr<SLE const> const& before,
    std::shared_ptr<SLE const> const& after)
{
    auto isBad = [](STAmount const& amount) {
        // XRP case
        if (amount.native())
        {
            if (amount.xrp() <= XRPAmount{0})
                return true;

            if (amount.xrp() >= INITIAL_XRP)
                return true;
        }
        else
        {
            // IOU case
            if (amount.holds<Issue>())
            {
                if (amount <= beast::zero)
                    return true;

                if (badCurrency() == amount.getCurrency())
                    return true;
            }

            // MPT case
            if (amount.holds<MPTIssue>())
            {
                if (amount <= beast::zero)
                    return true;

                if (amount.mpt() > MPTAmount{maxMPTokenAmount})
                    return true;  // LCOV_EXCL_LINE
            }
        }
        return false;
    };

    if (before && before->getType() == ltESCROW)
        bad_ |= isBad((*before)[sfAmount]);

    if (after && after->getType() == ltESCROW)
        bad_ |= isBad((*after)[sfAmount]);

    auto checkAmount = [this](std::int64_t amount) {
        if (amount > maxMPTokenAmount || amount < 0)
            bad_ = true;
    };

    if (after && after->getType() == ltMPTOKEN_ISSUANCE)
    {
        auto const outstanding = (*after)[sfOutstandingAmount];
        checkAmount(outstanding);
        if (auto const locked = (*after)[~sfLockedAmount])
        {
            checkAmount(*locked);
            bad_ = outstanding < *locked;
        }
    }

    if (after && after->getType() == ltMPTOKEN)
    {
        auto const mptAmount = (*after)[sfMPTAmount];
        checkAmount(mptAmount);
        if (auto const locked = (*after)[~sfLockedAmount])
        {
            checkAmount(*locked);
        }
    }
}

bool
NoZeroEscrow::finalize(
    STTx const& txn,
    TER const,
    XRPAmount const,
    ReadView const& rv,
    beast::Journal const& j)
{
    if (bad_)
    {
        JLOG(j.fatal()) << "Invariant failed: escrow specifies invalid amount";
        return false;
    }

    return true;
}

//------------------------------------------------------------------------------

void
AccountRootsNotDeleted::visitEntry(
    bool isDelete,
    std::shared_ptr<SLE const> const& before,
    std::shared_ptr<SLE const> const&)
{
    if (isDelete && before && before->getType() == ltACCOUNT_ROOT)
        accountsDeleted_++;
}

bool
AccountRootsNotDeleted::finalize(
    STTx const& tx,
    TER const result,
    XRPAmount const,
    ReadView const&,
    beast::Journal const& j)
{
    // AMM account root can be deleted as the result of AMM withdraw/delete
    // transaction when the total AMM LP Tokens balance goes to 0.
    // A successful AccountDelete or AMMDelete MUST delete exactly
    // one account root.
    if (hasPrivilege(tx, mustDeleteAcct) && result == tesSUCCESS)
    {
        if (accountsDeleted_ == 1)
            return true;

        if (accountsDeleted_ == 0)
            JLOG(j.fatal()) << "Invariant failed: account deletion "
                               "succeeded without deleting an account";
        else
            JLOG(j.fatal()) << "Invariant failed: account deletion "
                               "succeeded but deleted multiple accounts!";
        return false;
    }

    // A successful AMMWithdraw/AMMClawback MAY delete one account root
    // when the total AMM LP Tokens balance goes to 0. Not every AMM withdraw
    // deletes the AMM account, accountsDeleted_ is set if it is deleted.
    if (hasPrivilege(tx, mayDeleteAcct) && result == tesSUCCESS &&
        accountsDeleted_ == 1)
        return true;

    if (accountsDeleted_ == 0)
        return true;

    JLOG(j.fatal()) << "Invariant failed: an account root was deleted";
    return false;
}

//------------------------------------------------------------------------------

void
AccountRootsDeletedClean::visitEntry(
    bool isDelete,
    std::shared_ptr<SLE const> const& before,
    std::shared_ptr<SLE const> const&)
{
    if (isDelete && before && before->getType() == ltACCOUNT_ROOT)
        accountsDeleted_.emplace_back(before);
}

bool
AccountRootsDeletedClean::finalize(
    STTx const& tx,
    TER const result,
    XRPAmount const,
    ReadView const& view,
    beast::Journal const& j)
{
    // Always check for objects in the ledger, but to prevent differing
    // transaction processing results, however unlikely, only fail if the
    // feature is enabled. Enabled, or not, though, a fatal-level message will
    // be logged
    [[maybe_unused]] bool const enforce =
        view.rules().enabled(featureInvariantsV1_1) ||
        view.rules().enabled(featureSingleAssetVault);

    auto const objectExists = [&view, enforce, &j](auto const& keylet) {
        (void)enforce;
        if (auto const sle = view.read(keylet))
        {
            // Finding the object is bad
            auto const typeName = [&sle]() {
                auto item =
                    LedgerFormats::getInstance().findByType(sle->getType());

                if (item != nullptr)
                    return item->getName();
                return std::to_string(sle->getType());
            }();

            JLOG(j.fatal())
                << "Invariant failed: account deletion left behind a "
                << typeName << " object";
            // The comment above starting with "assert(enforce)" explains this
            // assert.
            XRPL_ASSERT(
                enforce,
                "ripple::AccountRootsDeletedClean::finalize::objectExists : "
                "account deletion left no objects behind");
            return true;
        }
        return false;
    };

    for (auto const& accountSLE : accountsDeleted_)
    {
        auto const accountID = accountSLE->getAccountID(sfAccount);
        // Simple types
        for (auto const& [keyletfunc, _, __] : directAccountKeylets)
        {
            if (objectExists(std::invoke(keyletfunc, accountID)) && enforce)
                return false;
        }

        {
            // NFT pages. ntfpage_min and nftpage_max were already explicitly
            // checked above as entries in directAccountKeylets. This uses
            // view.succ() to check for any NFT pages in between the two
            // endpoints.
            Keylet const first = keylet::nftpage_min(accountID);
            Keylet const last = keylet::nftpage_max(accountID);

            std::optional<uint256> key = view.succ(first.key, last.key.next());

            // current page
            if (key && objectExists(Keylet{ltNFTOKEN_PAGE, *key}) && enforce)
                return false;
        }

        // If the account is a pseudo account, then the linked object must
        // also be deleted. e.g. AMM, Vault, etc.
        for (auto const& field : getPseudoAccountFields())
        {
            if (accountSLE->isFieldPresent(*field))
            {
                auto const key = accountSLE->getFieldH256(*field);
                if (objectExists(keylet::unchecked(key)) && enforce)
                    return false;
            }
        }
    }

    return true;
}

//------------------------------------------------------------------------------

void
LedgerEntryTypesMatch::visitEntry(
    bool,
    std::shared_ptr<SLE const> const& before,
    std::shared_ptr<SLE const> const& after)
{
    if (before && after && before->getType() != after->getType())
        typeMismatch_ = true;

    if (after)
    {
#pragma push_macro("LEDGER_ENTRY")
#undef LEDGER_ENTRY

#define LEDGER_ENTRY(tag, ...) case tag:

        switch (after->getType())
        {
#include <xrpl/protocol/detail/ledger_entries.macro>

            break;
            default:
                invalidTypeAdded_ = true;
                break;
        }

#undef LEDGER_ENTRY
#pragma pop_macro("LEDGER_ENTRY")
    }
}

bool
LedgerEntryTypesMatch::finalize(
    STTx const&,
    TER const,
    XRPAmount const,
    ReadView const&,
    beast::Journal const& j)
{
    if ((!typeMismatch_) && (!invalidTypeAdded_))
        return true;

    if (typeMismatch_)
    {
        JLOG(j.fatal()) << "Invariant failed: ledger entry type mismatch";
    }

    if (invalidTypeAdded_)
    {
        JLOG(j.fatal()) << "Invariant failed: invalid ledger entry type added";
    }

    return false;
}

//------------------------------------------------------------------------------

void
NoXRPTrustLines::visitEntry(
    bool,
    std::shared_ptr<SLE const> const&,
    std::shared_ptr<SLE const> const& after)
{
    if (after && after->getType() == ltRIPPLE_STATE)
    {
        // checking the issue directly here instead of
        // relying on .native() just in case native somehow
        // were systematically incorrect
        xrpTrustLine_ =
            after->getFieldAmount(sfLowLimit).issue() == xrpIssue() ||
            after->getFieldAmount(sfHighLimit).issue() == xrpIssue();
    }
}

bool
NoXRPTrustLines::finalize(
    STTx const&,
    TER const,
    XRPAmount const,
    ReadView const&,
    beast::Journal const& j)
{
    if (!xrpTrustLine_)
        return true;

    JLOG(j.fatal()) << "Invariant failed: an XRP trust line was created";
    return false;
}

//------------------------------------------------------------------------------

void
NoDeepFreezeTrustLinesWithoutFreeze::visitEntry(
    bool,
    std::shared_ptr<SLE const> const&,
    std::shared_ptr<SLE const> const& after)
{
    if (after && after->getType() == ltRIPPLE_STATE)
    {
        std::uint32_t const uFlags = after->getFieldU32(sfFlags);
        bool const lowFreeze = uFlags & lsfLowFreeze;
        bool const lowDeepFreeze = uFlags & lsfLowDeepFreeze;

        bool const highFreeze = uFlags & lsfHighFreeze;
        bool const highDeepFreeze = uFlags & lsfHighDeepFreeze;

        deepFreezeWithoutFreeze_ =
            (lowDeepFreeze && !lowFreeze) || (highDeepFreeze && !highFreeze);
    }
}

bool
NoDeepFreezeTrustLinesWithoutFreeze::finalize(
    STTx const&,
    TER const,
    XRPAmount const,
    ReadView const&,
    beast::Journal const& j)
{
    if (!deepFreezeWithoutFreeze_)
        return true;

    JLOG(j.fatal()) << "Invariant failed: a trust line with deep freeze flag "
                       "without normal freeze was created";
    return false;
}

//------------------------------------------------------------------------------

void
TransfersNotFrozen::visitEntry(
    bool isDelete,
    std::shared_ptr<SLE const> const& before,
    std::shared_ptr<SLE const> const& after)
{
    /*
     * A trust line freeze state alone doesn't determine if a transfer is
     * frozen. The transfer must be examined "end-to-end" because both sides of
     * the transfer may have different freeze states and freeze impact depends
     * on the transfer direction. This is why first we need to track the
     * transfers using IssuerChanges senders/receivers.
     *
     * Only in validateIssuerChanges, after we collected all changes can we
     * determine if the transfer is valid.
     */
    if (!isValidEntry(before, after))
    {
        return;
    }

    auto const balanceChange = calculateBalanceChange(before, after, isDelete);
    if (balanceChange.signum() == 0)
    {
        return;
    }

    recordBalanceChanges(after, balanceChange);
}

bool
TransfersNotFrozen::finalize(
    STTx const& tx,
    TER const ter,
    XRPAmount const fee,
    ReadView const& view,
    beast::Journal const& j)
{
    /*
     * We check this invariant regardless of deep freeze amendment status,
     * allowing for detection and logging of potential issues even when the
     * amendment is disabled.
     *
     * If an exploit that allows moving frozen assets is discovered,
     * we can alert operators who monitor fatal messages and trigger assert in
     * debug builds for an early warning.
     *
     * In an unlikely event that an exploit is found, this early detection
     * enables encouraging the UNL to expedite deep freeze amendment activation
     * or deploy hotfixes via new amendments. In case of a new amendment, we'd
     * only have to change this line setting 'enforce' variable.
     * enforce = view.rules().enabled(featureDeepFreeze) ||
     *           view.rules().enabled(fixFreezeExploit);
     */
    [[maybe_unused]] bool const enforce =
        view.rules().enabled(featureDeepFreeze);

    for (auto const& [issue, changes] : balanceChanges_)
    {
        auto const issuerSle = findIssuer(issue.account, view);
        // It should be impossible for the issuer to not be found, but check
        // just in case so rippled doesn't crash in release.
        if (!issuerSle)
        {
            // The comment above starting with "assert(enforce)" explains this
            // assert.
            XRPL_ASSERT(
                enforce,
                "ripple::TransfersNotFrozen::finalize : enforce "
                "invariant.");
            if (enforce)
            {
                return false;
            }
            continue;
        }

        if (!validateIssuerChanges(issuerSle, changes, tx, j, enforce))
        {
            return false;
        }
    }

    return true;
}

bool
TransfersNotFrozen::isValidEntry(
    std::shared_ptr<SLE const> const& before,
    std::shared_ptr<SLE const> const& after)
{
    // `after` can never be null, even if the trust line is deleted.
    XRPL_ASSERT(
        after, "ripple::TransfersNotFrozen::isValidEntry : valid after.");
    if (!after)
    {
        return false;
    }

    if (after->getType() == ltACCOUNT_ROOT)
    {
        possibleIssuers_.emplace(after->at(sfAccount), after);
        return false;
    }

    /* While LedgerEntryTypesMatch invariant also checks types, all invariants
     * are processed regardless of previous failures.
     *
     * This type check is still necessary here because it prevents potential
     * issues in subsequent processing.
     */
    return after->getType() == ltRIPPLE_STATE &&
        (!before || before->getType() == ltRIPPLE_STATE);
}

STAmount
TransfersNotFrozen::calculateBalanceChange(
    std::shared_ptr<SLE const> const& before,
    std::shared_ptr<SLE const> const& after,
    bool isDelete)
{
    auto const getBalance = [](auto const& line, auto const& other, bool zero) {
        STAmount amt =
            line ? line->at(sfBalance) : other->at(sfBalance).zeroed();
        return zero ? amt.zeroed() : amt;
    };

    /* Trust lines can be created dynamically by other transactions such as
     * Payment and OfferCreate that cross offers. Such trust line won't be
     * created frozen, but the sender might be, so the starting balance must be
     * treated as zero.
     */
    auto const balanceBefore = getBalance(before, after, false);

    /* Same as above, trust lines can be dynamically deleted, and for frozen
     * trust lines, payments not involving the issuer must be blocked. This is
     * achieved by treating the final balance as zero when isDelete=true to
     * ensure frozen line restrictions are enforced even during deletion.
     */
    auto const balanceAfter = getBalance(after, before, isDelete);

    return balanceAfter - balanceBefore;
}

void
TransfersNotFrozen::recordBalance(Issue const& issue, BalanceChange change)
{
    XRPL_ASSERT(
        change.balanceChangeSign,
        "ripple::TransfersNotFrozen::recordBalance : valid trustline "
        "balance sign.");
    auto& changes = balanceChanges_[issue];
    if (change.balanceChangeSign < 0)
        changes.senders.emplace_back(std::move(change));
    else
        changes.receivers.emplace_back(std::move(change));
}

void
TransfersNotFrozen::recordBalanceChanges(
    std::shared_ptr<SLE const> const& after,
    STAmount const& balanceChange)
{
    auto const balanceChangeSign = balanceChange.signum();
    auto const currency = after->at(sfBalance).getCurrency();

    // Change from low account's perspective, which is trust line default
    recordBalance(
        {currency, after->at(sfHighLimit).getIssuer()},
        {after, balanceChangeSign});

    // Change from high account's perspective, which reverses the sign.
    recordBalance(
        {currency, after->at(sfLowLimit).getIssuer()},
        {after, -balanceChangeSign});
}

std::shared_ptr<SLE const>
TransfersNotFrozen::findIssuer(AccountID const& issuerID, ReadView const& view)
{
    if (auto it = possibleIssuers_.find(issuerID); it != possibleIssuers_.end())
    {
        return it->second;
    }

    return view.read(keylet::account(issuerID));
}

bool
TransfersNotFrozen::validateIssuerChanges(
    std::shared_ptr<SLE const> const& issuer,
    IssuerChanges const& changes,
    STTx const& tx,
    beast::Journal const& j,
    bool enforce)
{
    if (!issuer)
    {
        return false;
    }

    bool const globalFreeze = issuer->isFlag(lsfGlobalFreeze);
    if (changes.receivers.empty() || changes.senders.empty())
    {
        /* If there are no receivers, then the holder(s) are returning
         * their tokens to the issuer. Likewise, if there are no
         * senders, then the issuer is issuing tokens to the holder(s).
         * This is allowed regardless of the issuer's freeze flags. (The
         * holder may have contradicting freeze flags, but that will be
         * checked when the holder is treated as issuer.)
         */
        return true;
    }

    for (auto const& actors : {changes.senders, changes.receivers})
    {
        for (auto const& change : actors)
        {
            bool const high = change.line->at(sfLowLimit).getIssuer() ==
                issuer->at(sfAccount);

            if (!validateFrozenState(
                    change, high, tx, j, enforce, globalFreeze))
            {
                return false;
            }
        }
    }
    return true;
}

bool
TransfersNotFrozen::validateFrozenState(
    BalanceChange const& change,
    bool high,
    STTx const& tx,
    beast::Journal const& j,
    bool enforce,
    bool globalFreeze)
{
    bool const freeze = change.balanceChangeSign < 0 &&
        change.line->isFlag(high ? lsfLowFreeze : lsfHighFreeze);
    bool const deepFreeze =
        change.line->isFlag(high ? lsfLowDeepFreeze : lsfHighDeepFreeze);
    bool const frozen = globalFreeze || deepFreeze || freeze;

    bool const isAMMLine = change.line->isFlag(lsfAMMNode);

    if (!frozen)
    {
        return true;
    }

    // AMMClawbacks are allowed to override some freeze rules
    if ((!isAMMLine || globalFreeze) && hasPrivilege(tx, overrideFreeze))
    {
        JLOG(j.debug()) << "Invariant check allowing funds to be moved "
                        << (change.balanceChangeSign > 0 ? "to" : "from")
                        << " a frozen trustline for AMMClawback "
                        << tx.getTransactionID();
        return true;
    }

    JLOG(j.fatal()) << "Invariant failed: Attempting to move frozen funds for "
                    << tx.getTransactionID();
    // The comment above starting with "assert(enforce)" explains this assert.
    XRPL_ASSERT(
        enforce,
        "ripple::TransfersNotFrozen::validateFrozenState : enforce "
        "invariant.");

    if (enforce)
    {
        return false;
    }

    return true;
}

//------------------------------------------------------------------------------

void
ValidNewAccountRoot::visitEntry(
    bool,
    std::shared_ptr<SLE const> const& before,
    std::shared_ptr<SLE const> const& after)
{
    if (!before && after->getType() == ltACCOUNT_ROOT)
    {
        accountsCreated_++;
        accountSeq_ = (*after)[sfSequence];
        pseudoAccount_ = isPseudoAccount(after);
        flags_ = after->getFlags();
    }
}

bool
ValidNewAccountRoot::finalize(
    STTx const& tx,
    TER const result,
    XRPAmount const,
    ReadView const& view,
    beast::Journal const& j)
{
    if (accountsCreated_ == 0)
        return true;

    if (accountsCreated_ > 1)
    {
        JLOG(j.fatal()) << "Invariant failed: multiple accounts "
                           "created in a single transaction";
        return false;
    }

    // From this point on we know exactly one account was created.
    if (hasPrivilege(tx, createAcct | createPseudoAcct) && result == tesSUCCESS)
    {
        bool const pseudoAccount =
            (pseudoAccount_ && view.rules().enabled(featureSingleAssetVault));

        if (pseudoAccount && !hasPrivilege(tx, createPseudoAcct))
        {
            JLOG(j.fatal()) << "Invariant failed: pseudo-account created by a "
                               "wrong transaction type";
            return false;
        }

        std::uint32_t const startingSeq =                     //
            pseudoAccount                                     //
            ? 0                                               //
            : view.rules().enabled(featureDeletableAccounts)  //
                ? view.seq()                                  //
                : 1;

        if (accountSeq_ != startingSeq)
        {
            JLOG(j.fatal()) << "Invariant failed: account created with "
                               "wrong starting sequence number";
            return false;
        }

        if (pseudoAccount)
        {
            std::uint32_t const expected =
                (lsfDisableMaster | lsfDefaultRipple | lsfDepositAuth);
            if (flags_ != expected)
            {
                JLOG(j.fatal())
                    << "Invariant failed: pseudo-account created with "
                       "wrong flags";
                return false;
            }
        }

        return true;
    }

    JLOG(j.fatal()) << "Invariant failed: account root created illegally";
    return false;
}  // namespace ripple

//------------------------------------------------------------------------------

void
ValidNFTokenPage::visitEntry(
    bool isDelete,
    std::shared_ptr<SLE const> const& before,
    std::shared_ptr<SLE const> const& after)
{
    static constexpr uint256 const& pageBits = nft::pageMask;
    static constexpr uint256 const accountBits = ~pageBits;

    if ((before && before->getType() != ltNFTOKEN_PAGE) ||
        (after && after->getType() != ltNFTOKEN_PAGE))
        return;

    auto check = [this, isDelete](std::shared_ptr<SLE const> const& sle) {
        uint256 const account = sle->key() & accountBits;
        uint256 const hiLimit = sle->key() & pageBits;
        std::optional<uint256> const prev = (*sle)[~sfPreviousPageMin];

        // Make sure that any page links...
        //  1. Are properly associated with the owning account and
        //  2. The page is correctly ordered between links.
        if (prev)
        {
            if (account != (*prev & accountBits))
                badLink_ = true;

            if (hiLimit <= (*prev & pageBits))
                badLink_ = true;
        }

        if (auto const next = (*sle)[~sfNextPageMin])
        {
            if (account != (*next & accountBits))
                badLink_ = true;

            if (hiLimit >= (*next & pageBits))
                badLink_ = true;
        }

        {
            auto const& nftokens = sle->getFieldArray(sfNFTokens);

            // An NFTokenPage should never contain too many tokens or be empty.
            if (std::size_t const nftokenCount = nftokens.size();
                (!isDelete && nftokenCount == 0) ||
                nftokenCount > dirMaxTokensPerPage)
                invalidSize_ = true;

            // If prev is valid, use it to establish a lower bound for
            // page entries.  If prev is not valid the lower bound is zero.
            uint256 const loLimit =
                prev ? *prev & pageBits : uint256(beast::zero);

            // Also verify that all NFTokenIDs in the page are sorted.
            uint256 loCmp = loLimit;
            for (auto const& obj : nftokens)
            {
                uint256 const tokenID = obj[sfNFTokenID];
                if (!nft::compareTokens(loCmp, tokenID))
                    badSort_ = true;
                loCmp = tokenID;

                // None of the NFTs on this page should belong on lower or
                // higher pages.
                if (uint256 const tokenPageBits = tokenID & pageBits;
                    tokenPageBits < loLimit || tokenPageBits >= hiLimit)
                    badEntry_ = true;

                if (auto uri = obj[~sfURI]; uri && uri->empty())
                    badURI_ = true;
            }
        }
    };

    if (before)
    {
        check(before);

        // While an account's NFToken directory contains any NFTokens, the last
        // NFTokenPage (with 96 bits of 1 in the low part of the index) should
        // never be deleted.
        if (isDelete && (before->key() & nft::pageMask) == nft::pageMask &&
            before->isFieldPresent(sfPreviousPageMin))
        {
            deletedFinalPage_ = true;
        }
    }

    if (after)
        check(after);

    if (!isDelete && before && after)
    {
        // If the NFTokenPage
        //  1. Has a NextMinPage field in before, but loses it in after, and
        //  2. This is not the last page in the directory
        // Then we have identified a corruption in the links between the
        // NFToken pages in the NFToken directory.
        if ((before->key() & nft::pageMask) != nft::pageMask &&
            before->isFieldPresent(sfNextPageMin) &&
            !after->isFieldPresent(sfNextPageMin))
        {
            deletedLink_ = true;
        }
    }
}

bool
ValidNFTokenPage::finalize(
    STTx const& tx,
    TER const result,
    XRPAmount const,
    ReadView const& view,
    beast::Journal const& j)
{
    if (badLink_)
    {
        JLOG(j.fatal()) << "Invariant failed: NFT page is improperly linked.";
        return false;
    }

    if (badEntry_)
    {
        JLOG(j.fatal()) << "Invariant failed: NFT found in incorrect page.";
        return false;
    }

    if (badSort_)
    {
        JLOG(j.fatal()) << "Invariant failed: NFTs on page are not sorted.";
        return false;
    }

    if (badURI_)
    {
        JLOG(j.fatal()) << "Invariant failed: NFT contains empty URI.";
        return false;
    }

    if (invalidSize_)
    {
        JLOG(j.fatal()) << "Invariant failed: NFT page has invalid size.";
        return false;
    }

    if (view.rules().enabled(fixNFTokenPageLinks))
    {
        if (deletedFinalPage_)
        {
            JLOG(j.fatal()) << "Invariant failed: Last NFT page deleted with "
                               "non-empty directory.";
            return false;
        }
        if (deletedLink_)
        {
            JLOG(j.fatal()) << "Invariant failed: Lost NextMinPage link.";
            return false;
        }
    }

    return true;
}

//------------------------------------------------------------------------------
void
NFTokenCountTracking::visitEntry(
    bool,
    std::shared_ptr<SLE const> const& before,
    std::shared_ptr<SLE const> const& after)
{
    if (before && before->getType() == ltACCOUNT_ROOT)
    {
        beforeMintedTotal += (*before)[~sfMintedNFTokens].value_or(0);
        beforeBurnedTotal += (*before)[~sfBurnedNFTokens].value_or(0);
    }

    if (after && after->getType() == ltACCOUNT_ROOT)
    {
        afterMintedTotal += (*after)[~sfMintedNFTokens].value_or(0);
        afterBurnedTotal += (*after)[~sfBurnedNFTokens].value_or(0);
    }
}

bool
NFTokenCountTracking::finalize(
    STTx const& tx,
    TER const result,
    XRPAmount const,
    ReadView const& view,
    beast::Journal const& j)
{
    if (!hasPrivilege(tx, changeNFTCounts))
    {
        if (beforeMintedTotal != afterMintedTotal)
        {
            JLOG(j.fatal()) << "Invariant failed: the number of minted tokens "
                               "changed without a mint transaction!";
            return false;
        }

        if (beforeBurnedTotal != afterBurnedTotal)
        {
            JLOG(j.fatal()) << "Invariant failed: the number of burned tokens "
                               "changed without a burn transaction!";
            return false;
        }

        return true;
    }

    if (tx.getTxnType() == ttNFTOKEN_MINT)
    {
        if (result == tesSUCCESS && beforeMintedTotal >= afterMintedTotal)
        {
            JLOG(j.fatal())
                << "Invariant failed: successful minting didn't increase "
                   "the number of minted tokens.";
            return false;
        }

        if (result != tesSUCCESS && beforeMintedTotal != afterMintedTotal)
        {
            JLOG(j.fatal()) << "Invariant failed: failed minting changed the "
                               "number of minted tokens.";
            return false;
        }

        if (beforeBurnedTotal != afterBurnedTotal)
        {
            JLOG(j.fatal())
                << "Invariant failed: minting changed the number of "
                   "burned tokens.";
            return false;
        }
    }

    if (tx.getTxnType() == ttNFTOKEN_BURN)
    {
        if (result == tesSUCCESS)
        {
            if (beforeBurnedTotal >= afterBurnedTotal)
            {
                JLOG(j.fatal())
                    << "Invariant failed: successful burning didn't increase "
                       "the number of burned tokens.";
                return false;
            }
        }

        if (result != tesSUCCESS && beforeBurnedTotal != afterBurnedTotal)
        {
            JLOG(j.fatal()) << "Invariant failed: failed burning changed the "
                               "number of burned tokens.";
            return false;
        }

        if (beforeMintedTotal != afterMintedTotal)
        {
            JLOG(j.fatal())
                << "Invariant failed: burning changed the number of "
                   "minted tokens.";
            return false;
        }
    }

    return true;
}

//------------------------------------------------------------------------------

void
ValidClawback::visitEntry(
    bool,
    std::shared_ptr<SLE const> const& before,
    std::shared_ptr<SLE const> const&)
{
    if (before && before->getType() == ltRIPPLE_STATE)
        trustlinesChanged++;

    if (before && before->getType() == ltMPTOKEN)
        mptokensChanged++;
}

bool
ValidClawback::finalize(
    STTx const& tx,
    TER const result,
    XRPAmount const,
    ReadView const& view,
    beast::Journal const& j)
{
    if (tx.getTxnType() != ttCLAWBACK)
        return true;

    if (result == tesSUCCESS)
    {
        if (trustlinesChanged > 1)
        {
            JLOG(j.fatal())
                << "Invariant failed: more than one trustline changed.";
            return false;
        }

        if (mptokensChanged > 1)
        {
            JLOG(j.fatal())
                << "Invariant failed: more than one mptokens changed.";
            return false;
        }

        if (trustlinesChanged == 1)
        {
            AccountID const issuer = tx.getAccountID(sfAccount);
            STAmount const& amount = tx.getFieldAmount(sfAmount);
            AccountID const& holder = amount.getIssuer();
            STAmount const holderBalance = accountHolds(
                view, holder, amount.getCurrency(), issuer, fhIGNORE_FREEZE, j);

            if (holderBalance.signum() < 0)
            {
                JLOG(j.fatal())
                    << "Invariant failed: trustline balance is negative";
                return false;
            }
        }
    }
    else
    {
        if (trustlinesChanged != 0)
        {
            JLOG(j.fatal()) << "Invariant failed: some trustlines were changed "
                               "despite failure of the transaction.";
            return false;
        }

        if (mptokensChanged != 0)
        {
            JLOG(j.fatal()) << "Invariant failed: some mptokens were changed "
                               "despite failure of the transaction.";
            return false;
        }
    }

    return true;
}

//------------------------------------------------------------------------------

void
ValidMPTIssuance::visitEntry(
    bool isDelete,
    std::shared_ptr<SLE const> const& before,
    std::shared_ptr<SLE const> const& after)
{
    if (after && after->getType() == ltMPTOKEN_ISSUANCE)
    {
        if (isDelete)
            mptIssuancesDeleted_++;
        else if (!before)
            mptIssuancesCreated_++;
    }

    if (after && after->getType() == ltMPTOKEN)
    {
        if (isDelete)
            mptokensDeleted_++;
        else if (!before)
            mptokensCreated_++;
    }
}

bool
ValidMPTIssuance::finalize(
    STTx const& tx,
    TER const result,
    XRPAmount const _fee,
    ReadView const& view,
    beast::Journal const& j)
{
    if (result == tesSUCCESS)
    {
        if (hasPrivilege(tx, createMPTIssuance))
        {
            if (mptIssuancesCreated_ == 0)
            {
                JLOG(j.fatal()) << "Invariant failed: transaction "
                                   "succeeded without creating a MPT issuance";
            }
            else if (mptIssuancesDeleted_ != 0)
            {
                JLOG(j.fatal()) << "Invariant failed: transaction "
                                   "succeeded while removing MPT issuances";
            }
            else if (mptIssuancesCreated_ > 1)
            {
                JLOG(j.fatal()) << "Invariant failed: transaction "
                                   "succeeded but created multiple issuances";
            }

            return mptIssuancesCreated_ == 1 && mptIssuancesDeleted_ == 0;
        }

        if (hasPrivilege(tx, destroyMPTIssuance))
        {
            if (mptIssuancesDeleted_ == 0)
            {
                JLOG(j.fatal()) << "Invariant failed: MPT issuance deletion "
                                   "succeeded without removing a MPT issuance";
            }
            else if (mptIssuancesCreated_ > 0)
            {
                JLOG(j.fatal()) << "Invariant failed: MPT issuance deletion "
                                   "succeeded while creating MPT issuances";
            }
            else if (mptIssuancesDeleted_ > 1)
            {
                JLOG(j.fatal()) << "Invariant failed: MPT issuance deletion "
                                   "succeeded but deleted multiple issuances";
            }

            return mptIssuancesCreated_ == 0 && mptIssuancesDeleted_ == 1;
        }

        // ttESCROW_FINISH may authorize an MPT, but it can't have the
        // mayAuthorizeMPT privilege, because that may cause
        // non-amendment-gated side effects.
        bool const enforceEscrowFinish = (tx.getTxnType() == ttESCROW_FINISH) &&
            (view.rules().enabled(featureSingleAssetVault)
             /*
               TODO: Uncomment when LendingProtocol is defined
               || view.rules().enabled(featureLendingProtocol)*/
            );
        if (hasPrivilege(tx, mustAuthorizeMPT | mayAuthorizeMPT) ||
            enforceEscrowFinish)
        {
            bool const submittedByIssuer = tx.isFieldPresent(sfHolder);

            if (mptIssuancesCreated_ > 0)
            {
                JLOG(j.fatal()) << "Invariant failed: MPT authorize "
                                   "succeeded but created MPT issuances";
                return false;
            }
            else if (mptIssuancesDeleted_ > 0)
            {
                JLOG(j.fatal()) << "Invariant failed: MPT authorize "
                                   "succeeded but deleted issuances";
                return false;
            }
            else if (
                submittedByIssuer &&
                (mptokensCreated_ > 0 || mptokensDeleted_ > 0))
            {
                JLOG(j.fatal())
                    << "Invariant failed: MPT authorize submitted by issuer "
                       "succeeded but created/deleted mptokens";
                return false;
            }
            else if (
                !submittedByIssuer && hasPrivilege(tx, mustAuthorizeMPT) &&
                (mptokensCreated_ + mptokensDeleted_ != 1))
            {
                // if the holder submitted this tx, then a mptoken must be
                // either created or deleted.
                JLOG(j.fatal())
                    << "Invariant failed: MPT authorize submitted by holder "
                       "succeeded but created/deleted bad number of mptokens";
                return false;
            }

            return true;
        }
        if (tx.getTxnType() == ttESCROW_FINISH)
        {
            // ttESCROW_FINISH may authorize an MPT, but it can't have the
            // mayAuthorizeMPT privilege, because that may cause
            // non-amendment-gated side effects.
            XRPL_ASSERT_PARTS(
                !enforceEscrowFinish,
                "ripple::ValidMPTIssuance::finalize",
                "not escrow finish tx");
            return true;
        }

        if (hasPrivilege(tx, mayDeleteMPT) && mptokensDeleted_ == 1 &&
            mptokensCreated_ == 0 && mptIssuancesCreated_ == 0 &&
            mptIssuancesDeleted_ == 0)
            return true;
    }

    if (mptIssuancesCreated_ != 0)
    {
        JLOG(j.fatal()) << "Invariant failed: a MPT issuance was created";
    }
    else if (mptIssuancesDeleted_ != 0)
    {
        JLOG(j.fatal()) << "Invariant failed: a MPT issuance was deleted";
    }
    else if (mptokensCreated_ != 0)
    {
        JLOG(j.fatal()) << "Invariant failed: a MPToken was created";
    }
    else if (mptokensDeleted_ != 0)
    {
        JLOG(j.fatal()) << "Invariant failed: a MPToken was deleted";
    }

    return mptIssuancesCreated_ == 0 && mptIssuancesDeleted_ == 0 &&
        mptokensCreated_ == 0 && mptokensDeleted_ == 0;
}

//------------------------------------------------------------------------------

void
ValidPermissionedDomain::visitEntry(
    bool,
    std::shared_ptr<SLE const> const& before,
    std::shared_ptr<SLE const> const& after)
{
    if (before && before->getType() != ltPERMISSIONED_DOMAIN)
        return;
    if (after && after->getType() != ltPERMISSIONED_DOMAIN)
        return;

    auto check = [](SleStatus& sleStatus,
                    std::shared_ptr<SLE const> const& sle) {
        auto const& credentials = sle->getFieldArray(sfAcceptedCredentials);
        sleStatus.credentialsSize_ = credentials.size();
        auto const sorted = credentials::makeSorted(credentials);
        sleStatus.isUnique_ = !sorted.empty();

        // If array have duplicates then all the other checks are invalid
        sleStatus.isSorted_ = false;

        if (sleStatus.isUnique_)
        {
            unsigned i = 0;
            for (auto const& cred : sorted)
            {
                auto const& credTx = credentials[i++];
                sleStatus.isSorted_ = (cred.first == credTx[sfIssuer]) &&
                    (cred.second == credTx[sfCredentialType]);
                if (!sleStatus.isSorted_)
                    break;
            }
        }
    };

    if (before)
    {
        sleStatus_[0] = SleStatus();
        check(*sleStatus_[0], after);
    }

    if (after)
    {
        sleStatus_[1] = SleStatus();
        check(*sleStatus_[1], after);
    }
}

bool
ValidPermissionedDomain::finalize(
    STTx const& tx,
    TER const result,
    XRPAmount const,
    ReadView const& view,
    beast::Journal const& j)
{
    if (tx.getTxnType() != ttPERMISSIONED_DOMAIN_SET || result != tesSUCCESS)
        return true;

    auto check = [](SleStatus const& sleStatus, beast::Journal const& j) {
        if (!sleStatus.credentialsSize_)
        {
            JLOG(j.fatal()) << "Invariant failed: permissioned domain with "
                               "no rules.";
            return false;
        }

        if (sleStatus.credentialsSize_ >
            maxPermissionedDomainCredentialsArraySize)
        {
            JLOG(j.fatal()) << "Invariant failed: permissioned domain bad "
                               "credentials size "
                            << sleStatus.credentialsSize_;
            return false;
        }

        if (!sleStatus.isUnique_)
        {
            JLOG(j.fatal())
                << "Invariant failed: permissioned domain credentials "
                   "aren't unique";
            return false;
        }

        if (!sleStatus.isSorted_)
        {
            JLOG(j.fatal())
                << "Invariant failed: permissioned domain credentials "
                   "aren't sorted";
            return false;
        }

        return true;
    };

    return (sleStatus_[0] ? check(*sleStatus_[0], j) : true) &&
        (sleStatus_[1] ? check(*sleStatus_[1], j) : true);
}

//------------------------------------------------------------------------------

void
ValidPseudoAccounts::visitEntry(
    bool isDelete,
    std::shared_ptr<SLE const> const& before,
    std::shared_ptr<SLE const> const& after)
{
    if (isDelete)
        // Deletion is ignored
        return;

    if (after && after->getType() == ltACCOUNT_ROOT)
    {
        bool const isPseudo = [&]() {
            // isPseudoAccount checks that any of the pseudo-account fields are
            // set.
            if (isPseudoAccount(after))
                return true;
            // Not all pseudo-accounts have a zero sequence, but all accounts
            // with a zero sequence had better be pseudo-accounts.
            if (after->at(sfSequence) == 0)
                return true;

            return false;
        }();
        if (isPseudo)
        {
            // Pseudo accounts must have the following properties:
            // 1. Exactly one of the pseudo-account fields is set.
            // 2. The sequence number is not changed.
            // 3. The lsfDisableMaster, lsfDefaultRipple, and lsfDepositAuth
            // flags are set.
            // 4. The RegularKey is not set.
            {
                std::vector<SField const*> const& fields =
                    getPseudoAccountFields();

                auto const numFields = std::count_if(
                    fields.begin(),
                    fields.end(),
                    [&after](SField const* sf) -> bool {
                        return after->isFieldPresent(*sf);
                    });
                if (numFields != 1)
                {
                    std::stringstream error;
                    error << "pseudo-account has " << numFields
                          << " pseudo-account fields set";
                    errors_.emplace_back(error.str());
                }
            }
            if (before && before->at(sfSequence) != after->at(sfSequence))
            {
                errors_.emplace_back("pseudo-account sequence changed");
            }
            if (!after->isFlag(
                    lsfDisableMaster | lsfDefaultRipple | lsfDepositAuth))
            {
                errors_.emplace_back("pseudo-account flags are not set");
            }
            if (after->isFieldPresent(sfRegularKey))
            {
                errors_.emplace_back("pseudo-account has a regular key");
            }
        }
    }
}

bool
ValidPseudoAccounts::finalize(
    STTx const& tx,
    TER const,
    XRPAmount const,
    ReadView const& view,
    beast::Journal const& j)
{
    bool const enforce = view.rules().enabled(featureSingleAssetVault);

    // The comment above starting with "assert(enforce)" explains this assert.
    XRPL_ASSERT(
        errors_.empty() || enforce,
        "ripple::ValidPseudoAccounts::finalize : no bad "
        "changes or enforce invariant");
    if (!errors_.empty())
    {
        for (auto const& error : errors_)
        {
            JLOG(j.fatal()) << "Invariant failed: " << error;
        }
        if (enforce)
            return false;
    }
    return true;
}

//------------------------------------------------------------------------------

void
ValidPermissionedDEX::visitEntry(
    bool,
    std::shared_ptr<SLE const> const& before,
    std::shared_ptr<SLE const> const& after)
{
    if (after && after->getType() == ltDIR_NODE)
    {
        if (after->isFieldPresent(sfDomainID))
            domains_.insert(after->getFieldH256(sfDomainID));
    }

    if (after && after->getType() == ltOFFER)
    {
        if (after->isFieldPresent(sfDomainID))
            domains_.insert(after->getFieldH256(sfDomainID));
        else
            regularOffers_ = true;

        // if a hybrid offer is missing domain or additional book, there's
        // something wrong
        if (after->isFlag(lsfHybrid) &&
            (!after->isFieldPresent(sfDomainID) ||
             !after->isFieldPresent(sfAdditionalBooks) ||
             after->getFieldArray(sfAdditionalBooks).size() > 1))
            badHybrids_ = true;
    }
}

bool
ValidPermissionedDEX::finalize(
    STTx const& tx,
    TER const result,
    XRPAmount const,
    ReadView const& view,
    beast::Journal const& j)
{
    auto const txType = tx.getTxnType();
    if ((txType != ttPAYMENT && txType != ttOFFER_CREATE) ||
        result != tesSUCCESS)
        return true;

    // For each offercreate transaction, check if
    // permissioned offers are valid
    if (txType == ttOFFER_CREATE && badHybrids_)
    {
        JLOG(j.fatal()) << "Invariant failed: hybrid offer is malformed";
        return false;
    }

    if (!tx.isFieldPresent(sfDomainID))
        return true;

    auto const domain = tx.getFieldH256(sfDomainID);

    if (!view.exists(keylet::permissionedDomain(domain)))
    {
        JLOG(j.fatal()) << "Invariant failed: domain doesn't exist";
        return false;
    }

    // for both payment and offercreate, there shouldn't be another domain
    // that's different from the domain specified
    for (auto const& d : domains_)
    {
        if (d != domain)
        {
            JLOG(j.fatal()) << "Invariant failed: transaction"
                               " consumed wrong domains";
            return false;
        }
    }

    if (regularOffers_)
    {
        JLOG(j.fatal()) << "Invariant failed: domain transaction"
                           " affected regular offers";
        return false;
    }

    return true;
}

void
ValidAMM::visitEntry(
    bool isDelete,
    std::shared_ptr<SLE const> const& before,
    std::shared_ptr<SLE const> const& after)
{
    if (isDelete)
        return;

    if (after)
    {
        auto const type = after->getType();
        // AMM object changed
        if (type == ltAMM)
        {
            ammAccount_ = after->getAccountID(sfAccount);
            lptAMMBalanceAfter_ = after->getFieldAmount(sfLPTokenBalance);
        }
        // AMM pool changed
        else if (
            (type == ltRIPPLE_STATE && after->getFlags() & lsfAMMNode) ||
            (type == ltACCOUNT_ROOT && after->isFieldPresent(sfAMMID)))
        {
            ammPoolChanged_ = true;
        }
    }

    if (before)
    {
        // AMM object changed
        if (before->getType() == ltAMM)
        {
            lptAMMBalanceBefore_ = before->getFieldAmount(sfLPTokenBalance);
        }
    }
}

static bool
validBalances(
    STAmount const& amount,
    STAmount const& amount2,
    STAmount const& lptAMMBalance,
    ValidAMM::ZeroAllowed zeroAllowed)
{
    bool const positive = amount > beast::zero && amount2 > beast::zero &&
        lptAMMBalance > beast::zero;
    if (zeroAllowed == ValidAMM::ZeroAllowed::Yes)
        return positive ||
            (amount == beast::zero && amount2 == beast::zero &&
             lptAMMBalance == beast::zero);
    return positive;
}

bool
ValidAMM::finalizeVote(bool enforce, beast::Journal const& j) const
{
    if (lptAMMBalanceAfter_ != lptAMMBalanceBefore_ || ammPoolChanged_)
    {
        // LPTokens and the pool can not change on vote
        // LCOV_EXCL_START
        JLOG(j.error()) << "AMMVote invariant failed: "
                        << lptAMMBalanceBefore_.value_or(STAmount{}) << " "
                        << lptAMMBalanceAfter_.value_or(STAmount{}) << " "
                        << ammPoolChanged_;
        if (enforce)
            return false;
        // LCOV_EXCL_STOP
    }

    return true;
}

bool
ValidAMM::finalizeBid(bool enforce, beast::Journal const& j) const
{
    if (ammPoolChanged_)
    {
        // The pool can not change on bid
        // LCOV_EXCL_START
        JLOG(j.error()) << "AMMBid invariant failed: pool changed";
        if (enforce)
            return false;
        // LCOV_EXCL_STOP
    }
    // LPTokens are burnt, therefore there should be fewer LPTokens
    else if (
        lptAMMBalanceBefore_ && lptAMMBalanceAfter_ &&
        (*lptAMMBalanceAfter_ > *lptAMMBalanceBefore_ ||
         *lptAMMBalanceAfter_ <= beast::zero))
    {
        // LCOV_EXCL_START
        JLOG(j.error()) << "AMMBid invariant failed: " << *lptAMMBalanceBefore_
                        << " " << *lptAMMBalanceAfter_;
        if (enforce)
            return false;
        // LCOV_EXCL_STOP
    }

    return true;
}

bool
ValidAMM::finalizeCreate(
    STTx const& tx,
    ReadView const& view,
    bool enforce,
    beast::Journal const& j) const
{
    if (!ammAccount_)
    {
        // LCOV_EXCL_START
        JLOG(j.error())
            << "AMMCreate invariant failed: AMM object is not created";
        if (enforce)
            return false;
        // LCOV_EXCL_STOP
    }
    else
    {
        auto const [amount, amount2] = ammPoolHolds(
            view,
            *ammAccount_,
            tx[sfAmount].get<Issue>(),
            tx[sfAmount2].get<Issue>(),
            fhIGNORE_FREEZE,
            j);
        // Create invariant:
        // sqrt(amount * amount2) == LPTokens
        // all balances are greater than zero
        if (!validBalances(
                amount, amount2, *lptAMMBalanceAfter_, ZeroAllowed::No) ||
            ammLPTokens(amount, amount2, lptAMMBalanceAfter_->issue()) !=
                *lptAMMBalanceAfter_)
        {
            JLOG(j.error()) << "AMMCreate invariant failed: " << amount << " "
                            << amount2 << " " << *lptAMMBalanceAfter_;
            if (enforce)
                return false;
        }
    }

    return true;
}

bool
ValidAMM::finalizeDelete(bool enforce, TER res, beast::Journal const& j) const
{
    if (ammAccount_)
    {
        // LCOV_EXCL_START
        std::string const msg = (res == tesSUCCESS)
            ? "AMM object is not deleted on tesSUCCESS"
            : "AMM object is changed on tecINCOMPLETE";
        JLOG(j.error()) << "AMMDelete invariant failed: " << msg;
        if (enforce)
            return false;
        // LCOV_EXCL_STOP
    }

    return true;
}

bool
ValidAMM::finalizeDEX(bool enforce, beast::Journal const& j) const
{
    if (ammAccount_)
    {
        // LCOV_EXCL_START
        JLOG(j.error()) << "AMM swap invariant failed: AMM object changed";
        if (enforce)
            return false;
        // LCOV_EXCL_STOP
    }

    return true;
}

bool
ValidAMM::generalInvariant(
    ripple::STTx const& tx,
    ripple::ReadView const& view,
    ZeroAllowed zeroAllowed,
    beast::Journal const& j) const
{
    auto const [amount, amount2] = ammPoolHolds(
        view,
        *ammAccount_,
        tx[sfAsset].get<Issue>(),
        tx[sfAsset2].get<Issue>(),
        fhIGNORE_FREEZE,
        j);
    // Deposit and Withdrawal invariant:
    // sqrt(amount * amount2) >= LPTokens
    // all balances are greater than zero
    // unless on last withdrawal
    auto const poolProductMean = root2(amount * amount2);
    bool const nonNegativeBalances =
        validBalances(amount, amount2, *lptAMMBalanceAfter_, zeroAllowed);
    bool const strongInvariantCheck = poolProductMean >= *lptAMMBalanceAfter_;
    // Allow for a small relative error if strongInvariantCheck fails
    auto weakInvariantCheck = [&]() {
        return *lptAMMBalanceAfter_ != beast::zero &&
            withinRelativeDistance(
                poolProductMean, Number{*lptAMMBalanceAfter_}, Number{1, -11});
    };
    if (!nonNegativeBalances ||
        (!strongInvariantCheck && !weakInvariantCheck()))
    {
        JLOG(j.error()) << "AMM " << tx.getTxnType() << " invariant failed: "
                        << tx.getHash(HashPrefix::transactionID) << " "
                        << ammPoolChanged_ << " " << amount << " " << amount2
                        << " " << poolProductMean << " "
                        << lptAMMBalanceAfter_->getText() << " "
                        << ((*lptAMMBalanceAfter_ == beast::zero)
                                ? Number{1}
                                : ((*lptAMMBalanceAfter_ - poolProductMean) /
                                   poolProductMean));
        return false;
    }

    return true;
}

bool
ValidAMM::finalizeDeposit(
    ripple::STTx const& tx,
    ripple::ReadView const& view,
    bool enforce,
    beast::Journal const& j) const
{
    if (!ammAccount_)
    {
        // LCOV_EXCL_START
        JLOG(j.error()) << "AMMDeposit invariant failed: AMM object is deleted";
        if (enforce)
            return false;
        // LCOV_EXCL_STOP
    }
    else if (!generalInvariant(tx, view, ZeroAllowed::No, j) && enforce)
        return false;

    return true;
}

bool
ValidAMM::finalizeWithdraw(
    ripple::STTx const& tx,
    ripple::ReadView const& view,
    bool enforce,
    beast::Journal const& j) const
{
    if (!ammAccount_)
    {
        // Last Withdraw or Clawback deleted AMM
    }
    else if (!generalInvariant(tx, view, ZeroAllowed::Yes, j))
    {
        if (enforce)
            return false;
    }

    return true;
}

bool
ValidAMM::finalize(
    STTx const& tx,
    TER const result,
    XRPAmount const,
    ReadView const& view,
    beast::Journal const& j)
{
    // Delete may return tecINCOMPLETE if there are too many
    // trustlines to delete.
    if (result != tesSUCCESS && result != tecINCOMPLETE)
        return true;

    bool const enforce = view.rules().enabled(fixAMMv1_3);

    switch (tx.getTxnType())
    {
        case ttAMM_CREATE:
            return finalizeCreate(tx, view, enforce, j);
        case ttAMM_DEPOSIT:
            return finalizeDeposit(tx, view, enforce, j);
        case ttAMM_CLAWBACK:
        case ttAMM_WITHDRAW:
            return finalizeWithdraw(tx, view, enforce, j);
        case ttAMM_BID:
            return finalizeBid(enforce, j);
        case ttAMM_VOTE:
            return finalizeVote(enforce, j);
        case ttAMM_DELETE:
            return finalizeDelete(enforce, result, j);
        case ttCHECK_CASH:
        case ttOFFER_CREATE:
        case ttPAYMENT:
            return finalizeDEX(enforce, j);
        default:
            break;
    }

    return true;
}

//------------------------------------------------------------------------------

ValidVault::Vault
ValidVault::Vault::make(SLE const& from)
{
    XRPL_ASSERT(
        from.getType() == ltVAULT,
        "ValidVault::Vault::make : from Vault object");

    ValidVault::Vault self;
    self.key = from.key();
    self.asset = from.at(sfAsset);
    self.pseudoId = from.getAccountID(sfAccount);
    self.shareMPTID = from.getFieldH192(sfShareMPTID);
    self.assetsTotal = from.at(sfAssetsTotal);
    self.assetsAvailable = from.at(sfAssetsAvailable);
    self.assetsMaximum = from.at(sfAssetsMaximum);
    self.lossUnrealized = from.at(sfLossUnrealized);
    return self;
}

ValidVault::Shares
ValidVault::Shares::make(SLE const& from)
{
    XRPL_ASSERT(
        from.getType() == ltMPTOKEN_ISSUANCE,
        "ValidVault::Shares::make : from MPTokenIssuance object");

    ValidVault::Shares self;
    self.share = MPTIssue(
        makeMptID(from.getFieldU32(sfSequence), from.getAccountID(sfIssuer)));
    self.sharesTotal = from.at(sfOutstandingAmount);
    self.sharesMaximum = from[~sfMaximumAmount].value_or(maxMPTokenAmount);
    return self;
}

void
ValidVault::visitEntry(
    bool isDelete,
    std::shared_ptr<SLE const> const& before,
    std::shared_ptr<SLE const> const& after)
{
    // If `before` is empty, this means an object is being created, in which
    // case `isDelete` must be false. Otherwise `before` and `after` are set and
    // `isDelete` indicates whether an object is being deleted or modified.
    XRPL_ASSERT(
        after != nullptr && (before != nullptr || !isDelete),
        "ripple::ValidVault::visitEntry : some object is available");

    // Number balanceDelta will capture the difference (delta) between "before"
    // state (zero if created) and "after" state (zero if destroyed), so the
    // invariants can validate that the change in account balances matches the
    // change in vault balances, stored to deltas_ at the end of this function.
    Number balanceDelta{};

    std::int8_t sign = 0;
    if (before)
    {
        switch (before->getType())
        {
            case ltVAULT:
                beforeVault_.push_back(Vault::make(*before));
                break;
            case ltMPTOKEN_ISSUANCE:
                // At this moment we have no way of telling if this object holds
                // vault shares or something else. Save it for finalize.
                beforeMPTs_.push_back(Shares::make(*before));
                balanceDelta = static_cast<std::int64_t>(
                    before->getFieldU64(sfOutstandingAmount));
                sign = 1;
                break;
            case ltMPTOKEN:
                balanceDelta =
                    static_cast<std::int64_t>(before->getFieldU64(sfMPTAmount));
                sign = -1;
                break;
            case ltACCOUNT_ROOT:
            case ltRIPPLE_STATE:
                balanceDelta = before->getFieldAmount(sfBalance);
                sign = -1;
                break;
            default:;
        }
    }

    if (!isDelete && after)
    {
        switch (after->getType())
        {
            case ltVAULT:
                afterVault_.push_back(Vault::make(*after));
                break;
            case ltMPTOKEN_ISSUANCE:
                // At this moment we have no way of telling if this object holds
                // vault shares or something else. Save it for finalize.
                afterMPTs_.push_back(Shares::make(*after));
                balanceDelta -= Number(static_cast<std::int64_t>(
                    after->getFieldU64(sfOutstandingAmount)));
                sign = 1;
                break;
            case ltMPTOKEN:
                balanceDelta -= Number(
                    static_cast<std::int64_t>(after->getFieldU64(sfMPTAmount)));
                sign = -1;
                break;
            case ltACCOUNT_ROOT:
            case ltRIPPLE_STATE:
                balanceDelta -= Number(after->getFieldAmount(sfBalance));
                sign = -1;
                break;
            default:;
        }
    }

    uint256 const key = (before ? before->key() : after->key());
    // Append to deltas if sign is non-zero, i.e. an object of an interesting
    // type has been updated. A transaction may update an object even when
    // its balance has not changed, e.g. transaction fee equals the amount
    // transferred to the account. We intentionally do not compare balanceDelta
    // against zero, to avoid missing such updates.
    if (sign != 0)
        deltas_[key] = balanceDelta * sign;
}

bool
ValidVault::finalize(
    STTx const& tx,
    TER const ret,
    XRPAmount const fee,
    ReadView const& view,
    beast::Journal const& j)
{
    bool const enforce = view.rules().enabled(featureSingleAssetVault);

    if (!isTesSuccess(ret))
        return true;  // Do not perform checks

    if (afterVault_.empty() && beforeVault_.empty())
    {
        if (hasPrivilege(tx, mustModifyVault))
        {
            JLOG(j.fatal()) <<  //
                "Invariant failed: vault operation succeeded without modifying "
                "a vault";
            XRPL_ASSERT(
                enforce, "ripple::ValidVault::finalize : vault noop invariant");
            return !enforce;
        }

        return true;  // Not a vault operation
    }
    else if (!hasPrivilege(tx, mustModifyVault))  // TODO: mayModifyVault
    {
        JLOG(j.fatal()) <<  //
            "Invariant failed: vault updated by a wrong transaction type";
        XRPL_ASSERT(
            enforce,
            "ripple::ValidVault::finalize : illegal vault transaction "
            "invariant");
        return !enforce;  // Also not a vault operation
    }

    if (beforeVault_.size() > 1 || afterVault_.size() > 1)
    {
        JLOG(j.fatal()) <<  //
            "Invariant failed: vault operation updated more than single vault";
        XRPL_ASSERT(
            enforce, "ripple::ValidVault::finalize : single vault invariant");
        return !enforce;  // That's all we can do here
    }

    auto const txnType = tx.getTxnType();

    // We do special handling for ttVAULT_DELETE first, because it's the only
    // vault-modifying transaction without an "after" state of the vault
    if (afterVault_.empty())
    {
        if (txnType != ttVAULT_DELETE)
        {
            JLOG(j.fatal()) <<  //
                "Invariant failed: vault deleted by a wrong transaction type";
            XRPL_ASSERT(
                enforce,
                "ripple::ValidVault::finalize : illegal vault deletion "
                "invariant");
            return !enforce;  // That's all we can do here
        }

        // Note, if afterVault_ is empty then we know that beforeVault_ is not
        // empty, as enforced at the top of this function
        auto const& beforeVault = beforeVault_[0];

        // At this moment we only know a vault is being deleted and there
        // might be some MPTokenIssuance objects which are deleted in the
        // same transaction. Find the one matching this vault.
        auto const deletedShares = [&]() -> std::optional<Shares> {
            for (auto const& e : beforeMPTs_)
            {
                if (e.share.getMptID() == beforeVault.shareMPTID)
                    return std::move(e);
            }
            return std::nullopt;
        }();

        if (!deletedShares)
        {
            JLOG(j.fatal()) << "Invariant failed: deleted vault must also "
                               "delete shares";
            XRPL_ASSERT(
                enforce,
                "ripple::ValidVault::finalize : shares deletion invariant");
            return !enforce;  // That's all we can do here
        }

        bool result = true;
        if (deletedShares->sharesTotal != 0)
        {
            JLOG(j.fatal()) << "Invariant failed: deleted vault must have no "
                               "shares outstanding";
            result = false;
        }
        if (beforeVault.assetsTotal != zero)
        {
            JLOG(j.fatal()) << "Invariant failed: deleted vault must have no "
                               "assets outstanding";
            result = false;
        }
        if (beforeVault.assetsAvailable != zero)
        {
            JLOG(j.fatal()) << "Invariant failed: deleted vault must have no "
                               "assets available";
            result = false;
        }

        return result;
    }
    else if (txnType == ttVAULT_DELETE)
    {
        JLOG(j.fatal()) << "Invariant failed: vault deletion succeeded without "
                           "deleting a vault";
        XRPL_ASSERT(
            enforce, "ripple::ValidVault::finalize : vault deletion invariant");
        return !enforce;  // That's all we can do here
    }

    // Note, `afterVault_.empty()` is handled above
    auto const& afterVault = afterVault_[0];
    XRPL_ASSERT(
        beforeVault_.empty() || beforeVault_[0].key == afterVault.key,
        "ripple::ValidVault::finalize : single vault operation");

    auto const updatedShares = [&]() -> std::optional<Shares> {
        // At this moment we only know that a vault is being updated and there
        // might be some MPTokenIssuance objects which are also updated in the
        // same transaction. Find the one matching the shares to this vault.
        // Note, we expect updatedMPTs collection to be extremely small. For
        // such collections linear search is faster than lookup.
        for (auto const& e : afterMPTs_)
        {
            if (e.share.getMptID() == afterVault.shareMPTID)
                return e;
        }

        auto const sleShares =
            view.read(keylet::mptIssuance(afterVault.shareMPTID));

        return sleShares ? std::optional<Shares>(Shares::make(*sleShares))
                         : std::nullopt;
    }();

    bool result = true;

    // Universal transaction checks
    if (!beforeVault_.empty())
    {
        auto const& beforeVault = beforeVault_[0];
        if (afterVault.asset != beforeVault.asset ||
            afterVault.pseudoId != beforeVault.pseudoId ||
            afterVault.shareMPTID != beforeVault.shareMPTID)
        {
            JLOG(j.fatal())
                << "Invariant failed: violation of vault immutable data";
            result = false;
        }
    }

    if (!updatedShares)
    {
        JLOG(j.fatal()) << "Invariant failed: updated vault must have shares";
        XRPL_ASSERT(
            enforce,
            "ripple::ValidVault::finalize : vault has shares invariant");
        return !enforce;  // That's all we can do here
    }

    if (updatedShares->sharesTotal == 0)
    {
        if (afterVault.assetsTotal != zero)
        {
            JLOG(j.fatal()) << "Invariant failed: updated zero sized "
                               "vault must have no assets outstanding";
            result = false;
        }
        if (afterVault.assetsAvailable != zero)
        {
            JLOG(j.fatal()) << "Invariant failed: updated zero sized "
                               "vault must have no assets available";
            result = false;
        }
    }
    else if (updatedShares->sharesTotal > updatedShares->sharesMaximum)
    {
        JLOG(j.fatal())  //
            << "Invariant failed: updated shares must not exceed maximum "
            << updatedShares->sharesMaximum;
        result = false;
    }

    if (afterVault.assetsAvailable < zero)
    {
        JLOG(j.fatal())
            << "Invariant failed: assets available must be positive";
        result = false;
    }

    if (afterVault.assetsAvailable > afterVault.assetsTotal)
    {
        JLOG(j.fatal()) << "Invariant failed: assets available must "
                           "not be greater than assets outstanding";
        result = false;
    }
    else if (
        afterVault.lossUnrealized >
        afterVault.assetsTotal - afterVault.assetsAvailable)
    {
        JLOG(j.fatal())  //
            << "Invariant failed: loss unrealized must not exceed "
               "the difference between assets outstanding and available";
        result = false;
    }

    if (afterVault.assetsTotal < zero)
    {
        JLOG(j.fatal())
            << "Invariant failed: assets outstanding must be positive";
        result = false;
    }

    if (afterVault.assetsMaximum < zero)
    {
        JLOG(j.fatal()) << "Invariant failed: assets maximum must be positive";
        result = false;
    }

    // Thanks to this check we can simply do `assert(!beforeVault_.empty()` when
    // enforcing invariants on transaction types other than ttVAULT_CREATE
    if (beforeVault_.empty() && txnType != ttVAULT_CREATE)
    {
        JLOG(j.fatal()) <<  //
            "Invariant failed: vault created by a wrong transaction type";
        XRPL_ASSERT(
            enforce, "ripple::ValidVault::finalize : vault creation invariant");
        return !enforce;  // That's all we can do here
    }

    if (!beforeVault_.empty() &&
        afterVault.lossUnrealized != beforeVault_[0].lossUnrealized)
    {
        JLOG(j.fatal()) <<  //
            "Invariant failed: vault transaction must not change loss "
            "unrealized";
        result = false;
    }

    auto const beforeShares = [&]() -> std::optional<Shares> {
        if (beforeVault_.empty())
            return std::nullopt;
        auto const& beforeVault = beforeVault_[0];

        for (auto const& e : beforeMPTs_)
        {
            if (e.share.getMptID() == beforeVault.shareMPTID)
                return std::move(e);
        }
        return std::nullopt;
    }();

    if (!beforeShares &&
        (tx.getTxnType() == ttVAULT_DEPOSIT ||   //
         tx.getTxnType() == ttVAULT_WITHDRAW ||  //
         tx.getTxnType() == ttVAULT_CLAWBACK))
    {
        JLOG(j.fatal()) << "Invariant failed: vault operation succeeded "
                           "without updating shares";
        XRPL_ASSERT(
            enforce, "ripple::ValidVault::finalize : shares noop invariant");
        return !enforce;  // That's all we can do here
    }

    auto const& vaultAsset = afterVault.asset;
    auto const deltaAssets = [&](AccountID const& id) -> std::optional<Number> {
        auto const get =  //
            [&](auto const& it, std::int8_t sign = 1) -> std::optional<Number> {
            if (it == deltas_.end())
                return std::nullopt;

            return it->second * sign;
        };

        return std::visit(
            [&]<typename TIss>(TIss const& issue) {
                if constexpr (std::is_same_v<TIss, Issue>)
                {
                    if (isXRP(issue))
                        return get(deltas_.find(keylet::account(id).key));
                    return get(
                        deltas_.find(keylet::line(id, issue).key),
                        id > issue.getIssuer() ? -1 : 1);
                }
                else if constexpr (std::is_same_v<TIss, MPTIssue>)
                {
                    return get(deltas_.find(
                        keylet::mptoken(issue.getMptID(), id).key));
                }
            },
            vaultAsset.value());
    };
    auto const deltaAssetsTxAccount = [&]() -> std::optional<Number> {
        auto ret = deltaAssets(tx[sfAccount]);
        // Nothing returned or not XRP transaction
        if (!ret.has_value() || !vaultAsset.native())
            return ret;

        // Delegated transaction; no need to compensate for fees
        if (auto const delegate = tx[~sfDelegate];
            delegate.has_value() && *delegate != tx[sfAccount])
            return ret;

        *ret += fee.drops();
        if (*ret == zero)
            return std::nullopt;

        return ret;
    };
    auto const deltaShares = [&](AccountID const& id) -> std::optional<Number> {
        auto const it = [&]() {
            if (id == afterVault.pseudoId)
                return deltas_.find(
                    keylet::mptIssuance(afterVault.shareMPTID).key);
            return deltas_.find(keylet::mptoken(afterVault.shareMPTID, id).key);
        }();

        return it != deltas_.end() ? std::optional<Number>(it->second)
                                   : std::nullopt;
    };

    // Technically this does not need to be a lambda, but it's more
    // convenient thanks to early "return false"; the not-so-nice
    // alternatives are several layers of nested if/else or more complex
    // (i.e. brittle) if statements.
    result &= [&]() {
        switch (txnType)
        {
            case ttVAULT_CREATE: {
                bool result = true;

                if (!beforeVault_.empty())
                {
                    JLOG(j.fatal())  //
                        << "Invariant failed: create operation must not have "
                           "updated a vault";
                    result = false;
                }

                if (afterVault.assetsAvailable != zero ||
                    afterVault.assetsTotal != zero ||
                    afterVault.lossUnrealized != zero ||
                    updatedShares->sharesTotal != 0)
                {
                    JLOG(j.fatal())  //
                        << "Invariant failed: created vault must be empty";
                    result = false;
                }

                if (afterVault.pseudoId != updatedShares->share.getIssuer())
                {
                    JLOG(j.fatal())  //
                        << "Invariant failed: shares issuer and vault "
                           "pseudo-account must be the same";
                    result = false;
                }

                auto const sleSharesIssuer = view.read(
                    keylet::account(updatedShares->share.getIssuer()));
                if (!sleSharesIssuer)
                {
                    JLOG(j.fatal())  //
                        << "Invariant failed: shares issuer must exist";
                    return false;
                }

                if (!isPseudoAccount(sleSharesIssuer))
                {
                    JLOG(j.fatal())  //
                        << "Invariant failed: shares issuer must be a "
                           "pseudo-account";
                    result = false;
                }

                if (auto const vaultId = (*sleSharesIssuer)[~sfVaultID];
                    !vaultId || *vaultId != afterVault.key)
                {
                    JLOG(j.fatal())  //
                        << "Invariant failed: shares issuer pseudo-account "
                           "must point back to the vault";
                    result = false;
                }

                return result;
            }
            case ttVAULT_SET: {
                bool result = true;

                XRPL_ASSERT(
                    !beforeVault_.empty(),
                    "ripple::ValidVault::finalize : set updated a vault");
                auto const& beforeVault = beforeVault_[0];

                auto const vaultDeltaAssets = deltaAssets(afterVault.pseudoId);
                if (vaultDeltaAssets)
                {
                    JLOG(j.fatal()) <<  //
                        "Invariant failed: set must not change vault balance";
                    result = false;
                }

                if (beforeVault.assetsTotal != afterVault.assetsTotal)
                {
                    JLOG(j.fatal()) <<  //
                        "Invariant failed: set must not change assets "
                        "outstanding";
                    result = false;
                }

                if (afterVault.assetsMaximum > zero &&
                    afterVault.assetsTotal > afterVault.assetsMaximum)
                {
                    JLOG(j.fatal()) <<  //
                        "Invariant failed: set assets outstanding must not "
                        "exceed assets maximum";
                    result = false;
                }

                if (beforeVault.assetsAvailable != afterVault.assetsAvailable)
                {
                    JLOG(j.fatal()) <<  //
                        "Invariant failed: set must not change assets "
                        "available";
                    result = false;
                }

                if (beforeShares && updatedShares &&
                    beforeShares->sharesTotal != updatedShares->sharesTotal)
                {
                    JLOG(j.fatal()) <<  //
                        "Invariant failed: set must not change shares "
                        "outstanding";
                    result = false;
                }

                return result;
            }
            case ttVAULT_DEPOSIT: {
                bool result = true;

                XRPL_ASSERT(
                    !beforeVault_.empty(),
                    "ripple::ValidVault::finalize : deposit updated a vault");
                auto const& beforeVault = beforeVault_[0];

                auto const vaultDeltaAssets = deltaAssets(afterVault.pseudoId);

                if (!vaultDeltaAssets)
                {
                    JLOG(j.fatal()) <<  //
                        "Invariant failed: deposit must change vault balance";
                    return false;  // That's all we can do
                }

                if (*vaultDeltaAssets > tx[sfAmount])
                {
                    JLOG(j.fatal()) <<  //
                        "Invariant failed: deposit must not change vault "
                        "balance by more than deposited amount";
                    result = false;
                }

                if (*vaultDeltaAssets <= zero)
                {
                    JLOG(j.fatal()) <<  //
                        "Invariant failed: deposit must increase vault balance";
                    result = false;
                }

                // Any payments (including deposits) made by the issuer
                // do not change their balance, but create funds instead.
                bool const issuerDeposit = [&]() -> bool {
                    if (vaultAsset.native())
                        return false;
                    return tx[sfAccount] == vaultAsset.getIssuer();
                }();

                if (!issuerDeposit)
                {
                    auto const accountDeltaAssets = deltaAssetsTxAccount();
                    if (!accountDeltaAssets)
                    {
                        JLOG(j.fatal()) <<  //
                            "Invariant failed: deposit must change depositor "
                            "balance";
                        return false;
                    }

                    if (*accountDeltaAssets >= zero)
                    {
                        JLOG(j.fatal()) <<  //
                            "Invariant failed: deposit must decrease depositor "
                            "balance";
                        result = false;
                    }

                    if (*accountDeltaAssets * -1 != *vaultDeltaAssets)
                    {
                        JLOG(j.fatal()) <<  //
                            "Invariant failed: deposit must change vault and "
                            "depositor balance by equal amount";
                        result = false;
                    }
                }

                if (afterVault.assetsMaximum > zero &&
                    afterVault.assetsTotal > afterVault.assetsMaximum)
                {
                    JLOG(j.fatal()) <<  //
                        "Invariant failed: deposit assets outstanding must not "
                        "exceed assets maximum";
                    result = false;
                }

                auto const accountDeltaShares = deltaShares(tx[sfAccount]);
                if (!accountDeltaShares)
                {
                    JLOG(j.fatal()) <<  //
                        "Invariant failed: deposit must change depositor "
                        "shares";
                    return false;  // That's all we can do
                }

                if (*accountDeltaShares <= zero)
                {
                    JLOG(j.fatal()) <<  //
                        "Invariant failed: deposit must increase depositor "
                        "shares";
                    result = false;
                }

                auto const vaultDeltaShares = deltaShares(afterVault.pseudoId);
                if (!vaultDeltaShares || *vaultDeltaShares == zero)
                {
                    JLOG(j.fatal()) <<  //
                        "Invariant failed: deposit must change vault shares";
                    return false;  // That's all we can do
                }

                if (*vaultDeltaShares * -1 != *accountDeltaShares)
                {
                    JLOG(j.fatal()) <<  //
                        "Invariant failed: deposit must change depositor and "
                        "vault shares by equal amount";
                    result = false;
                }

                if (beforeVault.assetsTotal + *vaultDeltaAssets !=
                    afterVault.assetsTotal)
                {
                    JLOG(j.fatal()) << "Invariant failed: deposit and assets "
                                       "outstanding must add up";
                    result = false;
                }
                if (beforeVault.assetsAvailable + *vaultDeltaAssets !=
                    afterVault.assetsAvailable)
                {
                    JLOG(j.fatal()) << "Invariant failed: deposit and assets "
                                       "available must add up";
                    result = false;
                }

                return result;
            }
            case ttVAULT_WITHDRAW: {
                bool result = true;

                XRPL_ASSERT(
                    !beforeVault_.empty(),
                    "ripple::ValidVault::finalize : withdrawal updated a "
                    "vault");
                auto const& beforeVault = beforeVault_[0];

                auto const vaultDeltaAssets = deltaAssets(afterVault.pseudoId);

                if (!vaultDeltaAssets)
                {
                    JLOG(j.fatal()) << "Invariant failed: withdrawal must "
                                       "change vault balance";
                    return false;  // That's all we can do
                }

                if (*vaultDeltaAssets >= zero)
                {
                    JLOG(j.fatal()) << "Invariant failed: withdrawal must "
                                       "decrease vault balance";
                    result = false;
                }

                // Any payments (including withdrawal) going to the issuer
                // do not change their balance, but destroy funds instead.
                bool const issuerWithdrawal = [&]() -> bool {
                    if (vaultAsset.native())
                        return false;
                    auto const destination =
                        tx[~sfDestination].value_or(tx[sfAccount]);
                    return destination == vaultAsset.getIssuer();
                }();

                if (!issuerWithdrawal)
                {
                    auto const accountDeltaAssets = deltaAssetsTxAccount();
                    auto const otherAccountDelta =
                        [&]() -> std::optional<Number> {
                        if (auto const destination = tx[~sfDestination];
                            destination && *destination != tx[sfAccount])
                            return deltaAssets(*destination);
                        return std::nullopt;
                    }();

                    if (accountDeltaAssets.has_value() ==
                        otherAccountDelta.has_value())
                    {
                        JLOG(j.fatal()) <<  //
                            "Invariant failed: withdrawal must change one "
                            "destination balance";
                        return false;
                    }

                    auto const destinationDelta =  //
                        accountDeltaAssets ? *accountDeltaAssets
                                           : *otherAccountDelta;

                    if (destinationDelta <= zero)
                    {
                        JLOG(j.fatal()) <<  //
                            "Invariant failed: withdrawal must increase "
                            "destination balance";
                        result = false;
                    }

                    if (*vaultDeltaAssets * -1 != destinationDelta)
                    {
                        JLOG(j.fatal()) <<  //
                            "Invariant failed: withdrawal must change vault "
                            "and destination balance by equal amount";
                        result = false;
                    }
                }

                auto const accountDeltaShares = deltaShares(tx[sfAccount]);
                if (!accountDeltaShares)
                {
                    JLOG(j.fatal()) <<  //
                        "Invariant failed: withdrawal must change depositor "
                        "shares";
                    return false;
                }

                if (*accountDeltaShares >= zero)
                {
                    JLOG(j.fatal()) <<  //
                        "Invariant failed: withdrawal must decrease depositor "
                        "shares";
                    result = false;
                }

                auto const vaultDeltaShares = deltaShares(afterVault.pseudoId);
                if (!vaultDeltaShares || *vaultDeltaShares == zero)
                {
                    JLOG(j.fatal()) <<  //
                        "Invariant failed: withdrawal must change vault shares";
                    return false;  // That's all we can do
                }

                if (*vaultDeltaShares * -1 != *accountDeltaShares)
                {
                    JLOG(j.fatal()) <<  //
                        "Invariant failed: withdrawal must change depositor "
                        "and vault shares by equal amount";
                    result = false;
                }

                // Note, vaultBalance is negative (see check above)
                if (beforeVault.assetsTotal + *vaultDeltaAssets !=
                    afterVault.assetsTotal)
                {
                    JLOG(j.fatal()) << "Invariant failed: withdrawal and "
                                       "assets outstanding must add up";
                    result = false;
                }

                if (beforeVault.assetsAvailable + *vaultDeltaAssets !=
                    afterVault.assetsAvailable)
                {
                    JLOG(j.fatal()) << "Invariant failed: withdrawal and "
                                       "assets available must add up";
                    result = false;
                }

                return result;
            }
            case ttVAULT_CLAWBACK: {
                bool result = true;

                XRPL_ASSERT(
                    !beforeVault_.empty(),
                    "ripple::ValidVault::finalize : clawback updated a vault");
                auto const& beforeVault = beforeVault_[0];

                if (vaultAsset.native() ||
                    vaultAsset.getIssuer() != tx[sfAccount])
                {
                    JLOG(j.fatal()) <<  //
                        "Invariant failed: clawback may only be performed by "
                        "the asset issuer";
                    return false;  // That's all we can do
                }

                auto const vaultDeltaAssets = deltaAssets(afterVault.pseudoId);

                if (!vaultDeltaAssets)
                {
                    JLOG(j.fatal()) <<  //
                        "Invariant failed: clawback must change vault balance";
                    return false;  // That's all we can do
                }

                if (*vaultDeltaAssets >= zero)
                {
                    JLOG(j.fatal()) <<  //
                        "Invariant failed: clawback must decrease vault "
                        "balance";
                    result = false;
                }

                auto const accountDeltaShares = deltaShares(tx[sfHolder]);
                if (!accountDeltaShares)
                {
                    JLOG(j.fatal()) <<  //
                        "Invariant failed: clawback must change holder shares";
                    return false;  // That's all we can do
                }

                if (*accountDeltaShares >= zero)
                {
                    JLOG(j.fatal()) <<  //
                        "Invariant failed: clawback must decrease holder "
                        "shares";
                    result = false;
                }

                auto const vaultDeltaShares = deltaShares(afterVault.pseudoId);
                if (!vaultDeltaShares || *vaultDeltaShares == zero)
                {
                    JLOG(j.fatal()) <<  //
                        "Invariant failed: clawback must change vault shares";
                    return false;  // That's all we can do
                }

                if (*vaultDeltaShares * -1 != *accountDeltaShares)
                {
                    JLOG(j.fatal()) <<  //
                        "Invariant failed: clawback must change holder and "
                        "vault shares by equal amount";
                    result = false;
                }

                if (beforeVault.assetsTotal + *vaultDeltaAssets !=
                    afterVault.assetsTotal)
                {
                    JLOG(j.fatal()) <<  //
                        "Invariant failed: clawback and assets outstanding "
                        "must add up";
                    result = false;
                }

                if (beforeVault.assetsAvailable + *vaultDeltaAssets !=
                    afterVault.assetsAvailable)
                {
                    JLOG(j.fatal()) <<  //
                        "Invariant failed: clawback and assets available must "
                        "add up";
                    result = false;
                }

                return result;
            }

            default:
                // LCOV_EXCL_START
                UNREACHABLE(
                    "ripple::ValidVault::finalize : unknown transaction type");
                return false;
                // LCOV_EXCL_STOP
        }
    }();

    if (!result)
    {
        // The comment at the top of this file starting with "assert(enforce)"
        // explains this assert.
        XRPL_ASSERT(enforce, "ripple::ValidVault::finalize : vault invariants");
        return !enforce;
    }

    return true;
}

}  // namespace ripple
