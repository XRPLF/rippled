#include <xrpl/protocol/STAmount.h>

#include <xrpl/basics/Number.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/Quality.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/protocol/jss.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <stdexcept>

using namespace xrpl;

namespace {

// A 25% transfer fee.
constexpr std::uint32_t kTransferRate125 = 1'250'000'000;

AccountID const kUsdIssuer{1};

}  // namespace

TEST(STAmount, mul_ratio_iou)
{
    Issue const usd{toCurrency("USD"), kUsdIssuer};

    EXPECT_EQ(
        mulRatio(STAmount{usd, 100}, QUALITY_ONE, kTransferRate125, Number::RoundingMode::Downward),
        STAmount(usd, 80));
    EXPECT_EQ(
        mulRatio(STAmount{usd, 80}, kTransferRate125, QUALITY_ONE, Number::RoundingMode::Upward),
        STAmount(usd, 100));

    // 1 / 1.7 does not fit in a 16-digit mantissa, so the rounding direction
    // decides the last digit.
    STAmount const one{usd, 1};
    EXPECT_LT(
        mulRatio(one, QUALITY_ONE, 1'700'000'000, Number::RoundingMode::Downward),
        mulRatio(one, QUALITY_ONE, 1'700'000'000, Number::RoundingMode::Upward));

    STAmount const full{usd, std::uint64_t{9'999'999'999'999'999}, -5};
    EXPECT_EQ(mulRatio(full, QUALITY_ONE, QUALITY_ONE, Number::RoundingMode::Downward), full);
}

TEST(STAmount, mul_ratio_mpt)
{
    MPTIssue const mpt{makeMptID(1, kUsdIssuer)};

    EXPECT_EQ(
        mulRatio(STAmount{mpt, 31}, QUALITY_ONE, kTransferRate125, Number::RoundingMode::Downward),
        STAmount(mpt, 24));
    EXPECT_EQ(
        mulRatio(STAmount{mpt, 31}, QUALITY_ONE, kTransferRate125, Number::RoundingMode::Upward),
        STAmount(mpt, 25));
    EXPECT_EQ(
        mulRatio(STAmount{mpt, 1}, QUALITY_ONE, kTransferRate125, Number::RoundingMode::Downward),
        STAmount(mpt));
    EXPECT_EQ(
        mulRatio(STAmount{mpt, 1}, QUALITY_ONE, kTransferRate125, Number::RoundingMode::Upward),
        STAmount(mpt, 1));

    STAmount const max{mpt, kMaxMpTokenAmount};
    EXPECT_EQ(mulRatio(max, QUALITY_ONE, QUALITY_ONE, Number::RoundingMode::Downward), max);
    EXPECT_NO_THROW(
        (void)mulRatio(max, QUALITY_ONE, kTransferRate125, Number::RoundingMode::Downward));
    EXPECT_THROW(
        (void)mulRatio(max, kTransferRate125, QUALITY_ONE, Number::RoundingMode::Upward),
        std::overflow_error);
}

TEST(STAmount, mul_ratio_xrp)
{
    EXPECT_EQ(
        mulRatio(
            STAmount{XRPAmount{31}}, QUALITY_ONE, kTransferRate125, Number::RoundingMode::Downward),
        STAmount{XRPAmount{24}});
    EXPECT_EQ(
        mulRatio(
            STAmount{XRPAmount{31}}, QUALITY_ONE, kTransferRate125, Number::RoundingMode::Upward),
        STAmount{XRPAmount{25}});
}

// A JSON number from 2^31 to 2^32 - 1 parses as a json::UInt. amountFromJson
// must read it from the "value" member, not from the enclosing object or array.
TEST(STAmount, from_json_uint_value)
{
    Issue const usd{toCurrency("USD"), kUsdIssuer};
    MPTIssue const mpt{makeMptID(1, kUsdIssuer)};

    auto iouObject = [&](json::Value const& value) {
        json::Value v(json::ValueType::Object);
        v[jss::currency] = "USD";
        v[jss::issuer] = toBase58(kUsdIssuer);
        v[jss::value] = value;
        return v;
    };

    auto iouArray = [&](json::Value const& value) {
        json::Value v(json::ValueType::Array);
        v.append(value);
        v.append("USD");
        v.append(toBase58(kUsdIssuer));
        return v;
    };

    auto mptObject = [&](json::Value const& value) {
        json::Value v(json::ValueType::Object);
        v[jss::mpt_issuance_id] = to_string(mpt.getMptID());
        v[jss::value] = value;
        return v;
    };

    std::uint64_t const intMax = std::numeric_limits<std::int32_t>::max();
    for (std::uint64_t const n :
         {intMax + 1, std::uint64_t{3'000'000'000}, std::uint64_t{json::Value::kMaxUInt}})
    {
        json::Value const value{static_cast<json::UInt>(n)};
        ASSERT_TRUE(value.isUInt());
        ASSERT_FALSE(value.isInt());

        EXPECT_EQ(amountFromJson(sfAmount, iouObject(value)), STAmount(usd, n));
        EXPECT_EQ(amountFromJson(sfAmount, iouArray(value)), STAmount(usd, n));
        EXPECT_EQ(amountFromJson(sfAmount, mptObject(value)), STAmount(mpt, n));

        // XRP, as a bare number and in array form.
        STAmount const xrp{XRPAmount{static_cast<std::int64_t>(n)}};
        json::Value xrpArray(json::ValueType::Array);
        xrpArray.append(value);
        EXPECT_EQ(amountFromJson(sfAmount, value), xrp);
        EXPECT_EQ(amountFromJson(sfAmount, xrpArray), xrp);

        STAmount result;
        EXPECT_TRUE(amountFromJsonNoThrow(result, iouObject(value)));
        EXPECT_EQ(result, STAmount(usd, n));
    }

    // Values that parse as json::Int keep working.
    json::Value const intValue{std::numeric_limits<std::int32_t>::max()};
    ASSERT_TRUE(intValue.isInt());
    EXPECT_EQ(amountFromJson(sfAmount, iouObject(intValue)), STAmount(usd, intMax));
    EXPECT_EQ(amountFromJson(sfAmount, mptObject(intValue)), STAmount(mpt, intMax));
}
