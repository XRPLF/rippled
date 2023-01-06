//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2022 Ripple Labs Inc.

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

#include <ripple/protocol/AMM.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/Rules.h>
#include <ripple/protocol/STAmount.h>
#include <ripple/protocol/STObject.h>
#include <ripple/protocol/digest.h>

namespace ripple {

AccountID
ammAccountID(
    std::uint16_t prefix,
    uint256 const& parentHash,
    uint256 const& ammID)
{
    ripesha_hasher rsh;
    auto const hash = sha512Half(prefix, parentHash, ammID);
    rsh(hash.data(), hash.size());
    return AccountID{static_cast<ripesha_hasher::result_type>(rsh)};
}

Currency
ammLPTCurrency(Currency const& cur1, Currency const& cur2)
{
    // AMM LPToken is 0x03 plus 19 bytes of the hash
    std::int32_t constexpr AMMCurrencyCode = 0x03;
    auto const [minC, maxC] = std::minmax(cur1, cur2);
    auto const hash = sha512Half(minC, maxC);
    Currency currency;
    *currency.begin() = AMMCurrencyCode;
    std::copy(
        hash.begin(), hash.begin() + currency.size() - 1, currency.begin() + 1);
    return currency;
}

Issue
ammLPTIssue(
    Currency const& cur1,
    Currency const& cur2,
    AccountID const& ammAccountID)
{
    return Issue(ammLPTCurrency(cur1, cur2), ammAccountID);
}

NotTEC
invalidAMMAsset(
    Issue const& issue,
    std::optional<std::pair<Issue, Issue>> const& pair)
{
    if (badCurrency() == issue.currency)
        return temBAD_CURRENCY;
    if (isXRP(issue) && true != !issue.account)
        return temBAD_ISSUER;
    if (pair && issue != pair->first && issue != pair->second)
        return temAMM_BAD_TOKENS;
    return tesSUCCESS;
}

NotTEC
invalidAMMAssetPair(
    Issue const& issue1,
    Issue const& issue2,
    std::optional<std::pair<Issue, Issue>> const& pair)
{
    if (auto const res = invalidAMMAsset(issue1, pair))
        return res;
    if (auto const res = invalidAMMAsset(issue2, pair))
        return res;
    if (issue1 == issue2)
        return temAMM_BAD_TOKENS;
    return tesSUCCESS;
}

NotTEC
invalidAMMAmount(
    std::optional<STAmount> const& amount,
    std::optional<std::pair<Issue, Issue>> const& pair,
    bool nonNegative)
{
    if (!amount)
        return tesSUCCESS;
    if (auto const res = invalidAMMAsset(amount->issue(), pair))
        return res;
    if (!nonNegative && *amount <= beast::zero)
        return temBAD_AMOUNT;
    return tesSUCCESS;
}

std::optional<std::uint8_t>
ammAuctionTimeSlot(std::uint64_t current, STObject const& auctionSlot)
{
    using namespace std::chrono;
    std::uint32_t constexpr totalSlotTimeSecs = 24 * 3600;
    std::uint32_t constexpr intervals = 20;
    std::uint32_t constexpr intervalDuration = totalSlotTimeSecs / intervals;
    if (auto const expiration = auctionSlot[~sfExpiration])
    {
        auto const diff = current - (*expiration - totalSlotTimeSecs);
        if (diff < totalSlotTimeSecs)
            return diff / intervalDuration;
    }
    return std::nullopt;
}

bool
ammEnabled(Rules const& rules)
{
    return rules.enabled(featureAMM) && rules.enabled(fixUniversalNumber);
}

}  // namespace ripple
