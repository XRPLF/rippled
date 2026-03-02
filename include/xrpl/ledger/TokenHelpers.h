#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/MPTokenHelpers.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/RippleStateHelpers.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Rate.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/TER.h>

#include <initializer_list>
#include <vector>

namespace xrpl {

//------------------------------------------------------------------------------
//
// Enums for token handling
//
//------------------------------------------------------------------------------

/** Controls the treatment of frozen account balances */
enum FreezeHandling { fhIGNORE_FREEZE, fhZERO_IF_FROZEN };

/** Controls the treatment of unauthorized MPT balances */
enum AuthHandling { ahIGNORE_AUTH, ahZERO_IF_UNAUTHORIZED };

/** Controls whether to include the account's full spendable balance */
enum SpendableHandling { shSIMPLE_BALANCE, shFULL_BALANCE };

enum class WaiveTransferFee : bool { No = false, Yes };

//------------------------------------------------------------------------------
//
// Freeze checking (Asset-based dispatchers)
//
//------------------------------------------------------------------------------

[[nodiscard]] bool
isGlobalFrozen(ReadView const& view, Asset const& asset);

[[nodiscard]] inline bool
isIndividualFrozen(ReadView const& view, AccountID const& account, Asset const& asset)
{
    return std::visit(
        [&](auto const& issue) { return isIndividualFrozen(view, account, issue); }, asset.value());
}

/**
 *   isFrozen check is recursive for MPT shares in a vault, descending to
 *   assets in the vault, up to maxAssetCheckDepth recursion depth. This is
 *   purely defensive, as we currently do not allow such vaults to be created.
 */
[[nodiscard]] inline bool
isFrozen(ReadView const& view, AccountID const& account, Asset const& asset, int depth = 0)
{
    return std::visit(
        [&](auto const& issue) { return isFrozen(view, account, issue, depth); }, asset.value());
}

[[nodiscard]] inline TER
checkFrozen(ReadView const& view, AccountID const& account, Issue const& issue)
{
    return isFrozen(view, account, issue) ? (TER)tecFROZEN : (TER)tesSUCCESS;
}

[[nodiscard]] inline TER
checkFrozen(ReadView const& view, AccountID const& account, MPTIssue const& mptIssue)
{
    return isFrozen(view, account, mptIssue) ? (TER)tecLOCKED : (TER)tesSUCCESS;
}

[[nodiscard]] inline TER
checkFrozen(ReadView const& view, AccountID const& account, Asset const& asset)
{
    return std::visit(
        [&](auto const& issue) { return checkFrozen(view, account, issue); }, asset.value());
}

[[nodiscard]] inline bool
isAnyFrozen(
    ReadView const& view,
    std::initializer_list<AccountID> const& accounts,
    Issue const& issue)
{
    for (auto const& account : accounts)
    {
        if (isFrozen(view, account, issue.currency, issue.account))
            return true;
    }
    return false;
}

[[nodiscard]] inline bool
isAnyFrozen(
    ReadView const& view,
    std::initializer_list<AccountID> const& accounts,
    Asset const& asset,
    int depth = 0)
{
    return std::visit(
        [&]<ValidIssueType TIss>(TIss const& issue) {
            if constexpr (std::is_same_v<TIss, Issue>)
                return isAnyFrozen(view, accounts, issue);
            else
                return isAnyFrozen(view, accounts, issue, depth);
        },
        asset.value());
}

[[nodiscard]] inline bool
isDeepFrozen(
    ReadView const& view,
    AccountID const& account,
    MPTIssue const& mptIssue,
    int depth = 0)
{
    // Unlike IOUs, frozen / locked MPTs are not allowed to send or receive
    // funds, so checking "deep frozen" is the same as checking "frozen".
    return isFrozen(view, account, mptIssue, depth);
}

/**
 *   isFrozen check is recursive for MPT shares in a vault, descending to
 *   assets in the vault, up to maxAssetCheckDepth recursion depth. This is
 *   purely defensive, as we currently do not allow such vaults to be created.
 */
[[nodiscard]] inline bool
isDeepFrozen(ReadView const& view, AccountID const& account, Asset const& asset, int depth = 0)
{
    return std::visit(
        [&](auto const& issue) { return isDeepFrozen(view, account, issue, depth); },
        asset.value());
}

[[nodiscard]] inline TER
checkDeepFrozen(ReadView const& view, AccountID const& account, MPTIssue const& mptIssue)
{
    return isDeepFrozen(view, account, mptIssue) ? (TER)tecLOCKED : (TER)tesSUCCESS;
}

[[nodiscard]] inline TER
checkDeepFrozen(ReadView const& view, AccountID const& account, Asset const& asset)
{
    return std::visit(
        [&](auto const& issue) { return checkDeepFrozen(view, account, issue); }, asset.value());
}

//------------------------------------------------------------------------------
//
// Account balance functions (Asset-based dispatchers)
//
//------------------------------------------------------------------------------

// Returns the amount an account can spend.
//
// If shSIMPLE_BALANCE is specified, this is the amount the account can spend
// without going into debt.
//
// If shFULL_BALANCE is specified, this is the amount the account can spend
// total. Specifically:
// * The account can go into debt if using a trust line, and the other side has
// a non-zero limit.
// * If the account is the asset issuer the limit is defined by the asset /
//   issuance.
//
// <-- saAmount: amount of currency held by account. May be negative.
[[nodiscard]] STAmount
accountHolds(
    ReadView const& view,
    AccountID const& account,
    Currency const& currency,
    AccountID const& issuer,
    FreezeHandling zeroIfFrozen,
    beast::Journal j,
    SpendableHandling includeFullBalance = shSIMPLE_BALANCE);

[[nodiscard]] STAmount
accountHolds(
    ReadView const& view,
    AccountID const& account,
    Issue const& issue,
    FreezeHandling zeroIfFrozen,
    beast::Journal j,
    SpendableHandling includeFullBalance = shSIMPLE_BALANCE);

[[nodiscard]] STAmount
accountHolds(
    ReadView const& view,
    AccountID const& account,
    MPTIssue const& mptIssue,
    FreezeHandling zeroIfFrozen,
    AuthHandling zeroIfUnauthorized,
    beast::Journal j,
    SpendableHandling includeFullBalance = shSIMPLE_BALANCE);

[[nodiscard]] STAmount
accountHolds(
    ReadView const& view,
    AccountID const& account,
    Asset const& asset,
    FreezeHandling zeroIfFrozen,
    AuthHandling zeroIfUnauthorized,
    beast::Journal j,
    SpendableHandling includeFullBalance = shSIMPLE_BALANCE);

// Returns the amount an account can spend of the currency type saDefault, or
// returns saDefault if this account is the issuer of the currency in
// question. Should be used in favor of accountHolds when questioning how much
// an account can spend while also allowing currency issuers to spend
// unlimited amounts of their own currency (since they can always issue more).
[[nodiscard]] STAmount
accountFunds(
    ReadView const& view,
    AccountID const& id,
    STAmount const& saDefault,
    FreezeHandling freezeHandling,
    beast::Journal j);

/** Returns the transfer fee as Rate based on the type of token
 * @param view The ledger view
 * @param amount The amount to transfer
 */
[[nodiscard]] Rate
transferRate(ReadView const& view, STAmount const& amount);

//------------------------------------------------------------------------------
//
// Holding operations (Asset-based dispatchers)
//
//------------------------------------------------------------------------------

[[nodiscard]] TER
canAddHolding(ReadView const& view, Asset const& asset);

[[nodiscard]] inline TER
addEmptyHolding(
    ApplyView& view,
    AccountID const& accountID,
    XRPAmount priorBalance,
    Asset const& asset,
    beast::Journal journal)
{
    return std::visit(
        [&]<ValidIssueType TIss>(TIss const& issue) -> TER {
            return addEmptyHolding(view, accountID, priorBalance, issue, journal);
        },
        asset.value());
}

[[nodiscard]] inline TER
removeEmptyHolding(
    ApplyView& view,
    AccountID const& accountID,
    Asset const& asset,
    beast::Journal journal)
{
    return std::visit(
        [&]<ValidIssueType TIss>(TIss const& issue) -> TER {
            return removeEmptyHolding(view, accountID, issue, journal);
        },
        asset.value());
}

//------------------------------------------------------------------------------
//
// Authorization and transfer checks (Asset-based dispatchers)
//
//------------------------------------------------------------------------------

[[nodiscard]] TER inline requireAuth(
    ReadView const& view,
    Asset const& asset,
    AccountID const& account,
    AuthType authType = AuthType::Legacy)
{
    return std::visit(
        [&]<ValidIssueType TIss>(TIss const& issue_) {
            return requireAuth(view, issue_, account, authType);
        },
        asset.value());
}

[[nodiscard]] TER inline canTransfer(
    ReadView const& view,
    Asset const& asset,
    AccountID const& from,
    AccountID const& to)
{
    return std::visit(
        [&]<ValidIssueType TIss>(TIss const& issue) -> TER {
            return canTransfer(view, issue, from, to);
        },
        asset.value());
}

//------------------------------------------------------------------------------
//
// Money Transfers (Asset-based dispatchers)
//
//------------------------------------------------------------------------------

// Direct send w/o fees:
// - Redeeming IOUs and/or sending sender's own IOUs.
// - Create trust line of needed.
// --> bCheckIssuer : normally require issuer to be involved.
// [[nodiscard]] // nodiscard commented out so DirectStep.cpp compiles.

/** Calls static rippleCreditIOU if saAmount represents Issue.
 * Calls static rippleCreditMPT if saAmount represents MPTIssue.
 */
TER
rippleCredit(
    ApplyView& view,
    AccountID const& uSenderID,
    AccountID const& uReceiverID,
    STAmount const& saAmount,
    bool bCheckIssuer,
    beast::Journal j);

/** Calls static accountSendIOU if saAmount represents Issue.
 * Calls static accountSendMPT if saAmount represents MPTIssue.
 */
[[nodiscard]] TER
accountSend(
    ApplyView& view,
    AccountID const& from,
    AccountID const& to,
    STAmount const& saAmount,
    beast::Journal j,
    WaiveTransferFee waiveFee = WaiveTransferFee::No);

using MultiplePaymentDestinations = std::vector<std::pair<AccountID, Number>>;
/** Like accountSend, except one account is sending multiple payments (with the
 *  same asset!) simultaneously
 *
 * Calls static accountSendMultiIOU if saAmount represents Issue.
 * Calls static accountSendMultiMPT if saAmount represents MPTIssue.
 */
[[nodiscard]] TER
accountSendMulti(
    ApplyView& view,
    AccountID const& senderID,
    Asset const& asset,
    MultiplePaymentDestinations const& receivers,
    beast::Journal j,
    WaiveTransferFee waiveFee = WaiveTransferFee::No);

[[nodiscard]] TER
transferXRP(
    ApplyView& view,
    AccountID const& from,
    AccountID const& to,
    STAmount const& amount,
    beast::Journal j);

}  // namespace xrpl
