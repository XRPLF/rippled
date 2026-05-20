#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/IOUAmount.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/UintTypes.h>

//------------------------------------------------------------------------------
//
// RippleState (Trustline) helpers
//
//------------------------------------------------------------------------------

namespace xrpl {

//------------------------------------------------------------------------------
//
// IOUIssuance ledger entry wrapper (view-parameterized)
//
//------------------------------------------------------------------------------

/**
 * View-parameterized wrapper for an IOU issuance.
 *
 * Because IOUs have no dedicated ledger entry — issuance properties live on
 * the issuer's AccountRoot — IOUIssuance<V> inherits from AccountRoot<V> and
 * adds the currency context plus IOU-specific accessors (RequireAuth /
 * AllowTrustLineClawback flags, freeze checks against the holder's trust
 * line, etc.).
 *
 * IOUIssuance<ReadView>  — read-only
 * IOUIssuance<ApplyView> — read-write
 */
template <typename ViewT>
class IOUIssuance : public AccountRoot<ViewT>, public TokenBase<ViewT>
{
    static constexpr bool kIsWritable = SLEBase<ViewT>::kIsWritable;

    Currency const currency_;

public:
    /** Constructor for read-only context (Issue). */
    IOUIssuance(
        ReadView const& view,
        Issue const& issue,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        requires(!kIsWritable)
        : AccountRoot<ViewT>(issue.getIssuer(), view, j), currency_(issue.currency)
    {
    }

    /** Constructor for read-only context (issuer + currency). */
    IOUIssuance(
        ReadView const& view,
        AccountID const& issuer,
        Currency const& currency,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        requires(!kIsWritable)
        : AccountRoot<ViewT>(issuer, view, j), currency_(currency)
    {
    }

    /** Constructor for writable context (Issue). */
    IOUIssuance(
        ApplyView& view,
        Issue const& issue,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        requires kIsWritable
        : AccountRoot<ViewT>(issue.getIssuer(), view, j), currency_(issue.currency)
    {
    }

    /** Constructor for writable context (issuer + currency). */
    IOUIssuance(
        ApplyView& view,
        AccountID const& issuer,
        Currency const& currency,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        requires kIsWritable
        : AccountRoot<ViewT>(issuer, view, j), currency_(currency)
    {
    }

    /** Converting constructor: writable → read-only. */
    template <WritableView OtherViewT>
    IOUIssuance(IOUIssuance<OtherViewT> const& other)
        requires(!kIsWritable)
        : AccountRoot<ViewT>(other), currency_(other.getCurrency())
    {
    }

    [[nodiscard]] Currency const&
    getCurrency() const
    {
        return currency_;
    }

    [[nodiscard]] Issue
    getIssue() const
    {
        return Issue{currency_, this->id()};
    }

    // --- IOU-specific domain methods ---

    /** Returns IOU issuer transfer fee as Rate (delegates to AccountRoot). */
    [[nodiscard]] Rate
    transferRate() const override
    {
        return AccountRoot<ViewT>::transferRate();
    }

    /** Check if the issuer is globally frozen (delegates to AccountRoot). */
    [[nodiscard]] bool
    isGlobalFrozen() const override
    {
        return AccountRoot<ViewT>::isGlobalFrozen();
    }

    /** Check if the issuer requires holder authorization (lsfRequireAuth). */
    [[nodiscard]] bool
    requiresAuth() const override;

    /** Check if the issuer has enabled clawback (lsfAllowTrustLineClawback). */
    [[nodiscard]] bool
    canClawback() const override;

    /** True if the trust line between `account` and the issuer has the
     *  issuer-side freeze flag set.
     */
    [[nodiscard]] bool
    isIndividualFrozen(AccountID const& account) const override;

    /** True if the issuance is globally frozen or the trust line is frozen.
     *  `depth` is ignored for IOUs (no vault recursion). */
    [[nodiscard]] bool
    isFrozen(AccountID const& account, int depth = 0) const override;

    /** True if the trust line has the issuer-side deep-freeze flag set. */
    [[nodiscard]] bool
    isDeepFrozen(AccountID const& account) const;

    /** Check if `account` is allowed to hold this IOU.
     *  `depth` is ignored for IOUs (no vault recursion). */
    [[nodiscard]] TER
    requireAuth(AccountID const& account, AuthType authType = AuthType::Legacy, int depth = 0)
        const override;

    /** Check if `from` is allowed to send this IOU to `to`. */
    [[nodiscard]] TER
    canTransfer(AccountID const& from, AccountID const& to) const override;

    // --- Credit / balance ---

    /** Maximum amount of this IOU that `account` can hold. */
    [[nodiscard]] STAmount
    creditLimit(AccountID const& account) const;

    [[nodiscard]] IOUAmount
    creditLimit2(AccountID const& account) const;

    /** Amount of this IOU held by `account`. */
    [[nodiscard]] STAmount
    creditBalance(AccountID const& account) const;

    // --- Trust line operations (writable) ---

    /** Create a trust line for this IOU's currency. */
    [[nodiscard]] TER
    trustCreate(
        bool const bSrcHigh,
        AccountID const& uSrcAccountID,
        AccountID const& uDstAccountID,
        uint256 const& uIndex,
        WAccountRoot& wrappedAcct,
        bool const bAuth,
        bool const bNoRipple,
        bool const bFreeze,
        bool bDeepFreeze,
        STAmount const& saBalance,
        STAmount const& saLimit,
        std::uint32_t uQualityIn,
        std::uint32_t uQualityOut,
        beast::Journal j)
        requires kIsWritable;

    /** Add an empty trust line for `accountID`. */
    [[nodiscard]] TER
    addEmptyHolding(AccountID const& accountID, XRPAmount priorBalance, beast::Journal journal)
        requires kIsWritable;

    /** Remove an empty trust line for `accountID`. */
    [[nodiscard]] TER
    removeEmptyHolding(AccountID const& accountID, beast::Journal journal)
        requires kIsWritable;

    /** Issue `amount` of this IOU from the issuer to `account`. */
    [[nodiscard]] TER
    issue(AccountID const& account, STAmount const& amount, beast::Journal j)
        requires kIsWritable;

    /** Redeem `amount` of this IOU from `account` back to the issuer. */
    [[nodiscard]] TER
    redeem(AccountID const& account, STAmount const& amount, beast::Journal j)
        requires kIsWritable;
};

// CTAD deduction guides — bare IOUIssuance(view, ...) always deduces read-only.
// For writable access, use WIOUIssuance(view, ...) explicitly.
IOUIssuance(ReadView const&, Issue const&) -> IOUIssuance<ReadView>;
IOUIssuance(ReadView const&, Issue const&, beast::Journal) -> IOUIssuance<ReadView>;
IOUIssuance(ReadView const&, AccountID const&, Currency const&) -> IOUIssuance<ReadView>;
IOUIssuance(ReadView const&, AccountID const&, Currency const&, beast::Journal)
    -> IOUIssuance<ReadView>;

// Backward-compatible aliases
using RIOUIssuance = IOUIssuance<ReadView>;
using WIOUIssuance = IOUIssuance<ApplyView>;

// Explicit instantiation declarations (definitions in .cpp)
extern template class IOUIssuance<ReadView>;
extern template class IOUIssuance<ApplyView>;

//------------------------------------------------------------------------------
//
// Trust line operations (SLE-level, no issuance context)
//
//------------------------------------------------------------------------------

[[nodiscard]] TER
trustDelete(
    ApplyView& view,
    std::shared_ptr<SLE> const& sleRippleState,
    AccountID const& uLowAccountID,
    AccountID const& uHighAccountID,
    beast::Journal j);

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
