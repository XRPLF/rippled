#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/entries/AccountRootHelpers.h>
#include <xrpl/ledger/entries/TokenHelpers.h>
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

class IOUToken : public virtual TokenBase
{
public:
    IOUToken(ReadView const& view, Issue const& issue)
        : ReadOnlySLE(view.read(keylet::account(issue.getIssuer())), view)
        , TokenBase(view, view.read(keylet::account(issue.getIssuer())))
        , issue_(issue)
        , issuer_(issue.getIssuer())
        , issuerAccount_(issuer_, view)
        , currency_(issue.currency)
    {
    }

    IOUToken(ReadView const& view, AccountID const& issuer, Currency const& currency)
        : IOUToken(view, Issue{currency, issuer})
    {
    }

    [[nodiscard]] AccountID const&
    getIssuer() const
    {
        return issuer_;
    }

    [[nodiscard]] Currency const&
    getCurrency() const
    {
        return currency_;
    }

    [[nodiscard]] bool
    isGlobalFrozen() const override
    {
        return issuerAccount_.isGlobalFrozen();
    }

    [[nodiscard]] bool
    isIndividualFrozen(AccountID const& account) const override;

    [[nodiscard]] bool
    isFrozen(AccountID const& account, int depth = 0) const override;

    [[nodiscard]] TER
    checkFrozen(AccountID const& account) const override;

    [[nodiscard]] bool
    isAnyFrozen(std::initializer_list<AccountID> const& accounts, int depth = 0) const override;

    [[nodiscard]] bool
    isDeepFrozen(AccountID const& account, int depth = 0) const override;

    [[nodiscard]] TER
    checkDeepFrozen(AccountID const& account) const override;

    [[nodiscard]] Rate
    transferRate() const override;

    STAmount
    accountHolds(
        AccountID const& account,
        FreezeHandling zeroIfFrozen,
        beast::Journal j,
        SpendableHandling includeFullBalance = shSIMPLE_BALANCE) const override;

    [[nodiscard]] STAmount
    accountHolds(
        AccountID const& account,
        FreezeHandling zeroIfFrozen,
        AuthHandling zeroIfUnauthorized,
        beast::Journal j,
        SpendableHandling includeFullBalance = shSIMPLE_BALANCE) const override;

    [[nodiscard]] TER
    canAddHolding() const override;

    //------------------------------------------------------------------------------
    //
    // Authorization and transfer checks (Asset-based dispatchers)
    //
    //------------------------------------------------------------------------------

    [[nodiscard]] TER
    requireAuth(AccountID const& account, AuthType authType = AuthType::Legacy, int depth = 0)
        const override;

    [[nodiscard]] TER
    canTransfer(AccountID const& from, AccountID const& to) const override;

    //------------------------------------------------------------------------------
    //
    // Token capability checks (IOU-specific)
    //
    //------------------------------------------------------------------------------

    [[nodiscard]] bool
    canClawback() const override;

    [[nodiscard]] bool
    requiresAuth() const override;

protected:
    Issue const issue_;
    AccountID const issuer_;
    AccountRoot const issuerAccount_;
    Currency const currency_;
};

class WritableIOUToken : public virtual WritableTokenBase, public virtual IOUToken
{
public:
    WritableIOUToken(ApplyView& view, Issue const& issue)
        : ReadOnlySLE(view.peek(keylet::account(issue.getIssuer())), view)
        , TokenBase(view, view.peek(keylet::account(issue.getIssuer())))
        , WritableSLE(view.peek(keylet::account(issue.getIssuer())), view)
        , WritableTokenBase(view, view.peek(keylet::account(issue.getIssuer())))
        , IOUToken(view, issue)
    {
    }

    WritableIOUToken(ApplyView& view, AccountID const& issuer, Currency const& currency)
        : WritableIOUToken(view, Issue{currency, issuer})
    {
    }

    // Resolve ambiguity: use writable operator-> for non-const, read-only for const
    using WritableSLE::operator->;
    using IOUToken::operator->;
    using WritableSLE::operator*;
    using IOUToken::operator*;

    //------------------------------------------------------------------------------
    //
    // Holding management (WritableTokenBase interface)
    //
    //------------------------------------------------------------------------------

    [[nodiscard]] TER
    addEmptyHolding(AccountID const& accountID, XRPAmount priorBalance, beast::Journal journal)
        override;

    [[nodiscard]] TER
    removeEmptyHolding(AccountID const& accountID, beast::Journal journal) override;
};

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
    uint256 const& uIndex,             // ripple state entry
    WritableAccountRoot& wrappedAcct,  // the account being set.
    bool const bAuth,                  // authorize account.
    bool const bNoRipple,              // others cannot ripple through
    bool const bFreeze,                // funds cannot leave
    bool bDeepFreeze,                  // can neither receive nor send funds
    STAmount const& saBalance,         // balance of account being set.
                                       // Issuer should be noAccount()
    STAmount const& saLimit,           // limit for account being set.
                                       // Issuer should be the account being set.
    std::uint32_t uQualityIn,
    std::uint32_t uQualityOut,
    beast::Journal j);

[[nodiscard]] TER
trustDelete(
    ApplyView& view,
    std::shared_ptr<SLE> const& sleRippleState,
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
    std::shared_ptr<SLE> sleState,
    std::optional<AccountID> const& ammAccountID,
    beast::Journal j);

}  // namespace xrpl
