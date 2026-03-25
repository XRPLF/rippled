#include <xrpl/ledger/TrustLine.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Quality.h>

namespace xrpl {

TrustLine::TrustLine(std::shared_ptr<SLE const> sle, AccountID const& viewAccount)
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
TrustLine::getJson(int)
{
    Json::Value ret(Json::objectValue);
    ret["low_id"] = to_string(mLowLimit.getIssuer());
    ret["high_id"] = to_string(mHighLimit.getIssuer());
    return ret;
}

std::optional<TrustLine>
TrustLine::makeItem(AccountID const& accountID, std::shared_ptr<SLE const> const& sle)
{
    if (!sle || sle->getType() != ltRIPPLE_STATE)
        return {};
    return TrustLine{sle, accountID};
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
        [&items, &accountID, &direction](std::shared_ptr<SLE const> const& sleCur) {
            auto ret = T::makeItem(accountID, sleCur);
            if (ret && (direction == LineDirection::outgoing || !ret->getNoRipple()))
                items.push_back(std::move(*ret));
        });
    // This list may be around for a while, so free up any unneeded
    // capacity
    items.shrink_to_fit();

    return items;
}
}  // namespace detail

std::vector<TrustLine>
TrustLine::getItems(AccountID const& accountID, ReadView const& view, LineDirection direction)
{
    return detail::getTrustLineItems<TrustLine>(accountID, view, direction);
}

}  // namespace xrpl
