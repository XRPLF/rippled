#pragma once

#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/tx/wasm/HostFunc.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>

#include <cstdint>
#include <expected>
#include <string_view>

namespace xrpl::test {

// A mock of the host the wasm engine calls back into.
//
// Only the methods the ABI currently declares are mocked, and that is deliberate: the ~60
// others keep `HostFunctions`' own `std::unexpected(Unimplemented)`, so a contract reaching
// for something the ABI has not declared yet fails the way production would. Add a
// `MOCK_METHOD` here when the matching entry is added to `host_functions!`.
struct MockHostFunctions : HostFunctions
{
    explicit MockHostFunctions(beast::Journal journal) : HostFunctions(journal)
    {
    }

    MOCK_METHOD(bool, checkSelf, (), (const, override));

    MOCK_METHOD(
        (std::expected<std::uint32_t, HostFunctionError>),
        getLedgerSqn,
        (),
        (const, override));

    MOCK_METHOD(
        (std::expected<Bytes, HostFunctionError>),
        getCurrentLedgerObjField,
        (SField const& fname),
        (const, override));

    MOCK_METHOD(
        (std::expected<Hash, HostFunctionError>),
        computeSha512HalfHash,
        (Slice const& data),
        (const, override));

    MOCK_METHOD(
        (std::expected<std::int32_t, HostFunctionError>),
        trace,
        (std::string_view const& msg, Slice const& data, bool asHex),
        (const, override));

    MOCK_METHOD(
        (std::expected<std::int32_t, HostFunctionError>),
        traceNum,
        (std::string_view const& msg, std::int64_t number),
        (const, override));
};

// Matches a `Slice` (or anything with `data()`/`size()`) against the bytes of a string, so
// an expectation can say *what* the guest asked the host to work on.
MATCHER_P(BytesAre, expected, "")
{
    return std::string_view{reinterpret_cast<char const*>(arg.data()), arg.size()} ==
        std::string_view{expected};
}

}  // namespace xrpl::test
