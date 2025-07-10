//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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
#include <xrpld/app/tx/detail/NFTokenUtils.h>

#include <xrpl/protocol/digest.h>

namespace ripple {

using SFieldCRef = std::reference_wrapper<SField const>;

static int32_t
setData(
    InstanceWrapper const* rt,
    int32_t dst,
    int32_t dsz,
    uint8_t const* src,
    int32_t ssz)
{
    if (!ssz)
        return 0;

    if (dst < 0 || dsz < 0 || !src || ssz < 0)
        return static_cast<std::underlying_type_t<HostFunctionError>>(
            HostFunctionError::INVALID_PARAMS);

    auto mem = rt ? rt->getMem() : wmem();

    if (!mem.s)
        return static_cast<std::underlying_type_t<HostFunctionError>>(
            HostFunctionError::NO_MEM_EXPORTED);
    if (dst + dsz > mem.s)
        return static_cast<std::underlying_type_t<HostFunctionError>>(
            HostFunctionError::POINTER_OUT_OF_BOUNDS);
    if (ssz > dsz)
        return static_cast<std::underlying_type_t<HostFunctionError>>(
            HostFunctionError::BUFFER_TOO_SMALL);

    memcpy(mem.p + dst, src, ssz);

    return ssz;
}

template <typename T>
Expected<T, HostFunctionError>
getData(InstanceWrapper const* rt, wasm_val_vec_t const* params, int32_t& src);

template <>
Expected<int32_t, HostFunctionError>
getData<int32_t>(
    InstanceWrapper const* _rt,
    wasm_val_vec_t const* params,
    int32_t& i)
{
    auto const result = params->data[i].of.i32;
    i++;
    return result;
}

template <>
Expected<int64_t, HostFunctionError>
getData<int64_t>(
    InstanceWrapper const* _rt,
    wasm_val_vec_t const* params,
    int32_t& i)
{
    auto const result = params->data[i].of.i64;
    i++;
    return result;
}

template <>
Expected<SFieldCRef, HostFunctionError>
getData<SFieldCRef>(
    InstanceWrapper const* _rt,
    wasm_val_vec_t const* params,
    int32_t& i)
{
    auto const& m = SField::getKnownCodeToField();
    auto const it = m.find(params->data[i].of.i32);
    if (it == m.end())
    {
        return Unexpected(HostFunctionError::INVALID_FIELD);
    }
    i++;
    return *it->second;
}

template <>
Expected<Slice, HostFunctionError>
getData<Slice>(
    InstanceWrapper const* rt,
    wasm_val_vec_t const* params,
    int32_t& i)
{
    auto const src = params->data[i].of.i32;
    auto const ssz = params->data[i + 1].of.i32;
    if (src < 0 || ssz <= 0)
        return Unexpected(HostFunctionError::INVALID_PARAMS);

    auto mem = rt ? rt->getMem() : wmem();
    if (!mem.s)
        return Unexpected(HostFunctionError::NO_MEM_EXPORTED);

    if (src + ssz > mem.s)
        return Unexpected(HostFunctionError::POINTER_OUT_OF_BOUNDS);

    Slice data(mem.p + src, ssz);
    i += 2;
    return data;
}

template <>
Expected<uint256, HostFunctionError>
getData<uint256>(
    InstanceWrapper const* rt,
    wasm_val_vec_t const* params,
    int32_t& i)
{
    auto const r = getData<Slice>(rt, params, i);
    if (!r)
    {
        return Unexpected(r.error());
    }

    if (r->size() != uint256::bytes)
    {
        return Unexpected(HostFunctionError::INVALID_PARAMS);
    }
    return uint256::fromVoid(r->data());
}

template <>
Expected<AccountID, HostFunctionError>
getData<AccountID>(
    InstanceWrapper const* rt,
    wasm_val_vec_t const* params,
    int32_t& i)
{
    auto const r = getData<Slice>(rt, params, i);
    if (!r || (r->size() != AccountID::bytes))
        return Unexpected(HostFunctionError::INVALID_PARAMS);

    return AccountID::fromVoid(r->data());
}

template <>
Expected<std::string_view, HostFunctionError>
getData<std::string_view>(
    InstanceWrapper const* rt,
    wasm_val_vec_t const* params,
    int32_t& i)
{
    auto const src = params->data[i].of.i32;
    auto const ssz = params->data[i + 1].of.i32;
    if (src < 0 || ssz <= 0)
        return Unexpected(HostFunctionError::INVALID_PARAMS);

    auto mem = rt ? rt->getMem() : wmem();
    if (!mem.s)
        return Unexpected(HostFunctionError::NO_MEM_EXPORTED);

    if (src + ssz > mem.s)
        return Unexpected(HostFunctionError::POINTER_OUT_OF_BOUNDS);

    std::string data(mem.p + src, mem.p + src + ssz);
    i += 2;
    return std::string_view(data);
}

template <class T>
std::nullptr_t
hfResult(wasm_val_vec_t* results, T value);

template <>
std::nullptr_t
hfResult(wasm_val_vec_t* results, int32_t value)
{
    results->data[0] = WASM_I32_VAL(value);
    results->num_elems = 1;
    return nullptr;
}
template <>
std::nullptr_t
hfResult(wasm_val_vec_t* results, HostFunctionError value)
{
    results->data[0] = WASM_I32_VAL(
        static_cast<std::underlying_type_t<HostFunctionError>>(value));
    results->num_elems = 1;
    return nullptr;
}

template <typename T>
std::nullptr_t
returnResult(
    InstanceWrapper const* rt,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results,
    Expected<T, HostFunctionError> const& result,
    int32_t index)
{
    if (!result)
    {
        return hfResult(results, result.error());
    }
    if constexpr (std::is_same_v<std::decay_t<decltype(*result)>, Bytes>)
    {
        return hfResult(
            results,
            setData(
                rt,
                params->data[index].of.i32,
                params->data[index + 1].of.i32,
                result->data(),
                result->size()));
    }
    else if constexpr (std::is_same_v<std::decay_t<decltype(*result)>, Hash>)
    {
        return hfResult(
            results,
            setData(
                rt,
                params->data[index].of.i32,
                params->data[index + 1].of.i32,
                result->data(),
                result->size()));
    }
    else if constexpr (std::is_same_v<std::decay_t<decltype(*result)>, int32_t>)
    {
        return hfResult(results, result.value());
    }
    else if constexpr (std::is_same_v<
                           std::decay_t<decltype(*result)>,
                           std::uint32_t>)
    {
        auto const resultValue = result.value();
        return hfResult(
            results,
            setData(
                rt,
                params->data[index].of.i32,
                params->data[index + 1].of.i32,
                reinterpret_cast<uint8_t const*>(&resultValue),
                static_cast<int32_t>(sizeof(resultValue))));
    }
    else
    {
        static_assert(
            [] { return false; }(), "Unhandled return type in returnResult");
    }
}

wasm_trap_t*
getLedgerSqnOld_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    // auto const* rt = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    auto const sqn = hf->getLedgerSqn();
    if (!sqn)
    {
        return hfResult(results, sqn.error());
    }
    return hfResult(results, static_cast<int32_t>(sqn.value()));
}

wasm_trap_t*
getLedgerSqn_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* rt = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    return returnResult(rt, params, results, hf->getLedgerSqn(), index);
}

wasm_trap_t*
getParentLedgerTime_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* rt = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    return returnResult(rt, params, results, hf->getParentLedgerTime(), index);
}

wasm_trap_t*
getParentLedgerHash_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* rt = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    return returnResult(rt, params, results, hf->getParentLedgerHash(), index);
}

wasm_trap_t*
cacheLedgerObj_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* rt = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const id = getData<uint256>(rt, params, index);
    if (!id)
    {
        return hfResult(results, id.error());
    }

    auto const cache = getData<int32_t>(rt, params, index);
    if (!cache)
    {
        return hfResult(results, cache.error());
    }

    return returnResult(
        rt, params, results, hf->cacheLedgerObj(*id, *cache), index);
}

wasm_trap_t*
getTxField_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* rt = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const fname = getData<SFieldCRef>(rt, params, index);
    if (!fname)
    {
        return hfResult(results, fname.error());
    }
    return returnResult(rt, params, results, hf->getTxField(*fname), index);
}

wasm_trap_t*
getCurrentLedgerObjField_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* rt = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const fname = getData<SFieldCRef>(rt, params, index);
    if (!fname)
    {
        return hfResult(results, fname.error());
    }

    return returnResult(
        rt, params, results, hf->getCurrentLedgerObjField(*fname), index);
}

wasm_trap_t*
getLedgerObjField_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* rt = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const cache = getData<int32_t>(rt, params, index);
    if (!cache)
    {
        return hfResult(results, cache.error());
    }

    auto const fname = getData<SFieldCRef>(rt, params, index);
    if (!fname)
    {
        return hfResult(results, fname.error());
    }

    return returnResult(
        rt, params, results, hf->getLedgerObjField(*cache, *fname), index);
}

wasm_trap_t*
getTxNestedField_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* rt = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const bytes = getData<Slice>(rt, params, index);
    if (!bytes)
    {
        return hfResult(results, bytes.error());
    }

    return returnResult(
        rt, params, results, hf->getTxNestedField(*bytes), index);
}

wasm_trap_t*
getCurrentLedgerObjNestedField_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* rt = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const bytes = getData<Slice>(rt, params, index);
    if (!bytes)
    {
        return hfResult(results, bytes.error());
    }
    return returnResult(
        rt, params, results, hf->getCurrentLedgerObjNestedField(*bytes), index);
}

wasm_trap_t*
getLedgerObjNestedField_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* rt = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const cache = getData<int32_t>(rt, params, index);
    if (!cache)
    {
        return hfResult(results, cache.error());
    }

    auto const bytes = getData<Slice>(rt, params, index);
    if (!bytes)
    {
        return hfResult(results, bytes.error());
    }

    return returnResult(
        rt,
        params,
        results,
        hf->getLedgerObjNestedField(*cache, *bytes),
        index);
}

wasm_trap_t*
getTxArrayLen_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* rt = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const fname = getData<SFieldCRef>(rt, params, index);
    if (!fname)
    {
        return hfResult(results, fname.error());
    }

    return returnResult(rt, params, results, hf->getTxArrayLen(*fname), index);
}

wasm_trap_t*
getCurrentLedgerObjArrayLen_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* rt = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const fname = getData<SFieldCRef>(rt, params, index);
    if (!fname)
    {
        return hfResult(results, fname.error());
    }

    return returnResult(
        rt, params, results, hf->getCurrentLedgerObjArrayLen(*fname), index);
}

wasm_trap_t*
getLedgerObjArrayLen_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* rt = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const cache = getData<int32_t>(rt, params, index);
    if (!cache)
    {
        return hfResult(results, cache.error());
    }

    auto const fname = getData<SFieldCRef>(rt, params, index);
    if (!fname)
    {
        return hfResult(results, fname.error());
    }

    return returnResult(
        rt, params, results, hf->getLedgerObjArrayLen(*cache, *fname), index);
}

wasm_trap_t*
getTxNestedArrayLen_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* rt = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const bytes = getData<Slice>(rt, params, index);
    if (!bytes)
    {
        return hfResult(results, bytes.error());
    }

    return returnResult(
        rt, params, results, hf->getTxNestedArrayLen(*bytes), index);
}

wasm_trap_t*
getCurrentLedgerObjNestedArrayLen_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* rt = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const bytes = getData<Slice>(rt, params, index);
    if (!bytes)
    {
        return hfResult(results, bytes.error());
    }

    return returnResult(
        rt,
        params,
        results,
        hf->getCurrentLedgerObjNestedArrayLen(*bytes),
        index);
}
wasm_trap_t*
getLedgerObjNestedArrayLen_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* rt = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const cache = getData<int32_t>(rt, params, index);
    if (!cache)
    {
        return hfResult(results, cache.error());
    }

    auto const bytes = getData<Slice>(rt, params, index);
    if (!bytes)
    {
        return hfResult(results, bytes.error());
    }
    return returnResult(
        rt,
        params,
        results,
        hf->getLedgerObjNestedArrayLen(*cache, *bytes),
        index);
}

wasm_trap_t*
updateData_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* rt = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const bytes = getData<Slice>(rt, params, index);
    if (!bytes)
    {
        return hfResult(results, bytes.error());
    }

    return returnResult(rt, params, results, hf->updateData(*bytes), index);
}

wasm_trap_t*
computeSha512HalfHash_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* rt = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const bytes = getData<Slice>(rt, params, index);
    if (!bytes)
    {
        return hfResult(results, bytes.error());
    }
    return returnResult(
        rt, params, results, hf->computeSha512HalfHash(*bytes), index);
}

wasm_trap_t*
accountKeylet_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* rt = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const acc = getData<AccountID>(rt, params, index);
    if (!acc)
    {
        return hfResult(results, acc.error());
    }

    return returnResult(rt, params, results, hf->accountKeylet(*acc), index);
}

wasm_trap_t*
credentialKeylet_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* rt = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const subj = getData<AccountID>(rt, params, index);
    if (!subj)
    {
        return hfResult(results, subj.error());
    }

    auto const iss = getData<AccountID>(rt, params, index);
    if (!iss)
    {
        return hfResult(results, iss.error());
    }

    auto const credType = getData<Slice>(rt, params, index);
    if (!credType)
    {
        return hfResult(results, credType.error());
    }

    return returnResult(
        rt,
        params,
        results,
        hf->credentialKeylet(*subj, *iss, *credType),
        index);
}

wasm_trap_t*
escrowKeylet_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* rt = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const acc = getData<AccountID>(rt, params, index);
    if (!acc)
    {
        return hfResult(results, acc.error());
    }

    auto const seq = getData<int32_t>(rt, params, index);
    if (!seq)
    {
        return hfResult(results, seq.error());
    }

    return returnResult(
        rt, params, results, hf->escrowKeylet(*acc, *seq), index);
}

wasm_trap_t*
oracleKeylet_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* rt = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const acc = getData<AccountID>(rt, params, index);
    if (!acc)
    {
        return hfResult(results, acc.error());
    }

    auto const seq = getData<int32_t>(rt, params, index);
    if (!seq)
    {
        return hfResult(results, seq.error());
    }
    return returnResult(
        rt, params, results, hf->oracleKeylet(*acc, *seq), index);
}

wasm_trap_t*
getNFT_wrap(void* env, wasm_val_vec_t const* params, wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* rt = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const acc = getData<AccountID>(rt, params, index);
    if (!acc)
    {
        return hfResult(results, acc.error());
    }

    auto const nftId = getData<uint256>(rt, params, index);
    if (!nftId)
    {
        return hfResult(results, nftId.error());
    }

    return returnResult(rt, params, results, hf->getNFT(*acc, *nftId), index);
}

wasm_trap_t*
trace_wrap(void* env, wasm_val_vec_t const* params, wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* rt = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const msg = getData<std::string_view>(rt, params, index);
    if (!msg)
    {
        return hfResult(results, msg.error());
    }

    auto const data = getData<Slice>(rt, params, index);
    if (!data)
    {
        return hfResult(results, data.error());
    }

    auto const asHex = getData<int32_t>(rt, params, index);
    if (!asHex)
    {
        return hfResult(results, asHex.error());
    }

    return returnResult(
        rt, params, results, hf->trace(*msg, *data, *asHex), index);
}

wasm_trap_t*
traceNum_wrap(void* env, wasm_val_vec_t const* params, wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* rt = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const msg = getData<std::string_view>(rt, params, index);
    if (!msg)
    {
        return hfResult(results, msg.error());
    }

    auto const number = getData<int64_t>(rt, params, index);
    if (!number)
    {
        return hfResult(results, number.error());
    }

    return returnResult(
        rt, params, results, hf->traceNum(*msg, *number), index);
}

}  // namespace ripple
