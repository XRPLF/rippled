#include <xrpl/tx/wasm/WasmVM.h>

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/tx/wasm/HostFuncWrapper.h>  // IWYU pragma: keep
#include <xrpl/tx/wasm/WasmCommon.h>
#include <xrpl/tx/wasm/WasmImportsHelper.h>

#include <cstdint>
#include <expected>
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
setCommonHostFunctions(HostFunctions& hfs, ImportVec& i)
{
    // clang-format off
    WASM_IMPORT_FUNC2(i, getLedgerSqn, "ldgr_index", hfs,                                                      60);
    WASM_IMPORT_FUNC2(i, getParentLedgerTime, "parent_ldgr_time", hfs,                                         60);
    WASM_IMPORT_FUNC2(i, getParentLedgerHash, "parent_ldgr_hash", hfs,                                         60);
    WASM_IMPORT_FUNC2(i, getBaseFee, "base_fee", hfs,                                                          60);
    WASM_IMPORT_FUNC2(i, isAmendmentEnabled, "amendment_enabled", hfs,                                        100);

    WASM_IMPORT_FUNC2(i, cacheLedgerObj, "cache_le", hfs,                                                   5'000);
    WASM_IMPORT_FUNC2(i, getTxField, "tx_field", hfs,                                                          70);
    WASM_IMPORT_FUNC2(i, getCurrentLedgerObjField, "home_le_field", hfs,                                       70);
    WASM_IMPORT_FUNC2(i, getLedgerObjField, "le_field", hfs,                                                   70);
    WASM_IMPORT_FUNC2(i, getTxNestedField, "tx_inner", hfs,                                                   110);
    WASM_IMPORT_FUNC2(i, getCurrentLedgerObjNestedField, "home_le_inner", hfs,                                110);
    WASM_IMPORT_FUNC2(i, getLedgerObjNestedField, "le_inner", hfs,                                            110);
    WASM_IMPORT_FUNC2(i, getTxArrayLen, "tx_arr_len", hfs,                                                     40);
    WASM_IMPORT_FUNC2(i, getCurrentLedgerObjArrayLen, "home_le_arr_len", hfs,                                  40);
    WASM_IMPORT_FUNC2(i, getLedgerObjArrayLen, "le_arr_len", hfs,                                              40);
    WASM_IMPORT_FUNC2(i, getTxNestedArrayLen, "tx_inner_arr_len", hfs,                                         70);
    WASM_IMPORT_FUNC2(i, getCurrentLedgerObjNestedArrayLen, "home_le_inner_arr_len", hfs,                      70);
    WASM_IMPORT_FUNC2(i, getLedgerObjNestedArrayLen, "le_inner_arr_len", hfs,                                  70);

    WASM_IMPORT_FUNC2(i, checkSignature, "check_sig", hfs,                                                  35'000);
    WASM_IMPORT_FUNC2(i, computeSha512HalfHash, "sha512_half", hfs,                                          2'000);

    WASM_IMPORT_FUNC2(i, accountKeylet, "accountroot_id", hfs,                                                350);
    WASM_IMPORT_FUNC2(i, ammKeylet, "amm_id", hfs,                                                            450);
    WASM_IMPORT_FUNC2(i, checkKeylet, "check_id", hfs,                                                        350);
    WASM_IMPORT_FUNC2(i, credentialKeylet, "credential_id", hfs,                                              350);
    WASM_IMPORT_FUNC2(i, delegateKeylet, "delegate_id", hfs,                                                  350);
    WASM_IMPORT_FUNC2(i, depositPreauthKeylet, "deposit_preauth_id", hfs,                                     350);
    WASM_IMPORT_FUNC2(i, didKeylet, "did_id", hfs,                                                            350);
    WASM_IMPORT_FUNC2(i, escrowKeylet, "escrow_id", hfs,                                                      350);
    WASM_IMPORT_FUNC2(i, trustLineKeylet, "trustline_id", hfs,                                                     400);
    WASM_IMPORT_FUNC2(i, mptokenIssuanceKeylet, "mpt_issuance_id", hfs,                                       350);
    WASM_IMPORT_FUNC2(i, mptokenKeylet, "mptoken_id", hfs,                                                    500);
    WASM_IMPORT_FUNC2(i, nftokenOfferKeylet, "nft_offer_id", hfs,                                                 350);
    WASM_IMPORT_FUNC2(i, offerKeylet, "offer_id", hfs,                                                        350);
    WASM_IMPORT_FUNC2(i, oracleKeylet, "oracle_id", hfs,                                                      350);
    WASM_IMPORT_FUNC2(i, paychannelKeylet, "paychan_id", hfs,                                                 350);
    WASM_IMPORT_FUNC2(i, permissionedDomainKeylet, "permissioned_domain_id", hfs,                             350);
    WASM_IMPORT_FUNC2(i, signerListKeylet, "signers_id", hfs,                                                 350);
    WASM_IMPORT_FUNC2(i, ticketKeylet, "ticket_id", hfs,                                                      350);
    WASM_IMPORT_FUNC2(i, vaultKeylet, "vault_id", hfs,                                                        350);

    WASM_IMPORT_FUNC2(i, getNFT, "nft_uri", hfs,                                                            5'000);
    WASM_IMPORT_FUNC2(i, getNFTIssuer, "nft_issuer", hfs,                                                      70);
    WASM_IMPORT_FUNC2(i, getNFTTaxon, "nft_taxon", hfs,                                                        60);
    WASM_IMPORT_FUNC2(i, getNFTFlags, "nft_flags", hfs,                                                        60);
    WASM_IMPORT_FUNC2(i, getNFTTransferFee, "nft_xfer_fee", hfs,                                               60);
    WASM_IMPORT_FUNC2(i, getNFTSequence, "nft_serial", hfs,                                                    60);

    WASM_IMPORT_FUNC (i, trace, hfs,                                                                           30);

    WASM_IMPORT_FUNC2(i, floatFromInt, "float_from_int", hfs,                                                 100);
    WASM_IMPORT_FUNC2(i, floatFromUint, "float_from_uint", hfs,                                               130);
    WASM_IMPORT_FUNC2(i, floatFromSTAmount, "float_from_stamount", hfs,                                       150);
    WASM_IMPORT_FUNC2(i, floatFromSTNumber, "float_from_stnumber", hfs,                                       150);
    WASM_IMPORT_FUNC2(i, floatToInt, "float_to_int", hfs,                                                     130);
    WASM_IMPORT_FUNC2(i, floatToMantExp, "float_to_mant_exp", hfs,                                            130);
    WASM_IMPORT_FUNC2(i, floatFromMantExp, "float_from_mant_exp", hfs,                                        100);
    WASM_IMPORT_FUNC2(i, floatCompare, "float_cmp", hfs,                                                       80);
    WASM_IMPORT_FUNC2(i, floatAdd, "float_add", hfs,                                                          160);
    WASM_IMPORT_FUNC2(i, floatSubtract, "float_sub", hfs,                                                     160);
    WASM_IMPORT_FUNC2(i, floatMultiply, "float_mult", hfs,                                                    300);
    WASM_IMPORT_FUNC2(i, floatDivide, "float_div", hfs,                                                       300);
    WASM_IMPORT_FUNC2(i, floatRoot, "float_root", hfs,                                                      5'500);
    WASM_IMPORT_FUNC2(i, floatPower, "float_pow", hfs,                                                      5'500);
    // clang-format on
}

ImportVec
createWasmImport(HostFunctions& hfs)
{
    ImportVec i;

    setCommonHostFunctions(hfs, i);
    WASM_IMPORT_FUNC2(i, updateData, "set_data", hfs, 1000);

    // clang-format off
    // Contract-specific host functions
    WASM_IMPORT_FUNC2(i, instanceParam, "instance_param", hfs,                                   100);
    WASM_IMPORT_FUNC2(i, functionParam, "function_param", hfs,                                   100);

    WASM_IMPORT_FUNC2(i, getDataObjectField, "get_data_object_field", hfs,                       500);
    WASM_IMPORT_FUNC2(i, getDataNestedObjectField, "get_data_nested_object_field", hfs,          500);
    WASM_IMPORT_FUNC2(i, getDataArrayElementField, "get_data_array_element_field", hfs,          500);
    WASM_IMPORT_FUNC2(i, getDataNestedArrayElementField, "get_data_nested_array_element_field", hfs,  500);

    WASM_IMPORT_FUNC2(i, setDataObjectField, "set_data_object_field", hfs,                       500);
    WASM_IMPORT_FUNC2(i, setDataNestedObjectField, "set_data_nested_object_field", hfs,          500);
    WASM_IMPORT_FUNC2(i, setDataArrayElementField, "set_data_array_element_field", hfs,          500);
    WASM_IMPORT_FUNC2(i, setDataNestedArrayElementField, "set_data_nested_array_element_field", hfs,  500);

    WASM_IMPORT_FUNC2(i, buildTxn, "build_txn", hfs,                                            200);
    WASM_IMPORT_FUNC2(i, addTxnField, "add_txn_field", hfs,                                     200);
    WASM_IMPORT_FUNC2(i, emitBuiltTxn, "emit_built_txn", hfs,                                   500);
    WASM_IMPORT_FUNC2(i, emitTxn, "emit_txn", hfs,                                              500);
    WASM_IMPORT_FUNC2(i, emitEvent, "emit_event", hfs,                                          500);
    // clang-format on

    return i;
}

std::expected<EscrowResult, WasmTER>
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

    auto const ret =
        vm.run(wasmCode, hfs, gasLimit, funcName, params, createWasmImport(hfs), hfs.getJournal());

    if (!ret)
    {
#ifdef DEBUG_OUTPUT
        std::cout << ", error: " << ret.error().ter << std::endl;
#endif
        // Carries the TER (tecOUT_OF_GAS / tecFAILED_PROCESSING / tecINTERNAL /
        // temBAD_AMOUNT) and, when meaningful, the gas consumed. The caller is
        // responsible for writing that gas to tx metadata.
        return std::unexpected(ret.error());
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

    auto const ret =
        vm.check(wasmCode, hfs, funcName, params, createWasmImport(hfs), hfs.getJournal());

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

std::expected<WasmResult<int32_t>, WasmTER>
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
