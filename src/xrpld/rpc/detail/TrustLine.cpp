#include <xrpld/rpc/detail/TrustLine.h>

#include <xrpl/json/json_value.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>
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

PathFindTrustLine::ChunkResult
PathFindTrustLine::getItemsChunk(
    AccountID const& accountID,
    ReadView const& view,
    LineDirection direction,
    DirCursor const& start,
    std::size_t maxLines)
{
    ChunkResult result;
    result.cursor = start;

    if (start.complete || maxLines == 0)
        return result;

    // Walk the owner directory so we can abort once maxLines is hit and resume
    // from DirCursor on a later expand. forEachItem cannot early-exit.
    auto const root = keylet::ownerDir(accountID);
    if (root.type != ltDIR_NODE)
    {
        result.cursor = {};
        result.cursor.complete = true;
        return result;
    }

    // page 0 = root owner-dir page; otherwise keylet::page(root, page).
    std::uint64_t currentPage = start.page;
    std::size_t indexInPage = start.indexInPage;

    while (result.lines.size() < maxLines)
    {
        auto const pos = currentPage == 0 ? root : keylet::page(root, currentPage);
        auto sle = view.read(pos);
        if (!sle)
        {
            result.cursor = {};
            result.cursor.complete = true;
            break;
        }

        auto const& indexes = sle->getFieldV256(sfIndexes);
        bool hitCap = false;
        while (indexInPage < indexes.size())
        {
            if (result.lines.size() >= maxLines)
            {
                hitCap = true;
                break;
            }

            auto const& key = indexes[indexInPage];
            ++indexInPage;

            auto const sleCur = view.read(keylet::child(key));
            if (!sleCur || sleCur->getType() != ltRIPPLE_STATE)
                continue;

            auto ret = makeItem(accountID, sleCur);
            if (!ret)
                continue;
            if (direction == LineDirection::Incoming && ret->getNoRipple())
                continue;

            result.lines.push_back(std::move(*ret));
        }

        if (hitCap || result.lines.size() >= maxLines)
        {
            result.cursor.page = currentPage;
            result.cursor.indexInPage = indexInPage;
            result.cursor.complete = false;
            break;
        }

        auto const next = sle->getFieldU64(sfIndexNext);
        if (next == 0u)
        {
            result.cursor = {};
            result.cursor.complete = true;
            break;
        }

        currentPage = next;
        indexInPage = 0;
    }

    result.lines.shrink_to_fit();
    return result;
}

std::vector<PathFindTrustLine>
PathFindTrustLine::getItems(
    AccountID const& accountID,
    ReadView const& view,
    LineDirection direction,
    std::size_t maxLines)
{
    // Compatibility wrapper: single walk (optionally capped).
    DirCursor const cursor;
    auto const want = maxLines == 0 ? std::numeric_limits<std::size_t>::max() : maxLines;
    auto chunk = getItemsChunk(accountID, view, direction, cursor, want);
    return std::move(chunk.lines);
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
    // This list may be around for a while, so free up any unneeded capacity
    items.shrink_to_fit();

    return items;
}
}  // namespace detail

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
