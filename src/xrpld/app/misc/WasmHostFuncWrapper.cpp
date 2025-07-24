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
        return 0;  // LCOV_EXCL_LINE

    if (dst < 0 || dsz < 0 || !src || ssz < 0)
        return static_cast<std::underlying_type_t<HostFunctionError>>(
            HostFunctionError::INVALID_PARAMS);

    auto mem = rt ? rt->getMem() : wmem();

    // LCOV_EXCL_START
    if (!mem.s)
    {
        return static_cast<std::underlying_type_t<HostFunctionError>>(
            HostFunctionError::NO_MEM_EXPORTED);
    }
    // LCOV_EXCL_STOP
    if (dst + dsz > mem.s)
        return static_cast<std::underlying_type_t<HostFunctionError>>(
            HostFunctionError::POINTER_OUT_OF_BOUNDS);
    if (ssz > dsz)
        return static_cast<std::underlying_type_t<HostFunctionError>>(
            HostFunctionError::BUFFER_TOO_SMALL);

    memcpy(mem.p + dst, src, ssz);

    return ssz;
}

template <class IW>
Expected<int32_t, HostFunctionError>
getDataInt32(IW const* _rt, wasm_val_vec_t const* params, int32_t& i)
{
    auto const result = params->data[i].of.i32;
    i++;
    return result;
}

template <class IW>
Expected<int64_t, HostFunctionError>
getDataInt64(IW const* _rt, wasm_val_vec_t const* params, int32_t& i)
{
    auto const result = params->data[i].of.i64;
    i++;
    return result;
}

template <class IW>
Expected<SFieldCRef, HostFunctionError>
getDataSField(IW const* _rt, wasm_val_vec_t const* params, int32_t& i)
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

template <class IW>
Expected<Slice, HostFunctionError>
getDataSlice(IW const* rt, wasm_val_vec_t const* params, int32_t& i)
{
    auto const src = params->data[i].of.i32;
    auto const ssz = params->data[i + 1].of.i32;
    if (src < 0 || ssz < 0)
        return Unexpected(HostFunctionError::INVALID_PARAMS);

    if (!ssz)
        return Slice();

    if (ssz > maxWasmDataLength)
        return Unexpected(HostFunctionError::DATA_FIELD_TOO_LARGE);

    auto mem = rt ? rt->getMem() : wmem();
    // LCOV_EXCL_START
    if (!mem.s)
        return Unexpected(HostFunctionError::NO_MEM_EXPORTED);
    // LCOV_EXCL_STOP

    if (src + ssz > mem.s)
        return Unexpected(HostFunctionError::POINTER_OUT_OF_BOUNDS);

    Slice data(mem.p + src, ssz);
    i += 2;
    return data;
}

template <class IW>
Expected<uint256, HostFunctionError>
getDataUInt256(IW const* rt, wasm_val_vec_t const* params, int32_t& i)
{
    auto const r = getDataSlice(rt, params, i);
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

template <class IW>
Expected<AccountID, HostFunctionError>
getDataAccountID(IW const* rt, wasm_val_vec_t const* params, int32_t& i)
{
    auto const slice = getDataSlice(rt, params, i);
    if (!slice)
    {
        return Unexpected(slice.error());
    }

    if (slice->size() != AccountID::bytes)
    {
        return Unexpected(HostFunctionError::INVALID_PARAMS);
    }

    return AccountID::fromVoid(slice->data());
}

template <class IW>
static Expected<Currency, HostFunctionError>
getDataCurrency(IW const* rt, wasm_val_vec_t const* params, int32_t& i)
{
    auto const slice = getDataSlice(rt, params, i);
    if (!slice)
    {
        return Unexpected(slice.error());
    }

    if (slice->size() != Currency::bytes)
    {
        return Unexpected(HostFunctionError::INVALID_PARAMS);
    }

    return Currency::fromVoid(slice->data());
}

template <class IW>
Expected<std::string_view, HostFunctionError>
getDataString(IW const* rt, wasm_val_vec_t const* params, int32_t& i)
{
    auto const r = getDataSlice(rt, params, i);
    if (!r)
        return Unexpected(r.error());
    return std::string_view(
        reinterpret_cast<char const*>(r->data()), r->size());
}

std::nullptr_t
hfResult(wasm_val_vec_t* results, int32_t value)
{
    results->data[0] = WASM_I32_VAL(value);
    results->num_elems = 1;
    return nullptr;
}

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
    Expected<T, HostFunctionError> const& res,
    int32_t index)
{
    if (!res)
    {
        return hfResult(results, res.error());
    }
    if constexpr (std::is_same_v<std::decay_t<decltype(*res)>, Bytes>)
    {
        return hfResult(
            results,
            setData(
                rt,
                params->data[index].of.i32,
                params->data[index + 1].of.i32,
                res->data(),
                res->size()));
    }
    else if constexpr (std::is_same_v<std::decay_t<decltype(*res)>, Hash>)
    {
        return hfResult(
            results,
            setData(
                rt,
                params->data[index].of.i32,
                params->data[index + 1].of.i32,
                res->data(),
                res->size()));
    }
    else if constexpr (std::is_same_v<std::decay_t<decltype(*res)>, int32_t>)
    {
        return hfResult(results, res.value());
    }
    else if constexpr (std::is_same_v<
                           std::decay_t<decltype(*res)>,
                           std::uint32_t>)
    {
        auto const resultValue = res.value();
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

    auto const id = getDataUInt256(rt, params, index);
    if (!id)
    {
        return hfResult(results, id.error());
    }

    auto const cache = getDataInt32(rt, params, index);
    if (!cache)
    {
        return hfResult(results, cache.error());  // LCOV_EXCL_LINE
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

    auto const fname = getDataSField(rt, params, index);
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

    auto const fname = getDataSField(rt, params, index);
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

    auto const cache = getDataInt32(rt, params, index);
    if (!cache)
    {
        return hfResult(results, cache.error());  // LCOV_EXCL_LINE
    }

    auto const fname = getDataSField(rt, params, index);
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

    auto const bytes = getDataSlice(rt, params, index);
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

    auto const bytes = getDataSlice(rt, params, index);
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

    auto const cache = getDataInt32(rt, params, index);
    if (!cache)
    {
        return hfResult(results, cache.error());  // LCOV_EXCL_LINE
    }

    auto const bytes = getDataSlice(rt, params, index);
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

    auto const fname = getDataSField(rt, params, index);
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

    auto const fname = getDataSField(rt, params, index);
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

    auto const cache = getDataInt32(rt, params, index);
    if (!cache)
    {
        return hfResult(results, cache.error());  // LCOV_EXCL_LINE
    }

    auto const fname = getDataSField(rt, params, index);
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

    auto const bytes = getDataSlice(rt, params, index);
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

    auto const bytes = getDataSlice(rt, params, index);
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

    auto const cache = getDataInt32(rt, params, index);
    if (!cache)
    {
        return hfResult(results, cache.error());  // LCOV_EXCL_LINE
    }

    auto const bytes = getDataSlice(rt, params, index);
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

    auto const bytes = getDataSlice(rt, params, index);
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

    auto const bytes = getDataSlice(rt, params, index);
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

    auto const acc = getDataAccountID(rt, params, index);
    if (!acc)
    {
        return hfResult(results, acc.error());
    }

    return returnResult(rt, params, results, hf->accountKeylet(*acc), index);
}

wasm_trap_t*
checkKeylet_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* rt = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const acc = getDataAccountID(rt, params, index);
    if (!acc)
    {
        return hfResult(results, acc.error());
    }

    auto const seq = getDataInt32(rt, params, index);
    if (!seq)
    {
        return hfResult(results, seq.error());  // LCOV_EXCL_LINE
    }

    return returnResult(
        rt, params, results, hf->checkKeylet(acc.value(), *seq), index);
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

    auto const subj = getDataAccountID(rt, params, index);
    if (!subj)
    {
        return hfResult(results, subj.error());
    }

    auto const iss = getDataAccountID(rt, params, index);
    if (!iss)
    {
        return hfResult(results, iss.error());
    }

    auto const credType = getDataSlice(rt, params, index);
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
delegateKeylet_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* rt = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const acc = getDataAccountID(rt, params, index);
    if (!acc)
    {
        return hfResult(results, acc.error());
    }

    auto const authorize = getDataAccountID(rt, params, index);
    if (!authorize)
    {
        return hfResult(results, authorize.error());
    }

    return returnResult(
        rt,
        params,
        results,
        hf->delegateKeylet(acc.value(), authorize.value()),
        index);
}

wasm_trap_t*
depositPreauthKeylet_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* rt = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const acc = getDataAccountID(rt, params, index);
    if (!acc)
    {
        return hfResult(results, acc.error());
    }

    auto const authorize = getDataAccountID(rt, params, index);
    if (!authorize)
    {
        return hfResult(results, authorize.error());
    }

    return returnResult(
        rt,
        params,
        results,
        hf->depositPreauthKeylet(acc.value(), authorize.value()),
        index);
}

wasm_trap_t*
didKeylet_wrap(void* env, wasm_val_vec_t const* params, wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* rt = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const acc = getDataAccountID(rt, params, index);
    if (!acc)
    {
        return hfResult(results, acc.error());
    }

    return returnResult(rt, params, results, hf->didKeylet(acc.value()), index);
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

    auto const acc = getDataAccountID(rt, params, index);
    if (!acc)
    {
        return hfResult(results, acc.error());
    }

    auto const seq = getDataInt32(rt, params, index);
    if (!seq)
    {
        return hfResult(results, seq.error());  // LCOV_EXCL_LINE
    }

    return returnResult(
        rt, params, results, hf->escrowKeylet(*acc, *seq), index);
}

wasm_trap_t*
lineKeylet_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* rt = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const acc1 = getDataAccountID(rt, params, index);
    if (!acc1)
    {
        return hfResult(results, acc1.error());
    }

    auto const acc2 = getDataAccountID(rt, params, index);
    if (!acc2)
    {
        return hfResult(results, acc2.error());
    }

    auto const currency = getDataCurrency(rt, params, index);
    if (!currency)
    {
        return hfResult(results, currency.error());
    }

    return returnResult(
        rt,
        params,
        results,
        hf->lineKeylet(acc1.value(), acc2.value(), currency.value()),
        index);
}

wasm_trap_t*
nftOfferKeylet_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* rt = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const acc = getDataAccountID(rt, params, index);
    if (!acc)
    {
        return hfResult(results, acc.error());
    }

    auto const seq = getDataInt32(rt, params, index);
    if (!seq)
    {
        return hfResult(results, seq.error());  // LCOV_EXCL_LINE
    }

    return returnResult(
        rt,
        params,
        results,
        hf->nftOfferKeylet(acc.value(), seq.value()),
        index);
}

wasm_trap_t*
offerKeylet_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* rt = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const acc = getDataAccountID(rt, params, index);
    if (!acc)
    {
        return hfResult(results, acc.error());
    }

    auto const seq = getDataInt32(rt, params, index);
    if (!seq)
    {
        return hfResult(results, seq.error());  // LCOV_EXCL_LINE
    }

    return returnResult(
        rt, params, results, hf->offerKeylet(acc.value(), seq.value()), index);
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

    auto const acc = getDataAccountID(rt, params, index);
    if (!acc)
    {
        return hfResult(results, acc.error());
    }

    auto const documentId = getDataInt32(rt, params, index);
    if (!documentId)
    {
        return hfResult(results, documentId.error());  // LCOV_EXCL_LINE
    }
    return returnResult(
        rt, params, results, hf->oracleKeylet(*acc, *documentId), index);
}

wasm_trap_t*
paychanKeylet_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* rt = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const acc = getDataAccountID(rt, params, index);
    if (!acc)
    {
        return hfResult(results, acc.error());
    }

    auto const dest = getDataAccountID(rt, params, index);
    if (!dest)
    {
        return hfResult(results, dest.error());
    }

    auto const seq = getDataInt32(rt, params, index);
    if (!seq)
    {
        return hfResult(results, seq.error());  // LCOV_EXCL_LINE
    }

    return returnResult(
        rt,
        params,
        results,
        hf->paychanKeylet(acc.value(), dest.value(), seq.value()),
        index);
}

wasm_trap_t*
signersKeylet_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* rt = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const acc = getDataAccountID(rt, params, index);
    if (!acc)
    {
        return hfResult(results, acc.error());
    }

    return returnResult(
        rt, params, results, hf->signersKeylet(acc.value()), index);
}

wasm_trap_t*
ticketKeylet_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* rt = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const acc = getDataAccountID(rt, params, index);
    if (!acc)
    {
        return hfResult(results, acc.error());
    }

    auto const seq = getDataInt32(rt, params, index);
    if (!seq)
    {
        return hfResult(results, seq.error());  // LCOV_EXCL_LINE
    }

    return returnResult(
        rt, params, results, hf->ticketKeylet(acc.value(), seq.value()), index);
    ;
}

wasm_trap_t*
getNFT_wrap(void* env, wasm_val_vec_t const* params, wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* rt = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const acc = getDataAccountID(rt, params, index);
    if (!acc)
    {
        return hfResult(results, acc.error());
    }

    auto const nftId = getDataUInt256(rt, params, index);
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

    if (params->data[1].of.i32 + params->data[3].of.i32 > maxWasmDataLength)
    {
        return hfResult(results, HostFunctionError::DATA_FIELD_TOO_LARGE);
    }

    auto const msg = getDataString(rt, params, index);
    if (!msg)
    {
        return hfResult(results, msg.error());
    }

    auto const data = getDataSlice(rt, params, index);
    if (!data)
    {
        return hfResult(results, data.error());
    }

    auto const asHex = getDataInt32(rt, params, index);
    if (!asHex)
    {
        return hfResult(results, asHex.error());  // LCOV_EXCL_LINE
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
    if (params->data[1].of.i32 > maxWasmDataLength)
    {
        return hfResult(results, HostFunctionError::DATA_FIELD_TOO_LARGE);
    }

    auto const msg = getDataString(rt, params, index);
    if (!msg)
    {
        return hfResult(results, msg.error());
    }

    auto const number = getDataInt64(rt, params, index);
    if (!number)
    {
        return hfResult(results, number.error());  // LCOV_EXCL_LINE
    }

    return returnResult(
        rt, params, results, hf->traceNum(*msg, *number), index);
}

class MockInstanceWrapper
{
    wmem mem_;

public:
    MockInstanceWrapper(wmem mem) : mem_(mem)
    {
    }

    // Mock methods to simulate the behavior of InstanceWrapper
    wmem
    getMem() const
    {
        return mem_;
    }
};

}  // namespace ripple
