#include <xrpl/tx/paths/OfferStream.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/MPTokenHelpers.h>
#include <xrpl/ledger/helpers/PermissionedDEXHelpers.h>
#include <xrpl/ledger/helpers/RippleStateHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Book.h>
#include <xrpl/protocol/Concepts.h>
#include <xrpl/protocol/IOUAmount.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/MPTAmount.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Quality.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/XRPAmount.h>

#include <algorithm>
#include <optional>

namespace xrpl {

namespace {
bool
checkIssuers(ReadView const& view, Book const& book)
{
    auto issuerExists = [](ReadView const& view, Asset const& asset) -> bool {
        auto const& issuer = asset.getIssuer();
        return isXRP(issuer) || view.exists(keylet::account(issuer));
    };
    return issuerExists(view, book.in) && issuerExists(view, book.out);
}
}  // namespace

template <StepAmount TIn, StepAmount TOut>
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
    XRPL_ASSERT(validBook_, "xrpl::TOfferStreamBase::TOfferStreamBase : valid book");
}

// Handle the case where a directory item with no corresponding ledger entry
// is found. This shouldn't happen but if it does we clean it up.
template <StepAmount TIn, StepAmount TOut>
void
TOfferStreamBase<TIn, TOut>::erase(ApplyView& view)
{
    // NIKB NOTE This should be using ApplyView::dirRemove, which would
    //           correctly remove the directory if its the last entry.
    //           Unfortunately this is a protocol breaking change.

    auto p = view.peek(keylet::page(tip_.dir()));

    if (p == nullptr)
    {
        JLOG(j_.error()) << "Missing directory " << tip_.dir() << " for offer " << tip_.index();
        return;
    }

    auto v(p->getFieldV256(sfIndexes));
    auto it(std::ranges::find(v, tip_.index()));

    if (it == v.end())
    {
        JLOG(j_.error()) << "Missing offer " << tip_.index() << " for directory " << tip_.dir();
        return;
    }

    v.erase(it);
    p->setFieldV256(sfIndexes, v);
    view.update(p);

    JLOG(j_.trace()) << "Missing offer " << tip_.index() << " removed from directory "
                     << tip_.dir();
}

template <StepAmount T>
static T
accountFundsHelper(
    ReadView const& view,
    AccountID const& id,
    T const& amtDefault,
    Asset const& asset,
    FreezeHandling freezeHandling,
    AuthHandling authHandling,
    beast::Journal j)
{
    if constexpr (std::is_same_v<T, IOUAmount>)
    {
        if (id == asset.getIssuer())
        {
            // self funded
            return amtDefault;
        }
    }
    else if constexpr (std::is_same_v<T, MPTAmount>)
    {
        if (id == asset.getIssuer())
        {
            return toAmount<T>(issuerFundsToSelfIssue(view, asset.get<MPTIssue>()));
        }
    }

    return toAmount<T>(accountHolds(view, id, asset, freezeHandling, authHandling, j));
}

template <StepAmount TIn, StepAmount TOut>
template <class TTakerPays, class TTakerGets>
    requires ValidTaker<TTakerPays, TTakerGets>
[[nodiscard]] bool
TOfferStreamBase<TIn, TOut>::shouldRmSmallIncreasedQOffer() const
{
    // Consider removing the offer if:
    //  o `TakerPays` is XRP (because of XRP drops granularity) or
    //  o `TakerPays` and `TakerGets` are both IOU and `TakerPays`<`TakerGets`
    static constexpr bool kInIsXrp = std::is_same_v<TTakerPays, XRPAmount>;
    static constexpr bool kOutIsXrp = std::is_same_v<TTakerGets, XRPAmount>;

    if constexpr (kOutIsXrp)
    {
        // If `TakerGets` is XRP, the worst this offer's quality can change is
        // to about 10^-81 `TakerPays` and 1 drop `TakerGets`. This will be
        // remarkably good quality for any realistic asset, so these offers
        // don't need this extra check.
        return false;
    }

    if (!ownerFunds_)
        return false;

    TAmounts<TTakerPays, TTakerGets> const ofrAmts{
        toAmount<TTakerPays>(offer_.amount().in), toAmount<TTakerGets>(offer_.amount().out)};

    if constexpr (!kInIsXrp && !kOutIsXrp)
    {
        if (Number(ofrAmts.in) >= Number(ofrAmts.out))
            return false;
    }

    TTakerGets const ownerFunds = toAmount<TTakerGets>(*ownerFunds_);

    auto const effectiveAmounts = [&] {
        if (offer_.owner() != offer_.assetOut().getIssuer() && ownerFunds < ofrAmts.out)
        {
            // adjust the amounts by owner funds.
            //
            // It turns out we can prevent order book blocking by rounding down
            // the ceil_out() result.
            return offer_.quality().ceilOutStrict(ofrAmts, ownerFunds, /* roundUp */ false);
        }
        return ofrAmts;
    }();

    // If either the effective in or out are zero then remove the offer.
    if (effectiveAmounts.in.signum() <= 0 || effectiveAmounts.out.signum() <= 0)
        return true;

    if (effectiveAmounts.in > TTakerPays::minPositiveAmount())
        return false;

    Quality const effectiveQuality{effectiveAmounts};
    return effectiveQuality < offer_.quality();
}

template <StepAmount TIn, StepAmount TOut>
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

        SLE::pointer const entry = tip_.entry();

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
        if (entry->isFieldPresent(sfExpiration) && tp{d{(*entry)[sfExpiration]}} <= expire_)
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

        if (isDeepFrozen(view_, offer_.owner(), offer_.assetIn()))
        {
            JLOG(j_.trace()) << "Removing deep frozen unfunded offer " << entry->key();
            permRmOffer(entry->key());
            offer_ = TOffer<TIn, TOut>{};
            continue;
        }

        if (entry->isFieldPresent(sfDomainID) &&
            !permissioned_dex::offerInDomain(
                view_, entry->key(), entry->getFieldH256(sfDomainID), j_))
        {
            JLOG(j_.trace()) << "Removing offer no longer in domain " << entry->key();
            permRmOffer(entry->key());
            offer_ = TOffer<TIn, TOut>{};
            continue;
        }

        // Calculate owner funds
        ownerFunds_ = accountFundsHelper(
            view_,
            offer_.owner(),
            amount.out,
            offer_.assetOut(),
            FreezeHandling::ZeroIfFrozen,
            AuthHandling::ZeroIfUnauthorized,
            j_);

        // Check for unfunded offer
        if (*ownerFunds_ <= beast::kZero)
        {
            // If the owner's balance in the pristine view is the same,
            // we haven't modified the balance and therefore the
            // offer is "found unfunded" versus "became unfunded"
            auto const originalFunds = accountFundsHelper(
                cancelView_,
                offer_.owner(),
                amount.out,
                offer_.assetOut(),
                FreezeHandling::ZeroIfFrozen,
                AuthHandling::ZeroIfUnauthorized,
                j_);

            if (originalFunds == *ownerFunds_)
            {
                permRmOffer(entry->key());
                JLOG(j_.trace()) << "Removing unfunded offer " << entry->key();
            }
            else
            {
                JLOG(j_.trace()) << "Removing became unfunded offer " << entry->key();
            }
            offer_ = TOffer<TIn, TOut>{};
            // See comment at top of loop for how the offer is removed
            continue;
        }

        if (shouldRmSmallIncreasedQOffer<TIn, TOut>())
        {
            auto const originalFunds = accountFundsHelper(
                cancelView_,
                offer_.owner(),
                amount.out,
                offer_.assetOut(),
                FreezeHandling::ZeroIfFrozen,
                AuthHandling::ZeroIfUnauthorized,
                j_);

            if (originalFunds == *ownerFunds_)
            {
                permRmOffer(entry->key());
                JLOG(j_.trace()) << "Removing tiny offer due to reduced quality " << entry->key();
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

template <StepAmount TIn, StepAmount TOut>
void
FlowOfferStream<TIn, TOut>::permRmOffer(uint256 const& offerIndex)
{
    permToRemove_.insert(offerIndex);
}

template class FlowOfferStream<IOUAmount, IOUAmount>;
template class FlowOfferStream<XRPAmount, IOUAmount>;
template class FlowOfferStream<IOUAmount, XRPAmount>;
template class FlowOfferStream<MPTAmount, MPTAmount>;
template class FlowOfferStream<XRPAmount, MPTAmount>;
template class FlowOfferStream<MPTAmount, XRPAmount>;
template class FlowOfferStream<IOUAmount, MPTAmount>;
template class FlowOfferStream<MPTAmount, IOUAmount>;

template class TOfferStreamBase<IOUAmount, IOUAmount>;
template class TOfferStreamBase<XRPAmount, IOUAmount>;
template class TOfferStreamBase<IOUAmount, XRPAmount>;
template class TOfferStreamBase<MPTAmount, MPTAmount>;
template class TOfferStreamBase<XRPAmount, MPTAmount>;
template class TOfferStreamBase<MPTAmount, XRPAmount>;
template class TOfferStreamBase<IOUAmount, MPTAmount>;
template class TOfferStreamBase<MPTAmount, IOUAmount>;
}  // namespace xrpl
