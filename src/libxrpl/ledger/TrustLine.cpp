#include <xrpl/ledger/TrustLine.h>
#include <xrpl/ledger/View.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Quality.h>

namespace xrpl {

TrustLine::TrustLine(std::shared_ptr<SLE const> sle, AccountID const& viewAccount)
    : rippleState_(std::move(sle))
    , viewLowest_(rippleState_.getLowLimit().getIssuer() == viewAccount)
{
}

TrustLine::TrustLine(ledger_entries::RippleState const& rippleState, AccountID const& viewAccount)
    : rippleState_(rippleState), viewLowest_(rippleState_.getLowLimit().getIssuer() == viewAccount)
{
}

std::optional<TrustLine>
TrustLine::makeItem(AccountID const& accountID, std::shared_ptr<SLE const> const& sle)
{
    if (!sle || sle->getType() != ltRIPPLE_STATE)
        return {};
    return TrustLine{sle, accountID};
}

std::vector<TrustLine>
TrustLine::getItems(AccountID const& accountID, ReadView const& view, LineDirection direction)
{
    std::vector<TrustLine> items;
    forEachItem(
        view,
        accountID,
        [&items, &accountID, &direction](std::shared_ptr<SLE const> const& sleCur) {
            auto line = TrustLine::makeItem(accountID, sleCur);
            if (line && (direction == LineDirection::outgoing || !line->getNoRipple()))
                items.push_back(std::move(*line));
        });
    items.shrink_to_fit();
    return items;
}

uint256
TrustLine::key() const
{
    return rippleState_.getSle()->key();
}

AccountID
TrustLine::getAccountID() const
{
    return viewLowest_ ? rippleState_.getLowLimit().getIssuer()
                       : rippleState_.getHighLimit().getIssuer();
}

AccountID
TrustLine::getAccountIDPeer() const
{
    return !viewLowest_ ? rippleState_.getLowLimit().getIssuer()
                        : rippleState_.getHighLimit().getIssuer();
}

bool
TrustLine::getAuth() const
{
    auto const flags = rippleState_.getFlags();
    return flags & (viewLowest_ ? lsfLowAuth : lsfHighAuth);
}

bool
TrustLine::getAuthPeer() const
{
    auto const flags = rippleState_.getFlags();
    return flags & (!viewLowest_ ? lsfLowAuth : lsfHighAuth);
}

bool
TrustLine::getNoRipple() const
{
    auto const flags = rippleState_.getFlags();
    return flags & (viewLowest_ ? lsfLowNoRipple : lsfHighNoRipple);
}

bool
TrustLine::getNoRipplePeer() const
{
    auto const flags = rippleState_.getFlags();
    return flags & (!viewLowest_ ? lsfLowNoRipple : lsfHighNoRipple);
}

bool
TrustLine::getFreeze() const
{
    auto const flags = rippleState_.getFlags();
    return flags & (viewLowest_ ? lsfLowFreeze : lsfHighFreeze);
}

bool
TrustLine::getDeepFreeze() const
{
    auto const flags = rippleState_.getFlags();
    return flags & (viewLowest_ ? lsfLowDeepFreeze : lsfHighDeepFreeze);
}

bool
TrustLine::getFreezePeer() const
{
    auto const flags = rippleState_.getFlags();
    return flags & (!viewLowest_ ? lsfLowFreeze : lsfHighFreeze);
}

bool
TrustLine::getDeepFreezePeer() const
{
    auto const flags = rippleState_.getFlags();
    return flags & (!viewLowest_ ? lsfLowDeepFreeze : lsfHighDeepFreeze);
}

STAmount
TrustLine::getBalance() const
{
    auto balance = rippleState_.getBalance();
    if (!viewLowest_)
        balance.negate();
    return balance;
}

STAmount
TrustLine::getLimit() const
{
    return viewLowest_ ? rippleState_.getLowLimit() : rippleState_.getHighLimit();
}

STAmount
TrustLine::getLimitPeer() const
{
    return !viewLowest_ ? rippleState_.getLowLimit() : rippleState_.getHighLimit();
}

Rate
TrustLine::getQualityIn() const
{
    auto const quality =
        viewLowest_ ? rippleState_.getLowQualityIn() : rippleState_.getHighQualityIn();
    return Rate{quality.value_or(0)};
}

Rate
TrustLine::getQualityOut() const
{
    auto const quality =
        viewLowest_ ? rippleState_.getLowQualityOut() : rippleState_.getHighQualityOut();
    return Rate{quality.value_or(0)};
}

Json::Value
TrustLine::getJson(int)
{
    Json::Value ret(Json::objectValue);
    ret["low_id"] = to_string(rippleState_.getLowLimit().getIssuer());
    ret["high_id"] = to_string(rippleState_.getHighLimit().getIssuer());
    return ret;
}

}  // namespace xrpl
