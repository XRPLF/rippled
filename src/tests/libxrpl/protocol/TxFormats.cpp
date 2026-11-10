#include <xrpl/protocol/TxFormats.h>

#include <xrpl/basics/safe_cast.h>
#include <xrpl/json/json_value.h>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace xrpl {

namespace {

// A TransactionType value and what txTypeFromJson returns for it.
struct TxTypeJsonCase
{
    std::string_view name;
    json::Value value;
    std::expected<TxType, TxTypeJsonError> expected;
};

constexpr auto kInvalidData = std::unexpected(TxTypeJsonError::InvalidData);
constexpr auto kBadType = std::unexpected(TxTypeJsonError::BadType);

auto const kTxTypeJsonCases = std::to_array<TxTypeJsonCase>({
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
    {.name = "unknown_name", .value = "Bogus", .expected = kInvalidData},
    {.name = "lowercase_name", .value = "payment", .expected = kInvalidData},
    {.name = "plus_sign_string", .value = "+1", .expected = kInvalidData},
    {.name = "minus_sign_string", .value = "-1", .expected = kInvalidData},
    {.name = "leading_space_string", .value = " 1", .expected = kInvalidData},
    {.name = "empty_string", .value = "", .expected = kInvalidData},
    {.name = "trailing_letter_string", .value = "1x", .expected = kInvalidData},
    {.name = "hex_string", .value = "0x1", .expected = kInvalidData},
    {.name = "decimal_string", .value = "1.0", .expected = kInvalidData},
    {.name = "one_above_max_string", .value = "65536", .expected = kInvalidData},
    {.name = "negative_int", .value = -1, .expected = kInvalidData},
    {.name = "one_above_max_int", .value = 65'536, .expected = kInvalidData},
    {.name = "one_above_max_uint", .value = 65'536u, .expected = kInvalidData},
    {.name = "max_uint", .value = 4'294'967'295u, .expected = kInvalidData},
    {.name = "bool_value", .value = true, .expected = kBadType},
    {.name = "double_value", .value = 1.5, .expected = kBadType},
    {.name = "null_value", .value = json::Value{}, .expected = kBadType},
    {.name = "array_value", .value = json::Value{json::ValueType::Array}, .expected = kBadType},
    {.name = "object_value", .value = json::Value{json::ValueType::Object}, .expected = kBadType},
});

}  // namespace

class TxTypeFromJsonTest : public ::testing::TestWithParam<TxTypeJsonCase>
{
};

TEST_P(TxTypeFromJsonTest, reads_transaction_type)
{
    auto const& testCase = GetParam();

    auto const result = txTypeFromJson(testCase.value);
    ASSERT_EQ(result.has_value(), testCase.expected.has_value());
    if (result)
    {
        EXPECT_EQ(*result, *testCase.expected);
    }
    else
    {
        EXPECT_EQ(result.error(), testCase.expected.error());
    }
}

INSTANTIATE_TEST_SUITE_P(
    transaction_type,
    TxTypeFromJsonTest,
    ::testing::ValuesIn(kTxTypeJsonCases),
    [](::testing::TestParamInfo<TxTypeJsonCase> const& info) {
        return std::string{info.param.name};
    });

}  // namespace xrpl
