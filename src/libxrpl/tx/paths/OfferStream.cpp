/** @file
 *  Implements the order-book cursor used by the Flow payment engine.
 *
 *  `TOfferStreamBase` and `FlowOfferStream` walk a single currency-pair book
 *  from the best-quality offer downward, permanently removing stale entries
 *  (expired, missing, found-unfunded, deep-frozen, out-of-domain) and skipping
 *  entries that only *became* unfunded within the current transaction.
 *
 *  All eight `TIn × TOut` combinations of `XRPAmount`, `IOUAmount`, and
 *  `MPTAmount` are explicitly instantiated at the bottom of this file.
 */
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
#include <memory>
#include <optional>

namespace xrpl {

namespace {
/** Return `true` if both issuers named in @p book exist in @p view.
 *
 *  XRP is its own issuer and always passes; for IOU/MPT assets the issuing
 *  `AccountRoot` must be present.  A book whose issuer has been deleted is
 *  treated as invalid and `step()` returns `false` immediately, preventing
 *  spurious work against a defunct order book.
 *
 *  @param view The ledger state to check.
 *  @param book The currency-pair book whose issuers are validated.
 *  @return `true` if both the in-asset and out-asset issuers exist.
 */
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

template <StepAmount TIn, StepAmount TOut>
void
TOfferStreamBase<TIn, TOut>::erase(ApplyView& view)
{
    // NOTE: This should use ApplyView::dirRemove, which would also clean up
    // empty directory pages.  However, that would change which ledger entries
    // a payment touches, making it a protocol-breaking change.  Empty pages
    // are therefore left in place to preserve consensus compatibility.

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

/** Compute how much of @p asset the account @p id can deliver.
 *
 *  Dispatches across all three amount types via `if constexpr`:
 *  - `IOUAmount`: if the account is the asset's issuer, the issuer is
 *    self-funded and `amtDefault` (the offer's stated output amount) is
 *    returned directly — an IOU issuer has no trust-line constraint with
 *    themselves.
 *  - `MPTAmount`: if the account is the issuer, `issuerFundsToSelfIssue`
 *    computes the remaining issuance headroom from the MPT's supply limits;
 *    MPT issuers are *not* self-funded without bound.
 *  - All other cases: delegates to `accountHolds` with the supplied freeze and
 *    auth handling, surfacing frozen or unauthorized balances as zero.
 *
 *  @tparam T         Amount type (`IOUAmount`, `MPTAmount`, or `XRPAmount`).
 *  @param view       Ledger view to query.
 *  @param id         Account whose available funds are computed.
 *  @param amtDefault Fallback amount returned for self-funded IOU issuers.
 *  @param asset      The asset being queried.
 *  @param freezeHandling Whether to zero out frozen trust-line balances.
 *  @param authHandling   Whether to zero out unauthorized holder balances.
 *  @param j          Journal for diagnostic logging inside `accountHolds`.
 *  @return The deliverable amount of @p asset held by @p id.
 */
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
    constexpr bool const kIN_IS_XRP = std::is_same_v<TTakerPays, XRPAmount>;
    constexpr bool const kOUT_IS_XRP = std::is_same_v<TTakerGets, XRPAmount>;

    if constexpr (kOUT_IS_XRP)
    {
        // If TakerGets is XRP, the worst-case effective quality after clamping
        // to owner funds is ~10^-81 TakerPays per 1-drop TakerGets — still
        // astronomically good for any realistic IOU input, so this protection
        // is unnecessary.
        return false;
    }

    if (!ownerFunds_)
        return false;

    TAmounts<TTakerPays, TTakerGets> const ofrAmts{
        toAmount<TTakerPays>(offer_.amount().in), toAmount<TTakerGets>(offer_.amount().out)};

    if constexpr (!kIN_IS_XRP && !kOUT_IS_XRP)
    {
        if (Number(ofrAmts.in) >= Number(ofrAmts.out))
            return false;
    }

    TTakerGets const ownerFunds = toAmount<TTakerGets>(*ownerFunds_);

    auto const effectiveAmounts = [&] {
        if (offer_.owner() != offer_.assetOut().getIssuer() && ownerFunds < ofrAmts.out)
        {
            // Round down (not up) so the adjustment cannot artificially raise
            // effective quality above the stored quality, which would distort
            // book ordering.
            return offer_.quality().ceilOutStrict(ofrAmts, ownerFunds, /* roundUp */ false);
        }
        return ofrAmts;
    }();

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
    // CONSENSUS INVARIANT: Modifying the order or logic of these checks
    // constitutes a protocol-breaking change — all validators must agree on
    // exactly which offers are consumed or removed for each payment.

    if (!validBook_)
        return false;

    for (;;)
    {
        ownerFunds_ = std::nullopt;
        if (!tip_.step(j_))
            return false;

        std::shared_ptr<SLE> const entry = tip_.entry();

        if (!counter_.step())
            return false;

        if (!entry)
        {
            erase(view_);
            erase(cancelView_);
            continue;
        }

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

        ownerFunds_ = accountFundsHelper(
            view_,
            offer_.owner(),
            amount.out,
            offer_.assetOut(),
            FreezeHandling::ZeroIfFrozen,
            AuthHandling::ZeroIfUnauthorized,
            j_);

        if (*ownerFunds_ <= beast::kZERO)
        {
            // Distinguish "found unfunded" (balance was already zero before
            // this transaction — permanent removal) from "became unfunded"
            // (balance dropped to zero inside this transaction due to an
            // earlier strand — skip only, since the strand may be rolled back).
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
            continue;
        }

        if (shouldRmSmallIncreasedQOffer<TIn, TOut>())
        {
            // Apply the same found-vs-became distinction: only permanently
            // remove if the tiny-quality condition existed before this tx.
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
