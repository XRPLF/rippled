#pragma once

#include <xrpl/ledger/LedgerEntryViewBase.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/STAmount.h>

#include <cstdint>

namespace xrpl {

/**
 * A view over a RippleState (trustline) ledger entry from the perspective
 * of a specific account.
 *
 * RippleState entries store two accounts in sorted order (low < high),
 * which means code must constantly check which account is which and select
 * the appropriate flags/fields. This class encapsulates that complexity,
 * presenting a clean "my" vs "peer" interface.
 *
 * Example usage:
 * @code
 *     RippleStateView rs(view, holder, issuer, currency);
 *     if (!rs)
 *         return tecNO_LINE;
 *     if (rs.getFreezePeer())  // Has the peer frozen us?
 *         return tecFROZEN;
 *     auto limit = rs.getLimit();  // My limit
 * @endcode
 *
 * @note This class is modeled after TrustLineBase but lives in libxrpl
 *       so it can be used by transactors and other library code.
 */
class RippleStateView : public LedgerEntryViewBase
{
public:
    /**
     * Construct a view of the trustline between two accounts.
     * @param view The ledger view to read from
     * @param account The perspective account ("my" side)
     * @param peer The counterparty account ("peer" side)
     * @param currency The currency of the trustline
     */
    RippleStateView(
        ReadView const& view,
        AccountID const& account,
        AccountID const& peer,
        Currency const& currency);

    /**
     * Construct a view from an existing SLE.
     * @param sle The RippleState SLE (must be ltRIPPLE_STATE or nullptr)
     * @param account The perspective account ("my" side)
     * @param peer The counterparty account ("peer" side)
     * @note The peer is needed to reliably determine which side the account
     *       is on, since the issuer field of the limit amounts can be
     *       temporarily modified by some transactions (e.g., CashCheck).
     */
    RippleStateView(
        std::shared_ptr<SLE const> sle,
        AccountID const& account,
        AccountID const& peer);

    /** Returns the state map key for the ledger entry. */
    uint256 const&
    key() const
    {
        return key_;
    }

    /** Returns the perspective account ID */
    AccountID const&
    getAccountID() const;

    /** Returns the peer account ID */
    AccountID const&
    getAccountIDPeer() const;

    // === Authorization ===

    /** True if we have authorized the peer */
    bool
    getAuth() const;

    /** True if the peer has authorized us */
    bool
    getAuthPeer() const;

    // === No Ripple ===

    /** True if we have set NoRipple */
    bool
    getNoRipple() const;

    /** True if the peer has set NoRipple */
    bool
    getNoRipplePeer() const;

    // === Freeze ===

    /** True if we have frozen the peer */
    bool
    getFreeze() const;

    /** True if the peer has frozen us */
    bool
    getFreezePeer() const;

    /** True if we have deep frozen the peer */
    bool
    getDeepFreeze() const;

    /** True if the peer has deep frozen us */
    bool
    getDeepFreezePeer() const;

    // === Balance and Limits ===

    /**
     * Returns the balance from our perspective.
     * Positive means we hold credit from the peer.
     * Negative means we owe the peer.
     */
    STAmount const&
    getBalance() const;

    /** Returns our limit (how much we're willing to hold) */
    STAmount const&
    getLimit() const;

    /** Returns the peer's limit */
    STAmount const&
    getLimitPeer() const;

    // === Quality ===

    /** Returns our quality in setting */
    std::uint32_t
    getQualityIn() const;

    /** Returns our quality out setting */
    std::uint32_t
    getQualityOut() const;

    /** Returns the peer's quality in setting */
    std::uint32_t
    getQualityInPeer() const;

    /** Returns the peer's quality out setting */
    std::uint32_t
    getQualityOutPeer() const;

    // === Reserve ===

    /** True if we have reserve on this line */
    bool
    getReserve() const;

    /** True if the peer has reserve on this line */
    bool
    getReservePeer() const;

    // === Raw access ===

    /** Returns the raw flags */
    std::uint32_t
    getFlags() const
    {
        return flags_;
    }

    /** Returns true if we are the low account */
    bool
    viewLowest() const
    {
        return viewLowest_;
    }

    // === Static helpers for mutation code ===
    // These help code that needs to modify SLEs directly determine
    // which flags/fields to use based on account positions.

    /**
     * Returns true if accountA is the "low" account (accountA < accountB).
     * This determines which set of flags/fields apply to which account.
     */
    [[nodiscard]] static bool
    isLow(AccountID const& accountA, AccountID const& accountB)
    {
        return accountA < accountB;
    }

    /** Returns the freeze flag for the given perspective */
    [[nodiscard]] static std::uint32_t
    freezeFlag(bool isLow)
    {
        return isLow ? lsfLowFreeze : lsfHighFreeze;
    }

    /** Returns the deep freeze flag for the given perspective */
    [[nodiscard]] static std::uint32_t
    deepFreezeFlag(bool isLow)
    {
        return isLow ? lsfLowDeepFreeze : lsfHighDeepFreeze;
    }

    /** Returns the no ripple flag for the given perspective */
    [[nodiscard]] static std::uint32_t
    noRippleFlag(bool isLow)
    {
        return isLow ? lsfLowNoRipple : lsfHighNoRipple;
    }

    /** Returns the reserve flag for the given perspective */
    [[nodiscard]] static std::uint32_t
    reserveFlag(bool isLow)
    {
        return isLow ? lsfLowReserve : lsfHighReserve;
    }

    /** Returns the auth flag for the given perspective */
    [[nodiscard]] static std::uint32_t
    authFlag(bool isLow)
    {
        return isLow ? lsfLowAuth : lsfHighAuth;
    }

    /** Returns the limit field for the given perspective */
    [[nodiscard]] static SField const&
    limitField(bool isLow)
    {
        return isLow ? sfLowLimit : sfHighLimit;
    }

    /** Returns the quality in field for the given perspective */
    [[nodiscard]] static SField const&
    qualityInField(bool isLow)
    {
        return isLow ? sfLowQualityIn : sfHighQualityIn;
    }

    /** Returns the quality out field for the given perspective */
    [[nodiscard]] static SField const&
    qualityOutField(bool isLow)
    {
        return isLow ? sfLowQualityOut : sfHighQualityOut;
    }

    // === Static convenience methods for common queries ===
    // These provide one-shot queries without needing to construct a view object.

    /**
     * Check if an account is frozen on a trustline by the issuer.
     * This checks both global freeze and individual trustline freeze.
     *
     * @param view The ledger view
     * @param account The account to check
     * @param currency The currency
     * @param issuer The issuer of the currency
     * @return true if the account is frozen
     */
    [[nodiscard]] static bool
    isFrozen(
        ReadView const& view,
        AccountID const& account,
        Currency const& currency,
        AccountID const& issuer);

    /**
     * Check if an account is deep frozen on a trustline.
     *
     * @param view The ledger view
     * @param account The account to check
     * @param currency The currency
     * @param issuer The issuer of the currency
     * @return true if the account is deep frozen
     */
    [[nodiscard]] static bool
    isDeepFrozen(
        ReadView const& view,
        AccountID const& account,
        Currency const& currency,
        AccountID const& issuer);

    /**
     * Get the credit limit for an account on a trustline.
     *
     * @param view The ledger view
     * @param account The account whose limit to retrieve
     * @param issuer The peer account
     * @param currency The currency
     * @return The credit limit (zero if no trustline exists)
     */
    [[nodiscard]] static STAmount
    creditLimit(
        ReadView const& view,
        AccountID const& account,
        AccountID const& issuer,
        Currency const& currency);

    /**
     * Get the credit balance for an account on a trustline.
     *
     * @param view The ledger view
     * @param account The account whose balance to retrieve
     * @param issuer The peer account
     * @param currency The currency
     * @return The credit balance (zero if no trustline exists)
     */
    [[nodiscard]] static STAmount
    creditBalance(
        ReadView const& view,
        AccountID const& account,
        AccountID const& issuer,
        Currency const& currency);

protected:
    uint256 key_;
    STAmount lowLimit_;
    STAmount highLimit_;
    STAmount balance_;
    std::uint32_t flags_ = 0;
    std::uint32_t lowQualityIn_ = 0;
    std::uint32_t lowQualityOut_ = 0;
    std::uint32_t highQualityIn_ = 0;
    std::uint32_t highQualityOut_ = 0;
    bool viewLowest_ = false;
};

}  // namespace xrpl
