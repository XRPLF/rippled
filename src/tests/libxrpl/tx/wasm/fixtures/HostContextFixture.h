#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/tx/wasm/HostContext.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <helpers/CaptureSink.h>
#include <rust/cxx.h>
#include <tx/wasm/fixtures/MockHostFunctions.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

namespace xrpl::test {

// Base for the tests that construct `HostContext` directly, rather than reaching it through
// an assembled module.
struct HostContextTest : testing::Test
{
    static rust::Slice<std::uint8_t const>
    bytesOf(Bytes const& bytes);

    // A scalar's wire form: its bytes little-endian, the way a wasm guest lays them out in
    // memory.
    //
    // Spelled out with shifts rather than a `memcpy` of the value, which would mirror what
    // `answerScalar` does and so assert nothing about the byte order. That is the whole reason
    // this exists, so keep it a shift.
    template <class T>
    static Bytes
    bytesOfScalar(T value)
    {
        static_assert(std::is_integral_v<T>, "Only integral types");

        auto const bits = static_cast<std::make_unsigned_t<T>>(value);
        Bytes bytes(sizeof(bits));
        for (std::size_t i = 0; i < sizeof(bits); ++i)
        {
            bytes[i] = static_cast<std::uint8_t>(bits >> (i * 8));
        }
        return bytes;
    }

    // A locator's wire form: each step as four little-endian bytes.
    static Bytes
    bytesOfSteps(std::vector<std::int32_t> const& steps);

    // Filled with a sentinel rather than left at zero: an answer can itself be all zero, so
    // only a byte no answer produces tells "wrote nothing" apart from "wrote zeros".
    struct OutRegion
    {
        static constexpr std::uint8_t kSentinel = 0xcd;

        std::vector<std::uint8_t> bytes;

        explicit OutRegion(std::size_t capacity);

        rust::Slice<std::uint8_t>
        slice();

        [[nodiscard]] bool
        wasWritten() const;

        // Means "this value and nothing past it".
        [[nodiscard]] bool
        holds(rust::Slice<std::uint8_t const> expected) const;
    };

    CaptureSink sink{beast::Severity::Warning};
    testing::StrictMock<MockHostFunctions> host{beast::Journal{sink}};
    HostContext hostContext{host};

    [[nodiscard]] std::string
    logged() const;
};

// `FieldLocator` has no `operator==` and is move-only, so an `EXPECT_CALL` needs a matcher
// rather than `testing::Ref`/`testing::Eq`. `invokeWithLocator` builds it as a local that is
// gone once the call returns, so the check has to happen inside the matcher.
//
// `MATCHER_P` emits a function of this name, and gmock matchers are CamelCase by convention.
// NOLINTNEXTLINE(readability-identifier-naming)
MATCHER_P(LocatorEquals, steps, "")
{
    if (arg.size() != static_cast<std::uint32_t>(steps.size()))
    {
        return false;
    }
    for (std::uint32_t i = 0; i < arg.size(); ++i)
    {
        if (arg[i] != steps[i])
        {
            return false;
        }
    }
    return true;
}

}  // namespace xrpl::test
