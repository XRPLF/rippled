#pragma once

#include <xrpl/basics/Number.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Rate.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/XRPAmount.h>

#include <cstdint>
#include <initializer_list>
#include <optional>
#include <utility>
#include <vector>

namespace xrpl {

//------------------------------------------------------------------------------
//
// Enums for token handling
//
//------------------------------------------------------------------------------

/**
 * Controls the treatment of frozen account balances
 */
enum class FreezeHandling { IgnoreFreeze, ZeroIfFrozen };

/**
 * Controls the treatment of unauthorized MPT balances
 */
enum class AuthHandling { IgnoreAuth, ZeroIfUnauthorized };

/**
 * Controls whether to include the account's full spendable balance
 */
enum class SpendableHandling { SimpleBalance, FullBalance };

enum class WaiveTransferFee : bool { No = false, Yes };

/**
 * Controls whether accountSend is allowed to overflow OutstandingAmount *
 */
enum class AllowMPTOverflow : bool { No = false, Yes };

/**
 * Controls whether canTransfer enforces lsfMPTCanTransfer on MPTs.
 *
 *  Default is No (enforce). Use Yes at call sites that must remain available
 *  even when an MPT issuer has cleared lsfMPTCanTransfer - for example,
 *  unwinding existing positions in SAV or the Lending Protocol. Has no
 *  effect on the IOU branch of canTransfer.
 */
enum class WaiveMPTCanTransfer : bool { No = false, Yes };

/**
 * Feature-agnostic trust-line dust primitive.
 *
 * A trust line is a single ledger entry (`ltRIPPLE_STATE`) shared between
 * a non-issuer account and its issuer. An IOU payment `A -> B` for issuer
 * `I` touches at most two trust lines: the sender's line
 * `RIPPLE_STATE{A, I}` and the receiver's line `RIPPLE_STATE{B, I}`.
 * Each non-issuer party can independently opt into dust semantics on
 * their OWN line by providing a per-leg `LegPolicy`.
 *
 * `DustSplit` is that opt-in: one struct with two optional per-leg
 * sub-policies (`sender`, `receiver`). `accountSend` and its callees route
 * each sub-policy to the corresponding `directSendNoFeeIOU` invocation.
 * A leg without a sub-policy runs the classic pre-dust path
 * byte-identically. The struct is intentionally consumer-agnostic — it
 * says nothing about Vaults, AMMs, or any specific consumer.
 *
 * Modes:
 * - `Override`: the trust-line layer keeps sfBalance representable at
 *   `overrideScale` and parks any sub-quantum remainder in sfDust. Any
 *   previously-deferred sfDust is automatically promoted when the combined
 *   sfDust + credit crosses a whole-quantum boundary. Callers supply
 *   `overrideScale` from their own accounting; scale-drift up to one
 *   decade may linger until the next operation that refines the scale.
 * - `Drain` (sender-leg only): folds all of sfDust on the sender's line
 *   into the outgoing transfer, then zeroes sfDust post-op. Used for
 *   terminal removals where the sender is winding down; there is no
 *   receiver-side counterpart because a receiver has no "reservoir to
 *   drain".
 *
 * Reporting (out-fields on `LegPolicy`) is FROM THAT LEG'S NON-ISSUER
 * PARTY'S PERSPECTIVE:
 * - `sender`-leg reports SENDER-POSITIVE deltas (positive `balanceDelta`
 *   means the sender's holdings grew, which is unusual — a normal send
 *   makes the sender's `balanceDelta` negative and, if any deferred dust
 *   was promoted into sfBalance, negates it further; the reported
 *   `dustDelta` is `-sfDust_before` under `Drain`).
 * - `receiver`-leg reports RECEIVER-POSITIVE deltas (positive
 *   `balanceDelta` means the receiver's holdings grew — the normal case).
 *
 * This per-leg-party-positive convention lets a Vault consumer reconcile
 * its own bookkeeping symmetrically: reads from `sender->balanceDelta` on
 * a withdrawal/clawback line up in sign with reads from
 * `receiver->balanceDelta` on a deposit, without a sign flip.
 *
 * Contract asserts (checked in debug):
 * - `Drain` on the receiver leg is a caller error.
 * - Attaching a policy to the issuer side of a direct payment (where one
 *   party IS the issuer) is a caller error — the policy must correspond
 *   to the non-issuer party's leg.
 * - Any non-empty `DustSplit` requires `featureLendingProtocolV1_1`
 *   enabled; a policy under an older rules set falls back to nullptr and
 *   asserts in debug.
 *
 * Scope note: Vault is the sole consumer today. Adding a new consumer
 * (AMM, LoanBroker, etc.) is purely additive: introduce a caller-side
 * eligibility gate (analogous to `xrpl::vault_dust::useVaultDust`),
 * construct a `DustSplit` in that consumer's transactor helpers, and
 * reconcile the consumer's own bookkeeping using the reported deltas.
 * The trust-line layer needs no per-consumer awareness.
 */
struct DustSplit
{
    struct LegPolicy
    {
        enum class Mode { Override, Drain };

        Mode mode = Mode::Override;
        int overrideScale = 0;  // used only when mode == Override; exponent
                                // sfBalance must remain representable at

        // out (from this leg's non-issuer party's perspective):
        Number balanceDelta{};
        Number dustDelta{};
    };

    std::optional<LegPolicy> sender;
    std::optional<LegPolicy> receiver;
};

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

[[nodiscard]] TER
checkGlobalFrozen(ReadView const& view, Asset const& asset);

[[nodiscard]] bool
isIndividualFrozen(ReadView const& view, AccountID const& account, Asset const& asset);

[[nodiscard]] TER
checkIndividualFrozen(ReadView const& view, AccountID const& account, Asset const& asset);

/**
 * isFrozen check is recursive for MPT shares in a vault, descending to
 * assets in the vault, up to maxAssetCheckDepth recursion depth. This is
 * purely defensive, as we currently do not allow such vaults to be created.
 */
[[nodiscard]] bool
isFrozen(
    ReadView const& view,
    AccountID const& account,
    Asset const& asset,
    std::uint8_t depth = 0);

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
    std::uint8_t depth = 0);

[[nodiscard]] bool
isDeepFrozen(
    ReadView const& view,
    AccountID const& account,
    MPTIssue const& mptIssue,
    std::uint8_t depth = 0);

/**
 * isFrozen check is recursive for MPT shares in a vault, descending to
 * assets in the vault, up to maxAssetCheckDepth recursion depth. This is
 * purely defensive, as we currently do not allow such vaults to be created.
 */
[[nodiscard]] bool
isDeepFrozen(
    ReadView const& view,
    AccountID const& account,
    Asset const& asset,
    std::uint8_t depth = 0);

[[nodiscard]] TER
checkDeepFrozen(ReadView const& view, AccountID const& account, MPTIssue const& mptIssue);

[[nodiscard]] TER
checkDeepFrozen(ReadView const& view, AccountID const& account, Asset const& asset);

/**
 * Checks freeze compliance for withdrawing an asset from a pseudo-account (e.g. Vault, AMM,
 * LoanBroker) to a destination account.
 *
 * Asserts that sourceAcct is a pseudo-account and that submitterAcct and dstAcct are not.
 *
 * Issuer exemption: returns tesSUCCESS immediately when dstAcct is the asset issuer — the issuer
 * can always receive their own token, even when the pool is frozen.  Callers that need to block
 * withdrawals from a frozen pool even for the issuer (e.g. because the pool math cannot handle it)
 * must check checkFrozen(sourceAcct, asset) separately before calling this function.
 *
 * Otherwise checks, in order:
 *   1. If the asset is globally frozen the remaining checks are redundant.
 *   2. The pseudo-account's trustline / MPToken must not be individually frozen for sending.
 *   3. The submitter's trustline / MPToken must not be individually frozen. Skipped when
 * submitter == dst (self-withdrawal) so a regular freeze does not prevent recovering one's own
 * funds. (Enforced as defensive code; no current caller exercises a frozen submitter ≠ dst.)
 *   4. The destination must not be deep-frozen.
 *
 * For IOUs a regular individual freeze on the submitter does NOT block self-withdrawal; only deep
 * freeze does. For MPTs "locked" is equivalent to deep-frozen, so locked MPT holders are always
 * blocked.
 *
 * @param view          Ledger view to read freeze state from.
 * @param pseudoAcct       Pseudo-account the funds are withdrawn from (sender).
 * @param submitterAcct Account that submitted the withdrawal transaction.
 * @param dstAcct       Account receiving the withdrawn funds.
 * @param asset         Asset being withdrawn.
 * @return tesSUCCESS if the withdrawal is permitted, otherwise a freeze
 *         result (tecFROZEN for IOUs, tecLOCKED for MPTs).
 */
[[nodiscard]] TER
checkWithdrawFreeze(
    ReadView const& view,
    AccountID const& pseudoAcct,
    AccountID const& submitterAcct,
    AccountID const& dstAcct,
    Asset const& asset);

/**
 * Checks freeze compliance for depositing an asset into a pseudo-account (e.g. Vault, AMM,
 * LoanBroker).
 *
 * Checks, in order:
 *   1. If the asset is globally frozen the remaining checks are redundant.
 *   2. The depositor must not be individually frozen for the asset. Skipped when srcAcct is the
 * asset issuer, since the issuer can always send its own asset.
 *   3. The pseudo-account must not be individually frozen for the asset.  Unlike regular accounts,
 * pseudo-accounts cannot receive deposits under a regular freeze because the deposited funds
 * could not later be withdrawn.
 *
 * @param view    Ledger view to read freeze state from.
 * @param srcAcct Depositor sending the funds.
 * @param pseudoAcct Pseudo-account receiving the deposit.
 * @param asset   Asset being deposited.
 * @return tesSUCCESS if the deposit is permitted, otherwise a freeze result
 *         (tecFROZEN for IOUs, tecLOCKED for MPTs).
 */
[[nodiscard]] TER
checkDepositFreeze(
    ReadView const& view,
    AccountID const& srcAcct,
    AccountID const& pseudoAcct,
    Asset const& asset);

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
    SpendableHandling includeFullBalance = SpendableHandling::SimpleBalance);

[[nodiscard]] STAmount
accountHolds(
    ReadView const& view,
    AccountID const& account,
    Issue const& issue,
    FreezeHandling zeroIfFrozen,
    beast::Journal j,
    SpendableHandling includeFullBalance = SpendableHandling::SimpleBalance);

[[nodiscard]] STAmount
accountHolds(
    ReadView const& view,
    AccountID const& account,
    MPTIssue const& mptIssue,
    FreezeHandling zeroIfFrozen,
    AuthHandling zeroIfUnauthorized,
    beast::Journal j,
    SpendableHandling includeFullBalance = SpendableHandling::SimpleBalance);

[[nodiscard]] STAmount
accountHolds(
    ReadView const& view,
    AccountID const& account,
    Asset const& asset,
    FreezeHandling zeroIfFrozen,
    AuthHandling zeroIfUnauthorized,
    beast::Journal j,
    SpendableHandling includeFullBalance = SpendableHandling::SimpleBalance);

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

/**
 * Returns the transfer fee as Rate based on the type of token
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
    ApplyViewContext ctx,
    AccountID const& accountID,
    XRPAmount priorBalance,
    Asset const& asset,
    beast::Journal journal);

[[nodiscard]] TER
removeEmptyHolding(
    ApplyViewContext ctx,
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
canTransfer(
    ReadView const& view,
    Asset const& asset,
    AccountID const& from,
    AccountID const& to,
    WaiveMPTCanTransfer waive = WaiveMPTCanTransfer::No,
    std::uint8_t depth = 0);

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

/**
 * Calls static directSendNoFeeIOU if saAmount represents Issue.
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

/**
 * Calls static accountSendIOU if saAmount represents Issue.
 * Calls static accountSendMPT if saAmount represents MPTIssue.
 */
[[nodiscard]] TER
accountSend(
    ApplyView& view,
    AccountID const& from,
    AccountID const& to,
    STAmount const& saAmount,
    beast::Journal j,
    SLE::ref sponsorSle = {},
    WaiveTransferFee waiveFee = WaiveTransferFee::No,
    AllowMPTOverflow allowOverflow = AllowMPTOverflow::No,
    DustSplit* dust = nullptr);

using MultiplePaymentDestinations = std::vector<std::pair<AccountID, Number>>;
/**
 * Like accountSend, except one account is sending multiple payments (with the
 *  same asset!) simultaneously
 *
 * Calls static accountSendMultiIOU if saAmount represents Issue.
 * Calls static accountSendMultiMPT if saAmount represents MPTIssue.
 *
 * The optional `dust` split applies only to the sender's leg (the single
 * shared sender trust line across all destinations). Only `dust->sender`
 * is consulted; `dust->receiver` must be nullopt.
 */
[[nodiscard]] TER
accountSendMulti(
    ApplyView& view,
    AccountID const& senderID,
    Asset const& asset,
    MultiplePaymentDestinations const& receivers,
    beast::Journal j,
    WaiveTransferFee waiveFee = WaiveTransferFee::No,
    DustSplit* dust = nullptr);

[[nodiscard]] TER
transferXRP(
    ApplyView& view,
    AccountID const& from,
    AccountID const& to,
    STAmount const& amount,
    beast::Journal j);

}  // namespace xrpl
