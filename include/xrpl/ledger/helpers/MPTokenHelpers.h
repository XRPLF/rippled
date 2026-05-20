#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/SLEBase.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Rate.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>

#include <initializer_list>
#include <memory>
#include <optional>

namespace xrpl {

//------------------------------------------------------------------------------
//
// MPTokenIssuance ledger entry wrapper (view-parameterized)
//
//------------------------------------------------------------------------------

/**
 * View-parameterized wrapper for MPTokenIssuance ledger entries.
 *
 * MPTokenIssuance<ReadView>  — read-only access to issuance data
 * MPTokenIssuance<ApplyView> — read-write access, with insert/update/erase
 *                              and domain-specific write methods
 *
 * Carries the MPTID and MPTIssue alongside the SLE so callers don't need to
 * thread them through every method call.
 */
template <typename ViewT>
class MPTokenIssuance : public SLEBase<ViewT>, public TokenBase<ViewT>
{
    static constexpr bool kIsWritable = SLEBase<ViewT>::kIsWritable;

    MPTID const mptID_;
    MPTIssue const mptIssue_;

public:
    /** Constructor for read-only context (MPTIssue) */
    MPTokenIssuance(
        ReadView const& view,
        MPTIssue const& mptIssue,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        requires(!kIsWritable)
        : SLEBase<ViewT>(view.read(keylet::mptIssuance(mptIssue.getMptID())), view, j)
        , mptID_(mptIssue.getMptID())
        , mptIssue_(mptIssue)
    {
    }

    /** Constructor for read-only context (MPTID) */
    MPTokenIssuance(
        ReadView const& view,
        MPTID const& mptID,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        requires(!kIsWritable)
        : SLEBase<ViewT>(view.read(keylet::mptIssuance(mptID)), view, j)
        , mptID_(mptID)
        , mptIssue_(MPTIssue(mptID))
    {
    }

    /** Constructor for writable context (MPTIssue) */
    MPTokenIssuance(
        ApplyView& view,
        MPTIssue const& mptIssue,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        requires kIsWritable
        : SLEBase<ViewT>(keylet::mptIssuance(mptIssue.getMptID()), view, j)
        , mptID_(mptIssue.getMptID())
        , mptIssue_(mptIssue)
    {
    }

    /** Constructor for writable context (MPTID) */
    MPTokenIssuance(
        ApplyView& view,
        MPTID const& mptID,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        requires kIsWritable
        : SLEBase<ViewT>(keylet::mptIssuance(mptID), view, j)
        , mptID_(mptID)
        , mptIssue_(MPTIssue(mptID))
    {
    }

    /** Converting constructor: writable → read-only. */
    template <WritableView OtherViewT>
    MPTokenIssuance(MPTokenIssuance<OtherViewT> const& other)
        requires(!kIsWritable)
        : SLEBase<ViewT>(other), mptID_(other.getMptID()), mptIssue_(other.getMptIssue())
    {
    }

    /** Create an MPTokenIssuance backed by a brand-new SLE (already inserted into
     *  the view).
     */
    [[nodiscard]] static MPTokenIssuance
    makeNew(
        MPTID const& mptID,
        ApplyView& view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        requires kIsWritable
    {
        return MPTokenIssuance(mptID, view, j, std::make_shared<SLE>(keylet::mptIssuance(mptID)));
    }

    [[nodiscard]] static MPTokenIssuance
    makeNew(
        std::uint32_t const seq,
        AccountID const& issuer,
        ApplyView& view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        requires kIsWritable
    {
        auto const mptID = makeMptID(seq, issuer);
        return MPTokenIssuance(mptID, view, j, std::make_shared<SLE>(keylet::mptIssuance(mptID)));
    }

    // --- Inline accessors ---

    [[nodiscard]] MPTID const&
    getMptID() const
    {
        return mptID_;
    }

    [[nodiscard]] MPTIssue const&
    getMptIssue() const
    {
        return mptIssue_;
    }

    [[nodiscard]] AccountID const&
    getIssuer() const
    {
        return mptIssue_.getIssuer();
    }

    // --- Read-only domain methods (available on both specializations) ---

    [[nodiscard]] bool
    isGlobalFrozen() const override;

    [[nodiscard]] bool
    isIndividualFrozen(AccountID const& account) const override;

    /**
     *   isFrozen check is recursive for MPT shares in a vault, descending to
     *   assets in the vault, up to kMaxAssetCheckDepth recursion depth. This is
     *   purely defensive, as we currently do not allow such vaults to be created.
     */
    [[nodiscard]] bool
    isFrozen(AccountID const& account, int depth = 0) const override;

    [[nodiscard]] bool
    isAnyFrozen(std::initializer_list<AccountID> const& accounts, int depth = 0) const;

    /** Returns MPT transfer fee as Rate. */
    [[nodiscard]] Rate
    transferRate() const override;

    [[nodiscard]] TER
    canAddHolding() const;

    /** Check if the account lacks required authorization for MPT. */
    [[nodiscard]] TER
    requireAuth(AccountID const& account, AuthType authType = AuthType::Legacy, int depth = 0)
        const override;

    /** Check if `from` is allowed to send MPT to `to`. */
    [[nodiscard]] TER
    canTransfer(AccountID const& from, AccountID const& to) const override;

    /** Check if the token requires authorization for holders. */
    [[nodiscard]] bool
    requiresAuth() const override;

    /** Check if the token issuer has enabled clawback capability. */
    [[nodiscard]] bool
    canClawback() const override;

    // --- Write-only domain methods (compile-time gated) ---

    [[nodiscard]] TER
    addEmptyHolding(AccountID const& accountID, XRPAmount priorBalance, beast::Journal journal)
        requires kIsWritable;

    [[nodiscard]] TER
    removeEmptyHolding(AccountID const& accountID, beast::Journal journal)
        requires kIsWritable;

    [[nodiscard]] TER
    authorizeMPToken(
        XRPAmount const& priorBalance,
        AccountID const& account,
        beast::Journal journal,
        std::uint32_t flags = 0,
        std::optional<AccountID> holderID = std::nullopt)
        requires kIsWritable;

    /** Enforce account has MPToken to match its authorization.
     *
     *   Called from doApply - it will check for expired (and delete if found any)
     *   credentials matching DomainID set in MPTokenIssuance. Must be called if
     *   requireAuth(...) returned tesSUCCESS or tecEXPIRED in preclaim.
     */
    [[nodiscard]] TER
    enforceMPTokenAuthorization(
        AccountID const& account,
        XRPAmount const& priorBalance,
        beast::Journal j)
        requires kIsWritable;

    // --- Amount accessors ---

    /** Maximum amount allowed for this issuance (sfMaximumAmount or default). */
    [[nodiscard]] std::int64_t
    maxAmount() const;

    /** Available amount = maxAmount - OutstandingAmount.
     *  Throws if the issuance SLE does not exist.
     */
    [[nodiscard]] std::int64_t
    availableAmount() const;

    /** Funds available for the issuer to sell in an issuer-owned offer. */
    [[nodiscard]] STAmount
    issuerFundsToSelfIssue() const;

    // --- Holder MPToken management (writable) ---

    /** Create a holder-side MPToken for `account` under this issuance. */
    [[nodiscard]] TER
    createMPToken(AccountID const& account, std::uint32_t flags)
        requires kIsWritable;

    /** Ensure `holder` has an MPToken under this issuance, creating one if needed. */
    [[nodiscard]] TER
    checkCreateMPT(AccountID const& holder, beast::Journal j)
        requires kIsWritable;

    /** Notify the view that the issuer self-debited `amount`. */
    void
    issuerSelfDebitHook(std::uint64_t amount)
        requires kIsWritable;

    /** Lock MPT for escrow from `sender`. The amount must match this issuance. */
    [[nodiscard]] TER
    lockEscrow(AccountID const& sender, STAmount const& amount, beast::Journal j)
        requires kIsWritable;

    /** Unlock previously-escrowed MPT, transferring `netAmount` to `receiver`,
     *  with `grossAmount - netAmount` accounted as a transfer fee against
     *  OutstandingAmount.
     */
    [[nodiscard]] TER
    unlockEscrow(
        AccountID const& sender,
        AccountID const& receiver,
        STAmount const& netAmount,
        STAmount const& grossAmount,
        beast::Journal j)
        requires kIsWritable;

private:
    // Private constructor only used by `makeNew`.
    MPTokenIssuance(MPTID const& mptID, ApplyView& view, beast::Journal j, std::shared_ptr<SLE> sle)
        requires kIsWritable
        : SLEBase<ViewT>(std::move(sle), view, j), mptID_(mptID), mptIssue_(MPTIssue(mptID))
    {
        this->insert();
    }
};

// CTAD deduction guides — bare MPTokenIssuance(view, ...) always deduces read-only.
// For writable access, use WMPTokenIssuance(view, ...) explicitly.
MPTokenIssuance(ReadView const&, MPTIssue const&) -> MPTokenIssuance<ReadView>;
MPTokenIssuance(ReadView const&, MPTIssue const&, beast::Journal) -> MPTokenIssuance<ReadView>;
MPTokenIssuance(ReadView const&, MPTID const&) -> MPTokenIssuance<ReadView>;
MPTokenIssuance(ReadView const&, MPTID const&, beast::Journal) -> MPTokenIssuance<ReadView>;

// Backward-compatible aliases
using RMPTokenIssuance = MPTokenIssuance<ReadView>;
using WMPTokenIssuance = MPTokenIssuance<ApplyView>;

// Explicit instantiation declarations (definitions in .cpp)
extern template class MPTokenIssuance<ReadView>;
extern template class MPTokenIssuance<ApplyView>;

/** Check if Asset can be traded on DEX. return tecNO_PERMISSION
 * if it doesn't and tesSUCCESS otherwise.
 */
[[nodiscard]] TER
canTrade(ReadView const& view, Asset const& asset);

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

/** Delete AMMs MPToken. The passed `sle` must be obtained from a prior
 * call to view.peek().
 */
[[nodiscard]] TER
deleteAMMMPToken(
    ApplyView& view,
    std::shared_ptr<SLE> sleMPT,
    AccountID const& ammAccountID,
    beast::Journal j);

//------------------------------------------------------------------------------
//
// MPT DEX
//
//------------------------------------------------------------------------------

/* Return true if a transaction is allowed for the specified MPT/account. The
 * function checks MPTokenIssuance and MPToken objects flags to determine if the
 * transaction is allowed.
 */
TER
checkMPTTxAllowed(ReadView const& v, TxType tx, Asset const& asset, AccountID const& accountID);

}  // namespace xrpl
