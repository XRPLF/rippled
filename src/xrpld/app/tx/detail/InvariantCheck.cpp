//------------------------------------------------------------------------------
/*
  This file is part of rippled: https://github.com/ripple/rippled
  Copyright (c) 2012-2016 Ripple Labs Inc.

  Permission to use, copy, modify, and/or distribute this software for any
  purpose  with  or without fee is hereby granted, provided that the above
  copyright notice and this permission notice appear in all copies.

  THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
  WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
  MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
  ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
  WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
  ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <xrpld/app/misc/CredentialHelpers.h>
#include <xrpld/app/tx/detail/InvariantCheck.h>
#include <xrpld/app/tx/detail/NFTokenUtils.h>
#include <xrpld/app/tx/detail/PermissionedDomainSet.h>
#include <xrpld/ledger/ReadView.h>
#include <xrpld/ledger/View.h>

#include <xrpl/basics/Log.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/FeeUnits.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/SystemParameters.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/nftPageMask.h>

namespace ripple {

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
                if (!isDelete)
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
        if (!amount.native())
            return true;

        if (amount.xrp() <= XRPAmount{0})
            return true;

        if (amount.xrp() >= INITIAL_XRP)
            return true;

        return false;
    };

    if (before && before->getType() == ltESCROW)
        bad_ |= isBad((*before)[sfAmount]);

    if (after && after->getType() == ltESCROW)
        bad_ |= isBad((*after)[sfAmount]);
}

bool
NoZeroEscrow::finalize(
    STTx const&,
    TER const,
    XRPAmount const,
    ReadView const&,
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
    if ((tx.getTxnType() == ttACCOUNT_DELETE ||
         tx.getTxnType() == ttAMM_DELETE) &&
        result == tesSUCCESS)
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
    if ((tx.getTxnType() == ttAMM_WITHDRAW ||
         tx.getTxnType() == ttAMM_CLAWBACK) &&
        result == tesSUCCESS && accountsDeleted_ == 1)
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
        view.rules().enabled(featureInvariantsV1_1);

    auto const objectExists = [&view, enforce, &j](auto const& keylet) {
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

        // Keys directly stored in the AccountRoot object
        if (auto const ammKey = accountSLE->at(~sfAMMID))
        {
            if (objectExists(keylet::amm(*ammKey)) && enforce)
                return false;
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
        switch (after->getType())
        {
            case ltACCOUNT_ROOT:
            case ltDIR_NODE:
            case ltRIPPLE_STATE:
            case ltTICKET:
            case ltSIGNER_LIST:
            case ltOFFER:
            case ltLEDGER_HASHES:
            case ltAMENDMENTS:
            case ltFEE_SETTINGS:
            case ltESCROW:
            case ltPAYCHAN:
            case ltCHECK:
            case ltDEPOSIT_PREAUTH:
            case ltNEGATIVE_UNL:
            case ltNFTOKEN_PAGE:
            case ltNFTOKEN_OFFER:
            case ltAMM:
            case ltBRIDGE:
            case ltXCHAIN_OWNED_CLAIM_ID:
            case ltXCHAIN_OWNED_CREATE_ACCOUNT_CLAIM_ID:
            case ltDID:
            case ltORACLE:
            case ltMPTOKEN_ISSUANCE:
            case ltMPTOKEN:
            case ltCREDENTIAL:
            case ltPERMISSIONED_DOMAIN:
                break;
            default:
                invalidTypeAdded_ = true;
                break;
        }
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
    if ((!isAMMLine || globalFreeze) && tx.getTxnType() == ttAMM_CLAWBACK)
    {
        JLOG(j.debug()) << "Invariant check allowing funds to be moved "
                        << (change.balanceChangeSign > 0 ? "to" : "from")
                        << " a frozen trustline for AMMClawback "
                        << tx.getTransactionID();
        return true;
    }

    JLOG(j.fatal()) << "Invariant failed: Attempting to move frozen funds for "
                    << tx.getTransactionID();
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
    if ((tx.getTxnType() == ttPAYMENT || tx.getTxnType() == ttAMM_CREATE ||
         tx.getTxnType() == ttXCHAIN_ADD_CLAIM_ATTESTATION ||
         tx.getTxnType() == ttXCHAIN_ADD_ACCOUNT_CREATE_ATTESTATION) &&
        result == tesSUCCESS)
    {
        std::uint32_t const startingSeq{
            view.rules().enabled(featureDeletableAccounts) ? view.seq() : 1};

        if (accountSeq_ != startingSeq)
        {
            JLOG(j.fatal()) << "Invariant failed: account created with "
                               "wrong starting sequence number";
            return false;
        }
        return true;
    }

    JLOG(j.fatal()) << "Invariant failed: account root created "
                       "by a non-Payment, by an unsuccessful transaction, "
                       "or by AMM";
    return false;
}

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
    if (TxType const txType = tx.getTxnType();
        txType != ttNFTOKEN_MINT && txType != ttNFTOKEN_BURN)
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
    ReadView const& _view,
    beast::Journal const& j)
{
    if (result == tesSUCCESS)
    {
        if (tx.getTxnType() == ttMPTOKEN_ISSUANCE_CREATE)
        {
            if (mptIssuancesCreated_ == 0)
            {
                JLOG(j.fatal()) << "Invariant failed: MPT issuance creation "
                                   "succeeded without creating a MPT issuance";
            }
            else if (mptIssuancesDeleted_ != 0)
            {
                JLOG(j.fatal()) << "Invariant failed: MPT issuance creation "
                                   "succeeded while removing MPT issuances";
            }
            else if (mptIssuancesCreated_ > 1)
            {
                JLOG(j.fatal()) << "Invariant failed: MPT issuance creation "
                                   "succeeded but created multiple issuances";
            }

            return mptIssuancesCreated_ == 1 && mptIssuancesDeleted_ == 0;
        }

        if (tx.getTxnType() == ttMPTOKEN_ISSUANCE_DESTROY)
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

        if (tx.getTxnType() == ttMPTOKEN_AUTHORIZE)
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
                !submittedByIssuer &&
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

        if (tx.getTxnType() == ttMPTOKEN_ISSUANCE_SET)
        {
            if (mptIssuancesDeleted_ > 0)
            {
                JLOG(j.fatal()) << "Invariant failed: MPT issuance set "
                                   "succeeded while removing MPT issuances";
            }
            else if (mptIssuancesCreated_ > 0)
            {
                JLOG(j.fatal()) << "Invariant failed: MPT issuance set "
                                   "succeeded while creating MPT issuances";
            }
            else if (mptokensDeleted_ > 0)
            {
                JLOG(j.fatal()) << "Invariant failed: MPT issuance set "
                                   "succeeded while removing MPTokens";
            }
            else if (mptokensCreated_ > 0)
            {
                JLOG(j.fatal()) << "Invariant failed: MPT issuance set "
                                   "succeeded while creating MPTokens";
            }

            return mptIssuancesCreated_ == 0 && mptIssuancesDeleted_ == 0 &&
                mptokensCreated_ == 0 && mptokensDeleted_ == 0;
        }
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

}  // namespace ripple
