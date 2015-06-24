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
#include <ripple/ledger/ViewAPIBasics.h>
#include <ripple/ledger/DeferredCredits.h>
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
class PaymentView : public View
{
private:
    MetaView view_;
    DeferredCredits tab_;
    PaymentView* pv_ = nullptr;

public:
    PaymentView (PaymentView const&) = delete;
    PaymentView& operator= (PaymentView const&) = delete;

    /** Construct contained MetaView from arguments */
    template <class... Args>
    explicit
    PaymentView (Args&&... args)
        : view_ (std::forward<Args>(args)...)
    {
    }

    /** Construct on top of existing PaymentView.

        The changes are pushed to the parent when
        apply() is called.

        @param parent A non-null pointer to the parent.

        @note A pointer is used to prevent confusion
              with copy construction.
    */
    explicit
    PaymentView (PaymentView* parent)
        : view_ (*parent,
            parent->openLedger())
        , pv_ (parent)
    {
    }

    bool
    exists (Keylet const& k) const override
    {
        return view_.exists(k);
    }

    boost::optional<uint256>
    succ (uint256 const& key,
        boost::optional<uint256> last =
            boost::none) const override
    {
        return view_.succ(key, last);
    }

    std::shared_ptr<SLE const>
    read (Keylet const& k) const override
    {
        return view_.read(k);
    }

    bool
    unchecked_erase (uint256 const& key) override
    {
        return view_.unchecked_erase(key);
    }

    void
    unchecked_insert(
        std::shared_ptr<SLE>&& sle) override
    {
        view_.unchecked_insert(
            std::move(sle));
    }

    void
    unchecked_replace (
        std::shared_ptr<SLE>&& sle) override
    {
        view_.unchecked_replace(
            std::move(sle));
    }

    BasicView const*
    parent() const override
    {
        return &view_;
    }

    //---------------------------------------------

    std::shared_ptr<SLE>
    peek (Keylet const& k) override
    {
        return view_.peek(k);
    }

    void
    erase (std::shared_ptr<SLE> const& sle) override
    {
        return view_.erase(sle);
    }

    void
    insert (std::shared_ptr<SLE> const& sle) override
    {
        return view_.insert(sle);
    }

    void
    update (std::shared_ptr<SLE> const& sle) override
    {
        return view_.update(sle);
    }

    bool
    openLedger() const override
    {
        return view_.openLedger();
    }

    //--------------------------------------------------------------------------

    STAmount
    balanceHook (AccountID const& account,
        AccountID const& issuer,
            STAmount const& amount) const override;

    void
    creditHook (AccountID const& from,
        AccountID const& to,
            STAmount const& amount) override;

    void
    apply();
};

}  // ripple

#endif
