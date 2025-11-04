#include <xrpld/app/paths/TrustLine.h>

#include <xrpl/protocol/STAmount.h>

#include <memory>

namespace ripple {

TrustLineBase::TrustLineBase(
    std::shared_ptr<SLE const> const& sle,
    AccountID const& viewAccount)
    : key_(sle->key())
    , mLowLimit(sle->getFieldAmount(sfLowLimit))
    , mHighLimit(sle->getFieldAmount(sfHighLimit))
    , mBalance(sle->getFieldAmount(sfBalance))
    , mFlags(sle->getFieldU32(sfFlags))
    , mViewLowest(mLowLimit.getIssuer() == viewAccount)
{
    if (!mViewLowest)
        mBalance.negate();
}

Json::Value
TrustLineBase::getJson(int)
{
    Json::Value ret(Json::objectValue);
    ret["low_id"] = to_string(mLowLimit.getIssuer());
    ret["high_id"] = to_string(mHighLimit.getIssuer());
    return ret;
}

std::optional<PathFindTrustLine>
PathFindTrustLine::makeItem(
    AccountID const& accountID,
    std::shared_ptr<SLE const> const& sle)
{
    if (!sle || sle->getType() != ltRIPPLE_STATE)
        return {};
    return std::optional{PathFindTrustLine{sle, accountID}};
}

namespace detail {
template <class T>
std::vector<T>
getTrustLineItems(
    AccountID const& accountID,
    ReadView const& view,
    LineDirection direction = LineDirection::outgoing)
{
    std::vector<T> items;
    forEachItem(
        view,
        accountID,
        [&items, &accountID, &direction](
            std::shared_ptr<SLE const> const& sleCur) {
            auto ret = T::makeItem(accountID, sleCur);
            if (ret &&
                (direction == LineDirection::outgoing || !ret->getNoRipple()))
                items.push_back(std::move(*ret));
        });
    // This list may be around for a while, so free up any unneeded
    // capacity
    items.shrink_to_fit();

    return items;
}
}  // namespace detail

std::vector<PathFindTrustLine>
PathFindTrustLine::getItems(
    AccountID const& accountID,
    ReadView const& view,
    LineDirection direction)
{
    return detail::getTrustLineItems<PathFindTrustLine>(
        accountID, view, direction);
}

RPCTrustLine::RPCTrustLine(
    std::shared_ptr<SLE const> const& sle,
    AccountID const& viewAccount)
    : TrustLineBase(sle, viewAccount)
    , lowQualityIn_(sle->getFieldU32(sfLowQualityIn))
    , lowQualityOut_(sle->getFieldU32(sfLowQualityOut))
    , highQualityIn_(sle->getFieldU32(sfHighQualityIn))
    , highQualityOut_(sle->getFieldU32(sfHighQualityOut))
{
}

std::optional<RPCTrustLine>
RPCTrustLine::makeItem(
    AccountID const& accountID,
    std::shared_ptr<SLE const> const& sle)
{
    if (!sle || sle->getType() != ltRIPPLE_STATE)
        return {};
    return std::optional{RPCTrustLine{sle, accountID}};
}

std::vector<RPCTrustLine>
RPCTrustLine::getItems(AccountID const& accountID, ReadView const& view)
{
    return detail::getTrustLineItems<RPCTrustLine>(accountID, view);
}

}  // namespace ripple
