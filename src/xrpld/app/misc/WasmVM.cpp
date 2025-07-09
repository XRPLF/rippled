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
#include <xrpld/app/misc/WasmHostFunc.h>
#include <xrpld/app/misc/WasmHostFuncWrapper.h>

#include <xrpl/basics/Log.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/LedgerFormats.h>

#include <memory>

namespace ripple {

static std::vector<WasmImportFunc>
createImports(HostFunctions* hfs)
{
    std::vector<WasmImportFunc> imports;

    if (hfs)
    {
        // TODO: remove after escrow_test wasm module will be updated
        WASM_IMPORT_FUNC2(imports, getLedgerSqnOld, "getLedgerSqn", hfs);

        WASM_IMPORT_FUNC2(imports, getLedgerSqn, "get_ledger_sqn", hfs);
        WASM_IMPORT_FUNC2(
            imports, getParentLedgerTime, "get_parent_ledger_time", hfs);
        WASM_IMPORT_FUNC2(
            imports, getParentLedgerHash, "get_parent_ledger_hash", hfs);
        WASM_IMPORT_FUNC2(imports, cacheLedgerObj, "cache_ledger_obj", hfs);
        WASM_IMPORT_FUNC2(imports, getTxField, "get_tx_field", hfs);
        WASM_IMPORT_FUNC2(
            imports,
            getCurrentLedgerObjField,
            "get_current_ledger_obj_field",
            hfs);
        WASM_IMPORT_FUNC2(
            imports, getLedgerObjField, "get_ledger_obj_field", hfs);
        WASM_IMPORT_FUNC2(
            imports, getTxNestedField, "get_tx_nested_field", hfs);
        WASM_IMPORT_FUNC2(
            imports,
            getCurrentLedgerObjNestedField,
            "get_current_ledger_obj_nested_field",
            hfs);
        WASM_IMPORT_FUNC2(
            imports,
            getLedgerObjNestedField,
            "get_ledger_obj_nested_field",
            hfs);
        WASM_IMPORT_FUNC2(imports, getTxArrayLen, "get_tx_array_len", hfs);
        WASM_IMPORT_FUNC2(
            imports,
            getCurrentLedgerObjArrayLen,
            "get_current_ledger_obj_array_len",
            hfs);
        WASM_IMPORT_FUNC2(
            imports, getLedgerObjArrayLen, "get_ledger_obj_array_len", hfs);
        WASM_IMPORT_FUNC2(
            imports, getTxNestedArrayLen, "get_tx_nested_array_len", hfs);
        WASM_IMPORT_FUNC2(
            imports,
            getCurrentLedgerObjNestedArrayLen,
            "get_current_ledger_obj_nested_array_len",
            hfs);
        WASM_IMPORT_FUNC2(
            imports,
            getLedgerObjNestedArrayLen,
            "get_ledger_obj_nested_array_len",
            hfs);
        WASM_IMPORT_FUNC2(imports, updateData, "update_data", hfs);
        WASM_IMPORT_FUNC2(
            imports, computeSha512HalfHash, "compute_sha512_half", hfs);
        WASM_IMPORT_FUNC2(imports, accountKeylet, "account_keylet", hfs);
        WASM_IMPORT_FUNC2(imports, checkKeylet, "check_keylet", hfs);
        WASM_IMPORT_FUNC2(imports, credentialKeylet, "credential_keylet", hfs);
        WASM_IMPORT_FUNC2(imports, didKeylet, "did_keylet", hfs);
        WASM_IMPORT_FUNC2(imports, delegateKeylet, "delegate_keylet", hfs);
        WASM_IMPORT_FUNC2(
            imports, depositPreauthKeylet, "deposit_preauth_keylet", hfs);
        WASM_IMPORT_FUNC2(imports, escrowKeylet, "escrow_keylet", hfs);
        WASM_IMPORT_FUNC2(imports, lineKeylet, "line_keylet", hfs);
        WASM_IMPORT_FUNC2(imports, nftOfferKeylet, "nft_offer_keylet", hfs);
        WASM_IMPORT_FUNC2(imports, offerKeylet, "offer_keylet", hfs);
        WASM_IMPORT_FUNC2(imports, oracleKeylet, "oracle_keylet", hfs);
        WASM_IMPORT_FUNC2(imports, paychanKeylet, "paychan_keylet", hfs);
        WASM_IMPORT_FUNC2(imports, signersKeylet, "signers_keylet", hfs);
        WASM_IMPORT_FUNC2(imports, ticketKeylet, "ticket_keylet", hfs);
        WASM_IMPORT_FUNC2(imports, getNFT, "get_NFT", hfs);
        WASM_IMPORT_FUNC(imports, trace, hfs);
        WASM_IMPORT_FUNC2(imports, traceNum, "trace_num", hfs);
    }

    return imports;
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
        createImports(hfs),
        hfs,
        gasLimit,
        hfs ? hfs->getJournal() : j);

    // std::cout << "runEscrowWasm, mod size: " << wasmCode.size()
    //           << ", gasLimit: " << gasLimit << ", funcName: " << funcName;

    if (!ret.has_value())
    {
        // std::cout << ", error: " << ret.error() << std::endl;
        return Unexpected<TER>(ret.error());
    }

    // std::cout << ", ret: " << ret->result << ", gas spent: " << ret->cost
    //           << std::endl;
    return EscrowResult{ret->result > 0, ret->cost};
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
        createImports(hfs),
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
