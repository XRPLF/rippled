#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/tx/wasm/HostFunc.h>
#include <xrpl/tx/wasm/WasmCommon.h>
#include <xrpl/tx/wasm/WasmImportsHelper.h>

#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace xrpl {

std::string_view inline constexpr wEnv = "env";
std::string_view inline constexpr wHostLib = "host_lib";
std::string_view inline constexpr wMem = "memory";
std::string_view inline constexpr wStore = "store";
std::string_view inline constexpr wLoad = "load";
std::string_view inline constexpr wSize = "size";
std::string_view inline constexpr wAlloc = "allocate";
std::string_view inline constexpr wDealloc = "deallocate";
std::string_view inline constexpr wProcExit = "proc_exit";

std::string_view inline constexpr escrowFunctionName = "escrow_finish";

uint32_t inline constexpr maxPages = 128;  // 8MB = 64KB*128

class WasmiEngine;

class WasmEngine
{
    std::unique_ptr<WasmiEngine> const impl_;

    WasmEngine();

public:
    WasmEngine(WasmEngine const&) = delete;
    WasmEngine(WasmEngine&&) = delete;
    WasmEngine&
    operator=(WasmEngine const&) = delete;
    WasmEngine&
    operator=(WasmEngine&&) = delete;

    static WasmEngine&
    instance();

    std::expected<WasmResult<int32_t>, WasmTER>
    run(Bytes const& wasmCode,
        HostFunctions& hfs,
        int64_t gasLimit,
        std::string_view funcName = {},
        std::vector<WasmParam> const& params = {},
        ImportVec const& imports = {},
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()});

    NotTEC
    check(
        Bytes const& wasmCode,
        HostFunctions& hfs,
        std::string_view funcName,
        std::vector<WasmParam> const& params = {},
        ImportVec const& imports = {},
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()});

    // Host functions helper functionality
    void*
    newTrap(std::string const& txt = std::string());

    [[nodiscard]] beast::Journal
    getJournal() const;
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

ImportVec
createWasmImport(HostFunctions& hfs);

std::expected<EscrowResult, WasmTER>
runEscrowWasm(
    Bytes const& wasmCode,
    HostFunctions& hfs,
    int64_t gasLimit,
    std::string_view funcName = escrowFunctionName,
    std::vector<WasmParam> const& params = {});

NotTEC
preflightEscrowWasm(
    Bytes const& wasmCode,
    HostFunctions& hfs,
    std::string_view funcName = escrowFunctionName,
    std::vector<WasmParam> const& params = {});

}  // namespace xrpl
