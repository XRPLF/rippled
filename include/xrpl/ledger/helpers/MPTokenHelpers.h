#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Rate.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/TER.h>

#include <initializer_list>
#include <optional>

namespace xrpl {

//------------------------------------------------------------------------------
//
// Freeze checking (MPT-specific)
//
//------------------------------------------------------------------------------

[[nodiscard]] bool
isGlobalFrozen(ReadView const& view, MPTIssue const& mptIssue);

[[nodiscard]] bool
isIndividualFrozen(ReadView const& view, AccountID const& account, MPTIssue const& mptIssue);

[[nodiscard]] bool
isFrozen(
    ReadView const& view,
    AccountID const& account,
    MPTIssue const& mptIssue,
    std::uint8_t depth = 0);

[[nodiscard]] bool
isAnyFrozen(
    ReadView const& view,
    std::initializer_list<AccountID> const& accounts,
    MPTIssue const& mptIssue,
    std::uint8_t depth = 0);

//------------------------------------------------------------------------------
//
// Transfer rate (MPT-specific)
//
//------------------------------------------------------------------------------

/** Returns MPT transfer fee as Rate. Rate specifies
 * the fee as fractions of 1 billion. For example, 1% transfer rate
 * is represented as 1,010,000,000.
 * @param issuanceID MPTokenIssuanceID of MPTTokenIssuance object
 */
[[nodiscard]] Rate
transferRate(ReadView const& view, MPTID const& issuanceID);

//------------------------------------------------------------------------------
//
// Holding checks (MPT-specific)
//
//------------------------------------------------------------------------------

[[nodiscard]] TER
canAddHolding(ReadView const& view, MPTIssue const& mptIssue);

//------------------------------------------------------------------------------
//
// Authorization (MPT-specific)
//
//------------------------------------------------------------------------------

[[nodiscard]] TER
authorizeMPToken(
    ApplyView& view,
    XRPAmount const& priorBalance,
    MPTID const& mptIssuanceID,
    AccountID const& account,
    beast::Journal journal,
    std::uint32_t flags = 0,
    std::optional<AccountID> holderID = std::nullopt);

/** Check if the account lacks required authorization for MPT.
 *
 * requireAuth check is recursive for MPT shares in a vault, descending to
 * assets in the vault, up to maxAssetCheckDepth recursion depth. This is
 * purely defensive, as we currently do not allow such vaults to be created.
 * WeakAuth intentionally allows missing MPTokens under MPToken V2.
 */
[[nodiscard]] TER
requireAuth(
    ReadView const& view,
    MPTIssue const& mptIssue,
    AccountID const& account,
    AuthType authType = AuthType::Legacy,
    std::uint8_t depth = 0);

/** Enforce account has MPToken to match its authorization.
 *
 *   Called from doApply - it will check for expired (and delete if found any)
 *   credentials matching DomainID set in MPTokenIssuance. Must be called if
 *   requireAuth(...MPTIssue...) returned tesSUCCESS or tecEXPIRED in preclaim.
 */
[[nodiscard]] TER
enforceMPTokenAuthorization(
    ApplyView& view,
    MPTID const& mptIssuanceID,
    AccountID const& account,
    XRPAmount const& priorBalance,
    beast::Journal j);

/** Resolve the underlying asset of a vault share.
 *
 *  Reads sfReferenceHolding from @p sleShareIssuance to determine which
 *  asset the vault wraps. @p sleHolding must be the SLE that
 *  sfReferenceHolding points to — either an ltMPTOKEN (returns its
 *  MPTIssue) or an ltRIPPLE_STATE (returns its low/high Issue).
 *
 *  @pre Both SLEs must exist and @p sleHolding must be of type ltMPTOKEN
 *       or ltRIPPLE_STATE. Passing any other type is undefined behaviour.
 *  @param sleShareIssuance  MPTokenIssuance SLE for the vault share token.
 *  @param sleHolding        SLE referenced by sfReferenceHolding.
 *  @return The underlying Asset (MPTIssue or Issue).
 */
[[nodiscard]] Asset
assetOfHolding(SLE const& sleShareIssuance, SLE const& sleHolding);

/** Check whether @p to may receive the given MPT from @p from.
 *
 *  The check passes when any of the following is true:
 *  - @p waive is WaiveMPTCanTransfer::Yes (recovery-path exemption), or
 *  - @p from or @p to is the issuer, or
 *  - lsfMPTCanTransfer is set on the MPTokenIssuance.
 *
 *  For vault shares (MPTokenIssuances that carry sfReferenceHolding) the
 *  check recurses into the underlying asset's transferability. This
 *  recursion is defensive; vault-of-vault-shares is rejected at vault
 *  creation, so in practice depth never exceeds 1.
 *
 *  @param view      Ledger state to read from.
 *  @param mptIssue  The MPT issuance being transferred.
 *  @param from      Sending account.
 *  @param to        Receiving account.
 *  @param waive     WaiveMPTCanTransfer::Yes skips the lsfMPTCanTransfer
 *                   check. Use for recovery paths (e.g. unwinding SAV or
 *                   Lending Protocol positions after an issuer revokes
 *                   transferability).
 *  @param depth     Recursion depth; bounded at kMaxAssetCheckDepth.
 *  @return tesSUCCESS if the transfer is allowed, tecNO_AUTH otherwise.
 */
[[nodiscard]] TER
canTransfer(
    ReadView const& view,
    MPTIssue const& mptIssue,
    AccountID const& from,
    AccountID const& to,
    WaiveMPTCanTransfer waive = WaiveMPTCanTransfer::No,
    std::uint8_t depth = 0);

/** Check whether @p asset may be traded on the DEX.
 *
 *  For IOU assets the check delegates to the existing offer/AMM freeze
 *  logic. For MPT assets it checks lsfMPTCanTrade on the MPTokenIssuance.
 *  Vault shares recurse into the underlying asset's tradability via
 *  sfReferenceHolding; depth is bounded at kMaxAssetCheckDepth.
 *
 *  @param view   Ledger state to read from.
 *  @param asset  The asset to check.
 *  @param depth  Recursion depth; bounded at kMaxAssetCheckDepth.
 *  @return tesSUCCESS if trading is allowed, tecNO_PERMISSION otherwise.
 */
[[nodiscard]] TER
canTrade(ReadView const& view, Asset const& asset, std::uint8_t depth = 0);

/** Convenience to combine canTrade/Transfer. Returns tesSUCCESS if Asset is Issue.
 */
[[nodiscard]] TER
canMPTTradeAndTransfer(
    ReadView const& v,
    Asset const& asset,
    AccountID const& from,
    AccountID const& to);

//------------------------------------------------------------------------------
//
// Empty holding operations (MPT-specific)
//
//------------------------------------------------------------------------------

[[nodiscard]] TER
addEmptyHolding(
    ApplyView& view,
    AccountID const& accountID,
    XRPAmount priorBalance,
    MPTIssue const& mptIssue,
    beast::Journal journal);

[[nodiscard]] TER
removeEmptyHolding(
    ApplyView& view,
    AccountID const& accountID,
    MPTIssue const& mptIssue,
    beast::Journal journal);

//------------------------------------------------------------------------------
//
// Escrow operations (MPT-specific)
//
//------------------------------------------------------------------------------

TER
lockEscrowMPT(
    ApplyView& view,
    AccountID const& uGrantorID,
    STAmount const& saAmount,
    beast::Journal j);

TER
unlockEscrowMPT(
    ApplyView& view,
    AccountID const& uGrantorID,
    AccountID const& uGranteeID,
    STAmount const& netAmount,
    STAmount const& grossAmount,
    beast::Journal j);

TER
createMPToken(
    ApplyView& view,
    MPTID const& mptIssuanceID,
    AccountID const& account,
    std::uint32_t const flags);

TER
checkCreateMPT(
    xrpl::ApplyView& view,
    xrpl::MPTIssue const& mptIssue,
    xrpl::AccountID const& holder,
    beast::Journal j);

//------------------------------------------------------------------------------
//
// MPT Overflow related
//
//------------------------------------------------------------------------------

// MaximumAmount doesn't exceed 2**63-1
std::int64_t
maxMPTAmount(SLE const& sleIssuance);

// OutstandingAmount may overflow and available amount might be negative.
// But available amount is always <= |MaximumAmount - OutstandingAmount|.
std::int64_t
availableMPTAmount(SLE const& sleIssuance);

std::int64_t
availableMPTAmount(ReadView const& view, MPTID const& mptID);

/** Checks for two types of OutstandingAmount overflow during a send operation.
 * 1.  **Direct directSendNoFee (Overflow: No):** A true overflow check when
 * `OutstandingAmount > MaximumAmount`. This threshold is used for direct
 * directSendNoFee transactions that bypass the payment engine.
 * 2.  **accountSend & Payment Engine (Overflow: Yes):** A temporary overflow
 * check when `OutstandingAmount > UINT64_MAX`. This higher threshold is used
 * for `accountSend` and payments processed via the payment engine.
 */
bool
isMPTOverflow(
    std::int64_t sendAmount,
    std::uint64_t outstandingAmount,
    std::int64_t maximumAmount,
    AllowMPTOverflow allowOverflow);

/**
 * Determine funds available for an issuer to sell in an issuer owned offer.
 * Issuing step, which could be either MPTEndPointStep last step or BookStep's
 * TakerPays may overflow OutstandingAmount. Redeeming step, in BookStep's
 * TakerGets redeems the offer's owner funds, essentially balancing out
 * the overflow, unless the offer's owner is the issuer.
 */
[[nodiscard]] STAmount
issuerFundsToSelfIssue(ReadView const& view, MPTIssue const& issue);

/** Facilitate tracking of MPT sold by an issuer owning MPT sell offer.
 * See ApplyView::issuerSelfDebitHookMPT().
 */
void
issuerSelfDebitHookMPT(ApplyView& view, MPTIssue const& issue, std::uint64_t amount);

}  // namespace xrpl
