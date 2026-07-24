#include <xrpld/rpc/detail/TrustLine.h>

#include <xrpl/json/json_value.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>

#include <optional>
#include <vector>

namespace xrpl {

TrustLineBase::TrustLineBase(SLE::const_ref sle, AccountID const& viewAccount)
    : key_(sle->key())
    , lowLimit_(sle->getFieldAmount(sfLowLimit))
    , highLimit_(sle->getFieldAmount(sfHighLimit))
    , balance_(sle->getFieldAmount(sfBalance))
    , flags_(sle->getFieldU32(sfFlags))
    , viewLowest_(lowLimit_.getIssuer() == viewAccount)
{
    if (!viewLowest_)
        balance_.negate();
}

json::Value
TrustLineBase::getJson(int)
{
    json::Value ret(json::ValueType::Object);
    ret["low_id"] = to_string(lowLimit_.getIssuer());
    ret["high_id"] = to_string(highLimit_.getIssuer());
    return ret;
}

std::optional<PathFindTrustLine>
PathFindTrustLine::makeItem(AccountID const& accountID, SLE::const_ref sle)
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
    LineDirection direction = LineDirection::Outgoing)
{
    std::vector<T> items;
    forEachItem(view, accountID, [&items, &accountID, &direction](SLE::const_ref sleCur) {
        auto ret = T::makeItem(accountID, sleCur);
        if (ret && (direction == LineDirection::Outgoing || !ret->getNoRipple()))
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
    return detail::getTrustLineItems<PathFindTrustLine>(accountID, view, direction);
}

RPCTrustLine::RPCTrustLine(SLE::const_ref sle, AccountID const& viewAccount)
    : TrustLineBase(sle, viewAccount)
    , lowQualityIn_(sle->getFieldU32(sfLowQualityIn))
    , lowQualityOut_(sle->getFieldU32(sfLowQualityOut))
    , highQualityIn_(sle->getFieldU32(sfHighQualityIn))
    , highQualityOut_(sle->getFieldU32(sfHighQualityOut))
{
}

std::optional<RPCTrustLine>
RPCTrustLine::makeItem(AccountID const& accountID, SLE::const_ref sle)
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

}  // namespace xrpl
