#include <xrpl/tx/wasm/WasmVM.h>

#include <xrpl/basics/Expected.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/tx/wasm/HostFuncWrapper.h>  // IWYU pragma: keep
#include <xrpl/tx/wasm/ParamsHelper.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>
#ifdef _DEBUG
// #define DEBUG_OUTPUT 1
#endif

#include <xrpl/tx/wasm/HostFunc.h>
#include <xrpl/tx/wasm/WasmiVM.h>

#include <memory>

namespace xrpl {
// WARNING: Per XLS-0102, the host functions registered here form a stable
// ABI. Their name, semantics, parameters, and return types must NEVER be
// changed, as there may always be a program that uses it. New host functions
// may be added and existing gas costs may be adjusted, but every such change
// must be gated by an amendment.
// See XLS-0102 §6.5 (Future-Proofing):
// https://github.com/XRPLF/XRPL-Standards/tree/master/XLS-0102-wasm-vm#65-future-proofing
static void
setCommonHostFunctions(HostFunctions* hfs, ImportVec& i)
{
    // clang-format off
    WASM_IMPORT_FUNC2(i, getLedgerSqn, "get_ledger_sqn", hfs,                                                   60);
    WASM_IMPORT_FUNC2(i, getParentLedgerTime, "get_parent_ledger_time", hfs,                                    60);
    WASM_IMPORT_FUNC2(i, getParentLedgerHash, "get_parent_ledger_hash", hfs,                                    60);
    WASM_IMPORT_FUNC2(i, getBaseFee, "get_base_fee", hfs,                                                       60);
    WASM_IMPORT_FUNC2(i, isAmendmentEnabled, "amendment_enabled", hfs,                                         100);

    WASM_IMPORT_FUNC2(i, cacheLedgerObj, "cache_ledger_obj", hfs,                                            5'000);
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

    WASM_IMPORT_FUNC2(i, checkSignature, "check_sig", hfs,                                                  35'000);
    WASM_IMPORT_FUNC2(i, computeSha512HalfHash, "compute_sha512_half", hfs,                                  1'500);

    WASM_IMPORT_FUNC2(i, accountKeylet, "account_keylet", hfs,                                                 350);
    WASM_IMPORT_FUNC2(i, ammKeylet, "amm_keylet", hfs,                                                         450);
    WASM_IMPORT_FUNC2(i, checkKeylet, "check_keylet", hfs,                                                     350);
    WASM_IMPORT_FUNC2(i, credentialKeylet, "credential_keylet", hfs,                                           350);
    WASM_IMPORT_FUNC2(i, delegateKeylet, "delegate_keylet", hfs,                                               350);
    WASM_IMPORT_FUNC2(i, depositPreauthKeylet, "deposit_preauth_keylet", hfs,                                  350);
    WASM_IMPORT_FUNC2(i, didKeylet, "did_keylet", hfs,                                                         350);
    WASM_IMPORT_FUNC2(i, escrowKeylet, "escrow_keylet", hfs,                                                   350);
    WASM_IMPORT_FUNC2(i, lineKeylet, "line_keylet", hfs,                                                       400);
    WASM_IMPORT_FUNC2(i, mptIssuanceKeylet, "mpt_issuance_keylet", hfs,                                        350);
    WASM_IMPORT_FUNC2(i, mptokenKeylet, "mptoken_keylet", hfs,                                                 500);
    WASM_IMPORT_FUNC2(i, nftOfferKeylet, "nft_offer_keylet", hfs,                                              350);
    WASM_IMPORT_FUNC2(i, offerKeylet, "offer_keylet", hfs,                                                     350);
    WASM_IMPORT_FUNC2(i, oracleKeylet, "oracle_keylet", hfs,                                                   350);
    WASM_IMPORT_FUNC2(i, paychanKeylet, "paychan_keylet", hfs,                                                 350);
    WASM_IMPORT_FUNC2(i, permissionedDomainKeylet, "permissioned_domain_keylet", hfs,                          350);
    WASM_IMPORT_FUNC2(i, signersKeylet, "signers_keylet", hfs,                                                 350);
    WASM_IMPORT_FUNC2(i, ticketKeylet, "ticket_keylet", hfs,                                                   350);
    WASM_IMPORT_FUNC2(i, vaultKeylet, "vault_keylet", hfs,                                                     350);

    WASM_IMPORT_FUNC2(i, getNFT, "get_nft", hfs,                                                             5'000);
    WASM_IMPORT_FUNC2(i, getNFTIssuer, "get_nft_issuer", hfs,                                                   70);
    WASM_IMPORT_FUNC2(i, getNFTTaxon, "get_nft_taxon", hfs,                                                     60);
    WASM_IMPORT_FUNC2(i, getNFTFlags, "get_nft_flags", hfs,                                                     60);
    WASM_IMPORT_FUNC2(i, getNFTTransferFee, "get_nft_transfer_fee", hfs,                                        60);
    WASM_IMPORT_FUNC2(i, getNFTSerial, "get_nft_serial", hfs,                                                   60);

    WASM_IMPORT_FUNC (i, trace, hfs,                                                                           500);
    WASM_IMPORT_FUNC2(i, traceNum, "trace_num", hfs,                                                           500);
    WASM_IMPORT_FUNC2(i, traceAccount, "trace_account", hfs,                                                   500);
    WASM_IMPORT_FUNC2(i, traceFloat, "trace_opaque_float", hfs,                                                500);
    WASM_IMPORT_FUNC2(i, traceAmount, "trace_amount", hfs,                                                     500);

    WASM_IMPORT_FUNC2(i, floatFromInt, "float_from_int", hfs,                                                  100);
    WASM_IMPORT_FUNC2(i, floatFromUint, "float_from_uint", hfs,                                                130);
    WASM_IMPORT_FUNC2(i, floatFromSTAmount, "float_from_stamount", hfs,                                        150);
    WASM_IMPORT_FUNC2(i, floatFromSTNumber, "float_from_stnumber", hfs,                                        150);
    WASM_IMPORT_FUNC2(i, floatToInt, "float_to_int", hfs,                                                      130);
    WASM_IMPORT_FUNC2(i, floatToMantExp, "float_to_mant_exp", hfs,                                             130);
    WASM_IMPORT_FUNC2(i, floatFromMantExp, "float_from_mant_exp", hfs,                                         100);
    WASM_IMPORT_FUNC2(i, floatCompare, "float_compare", hfs,                                                    80);
    WASM_IMPORT_FUNC2(i, floatAdd, "float_add", hfs,                                                           160);
    WASM_IMPORT_FUNC2(i, floatSubtract, "float_subtract", hfs,                                                 160);
    WASM_IMPORT_FUNC2(i, floatMultiply, "float_multiply", hfs,                                                 300);
    WASM_IMPORT_FUNC2(i, floatDivide, "float_divide", hfs,                                                     300);
    WASM_IMPORT_FUNC2(i, floatRoot, "float_root", hfs,                                                       5'500);
    WASM_IMPORT_FUNC2(i, floatPower, "float_pow", hfs,                                                       5'500);
    // clang-format on
}

ImportVec
createWasmImport(HostFunctions& hfs)
{
    ImportVec i;

    setCommonHostFunctions(&hfs, i);
    WASM_IMPORT_FUNC2(i, updateData, "update_data", &hfs, 1000);

    return i;
}

Expected<EscrowResult, TER>
runEscrowWasm(
    Bytes const& wasmCode,
    HostFunctions& hfs,
    int64_t gasLimit,
    std::string_view funcName,
    std::vector<WasmParam> const& params)
{
    //  create VM and set cost limit
    auto& vm = WasmEngine::instance();
    // vm.initMaxPages(MAX_PAGES);

    auto const start = std::chrono::steady_clock::now();

    auto const ret =
        vm.run(wasmCode, hfs, gasLimit, funcName, params, createWasmImport(hfs), hfs.getJournal());

    // microseconds is intentional.  The resolution of the StatsD is milliseconds,
    // but this runs too fast for that.
    hfs.executionTimeEvent("runEscrowWasm")
        .notify(
            std::chrono::milliseconds{std::chrono::duration_cast<std::chrono::microseconds>(
                                          std::chrono::steady_clock::now() - start)
                                          .count()});

    if (!ret)
    {
#ifdef DEBUG_OUTPUT
        std::cout << ", error: " << ret.error() << std::endl;
#endif
        return Unexpected<TER>(ret.error());
    }

#ifdef DEBUG_OUTPUT
    std::cout << ", ret: " << ret->result << ", gas spent: " << ret->cost << std::endl;
#endif
    return EscrowResult{.result = ret->result, .cost = ret->cost};
}

NotTEC
preflightEscrowWasm(
    Bytes const& wasmCode,
    HostFunctions& hfs,
    std::string_view funcName,
    std::vector<WasmParam> const& params)
{
    //  create VM and set cost limit
    auto& vm = WasmEngine::instance();
    // vm.initMaxPages(MAX_PAGES);

    auto const start = std::chrono::steady_clock::now();

    auto const ret =
        vm.check(wasmCode, hfs, funcName, params, createWasmImport(hfs), hfs.getJournal());

    // microseconds is intentional.  The resolution of the StatsD is milliseconds,
    // but this runs too fast for that.
    hfs.executionTimeEvent("preflightEscrowWasm")
        .notify(
            std::chrono::milliseconds{std::chrono::duration_cast<std::chrono::microseconds>(
                                          std::chrono::steady_clock::now() - start)
                                          .count()});

    return ret;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

WasmEngine::WasmEngine() : impl_(std::make_unique<WasmiEngine>())
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
    HostFunctions& hfs,
    int64_t gasLimit,
    std::string_view funcName,
    std::vector<WasmParam> const& params,
    ImportVec const& imports,
    beast::Journal j)
{
    return impl_->run(wasmCode, hfs, gasLimit, funcName, params, imports, j);
}

NotTEC
WasmEngine::check(
    Bytes const& wasmCode,
    HostFunctions& hfs,
    std::string_view funcName,
    std::vector<WasmParam> const& params,
    ImportVec const& imports,
    beast::Journal j)
{
    return impl_->check(wasmCode, hfs, funcName, params, imports, j);
}

void*
WasmEngine::newTrap(std::string const& msg)
{
    return impl_->newTrap(msg);
}

// LCOV_EXCL_START
beast::Journal
WasmEngine::getJournal() const
{
    return impl_->getJournal();
}
// LCOV_EXCL_STOP

}  // namespace xrpl
