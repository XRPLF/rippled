#include <xrpl/basics/base_uint.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/HostContextFixture.h>

#include <expected>
#include <stdexcept>
#include <string_view>

namespace xrpl::test {

// The whole point of this file: a 32-byte input tries as an amendment id first, and falls back
// to a name lookup - on those same bytes - only if that id lookup does not answer enabled.
struct IsAmendmentEnabledCall : HostContextTest
{
    Bytes const idBytes = Bytes(uint256::size(), 0x11);
    uint256 const id = uint256::fromVoid(idBytes.data());
};

TEST_F(IsAmendmentEnabledCall, ThirtyTwoByteEnabledIdAnswersOneWithoutNameLookup)
{
    EXPECT_CALL(host, isAmendmentEnabled(testing::Matcher<uint256 const&>(testing::Eq(id))))
        .WillOnce(testing::Return(1));
    EXPECT_CALL(host, isAmendmentEnabled(testing::Matcher<std::string_view const&>(testing::_)))
        .Times(0);

    EXPECT_EQ(hostContext.isAmendmentEnabled(bytesOf(idBytes)), 1);
}

// The same 32 bytes, read first as an id and, once that is not an enabled one, as a name.
TEST_F(IsAmendmentEnabledCall, ThirtyTwoByteDisabledIdFallsThroughToNameLookupWithSameBytes)
{
    std::string_view const nameFromBytes{
        reinterpret_cast<char const*>(idBytes.data()), idBytes.size()};
    EXPECT_CALL(host, isAmendmentEnabled(testing::Matcher<uint256 const&>(testing::Eq(id))))
        .WillOnce(testing::Return(0));
    EXPECT_CALL(
        host,
        isAmendmentEnabled(testing::Matcher<std::string_view const&>(testing::Eq(nameFromBytes))))
        .WillOnce(testing::Return(1));

    EXPECT_EQ(hostContext.isAmendmentEnabled(bytesOf(idBytes)), 1);
}

// An id lookup that errors is treated the same as one that says no: both fall through to the
// name lookup rather than surfacing the error.
TEST_F(IsAmendmentEnabledCall, ThirtyTwoByteIdLookupErrorFallsThroughToNameLookup)
{
    EXPECT_CALL(host, isAmendmentEnabled(testing::Matcher<uint256 const&>(testing::Eq(id))))
        .WillOnce(testing::Return(std::unexpected(HostFunctionError::Unimplemented)));
    EXPECT_CALL(host, isAmendmentEnabled(testing::Matcher<std::string_view const&>(testing::_)))
        .WillOnce(testing::Return(1));

    EXPECT_EQ(hostContext.isAmendmentEnabled(bytesOf(idBytes)), 1);
}

// Over 64 bytes cannot be a 32-byte id nor a name short enough to matter, so it is refused
// before either overload runs.
TEST_F(IsAmendmentEnabledCall, InputOverSixtyFourBytesIsRefusedWithoutAskingHost)
{
    Bytes const tooLong(65, 0x22);
    EXPECT_CALL(host, isAmendmentEnabled(testing::Matcher<uint256 const&>(testing::_))).Times(0);
    EXPECT_CALL(host, isAmendmentEnabled(testing::Matcher<std::string_view const&>(testing::_)))
        .Times(0);

    EXPECT_EQ(
        hostContext.isAmendmentEnabled(bytesOf(tooLong)),
        hfErrorToInt(HostFunctionError::DataFieldTooLarge));
}

TEST_F(IsAmendmentEnabledCall, HostErrorBecomesContractReturnValue)
{
    Bytes const name{'F', 'e', 'a', 't', 'u', 'r', 'e'};
    EXPECT_CALL(host, isAmendmentEnabled(testing::Matcher<std::string_view const&>(testing::_)))
        .WillOnce(testing::Return(std::unexpected(HostFunctionError::FieldNotFound)));

    EXPECT_EQ(
        hostContext.isAmendmentEnabled(bytesOf(name)),
        hfErrorToInt(HostFunctionError::FieldNotFound));
}

TEST_F(IsAmendmentEnabledCall, HostExceptionBecomesInternalFatalAndIsLogged)
{
    Bytes const name{'F', 'e', 'a', 't', 'u', 'r', 'e'};
    EXPECT_CALL(host, isAmendmentEnabled(testing::Matcher<std::string_view const&>(testing::_)))
        .WillOnce(testing::Throw(std::runtime_error{"amendment lookup came apart"}));

    EXPECT_EQ(
        hostContext.isAmendmentEnabled(bytesOf(name)),
        hfErrorToInt(HostFunctionError::InternalFatal));
    EXPECT_THAT(logged(), testing::HasSubstr("amendment lookup came apart"));
    EXPECT_THAT(logged(), testing::HasSubstr("isAmendmentEnabled"));
}

TEST_F(IsAmendmentEnabledCall, NameBytesForwardedVerbatimToNameLookup)
{
    std::string_view const name{"MyAmendment"};
    Bytes const nameBytes{name.begin(), name.end()};
    EXPECT_CALL(
        host, isAmendmentEnabled(testing::Matcher<std::string_view const&>(testing::Eq(name))))
        .WillOnce(testing::Return(1));

    EXPECT_EQ(hostContext.isAmendmentEnabled(bytesOf(nameBytes)), 1);
}

}  // namespace xrpl::test
