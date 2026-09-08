#include <tx/wasm/fixtures/WasmRun.h>

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

Bytes
assembleWat(std::string_view wat)
{
    auto const wasm = rs::wasm_testkit::compile_wat(rust::Str{wat.data(), wat.size()});
    return Bytes{wasm.begin(), wasm.end()};
}

std::string
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

std::string
watEscaped(Bytes const& bytes)
{
    return watEscaped(std::span<std::uint8_t const>{bytes.data(), bytes.size()});
}

std::expected<EscrowResult, WasmTER>
runWat(HostFunctions& host, std::string_view wat, std::int64_t gas, std::string_view entryPoint)
{
    return runEscrowWasm(assembleWat(wat), host, gas, entryPoint);
}

}  // namespace xrpl::test
