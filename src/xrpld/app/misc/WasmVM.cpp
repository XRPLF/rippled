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

#include <xrpld/app/misc/WamrVM.h>
#include <xrpld/app/misc/WasmHostFunc.h>
#include <xrpld/app/misc/WasmHostFuncWrapper.h>

#include <xrpl/basics/Log.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/LedgerFormats.h>

#include <memory>

namespace ripple {

std::vector<WasmImportFunc>
createWasmImport(HostFunctions* hfs)
{
    std::vector<WasmImportFunc> i;

    if (hfs)
    {
        // clang-format off

        WASM_IMPORT_FUNC2(i, getLedgerSqn, "get_ledger_sqn", hfs,                                                   60);
        WASM_IMPORT_FUNC2(i, getParentLedgerTime, "get_parent_ledger_time", hfs,                                    60);
        WASM_IMPORT_FUNC2(i, getParentLedgerHash, "get_parent_ledger_hash", hfs,                                    60);
        WASM_IMPORT_FUNC2(i, getParentAccountHash, "get_parent_account_hash", hfs,                                  60);
        WASM_IMPORT_FUNC2(i, getParentTransactionHash, "get_parent_tx_hash", hfs,                                   60);
        WASM_IMPORT_FUNC2(i, cacheLedgerObj, "cache_ledger_obj", hfs,                                             5000);
        WASM_IMPORT_FUNC2(i, getTxField, "get_tx_field", hfs,                                                       70);
        WASM_IMPORT_FUNC2(i, getCurrentLedgerObjField, "get_current_ledger_obj_field", hfs,                         70);
        WASM_IMPORT_FUNC2(i, getLedgerObjField, "get_ledger_obj_field", hfs,                                        70);
        WASM_IMPORT_FUNC2(i, getTxNestedField, "get_tx_nested_field", hfs,                                         110);
        WASM_IMPORT_FUNC2(i, getCurrentLedgerObjNestedField, "get_current_ledger_obj_nested_field", hfs,           110);
        WASM_IMPORT_FUNC2(i, getLedgerObjNestedField, "get_ledger_obj_nested_field", hfs,                          110);
        WASM_IMPORT_FUNC2(i, getTxArrayLen, "get_tx_array_len", hfs,                                                40);
        WASM_IMPORT_FUNC2(i, getCurrentLedgerObjArrayLen, "get_current_ledger_obj_array_len", hfs,                  40);
        WASM_IMPORT_FUNC2(i, getLedgerObjArrayLen, "get_ledger_obj_array_len", hfs,                                 40);
        WASM_IMPORT_FUNC2(i, getTxNestedArrayLen, "get_tx_nested_array_len", hfs,                                   70);
        WASM_IMPORT_FUNC2(i, getCurrentLedgerObjNestedArrayLen, "get_current_ledger_obj_nested_array_len",  hfs,    70);
        WASM_IMPORT_FUNC2(i, getLedgerObjNestedArrayLen, "get_ledger_obj_nested_array_len", hfs,                    70);
        WASM_IMPORT_FUNC2(i, updateData, "update_data", hfs,                                                      1000);
        WASM_IMPORT_FUNC2(i, checkSignature, "check_sig", hfs,                                                    2000);
        WASM_IMPORT_FUNC2(i, computeSha512HalfHash, "compute_sha512_half", hfs,                                   2000);
        WASM_IMPORT_FUNC2(i, accountKeylet, "account_keylet", hfs,                                                 350);
        WASM_IMPORT_FUNC2(i, checkKeylet, "check_keylet", hfs,                                                     350);
        WASM_IMPORT_FUNC2(i, credentialKeylet, "credential_keylet", hfs,                                           350);
        WASM_IMPORT_FUNC2(i, delegateKeylet, "delegate_keylet", hfs,                                               350);
        WASM_IMPORT_FUNC2(i, depositPreauthKeylet, "deposit_preauth_keylet", hfs,                                  350);
        WASM_IMPORT_FUNC2(i, didKeylet, "did_keylet", hfs,                                                         350);
        WASM_IMPORT_FUNC2(i, escrowKeylet, "escrow_keylet", hfs,                                                   350);
        WASM_IMPORT_FUNC2(i, lineKeylet, "line_keylet", hfs,                                                       350);
        WASM_IMPORT_FUNC2(i, nftOfferKeylet, "nft_offer_keylet", hfs,                                              350);
        WASM_IMPORT_FUNC2(i, offerKeylet, "offer_keylet", hfs,                                                     350);
        WASM_IMPORT_FUNC2(i, oracleKeylet, "oracle_keylet", hfs,                                                   350);
        WASM_IMPORT_FUNC2(i, paychanKeylet, "paychan_keylet", hfs,                                                 350);
        WASM_IMPORT_FUNC2(i, signersKeylet, "signers_keylet", hfs,                                                 350);
        WASM_IMPORT_FUNC2(i, ticketKeylet, "ticket_keylet", hfs,                                                   350);
        WASM_IMPORT_FUNC2(i, getNFT, "get_nft", hfs,                                                              1000);
        WASM_IMPORT_FUNC (i, trace, hfs,                                                                           500);
        WASM_IMPORT_FUNC2(i, traceNum, "trace_num", hfs,                                                           500);

        // clang-format on
    }

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
