#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/MPTIssue.h>
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

/** Controls whether accountSend is allowed to overflow OutstandingAmount **/
enum class AllowMPTOverflow : bool { No = false, Yes };

/* Check if MPToken (for MPT) or trust line (for IOU) exists:
 * - StrongAuth - before checking if authorization is required
 * - WeakAuth
 *    for MPT - after checking lsfMPTRequireAuth flag
 *    for IOU - do not check if trust line exists
 * - Legacy
 *    for MPT - before checking lsfMPTRequireAuth flag i.e. same as StrongAuth
 *    for IOU - do not check if trust line exists i.e. same as WeakAuth
 */
enum class AuthType { StrongAuth, WeakAuth, Legacy };

//------------------------------------------------------------------------------
//
// Freeze checking (Asset-based dispatchers)
//
//------------------------------------------------------------------------------

[[nodiscard]] bool
isGlobalFrozen(ReadView const& view, Asset const& asset);

[[nodiscard]] bool
isIndividualFrozen(ReadView const& view, AccountID const& account, Asset const& asset);

/**
 *   isFrozen check is recursive for MPT shares in a vault, descending to
 *   assets in the vault, up to maxAssetCheckDepth recursion depth. This is
 *   purely defensive, as we currently do not allow such vaults to be created.
 */
[[nodiscard]] bool
isFrozen(ReadView const& view, AccountID const& account, Asset const& asset, int depth = 0);

[[nodiscard]] TER
checkFrozen(ReadView const& view, AccountID const& account, Issue const& issue);

[[nodiscard]] TER
checkFrozen(ReadView const& view, AccountID const& account, MPTIssue const& mptIssue);

[[nodiscard]] TER
checkFrozen(ReadView const& view, AccountID const& account, Asset const& asset);

[[nodiscard]] bool
isAnyFrozen(
    ReadView const& view,
    std::initializer_list<AccountID> const& accounts,
    Issue const& issue);

[[nodiscard]] bool
isAnyFrozen(
    ReadView const& view,
    std::initializer_list<AccountID> const& accounts,
    Asset const& asset,
    int depth = 0);

[[nodiscard]] bool
isDeepFrozen(
    ReadView const& view,
    AccountID const& account,
    MPTIssue const& mptIssue,
    int depth = 0);

/**
 *   isFrozen check is recursive for MPT shares in a vault, descending to
 *   assets in the vault, up to maxAssetCheckDepth recursion depth. This is
 *   purely defensive, as we currently do not allow such vaults to be created.
 */
[[nodiscard]] bool
isDeepFrozen(ReadView const& view, AccountID const& account, Asset const& asset, int depth = 0);

[[nodiscard]] TER
checkDeepFrozen(ReadView const& view, AccountID const& account, MPTIssue const& mptIssue);

[[nodiscard]] TER
checkDeepFrozen(ReadView const& view, AccountID const& account, Asset const& asset);

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

// Overload with AuthHandling to support IOU and MPT.
[[nodiscard]] STAmount
accountFunds(
    ReadView const& view,
    AccountID const& id,
    STAmount const& saDefault,
    FreezeHandling freezeHandling,
    AuthHandling authHandling,
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

[[nodiscard]] TER
addEmptyHolding(
    ApplyView& view,
    AccountID const& accountID,
    XRPAmount priorBalance,
    Asset const& asset,
    beast::Journal journal);

[[nodiscard]] TER
removeEmptyHolding(
    ApplyView& view,
    AccountID const& accountID,
    Asset const& asset,
    beast::Journal journal);

//------------------------------------------------------------------------------
//
// Authorization and transfer checks (Asset-based dispatchers)
//
//------------------------------------------------------------------------------

[[nodiscard]] TER
requireAuth(
    ReadView const& view,
    Asset const& asset,
    AccountID const& account,
    AuthType authType = AuthType::Legacy);

[[nodiscard]] TER
canTransfer(ReadView const& view, Asset const& asset, AccountID const& from, AccountID const& to);

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

/** Calls static directSendNoFeeIOU if saAmount represents Issue.
 * Calls static directSendNoFeeMPT if saAmount represents MPTIssue.
 */
TER
directSendNoFee(
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
    WaiveTransferFee waiveFee = WaiveTransferFee::No,
    AllowMPTOverflow allowOverflow = AllowMPTOverflow::No);

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
