#pragma once

#include <xrpl/tx/wasm/WasmCommon.h>

#include <tx/wasm/fixtures/WasmFixture.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace xrpl::test {

// Base for the per-host-function fixtures: a guest module run through the real engine
// against the mocked host `MockVmTest` holds.
struct GuestCallTest : MockVmTest
{
    // The first address past guest memory.
    static constexpr std::int32_t kOnePage = 65536;

    // One argument, in wasm parameter order. A region is *two* wasm parameters, and a
    // declared `u32` is a region of four little-endian bytes, not a scalar (`args.rs`).
    struct Arg
    {
        enum class Kind : std::uint8_t { Scalar32, Scalar64, Region, OutRegion };

        Kind kind;
        std::int64_t value;  // the scalar, or the region's pointer
        std::int32_t len;    // the region's length; unread for a scalar

        static constexpr Arg
        scalar(std::int32_t value)
        {
            return Arg{.kind = Kind::Scalar32, .value = value, .len = 0};
        }

        static constexpr Arg
        scalar64(std::int64_t value)
        {
            return Arg{.kind = Kind::Scalar64, .value = value, .len = 0};
        }

        static constexpr Arg
        region(std::int32_t ptr, std::int32_t len)
        {
            return Arg{.kind = Kind::Region, .value = ptr, .len = len};
        }

        static constexpr Arg
        outRegion(std::int32_t ptr, std::int32_t len)
        {
            return Arg{.kind = Kind::OutRegion, .value = ptr, .len = len};
        }
    };

    // Bytes placed in guest memory at instantiation.
    struct Memory
    {
        std::int32_t at;
        Bytes bytes;
    };

    // What a successful call answers: the out region's first four bytes, or the status.
    // With no out region it is the status either way.
    enum class Answer : std::uint8_t { WrittenBytes, Status };

    // A module whose `escrow_finish` makes one host call, its import declared at the
    // signature `args` implies.
    [[nodiscard]] static std::string
    hostCallWat(
        std::string_view importName,
        std::vector<Arg> const& args,
        std::vector<Memory> const& memory = {},
        Answer answer = Answer::WrittenBytes);

    // The contract's return: the host's answer, or a negative error code. Adds a failure if
    // the run did not complete.
    std::int32_t
    hostAnswer(std::string_view wat);
};

}  // namespace xrpl::test
