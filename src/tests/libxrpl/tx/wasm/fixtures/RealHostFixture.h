#pragma once

#include <xrpl/protocol/Keylet.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <tx/wasm/fixtures/WasmLedger.h>

#include <expected>
#include <source_location>

// The GTest layer over `WasmLedger`: the assertion helpers, and the fixture base the
// `host_functions/` tests derive from.
//
// Everything that touches a ledger or builds a host lives in `WasmLedger.h`, which knows nothing
// about GTest so the benchmarks can share it. Only what genuinely needs the framework is here.

namespace xrpl::test {

template <typename T, typename U>
void
expectValue(
    std::expected<T, HostFunctionError> const& result,
    U const& expected,
    std::source_location loc = std::source_location::current())
{
    auto trace = testing::ScopedTrace{loc.file_name(), static_cast<int>(loc.line()), ""};
    ASSERT_TRUE(result.has_value())
        << "expected a value, got error " << static_cast<int>(result.error());
    EXPECT_EQ(*result, expected);
}

template <typename T>
void
expectError(
    std::expected<T, HostFunctionError> const& result,
    HostFunctionError expected,
    std::source_location loc = std::source_location::current())
{
    auto trace = testing::ScopedTrace{loc.file_name(), static_cast<int>(loc.line()), ""};
    ASSERT_FALSE(result.has_value()) << "expected error, got a value";
    EXPECT_EQ(result.error(), expected);
}

void
expectKeyletMatches(std::expected<Bytes, HostFunctionError> const& result, Keylet const& expected);

// A `WasmLedger` with GTest's lifecycle attached. Tests derive from this; benchmarks use
// `WasmLedger` directly.
struct RealHostFixture : testing::Test, WasmLedger
{
};

}  // namespace xrpl::test
