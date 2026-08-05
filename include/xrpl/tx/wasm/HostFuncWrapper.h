#pragma once

#include <xrpl/tx/wasm/HostFunc.h>

#include <wasm.h>

#include <cstdint>

namespace xrpl {

#define WASM_CB_PARAMS_LIST void *env, wasm_val_vec_t const *params, wasm_val_vec_t *results
#define WASM_SECONDARY_CB_PARAMS_LIST \
    HostFunctions &hf, wasm_val_vec_t const *params, wasm_val_vec_t *results

wasm_trap_t* HostFuncMain_wrap(WASM_CB_PARAMS_LIST);

using getLedgerSqn_proto = int32_t(uint8_t*, int32_t);
wasm_trap_t* getLedgerSqn_wrap(WASM_SECONDARY_CB_PARAMS_LIST);

using getParentLedgerTime_proto = int32_t(uint8_t*, int32_t);
wasm_trap_t* getParentLedgerTime_wrap(WASM_SECONDARY_CB_PARAMS_LIST);

using getParentLedgerHash_proto = int32_t(uint8_t*, int32_t);
wasm_trap_t* getParentLedgerHash_wrap(WASM_SECONDARY_CB_PARAMS_LIST);

using getBaseFee_proto = int32_t(uint8_t*, int32_t);
wasm_trap_t* getBaseFee_wrap(WASM_SECONDARY_CB_PARAMS_LIST);

using isAmendmentEnabled_proto = int32_t(uint8_t const*, int32_t);
wasm_trap_t* isAmendmentEnabled_wrap(WASM_SECONDARY_CB_PARAMS_LIST);

using cacheLedgerObj_proto = int32_t(uint8_t const*, int32_t, int32_t);
wasm_trap_t* cacheLedgerObj_wrap(WASM_SECONDARY_CB_PARAMS_LIST);

using getTxField_proto = int32_t(int32_t, uint8_t*, int32_t);
wasm_trap_t* getTxField_wrap(WASM_SECONDARY_CB_PARAMS_LIST);

using getCurrentLedgerObjField_proto = int32_t(int32_t, uint8_t*, int32_t);
wasm_trap_t* getCurrentLedgerObjField_wrap(WASM_SECONDARY_CB_PARAMS_LIST);

using getLedgerObjField_proto = int32_t(int32_t, int32_t, uint8_t*, int32_t);
wasm_trap_t* getLedgerObjField_wrap(WASM_SECONDARY_CB_PARAMS_LIST);

using getTxNestedField_proto = int32_t(uint8_t const*, int32_t, uint8_t*, int32_t);
wasm_trap_t* getTxNestedField_wrap(WASM_SECONDARY_CB_PARAMS_LIST);

using getCurrentLedgerObjNestedField_proto = int32_t(uint8_t const*, int32_t, uint8_t*, int32_t);
wasm_trap_t* getCurrentLedgerObjNestedField_wrap(WASM_SECONDARY_CB_PARAMS_LIST);

using getLedgerObjNestedField_proto = int32_t(int32_t, uint8_t const*, int32_t, uint8_t*, int32_t);
wasm_trap_t* getLedgerObjNestedField_wrap(WASM_SECONDARY_CB_PARAMS_LIST);

using getTxArrayLen_proto = int32_t(int32_t);
wasm_trap_t* getTxArrayLen_wrap(WASM_SECONDARY_CB_PARAMS_LIST);

using getCurrentLedgerObjArrayLen_proto = int32_t(int32_t);
wasm_trap_t* getCurrentLedgerObjArrayLen_wrap(WASM_SECONDARY_CB_PARAMS_LIST);

using getLedgerObjArrayLen_proto = int32_t(int32_t, int32_t);
wasm_trap_t* getLedgerObjArrayLen_wrap(WASM_SECONDARY_CB_PARAMS_LIST);

using getTxNestedArrayLen_proto = int32_t(uint8_t const*, int32_t);
wasm_trap_t* getTxNestedArrayLen_wrap(WASM_SECONDARY_CB_PARAMS_LIST);

using getCurrentLedgerObjNestedArrayLen_proto = int32_t(uint8_t const*, int32_t);
wasm_trap_t* getCurrentLedgerObjNestedArrayLen_wrap(WASM_SECONDARY_CB_PARAMS_LIST);

using getLedgerObjNestedArrayLen_proto = int32_t(int32_t, uint8_t const*, int32_t);
wasm_trap_t* getLedgerObjNestedArrayLen_wrap(WASM_SECONDARY_CB_PARAMS_LIST);

using updateData_proto = int32_t(uint8_t const*, int32_t);
wasm_trap_t* updateData_wrap(WASM_SECONDARY_CB_PARAMS_LIST);

using checkSignature_proto =
    int32_t(uint8_t const*, int32_t, uint8_t const*, int32_t, uint8_t const*, int32_t);
wasm_trap_t* checkSignature_wrap(WASM_SECONDARY_CB_PARAMS_LIST);

using computeSha512HalfHash_proto = int32_t(uint8_t const*, int32_t, uint8_t*, int32_t);
wasm_trap_t* computeSha512HalfHash_wrap(WASM_SECONDARY_CB_PARAMS_LIST);

using accountKeylet_proto = int32_t(uint8_t const*, int32_t, uint8_t*, int32_t);
wasm_trap_t* accountKeylet_wrap(WASM_SECONDARY_CB_PARAMS_LIST);

using ammKeylet_proto =
    int32_t(uint8_t const*, int32_t, uint8_t const*, int32_t, uint8_t*, int32_t);
wasm_trap_t* ammKeylet_wrap(WASM_SECONDARY_CB_PARAMS_LIST);

using checkKeylet_proto =
    int32_t(uint8_t const*, int32_t, uint8_t const*, int32_t, uint8_t*, int32_t);
wasm_trap_t* checkKeylet_wrap(WASM_SECONDARY_CB_PARAMS_LIST);

using credentialKeylet_proto = int32_t(
    uint8_t const*,
    int32_t,
    uint8_t const*,
    int32_t,
    uint8_t const*,
    int32_t,
    uint8_t*,
    int32_t);
wasm_trap_t* credentialKeylet_wrap(WASM_SECONDARY_CB_PARAMS_LIST);

using delegateKeylet_proto =
    int32_t(uint8_t const*, int32_t, uint8_t const*, int32_t, uint8_t*, int32_t);
wasm_trap_t* delegateKeylet_wrap(WASM_SECONDARY_CB_PARAMS_LIST);

using depositPreauthKeylet_proto =
    int32_t(uint8_t const*, int32_t, uint8_t const*, int32_t, uint8_t*, int32_t);
wasm_trap_t* depositPreauthKeylet_wrap(WASM_SECONDARY_CB_PARAMS_LIST);

using didKeylet_proto = int32_t(uint8_t const*, int32_t, uint8_t*, int32_t);
wasm_trap_t* didKeylet_wrap(WASM_SECONDARY_CB_PARAMS_LIST);

using escrowKeylet_proto =
    int32_t(uint8_t const*, int32_t, uint8_t const*, int32_t, uint8_t*, int32_t);
wasm_trap_t* escrowKeylet_wrap(WASM_SECONDARY_CB_PARAMS_LIST);

using trustLineKeylet_proto = int32_t(
    uint8_t const*,
    int32_t,
    uint8_t const*,
    int32_t,
    uint8_t const*,
    int32_t,
    uint8_t*,
    int32_t);
wasm_trap_t* trustLineKeylet_wrap(WASM_SECONDARY_CB_PARAMS_LIST);

using mptokenIssuanceKeylet_proto =
    int32_t(uint8_t const*, int32_t, uint8_t const*, int32_t, uint8_t*, int32_t);
wasm_trap_t* mptokenIssuanceKeylet_wrap(WASM_SECONDARY_CB_PARAMS_LIST);

using mptokenKeylet_proto =
    int32_t(uint8_t const*, int32_t, uint8_t const*, int32_t, uint8_t*, int32_t);
wasm_trap_t* mptokenKeylet_wrap(WASM_SECONDARY_CB_PARAMS_LIST);

using nftokenOfferKeylet_proto =
    int32_t(uint8_t const*, int32_t, uint8_t const*, int32_t, uint8_t*, int32_t);
wasm_trap_t* nftokenOfferKeylet_wrap(WASM_SECONDARY_CB_PARAMS_LIST);

using offerKeylet_proto =
    int32_t(uint8_t const*, int32_t, uint8_t const*, int32_t, uint8_t*, int32_t);
wasm_trap_t* offerKeylet_wrap(WASM_SECONDARY_CB_PARAMS_LIST);

using oracleKeylet_proto =
    int32_t(uint8_t const*, int32_t, uint8_t const*, int32_t, uint8_t*, int32_t);
wasm_trap_t* oracleKeylet_wrap(WASM_SECONDARY_CB_PARAMS_LIST);

using paychannelKeylet_proto = int32_t(
    uint8_t const*,
    int32_t,
    uint8_t const*,
    int32_t,
    uint8_t const*,
    int32_t,
    uint8_t*,
    int32_t);
wasm_trap_t* paychannelKeylet_wrap(WASM_SECONDARY_CB_PARAMS_LIST);

using permissionedDomainKeylet_proto =
    int32_t(uint8_t const*, int32_t, uint8_t const*, int32_t, uint8_t*, int32_t);
wasm_trap_t* permissionedDomainKeylet_wrap(WASM_SECONDARY_CB_PARAMS_LIST);

using signerListKeylet_proto = int32_t(uint8_t const*, int32_t, uint8_t*, int32_t);
wasm_trap_t* signerListKeylet_wrap(WASM_SECONDARY_CB_PARAMS_LIST);

using ticketKeylet_proto =
    int32_t(uint8_t const*, int32_t, uint8_t const*, int32_t, uint8_t*, int32_t);
wasm_trap_t* ticketKeylet_wrap(WASM_SECONDARY_CB_PARAMS_LIST);

using vaultKeylet_proto =
    int32_t(uint8_t const*, int32_t, uint8_t const*, int32_t, uint8_t*, int32_t);
wasm_trap_t* vaultKeylet_wrap(WASM_SECONDARY_CB_PARAMS_LIST);

using getNFT_proto = int32_t(uint8_t const*, int32_t, uint8_t const*, int32_t, uint8_t*, int32_t);
wasm_trap_t* getNFT_wrap(WASM_SECONDARY_CB_PARAMS_LIST);

using getNFTIssuer_proto = int32_t(uint8_t const*, int32_t, uint8_t*, int32_t);
wasm_trap_t* getNFTIssuer_wrap(WASM_SECONDARY_CB_PARAMS_LIST);

using getNFTTaxon_proto = int32_t(uint8_t const*, int32_t, uint8_t*, int32_t);
wasm_trap_t* getNFTTaxon_wrap(WASM_SECONDARY_CB_PARAMS_LIST);

using getNFTFlags_proto = int32_t(uint8_t const*, int32_t);
wasm_trap_t* getNFTFlags_wrap(WASM_SECONDARY_CB_PARAMS_LIST);

using getNFTTransferFee_proto = int32_t(uint8_t const*, int32_t);
wasm_trap_t* getNFTTransferFee_wrap(WASM_SECONDARY_CB_PARAMS_LIST);

using getNFTSequence_proto = int32_t(uint8_t const*, int32_t, uint8_t*, int32_t);
wasm_trap_t* getNFTSequence_wrap(WASM_SECONDARY_CB_PARAMS_LIST);

using trace_proto = int32_t(uint8_t const*, int32_t, uint8_t const*, int32_t, int32_t);
wasm_trap_t* trace_wrap(WASM_SECONDARY_CB_PARAMS_LIST);

using traceNum_proto = int32_t(uint8_t const*, int32_t, int64_t);
wasm_trap_t* traceNum_wrap(WASM_SECONDARY_CB_PARAMS_LIST);

using traceAccount_proto = int32_t(uint8_t const*, int32_t, uint8_t const*, int32_t);
wasm_trap_t* traceAccount_wrap(WASM_SECONDARY_CB_PARAMS_LIST);

using traceFloat_proto = int32_t(uint8_t const*, int32_t, uint8_t const*, int32_t);
wasm_trap_t* traceFloat_wrap(WASM_SECONDARY_CB_PARAMS_LIST);

using traceAmount_proto = int32_t(uint8_t const*, int32_t, uint8_t const*, int32_t);
wasm_trap_t* traceAmount_wrap(WASM_SECONDARY_CB_PARAMS_LIST);

using floatFromInt_proto = int32_t(int64_t, uint8_t*, int32_t, int32_t);
wasm_trap_t* floatFromInt_wrap(WASM_SECONDARY_CB_PARAMS_LIST);

using floatFromUint_proto = int32_t(uint8_t const*, int32_t, uint8_t*, int32_t, int32_t);
wasm_trap_t* floatFromUint_wrap(WASM_SECONDARY_CB_PARAMS_LIST);

using floatFromSTAmount_proto = int32_t(uint8_t const*, int32_t, uint8_t*, int32_t, int32_t);
wasm_trap_t* floatFromSTAmount_wrap(WASM_SECONDARY_CB_PARAMS_LIST);

using floatFromSTNumber_proto = int32_t(uint8_t const*, int32_t, uint8_t*, int32_t, int32_t);
wasm_trap_t* floatFromSTNumber_wrap(WASM_SECONDARY_CB_PARAMS_LIST);

using floatToInt_proto = int32_t(uint8_t const*, int32_t, uint8_t*, int32_t, int32_t);
wasm_trap_t* floatToInt_wrap(WASM_SECONDARY_CB_PARAMS_LIST);

using floatToMantExp_proto = int32_t(uint8_t const*, int32_t, uint8_t*, int32_t, uint8_t*, int32_t);
wasm_trap_t* floatToMantExp_wrap(WASM_SECONDARY_CB_PARAMS_LIST);

using floatFromMantExp_proto = int32_t(int64_t, int32_t, uint8_t*, int32_t, int32_t);
wasm_trap_t* floatFromMantExp_wrap(WASM_SECONDARY_CB_PARAMS_LIST);

using floatCompare_proto = int32_t(uint8_t const*, int32_t, uint8_t const*, int32_t);
wasm_trap_t* floatCompare_wrap(WASM_SECONDARY_CB_PARAMS_LIST);

using floatAdd_proto =
    int32_t(uint8_t const*, int32_t, uint8_t const*, int32_t, uint8_t*, int32_t, int32_t);
wasm_trap_t* floatAdd_wrap(WASM_SECONDARY_CB_PARAMS_LIST);

using floatSubtract_proto =
    int32_t(uint8_t const*, int32_t, uint8_t const*, int32_t, uint8_t*, int32_t, int32_t);
wasm_trap_t* floatSubtract_wrap(WASM_SECONDARY_CB_PARAMS_LIST);

using floatMultiply_proto =
    int32_t(uint8_t const*, int32_t, uint8_t const*, int32_t, uint8_t*, int32_t, int32_t);
wasm_trap_t* floatMultiply_wrap(WASM_SECONDARY_CB_PARAMS_LIST);

using floatDivide_proto =
    int32_t(uint8_t const*, int32_t, uint8_t const*, int32_t, uint8_t*, int32_t, int32_t);
wasm_trap_t* floatDivide_wrap(WASM_SECONDARY_CB_PARAMS_LIST);

using floatRoot_proto = int32_t(uint8_t const*, int32_t, int32_t, uint8_t*, int32_t, int32_t);
wasm_trap_t* floatRoot_wrap(WASM_SECONDARY_CB_PARAMS_LIST);

using floatPower_proto = int32_t(uint8_t const*, int32_t, int32_t, uint8_t*, int32_t, int32_t);
wasm_trap_t* floatPower_wrap(WASM_SECONDARY_CB_PARAMS_LIST);

// Contract-specific host function wrappers

using instanceParam_proto = int32_t(int32_t, int32_t, uint8_t*, int32_t);
wasm_trap_t* instanceParam_wrap(WASM_SECONDARY_CB_PARAMS_LIST);

using functionParam_proto = int32_t(int32_t, int32_t, uint8_t*, int32_t);
wasm_trap_t* functionParam_wrap(WASM_SECONDARY_CB_PARAMS_LIST);

using getDataObjectField_proto =
    int32_t(uint8_t*, int32_t, uint8_t const*, int32_t, uint8_t*, int32_t);
wasm_trap_t* getDataObjectField_wrap(WASM_SECONDARY_CB_PARAMS_LIST);

using getDataNestedObjectField_proto =
    int32_t(uint8_t*, int32_t, uint8_t const*, int32_t, uint8_t const*, int32_t, uint8_t*, int32_t);
wasm_trap_t* getDataNestedObjectField_wrap(WASM_SECONDARY_CB_PARAMS_LIST);

using setDataObjectField_proto =
    int32_t(uint8_t*, int32_t, uint8_t const*, int32_t, uint8_t*, int32_t);
wasm_trap_t* setDataObjectField_wrap(WASM_SECONDARY_CB_PARAMS_LIST);

using setDataNestedObjectField_proto =
    int32_t(uint8_t*, int32_t, uint8_t const*, int32_t, uint8_t const*, int32_t, uint8_t*, int32_t);
wasm_trap_t* setDataNestedObjectField_wrap(WASM_SECONDARY_CB_PARAMS_LIST);

using getDataArrayElementField_proto =
    int32_t(uint8_t*, int32_t, int32_t, uint8_t const*, int32_t, uint8_t*, int32_t);
wasm_trap_t* getDataArrayElementField_wrap(WASM_SECONDARY_CB_PARAMS_LIST);

using getDataNestedArrayElementField_proto = int32_t(
    uint8_t*,
    int32_t,
    uint8_t const*,
    int32_t,
    int32_t,
    uint8_t const*,
    int32_t,
    uint8_t*,
    int32_t);
wasm_trap_t* getDataNestedArrayElementField_wrap(WASM_SECONDARY_CB_PARAMS_LIST);

using setDataArrayElementField_proto =
    int32_t(uint8_t*, int32_t, int32_t, uint8_t const*, int32_t, uint8_t*, int32_t);
wasm_trap_t* setDataArrayElementField_wrap(WASM_SECONDARY_CB_PARAMS_LIST);

using setDataNestedArrayElementField_proto = int32_t(
    uint8_t*,
    int32_t,
    uint8_t const*,
    int32_t,
    int32_t,
    uint8_t const*,
    int32_t,
    uint8_t*,
    int32_t);
wasm_trap_t* setDataNestedArrayElementField_wrap(WASM_SECONDARY_CB_PARAMS_LIST);

using buildTxn_proto = int32_t(int32_t);
wasm_trap_t* buildTxn_wrap(WASM_SECONDARY_CB_PARAMS_LIST);

using addTxnField_proto = int32_t(int32_t, int32_t, uint8_t const*, int32_t);
wasm_trap_t* addTxnField_wrap(WASM_SECONDARY_CB_PARAMS_LIST);

using emitBuiltTxn_proto = int32_t(int32_t);
wasm_trap_t* emitBuiltTxn_wrap(WASM_SECONDARY_CB_PARAMS_LIST);

using emitTxn_proto = int32_t(uint8_t const*, int32_t);
wasm_trap_t* emitTxn_wrap(WASM_SECONDARY_CB_PARAMS_LIST);

using emitEvent_proto = int32_t(uint8_t const*, int32_t, uint8_t const*, int32_t);
wasm_trap_t* emitEvent_wrap(WASM_SECONDARY_CB_PARAMS_LIST);

}  // namespace xrpl
