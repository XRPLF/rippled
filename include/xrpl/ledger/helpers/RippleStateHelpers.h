#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/IOUAmount.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/TER.h>

//------------------------------------------------------------------------------
//
// RippleState (Trustline) helpers
//
//------------------------------------------------------------------------------

namespace xrpl {

//------------------------------------------------------------------------------
//
// Credit functions (from Credit.h)
//
//------------------------------------------------------------------------------

/** Calculate the maximum amount of IOUs that an account can hold
    @param view the ledger to check against.
    @param account the account of interest.
    @param issuer the issuer of the IOU.
    @param currency the IOU to check.
    @return The maximum amount that can be held.
*/
/** @{ */
STAmount
creditLimit(
    ReadView const& view,
    AccountID const& account,
    AccountID const& issuer,
    Currency const& currency);

IOUAmount
creditLimit2(ReadView const& v, AccountID const& acc, AccountID const& iss, Currency const& cur);
/** @} */

/** Returns the amount of IOUs issued by issuer that are held by an account
    @param view the ledger to check against.
    @param account the account of interest.
    @param issuer the issuer of the IOU.
    @param currency the IOU to check.
*/
/** @{ */
STAmount
creditBalance(
    ReadView const& view,
    AccountID const& account,
    AccountID const& issuer,
    Currency const& currency);
/** @} */

//------------------------------------------------------------------------------
//
// Freeze checking (IOU-specific)
//
//------------------------------------------------------------------------------

[[nodiscard]] bool
isIndividualFrozen(
    ReadView const& view,
    AccountID const& account,
    Currency const& currency,
    AccountID const& issuer);

[[nodiscard]] inline bool
isIndividualFrozen(ReadView const& view, AccountID const& account, Issue const& issue)
{
    return isIndividualFrozen(view, account, issue.currency, issue.account);
}

[[nodiscard]] bool
isFrozen(
    ReadView const& view,
    AccountID const& account,
    Currency const& currency,
    AccountID const& issuer);

[[nodiscard]] inline bool
isFrozen(ReadView const& view, AccountID const& account, Issue const& issue)
{
    return isFrozen(view, account, issue.currency, issue.account);
}

// Overload with depth parameter for uniformity with MPTIssue version.
// The depth parameter is ignored for IOUs since they don't have vault recursion.
[[nodiscard]] inline bool
isFrozen(ReadView const& view, AccountID const& account, Issue const& issue, std::uint8_t /*depth*/)
{
    return isFrozen(view, account, issue);
}

[[nodiscard]] bool
isDeepFrozen(
    ReadView const& view,
    AccountID const& account,
    Currency const& currency,
    AccountID const& issuer);

[[nodiscard]] inline bool
isDeepFrozen(
    ReadView const& view,
    AccountID const& account,
    Issue const& issue,
    std::uint8_t = 0 /*ignored*/)
{
    return isDeepFrozen(view, account, issue.currency, issue.account);
}

[[nodiscard]] inline TER
checkDeepFrozen(ReadView const& view, AccountID const& account, Issue const& issue)
{
    return isDeepFrozen(view, account, issue) ? (TER)tecFROZEN : (TER)tesSUCCESS;
}

//------------------------------------------------------------------------------
//
// Trust line operations
//
//------------------------------------------------------------------------------

/** Create a trust line

    This can set an initial balance.
*/
[[nodiscard]] TER
trustCreate(
    ApplyView& view,
    bool const bSrcHigh,
    AccountID const& uSrcAccountID,
    AccountID const& uDstAccountID,
    uint256 const& uIndex,      // --> ripple state entry
    SLE::ref sleAccount,        // --> the account being set.
    bool const bAuth,           // --> authorize account.
    bool const bNoRipple,       // --> others cannot ripple through
    bool const bFreeze,         // --> funds cannot leave
    bool bDeepFreeze,           // --> can neither receive nor send funds
    STAmount const& saBalance,  // --> balance of account being set.
                                // Issuer should be noAccount()
    STAmount const& saLimit,    // --> limit for account being set.
                                // Issuer should be the account being set.
    std::uint32_t uQualityIn,
    std::uint32_t uQualityOut,
    beast::Journal j);

[[nodiscard]] TER
trustDelete(
    ApplyView& view,
    SLE::ref sleRippleState,
    AccountID const& uLowAccountID,
    AccountID const& uHighAccountID,
    beast::Journal j);

//------------------------------------------------------------------------------
//
// IOU issuance/redemption
//
//------------------------------------------------------------------------------

[[nodiscard]] TER
issueIOU(
    ApplyView& view,
    AccountID const& account,
    STAmount const& amount,
    Issue const& issue,
    beast::Journal j);

[[nodiscard]] TER
redeemIOU(
    ApplyView& view,
    AccountID const& account,
    STAmount const& amount,
    Issue const& issue,
    beast::Journal j);

//------------------------------------------------------------------------------
//
// Authorization and transfer checks (IOU-specific)
//
//------------------------------------------------------------------------------

/** Check if the account lacks required authorization.
 *
 * Return tecNO_AUTH or tecNO_LINE if it does
 * and tesSUCCESS otherwise.
 *
 * If StrongAuth then return tecNO_LINE if the RippleState doesn't exist. Return
 * tecNO_AUTH if lsfRequireAuth is set on the issuer's AccountRoot, and the
 * RippleState does exist, and the RippleState is not authorized.
 *
 * If WeakAuth then return tecNO_AUTH if lsfRequireAuth is set, and the
 * RippleState exists, and is not authorized. Return tecNO_LINE if
 * lsfRequireAuth is set and the RippleState doesn't exist. Consequently, if
 * WeakAuth and lsfRequireAuth is *not* set, this function will return
 * tesSUCCESS even if RippleState does *not* exist.
 *
 * The default "Legacy" auth type is equivalent to WeakAuth.
 */
[[nodiscard]] TER
requireAuth(
    ReadView const& view,
    Issue const& issue,
    AccountID const& account,
    AuthType authType = AuthType::Legacy);

/** Check if the destination account is allowed
 *  to receive IOU. Return terNO_RIPPLE if rippling is
 *  disabled on both sides and tesSUCCESS otherwise.
 */
[[nodiscard]] TER
canTransfer(ReadView const& view, Issue const& issue, AccountID const& from, AccountID const& to);

//------------------------------------------------------------------------------
//
// Empty holding operations (IOU-specific)
//
//------------------------------------------------------------------------------

/// Any transactors that call addEmptyHolding() in doApply must call
/// canAddHolding() in preflight with the same View and Asset
[[nodiscard]] TER
addEmptyHolding(
    ApplyView& view,
    AccountID const& accountID,
    XRPAmount priorBalance,
    Issue const& issue,
    beast::Journal journal);

[[nodiscard]] TER
removeEmptyHolding(
    ApplyView& view,
    AccountID const& accountID,
    Issue const& issue,
    beast::Journal journal);

/** Delete trustline to AMM. The passed `sle` must be obtained from a prior
 * call to view.peek(). Fail if neither side of the trustline is AMM or
 * if ammAccountID is seated and is not one of the trustline's side.
 */
[[nodiscard]] TER
deleteAMMTrustLine(
    ApplyView& view,
    SLE::pointer sleState,
    std::optional<AccountID> const& ammAccountID,
    beast::Journal j);

/** Delete AMMs MPToken. The passed `sle` must be obtained from a prior
 * call to view.peek().
 */
[[nodiscard]] TER
deleteAMMMPToken(
    ApplyView& view,
    SLE::pointer sleMPT,
    AccountID const& ammAccountID,
    beast::Journal j);

}  // namespace xrpl
