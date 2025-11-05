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
    if (isXRP(issue) && issue.account.isNonZero())
        return temBAD_ISSUER;
    if (pair && issue != pair->first && issue != pair->second)
        return temBAD_AMM_TOKENS;
    return tesSUCCESS;
}

NotTEC
invalidAMMAssetPair(
    Issue const& issue1,
    Issue const& issue2,
    std::optional<std::pair<Issue, Issue>> const& pair)
{
    if (issue1 == issue2)
        return temBAD_AMM_TOKENS;
    if (auto const res = invalidAMMAsset(issue1, pair))
        return res;
    if (auto const res = invalidAMMAsset(issue2, pair))
        return res;
    return tesSUCCESS;
}

NotTEC
invalidAMMAmount(
    STAmount const& amount,
    std::optional<std::pair<Issue, Issue>> const& pair,
    bool validZero)
{
    if (auto const res = invalidAMMAsset(amount.issue(), pair))
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
