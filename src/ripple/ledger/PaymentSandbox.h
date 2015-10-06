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

#include <ripple/ledger/RawView.h>
#include <ripple/ledger/Sandbox.h>
#include <ripple/ledger/detail/ApplyViewBase.h>
#include <ripple/protocol/AccountID.h>
#include <map>
#include <utility>

namespace ripple {

namespace detail {

// VFALCO TODO Inline this implementation
//        into the PaymentSandbox class itself
class DeferredCredits
{
public:
    // Get the adjusted balance of main for the
    // balance between main and other.
    STAmount
    adjustedBalance (AccountID const& main,
        AccountID const& other,
            STAmount const& curBalance) const;

    void credit (AccountID const& sender,
                 AccountID const& receiver,
                 STAmount const& amount);

    void apply (DeferredCredits& to);

    // VFALCO Is this needed?
    // DEPRECATED
    void clear ();

private:
    // lowAccount, highAccount
    using Key = std::tuple<
        AccountID, AccountID, Currency>;

    // lowAccountCredits, highAccountCredits
    using Value = std::tuple<
        STAmount, STAmount>;

    static
    Key
    makeKey (AccountID const& a1,
        AccountID const& a2,
            Currency const& c);

    std::map<Key, Value> map_;
};

} // detail

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
class PaymentSandbox
    : public detail::ApplyViewBase
{
public:
    PaymentSandbox() = delete;
    PaymentSandbox (PaymentSandbox const&) = delete;
    PaymentSandbox& operator= (PaymentSandbox&&) = delete;
    PaymentSandbox& operator= (PaymentSandbox const&) = delete;

    PaymentSandbox (PaymentSandbox&&) = default;

    PaymentSandbox (ReadView const* base, ApplyFlags flags)
        : ApplyViewBase (base, flags)
    {
    }

    PaymentSandbox (ApplyView const* base)
        : ApplyViewBase (base, base->flags())
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
    explicit
    PaymentSandbox (PaymentSandbox const* base)
        : ApplyViewBase(base, base->flags())
        , ps_ (base)
    {
    }

    explicit
    PaymentSandbox (PaymentSandbox* base)
        : ApplyViewBase(base, base->flags())
        , ps_ (base)
    {
    }
    /** @} */

    STAmount
    balanceHook (AccountID const& account,
        AccountID const& issuer,
            STAmount const& amount) const override;

    void
    creditHook (AccountID const& from,
        AccountID const& to,
            STAmount const& amount) override;

    /** Apply changes to base view.

        `to` must contain contents identical to the parent
        view passed upon construction, else undefined
        behavior will result.
    */
    /** @{ */
    void
    apply (RawView& to);

    void
    apply (PaymentSandbox& to);
    /** @} */

private:
    detail::DeferredCredits tab_;
    PaymentSandbox const* ps_ = nullptr;
};

}  // ripple

#endif
