#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/Feature.h>

#include <gtest/gtest.h>
#include <tx/wasm/RealHostFixture.h>

#include <expected>
#include <string_view>

namespace xrpl::test {

struct IsAmendmentEnabledImpl : WasmImplTest
{
};

TEST_F(IsAmendmentEnabledImpl, EnabledAmendmentByIdReadsOne)
{
    auto const id = getRegisteredFeature("TokenEscrow");
    ASSERT_TRUE(id.has_value());
    auto const result = host().isAmendmentEnabled(id.value_or(uint256{}));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1);
}

TEST_F(IsAmendmentEnabledImpl, EnabledAmendmentByNameReadsOne)
{
    auto const result = host().isAmendmentEnabled(std::string_view{"TokenEscrow"});

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1);
}

TEST_F(IsAmendmentEnabledImpl, UnknownAmendmentByIdReadsZero)
{
    auto const result = host().isAmendmentEnabled(
        uint256{"DEADBEEF00000000000000000000000000000000000000000000000000000000"});

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 0);
}

TEST_F(IsAmendmentEnabledImpl, UnknownAmendmentNameReadsZero)
{
    auto const result = host().isAmendmentEnabled(std::string_view{"DEADBEEF"});

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 0);
}

}  // namespace xrpl::test
