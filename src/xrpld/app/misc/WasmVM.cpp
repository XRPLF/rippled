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

#include <xrpld/app/misc/WamrVM.h>
#include <xrpld/app/misc/WasmHostFuncWrapper.h>

#include <xrpl/basics/Log.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/LedgerFormats.h>

#include <memory>

namespace ripple {

Expected<EscrowResult, TER>
runEscrowWasm(
    Bytes const& wasmCode,
    std::string_view funcName,
    HostFunctions* hfs,
    int64_t gasLimit,
    std::vector<WasmParam> const& params)
{
    //  create VM and set cost limit
    auto& vm = WasmEngine::instance();
    vm.initGas(gasLimit);
    vm.initMaxPages(MAX_PAGES);

    std::vector<WasmImportFunc> imports;

    if (hfs)
    {
        WASM_IMPORT_FUNC(imports, getLedgerSqn, hfs);
        WASM_IMPORT_FUNC(imports, getParentLedgerTime, hfs);
        WASM_IMPORT_FUNC(imports, getTxField, hfs);
        WASM_IMPORT_FUNC(imports, getLedgerEntryField, hfs);
        WASM_IMPORT_FUNC(imports, getCurrentLedgerEntryField, hfs);
        WASM_IMPORT_FUNC(imports, getNFT, hfs);
        WASM_IMPORT_FUNC(imports, accountKeylet, hfs);
        WASM_IMPORT_FUNC(imports, credentialKeylet, hfs);
        WASM_IMPORT_FUNC(imports, escrowKeylet, hfs);
        WASM_IMPORT_FUNC(imports, oracleKeylet, hfs);
        WASM_IMPORT_FUNC(imports, updateData, hfs);
        WASM_IMPORT_FUNC(imports, computeSha512HalfHash, hfs);
        WASM_IMPORT_FUNC(imports, print, hfs);
    }

    std::int64_t const sgas = gasLimit;  // vm.getGas();
    auto ret = vm.run(
        wasmCode,
        funcName,
        imports,
        params,
        hfs ? hfs->getJournal() : debugLog());

    // std::cout << "runEscrowWasm, mod size: " << wasmCode.size()
    //           << ", gasLimit: " << gasLimit << ", funcName: " << funcName;

    if (!ret.has_value())
    {
        // std::cout << ", error: " << ret.error() << std::endl;
        return Unexpected<TER>(ret.error());
    }
    std::int64_t const egas = vm.getGas();
    std::int64_t const spent = sgas - egas;

    // std::cout << ", ret: " << ret.value() << ", gas spent: " << spent
    //           << std::endl;
    return EscrowResult{static_cast<bool>(ret.value()), spent};
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

Expected<int32_t, TER>
WasmEngine::run(
    wbytes const& wasmCode,
    std::string_view funcName,
    std::vector<WasmImportFunc> const& imports,
    std::vector<WasmParam> const& params,
    beast::Journal j)
{
    return impl->run(wasmCode, funcName, imports, params, j);
}

std::int64_t
WasmEngine::initGas(std::int64_t def)
{
    return impl->initGas(def);
}

std::int32_t
WasmEngine::initMaxPages(std::int32_t def)
{
    return impl->initMaxPages(def);
}

// gas = 1'000'000'000LL
std::int64_t
WasmEngine::setGas(std::int64_t gas)
{
    return impl->setGas(gas);
}

std::int64_t
WasmEngine::getGas()
{
    return impl->getGas();
}

wmem
WasmEngine::getMem() const
{
    return impl->getMem();
}

int32_t
WasmEngine::allocate(int32_t size)
{
    return impl->allocate(size);
}

void*
WasmEngine::newTrap(std::string_view msg)
{
    return impl->newTrap(msg);
}

}  // namespace ripple
