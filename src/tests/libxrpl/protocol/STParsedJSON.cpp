#include <xrpl/protocol/STParsedJSON.h>

#include <xrpl/basics/safe_cast.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/jss.h>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace xrpl {

namespace {

// A TransactionType value and what STParsedJSONObject makes of it: the parsed
// type, or the error message.
struct TransactionTypeCase
{
    std::string_view name;
    json::Value value;
    std::expected<TxType, std::string_view> expected;
};

constexpr std::string_view kInvalidData = "Field 'tx_json.TransactionType' has invalid data.";
constexpr std::string_view kBadType = "Field 'tx_json.TransactionType' has bad type.";

auto const kTransactionTypeCases = std::to_array<TransactionTypeCase>({
    {.name = "payment_name", .value = "Payment", .expected = ttPAYMENT},
    {.name = "payment_int", .value = 0, .expected = ttPAYMENT},
    {.name = "payment_uint", .value = 0u, .expected = ttPAYMENT},
    {.name = "payment_string", .value = "0", .expected = ttPAYMENT},
    {.name = "payment_zero_padded_string", .value = "00", .expected = ttPAYMENT},
    {.name = "batch_name", .value = "Batch", .expected = ttBATCH},
    {.name = "batch_int", .value = 71, .expected = ttBATCH},
    {
        .name = "max_type_string",
        .value = "65535",
        .expected = safeCast<TxType>(std::uint16_t{65'535}),
    },
    {
        .name = "max_type_int",
        .value = 65'535,
        .expected = safeCast<TxType>(std::uint16_t{65'535}),
    },
    {
        .name = "max_type_uint",
        .value = 65'535u,
        .expected = safeCast<TxType>(std::uint16_t{65'535}),
    },
    {.name = "unknown_name", .value = "Bogus", .expected = std::unexpected(kInvalidData)},
    {.name = "lowercase_name", .value = "payment", .expected = std::unexpected(kInvalidData)},
    {.name = "plus_sign_string", .value = "+1", .expected = std::unexpected(kInvalidData)},
    {.name = "minus_sign_string", .value = "-1", .expected = std::unexpected(kInvalidData)},
    {.name = "leading_space_string", .value = " 1", .expected = std::unexpected(kInvalidData)},
    {.name = "empty_string", .value = "", .expected = std::unexpected(kInvalidData)},
    {.name = "trailing_letter_string", .value = "1x", .expected = std::unexpected(kInvalidData)},
    {.name = "hex_string", .value = "0x1", .expected = std::unexpected(kInvalidData)},
    {.name = "decimal_string", .value = "1.0", .expected = std::unexpected(kInvalidData)},
    {.name = "one_above_max_string", .value = "65536", .expected = std::unexpected(kInvalidData)},
    {.name = "negative_int", .value = -1, .expected = std::unexpected(kInvalidData)},
    {.name = "one_above_max_int", .value = 65'536, .expected = std::unexpected(kInvalidData)},
    {.name = "one_above_max_uint", .value = 65'536u, .expected = std::unexpected(kInvalidData)},
    {.name = "max_uint", .value = 4'294'967'295u, .expected = std::unexpected(kInvalidData)},
    {.name = "bool_value", .value = true, .expected = std::unexpected(kBadType)},
    {.name = "double_value", .value = 1.5, .expected = std::unexpected(kBadType)},
    {.name = "null_value", .value = json::Value{}, .expected = std::unexpected(kBadType)},
    {
        .name = "array_value",
        .value = json::Value{json::ValueType::Array},
        .expected = std::unexpected(kBadType),
    },
    {
        .name = "object_value",
        .value = json::Value{json::ValueType::Object},
        .expected = std::unexpected(kBadType),
    },
});

}  // namespace

class STParsedJSONTest : public ::testing::TestWithParam<TransactionTypeCase>
{
};

TEST_P(STParsedJSONTest, reads_transaction_type)
{
    auto const& testCase = GetParam();

    json::Value j{json::ValueType::Object};
    j[sfTransactionType] = testCase.value;
    STParsedJSONObject const parsed("tx_json", j);

    auto const type = parsed.object.transform(
        [](STObject const& object) { return object.getFieldU16(sfTransactionType); });
    if (testCase.expected)
    {
        EXPECT_EQ(type, *testCase.expected) << to_string(parsed.error);
    }
    else
    {
        EXPECT_FALSE(type.has_value());
        EXPECT_EQ(parsed.error[jss::error].asString(), "invalidParams");
        EXPECT_EQ(parsed.error[jss::error_message].asString(), testCase.expected.error());
    }
}

INSTANTIATE_TEST_SUITE_P(
    TransactionType,
    STParsedJSONTest,
    ::testing::ValuesIn(kTransactionTypeCases),
    [](::testing::TestParamInfo<TransactionTypeCase> const& info) {
        return std::string{info.param.name};
    });

}  // namespace xrpl
