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
isFrozen(ReadView const& view, AccountID const& account, MPTIssue const& mptIssue, int depth = 0);

[[nodiscard]] bool
isAnyFrozen(
    ReadView const& view,
    std::initializer_list<AccountID> const& accounts,
    MPTIssue const& mptIssue,
    int depth = 0);

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
 */
[[nodiscard]] TER
requireAuth(
    ReadView const& view,
    MPTIssue const& mptIssue,
    AccountID const& account,
    AuthType authType = AuthType::Legacy,
    int depth = 0);

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

/** Check if the destination account is allowed
 *  to receive MPT. Return tecNO_AUTH if it doesn't
 *  and tesSUCCESS otherwise.
 */
[[nodiscard]] TER
canTransfer(
    ReadView const& view,
    MPTIssue const& mptIssue,
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

}  // namespace xrpl
