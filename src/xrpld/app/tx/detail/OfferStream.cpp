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

#include <xrpld/app/tx/detail/OfferStream.h>
#include <xrpl/basics/Log.h>
#include <xrpl/protocol/Feature.h>

namespace ripple {

namespace {
bool
checkIssuers(ReadView const& view, Book const& book)
{
    auto issuerExists = [](ReadView const& view, Issue const& iss) -> bool {
        return isXRP(iss.account) || view.read(keylet::account(iss.account));
    };
    return issuerExists(view, book.in) && issuerExists(view, book.out);
}
}  // namespace

template <class TIn, class TOut>
TOfferStreamBase<TIn, TOut>::TOfferStreamBase(
    ApplyView& view,
    ApplyView& cancelView,
    Book const& book,
    NetClock::time_point when,
    StepCounter& counter,
    beast::Journal journal)
    : j_(journal)
    , view_(view)
    , cancelView_(cancelView)
    , book_(book)
    , validBook_(checkIssuers(view, book))
    , expire_(when)
    , tip_(view, book_)
    , counter_(counter)
{
    ASSERT(
        validBook_, "ripple::TOfferStreamBase::TOfferStreamBase : valid book");
}

// Handle the case where a directory item with no corresponding ledger entry
// is found. This shouldn't happen but if it does we clean it up.
template <class TIn, class TOut>
void
TOfferStreamBase<TIn, TOut>::erase(ApplyView& view)
{
    // NIKB NOTE This should be using ApplyView::dirRemove, which would
    //           correctly remove the directory if its the last entry.
    //           Unfortunately this is a protocol breaking change.

    auto p = view.peek(keylet::page(tip_.dir()));

    if (p == nullptr)
    {
        JLOG(j_.error()) << "Missing directory " << tip_.dir() << " for offer "
                         << tip_.index();
        return;
    }

    auto v(p->getFieldV256(sfIndexes));
    auto it(std::find(v.begin(), v.end(), tip_.index()));

    if (it == v.end())
    {
        JLOG(j_.error()) << "Missing offer " << tip_.index()
                         << " for directory " << tip_.dir();
        return;
    }

    v.erase(it);
    p->setFieldV256(sfIndexes, v);
    view.update(p);

    JLOG(j_.trace()) << "Missing offer " << tip_.index()
                     << " removed from directory " << tip_.dir();
}

static STAmount
accountFundsHelper(
    ReadView const& view,
    AccountID const& id,
    STAmount const& saDefault,
    Issue const&,
    FreezeHandling freezeHandling,
    beast::Journal j)
{
    return accountFunds(view, id, saDefault, freezeHandling, j);
}

static IOUAmount
accountFundsHelper(
    ReadView const& view,
    AccountID const& id,
    IOUAmount const& amtDefault,
    Issue const& issue,
    FreezeHandling freezeHandling,
    beast::Journal j)
{
    if (issue.account == id)
        // self funded
        return amtDefault;

    return toAmount<IOUAmount>(accountHolds(
        view, id, issue.currency, issue.account, freezeHandling, j));
}

static XRPAmount
accountFundsHelper(
    ReadView const& view,
    AccountID const& id,
    XRPAmount const& amtDefault,
    Issue const& issue,
    FreezeHandling freezeHandling,
    beast::Journal j)
{
    return toAmount<XRPAmount>(accountHolds(
        view, id, issue.currency, issue.account, freezeHandling, j));
}

template <class TIn, class TOut>
template <class TTakerPays, class TTakerGets>
bool
TOfferStreamBase<TIn, TOut>::shouldRmSmallIncreasedQOffer() const
{
    static_assert(
        std::is_same_v<TTakerPays, IOUAmount> ||
            std::is_same_v<TTakerPays, XRPAmount>,
        "STAmount is not supported");

    static_assert(
        std::is_same_v<TTakerGets, IOUAmount> ||
            std::is_same_v<TTakerGets, XRPAmount>,
        "STAmount is not supported");

    static_assert(
        !std::is_same_v<TTakerPays, XRPAmount> ||
            !std::is_same_v<TTakerGets, XRPAmount>,
        "Cannot have XRP/XRP offers");

    if (!view_.rules().enabled(fixRmSmallIncreasedQOffers))
        return false;

    // Consider removing the offer if:
    //  o `TakerPays` is XRP (because of XRP drops granularity) or
    //  o `TakerPays` and `TakerGets` are both IOU and `TakerPays`<`TakerGets`
    constexpr bool const inIsXRP = std::is_same_v<TTakerPays, XRPAmount>;
    constexpr bool const outIsXRP = std::is_same_v<TTakerGets, XRPAmount>;

    if constexpr (outIsXRP)
    {
        // If `TakerGets` is XRP, the worst this offer's quality can change is
        // to about 10^-81 `TakerPays` and 1 drop `TakerGets`. This will be
        // remarkably good quality for any realistic asset, so these offers
        // don't need this extra check.
        return false;
    }

    TAmounts<TTakerPays, TTakerGets> const ofrAmts{
        toAmount<TTakerPays>(offer_.amount().in),
        toAmount<TTakerGets>(offer_.amount().out)};

    if constexpr (!inIsXRP && !outIsXRP)
    {
        if (ofrAmts.in >= ofrAmts.out)
            return false;
    }

    TTakerGets const ownerFunds = toAmount<TTakerGets>(*ownerFunds_);
    bool const fixReduced = view_.rules().enabled(fixReducedOffersV1);

    auto const effectiveAmounts = [&] {
        if (offer_.owner() != offer_.issueOut().account &&
            ownerFunds < ofrAmts.out)
        {
            // adjust the amounts by owner funds.
            //
            // It turns out we can prevent order book blocking by rounding down
            // the ceil_out() result.  This adjustment changes transaction
            // results, so it must be made under an amendment.
            if (fixReduced)
                return offer_.quality().ceil_out_strict(
                    ofrAmts, ownerFunds, /* roundUp */ false);

            return offer_.quality().ceil_out(ofrAmts, ownerFunds);
        }
        return ofrAmts;
    }();

    // If either the effective in or out are zero then remove the offer.
    // This can happen with fixReducedOffersV1 since it rounds down.
    if (fixReduced &&
        (effectiveAmounts.in.signum() <= 0 ||
         effectiveAmounts.out.signum() <= 0))
        return true;

    if (effectiveAmounts.in > TTakerPays::minPositiveAmount())
        return false;

    Quality const effectiveQuality{effectiveAmounts};
    return effectiveQuality < offer_.quality();
}

template <class TIn, class TOut>
bool
TOfferStreamBase<TIn, TOut>::step()
{
    // Modifying the order or logic of these
    // operations causes a protocol breaking change.

    if (!validBook_)
        return false;

    for (;;)
    {
        ownerFunds_ = std::nullopt;
        // BookTip::step deletes the current offer from the view before
        // advancing to the next (unless the ledger entry is missing).
        if (!tip_.step(j_))
            return false;

        std::shared_ptr<SLE> entry = tip_.entry();

        // If we exceed the maximum number of allowed steps, we're done.
        if (!counter_.step())
            return false;

        // Remove if missing
        if (!entry)
        {
            erase(view_);
            erase(cancelView_);
            continue;
        }

        // Remove if expired
        using d = NetClock::duration;
        using tp = NetClock::time_point;
        if (entry->isFieldPresent(sfExpiration) &&
            tp{d{(*entry)[sfExpiration]}} <= expire_)
        {
            JLOG(j_.trace()) << "Removing expired offer " << entry->key();
            permRmOffer(entry->key());
            continue;
        }

        offer_ = TOffer<TIn, TOut>(entry, tip_.quality());

        auto const amount(offer_.amount());

        // Remove if either amount is zero
        if (amount.empty())
        {
            JLOG(j_.warn()) << "Removing bad offer " << entry->key();
            permRmOffer(entry->key());
            offer_ = TOffer<TIn, TOut>{};
            continue;
        }

        // Calculate owner funds
        ownerFunds_ = accountFundsHelper(
            view_,
            offer_.owner(),
            amount.out,
            offer_.issueOut(),
            fhZERO_IF_FROZEN,
            j_);

        // Check for unfunded offer
        if (*ownerFunds_ <= beast::zero)
        {
            // If the owner's balance in the pristine view is the same,
            // we haven't modified the balance and therefore the
            // offer is "found unfunded" versus "became unfunded"
            auto const original_funds = accountFundsHelper(
                cancelView_,
                offer_.owner(),
                amount.out,
                offer_.issueOut(),
                fhZERO_IF_FROZEN,
                j_);

            if (original_funds == *ownerFunds_)
            {
                permRmOffer(entry->key());
                JLOG(j_.trace()) << "Removing unfunded offer " << entry->key();
            }
            else
            {
                JLOG(j_.trace())
                    << "Removing became unfunded offer " << entry->key();
            }
            offer_ = TOffer<TIn, TOut>{};
            // See comment at top of loop for how the offer is removed
            continue;
        }

        bool const rmSmallIncreasedQOffer = [&] {
            bool const inIsXRP = isXRP(offer_.issueIn());
            bool const outIsXRP = isXRP(offer_.issueOut());
            if (inIsXRP && !outIsXRP)
            {
                // Without the `if constexpr`, the
                // `shouldRmSmallIncreasedQOffer` template will be instantiated
                // even if it is never used. This can cause compiler errors in
                // some cases, hence the `if constexpr` guard.
                // Note that TIn can be XRPAmount or STAmount, and TOut can be
                // IOUAmount or STAmount.
                if constexpr (!(std::is_same_v<TIn, IOUAmount> ||
                                std::is_same_v<TOut, XRPAmount>))
                    return shouldRmSmallIncreasedQOffer<XRPAmount, IOUAmount>();
            }
            if (!inIsXRP && outIsXRP)
            {
                // See comment above for `if constexpr` rationale
                if constexpr (!(std::is_same_v<TIn, XRPAmount> ||
                                std::is_same_v<TOut, IOUAmount>))
                    return shouldRmSmallIncreasedQOffer<IOUAmount, XRPAmount>();
            }
            if (!inIsXRP && !outIsXRP)
            {
                // See comment above for `if constexpr` rationale
                if constexpr (!(std::is_same_v<TIn, XRPAmount> ||
                                std::is_same_v<TOut, XRPAmount>))
                    return shouldRmSmallIncreasedQOffer<IOUAmount, IOUAmount>();
            }
            UNREACHABLE(
                "rippls::TOfferStreamBase::step::rmSmallIncreasedQOffer : XRP "
                "vs XRP offer");
            return false;
        }();

        if (rmSmallIncreasedQOffer)
        {
            auto const original_funds = accountFundsHelper(
                cancelView_,
                offer_.owner(),
                amount.out,
                offer_.issueOut(),
                fhZERO_IF_FROZEN,
                j_);

            if (original_funds == *ownerFunds_)
            {
                permRmOffer(entry->key());
                JLOG(j_.trace())
                    << "Removing tiny offer due to reduced quality "
                    << entry->key();
            }
            else
            {
                JLOG(j_.trace()) << "Removing tiny offer that became tiny due "
                                    "to reduced quality "
                                 << entry->key();
            }
            offer_ = TOffer<TIn, TOut>{};
            // See comment at top of loop for how the offer is removed
            continue;
        }

        break;
    }

    return true;
}

void
OfferStream::permRmOffer(uint256 const& offerIndex)
{
    offerDelete(cancelView_, cancelView_.peek(keylet::offer(offerIndex)), j_);
}

template <class TIn, class TOut>
void
FlowOfferStream<TIn, TOut>::permRmOffer(uint256 const& offerIndex)
{
    permToRemove_.insert(offerIndex);
}

template class FlowOfferStream<STAmount, STAmount>;
template class FlowOfferStream<IOUAmount, IOUAmount>;
template class FlowOfferStream<XRPAmount, IOUAmount>;
template class FlowOfferStream<IOUAmount, XRPAmount>;

template class TOfferStreamBase<STAmount, STAmount>;
template class TOfferStreamBase<IOUAmount, IOUAmount>;
template class TOfferStreamBase<XRPAmount, IOUAmount>;
template class TOfferStreamBase<IOUAmount, XRPAmount>;
}  // namespace ripple
