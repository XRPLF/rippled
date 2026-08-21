#pragma once

#include <xrpl/basics/Slice.h>
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
// Only the methods the tests beside it exercise are mocked, and that is deliberate: the
// rest keep `HostFunctions`' own `std::unexpected(Unimplemented)`, so a contract reaching
// for one fails the way production would. Add a `MOCK_METHOD` here when a test needs to
// say what that host function answers.
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

    // Takes the rendered text, not the guest's buffer: rendering is `HostContext`'s, so what
    // a test asserts here is the log line a node would write.
    MOCK_METHOD(
        void,
        trace,
        (std::string_view const& msg, std::string_view const& data),
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
