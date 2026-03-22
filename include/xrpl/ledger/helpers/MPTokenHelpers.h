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

class MPTokenIssuance : public virtual TokenBase
{
public:
    MPTokenIssuance(ReadView const& view, MPTIssue const& mptIssue)
        : ReadOnlySLE(view.read(keylet::mptIssuance(mptIssue.getMptID())), view)
        , TokenBase(view, view.read(keylet::mptIssuance(mptIssue.getMptID())))
        , mptID_(mptIssue.getMptID())
        , mptIssue_(mptIssue)
    {
    }

    MPTokenIssuance(ReadView const& view, MPTID const& mptID)
        : ReadOnlySLE(view.read(keylet::mptIssuance(mptID)), view)
        , TokenBase(view, view.read(keylet::mptIssuance(mptID)))
        , mptID_(mptID)
        , mptIssue_(MPTIssue(mptID_))
    {
    }

    MPTID const&
    getMptID() const
    {
        return mptID_;
    }

    MPTIssue const&
    getMptIssue() const
    {
        return mptIssue_;
    }

    AccountID const&
    getIssuer() const
    {
        return mptIssue_.getIssuer();
    }

    //------------------------------------------------------------------------------
    //
    // Freeze checking (MPT-specific)
    //
    //------------------------------------------------------------------------------

    [[nodiscard]] bool
    isGlobalFrozen() const override;

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
    transferRate() const override;

    //------------------------------------------------------------------------------
    //
    // Holding checks (MPT-specific)
    //
    //------------------------------------------------------------------------------

    [[nodiscard]] TER
    canAddHolding() const override;

    /** Check if the account lacks required authorization for MPT.
     *
     * requireAuth check is recursive for MPT shares in a vault, descending to
     * assets in the vault, up to maxAssetCheckDepth recursion depth. This is
     * purely defensive, as we currently do not allow such vaults to be created.
     */
    [[nodiscard]] TER
    requireAuth(AccountID const& account, AuthType authType = AuthType::Legacy, int depth = 0)
        const override;

    /** Check if the destination account is allowed
     *  to receive MPT. Return tecNO_AUTH if it doesn't
     *  and tesSUCCESS otherwise.
     */
    [[nodiscard]] TER
    canTransfer(AccountID const& from, AccountID const& to) const override;

    //------------------------------------------------------------------------------
    //
    // Token capability checks (MPT-specific)
    //
    //------------------------------------------------------------------------------

    [[nodiscard]] bool
    canClawback() const override;

    [[nodiscard]] bool
    requiresAuth() const override;

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

protected:
    MPTID const mptID_;
    MPTIssue const mptIssue_;
};

class WritableMPTokenIssuance : public virtual WritableTokenBase, public virtual MPTokenIssuance
{
public:
    WritableMPTokenIssuance(ApplyView& view, MPTIssue const& mptIssue)
        : ReadOnlySLE(view.peek(keylet::mptIssuance(mptIssue.getMptID())), view)
        , TokenBase(view, view.peek(keylet::mptIssuance(mptIssue.getMptID())))
        , WritableSLE(view.peek(keylet::mptIssuance(mptIssue.getMptID())), view)
        , WritableTokenBase(view, view.peek(keylet::mptIssuance(mptIssue.getMptID())))
        , MPTokenIssuance(view, mptIssue)
    {
    }

    WritableMPTokenIssuance(ApplyView& view, MPTID const& mptID)
        : ReadOnlySLE(view.peek(keylet::mptIssuance(mptID)), view)
        , TokenBase(view, view.peek(keylet::mptIssuance(mptID)))
        , WritableSLE(view.peek(keylet::mptIssuance(mptID)), view)
        , WritableTokenBase(view, view.peek(keylet::mptIssuance(mptID)))
        , MPTokenIssuance(view, mptID)
    {
    }

    // Resolve ambiguity: use writable operator-> for non-const, read-only for const
    using WritableSLE::operator->;
    using MPTokenIssuance::operator->;
    using WritableSLE::operator*;
    using MPTokenIssuance::operator*;

    //------------------------------------------------------------------------------
    //
    // Authorization (MPT-specific)
    //
    //------------------------------------------------------------------------------

    [[nodiscard]] TER
    authorizeMPToken(
        XRPAmount const& priorBalance,
        AccountID const& account,
        beast::Journal journal,
        std::uint32_t flags = 0,
        std::optional<AccountID> holderID = std::nullopt);

    /** Enforce account has MPToken to match its authorization.
     *
     *   Called from doApply - it will check for expired (and delete if found any)
     *   credentials matching DomainID set in MPTokenIssuance. Must be called if
     *   requireAuth(...MPTIssue...) returned tesSUCCESS or tecEXPIRED in preclaim.
     */
    [[nodiscard]] TER
    enforceMPTokenAuthorization(
        AccountID const& account,
        XRPAmount const& priorBalance,
        beast::Journal j);

    //------------------------------------------------------------------------------
    //
    // Empty holding operations (MPT-specific)
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
// Escrow operations (MPT-specific)
//
//------------------------------------------------------------------------------

TER
rippleLockEscrowMPT(
    ApplyView& view,
    AccountID const& uGrantorID,
    STAmount const& saAmount,
    beast::Journal j);

TER
rippleUnlockEscrowMPT(
    ApplyView& view,
    AccountID const& uGrantorID,
    AccountID const& uGranteeID,
    STAmount const& netAmount,
    STAmount const& grossAmount,
    beast::Journal j);

}  // namespace xrpl
