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

#ifndef RIPPLE_APP_MISC_WASMHOSTFUNCWRAPPER_H_INCLUDED
#define RIPPLE_APP_MISC_WASMHOSTFUNCWRAPPER_H_INCLUDED

#include <xrpld/app/misc/WamrVM.h>

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

using getTxField_proto = uint32_t*(char const*, int32_t);
wasm_trap_t*
getTxField_wrap(
    void* env,
    const wasm_val_vec_t* params,
    wasm_val_vec_t* results);

using getLedgerEntryField_proto =
    uint32_t*(int32_t, uint8_t const*, int32_t, char const*, int32_t);
wasm_trap_t*
getLedgerEntryField_wrap(
    void* env,
    const wasm_val_vec_t* params,
    wasm_val_vec_t* results);

using getCurrentLedgerEntryField_proto = uint32_t*(char const*, int32_t);
wasm_trap_t*
getCurrentLedgerEntryField_wrap(
    void* env,
    const wasm_val_vec_t* params,
    wasm_val_vec_t* results);

using getNFT_proto = uint32_t*(char const*, int32_t, char const*, int32_t);
wasm_trap_t*
getNFT_wrap(void* env, const wasm_val_vec_t* params, wasm_val_vec_t* results);

using accountKeylet_proto = uint32_t*(char const*, int32_t);
wasm_trap_t*
accountKeylet_wrap(
    void* env,
    const wasm_val_vec_t* params,
    wasm_val_vec_t* results);

using credentialKeylet_proto =
    uint32_t*(char const*, int32_t, char const*, int32_t, char const*, int32_t);
wasm_trap_t*
credentialKeylet_wrap(
    void* env,
    const wasm_val_vec_t* params,
    wasm_val_vec_t* results);

using escrowKeylet_proto = uint32_t*(char const*, int32_t, int32_t);
wasm_trap_t*
escrowKeylet_wrap(
    void* env,
    const wasm_val_vec_t* params,
    wasm_val_vec_t* results);

using oracleKeylet_proto = uint32_t*(char const*, int32_t, int32_t);
wasm_trap_t*
oracleKeylet_wrap(
    void* env,
    const wasm_val_vec_t* params,
    wasm_val_vec_t* results);

using updateData_proto = void(uint8_t const*, int32_t);
wasm_trap_t*
updateData_wrap(
    void* env,
    const wasm_val_vec_t* params,
    wasm_val_vec_t* results);

using computeSha512HalfHash_proto = uint32_t*(uint8_t const*, int32_t);
wasm_trap_t*
computeSha512HalfHash_wrap(
    void* env,
    const wasm_val_vec_t* params,
    wasm_val_vec_t* results);

using print_proto = void(char const*, int32_t);
wasm_trap_t*
print_wrap(void* env, const wasm_val_vec_t* params, wasm_val_vec_t* results);

}  // namespace ripple

#endif  // RIPPLE_APP_MISC_WASMHOSTFUNCWRAPPER_H_INCLUDED
