#pragma once

#include <xrpl/tx/wasm/WasmCommon.h>
#include <xrpl/tx/wasm/WasmVM.h>

#include <tx/wasm/fixtures/WasmFixture.h>

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace xrpl::test {

// Base for the per-host-function fixtures: a guest module run through the real engine
// against the mocked host `MockVmTest` holds.
//
// A test builds its module and hands it over, rather than the fixture holding one. An
// argument axis is then a value written where it is read, and a case the builder cannot
// express — a second export, an import returning nothing — passes `hostAnswer` its own text
// instead.
struct HostCallTest : MockVmTest
{
    // These modules declare one page, so this is the first address past guest memory: what a
    // test reaches for when it wants a region the engine must refuse.
    static constexpr std::int32_t kOnePage = 65536;

    // One argument of a host call, in wasm parameter order.
    //
    // A region is *two* wasm parameters, and `value` and `len` are separate because
    // perturbing one of them is what most of a host function's argument axes are. `kind`
    // also decides the import's declared signature, so a module's signature cannot disagree
    // with the arguments it passes.
    //
    // A declared `u32` is a `region` of four bytes rather than a `scalar`: the ABI carries a
    // sequence number as four little-endian bytes in memory (`args.rs`'s `InU32`).
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

    // Bytes laid into guest memory at instantiation, for the regions a call reads.
    struct Memory
    {
        std::int32_t at;
        Bytes bytes;
    };

    // What a successful call answers the guest: the first four bytes of its out region, so
    // the value is shown to have arrived rather than only been counted, or the call's own
    // status, which for a call that writes is the length. A call with no out region answers
    // its status whichever of these is asked for.
    enum class Answer : std::uint8_t { WrittenBytes, Status };

    // A module whose `escrow_finish` makes exactly one host call.
    //
    // The import is declared at the signature `args` implies, `memory` becomes `(data ...)`
    // segments, and the arguments are `args` spelled as constants. A negative status comes
    // back unchanged whatever `answer` asks for, there being nothing then to read.
    [[nodiscard]] static std::string
    hostCallWat(
        std::string_view importName,
        std::vector<Arg> const& args,
        std::vector<Memory> const& memory = {},
        Answer answer = Answer::WrittenBytes);

    [[nodiscard]] std::expected<EscrowResult, WasmTER>
    callHost(std::string_view wat, std::string_view entryPoint = escrowFunctionName)
    {
        return run(wat, kAmpleGas, entryPoint);
    }

    // What the contract returned, which for these modules is the host's answer or its
    // negative error code. Fails the test if the run did not complete.
    std::int32_t
    hostAnswer(std::string_view wat, std::string_view entryPoint = escrowFunctionName);
};

}  // namespace xrpl::test
