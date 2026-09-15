#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/Feature.h>

#include <gtest/gtest.h>
#include <tx/wasm/fixtures/RealHostFixture.h>

#include <string_view>

namespace xrpl::test {

struct IsAmendmentEnabledImpl : RealHostFixture
{
};

TEST_F(IsAmendmentEnabledImpl, EnabledAmendmentByIdReadsOne)
{
    auto const id = getRegisteredFeature("TokenEscrow");
    ASSERT_TRUE(id.has_value());
    auto const result = makeHost()->isAmendmentEnabled(id.value_or(uint256{}));
    expectValue(result, 1);
}

TEST_F(IsAmendmentEnabledImpl, EnabledAmendmentByNameReadsOne)
{
    auto const result = makeHost()->isAmendmentEnabled(std::string_view{"TokenEscrow"});

    expectValue(result, 1);
}

TEST_F(IsAmendmentEnabledImpl, UnknownAmendmentByIdReadsZero)
{
    auto const result = makeHost()->isAmendmentEnabled(
        uint256{"DEADBEEF00000000000000000000000000000000000000000000000000000000"});

    expectValue(result, 0);
}

TEST_F(IsAmendmentEnabledImpl, UnknownAmendmentNameReadsZero)
{
    auto const result = makeHost()->isAmendmentEnabled(std::string_view{"DEADBEEF"});

    expectValue(result, 0);
}

}  // namespace xrpl::test
