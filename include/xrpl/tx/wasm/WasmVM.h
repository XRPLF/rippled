#pragma once

#include <xrpl/tx/wasm/HostFunc.h>

#include <string_view>

namespace xrpl {

std::string_view inline constexpr W_ENV = "env";
std::string_view inline constexpr W_HOST_LIB = "host_lib";
std::string_view inline constexpr W_MEM = "memory";
std::string_view inline constexpr W_STORE = "store";
std::string_view inline constexpr W_LOAD = "load";
std::string_view inline constexpr W_SIZE = "size";
std::string_view inline constexpr W_ALLOC = "allocate";
std::string_view inline constexpr W_DEALLOC = "deallocate";
std::string_view inline constexpr W_PROC_EXIT = "proc_exit";

std::string_view inline constexpr ESCROW_FUNCTION_NAME = "finish";

uint32_t inline constexpr MAX_PAGES = 128;  // 8MB = 64KB*128

class WasmiEngine;

class WasmEngine
{
    std::unique_ptr<WasmiEngine> const impl_;

    WasmEngine();

    WasmEngine(WasmEngine const&) = delete;
    WasmEngine(WasmEngine&&) = delete;
    WasmEngine&
    operator=(WasmEngine const&) = delete;
    WasmEngine&
    operator=(WasmEngine&&) = delete;

public:
    static WasmEngine&
    instance();

    Expected<WasmResult<int32_t>, TER>
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

    beast::Journal
    getJournal() const;
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

ImportVec
createWasmImport(HostFunctions& hfs);

Expected<EscrowResult, TER>
runEscrowWasm(
    Bytes const& wasmCode,
    HostFunctions& hfs,
    int64_t gasLimit,
    std::string_view funcName = ESCROW_FUNCTION_NAME,
    std::vector<WasmParam> const& params = {});

NotTEC
preflightEscrowWasm(
    Bytes const& wasmCode,
    HostFunctions& hfs,
    std::string_view funcName = ESCROW_FUNCTION_NAME,
    std::vector<WasmParam> const& params = {});

}  // namespace xrpl
