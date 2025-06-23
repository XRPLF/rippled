//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef RIPPLE_LEDGER_PAYMENTSANDBOX_H_INCLUDED
#define RIPPLE_LEDGER_PAYMENTSANDBOX_H_INCLUDED

#include <xrpld/ledger/RawView.h>
#include <xrpld/ledger/Sandbox.h>
#include <xrpld/ledger/detail/ApplyViewBase.h>

#include <xrpl/protocol/AccountID.h>

#include <map>
#include <utility>

namespace ripple {

namespace detail {

// VFALCO TODO Inline this implementation
//        into the PaymentSandbox class itself
class DeferredCredits
{
private:
    using KeyIOU = std::tuple<AccountID, AccountID, Currency>;
    struct ValueIOU
    {
        explicit ValueIOU() = default;

        STAmount lowAcctCredits;
        STAmount highAcctCredits;
        STAmount lowAcctOrigBalance;
    };

    struct HolderValueMPT
    {
        HolderValueMPT() = default;
        // Debit to issuer
        std::uint64_t debit = 0;
        std::uint64_t origBalance = 0;
    };

    struct IssuerValueMPT
    {
        IssuerValueMPT() = default;
        std::map<AccountID, HolderValueMPT> holders;
        // Credit to holder
        std::uint64_t credit = 0;
        // OutstandingAmount might overflow when MPTs are credited to a holder.
        // Consider A1 paying 100MPT to A2 and A1 already having maximum MPTs.
        // Since the payment engine executes a payment in revers, A2 is
        // credited first and OutstandingAmount is going to be equal
        // to MaximumAmount + 100MPT. In the next step A1 redeems 100MPT
        // to the issuer and OutstandingAmount balances out.
        std::int64_t origBalance = 0;
        // Self debit on offer selling MPT. Since the payment engine executes
        // a payment in revers, a crediting/buying step may overflow
        // OutstandingAmount. A sell MPT offer owned by a holder can redeem any
        // amount up to the offer's amount and holder's available funds,
        // balancing out OutstandingAmount. But if the offer's owner is issuer
        // then it issues more MPT. In this case the available amount to issue
        // is the initial issuer's available amount less all offer sell amounts
        // by the issuer. This is self-debit, where the offer's owner,
        // issuer in this case, debits to self.
        std::uint64_t selfDebit = 0;
    };
    using AdjustmentMPT = IssuerValueMPT;

public:
    struct AdjustmentIOU
    {
        AdjustmentIOU(STAmount const& d, STAmount const& c, STAmount const& b)
            : debits(d), credits(c), origBalance(b)
        {
        }
        STAmount debits;
        STAmount credits;
        STAmount origBalance;
    };

    // Get the adjustments for the balance between main and other.
    // Returns the debits, credits and the original balance
    std::optional<AdjustmentIOU>
    adjustmentsIOU(
        AccountID const& main,
        AccountID const& other,
        Currency const& currency) const;

    std::optional<AdjustmentMPT>
    adjustmentsMPT(MPTID const& mptID) const;

    void
    creditIOU(
        AccountID const& sender,
        AccountID const& receiver,
        STAmount const& amount,
        STAmount const& preCreditSenderBalance);

    void
    creditMPT(
        AccountID const& sender,
        AccountID const& receiver,
        STAmount const& amount,
        std::uint64_t preCreditBalanceHolder,
        std::int64_t preCreditBalanceIssuer);

    void
    issuerSelfDebitMPT(
        MPTIssue const& issue,
        std::uint64_t amount,
        std::int64_t origBalance);

    void
    ownerCount(AccountID const& id, std::uint32_t cur, std::uint32_t next);

    // Get the adjusted owner count. Since DeferredCredits is meant to be used
    // in payments, and payments only decrease owner counts, return the max
    // remembered owner count.
    std::optional<std::uint32_t>
    ownerCount(AccountID const& id) const;

    void
    apply(DeferredCredits& to);

private:
    static KeyIOU
    makeKeyIOU(
        AccountID const& a1,
        AccountID const& a2,
        Currency const& currency);

    std::map<KeyIOU, ValueIOU> creditsIOU_;
    std::map<MPTID, IssuerValueMPT> creditsMPT_;
    std::map<AccountID, std::uint32_t> ownerCounts_;
};

}  // namespace detail

//------------------------------------------------------------------------------

/** A wrapper which makes credits unavailable to balances.

    This is used for payments and pathfinding, so that consuming
    liquidity from a path never causes portions of that path or
    other paths to gain liquidity.

    The behavior of certain free functions in the ApplyView API
    will change via the balanceHook and creditHook overrides
    of PaymentSandbox.

    @note Presented as ApplyView to clients
*/
class PaymentSandbox final : public detail::ApplyViewBase
{
public:
    PaymentSandbox() = delete;
    PaymentSandbox(PaymentSandbox const&) = delete;
    PaymentSandbox&
    operator=(PaymentSandbox&&) = delete;
    PaymentSandbox&
    operator=(PaymentSandbox const&) = delete;

    PaymentSandbox(PaymentSandbox&&) = default;

    PaymentSandbox(ReadView const* base, ApplyFlags flags)
        : ApplyViewBase(base, flags)
    {
    }

    PaymentSandbox(ApplyView const* base) : ApplyViewBase(base, base->flags())
    {
    }

    /** Construct on top of existing PaymentSandbox.

        The changes are pushed to the parent when
        apply() is called.

        @param parent A non-null pointer to the parent.

        @note A pointer is used to prevent confusion
              with copy construction.
    */
    // VFALCO If we are constructing on top of a PaymentSandbox,
    //        or a PaymentSandbox-derived class, we MUST go through
    //        one of these constructors or invariants will be broken.
    /** @{ */
    explicit PaymentSandbox(PaymentSandbox const* base)
        : ApplyViewBase(base, base->flags()), ps_(base)
    {
    }

    explicit PaymentSandbox(PaymentSandbox* base)
        : ApplyViewBase(base, base->flags()), ps_(base)
    {
    }
    /** @} */

    STAmount
    balanceHookIOU(
        AccountID const& account,
        AccountID const& issuer,
        STAmount const& amount) const override;

    STAmount
    balanceHookMPT(
        AccountID const& account,
        MPTIssue const& issue,
        std::int64_t amount) const override;

    STAmount
    balanceHookSelfIssueMPT(MPTIssue const& issue, std::int64_t amount)
        const override;

    void
    creditHookIOU(
        AccountID const& from,
        AccountID const& to,
        STAmount const& amount,
        STAmount const& preCreditBalance) override;

    void
    creditHookMPT(
        AccountID const& from,
        AccountID const& to,
        STAmount const& amount,
        std::uint64_t preCreditBalanceHolder,
        std::int64_t preCreditBalanceIssuer) override;

    void
    issuerSelfDebitHookMPT(
        MPTIssue const& issue,
        std::uint64_t amount,
        std::int64_t origBalance) override;

    void
    adjustOwnerCountHook(
        AccountID const& account,
        std::uint32_t cur,
        std::uint32_t next) override;

    std::uint32_t
    ownerCountHook(AccountID const& account, std::uint32_t count)
        const override;

    /** Apply changes to base view.

        `to` must contain contents identical to the parent
        view passed upon construction, else undefined
        behavior will result.
    */
    /** @{ */
    void
    apply(RawView& to);

    void
    apply(PaymentSandbox& to);
    /** @} */

    // Return a map of balance changes on trust lines. The low account is the
    // first account in the key. If the two accounts are equal, the map contains
    // the total changes in currency regardless of issuer. This is useful to get
    // the total change in XRP balances.
    std::map<std::tuple<AccountID, AccountID, Currency>, STAmount>
    balanceChanges(ReadView const& view) const;

    XRPAmount
    xrpDestroyed() const;

private:
    detail::DeferredCredits tab_;
    PaymentSandbox const* ps_ = nullptr;
};

}  // namespace ripple

#endif
