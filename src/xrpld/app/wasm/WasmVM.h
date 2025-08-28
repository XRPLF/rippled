//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2025 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================
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

uint32_t const MAX_PAGES = 128;  // 8MB = 64KB*128

class WamrEngine;
class WasmEngine
{
    std::unique_ptr<WamrEngine> const impl;

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

    std::int32_t
    initMaxPages(std::int32_t def);

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
