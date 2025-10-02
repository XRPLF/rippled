//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2023 Ripple Labs Inc.

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

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/AMMCore.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/digest.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <utility>

namespace ripple {

Currency
ammLPTCurrency(Asset const& asset1, Asset const& asset2)
{
    // AMM LPToken is 0x03 plus 19 bytes of the hash
    std::int32_t constexpr AMMCurrencyCode = 0x03;
    auto const& [minA, maxA] = std::minmax(asset1, asset2);
    uint256 const hash = std::visit(
        [](auto&& issue1, auto&& issue2) {
            auto fromIss = []<ValidIssueType T>(T const& issue) {
                if constexpr (std::is_same_v<T, Issue>)
                    return issue.currency;
                if constexpr (std::is_same_v<T, MPTIssue>)
                    return issue.getMptID();
            };
            return sha512Half(fromIss(issue1), fromIss(issue2));
        },
        minA.value(),
        maxA.value());
    Currency currency;
    *currency.begin() = AMMCurrencyCode;
    std::copy(
        hash.begin(), hash.begin() + currency.size() - 1, currency.begin() + 1);
    return currency;
}

Issue
ammLPTIssue(
    Asset const& asset1,
    Asset const& asset2,
    AccountID const& ammAccountID)
{
    return Issue(ammLPTCurrency(asset1, asset2), ammAccountID);
}

NotTEC
invalidAMMAsset(
    Asset const& asset,
    std::optional<std::pair<Asset, Asset>> const& pair)
{
    if (asset.holds<MPTIssue>() &&
        asset.get<MPTIssue>().getIssuer() == beast::zero)
        return temBAD_MPT;
    if (asset.holds<Issue>() && badCurrency() == asset.get<Issue>().currency)
        return temBAD_CURRENCY;
    if (isXRP(asset) && asset.getIssuer().isNonZero())
        return temBAD_ISSUER;
    if (pair && asset != pair->first && asset != pair->second)
        return temBAD_AMM_TOKENS;
    return tesSUCCESS;
}

NotTEC
invalidAMMAssetPair(
    Asset const& asset1,
    Asset const& asset2,
    std::optional<std::pair<Asset, Asset>> const& pair)
{
    if (asset1 == asset2)
        return temBAD_AMM_TOKENS;
    if (auto const res = invalidAMMAsset(asset1, pair))
        return res;
    if (auto const res = invalidAMMAsset(asset2, pair))
        return res;
    return tesSUCCESS;
}

NotTEC
invalidAMMAmount(
    STAmount const& amount,
    std::optional<std::pair<Asset, Asset>> const& pair,
    bool validZero)
{
    if (auto const res = invalidAMMAsset(amount.asset(), pair))
        return res;
    if (amount < beast::zero || (!validZero && amount == beast::zero))
        return temBAD_AMOUNT;
    return tesSUCCESS;
}

std::optional<std::uint8_t>
ammAuctionTimeSlot(std::uint64_t current, STObject const& auctionSlot)
{
    // It should be impossible for expiration to be < TOTAL_TIME_SLOT_SECS,
    // but check just to be safe
    auto const expiration = auctionSlot[sfExpiration];
    XRPL_ASSERT(
        expiration >= TOTAL_TIME_SLOT_SECS,
        "ripple::ammAuctionTimeSlot : minimum expiration");
    if (expiration >= TOTAL_TIME_SLOT_SECS)
    {
        if (auto const start = expiration - TOTAL_TIME_SLOT_SECS;
            current >= start)
        {
            if (auto const diff = current - start; diff < TOTAL_TIME_SLOT_SECS)
                return diff / AUCTION_SLOT_INTERVAL_DURATION;
        }
    }
    return std::nullopt;
}

bool
ammEnabled(Rules const& rules)
{
    return rules.enabled(featureAMM) && rules.enabled(fixUniversalNumber);
}

}  // namespace ripple
