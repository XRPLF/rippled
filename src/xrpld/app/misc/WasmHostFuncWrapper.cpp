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

#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/digest.h>

namespace ripple {

using SFieldCRef = std::reference_wrapper<SField const>;

static int32_t
setData(
    InstanceWrapper const* runtime,
    int32_t dst,
    int32_t dstSize,
    uint8_t const* src,
    int32_t srcSize)
{
    if (!srcSize)
        return 0;  // LCOV_EXCL_LINE

    if (dst < 0 || dstSize < 0 || !src || srcSize < 0)
        return static_cast<std::underlying_type_t<HostFunctionError>>(
            HostFunctionError::INVALID_PARAMS);

    auto memory = runtime ? runtime->getMem() : wmem();

    // LCOV_EXCL_START
    if (!memory.s)
        return static_cast<std::underlying_type_t<HostFunctionError>>(
            HostFunctionError::NO_MEM_EXPORTED);
    // LCOV_EXCL_STOP
    if (dst + dstSize > memory.s)
        return static_cast<std::underlying_type_t<HostFunctionError>>(
            HostFunctionError::POINTER_OUT_OF_BOUNDS);
    if (srcSize > dstSize)
        return static_cast<std::underlying_type_t<HostFunctionError>>(
            HostFunctionError::BUFFER_TOO_SMALL);

    memcpy(memory.p + dst, src, srcSize);

    return srcSize;
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
Expected<uint64_t, HostFunctionError>
getDataUint64(IW const* rt, wasm_val_vec_t const* params, int32_t& i)
{
    auto const r = getDataSlice(rt, params, i);
    if (!r)
        return Unexpected(r.error());
    if (r->size() != sizeof(uint64_t))
        return Unexpected(HostFunctionError::INVALID_PARAMS);

    return *reinterpret_cast<uint64_t const*>(r->data());
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
getDataSlice(IW const* runtime, wasm_val_vec_t const* params, int32_t& i)
{
    auto const ptr = params->data[i].of.i32;
    auto const size = params->data[i + 1].of.i32;
    if (ptr < 0 || size < 0)
        return Unexpected(HostFunctionError::INVALID_PARAMS);

    if (!size)
        return Slice();

    if (size > maxWasmDataLength)
        return Unexpected(HostFunctionError::DATA_FIELD_TOO_LARGE);

    auto memory = runtime ? runtime->getMem() : wmem();
    // LCOV_EXCL_START
    if (!memory.s)
        return Unexpected(HostFunctionError::NO_MEM_EXPORTED);
    // LCOV_EXCL_STOP

    if (ptr + size > memory.s)
        return Unexpected(HostFunctionError::POINTER_OUT_OF_BOUNDS);

    Slice data(memory.p + ptr, size);
    i += 2;
    return data;
}

template <class IW>
Expected<uint256, HostFunctionError>
getDataUInt256(IW const* runtime, wasm_val_vec_t const* params, int32_t& i)
{
    auto const slice = getDataSlice(runtime, params, i);
    if (!slice)
    {
        return Unexpected(slice.error());
    }

    if (slice->size() != uint256::bytes)
    {
        return Unexpected(HostFunctionError::INVALID_PARAMS);
    }
    return uint256::fromVoid(slice->data());
}

template <class IW>
Expected<AccountID, HostFunctionError>
getDataAccountID(IW const* runtime, wasm_val_vec_t const* params, int32_t& i)
{
    auto const slice = getDataSlice(runtime, params, i);
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
getDataCurrency(IW const* runtime, wasm_val_vec_t const* params, int32_t& i)
{
    auto const slice = getDataSlice(runtime, params, i);
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
getDataString(IW const* runtime, wasm_val_vec_t const* params, int32_t& i)
{
    auto const slice = getDataSlice(runtime, params, i);
    if (!slice)
        return Unexpected(slice.error());
    return std::string_view(
        reinterpret_cast<char const*>(slice->data()), slice->size());
}

template <class IW>
Expected<Number, HostFunctionError>
getDataNumber(IW const* rt, wasm_val_vec_t const* params, int32_t& i)
{
    auto const r = getDataSlice(rt, params, i);
    if (!r)
        return Unexpected(r.error());

    try
    {
        SerialIter sit(*r);
        STNumber num(sit, sfNumber);
        return num.value();
    }
    catch (...)
    {
    }
    return Unexpected(HostFunctionError::FLOAT_INPUT_MALFORMED);
}

template <class IW>
Expected<Number::rounding_mode, HostFunctionError>
getDataRM(IW const* rt, wasm_val_vec_t const* params, int32_t& i)
{
    auto const rmw = getDataInt32(rt, params, i);
    if (!rmw)
        return Unexpected(rmw.error());

    if (*rmw < Number::rounding_mode::to_nearest ||
        *rmw > Number::rounding_mode::upward)
        return Unexpected(HostFunctionError::INVALID_PARAMS);

    auto const rm = static_cast<Number::rounding_mode>(*rmw);
    return rm;
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
    InstanceWrapper const* runtime,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results,
    Expected<T, HostFunctionError> const& res,
    int32_t index)
{
    if (!res)
    {
        return hfResult(results, res.error());
    }

    using t = std::decay_t<decltype(*res)>;
    if constexpr (std::is_same_v<t, Bytes>)
    {
        return hfResult(
            results,
            setData(
                runtime,
                params->data[index].of.i32,
                params->data[index + 1].of.i32,
                res->data(),
                res->size()));
    }
    else if constexpr (std::is_same_v<t, Number>)
    {
        STNumber num(sfNumber, *res);
        Serializer msg;
        num.add(msg);
        auto const data = msg.getData();

        return hfResult(
            results,
            setData(
                runtime,
                params->data[index].of.i32,
                params->data[index + 1].of.i32,
                data.data(),
                data.size()));
    }
    else if constexpr (std::is_same_v<t, Hash>)
    {
        return hfResult(
            results,
            setData(
                runtime,
                params->data[index].of.i32,
                params->data[index + 1].of.i32,
                res->data(),
                res->size()));
    }
    else if constexpr (std::is_same_v<t, int32_t>)
    {
        return hfResult(results, res.value());
    }
    else if constexpr (std::is_same_v<t, std::uint32_t>)
    {
        auto const resultValue = res.value();
        return hfResult(
            results,
            setData(
                runtime,
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
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    return returnResult(runtime, params, results, hf->getLedgerSqn(), index);
}

wasm_trap_t*
getParentLedgerTime_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    return returnResult(
        runtime, params, results, hf->getParentLedgerTime(), index);
}

wasm_trap_t*
getParentLedgerHash_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    return returnResult(
        runtime, params, results, hf->getParentLedgerHash(), index);
}

wasm_trap_t*
cacheLedgerObj_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const id = getDataUInt256(runtime, params, index);
    if (!id)
    {
        return hfResult(results, id.error());
    }

    auto const cache = getDataInt32(runtime, params, index);
    if (!cache)
    {
        return hfResult(results, cache.error());  // LCOV_EXCL_LINE
    }

    return returnResult(
        runtime, params, results, hf->cacheLedgerObj(*id, *cache), index);
}

wasm_trap_t*
getTxField_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const fname = getDataSField(runtime, params, index);
    if (!fname)
    {
        return hfResult(results, fname.error());
    }
    return returnResult(
        runtime, params, results, hf->getTxField(*fname), index);
}

wasm_trap_t*
getCurrentLedgerObjField_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const fname = getDataSField(runtime, params, index);
    if (!fname)
    {
        return hfResult(results, fname.error());
    }

    return returnResult(
        runtime, params, results, hf->getCurrentLedgerObjField(*fname), index);
}

wasm_trap_t*
getLedgerObjField_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const cache = getDataInt32(runtime, params, index);
    if (!cache)
    {
        return hfResult(results, cache.error());  // LCOV_EXCL_LINE
    }

    auto const fname = getDataSField(runtime, params, index);
    if (!fname)
    {
        return hfResult(results, fname.error());
    }

    return returnResult(
        runtime, params, results, hf->getLedgerObjField(*cache, *fname), index);
}

wasm_trap_t*
getTxNestedField_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const bytes = getDataSlice(runtime, params, index);
    if (!bytes)
    {
        return hfResult(results, bytes.error());
    }

    return returnResult(
        runtime, params, results, hf->getTxNestedField(*bytes), index);
}

wasm_trap_t*
getCurrentLedgerObjNestedField_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const bytes = getDataSlice(runtime, params, index);
    if (!bytes)
    {
        return hfResult(results, bytes.error());
    }
    return returnResult(
        runtime,
        params,
        results,
        hf->getCurrentLedgerObjNestedField(*bytes),
        index);
}

wasm_trap_t*
getLedgerObjNestedField_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const cache = getDataInt32(runtime, params, index);
    if (!cache)
    {
        return hfResult(results, cache.error());  // LCOV_EXCL_LINE
    }

    auto const bytes = getDataSlice(runtime, params, index);
    if (!bytes)
    {
        return hfResult(results, bytes.error());
    }

    return returnResult(
        runtime,
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
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const fname = getDataSField(runtime, params, index);
    if (!fname)
    {
        return hfResult(results, fname.error());
    }

    return returnResult(
        runtime, params, results, hf->getTxArrayLen(*fname), index);
}

wasm_trap_t*
getCurrentLedgerObjArrayLen_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const fname = getDataSField(runtime, params, index);
    if (!fname)
    {
        return hfResult(results, fname.error());
    }

    return returnResult(
        runtime,
        params,
        results,
        hf->getCurrentLedgerObjArrayLen(*fname),
        index);
}

wasm_trap_t*
getLedgerObjArrayLen_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const cache = getDataInt32(runtime, params, index);
    if (!cache)
    {
        return hfResult(results, cache.error());  // LCOV_EXCL_LINE
    }

    auto const fname = getDataSField(runtime, params, index);
    if (!fname)
    {
        return hfResult(results, fname.error());
    }

    return returnResult(
        runtime,
        params,
        results,
        hf->getLedgerObjArrayLen(*cache, *fname),
        index);
}

wasm_trap_t*
getTxNestedArrayLen_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const bytes = getDataSlice(runtime, params, index);
    if (!bytes)
    {
        return hfResult(results, bytes.error());
    }

    return returnResult(
        runtime, params, results, hf->getTxNestedArrayLen(*bytes), index);
}

wasm_trap_t*
getCurrentLedgerObjNestedArrayLen_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const bytes = getDataSlice(runtime, params, index);
    if (!bytes)
    {
        return hfResult(results, bytes.error());
    }

    return returnResult(
        runtime,
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
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const cache = getDataInt32(runtime, params, index);
    if (!cache)
    {
        return hfResult(results, cache.error());  // LCOV_EXCL_LINE
    }

    auto const bytes = getDataSlice(runtime, params, index);
    if (!bytes)
    {
        return hfResult(results, bytes.error());
    }
    return returnResult(
        runtime,
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
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const bytes = getDataSlice(runtime, params, index);
    if (!bytes)
    {
        return hfResult(results, bytes.error());
    }

    return returnResult(
        runtime, params, results, hf->updateData(*bytes), index);
}

wasm_trap_t*
computeSha512HalfHash_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const bytes = getDataSlice(runtime, params, index);
    if (!bytes)
    {
        return hfResult(results, bytes.error());
    }
    return returnResult(
        runtime, params, results, hf->computeSha512HalfHash(*bytes), index);
}

wasm_trap_t*
accountKeylet_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const acc = getDataAccountID(runtime, params, index);
    if (!acc)
    {
        return hfResult(results, acc.error());
    }

    return returnResult(
        runtime, params, results, hf->accountKeylet(*acc), index);
}

wasm_trap_t*
checkKeylet_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const acc = getDataAccountID(runtime, params, index);
    if (!acc)
    {
        return hfResult(results, acc.error());
    }

    auto const seq = getDataInt32(runtime, params, index);
    if (!seq)
    {
        return hfResult(results, seq.error());  // LCOV_EXCL_LINE
    }

    return returnResult(
        runtime, params, results, hf->checkKeylet(acc.value(), *seq), index);
}

wasm_trap_t*
credentialKeylet_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const subj = getDataAccountID(runtime, params, index);
    if (!subj)
    {
        return hfResult(results, subj.error());
    }

    auto const iss = getDataAccountID(runtime, params, index);
    if (!iss)
    {
        return hfResult(results, iss.error());
    }

    auto const credType = getDataSlice(runtime, params, index);
    if (!credType)
    {
        return hfResult(results, credType.error());
    }

    return returnResult(
        runtime,
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
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const acc = getDataAccountID(runtime, params, index);
    if (!acc)
    {
        return hfResult(results, acc.error());
    }

    auto const authorize = getDataAccountID(runtime, params, index);
    if (!authorize)
    {
        return hfResult(results, authorize.error());
    }

    return returnResult(
        runtime,
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
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const acc = getDataAccountID(runtime, params, index);
    if (!acc)
    {
        return hfResult(results, acc.error());
    }

    auto const authorize = getDataAccountID(runtime, params, index);
    if (!authorize)
    {
        return hfResult(results, authorize.error());
    }

    return returnResult(
        runtime,
        params,
        results,
        hf->depositPreauthKeylet(acc.value(), authorize.value()),
        index);
}

wasm_trap_t*
didKeylet_wrap(void* env, wasm_val_vec_t const* params, wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const acc = getDataAccountID(runtime, params, index);
    if (!acc)
    {
        return hfResult(results, acc.error());
    }

    return returnResult(
        runtime, params, results, hf->didKeylet(acc.value()), index);
}

wasm_trap_t*
escrowKeylet_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const acc = getDataAccountID(runtime, params, index);
    if (!acc)
    {
        return hfResult(results, acc.error());
    }

    auto const seq = getDataInt32(runtime, params, index);
    if (!seq)
    {
        return hfResult(results, seq.error());  // LCOV_EXCL_LINE
    }

    return returnResult(
        runtime, params, results, hf->escrowKeylet(*acc, *seq), index);
}

wasm_trap_t*
lineKeylet_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const acc1 = getDataAccountID(runtime, params, index);
    if (!acc1)
    {
        return hfResult(results, acc1.error());
    }

    auto const acc2 = getDataAccountID(runtime, params, index);
    if (!acc2)
    {
        return hfResult(results, acc2.error());
    }

    auto const currency = getDataCurrency(runtime, params, index);
    if (!currency)
    {
        return hfResult(results, currency.error());
    }

    return returnResult(
        runtime,
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
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const acc = getDataAccountID(runtime, params, index);
    if (!acc)
    {
        return hfResult(results, acc.error());
    }

    auto const seq = getDataInt32(runtime, params, index);
    if (!seq)
    {
        return hfResult(results, seq.error());  // LCOV_EXCL_LINE
    }

    return returnResult(
        runtime,
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
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const acc = getDataAccountID(runtime, params, index);
    if (!acc)
    {
        return hfResult(results, acc.error());
    }

    auto const seq = getDataInt32(runtime, params, index);
    if (!seq)
    {
        return hfResult(results, seq.error());  // LCOV_EXCL_LINE
    }

    return returnResult(
        runtime,
        params,
        results,
        hf->offerKeylet(acc.value(), seq.value()),
        index);
}

wasm_trap_t*
oracleKeylet_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const acc = getDataAccountID(runtime, params, index);
    if (!acc)
    {
        return hfResult(results, acc.error());
    }

    auto const documentId = getDataInt32(runtime, params, index);
    if (!documentId)
    {
        return hfResult(results, documentId.error());  // LCOV_EXCL_LINE
    }
    return returnResult(
        runtime, params, results, hf->oracleKeylet(*acc, *documentId), index);
}

wasm_trap_t*
paychanKeylet_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const acc = getDataAccountID(runtime, params, index);
    if (!acc)
    {
        return hfResult(results, acc.error());
    }

    auto const dest = getDataAccountID(runtime, params, index);
    if (!dest)
    {
        return hfResult(results, dest.error());
    }

    auto const seq = getDataInt32(runtime, params, index);
    if (!seq)
    {
        return hfResult(results, seq.error());  // LCOV_EXCL_LINE
    }

    return returnResult(
        runtime,
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
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const acc = getDataAccountID(runtime, params, index);
    if (!acc)
    {
        return hfResult(results, acc.error());
    }

    return returnResult(
        runtime, params, results, hf->signersKeylet(acc.value()), index);
}

wasm_trap_t*
ticketKeylet_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const acc = getDataAccountID(runtime, params, index);
    if (!acc)
    {
        return hfResult(results, acc.error());
    }

    auto const seq = getDataInt32(runtime, params, index);
    if (!seq)
    {
        return hfResult(results, seq.error());  // LCOV_EXCL_LINE
    }

    return returnResult(
        runtime,
        params,
        results,
        hf->ticketKeylet(acc.value(), seq.value()),
        index);
}

wasm_trap_t*
getNFT_wrap(void* env, wasm_val_vec_t const* params, wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const acc = getDataAccountID(runtime, params, index);
    if (!acc)
    {
        return hfResult(results, acc.error());
    }

    auto const nftId = getDataUInt256(runtime, params, index);
    if (!nftId)
    {
        return hfResult(results, nftId.error());
    }

    return returnResult(
        runtime, params, results, hf->getNFT(*acc, *nftId), index);
}

wasm_trap_t*
trace_wrap(void* env, wasm_val_vec_t const* params, wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    if (params->data[1].of.i32 + params->data[3].of.i32 > maxWasmDataLength)
    {
        return hfResult(results, HostFunctionError::DATA_FIELD_TOO_LARGE);
    }

    auto const msg = getDataString(runtime, params, index);
    if (!msg)
    {
        return hfResult(results, msg.error());
    }

    auto const data = getDataSlice(runtime, params, index);
    if (!data)
    {
        return hfResult(results, data.error());
    }

    auto const asHex = getDataInt32(runtime, params, index);
    if (!asHex)
    {
        return hfResult(results, asHex.error());  // LCOV_EXCL_LINE
    }

    return returnResult(
        runtime, params, results, hf->trace(*msg, *data, *asHex), index);
}

wasm_trap_t*
traceNum_wrap(void* env, wasm_val_vec_t const* params, wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;
    if (params->data[1].of.i32 > maxWasmDataLength)
    {
        return hfResult(results, HostFunctionError::DATA_FIELD_TOO_LARGE);
    }

    auto const msg = getDataString(runtime, params, index);
    if (!msg)
    {
        return hfResult(results, msg.error());
    }

    auto const number = getDataInt64(runtime, params, index);
    if (!number)
    {
        return hfResult(results, number.error());  // LCOV_EXCL_LINE
    }

    return returnResult(
        runtime, params, results, hf->traceNum(*msg, *number), index);
}

wasm_trap_t*
traceFloat_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* rt = reinterpret_cast<InstanceWrapper const*>(hf->getRT());

    if (params->data[1].of.i32 > maxWasmDataLength)
        return hfResult(results, HostFunctionError::DATA_FIELD_TOO_LARGE);

    int index = 0;
    auto const msg = getDataString(rt, params, index);
    if (!msg)
        return hfResult(results, msg.error());

    auto const number = getDataNumber(rt, params, index);
    if (!number)
        return hfResult(results, number.error());

    return returnResult(
        rt, params, results, hf->traceFloat(*msg, *number), index);
}

wasm_trap_t*
floatFromInt_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* rt = reinterpret_cast<InstanceWrapper const*>(hf->getRT());

    int i = 0;
    auto const x = getDataInt64(rt, params, i);
    if (!x)
        return hfResult(results, x.error());

    i = 3;
    auto const rmw = getDataRM(rt, params, i);
    if (!rmw)
        return hfResult(results, rmw.error());

    i = 1;
    return returnResult(rt, params, results, hf->floatFromInt(*x, *rmw), i);
}

wasm_trap_t*
floatFromUint_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* rt = reinterpret_cast<InstanceWrapper const*>(hf->getRT());

    int i = 0;
    auto const x = getDataUint64(rt, params, i);
    if (!x)
        return hfResult(results, x.error());

    i = 4;
    auto const rmw = getDataRM(rt, params, i);
    if (!rmw)
        return hfResult(results, rmw.error());

    i = 2;
    return returnResult(rt, params, results, hf->floatFromUint(*x, *rmw), i);
}

wasm_trap_t*
floatSet_wrap(void* env, wasm_val_vec_t const* params, wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* rt = reinterpret_cast<InstanceWrapper const*>(hf->getRT());

    int i = 0;
    auto const e = getDataInt32(rt, params, i);
    if (!e)
        return hfResult(results, e.error());

    auto const m = getDataInt64(rt, params, i);
    if (!m)
        return hfResult(results, m.error());

    i = 4;
    auto const rmw = getDataRM(rt, params, i);
    if (!rmw)
        return hfResult(results, rmw.error());

    i = 2;
    return returnResult(rt, params, results, hf->floatSet(*m, *e, *rmw), i);
}

wasm_trap_t*
floatCompare_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* rt = reinterpret_cast<InstanceWrapper const*>(hf->getRT());

    int i = 0;
    auto const x = getDataNumber(rt, params, i);
    if (!x)
        return hfResult(results, x.error());

    auto const y = getDataNumber(rt, params, i);
    if (!y)
        return hfResult(results, y.error());

    return returnResult(rt, params, results, hf->floatCompare(*x, *y), i);
}

wasm_trap_t*
floatAdd_wrap(void* env, wasm_val_vec_t const* params, wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* rt = reinterpret_cast<InstanceWrapper const*>(hf->getRT());

    int i = 0;
    auto const x = getDataNumber(rt, params, i);
    if (!x)
        return hfResult(results, x.error());

    auto const y = getDataNumber(rt, params, i);
    if (!y)
        return hfResult(results, y.error());

    i = 6;
    auto const rmw = getDataRM(rt, params, i);
    if (!rmw)
        return hfResult(results, rmw.error());

    i = 4;
    return returnResult(rt, params, results, hf->floatAdd(*x, *y, *rmw), i);
}

wasm_trap_t*
floatSubtract_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* rt = reinterpret_cast<InstanceWrapper const*>(hf->getRT());

    int i = 0;
    auto const x = getDataNumber(rt, params, i);
    if (!x)
        return hfResult(results, x.error());

    auto const y = getDataNumber(rt, params, i);
    if (!y)
        return hfResult(results, y.error());

    i = 6;
    auto const rmw = getDataRM(rt, params, i);
    if (!rmw)
        return hfResult(results, rmw.error());

    i = 4;
    return returnResult(
        rt, params, results, hf->floatSubtract(*x, *y, *rmw), i);
}

wasm_trap_t*
floatMultiply_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* rt = reinterpret_cast<InstanceWrapper const*>(hf->getRT());

    int i = 0;
    auto const x = getDataNumber(rt, params, i);
    if (!x)
        return hfResult(results, x.error());

    auto const y = getDataNumber(rt, params, i);
    if (!y)
        return hfResult(results, y.error());

    i = 6;
    auto const rmw = getDataRM(rt, params, i);
    if (!rmw)
        return hfResult(results, rmw.error());

    i = 4;
    return returnResult(
        rt, params, results, hf->floatMultiply(*x, *y, *rmw), i);
}

wasm_trap_t*
floatDivide_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* rt = reinterpret_cast<InstanceWrapper const*>(hf->getRT());

    int i = 0;
    auto const x = getDataNumber(rt, params, i);
    if (!x)
        return hfResult(results, x.error());

    auto const y = getDataNumber(rt, params, i);
    if (!y)
        return hfResult(results, y.error());

    i = 6;
    auto const rmw = getDataRM(rt, params, i);
    if (!rmw)
        return hfResult(results, rmw.error());

    i = 4;
    return returnResult(rt, params, results, hf->floatDivide(*x, *y, *rmw), i);
}

wasm_trap_t*
floatRoot_wrap(void* env, wasm_val_vec_t const* params, wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* rt = reinterpret_cast<InstanceWrapper const*>(hf->getRT());

    int i = 0;
    auto const x = getDataNumber(rt, params, i);
    if (!x)
        return hfResult(results, x.error());

    auto const n = getDataInt32(rt, params, i);
    if (!n)
        return hfResult(results, n.error());

    i = 5;
    auto const rmw = getDataRM(rt, params, i);
    if (!rmw)
        return hfResult(results, rmw.error());

    i = 3;
    return returnResult(rt, params, results, hf->floatRoot(*x, *n, *rmw), i);
}

wasm_trap_t*
floatPower_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* rt = reinterpret_cast<InstanceWrapper const*>(hf->getRT());

    int i = 0;
    auto const x = getDataNumber(rt, params, i);
    if (!x)
        return hfResult(results, x.error());

    auto const n = getDataInt32(rt, params, i);
    if (!n)
        return hfResult(results, n.error());

    i = 5;
    auto const rmw = getDataRM(rt, params, i);
    if (!rmw)
        return hfResult(results, rmw.error());

    i = 3;
    return returnResult(rt, params, results, hf->floatPower(*x, *n, *rmw), i);
}

wasm_trap_t*
floatLog_wrap(void* env, wasm_val_vec_t const* params, wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* rt = reinterpret_cast<InstanceWrapper const*>(hf->getRT());

    int i = 0;
    auto const x = getDataNumber(rt, params, i);
    if (!x)
        return hfResult(results, x.error());

    i = 4;
    auto const rmw = getDataRM(rt, params, i);
    if (!rmw)
        return hfResult(results, rmw.error());

    i = 2;
    return returnResult(rt, params, results, hf->floatLog(*x, *rmw), i);
}

namespace test {

class MockInstanceWrapper
{
    wmem mem_;

public:
    MockInstanceWrapper(wmem memory) : mem_(memory)
    {
    }

    // Mock methods to simulate the behavior of InstanceWrapper
    wmem
    getMem() const
    {
        return mem_;
    }
};


// LCOV_EXCL_START
bool
testGetDataIncrement()
{
    wasm_val_t values[4];

    std::array<std::uint8_t, 128> buffer = {
        'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h'};
    MockInstanceWrapper runtime(wmem{buffer.data(), buffer.size()});

    {
        // test int32_t
        wasm_val_vec_t params = {1, &values[0], 1, sizeof(wasm_val_t), nullptr};

        values[0] = WASM_I32_VAL(42);

        int index = 0;
        auto const result = getDataInt32(&runtime, &params, index);
        if (!result || result.value() != 42 || index != 1)
            return false;
    }

    {
        // test int64_t
        wasm_val_vec_t params = {1, &values[0], 1, sizeof(wasm_val_t), nullptr};

        values[0] = WASM_I64_VAL(1234);

        int index = 0;
        auto const result = getDataInt64(&runtime, &params, index);
        if (!result || result.value() != 1234 || index != 1)
            return false;
    }

    {
        // test SFieldCRef
        wasm_val_vec_t params = {1, &values[0], 1, sizeof(wasm_val_t), nullptr};

        values[0] = WASM_I32_VAL(sfAccount.fieldCode);

        int index = 0;
        auto const result = getDataSField(&runtime, &params, index);
        if (!result || result.value().get() != sfAccount || index != 1)
            return false;
    }

    {
        // test Slice
        wasm_val_vec_t params = {2, &values[0], 2, sizeof(wasm_val_t), nullptr};

        values[0] = WASM_I32_VAL(0);
        values[1] = WASM_I32_VAL(3);

        int index = 0;
        auto const result = getDataSlice(&runtime, &params, index);
        if (!result || result.value() != Slice(buffer.data(), 3) || index != 2)
            return false;
    }

    {
        // test string
        wasm_val_vec_t params = {2, &values[0], 2, sizeof(wasm_val_t), nullptr};

        values[0] = WASM_I32_VAL(0);
        values[1] = WASM_I32_VAL(5);

        int index = 0;
        auto const result = getDataString(&runtime, &params, index);
        if (!result ||
            result.value() !=
                std::string_view(
                    reinterpret_cast<char const*>(buffer.data()), 5) ||
            index != 2)
            return false;
    }

    {
        // test account
        AccountID const id(calcAccountID(
            generateKeyPair(KeyType::secp256k1, generateSeed("alice")).first));

        wasm_val_vec_t params = {2, &values[0], 2, sizeof(wasm_val_t), nullptr};

        values[0] = WASM_I32_VAL(0);
        values[1] = WASM_I32_VAL(id.bytes);
        memcpy(&buffer[0], id.data(), id.bytes);

        int index = 0;
        auto const result = getDataAccountID(&runtime, &params, index);
        if (!result || result.value() != id || index != 2)
            return false;
    }

    {
        // test uint256

        Hash h1 = sha512Half(Slice(buffer.data(), 8));
        wasm_val_vec_t params = {2, &values[0], 2, sizeof(wasm_val_t), nullptr};

        values[0] = WASM_I32_VAL(0);
        values[1] = WASM_I32_VAL(h1.bytes);
        memcpy(&buffer[0], h1.data(), h1.bytes);

        int index = 0;
        auto const result = getDataUInt256(&runtime, &params, index);
        if (!result || result.value() != h1 || index != 2)
            return false;
    }

    {
        // test Currency

        Currency const c = xrpCurrency();
        wasm_val_vec_t params = {2, &values[0], 2, sizeof(wasm_val_t), nullptr};

        values[0] = WASM_I32_VAL(0);
        values[1] = WASM_I32_VAL(c.bytes);
        memcpy(&buffer[0], c.data(), c.bytes);

        int index = 0;
        auto const result = getDataCurrency(&runtime, &params, index);
        if (!result || result.value() != c || index != 2)
            return false;
    }

    return true;
}
// LCOV_EXCL_STOP

}  // namespace test
}  // namespace ripple
