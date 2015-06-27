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

#ifndef RIPPLE_APP_PAYMENTVIEW_H_INCLUDED
#define RIPPLE_APP_PAYMENTVIEW_H_INCLUDED

#include <ripple/core/Config.h>
#include <ripple/app/ledger/MetaView.h>
#include <ripple/ledger/View.h>
#include <ripple/ledger/DeferredCredits.h>
#include <beast/cxx14/type_traits.h> // <type_traits>
#include <utility>

namespace ripple {

/** A View wrapper which makes credits unavailable to balances.

    This is used for payments and pathfinding, so that consuming
    liquidity from a path never causes portions of that path or
    other paths to gain liquidity.

    The behavior of certain free functions in the View API
    will change via the balanceHook and creditHook overrides
    of PaymentView.
*/
class PaymentView : public ViewWrapper<MetaView>
{
private:
    DeferredCredits tab_;
    PaymentView const* pv_ = nullptr;

public:
    PaymentView (PaymentView const&) = delete;
    PaymentView& operator= (PaymentView const&) = delete;

    /** Construct contained MetaView from arguments */
    template <class... Args>
    explicit
    PaymentView (Args&&... args)
        : ViewWrapper (std::forward<Args>(args)...)
    {
    }

    /** Construct on top of existing PaymentView.

        The changes are pushed to the parent when
        apply() is called.

        @param parent A non-null pointer to the parent.

        @note A pointer is used to prevent confusion
              with copy construction.
    */
    // VFALCO If we are constructing on top of a PaymentView,
    //        or a PaymentView-derived class, we MUST go through
    //        one of these constructors or invariants will be broken.
    /** @{ */
    explicit
    PaymentView (PaymentView const* parent)
        : ViewWrapper (*parent, parent->flags())
        , pv_ (parent)
    {
    }

    explicit
    PaymentView (PaymentView* parent)
        : ViewWrapper (*parent, parent->flags())
        , pv_ (parent)
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

    /** Apply changes to the parent View.

        `to` must contain contents identical to the parent
        view passed upon construction, else undefined
        behavior will result.

        After a call to apply(), the only valid operation that
        may be performed on this is a call to the destructor.
    */
    /** @{ */
    void
    apply (BasicView& to);

    void
    apply (PaymentView& to);
    /** @} */
};

}  // ripple

#endif
