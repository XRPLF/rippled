#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/HostContextFixture.h>

#include <cstdint>
#include <expected>
#include <stdexcept>

namespace xrpl::test {

namespace {

Bytes
serialized(STAmount const& amount)
{
    Serializer s;
    amount.add(s);
    return s.getData();
}

}  // namespace

// The only file exercising `parseST<STAmount>`. A malformed buffer throws inside `STAmount`'s
// deserializing constructor; `parseST` catches that itself, so the host is never asked - unlike
// a `guarded`-caught throw from the host's own body.
struct FloatFromSTAmountCall : HostContextTest
{
    STAmount const amount{XRPAmount{1000}};
    Bytes const wireBytes = serialized(amount);
    std::int32_t const mode = 1;
};

TEST_F(FloatFromSTAmountCall, SerializedAmountDecodesToValueHostIsAskedFor)
{
    Bytes const result{1, 2, 3};
    EXPECT_CALL(host, floatFromSTAmount(testing::Eq(amount), mode))
        .WillOnce(testing::Return(result));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.floatFromSTAmount(bytesOf(wireBytes), mode, out.slice()),
        static_cast<std::int32_t>(result.size()));
    EXPECT_TRUE(out.holds(bytesOf(result)));
}

TEST_F(FloatFromSTAmountCall, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, floatFromSTAmount(testing::Eq(amount), mode))
        .WillOnce(testing::Return(std::unexpected(HostFunctionError::FloatComputationError)));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.floatFromSTAmount(bytesOf(wireBytes), mode, out.slice()),
        hfErrorToInt(HostFunctionError::FloatComputationError));
    EXPECT_FALSE(out.wasWritten());
}

TEST_F(FloatFromSTAmountCall, HostExceptionBecomesInternalFatalAndIsLogged)
{
    EXPECT_CALL(host, floatFromSTAmount(testing::Eq(amount), mode))
        .WillOnce(testing::Throw(std::runtime_error{"float from st amount came apart"}));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.floatFromSTAmount(bytesOf(wireBytes), mode, out.slice()),
        hfErrorToInt(HostFunctionError::InternalFatal));
    EXPECT_THAT(logged(), testing::HasSubstr("float from st amount came apart"));
    EXPECT_THAT(logged(), testing::HasSubstr("floatFromSTAmount"));
}

// `parseST` catches its own failure: a malformed buffer never reaches the host at all.
TEST_F(FloatFromSTAmountCall, MalformedBytesAreRefusedWithoutAskingHost)
{
    Bytes const malformedBytes{0xff, 0xff, 0xff};
    EXPECT_CALL(host, floatFromSTAmount).Times(0);

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.floatFromSTAmount(bytesOf(malformedBytes), mode, out.slice()),
        hfErrorToInt(HostFunctionError::InvalidParams));
}

// The out-region contract: write only if the whole value fits, and return the true length
// either way.
TEST_F(FloatFromSTAmountCall, ShortOutRegionWritesNothingAndReturnsTrueLength)
{
    Bytes const result{1, 2, 3};
    EXPECT_CALL(host, floatFromSTAmount(testing::Eq(amount), mode))
        .WillOnce(testing::Return(result));

    OutRegion out{result.size() - 1};
    EXPECT_EQ(
        hostContext.floatFromSTAmount(bytesOf(wireBytes), mode, out.slice()),
        static_cast<std::int32_t>(result.size()));
    EXPECT_FALSE(out.wasWritten());
}

// Bytes that parse are not yet bytes that are safe to use: both branches of
// `STAmount(SerialIter&, SField const&)` return before `canonicalize`, where the bounds are.
// `STAmount`'s own constructors canonicalize, so these are laid out by hand.
struct FloatFromSTAmountBounds : HostContextTest
{
    std::int32_t const mode = 1;

    // The MPT wire form: eight bytes carrying the flags, one more byte of magnitude, then the
    // 192-bit issuance id. The magnitude is reassembled as `(first << 8) | second`, so the
    // flag bits shift out and the low 56 bits of the first word hold all but the last byte.
    static Bytes
    serializedMpt(std::uint64_t magnitude, bool positive)
    {
        auto header = (magnitude >> 8) | STAmount::kMpToken;
        if (positive)
        {
            header |= STAmount::kPositive;
        }

        auto s = Serializer{};
        s.add64(header);
        s.add8(static_cast<unsigned char>(magnitude & 0xffU));
        s.addBitString(MPTID{42});
        return s.getData();
    }

    // The XRP wire form is the eight bytes alone; `kValueMask` clears the two flag bits, so
    // everything below them is magnitude.
    static Bytes
    serializedXrp(std::uint64_t drops)
    {
        auto s = Serializer{};
        s.add64(drops | STAmount::kPositive);
        return s.getData();
    }
};

TEST_F(FloatFromSTAmountBounds, AnMptMagnitudeAtTheSignBitIsRefused)
{
    EXPECT_CALL(host, floatFromSTAmount).Times(0);

    auto const wire = serializedMpt(std::uint64_t{1} << 63U, /*positive*/ false);
    auto out = OutRegion{32};
    EXPECT_EQ(
        hostContext.floatFromSTAmount(bytesOf(wire), mode, out.slice()),
        hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(FloatFromSTAmountBounds, AnMptMagnitudePastTheProtocolMaximumIsRefused)
{
    EXPECT_CALL(host, floatFromSTAmount).Times(0);

    auto const wire = serializedMpt(kMaxMpTokenAmount + 1, /*positive*/ true);
    auto out = OutRegion{32};
    EXPECT_EQ(
        hostContext.floatFromSTAmount(bytesOf(wire), mode, out.slice()),
        hfErrorToInt(HostFunctionError::InvalidParams));
}

// MPT amounts are non-negative by protocol invariant, which is why `isLegalMPT` asks.
TEST_F(FloatFromSTAmountBounds, ANegativeMptIsRefused)
{
    EXPECT_CALL(host, floatFromSTAmount).Times(0);

    auto const wire = serializedMpt(1000, /*positive*/ false);
    auto out = OutRegion{32};
    EXPECT_EQ(
        hostContext.floatFromSTAmount(bytesOf(wire), mode, out.slice()),
        hfErrorToInt(HostFunctionError::InvalidParams));
}

// `isLegalNet`'s half: the XRP branch skips `canonicalize` too, so drops past the network
// maximum reach the host unchallenged without this.
TEST_F(FloatFromSTAmountBounds, XrpDropsPastTheNetworkMaximumAreRefused)
{
    EXPECT_CALL(host, floatFromSTAmount).Times(0);

    auto const wire = serializedXrp(STAmount::kMaxNativeN + 1);
    auto out = OutRegion{32};
    EXPECT_EQ(
        hostContext.floatFromSTAmount(bytesOf(wire), mode, out.slice()),
        hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(FloatFromSTAmountBounds, TheMaximaThemselvesStillReachTheHost)
{
    auto const result = Bytes{1, 2, 3};
    EXPECT_CALL(host, floatFromSTAmount(testing::_, mode))
        .Times(2)
        .WillRepeatedly(testing::Return(result));

    for (auto const& wire :
         {serializedMpt(kMaxMpTokenAmount, /*positive*/ true),
          serializedXrp(STAmount::kMaxNativeN)})
    {
        auto out = OutRegion{32};
        EXPECT_EQ(
            hostContext.floatFromSTAmount(bytesOf(wire), mode, out.slice()),
            static_cast<std::int32_t>(result.size()));
    }
}

}  // namespace xrpl::test
