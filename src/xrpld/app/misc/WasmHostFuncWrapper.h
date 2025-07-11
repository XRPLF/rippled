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

#include <xrpld/app/misc/WamrVM.h>

namespace ripple {

using SFieldCRef = std::reference_wrapper<SField const>;

// helper functions, only in `.h` for testing purposes
template <class IW>
Expected<int32_t, HostFunctionError>
getDataInt32(IW const* _rt, wasm_val_vec_t const* params, int32_t& i);
template <class IW>
Expected<int64_t, HostFunctionError>
getDataInt64(IW const* _rt, wasm_val_vec_t const* params, int32_t& i);
template <class IW>
Expected<SFieldCRef, HostFunctionError>
getDataSField(IW const* _rt, wasm_val_vec_t const* params, int32_t& i);
template <class IW>
Expected<Slice, HostFunctionError>
getDataSlice(IW const* rt, wasm_val_vec_t const* params, int32_t& i);
template <class IW>
Expected<uint256, HostFunctionError>
getDataUInt256(IW const* rt, wasm_val_vec_t const* params, int32_t& i);
template <class IW>
Expected<AccountID, HostFunctionError>
getDataAccountID(IW const* rt, wasm_val_vec_t const* params, int32_t& i);
template <class IW>
Expected<std::string_view, HostFunctionError>
getDataString(IW const* rt, wasm_val_vec_t const* params, int32_t& i);

using getLedgerSqnOld_proto = int32_t();
wasm_trap_t*
getLedgerSqnOld_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results);

using getLedgerSqn_proto = int32_t(uint8_t*, int32_t);
wasm_trap_t*
getLedgerSqn_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results);

using getParentLedgerTime_proto = int32_t(uint8_t*, int32_t);
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

using escrowKeylet_proto =
    int32_t(uint8_t const*, int32_t, int32_t, uint8_t*, int32_t);
wasm_trap_t*
escrowKeylet_wrap(
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

using getNFT_proto = int32_t(
    uint8_t const*,
    int32_t,
    uint8_t const*,
    int32_t,
    uint8_t*,
    int32_t);
wasm_trap_t*
getNFT_wrap(void* env, wasm_val_vec_t const* params, wasm_val_vec_t* results);

using trace_proto =
    int32_t(uint8_t const*, int32_t, uint8_t const*, int32_t, int32_t);
wasm_trap_t*
trace_wrap(void* env, wasm_val_vec_t const* params, wasm_val_vec_t* results);

using traceNum_proto = int32_t(uint8_t const*, int32_t, int64_t);
wasm_trap_t*
traceNum_wrap(void* env, wasm_val_vec_t const* params, wasm_val_vec_t* results);

}  // namespace ripple
