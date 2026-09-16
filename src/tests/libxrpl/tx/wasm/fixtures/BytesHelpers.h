#pragma once

#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <vector>

namespace xrpl::test {

// The ABI's wire encodings, spelled independently of the code that produces them.
//
// Apart from either layer's fixture because both build them: the direct `HostContext` tests
// and the guest-side `guest_calls` tests.

// A scalar's wire form: its bytes little-endian, the way a wasm guest lays them out in
// memory.
//
// Spelled out with shifts rather than a `memcpy` of the value, which would mirror what
// `answerScalar` does and so assert nothing about the byte order. That is the whole reason
// this exists, so keep it a shift.
template <class T>
Bytes
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
Bytes
bytesOfSteps(std::vector<std::int32_t> const& steps);

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
