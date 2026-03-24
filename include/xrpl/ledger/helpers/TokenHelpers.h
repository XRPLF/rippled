#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/SLEBase.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Rate.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/TER.h>

#include <initializer_list>
#include <memory>
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

class TokenBase : public virtual ReadOnlySLE
{
public:
    [[nodiscard]] virtual bool
    isGlobalFrozen() const = 0;

    [[nodiscard]] virtual bool
    isIndividualFrozen(AccountID const& account) const = 0;

    /**
     *   isFrozen check is recursive for MPT shares in a vault, descending to
     *   assets in the vault, up to maxAssetCheckDepth recursion depth. This is
     *   purely defensive, as we currently do not allow such vaults to be created.
     */
    [[nodiscard]] virtual bool
    isFrozen(AccountID const& account, int depth = 0) const;

    [[nodiscard]] virtual TER
    checkFrozen(AccountID const& account) const = 0;

    [[nodiscard]] virtual bool
    isAnyFrozen(std::initializer_list<AccountID> const& accounts, int depth = 0) const = 0;

    /**
     *   isFrozen check is recursive for MPT shares in a vault, descending to
     *   assets in the vault, up to maxAssetCheckDepth recursion depth. This is
     *   purely defensive, as we currently do not allow such vaults to be created.
     */
    [[nodiscard]] virtual bool
    isDeepFrozen(AccountID const& account, int depth = 0) const = 0;

    [[nodiscard]] virtual TER
    checkDeepFrozen(AccountID const& account) const = 0;

    /** Returns the transfer fee as Rate based on the type of token
     * @param view The ledger view
     * @param amount The amount to transfer
     */
    [[nodiscard]] virtual Rate
    transferRate() const = 0;
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
    virtual STAmount
    accountHolds(
        AccountID const& account,
        FreezeHandling zeroIfFrozen,
        beast::Journal j,
        SpendableHandling includeFullBalance = shSIMPLE_BALANCE) const = 0;

    [[nodiscard]] virtual STAmount
    accountHolds(
        AccountID const& account,
        FreezeHandling zeroIfFrozen,
        AuthHandling zeroIfUnauthorized,
        beast::Journal j,
        SpendableHandling includeFullBalance = shSIMPLE_BALANCE) const = 0;

    [[nodiscard]] virtual TER
    canAddHolding() const = 0;

    //------------------------------------------------------------------------------
    //
    // Authorization and transfer checks (Asset-based dispatchers)
    //
    //------------------------------------------------------------------------------

    [[nodiscard]] virtual TER
    requireAuth(AccountID const& account, AuthType authType = AuthType::Legacy, int depth = 0)
        const = 0;

    [[nodiscard]] virtual TER
    canTransfer(AccountID const& from, AccountID const& to) const = 0;

    //------------------------------------------------------------------------------
    //
    // Token capability checks (Asset-based dispatchers)
    //
    //------------------------------------------------------------------------------

    /** Check if the token issuer has enabled clawback capability.
     * For IOUs, checks lsfAllowTrustLineClawback on issuer's AccountRoot.
     * For MPTs, checks lsfMPTCanClawback on the issuance.
     */
    [[nodiscard]] virtual bool
    canClawback() const = 0;

    /** Check if the token requires authorization for holders.
     * For IOUs, checks lsfRequireAuth on issuer's AccountRoot.
     * For MPTs, checks lsfMPTRequireAuth on the issuance.
     */
    [[nodiscard]] virtual bool
    requiresAuth() const = 0;

    [[nodiscard]] virtual bool
    hasHolder(AccountID const& holder) const = 0;

protected:
    TokenBase(ReadView const& view, std::shared_ptr<SLE const> sle) : ReadOnlySLE(sle, view)
    {
    }
};

class WritableTokenBase : public virtual TokenBase, public virtual WritableSLE
{
public:
    //------------------------------------------------------------------------------
    //
    // Holding operations (Asset-based dispatchers)
    //
    //------------------------------------------------------------------------------

    [[nodiscard]] virtual TER
    addEmptyHolding(AccountID const& accountID, XRPAmount priorBalance, beast::Journal journal) = 0;

    [[nodiscard]] virtual TER
    removeEmptyHolding(AccountID const& accountID, beast::Journal journal) = 0;

protected:
    WritableTokenBase(ApplyView& view, std::shared_ptr<SLE> sle)
        : TokenBase(view, sle), WritableSLE(sle, view)
    {
    }
};

std::unique_ptr<TokenBase>
makeTokenBase(ReadView const& view, Asset const& asset);

std::unique_ptr<WritableTokenBase>
makeWritableTokenBase(ApplyView& view, Asset const& asset);

// Helper function to get transfer rate from an STAmount
[[nodiscard]] Rate
transferRate(ReadView const& view, STAmount const& amount);

// Returns the amount the specified account can spend.
// Supports both IOU and MPT via Currency/AccountID parameters.
[[nodiscard]] STAmount
accountHolds(
    ReadView const& view,
    AccountID const& account,
    Currency const& currency,
    AccountID const& issuer,
    FreezeHandling zeroIfFrozen,
    beast::Journal j,
    SpendableHandling includeFullBalance = shSIMPLE_BALANCE);

// Returns the amount the specified account can spend for a given Asset.
// Dispatches to appropriate token wrapper based on Asset type.
[[nodiscard]] STAmount
accountHolds(
    ReadView const& view,
    AccountID const& account,
    Asset const& asset,
    FreezeHandling zeroIfFrozen,
    AuthHandling zeroIfUnauthorized,
    beast::Journal j,
    SpendableHandling includeFullBalance = shSIMPLE_BALANCE);

//------------------------------------------------------------------------------
//
// Money Transfers (Asset-based dispatchers)
//
//------------------------------------------------------------------------------

// Direct send w/o fees:
// - Redeeming IOUs and/or sending sender's own IOUs.
// - Create trust line of needed.
// bCheckIssuer : normally require issuer to be involved.
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
