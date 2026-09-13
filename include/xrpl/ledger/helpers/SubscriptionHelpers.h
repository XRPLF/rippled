#pragma once

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/MPTokenHelpers.h>
#include <xrpl/ledger/helpers/RippleStateHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Concepts.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/UintTypes.h>

#include <cstdint>

namespace xrpl {

template <ValidIssueType T>
TER
canTransferTokenHelper(
    ReadView const& view,
    AccountID const& account,
    AccountID const& dest,
    STAmount const& amount,
    beast::Journal const& j);

template <>
inline TER
canTransferTokenHelper<Issue>(
    ReadView const& view,
    AccountID const& account,
    AccountID const& dest,
    STAmount const& amount,
    beast::Journal const& j)
{
    AccountID issuer = amount.getIssuer();
    if (issuer == account)
    {
        JLOG(j.trace()) << "canTransferTokenHelper: Issuer is the same as the account.";
        return tesSUCCESS;
    }

    // If the issuer does not exist, return tecNO_ISSUER
    auto const sleIssuer = view.read(keylet::account(issuer));
    if (!sleIssuer)
    {
        JLOG(j.trace()) << "canTransferTokenHelper: Issuer does not exist.";
        return tecNO_ISSUER;
    }

    // If the account does not have a trustline to the issuer, return tecNO_LINE
    auto const sleRippleState =
        view.read(keylet::trustLine(account, issuer, amount.get<Issue>().currency));
    if (!sleRippleState)
    {
        JLOG(j.trace()) << "canTransferTokenHelper: Trust line does not exist.";
        return tecNO_LINE;
    }

    STAmount const balance = (*sleRippleState)[sfBalance];

    // If balance is positive, issuer must have higher address than account
    if (balance > beast::kZero && issuer < account)
    {
        JLOG(j.trace()) << "canTransferTokenHelper: Invalid trust line state.";
        return tecNO_PERMISSION;
    }

    // If balance is negative, issuer must have lower address than account
    if (balance < beast::kZero && issuer > account)
    {
        JLOG(j.trace()) << "canTransferTokenHelper: Invalid trust line state.";
        return tecNO_PERMISSION;
    }

    // If the issuer has requireAuth set, check if the account is authorized
    if (auto const ter = requireAuth(view, amount.get<Issue>(), account); ter != tesSUCCESS)
    {
        JLOG(j.trace()) << "canTransferTokenHelper: Account is not authorized";
        return ter;
    }

    // If the issuer has requireAuth set, check if the destination is authorized
    if (auto const ter = requireAuth(view, amount.get<Issue>(), dest); ter != tesSUCCESS)
    {
        JLOG(j.trace()) << "canTransferTokenHelper: Destination is not authorized.";
        return ter;
    }

    // If the issuer has frozen the account, return tecFROZEN
    if (isFrozen(view, account, amount.get<Issue>()) ||
        isDeepFrozen(view, account, amount.get<Issue>().currency, amount.get<Issue>().account))
    {
        JLOG(j.trace()) << "canTransferTokenHelper: Account is frozen.";
        return tecFROZEN;
    }

    // If the issuer has frozen the destination, return tecFROZEN
    if (isFrozen(view, dest, amount.get<Issue>()) ||
        isDeepFrozen(view, dest, amount.get<Issue>().currency, amount.get<Issue>().account))
    {
        JLOG(j.trace()) << "canTransferTokenHelper: Destination is frozen.";
        return tecFROZEN;
    }

    STAmount const spendableAmount = accountHolds(
        view, account, amount.get<Issue>().currency, issuer, FreezeHandling::IgnoreFreeze, j);

    // If the balance is less than or equal to 0, return
    // tecINSUFFICIENT_FUNDS
    if (spendableAmount <= beast::kZero)
    {
        JLOG(j.trace()) << "canTransferTokenHelper: Spendable amount is less "
                           "than or equal to 0.";
        return tecINSUFFICIENT_FUNDS;
    }

    // If the spendable amount is less than the amount, return
    // tecINSUFFICIENT_FUNDS
    if (spendableAmount < amount)
    {
        JLOG(j.trace()) << "canTransferTokenHelper: Spendable amount is less "
                           "than the amount.";
        return tecINSUFFICIENT_FUNDS;
    }

    // If the amount is not addable to the balance, return tecPRECISION_LOSS
    if (!canAdd(spendableAmount, amount))
        return tecPRECISION_LOSS;

    return tesSUCCESS;
}

template <>
inline TER
canTransferTokenHelper<MPTIssue>(
    ReadView const& view,
    AccountID const& account,
    AccountID const& dest,
    STAmount const& amount,
    beast::Journal const& j)
{
    AccountID issuer = amount.getIssuer();
    if (issuer == account)
    {
        JLOG(j.trace()) << "canTransferTokenHelper: Issuer is the same as the account.";
        return tesSUCCESS;
    }

    // If the mpt does not exist, return tecOBJECT_NOT_FOUND
    auto const issuanceKey = keylet::mptokenIssuance(amount.get<MPTIssue>().getMptID());
    auto const sleIssuance = view.read(issuanceKey);
    if (!sleIssuance)
    {
        JLOG(j.trace()) << "canTransferTokenHelper: MPT issuance does not exist.";
        return tecOBJECT_NOT_FOUND;
    }

    // If the issuer is not the same as the issuer of the mpt, return
    // tecNO_PERMISSION
    if (sleIssuance->getAccountID(sfIssuer) != issuer)
    {
        JLOG(j.trace()) << "canTransferTokenHelper: Issuer is not the same as "
                           "the issuer of the MPT.";
        return tecNO_PERMISSION;
    }

    // If the account does not have the mpt, return tecOBJECT_NOT_FOUND
    if (!view.exists(keylet::mptoken(issuanceKey.key, account)))
    {
        JLOG(j.trace()) << "canTransferTokenHelper: Account does not have the MPT.";
        return tecOBJECT_NOT_FOUND;
    }

    // If the issuer has requireAuth set, check if the account is
    // authorized
    auto const& mptIssue = amount.get<MPTIssue>();
    if (auto const ter = requireAuth(view, mptIssue, account, AuthType::WeakAuth);
        ter != tesSUCCESS)
    {
        JLOG(j.trace()) << "canTransferTokenHelper: Account is not authorized.";
        return ter;
    }

    // If the issuer has requireAuth set, check if the destination is
    // authorized
    if (auto const ter = requireAuth(view, mptIssue, dest, AuthType::WeakAuth); ter != tesSUCCESS)
    {
        JLOG(j.trace()) << "canTransferTokenHelper: Destination is not authorized.";
        return ter;
    }

    // If the issuer has locked the account, return tecLOCKED
    if (isFrozen(view, account, mptIssue))
    {
        JLOG(j.trace()) << "canTransferTokenHelper: Account is locked.";
        return tecLOCKED;
    }

    // If the issuer has locked the destination, return tecLOCKED
    if (isFrozen(view, dest, mptIssue))
    {
        JLOG(j.trace()) << "canTransferTokenHelper: Destination is locked.";
        return tecLOCKED;
    }

    // If the mpt cannot be transferred, return tecNO_AUTH
    if (auto const ter = canTransfer(view, mptIssue, account, dest); ter != tesSUCCESS)
    {
        JLOG(j.trace()) << "canTransferTokenHelper: MPT cannot be transferred.";
        return ter;
    }

    STAmount const spendableAmount = accountHolds(
        view,
        account,
        amount.get<MPTIssue>(),
        FreezeHandling::IgnoreFreeze,
        AuthHandling::IgnoreAuth,
        j);

    // If the balance is less than or equal to 0, return
    // tecINSUFFICIENT_FUNDS
    if (spendableAmount <= beast::kZero)
    {
        JLOG(j.trace()) << "canTransferTokenHelper: Spendable amount is less "
                           "than or equal to 0.";
        return tecINSUFFICIENT_FUNDS;
    }

    // If the spendable amount is less than the amount, return
    // tecINSUFFICIENT_FUNDS
    if (spendableAmount < amount)
    {
        JLOG(j.trace()) << "canTransferTokenHelper: Spendable amount is less "
                           "than the amount.";
        return tecINSUFFICIENT_FUNDS;
    }

    // If the amount is not addable to the balance, return tecPRECISION_LOSS
    if (!canAdd(spendableAmount, amount))
        return tecPRECISION_LOSS;

    return tesSUCCESS;
}

template <ValidIssueType T>
TER
doTransferTokenHelper(
    ApplyView& view,
    SLE::ref sleDest,
    STAmount const& xrpBalance,
    STAmount const& amount,
    AccountID const& issuer,
    AccountID const& sender,
    AccountID const& receiver,
    bool createAsset,
    beast::Journal journal);

template <>
inline TER
doTransferTokenHelper<Issue>(
    ApplyView& view,
    SLE::ref sleDest,
    STAmount const& xrpBalance,
    STAmount const& amount,
    AccountID const& issuer,
    AccountID const& sender,
    AccountID const& receiver,
    bool createAsset,
    beast::Journal journal)
{
    Keylet const trustLineKey = keylet::trustLine(receiver, amount.get<Issue>());
    bool const recvLow = issuer > receiver;

    // Review Note: We could remove this and just say to use batch to auth the
    // token first
    if (!view.exists(trustLineKey) && createAsset && issuer != receiver)
    {
        // Can the account cover the trust line's reserve?
        if (xrpBalance < accountReserve(view, sleDest, journal, {.ownerCountDelta = 1}))
        {
            JLOG(journal.trace()) << "doTransferTokenHelper: Trust line does not exist. "
                                     "Insufficent reserve to create line.";

            return tecNO_LINE_INSUF_RESERVE;
        }

        Currency const currency = amount.get<Issue>().currency;
        STAmount initialBalance(amount.get<Issue>());
        initialBalance.get<Issue>().account = noAccount();

        // clang-format off
        if (TER const ter = trustCreate(
                view,                            // payment sandbox
                recvLow,                        // is dest low?
                issuer,                         // source
                receiver,                           // destination
                trustLineKey.key,               // ledger index
                sleDest,                        // Account to add to
                false,                          // authorize account
                (sleDest->getFlags() & lsfDefaultRipple) == 0,
                false,                          // freeze trust line
                false,                          // deep freeze trust line
                initialBalance,                 // zero initial balance
                Issue(currency, receiver),   // limit of zero
                0,                              // quality in
                0,                              // quality out
                SLE::pointer(),                 // sponsor
                journal);                       // journal
            !isTesSuccess(ter))
        {
            JLOG(journal.trace()) << "doTransferTokenHelper: Failed to create trust line: " << transToken(ter);
            return ter;
        }
        // clang-format on

        view.update(sleDest);
    }

    if (!view.exists(trustLineKey) && issuer != receiver)
        return tecNO_LINE;

    auto const ter =
        accountSend(view, sender, receiver, amount, journal, SLE::pointer(), WaiveTransferFee::No);
    if (ter != tesSUCCESS)
    {
        JLOG(journal.trace()) << "doTransferTokenHelper: Failed to send token: " << transToken(ter);
        return ter;  // LCOV_EXCL_LINE
    }

    return tesSUCCESS;
}

template <>
inline TER
doTransferTokenHelper<MPTIssue>(
    ApplyView& view,
    SLE::ref sleDest,
    STAmount const& xrpBalance,
    STAmount const& amount,
    AccountID const& issuer,
    AccountID const& sender,
    AccountID const& receiver,
    bool createAsset,
    beast::Journal journal)
{
    auto const mptID = amount.get<MPTIssue>().getMptID();
    auto const issuanceKey = keylet::mptokenIssuance(mptID);
    if (!view.exists(keylet::mptoken(issuanceKey.key, receiver)) && createAsset &&
        issuer != receiver)
    {
        if (xrpBalance < accountReserve(view, sleDest, journal, {.ownerCountDelta = 1}))
        {
            JLOG(journal.trace()) << "doTransferTokenHelper: MPT does not exist. "
                                     "Insufficent reserve to create MPT.";
            return tecINSUFFICIENT_RESERVE;
        }

        if (auto const ter = createMPToken(view, mptID, receiver, SLE::pointer(), 0);
            !isTesSuccess(ter))
        {
            JLOG(journal.trace()) << "doTransferTokenHelper: Failed to create MPT: "
                                  << transToken(ter);
            return ter;
        }

        // Update owner count.
        increaseOwnerCount(view, sleDest, SLE::pointer(), 1, journal);
    }

    if (issuer != receiver && !view.exists(keylet::mptoken(issuanceKey.key, receiver)))
    {
        JLOG(journal.trace()) << "doTransferTokenHelper: MPT does not exist.";
        return tecNO_PERMISSION;
    }

    auto const ter =
        accountSend(view, sender, receiver, amount, journal, SLE::pointer(), WaiveTransferFee::No);
    if (ter != tesSUCCESS)
    {
        JLOG(journal.trace()) << "doTransferTokenHelper: Failed to send MPT: " << transToken(ter);
        return ter;  // LCOV_EXCL_LINE
    }

    return tesSUCCESS;
}

// Remove a subscription from both owner directories, release the owner's
// reserve, and erase the object. Shared by SubscriptionCancel and the
// single-use claim path so the two never diverge.
inline TER
deleteSubscription(ApplyView& view, SLE::ref sleSub, beast::Journal journal)
{
    AccountID const account{sleSub->getAccountID(sfAccount)};
    AccountID const dstAcct{sleSub->getAccountID(sfDestination)};

    std::uint64_t const ownerPage{(*sleSub)[sfOwnerNode]};
    if (!view.dirRemove(keylet::ownerDir(account), ownerPage, sleSub->key(), true))
    {
        JLOG(journal.fatal()) << "deleteSubscription: Unable to delete from source.";
        return tefBAD_LEDGER;
    }

    std::uint64_t const destPage{(*sleSub)[sfDestinationNode]};
    if (!view.dirRemove(keylet::ownerDir(dstAcct), destPage, sleSub->key(), true))
    {
        JLOG(journal.fatal()) << "deleteSubscription: Unable to delete from destination.";
        return tefBAD_LEDGER;
    }

    auto const sleSrc = view.peek(keylet::account(account));
    decreaseOwnerCount(view, sleSrc, SLE::pointer(), 1, journal);
    view.erase(sleSub);
    return tesSUCCESS;
}

}  // namespace xrpl
