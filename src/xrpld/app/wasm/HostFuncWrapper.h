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

// #include <xrpld/app/wasm/WamrVM.h>
#include <xrpld/app/wasm/WasmtimeVM.h>

namespace ripple {

// wasm_trap_t *(*wasmtime_func_callback_t)(
//     void *env, wasmtime_caller_t *caller, const wasmtime_val_t *args,
//     size_t nargs, wasmtime_val_t *results, size_t nresults);

#define WRAPPER_PARAMS                                                  \
    void *env, wasmtime_caller_t *caller, const wasmtime_val_t *params, \
        size_t nargs, wasmtime_val_t *results, size_t nresults

using getLedgerSqn_proto = int32_t();
wasm_trap_t* getLedgerSqn_wrap(WRAPPER_PARAMS);

using getParentLedgerTime_proto = int32_t();
wasm_trap_t* getParentLedgerTime_wrap(WRAPPER_PARAMS);

using getParentLedgerHash_proto = int32_t(uint8_t*, int32_t);
wasm_trap_t* getParentLedgerHash_wrap(WRAPPER_PARAMS);

using getLedgerAccountHash_proto = int32_t(uint8_t*, int32_t);
wasm_trap_t* getLedgerAccountHash_wrap(WRAPPER_PARAMS);

using getLedgerTransactionHash_proto = int32_t(uint8_t*, int32_t);
wasm_trap_t* getLedgerTransactionHash_wrap(WRAPPER_PARAMS);

using getBaseFee_proto = int32_t();
wasm_trap_t* getBaseFee_wrap(WRAPPER_PARAMS);

using isAmendmentEnabled_proto = int32_t(uint8_t const*, int32_t);
wasm_trap_t* isAmendmentEnabled_wrap(WRAPPER_PARAMS);

using cacheLedgerObj_proto = int32_t(uint8_t const*, int32_t, int32_t);
wasm_trap_t* cacheLedgerObj_wrap(WRAPPER_PARAMS);

using getTxField_proto = int32_t(int32_t, uint8_t*, int32_t);
wasm_trap_t* getTxField_wrap(WRAPPER_PARAMS);

using getCurrentLedgerObjField_proto = int32_t(int32_t, uint8_t*, int32_t);
wasm_trap_t* getCurrentLedgerObjField_wrap(WRAPPER_PARAMS);

using getLedgerObjField_proto = int32_t(int32_t, int32_t, uint8_t*, int32_t);
wasm_trap_t* getLedgerObjField_wrap(WRAPPER_PARAMS);

using getTxNestedField_proto =
    int32_t(uint8_t const*, int32_t, uint8_t*, int32_t);
wasm_trap_t* getTxNestedField_wrap(WRAPPER_PARAMS);

using getCurrentLedgerObjNestedField_proto =
    int32_t(uint8_t const*, int32_t, uint8_t*, int32_t);
wasm_trap_t* getCurrentLedgerObjNestedField_wrap(WRAPPER_PARAMS);

using getLedgerObjNestedField_proto =
    int32_t(int32_t, uint8_t const*, int32_t, uint8_t*, int32_t);
wasm_trap_t* getLedgerObjNestedField_wrap(WRAPPER_PARAMS);

using getTxArrayLen_proto = int32_t(int32_t);
wasm_trap_t* getTxArrayLen_wrap(WRAPPER_PARAMS);

using getCurrentLedgerObjArrayLen_proto = int32_t(int32_t);
wasm_trap_t* getCurrentLedgerObjArrayLen_wrap(WRAPPER_PARAMS);

using getLedgerObjArrayLen_proto = int32_t(int32_t, int32_t);
wasm_trap_t* getLedgerObjArrayLen_wrap(WRAPPER_PARAMS);

using getTxNestedArrayLen_proto = int32_t(uint8_t const*, int32_t);
wasm_trap_t* getTxNestedArrayLen_wrap(WRAPPER_PARAMS);

using getCurrentLedgerObjNestedArrayLen_proto =
    int32_t(uint8_t const*, int32_t);
wasm_trap_t* getCurrentLedgerObjNestedArrayLen_wrap(WRAPPER_PARAMS);

using getLedgerObjNestedArrayLen_proto =
    int32_t(int32_t, uint8_t const*, int32_t);
wasm_trap_t* getLedgerObjNestedArrayLen_wrap(WRAPPER_PARAMS);

using updateData_proto = int32_t(uint8_t const*, int32_t);
wasm_trap_t* updateData_wrap(WRAPPER_PARAMS);

using checkSignature_proto = int32_t(
    uint8_t const*,
    int32_t,
    uint8_t const*,
    int32_t,
    uint8_t const*,
    int32_t);
wasm_trap_t* checkSignature_wrap(WRAPPER_PARAMS);

using computeSha512HalfHash_proto =
    int32_t(uint8_t const*, int32_t, uint8_t*, int32_t);
wasm_trap_t* computeSha512HalfHash_wrap(WRAPPER_PARAMS);

using accountKeylet_proto = int32_t(uint8_t const*, int32_t, uint8_t*, int32_t);
wasm_trap_t* accountKeylet_wrap(WRAPPER_PARAMS);

using ammKeylet_proto = int32_t(
    uint8_t const*,
    int32_t,
    uint8_t const*,
    int32_t,
    uint8_t*,
    int32_t);
wasm_trap_t* ammKeylet_wrap(WRAPPER_PARAMS);

using checkKeylet_proto =
    int32_t(uint8_t const*, int32_t, int32_t, uint8_t*, int32_t);
wasm_trap_t* checkKeylet_wrap(WRAPPER_PARAMS);

using credentialKeylet_proto = int32_t(
    uint8_t const*,
    int32_t,
    uint8_t const*,
    int32_t,
    uint8_t const*,
    int32_t,
    uint8_t*,
    int32_t);
wasm_trap_t* credentialKeylet_wrap(WRAPPER_PARAMS);

using delegateKeylet_proto = int32_t(
    uint8_t const*,
    int32_t,
    uint8_t const*,
    int32_t,
    uint8_t*,
    int32_t);
wasm_trap_t* delegateKeylet_wrap(WRAPPER_PARAMS);

using depositPreauthKeylet_proto = int32_t(
    uint8_t const*,
    int32_t,
    uint8_t const*,
    int32_t,
    uint8_t*,
    int32_t);
wasm_trap_t* depositPreauthKeylet_wrap(WRAPPER_PARAMS);

using didKeylet_proto = int32_t(uint8_t const*, int32_t, uint8_t*, int32_t);
wasm_trap_t* didKeylet_wrap(WRAPPER_PARAMS);

using escrowKeylet_proto =
    int32_t(uint8_t const*, int32_t, int32_t, uint8_t*, int32_t);
wasm_trap_t* escrowKeylet_wrap(WRAPPER_PARAMS);

using lineKeylet_proto = int32_t(
    uint8_t const*,
    int32_t,
    uint8_t const*,
    int32_t,
    uint8_t const*,
    int32_t,
    uint8_t*,
    int32_t);
wasm_trap_t* lineKeylet_wrap(WRAPPER_PARAMS);

using mptIssuanceKeylet_proto =
    int32_t(uint8_t const*, int32_t, int32_t, uint8_t*, int32_t);
wasm_trap_t* mptIssuanceKeylet_wrap(WRAPPER_PARAMS);

using mptokenKeylet_proto = int32_t(
    uint8_t const*,
    int32_t,
    uint8_t const*,
    int32_t,
    uint8_t*,
    int32_t);
wasm_trap_t* mptokenKeylet_wrap(WRAPPER_PARAMS);

using nftOfferKeylet_proto =
    int32_t(uint8_t const*, int32_t, int32_t, uint8_t*, int32_t);
wasm_trap_t* nftOfferKeylet_wrap(WRAPPER_PARAMS);

using offerKeylet_proto =
    int32_t(uint8_t const*, int32_t, int32_t, uint8_t*, int32_t);
wasm_trap_t* offerKeylet_wrap(WRAPPER_PARAMS);

using oracleKeylet_proto =
    int32_t(uint8_t const*, int32_t, int32_t, uint8_t*, int32_t);
wasm_trap_t* oracleKeylet_wrap(WRAPPER_PARAMS);

using paychanKeylet_proto = int32_t(
    uint8_t const*,
    int32_t,
    uint8_t const*,
    int32_t,
    int32_t,
    uint8_t*,
    int32_t);
wasm_trap_t* paychanKeylet_wrap(WRAPPER_PARAMS);

using permissionedDomainKeylet_proto =
    int32_t(uint8_t const*, int32_t, int32_t, uint8_t*, int32_t);
wasm_trap_t* permissionedDomainKeylet_wrap(WRAPPER_PARAMS);

using signersKeylet_proto = int32_t(uint8_t const*, int32_t, uint8_t*, int32_t);
wasm_trap_t* signersKeylet_wrap(WRAPPER_PARAMS);

using ticketKeylet_proto =
    int32_t(uint8_t const*, int32_t, int32_t, uint8_t*, int32_t);
wasm_trap_t* ticketKeylet_wrap(WRAPPER_PARAMS);

using vaultKeylet_proto =
    int32_t(uint8_t const*, int32_t, int32_t, uint8_t*, int32_t);
wasm_trap_t* vaultKeylet_wrap(WRAPPER_PARAMS);

using getNFT_proto = int32_t(
    uint8_t const*,
    int32_t,
    uint8_t const*,
    int32_t,
    uint8_t*,
    int32_t);
wasm_trap_t* getNFT_wrap(WRAPPER_PARAMS);

using getNFTIssuer_proto = int32_t(uint8_t const*, int32_t, uint8_t*, int32_t);
wasm_trap_t* getNFTIssuer_wrap(WRAPPER_PARAMS);

using getNFTTaxon_proto = int32_t(uint8_t const*, int32_t, uint8_t*, int32_t);
wasm_trap_t* getNFTTaxon_wrap(WRAPPER_PARAMS);

using getNFTFlags_proto = int32_t(uint8_t const*, int32_t);
wasm_trap_t* getNFTFlags_wrap(WRAPPER_PARAMS);

using getNFTTransferFee_proto = int32_t(uint8_t const*, int32_t);
wasm_trap_t* getNFTTransferFee_wrap(WRAPPER_PARAMS);

using getNFTSerial_proto = int32_t(uint8_t const*, int32_t, uint8_t*, int32_t);
wasm_trap_t* getNFTSerial_wrap(WRAPPER_PARAMS);

using trace_proto =
    int32_t(uint8_t const*, int32_t, uint8_t const*, int32_t, int32_t);
wasm_trap_t* trace_wrap(WRAPPER_PARAMS);

using traceNum_proto = int32_t(uint8_t const*, int32_t, int64_t);
wasm_trap_t* traceNum_wrap(WRAPPER_PARAMS);

using traceAccount_proto =
    int32_t(uint8_t const*, int32_t, uint8_t const*, int32_t);
wasm_trap_t* traceAccount_wrap(WRAPPER_PARAMS);

using traceFloat_proto =
    int32_t(uint8_t const*, int32_t, uint8_t const*, int32_t);
wasm_trap_t* traceFloat_wrap(WRAPPER_PARAMS);

using traceAmount_proto =
    int32_t(uint8_t const*, int32_t, uint8_t const*, int32_t);
wasm_trap_t* traceAmount_wrap(WRAPPER_PARAMS);

using floatFromInt_proto = int32_t(int64_t, uint8_t*, int32_t, int32_t);
wasm_trap_t* floatFromInt_wrap(WRAPPER_PARAMS);

using floatFromUint_proto =
    int32_t(uint8_t const*, int32_t, uint8_t*, int32_t, int32_t);
wasm_trap_t* floatFromUint_wrap(WRAPPER_PARAMS);

using floatSet_proto = int32_t(int32_t, int64_t, uint8_t*, int32_t, int32_t);
wasm_trap_t* floatSet_wrap(WRAPPER_PARAMS);

using floatCompare_proto =
    int32_t(uint8_t const*, int32_t, uint8_t const*, int32_t);
wasm_trap_t* floatCompare_wrap(WRAPPER_PARAMS);

using floatAdd_proto = int32_t(
    uint8_t const*,
    int32_t,
    uint8_t const*,
    int32_t,
    uint8_t*,
    int32_t,
    int32_t);
wasm_trap_t* floatAdd_wrap(WRAPPER_PARAMS);

using floatSubtract_proto = int32_t(
    uint8_t const*,
    int32_t,
    uint8_t const*,
    int32_t,
    uint8_t*,
    int32_t,
    int32_t);
wasm_trap_t* floatSubtract_wrap(WRAPPER_PARAMS);

using floatMultiply_proto = int32_t(
    uint8_t const*,
    int32_t,
    uint8_t const*,
    int32_t,
    uint8_t*,
    int32_t,
    int32_t);
wasm_trap_t* floatMultiply_wrap(WRAPPER_PARAMS);

using floatDivide_proto = int32_t(
    uint8_t const*,
    int32_t,
    uint8_t const*,
    int32_t,
    uint8_t*,
    int32_t,
    int32_t);
wasm_trap_t* floatDivide_wrap(WRAPPER_PARAMS);

using floatRoot_proto =
    int32_t(uint8_t const*, int32_t, int32_t, uint8_t*, int32_t, int32_t);
wasm_trap_t* floatRoot_wrap(WRAPPER_PARAMS);

using floatPower_proto =
    int32_t(uint8_t const*, int32_t, int32_t, uint8_t*, int32_t, int32_t);
wasm_trap_t* floatPower_wrap(WRAPPER_PARAMS);

using floatLog_proto =
    int32_t(uint8_t const*, int32_t, uint8_t*, int32_t, int32_t);
wasm_trap_t* floatLog_wrap(WRAPPER_PARAMS);

}  // namespace ripple
