#include <xrpl/basics/Number.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/tx/wasm/HostFunc.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/MockHostFunctions.h>
#include <tx/wasm/WasmFixture.h>
// For `TraceDataType`, which the bridge declares and this header defines.
#include <xrpl_wasm_vm_ffi_cxxbridge/lib.h>

#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <utility>

namespace xrpl::test {

namespace {

// Bytes as a WAT data segment's contents. Hex-escaped throughout, so a buffer needs no
// thought about which of its bytes the text format would otherwise read.
std::string
watBytes(Bytes const& bytes)
{
    std::string escaped;
    escaped.reserve(bytes.size() * 4);
    for (auto const byte : bytes)
        escaped += std::format("\\{:02x}", byte);
    return escaped;
}

Bytes
serialized(STAmount const& amount)
{
    Serializer s;
    amount.add(s);
    return s.getData();
}

}  // namespace

// trace — a message, a data type, and a buffer holding what that type says. One import for
// what were five, so what a test varies is the type rather than the function.
//
// The buffer arrives as bytes and leaves as text: `HostContext` renders it, and the host is
// handed the finished line. So a test says which renderer the type selected.
struct TraceCall : HostCallTest
{
    static constexpr std::int32_t kDataAt = 64;

    // What the guest passes. `typeCode` rather than a `TraceDataType` so a test can send a
    // code that names no type, which is the guest's to get wrong.
    std::int32_t typeCode{static_cast<std::int32_t>(TraceDataType::AsText)};
    Bytes data;

    void
    traces(TraceDataType type, Bytes bytes)
    {
        typeCode = static_cast<std::int32_t>(type);
        data = std::move(bytes);
    }

    void
    traces(TraceDataType type, std::string_view text)
    {
        traces(type, Bytes{text.begin(), text.end()});
    }

    [[nodiscard]] std::string
    wat() const override
    {
        // {0} data offset, {1} the data itself, {2} the type under test, {3} its length,
        // {4} a type the constant modules can name, {5} the data cap.
        return std::format(
            R"wat(
(module
  (import "host_lib" "trace" (func $trace (param i32 i32 i32 i32 i32)))
  (memory (export "memory") 1)
  (data (i32.const 0) "note")
  (data (i32.const {0}) "{1}")

  (func (export "escrow_finish") (result i32)
    (call $trace (i32.const 0) (i32.const 4) (i32.const {2}) (i32.const {0}) (i32.const {3}))
    (i32.const 1))

  (func (export "unnamed_type") (result i32)
    (call $trace (i32.const 0) (i32.const 4) (i32.const 0) (i32.const {0}) (i32.const 0))
    (i32.const 1))

  (func (export "past_memory") (result i32)
    (call $trace (i32.const 0) (i32.const 4) (i32.const {4}) (i32.const 65536) (i32.const 1))
    (i32.const 1))

  (func (export "too_long") (result i32)
    (call $trace (i32.const 0) (i32.const 4) (i32.const {4}) (i32.const {0}) (i32.const {5}))
    (i32.const 1)))
)wat",
            kDataAt,
            watBytes(data),
            typeCode,
            data.size(),
            static_cast<std::int32_t>(TraceDataType::AsHex),
            kMaxWasmDataLength);
    }

    // The line the host was handed, for a run that is expected to reach it.
    void
    expectTraced(std::string_view text)
    {
        EXPECT_CALL(host, trace(std::string_view("note"), text));

        EXPECT_EQ(hostAnswer(), 1) << "the contract runs on past its trace";
    }
};

// The eight-byte types are the pair worth naming: the same bytes, and the type is the whole
// difference between the two readings.
TEST_F(TraceCall, Int64ReadsTheBufferSigned)
{
    traces(TraceDataType::Int64, Bytes(8, 0xff));

    expectTraced("-1");
}

TEST_F(TraceCall, Uint64ReadsTheSameBufferUnsigned)
{
    traces(TraceDataType::Uint64, Bytes(8, 0xff));

    expectTraced("18446744073709551615");
}

TEST_F(TraceCall, AsTextTakesTheBufferVerbatim)
{
    traces(TraceDataType::AsText, "hello");

    expectTraced("hello");
}

TEST_F(TraceCall, AsHexEncodesTheBuffer)
{
    traces(TraceDataType::AsHex, Bytes{0x07, 0x08, 0xff});

    expectTraced("0708FF");
}

// The zero account, so the expectation is the well-known base58 rather than a rendering of
// whatever the renderer happened to do.
TEST_F(TraceCall, AccountIsBase58)
{
    traces(TraceDataType::Account, Bytes(AccountID::size(), 0));

    expectTraced("rrrrrrrrrrrrrrrrrrrrrhoLvTp");
}

TEST_F(TraceCall, AmountCarriesItsAssetIntoTheText)
{
    traces(TraceDataType::Amount, serialized(STAmount{XRPAmount{1000}}));

    expectTraced("1000/XRP");
}

TEST_F(TraceCall, XfloatIsDecodedToItsValue)
{
    auto const encoded = wasm_float::floatFromIntImpl(
        42, static_cast<std::int32_t>(Number::RoundingMode::ToNearest));
    ASSERT_TRUE(encoded.has_value());
    traces(TraceDataType::Xfloat, *encoded);

    expectTraced("42");
}

// The width is part of the type, and a buffer that is not it holds no value to print. The
// contract is not told: a trace answers nothing at all.
TEST_F(TraceCall, ABufferOfTheWrongWidthIsDropped)
{
    traces(TraceDataType::Int64, Bytes(4, 0xff));

    EXPECT_CALL(host, trace).Times(0);
    EXPECT_EQ(hostAnswer(), 1);
}

// `STAmount`'s deserializer rejects this by throwing, which must not escape into the run.
TEST_F(TraceCall, AMalformedAmountIsDroppedRatherThanThrown)
{
    traces(TraceDataType::Amount, Bytes(3, 0xff));

    EXPECT_CALL(host, trace).Times(0);
    EXPECT_EQ(hostAnswer(), 1);
}

// Zero is the code a guest sends by omission, which is why no type carries it.
TEST_F(TraceCall, ACodeThatNamesNoTypeIsDropped)
{
    EXPECT_CALL(host, trace).Times(0);

    EXPECT_EQ(hostAnswer("unnamed_type"), 1);
}

// The memory policy every input region is held to, on the one call that cannot report it.
TEST_F(TraceCall, ARegionPastMemoryIsDropped)
{
    EXPECT_CALL(host, trace).Times(0);

    EXPECT_EQ(hostAnswer("past_memory"), 1);
}

TEST_F(TraceCall, AMessageAndBufferPastTheDataCapAreDropped)
{
    EXPECT_CALL(host, trace).Times(0);

    EXPECT_EQ(hostAnswer("too_long"), 1);
}

}  // namespace xrpl::test
