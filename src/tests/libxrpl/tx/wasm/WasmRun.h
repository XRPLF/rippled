#pragma once

#include <xrpl/tx/wasm/HostFunc.h>
#include <xrpl/tx/wasm/WasmCommon.h>
#include <xrpl/tx/wasm/WasmVM.h>

#include <rust/cxx.h>
#include <xrpl_wasm_testkit_cxxbridge/lib.h>

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>

namespace xrpl::test {

// Enough gas for a small module to run to completion; a test about budgets passes its own.
inline constexpr std::int64_t kAmpleGas = 100'000;

// Assemble WebAssembly text to bytes via the test-only `wasm_testkit` crate. The engine
// itself refuses text (a text assembler on the consensus path would make a transaction's
// validity a build flag), so this is where a WAT string becomes something runnable. Throws
// `rust::Error` on a typo, which gtest reports against the test that holds it.
inline Bytes
assembleWat(std::string_view wat)
{
    auto const wasm = rs::wasm_testkit::compile_wat(rust::Str{wat.data(), wat.size()});
    return Bytes{wasm.begin(), wasm.end()};
}

// `bytes` as the escape sequence a WAT string literal wants (`\aa\bb...`), for seeding a
// contract's memory through a `(data ...)` segment.
//
// Guest memory starts zeroed, and zeros are not a usable input to most host functions: an
// all-zero account id is `InvalidAccount`, an all-zero float is non-canonical. A contract
// that needs real bytes to work on gets them here, once at instantiation, rather than
// building them out of `i32.store` instructions.
inline std::string
watEscaped(std::span<std::uint8_t const> bytes)
{
    static constexpr char kHex[] = "0123456789abcdef";
    auto out = std::string{};
    out.reserve(bytes.size() * 3);
    for (auto const byte : bytes)
    {
        out += '\\';
        out += kHex[byte >> 4];
        out += kHex[byte & 0x0F];
    }
    return out;
}

inline std::string
watEscaped(Bytes const& bytes)
{
    return watEscaped(std::span<std::uint8_t const>{bytes.data(), bytes.size()});
}

// Assemble and run `wat`'s `entryPoint` through the real VM, servicing host calls through
// `host` — a mock (`MockVmTest`) or the real impl over a ledger (`RealVmTest`). The one
// host-agnostic harness both fixtures inject their host into.
inline std::expected<EscrowResult, WasmTER>
runWat(
    HostFunctions& host,
    std::string_view wat,
    std::int64_t gas = kAmpleGas,
    std::string_view entryPoint = escrowFunctionName)
{
    return runEscrowWasm(assembleWat(wat), host, gas, entryPoint);
}

}  // namespace xrpl::test
