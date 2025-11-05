#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/Quality.h>
#include <xrpl/protocol/Rate.h>
#include <xrpl/protocol/STAmount.h>

#include <cstdint>

namespace ripple {

Rate const parityRate(QUALITY_ONE);

namespace detail {

STAmount
as_amount(Rate const& rate)
{
    return {noIssue(), rate.value, -9, false};
}

}  // namespace detail

namespace nft {
Rate
transferFeeAsRate(std::uint16_t fee)
{
    return Rate{static_cast<std::uint32_t>(fee) * 10000};
}

}  // namespace nft

STAmount
multiply(STAmount const& amount, Rate const& rate)
{
    XRPL_ASSERT(rate.value, "ripple::nft::multiply : nonzero rate input");

    if (rate == parityRate)
        return amount;

    return multiply(amount, detail::as_amount(rate), amount.asset());
}

STAmount
multiplyRound(STAmount const& amount, Rate const& rate, bool roundUp)
{
    XRPL_ASSERT(rate.value, "ripple::nft::multiplyRound : nonzero rate input");

    if (rate == parityRate)
        return amount;

    return mulRound(amount, detail::as_amount(rate), amount.asset(), roundUp);
}

STAmount
multiplyRound(
    STAmount const& amount,
    Rate const& rate,
    Asset const& asset,
    bool roundUp)
{
    XRPL_ASSERT(
        rate.value, "ripple::nft::multiplyRound(Issue) : nonzero rate input");

    if (rate == parityRate)
    {
        return amount;
    }

    return mulRound(amount, detail::as_amount(rate), asset, roundUp);
}

STAmount
divide(STAmount const& amount, Rate const& rate)
{
    XRPL_ASSERT(rate.value, "ripple::nft::divide : nonzero rate input");

    if (rate == parityRate)
        return amount;

    return divide(amount, detail::as_amount(rate), amount.asset());
}

STAmount
divideRound(STAmount const& amount, Rate const& rate, bool roundUp)
{
    XRPL_ASSERT(rate.value, "ripple::nft::divideRound : nonzero rate input");

    if (rate == parityRate)
        return amount;

    return divRound(amount, detail::as_amount(rate), amount.asset(), roundUp);
}

STAmount
divideRound(
    STAmount const& amount,
    Rate const& rate,
    Asset const& asset,
    bool roundUp)
{
    XRPL_ASSERT(
        rate.value, "ripple::nft::divideRound(Issue) : nonzero rate input");

    if (rate == parityRate)
        return amount;

    return divRound(amount, detail::as_amount(rate), asset, roundUp);
}

}  // namespace ripple
