#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/tx/wasm/HostContext.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <helpers/CaptureSink.h>
#include <rust/cxx.h>
#include <tx/wasm/MockHostFunctions.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace xrpl::test {

// Base for the tests that construct `HostContext` directly, rather than reaching it through
// an assembled module.
struct HostContextTest : testing::Test
{
    static rust::Slice<std::uint8_t const>
    bytesOf(Bytes const& bytes);

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

}  // namespace xrpl::test
