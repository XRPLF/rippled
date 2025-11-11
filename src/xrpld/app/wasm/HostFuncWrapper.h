#pragma once

#include <xrpld/app/wasm/WasmiVM.h>

namespace ripple {

using getLedgerSqn_proto = int32_t();
wasm_trap_t*
getLedgerSqn_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results);

using getParentLedgerTime_proto = int32_t();
wasm_trap_t*
getParentLedgerTime_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results);

using getParentLedgerHash_proto = int32_t(uint8_t*, int32_t);
wasm_trap_t*
getParentLedgerHash_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results);

using getBaseFee_proto = int32_t();
wasm_trap_t*
getBaseFee_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results);

using isAmendmentEnabled_proto = int32_t(uint8_t const*, int32_t);
wasm_trap_t*
isAmendmentEnabled_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results);

using cacheLedgerObj_proto = int32_t(uint8_t const*, int32_t, int32_t);
wasm_trap_t*
cacheLedgerObj_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results);

using getTxField_proto = int32_t(int32_t, uint8_t*, int32_t);
wasm_trap_t*
getTxField_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results);

using getCurrentLedgerObjField_proto = int32_t(int32_t, uint8_t*, int32_t);
wasm_trap_t*
getCurrentLedgerObjField_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results);

using getLedgerObjField_proto = int32_t(int32_t, int32_t, uint8_t*, int32_t);
wasm_trap_t*
getLedgerObjField_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results);

using getTxNestedField_proto =
    int32_t(uint8_t const*, int32_t, uint8_t*, int32_t);
wasm_trap_t*
getTxNestedField_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results);

using getCurrentLedgerObjNestedField_proto =
    int32_t(uint8_t const*, int32_t, uint8_t*, int32_t);
wasm_trap_t*
getCurrentLedgerObjNestedField_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results);

using getLedgerObjNestedField_proto =
    int32_t(int32_t, uint8_t const*, int32_t, uint8_t*, int32_t);
wasm_trap_t*
getLedgerObjNestedField_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results);

using getTxArrayLen_proto = int32_t(int32_t);
wasm_trap_t*
getTxArrayLen_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results);

using getCurrentLedgerObjArrayLen_proto = int32_t(int32_t);
wasm_trap_t*
getCurrentLedgerObjArrayLen_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results);

using getLedgerObjArrayLen_proto = int32_t(int32_t, int32_t);
wasm_trap_t*
getLedgerObjArrayLen_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results);

using getTxNestedArrayLen_proto = int32_t(uint8_t const*, int32_t);
wasm_trap_t*
getTxNestedArrayLen_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results);

using getCurrentLedgerObjNestedArrayLen_proto =
    int32_t(uint8_t const*, int32_t);
wasm_trap_t*
getCurrentLedgerObjNestedArrayLen_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results);

using getLedgerObjNestedArrayLen_proto =
    int32_t(int32_t, uint8_t const*, int32_t);
wasm_trap_t*
getLedgerObjNestedArrayLen_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results);

using updateData_proto = int32_t(uint8_t const*, int32_t);
wasm_trap_t*
updateData_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results);

using checkSignature_proto = int32_t(
    uint8_t const*,
    int32_t,
    uint8_t const*,
    int32_t,
    uint8_t const*,
    int32_t);
wasm_trap_t*
checkSignature_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results);

using computeSha512HalfHash_proto =
    int32_t(uint8_t const*, int32_t, uint8_t*, int32_t);
wasm_trap_t*
computeSha512HalfHash_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results);

using accountKeylet_proto = int32_t(uint8_t const*, int32_t, uint8_t*, int32_t);
wasm_trap_t*
accountKeylet_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results);

using ammKeylet_proto = int32_t(
    uint8_t const*,
    int32_t,
    uint8_t const*,
    int32_t,
    uint8_t*,
    int32_t);
wasm_trap_t*
ammKeylet_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results);

using checkKeylet_proto =
    int32_t(uint8_t const*, int32_t, int32_t, uint8_t*, int32_t);
wasm_trap_t*
checkKeylet_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results);

using credentialKeylet_proto = int32_t(
    uint8_t const*,
    int32_t,
    uint8_t const*,
    int32_t,
    uint8_t const*,
    int32_t,
    uint8_t*,
    int32_t);
wasm_trap_t*
credentialKeylet_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results);

using delegateKeylet_proto = int32_t(
    uint8_t const*,
    int32_t,
    uint8_t const*,
    int32_t,
    uint8_t*,
    int32_t);
wasm_trap_t*
delegateKeylet_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results);

using depositPreauthKeylet_proto = int32_t(
    uint8_t const*,
    int32_t,
    uint8_t const*,
    int32_t,
    uint8_t*,
    int32_t);
wasm_trap_t*
depositPreauthKeylet_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results);

using didKeylet_proto = int32_t(uint8_t const*, int32_t, uint8_t*, int32_t);
wasm_trap_t*
didKeylet_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results);

using escrowKeylet_proto =
    int32_t(uint8_t const*, int32_t, int32_t, uint8_t*, int32_t);
wasm_trap_t*
escrowKeylet_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results);

using lineKeylet_proto = int32_t(
    uint8_t const*,
    int32_t,
    uint8_t const*,
    int32_t,
    uint8_t const*,
    int32_t,
    uint8_t*,
    int32_t);
wasm_trap_t*
lineKeylet_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results);

using mptIssuanceKeylet_proto =
    int32_t(uint8_t const*, int32_t, int32_t, uint8_t*, int32_t);
wasm_trap_t*
mptIssuanceKeylet_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results);

using mptokenKeylet_proto = int32_t(
    uint8_t const*,
    int32_t,
    uint8_t const*,
    int32_t,
    uint8_t*,
    int32_t);
wasm_trap_t*
mptokenKeylet_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results);

using nftOfferKeylet_proto =
    int32_t(uint8_t const*, int32_t, int32_t, uint8_t*, int32_t);
wasm_trap_t*
nftOfferKeylet_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results);

using offerKeylet_proto =
    int32_t(uint8_t const*, int32_t, int32_t, uint8_t*, int32_t);
wasm_trap_t*
offerKeylet_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results);

using oracleKeylet_proto =
    int32_t(uint8_t const*, int32_t, int32_t, uint8_t*, int32_t);
wasm_trap_t*
oracleKeylet_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results);

using paychanKeylet_proto = int32_t(
    uint8_t const*,
    int32_t,
    uint8_t const*,
    int32_t,
    int32_t,
    uint8_t*,
    int32_t);
wasm_trap_t*
paychanKeylet_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results);

using permissionedDomainKeylet_proto =
    int32_t(uint8_t const*, int32_t, int32_t, uint8_t*, int32_t);
wasm_trap_t*
permissionedDomainKeylet_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results);

using signersKeylet_proto = int32_t(uint8_t const*, int32_t, uint8_t*, int32_t);
wasm_trap_t*
signersKeylet_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results);

using ticketKeylet_proto =
    int32_t(uint8_t const*, int32_t, int32_t, uint8_t*, int32_t);
wasm_trap_t*
ticketKeylet_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results);

using vaultKeylet_proto =
    int32_t(uint8_t const*, int32_t, int32_t, uint8_t*, int32_t);
wasm_trap_t*
vaultKeylet_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results);

using getNFT_proto = int32_t(
    uint8_t const*,
    int32_t,
    uint8_t const*,
    int32_t,
    uint8_t*,
    int32_t);
wasm_trap_t*
getNFT_wrap(void* env, wasm_val_vec_t const* params, wasm_val_vec_t* results);

using getNFTIssuer_proto = int32_t(uint8_t const*, int32_t, uint8_t*, int32_t);
wasm_trap_t*
getNFTIssuer_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results);

using getNFTTaxon_proto = int32_t(uint8_t const*, int32_t, uint8_t*, int32_t);
wasm_trap_t*
getNFTTaxon_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results);

using getNFTFlags_proto = int32_t(uint8_t const*, int32_t);
wasm_trap_t*
getNFTFlags_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results);

using getNFTTransferFee_proto = int32_t(uint8_t const*, int32_t);
wasm_trap_t*
getNFTTransferFee_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results);

using getNFTSerial_proto = int32_t(uint8_t const*, int32_t, uint8_t*, int32_t);
wasm_trap_t*
getNFTSerial_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results);

using trace_proto =
    int32_t(uint8_t const*, int32_t, uint8_t const*, int32_t, int32_t);
wasm_trap_t*
trace_wrap(void* env, wasm_val_vec_t const* params, wasm_val_vec_t* results);

using traceNum_proto = int32_t(uint8_t const*, int32_t, int64_t);
wasm_trap_t*
traceNum_wrap(void* env, wasm_val_vec_t const* params, wasm_val_vec_t* results);

using traceAccount_proto =
    int32_t(uint8_t const*, int32_t, uint8_t const*, int32_t);
wasm_trap_t*
traceAccount_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results);

using traceFloat_proto =
    int32_t(uint8_t const*, int32_t, uint8_t const*, int32_t);
wasm_trap_t*
traceFloat_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results);

using traceAmount_proto =
    int32_t(uint8_t const*, int32_t, uint8_t const*, int32_t);
wasm_trap_t*
traceAmount_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results);

using floatFromInt_proto = int32_t(int64_t, uint8_t*, int32_t, int32_t);
wasm_trap_t*
floatFromInt_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results);

using floatFromUint_proto =
    int32_t(uint8_t const*, int32_t, uint8_t*, int32_t, int32_t);
wasm_trap_t*
floatFromUint_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results);

using floatSet_proto = int32_t(int32_t, int64_t, uint8_t*, int32_t, int32_t);
wasm_trap_t*
floatSet_wrap(void* env, wasm_val_vec_t const* params, wasm_val_vec_t* results);

using floatCompare_proto =
    int32_t(uint8_t const*, int32_t, uint8_t const*, int32_t);
wasm_trap_t*
floatCompare_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results);

using floatAdd_proto = int32_t(
    uint8_t const*,
    int32_t,
    uint8_t const*,
    int32_t,
    uint8_t*,
    int32_t,
    int32_t);
wasm_trap_t*
floatAdd_wrap(void* env, wasm_val_vec_t const* params, wasm_val_vec_t* results);

using floatSubtract_proto = int32_t(
    uint8_t const*,
    int32_t,
    uint8_t const*,
    int32_t,
    uint8_t*,
    int32_t,
    int32_t);
wasm_trap_t*
floatSubtract_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results);

using floatMultiply_proto = int32_t(
    uint8_t const*,
    int32_t,
    uint8_t const*,
    int32_t,
    uint8_t*,
    int32_t,
    int32_t);
wasm_trap_t*
floatMultiply_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results);

using floatDivide_proto = int32_t(
    uint8_t const*,
    int32_t,
    uint8_t const*,
    int32_t,
    uint8_t*,
    int32_t,
    int32_t);
wasm_trap_t*
floatDivide_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results);

using floatRoot_proto =
    int32_t(uint8_t const*, int32_t, int32_t, uint8_t*, int32_t, int32_t);
wasm_trap_t*
floatRoot_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results);

using floatPower_proto =
    int32_t(uint8_t const*, int32_t, int32_t, uint8_t*, int32_t, int32_t);
wasm_trap_t*
floatPower_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results);

using floatLog_proto =
    int32_t(uint8_t const*, int32_t, uint8_t*, int32_t, int32_t);
wasm_trap_t*
floatLog_wrap(void* env, wasm_val_vec_t const* params, wasm_val_vec_t* results);

}  // namespace ripple
