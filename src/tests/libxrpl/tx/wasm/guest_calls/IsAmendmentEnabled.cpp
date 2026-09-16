#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/HostCallFixture.h>

#include <cstdint>
#include <expected>
#include <stdexcept>
#include <string>
#include <string_view>

namespace xrpl::test {

using testing::Eq;
using testing::Return;

// amendment_enabled — one region in, the answer returned directly.
//
// The shim reads the region twice over: as a 32-byte amendment id, and, if that is not an
// enabled one, as the name those same bytes spell. `host_context/IsAmendmentEnabled.cpp`
// pins that fall-through below the forward; here it is the guest's bytes that have to reach
// both overloads.
struct IsAmendmentEnabledGuest : HostCallTest
{
    static constexpr std::int32_t kIdAt = 0;
    static constexpr std::int32_t kNameAt = 64;
    static constexpr std::int32_t kTooLongAt = 128;
    static constexpr std::int32_t kIdLen = static_cast<std::int32_t>(uint256::size());

    // One past the 64 bytes the shim will read as a name.
    static constexpr std::int32_t kTooLongLen = 65;

    static constexpr std::string_view kAmendmentName = "MyAmendment";
    static constexpr std::int32_t kNameLen = static_cast<std::int32_t>(kAmendmentName.size());

    static constexpr Arg kId = Arg::region(kIdAt, kIdLen);
    static constexpr Arg kName = Arg::region(kNameAt, kNameLen);
    static constexpr Arg kTooLong = Arg::region(kTooLongAt, kTooLongLen);

    // The overload set means a matcher has to say which of the two it is for, even where it
    // matches anything.
    using IdMatcher = testing::Matcher<uint256 const&>;
    using NameMatcher = testing::Matcher<std::string_view const&>;

    Bytes const idBytes = Bytes(uint256::size(), 0x11);
    uint256 const id = uint256::fromVoid(idBytes.data());
    std::string_view const idAsName{reinterpret_cast<char const*>(idBytes.data()), idBytes.size()};

    Bytes const nameBytes{kAmendmentName.begin(), kAmendmentName.end()};
    Bytes const tooLongBytes = Bytes(kTooLongLen, 0x22);

    [[nodiscard]] std::string
    watFor(Arg amendmentArg) const
    {
        return hostCallWat(
            "amendment_enabled",
            {amendmentArg},
            {{.at = kIdAt, .bytes = idBytes},
             {.at = kNameAt, .bytes = nameBytes},
             {.at = kTooLongAt, .bytes = tooLongBytes}});
    }
};

TEST_F(IsAmendmentEnabledGuest, EnabledIdReachesHostAndAnswersOneWithoutNameLookup)
{
    EXPECT_CALL(host, isAmendmentEnabled(IdMatcher(Eq(id)))).WillOnce(Return(1));
    EXPECT_CALL(host, isAmendmentEnabled(NameMatcher(testing::_))).Times(0);

    auto const wat = watFor(kId);
    EXPECT_EQ(hostAnswer(wat), 1);
}

TEST_F(IsAmendmentEnabledGuest, DisabledIdFallsThroughToNameLookupWithTheSameGuestBytes)
{
    EXPECT_CALL(host, isAmendmentEnabled(IdMatcher(Eq(id)))).WillOnce(Return(0));
    EXPECT_CALL(host, isAmendmentEnabled(NameMatcher(Eq(idAsName)))).WillOnce(Return(1));

    auto const wat = watFor(kId);
    EXPECT_EQ(hostAnswer(wat), 1);
}

TEST_F(IsAmendmentEnabledGuest, IdLookupErrorFallsThroughToNameLookup)
{
    EXPECT_CALL(host, isAmendmentEnabled(IdMatcher(Eq(id))))
        .WillOnce(Return(std::unexpected(HostFunctionError::LedgerObjNotFound)));
    EXPECT_CALL(host, isAmendmentEnabled(NameMatcher(Eq(idAsName)))).WillOnce(Return(0));

    auto const wat = watFor(kId);
    EXPECT_EQ(hostAnswer(wat), 0);
}

TEST_F(IsAmendmentEnabledGuest, NameReachesHostVerbatim)
{
    EXPECT_CALL(host, isAmendmentEnabled(NameMatcher(Eq(kAmendmentName)))).WillOnce(Return(1));

    auto const wat = watFor(kName);
    EXPECT_EQ(hostAnswer(wat), 1);
}

TEST_F(IsAmendmentEnabledGuest, InputOverSixtyFourBytesIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, isAmendmentEnabled(IdMatcher(testing::_))).Times(0);
    EXPECT_CALL(host, isAmendmentEnabled(NameMatcher(testing::_))).Times(0);

    auto const wat = watFor(kTooLong);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::DataFieldTooLarge));
}

TEST_F(IsAmendmentEnabledGuest, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, isAmendmentEnabled(NameMatcher(Eq(kAmendmentName))))
        .WillOnce(Return(std::unexpected(HostFunctionError::FieldNotFound)));

    auto const wat = watFor(kName);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::FieldNotFound));
}

// `guarded` turns the throw into `InternalFatal`, which the engine treats as fatal rather
// than passing back: the run ends, and the guest never resumes to read it.
TEST_F(IsAmendmentEnabledGuest, HostExceptionStopsTheRunAndIsLogged)
{
    EXPECT_CALL(host, isAmendmentEnabled(NameMatcher(Eq(kAmendmentName))))
        .WillOnce(testing::Throw(std::runtime_error{"amendment lookup came apart"}));

    auto const outcome = callHost(watFor(kName));
    ASSERT_FALSE(outcome.has_value());
    EXPECT_EQ(outcome.error().ter, tecINTERNAL);
    EXPECT_THAT(logged(), testing::HasSubstr("isAmendmentEnabled"));
}

TEST_F(IsAmendmentEnabledGuest, InputRegionPastMemoryIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, isAmendmentEnabled(IdMatcher(testing::_))).Times(0);
    EXPECT_CALL(host, isAmendmentEnabled(NameMatcher(testing::_))).Times(0);

    auto const wat = watFor(Arg::region(kOnePage, kIdLen));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::PointerOutOfBounds));
}

TEST_F(IsAmendmentEnabledGuest, NegativeInputPointerIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, isAmendmentEnabled(IdMatcher(testing::_))).Times(0);
    EXPECT_CALL(host, isAmendmentEnabled(NameMatcher(testing::_))).Times(0);

    auto const wat = watFor(Arg::region(-1, kIdLen));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

}  // namespace xrpl::test
