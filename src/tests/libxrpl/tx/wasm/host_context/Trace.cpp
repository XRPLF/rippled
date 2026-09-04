#include <xrpl/beast/utility/Journal.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/HostContextFixture.h>
#include <xrpl_wasm_vm_ffi_cxxbridge/lib.h>

#include <array>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace xrpl::test {

struct TraceDirectCall : HostContextTest
{
};

// The catch sits in `trace` itself, not in `guarded`. It logs at trace level, below the
// fixture's default threshold, so the threshold is lowered to observe it.
TEST_F(TraceDirectCall, HostExceptionIsSwallowedRatherThanEscaping)
{
    sink.threshold(beast::Severity::Trace);
    EXPECT_CALL(host, trace).WillOnce(testing::Throw(std::runtime_error{"trace sink came apart"}));

    hostContext.trace("note", bytesOf(Bytes{'h', 'i'}), TraceDataType::AsText);

    EXPECT_THAT(logged(), testing::HasSubstr("trace sink came apart"));
}

// The cap is on message and data together, not on data alone.
TEST_F(TraceDirectCall, MessagePlusDataPastCapIsDroppedWithoutAskingHost)
{
    Bytes const data(kMaxWasmDataLength, 0x41);
    EXPECT_CALL(host, trace).Times(0);

    hostContext.trace("x", bytesOf(data), TraceDataType::AsText);
}

TEST_F(TraceDirectCall, CodesNamingNoTypeAreDropped)
{
    // Either side of the seven that name a type, and the ends of the range the guest's `i32`
    // can hold.
    constexpr std::array kCodesNamingNoType{
        std::numeric_limits<std::int32_t>::min(),
        -1,
        0,
        8,
        9,
        std::numeric_limits<std::int32_t>::max()};
    // Bytes several of the types render, so a drop is the code's doing rather than the buffer's.
    Bytes const data(8, 0xff);
    EXPECT_CALL(host, trace).Times(0);

    for (auto const code : kCodesNamingNoType)
    {
        hostContext.trace("note", bytesOf(data), static_cast<TraceDataType>(code));
    }
}

// The only buffer `AsText` cannot take verbatim: an empty `Slice` has a null `data()`, which
// `std::string` may not be handed.
TEST_F(TraceDirectCall, EmptyBufferIsRenderedAsEmptyText)
{
    EXPECT_CALL(host, trace(std::string_view("note"), std::string_view("")));

    hostContext.trace("note", bytesOf(Bytes{}), TraceDataType::AsText);
}

// The exception to the widths below: `floatToString` renders an undecodable buffer as text
// rather than refusing it, so this is the one type whose malformed data still reaches the host.
TEST_F(TraceDirectCall, XfloatOfTheWrongWidthReachesHostAsInvalidData)
{
    EXPECT_CALL(host, trace(std::string_view("note"), std::string_view("Invalid data: FFFFFFFF")));

    hostContext.trace("note", bytesOf(Bytes(4, 0xff)), TraceDataType::Xfloat);
}

// An amount is read rather than measured: the deserializer takes what it needs and is not asked
// whether anything is left, so trailing bytes are ignored rather than refused.
TEST_F(TraceDirectCall, AmountPastItsWidthIsReadFromTheFrontOfTheBuffer)
{
    Bytes const data{0x40, 0, 0, 0, 0, 0, 0x03, 0xe8, 0xff, 0xff, 0xff, 0xff};
    EXPECT_CALL(host, trace(std::string_view("note"), std::string_view("1000/XRP")));

    hostContext.trace("note", bytesOf(data), TraceDataType::Amount);
}

struct TraceRenderingBundle
{
    std::string name;
    TraceDataType type;
    Bytes data;
    std::string text;
};

struct TraceRendering : HostContextTest, testing::WithParamInterface<TraceRenderingBundle>
{
};

TEST_P(TraceRendering, DataIsRenderedAsItsTypeNames)
{
    auto const& rendering = GetParam();
    EXPECT_CALL(host, trace(std::string_view("note"), std::string_view(rendering.text)));

    hostContext.trace("note", bytesOf(rendering.data), rendering.type);
}

INSTANTIATE_TEST_SUITE_P(
    EveryDataType,
    TraceRendering,
    testing::ValuesIn({
        TraceRenderingBundle{
            .name = "Int64",
            .type = TraceDataType::Int64,
            .data = Bytes(8, 0xff),
            .text = "-1"},
        TraceRenderingBundle{
            .name = "Uint64",
            .type = TraceDataType::Uint64,
            .data = Bytes(8, 0xff),
            .text = "18446744073709551615"},
        TraceRenderingBundle{
            .name = "Xfloat",
            .type = TraceDataType::Xfloat,
            .data = Bytes{0, 0, 0, 0, 0, 0, 0, 42, 0, 0, 0, 0},
            .text = "42"},
        TraceRenderingBundle{
            .name = "Account",
            .type = TraceDataType::Account,
            .data = Bytes(AccountID::size(), 0),
            .text = "rrrrrrrrrrrrrrrrrrrrrhoLvTp"},
        TraceRenderingBundle{
            .name = "Amount",
            .type = TraceDataType::Amount,
            .data = Bytes{0x40, 0, 0, 0, 0, 0, 0x03, 0xe8},
            .text = "1000/XRP"},
        TraceRenderingBundle{
            .name = "AsHex",
            .type = TraceDataType::AsHex,
            .data = Bytes{0x07, 0x08, 0xff},
            .text = "0708FF"},
        TraceRenderingBundle{
            .name = "AsText",
            .type = TraceDataType::AsText,
            .data = Bytes{'h', 'e', 'l', 'l', 'o'},
            .text = "hello"},
    }),
    [](testing::TestParamInfo<TraceRenderingBundle> const& info) { return info.param.name; });

// A buffer that does not hold what its type claims. The width is part of the type, and bytes
// that are not it hold no value to print.
struct TraceRefusalBundle
{
    std::string name;
    TraceDataType type;
    Bytes data;
};

struct TraceRefusal : HostContextTest, testing::WithParamInterface<TraceRefusalBundle>
{
};

// A trace answers the guest nothing, so a buffer it cannot read is dropped rather than reported.
TEST_P(TraceRefusal, DataThatDoesNotHoldItsTypeIsDropped)
{
    auto const& refusal = GetParam();
    EXPECT_CALL(host, trace).Times(0);

    hostContext.trace("note", bytesOf(refusal.data), refusal.type);
}

INSTANTIATE_TEST_SUITE_P(
    EveryWidth,
    TraceRefusal,
    testing::ValuesIn({
        TraceRefusalBundle{
            .name = "Int64Short",
            .type = TraceDataType::Int64,
            .data = Bytes(7, 0xff)},
        TraceRefusalBundle{
            .name = "Int64Long",
            .type = TraceDataType::Int64,
            .data = Bytes(9, 0xff)},
        TraceRefusalBundle{.name = "Int64Empty", .type = TraceDataType::Int64, .data = Bytes{}},
        TraceRefusalBundle{
            .name = "Uint64Short",
            .type = TraceDataType::Uint64,
            .data = Bytes(7, 0xff)},
        TraceRefusalBundle{
            .name = "Uint64Long",
            .type = TraceDataType::Uint64,
            .data = Bytes(9, 0xff)},
        TraceRefusalBundle{
            .name = "AccountShort",
            .type = TraceDataType::Account,
            .data = Bytes(AccountID::size() - 1, 0)},
        TraceRefusalBundle{
            .name = "AccountLong",
            .type = TraceDataType::Account,
            .data = Bytes(AccountID::size() + 1, 0)},
        // `STAmount`'s deserializer rejects these by throwing, which must not escape the run.
        TraceRefusalBundle{
            .name = "AmountMalformed",
            .type = TraceDataType::Amount,
            .data = Bytes(3, 0xff)},
        TraceRefusalBundle{.name = "AmountEmpty", .type = TraceDataType::Amount, .data = Bytes{}},
    }),
    [](testing::TestParamInfo<TraceRefusalBundle> const& info) { return info.param.name; });

}  // namespace xrpl::test
