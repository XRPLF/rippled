#include <xrpl/tx/invariants/InvariantCheck.h>
//
#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/View.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/SystemParameters.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/tx/invariants/InvariantCheckPrivilege.h>

#include <cstdint>
#include <optional>

namespace xrpl {

#pragma push_macro("TRANSACTION")
#undef TRANSACTION

#define TRANSACTION(tag, value, name, delegable, amendment, privileges, ...) \
    case tag: {                                                              \
        return (privileges) & priv;                                          \
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
        JLOG(j.fatal()) << "Invariant failed: fee paid was negative: " << fee.drops();
        return false;
    }

    // We should never charge a fee that's greater than or equal to the
    // entire XRP supply.
    if (fee >= INITIAL_XRP)
    {
        JLOG(j.fatal()) << "Invariant failed: fee paid exceeds system limit: " << fee.drops();
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
                drops_ -= ((*before)[sfAmount] - (*before)[sfBalance]).xrp().drops();
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
                    drops_ += ((*after)[sfAmount] - (*after)[sfBalance]).xrp().drops();
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
        JLOG(j.fatal()) << "Invariant failed: XRP net change was positive: " << drops_;
        return false;
    }

    // The negative of the net change should be equal to actual fee charged.
    if (-drops_ != fee.drops())
    {
        JLOG(j.fatal()) << "Invariant failed: XRP net change of " << drops_ << " doesn't match fee "
                        << fee.drops();
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
    if (hasPrivilege(tx, mayDeleteAcct) && result == tesSUCCESS && accountsDeleted_ == 1)
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
    std::shared_ptr<SLE const> const& after)
{
    if (isDelete && before && before->getType() == ltACCOUNT_ROOT)
        accountsDeleted_.emplace_back(before, after);
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
    [[maybe_unused]] bool const enforce = view.rules().enabled(featureInvariantsV1_1) ||
        view.rules().enabled(featureSingleAssetVault) ||
        view.rules().enabled(featureLendingProtocol);

    auto const objectExists = [&view, enforce, &j](auto const& keylet) {
        (void)enforce;
        if (auto const sle = view.read(keylet))
        {
            // Finding the object is bad
            auto const typeName = [&sle]() {
                auto item = LedgerFormats::getInstance().findByType(sle->getType());

                if (item != nullptr)
                    return item->getName();
                return std::to_string(sle->getType());
            }();

            JLOG(j.fatal()) << "Invariant failed: account deletion left behind a " << typeName
                            << " object";
            // The comment above starting with "assert(enforce)" explains this
            // assert.
            XRPL_ASSERT(
                enforce,
                "xrpl::AccountRootsDeletedClean::finalize::objectExists : "
                "account deletion left no objects behind");
            return true;
        }
        return false;
    };

    for (auto const& [before, after] : accountsDeleted_)
    {
        auto const accountID = before->getAccountID(sfAccount);
        // An account should not be deleted with a balance
        if (after->at(sfBalance) != beast::zero)
        {
            JLOG(j.fatal()) << "Invariant failed: account deletion left "
                               "behind a non-zero balance";
            XRPL_ASSERT(
                enforce,
                "xrpl::AccountRootsDeletedClean::finalize : "
                "deleted account has zero balance");
            if (enforce)
                return false;
        }
        // An account should not be deleted with a non-zero owner count
        if (after->at(sfOwnerCount) != 0)
        {
            JLOG(j.fatal()) << "Invariant failed: account deletion left "
                               "behind a non-zero owner count";
            XRPL_ASSERT(
                enforce,
                "xrpl::AccountRootsDeletedClean::finalize : "
                "deleted account has zero owner count");
            if (enforce)
                return false;
        }
        // Simple types
        for (auto const& [keyletfunc, _, __] : directAccountKeylets)
        {
            if (objectExists(std::invoke(keyletfunc, accountID)) && enforce)
                return false;
        }

        {
            // NFT pages. nftpage_min and nftpage_max were already explicitly
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
            if (before->isFieldPresent(*field))
            {
                auto const key = before->getFieldH256(*field);
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
        xrpTrustLine_ = after->getFieldAmount(sfLowLimit).issue() == xrpIssue() ||
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

        deepFreezeWithoutFreeze_ = (lowDeepFreeze && !lowFreeze) || (highDeepFreeze && !highFreeze);
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
            (pseudoAccount_ &&
             (view.rules().enabled(featureSingleAssetVault) ||
              view.rules().enabled(featureLendingProtocol)));

        if (pseudoAccount && !hasPrivilege(tx, createPseudoAcct))
        {
            JLOG(j.fatal()) << "Invariant failed: pseudo-account created by a "
                               "wrong transaction type";
            return false;
        }

        std::uint32_t const startingSeq = pseudoAccount ? 0 : view.seq();

        if (accountSeq_ != startingSeq)
        {
            JLOG(j.fatal()) << "Invariant failed: account created with "
                               "wrong starting sequence number";
            return false;
        }

        if (pseudoAccount)
        {
            std::uint32_t const expected = (lsfDisableMaster | lsfDefaultRipple | lsfDepositAuth);
            if (flags_ != expected)
            {
                JLOG(j.fatal()) << "Invariant failed: pseudo-account created with "
                                   "wrong flags";
                return false;
            }
        }

        return true;
    }

    JLOG(j.fatal()) << "Invariant failed: account root created illegally";
    return false;
}  // namespace xrpl

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
            JLOG(j.fatal()) << "Invariant failed: more than one trustline changed.";
            return false;
        }

        if (mptokensChanged > 1)
        {
            JLOG(j.fatal()) << "Invariant failed: more than one mptokens changed.";
            return false;
        }

        if (trustlinesChanged == 1)
        {
            AccountID const issuer = tx.getAccountID(sfAccount);
            STAmount const& amount = tx.getFieldAmount(sfAmount);
            AccountID const& holder = amount.getIssuer();
            STAmount const holderBalance =
                accountHolds(view, holder, amount.getCurrency(), issuer, fhIGNORE_FREEZE, j);

            if (holderBalance.signum() < 0)
            {
                JLOG(j.fatal()) << "Invariant failed: trustline balance is negative";
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
                std::vector<SField const*> const& fields = getPseudoAccountFields();

                auto const numFields =
                    std::count_if(fields.begin(), fields.end(), [&after](SField const* sf) -> bool {
                        return after->isFieldPresent(*sf);
                    });
                if (numFields != 1)
                {
                    std::stringstream error;
                    error << "pseudo-account has " << numFields << " pseudo-account fields set";
                    errors_.emplace_back(error.str());
                }
            }
            if (before && before->at(sfSequence) != after->at(sfSequence))
            {
                errors_.emplace_back("pseudo-account sequence changed");
            }
            if (!after->isFlag(lsfDisableMaster | lsfDefaultRipple | lsfDepositAuth))
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
    XRPL_ASSERT(
        errors_.empty() || enforce,
        "xrpl::ValidPseudoAccounts::finalize : no bad "
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
NoModifiedUnmodifiableFields::visitEntry(
    bool isDelete,
    std::shared_ptr<SLE const> const& before,
    std::shared_ptr<SLE const> const& after)
{
    if (isDelete || !before)
        // Creation and deletion are ignored
        return;

    changedEntries_.emplace(before, after);
}

bool
NoModifiedUnmodifiableFields::finalize(
    STTx const& tx,
    TER const,
    XRPAmount const,
    ReadView const& view,
    beast::Journal const& j)
{
    static auto const fieldChanged = [](auto const& before, auto const& after, auto const& field) {
        bool const beforeField = before->isFieldPresent(field);
        bool const afterField = after->isFieldPresent(field);
        return beforeField != afterField || (afterField && before->at(field) != after->at(field));
    };
    for (auto const& slePair : changedEntries_)
    {
        auto const& before = slePair.first;
        auto const& after = slePair.second;
        auto const type = after->getType();
        bool bad = false;
        [[maybe_unused]] bool enforce = false;
        switch (type)
        {
            case ltLOAN_BROKER:
                /*
                 * We check this invariant regardless of lending protocol
                 * amendment status, allowing for detection and logging of
                 * potential issues even when the amendment is disabled.
                 */
                enforce = view.rules().enabled(featureLendingProtocol);
                bad = fieldChanged(before, after, sfLedgerEntryType) ||
                    fieldChanged(before, after, sfLedgerIndex) ||
                    fieldChanged(before, after, sfSequence) ||
                    fieldChanged(before, after, sfOwnerNode) ||
                    fieldChanged(before, after, sfVaultNode) ||
                    fieldChanged(before, after, sfVaultID) ||
                    fieldChanged(before, after, sfAccount) ||
                    fieldChanged(before, after, sfOwner) ||
                    fieldChanged(before, after, sfManagementFeeRate) ||
                    fieldChanged(before, after, sfCoverRateMinimum) ||
                    fieldChanged(before, after, sfCoverRateLiquidation);
                break;
            case ltLOAN:
                /*
                 * We check this invariant regardless of lending protocol
                 * amendment status, allowing for detection and logging of
                 * potential issues even when the amendment is disabled.
                 */
                enforce = view.rules().enabled(featureLendingProtocol);
                bad = fieldChanged(before, after, sfLedgerEntryType) ||
                    fieldChanged(before, after, sfLedgerIndex) ||
                    fieldChanged(before, after, sfSequence) ||
                    fieldChanged(before, after, sfOwnerNode) ||
                    fieldChanged(before, after, sfLoanBrokerNode) ||
                    fieldChanged(before, after, sfLoanBrokerID) ||
                    fieldChanged(before, after, sfBorrower) ||
                    fieldChanged(before, after, sfLoanOriginationFee) ||
                    fieldChanged(before, after, sfLoanServiceFee) ||
                    fieldChanged(before, after, sfLatePaymentFee) ||
                    fieldChanged(before, after, sfClosePaymentFee) ||
                    fieldChanged(before, after, sfOverpaymentFee) ||
                    fieldChanged(before, after, sfInterestRate) ||
                    fieldChanged(before, after, sfLateInterestRate) ||
                    fieldChanged(before, after, sfCloseInterestRate) ||
                    fieldChanged(before, after, sfOverpaymentInterestRate) ||
                    fieldChanged(before, after, sfStartDate) ||
                    fieldChanged(before, after, sfPaymentInterval) ||
                    fieldChanged(before, after, sfGracePeriod) ||
                    fieldChanged(before, after, sfLoanScale);
                break;
            default:
                /*
                 * We check this invariant regardless of lending protocol
                 * amendment status, allowing for detection and logging of
                 * potential issues even when the amendment is disabled.
                 *
                 * We use the lending protocol as a gate, even though
                 * all transactions are affected because that's when it
                 * was added.
                 */
                enforce = view.rules().enabled(featureLendingProtocol);
                bad = fieldChanged(before, after, sfLedgerEntryType) ||
                    fieldChanged(before, after, sfLedgerIndex);
        }
        XRPL_ASSERT(
            !bad || enforce,
            "xrpl::NoModifiedUnmodifiableFields::finalize : no bad "
            "changes or enforce invariant");
        if (bad)
        {
            JLOG(j.fatal()) << "Invariant failed: changed an unchangeable field for "
                            << tx.getTransactionID();
            if (enforce)
                return false;
        }
    }
    return true;
}

}  // namespace xrpl
