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

#ifdef _DEBUG
// #define DEBUG_OUTPUT 1
#endif

#include <xrpld/app/wasm/HostFunc.h>
#include <xrpld/app/wasm/HostFuncWrapper.h>
#include <xrpld/app/wasm/WamrVM.h>

#include <xrpl/basics/Log.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/LedgerFormats.h>

#include <memory>

namespace ripple {

std::vector<WasmImportFunc>
createWasmImport(HostFunctions* hfs)
{
    std::vector<WasmImportFunc> i;

    // Add host functions here

    return i;
}

Expected<EscrowResult, TER>
runEscrowWasm(
    Bytes const& wasmCode,
    std::string_view funcName,
    std::vector<WasmParam> const& params,
    HostFunctions* hfs,
    int64_t gasLimit,
    beast::Journal j)
{
    //  create VM and set cost limit
    auto& vm = WasmEngine::instance();
    vm.initMaxPages(MAX_PAGES);

    auto const ret = vm.run(
        wasmCode,
        funcName,
        params,
        createWasmImport(hfs),
        hfs,
        gasLimit,
        hfs ? hfs->getJournal() : j);

    // std::cout << "runEscrowWasm, mod size: " << wasmCode.size()
    //           << ", gasLimit: " << gasLimit << ", funcName: " << funcName;

    if (!ret)
    {
#ifdef DEBUG_OUTPUT
        std::cout << ", error: " << ret.error() << std::endl;
#endif
        return Unexpected<TER>(ret.error());
    }

#ifdef DEBUG_OUTPUT
    std::cout << ", ret: " << ret->result << ", gas spent: " << ret->cost
              << std::endl;
#endif
    return EscrowResult{ret->result, ret->cost};
}

NotTEC
preflightEscrowWasm(
    Bytes const& wasmCode,
    std::string_view funcName,
    std::vector<WasmParam> const& params,
    HostFunctions* hfs,
    beast::Journal j)
{
    //  create VM and set cost limit
    auto& vm = WasmEngine::instance();
    vm.initMaxPages(MAX_PAGES);

    auto const ret = vm.check(
        wasmCode,
        funcName,
        params,
        createWasmImport(hfs),
        hfs ? hfs->getJournal() : j);

    return ret;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

WasmEngine::WasmEngine() : impl(std::make_unique<WamrEngine>())
{
}

WasmEngine&
WasmEngine::instance()
{
    static WasmEngine e;
    return e;
}

Expected<WasmResult<int32_t>, TER>
WasmEngine::run(
    Bytes const& wasmCode,
    std::string_view funcName,
    std::vector<WasmParam> const& params,
    std::vector<WasmImportFunc> const& imports,
    HostFunctions* hfs,
    int64_t gasLimit,
    beast::Journal j)
{
    return impl->run(wasmCode, funcName, params, imports, hfs, gasLimit, j);
}

NotTEC
WasmEngine::check(
    Bytes const& wasmCode,
    std::string_view funcName,
    std::vector<WasmParam> const& params,
    std::vector<WasmImportFunc> const& imports,
    beast::Journal j)
{
    return impl->check(wasmCode, funcName, params, imports, j);
}

std::int32_t
WasmEngine::initMaxPages(std::int32_t def)
{
    return impl->initMaxPages(def);
}

void*
WasmEngine::newTrap(std::string_view msg)
{
    return impl->newTrap(msg);
}

beast::Journal
WasmEngine::getJournal() const
{
    return impl->getJournal();
}

}  // namespace ripple
