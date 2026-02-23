#include <xrpl/ledger/RippleStateView.h>

namespace xrpl {

RippleStateView::RippleStateView(
    ReadView const& view,
    AccountID const& account,
    AccountID const& peer,
    Currency const& currency)
    : RippleStateView(view.read(keylet::line(account, peer, currency)), account, peer)
{
}

RippleStateView::RippleStateView(
    std::shared_ptr<SLE const> sle,
    AccountID const& account,
    AccountID const& peer)
    : LedgerEntryViewBase(std::move(sle))
{
    if (!sle_ || sle_->getType() != ltRIPPLE_STATE)
    {
        sle_ = nullptr;
        return;
    }

    key_ = sle_->key();
    lowLimit_ = sle_->getFieldAmount(sfLowLimit);
    highLimit_ = sle_->getFieldAmount(sfHighLimit);
    balance_ = sle_->getFieldAmount(sfBalance);
    flags_ = sle_->getFieldU32(sfFlags);
    lowQualityIn_ = sle_->getFieldU32(sfLowQualityIn);
    lowQualityOut_ = sle_->getFieldU32(sfLowQualityOut);
    highQualityIn_ = sle_->getFieldU32(sfHighQualityIn);
    highQualityOut_ = sle_->getFieldU32(sfHighQualityOut);

    // Use account comparison to determine which side we're viewing from.
    // We cannot rely on lowLimit_.getIssuer() == account because some
    // transactions (e.g., CashCheck) temporarily modify the issuer field
    // of the limit amounts.
    viewLowest_ = (account < peer);

    // Negate balance if we're viewing from the high side.
    // This puts the balance in "account terms" where:
    // - Positive balance means you hold IOUs (credit)
    // - Negative balance means you owe IOUs (debt)
    // This matches TrustLineBase and getTrustLineBalance semantics.
    if (!viewLowest_)
        balance_.negate();
}

AccountID const&
RippleStateView::getAccountID() const
{
    return viewLowest_ ? lowLimit_.getIssuer() : highLimit_.getIssuer();
}

AccountID const&
RippleStateView::getAccountIDPeer() const
{
    return !viewLowest_ ? lowLimit_.getIssuer() : highLimit_.getIssuer();
}

bool
RippleStateView::getAuth() const
{
    return flags_ & (viewLowest_ ? lsfLowAuth : lsfHighAuth);
}

bool
RippleStateView::getAuthPeer() const
{
    return flags_ & (!viewLowest_ ? lsfLowAuth : lsfHighAuth);
}

bool
RippleStateView::getNoRipple() const
{
    return flags_ & (viewLowest_ ? lsfLowNoRipple : lsfHighNoRipple);
}

bool
RippleStateView::getNoRipplePeer() const
{
    return flags_ & (!viewLowest_ ? lsfLowNoRipple : lsfHighNoRipple);
}

bool
RippleStateView::getFreeze() const
{
    return flags_ & (viewLowest_ ? lsfLowFreeze : lsfHighFreeze);
}

bool
RippleStateView::getFreezePeer() const
{
    return flags_ & (!viewLowest_ ? lsfLowFreeze : lsfHighFreeze);
}

bool
RippleStateView::getDeepFreeze() const
{
    return flags_ & (viewLowest_ ? lsfLowDeepFreeze : lsfHighDeepFreeze);
}

bool
RippleStateView::getDeepFreezePeer() const
{
    return flags_ & (!viewLowest_ ? lsfLowDeepFreeze : lsfHighDeepFreeze);
}

STAmount const&
RippleStateView::getBalance() const
{
    return balance_;
}

STAmount const&
RippleStateView::getLimit() const
{
    return viewLowest_ ? lowLimit_ : highLimit_;
}

STAmount const&
RippleStateView::getLimitPeer() const
{
    return !viewLowest_ ? lowLimit_ : highLimit_;
}

std::uint32_t
RippleStateView::getQualityIn() const
{
    return viewLowest_ ? lowQualityIn_ : highQualityIn_;
}

std::uint32_t
RippleStateView::getQualityOut() const
{
    return viewLowest_ ? lowQualityOut_ : highQualityOut_;
}

std::uint32_t
RippleStateView::getQualityInPeer() const
{
    return !viewLowest_ ? lowQualityIn_ : highQualityIn_;
}

std::uint32_t
RippleStateView::getQualityOutPeer() const
{
    return !viewLowest_ ? lowQualityOut_ : highQualityOut_;
}

bool
RippleStateView::getReserve() const
{
    return flags_ & (viewLowest_ ? lsfLowReserve : lsfHighReserve);
}

bool
RippleStateView::getReservePeer() const
{
    return flags_ & (!viewLowest_ ? lsfLowReserve : lsfHighReserve);
}

// Static convenience methods

bool
RippleStateView::isFrozen(
    ReadView const& view,
    AccountID const& account,
    Currency const& currency,
    AccountID const& issuer)
{
    if (isXRP(currency))
        return false;

    // Check global freeze on issuer
    auto sle = view.read(keylet::account(issuer));
    if (sle && sle->isFlag(lsfGlobalFreeze))
        return true;

    // Check individual trustline freeze
    if (issuer != account)
    {
        RippleStateView const rsv(view, account, issuer, currency);
        if (rsv && rsv.getFreezePeer())
            return true;
    }
    return false;
}

bool
RippleStateView::isDeepFrozen(
    ReadView const& view,
    AccountID const& account,
    Currency const& currency,
    AccountID const& issuer)
{
    if (isXRP(currency))
        return false;

    if (issuer == account)
        return false;

    RippleStateView const rsv(view, account, issuer, currency);
    if (!rsv)
        return false;

    // Deep freeze applies if either side has set it
    return rsv.getDeepFreeze() || rsv.getDeepFreezePeer();
}

STAmount
RippleStateView::creditLimit(
    ReadView const& view,
    AccountID const& account,
    AccountID const& issuer,
    Currency const& currency)
{
    STAmount result(Issue{currency, account});

    RippleStateView const rsv(view, account, issuer, currency);
    if (rsv)
    {
        result = rsv.getLimit();
        result.setIssuer(account);
    }

    return result;
}

STAmount
RippleStateView::creditBalance(
    ReadView const& view,
    AccountID const& account,
    AccountID const& issuer,
    Currency const& currency)
{
    STAmount result(Issue{currency, account});

    RippleStateView const rsv(view, account, issuer, currency);
    if (rsv)
    {
        result = rsv.getBalance();
        // Negate to convert from getBalance() semantics (positive = hold)
        // to creditBalance() semantics (positive = owe/debt).
        // The original creditBalance() negated when account < issuer (LOW),
        // while getBalance() negates when account > issuer (HIGH).
        result.negate();
        result.setIssuer(account);
    }

    return result;
}

}  // namespace xrpl
