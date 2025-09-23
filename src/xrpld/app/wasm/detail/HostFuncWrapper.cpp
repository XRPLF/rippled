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

#include <xrpld/app/wasm/HostFunc.h>
#include <xrpld/app/wasm/HostFuncWrapper.h>
#include <xrpld/app/wasm/WamrVM.h>

namespace ripple {

inline static int32_t
HfErrorToInt(HostFunctionError e)
{
    return static_cast<int32_t>(e);
}

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
        return HfErrorToInt(HostFunctionError::INVALID_PARAMS);

    auto memory = runtime ? runtime->getMem() : wmem();

    // LCOV_EXCL_START
    if (!memory.s)
        return HfErrorToInt(HostFunctionError::NO_MEM_EXPORTED);
    // LCOV_EXCL_STOP
    if (dst + dstSize > memory.s)
        return HfErrorToInt(HostFunctionError::POINTER_OUT_OF_BOUNDS);
    if (srcSize > dstSize)
        return HfErrorToInt(HostFunctionError::BUFFER_TOO_SMALL);

    memcpy(memory.p + dst, src, srcSize);

    return srcSize;
}

template <class IW>
Expected<int32_t, HostFunctionError>
getDataInt32(IW const* _runtime, wasm_val_vec_t const* params, int32_t& i)
{
    auto const result = params->data[i].of.i32;
    i++;
    return result;
}

template <class IW>
Expected<int64_t, HostFunctionError>
getDataInt64(IW const* _runtime, wasm_val_vec_t const* params, int32_t& i)
{
    auto const result = params->data[i].of.i64;
    i++;
    return result;
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
    results->data[0] = WASM_I32_VAL(HfErrorToInt(value));
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

}  // namespace ripple
