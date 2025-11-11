#pragma once

#include <xrpld/app/wasm/HostFunc.h>

#include <string_view>

namespace ripple {

static std::string_view const W_ENV = "env";
static std::string_view const W_HOST_LIB = "host_lib";
static std::string_view const W_MEM = "memory";
static std::string_view const W_STORE = "store";
static std::string_view const W_LOAD = "load";
static std::string_view const W_SIZE = "size";
static std::string_view const W_ALLOC = "allocate";
static std::string_view const W_DEALLOC = "deallocate";
static std::string_view const W_PROC_EXIT = "proc_exit";

static std::string_view const ESCROW_FUNCTION_NAME = "finish";

class WasmiEngine;

class WasmEngine
{
    std::unique_ptr<WasmiEngine> const impl;

    WasmEngine();

    WasmEngine(WasmEngine const&) = delete;
    WasmEngine(WasmEngine&&) = delete;
    WasmEngine&
    operator=(WasmEngine const&) = delete;
    WasmEngine&
    operator=(WasmEngine&&) = delete;

public:
    ~WasmEngine() = default;

    static WasmEngine&
    instance();

    Expected<WasmResult<int32_t>, TER>
    run(Bytes const& wasmCode,
        std::string_view funcName = {},
        std::vector<WasmParam> const& params = {},
        std::vector<WasmImportFunc> const& imports = {},
        HostFunctions* hfs = nullptr,
        int64_t gasLimit = -1,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()});

    NotTEC
    check(
        Bytes const& wasmCode,
        std::string_view funcName,
        std::vector<WasmParam> const& params = {},
        std::vector<WasmImportFunc> const& imports = {},
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()});

    // Host functions helper functionality
    void*
    newTrap(std::string_view msg = {});

    beast::Journal
    getJournal() const;
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

std::vector<WasmImportFunc>
createWasmImport(HostFunctions* hfs);

Expected<EscrowResult, TER>
runEscrowWasm(
    Bytes const& wasmCode,
    std::string_view funcName = ESCROW_FUNCTION_NAME,
    std::vector<WasmParam> const& params = {},
    HostFunctions* hfs = nullptr,
    int64_t gasLimit = -1,
    beast::Journal j = beast::Journal(beast::Journal::getNullSink()));

NotTEC
preflightEscrowWasm(
    Bytes const& wasmCode,
    std::string_view funcName = ESCROW_FUNCTION_NAME,
    std::vector<WasmParam> const& params = {},
    HostFunctions* hfs = nullptr,
    beast::Journal j = beast::Journal(beast::Journal::getNullSink()));

}  // namespace ripple
