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

#define SFIELD_PARAM std::reference_wrapper<SField const>

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

    auto mem = rt ? rt->getMem() : wmem();

    if (!mem.s)
        return HF_ERR_NO_MEM_EXPORTED;
    if (dst + dsz > mem.s)
        return HF_ERR_OUT_OF_BOUNDS;
    if (ssz > dsz)
        return HF_ERR_BUFFER_TOO_SMALL;

    memcpy(mem.p + dst, src, ssz);

    return ssz;
}

template <typename T>
Expected<T, HostFuncError>
getData(InstanceWrapper const* rt, wasm_val_vec_t const* params, int32_t& src);

template <>
Expected<int32_t, HostFuncError>
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
Expected<int64_t, HostFuncError>
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
Expected<SFIELD_PARAM, HostFuncError>
getData<SFIELD_PARAM>(
    InstanceWrapper const* _rt,
    wasm_val_vec_t const* params,
    int32_t& i)
{
    auto const& m = SField::getKnownCodeToField();
    auto const it = m.find(params->data[i].of.i32);
    if (it == m.end())
    {
        return Unexpected(HF_ERR_FIELD_NOT_FOUND);
    }
    i++;
    return *it->second;
}

template <>
Expected<Bytes, HostFuncError>
getData<Bytes>(
    InstanceWrapper const* rt,
    wasm_val_vec_t const* params,
    int32_t& i)
{
    auto const src = params->data[i].of.i32;
    auto const ssz = params->data[i + 1].of.i32;
    i += 2;
    if (!ssz)
        return Unexpected(HF_ERR_INVALID_PARAMS);

    auto mem = rt ? rt->getMem() : wmem();
    if (!mem.s)
        return Unexpected(HF_ERR_NO_MEM_EXPORTED);

    if (src + ssz > mem.s)
        return Unexpected(HF_ERR_OUT_OF_BOUNDS);

    Bytes data(mem.p + src, mem.p + src + ssz);
    return data;
}

template <>
Expected<uint256, HostFuncError>
getData<uint256>(
    InstanceWrapper const* rt,
    wasm_val_vec_t const* params,
    int32_t& i)
{
    auto const r = getData<Bytes>(rt, params, i);
    if (!r)
    {
        return Unexpected(r.error());
    }

    if (r->size() != uint256::bytes)
    {
        return Unexpected(HF_ERR_INVALID_PARAMS);
    }
    return uint256::fromVoid(r->data());
}

template <>
Expected<AccountID, HostFuncError>
getData<AccountID>(
    InstanceWrapper const* rt,
    wasm_val_vec_t const* params,
    int32_t& i)
{
    auto const r = getData<Bytes>(rt, params, i);
    if (!r || (r->size() < AccountID::bytes))
        return Unexpected(HF_ERR_INVALID_PARAMS);

    return AccountID::fromVoid(r->data());
}

template <>
Expected<std::string, HostFuncError>
getData<std::string>(
    InstanceWrapper const* rt,
    wasm_val_vec_t const* params,
    int32_t& i)
{
    auto const src = params->data[i].of.i32;
    auto const ssz = params->data[i + 1].of.i32;
    i += 2;
    if (!ssz)
        return Unexpected(HF_ERR_INVALID_PARAMS);

    auto mem = rt ? rt->getMem() : wmem();
    if (!mem.s)
        return Unexpected(HF_ERR_NO_MEM_EXPORTED);

    if (src + ssz > mem.s)
        return Unexpected(HF_ERR_OUT_OF_BOUNDS);

    std::string data(mem.p + src, mem.p + src + ssz);
    return data;
}

template <typename T>
nullptr_t
hfResult(wasm_val_vec_t* results, T value)
{
    results->data[0] = WASM_I32_VAL(value);
    results->num_elems = 1;
    return nullptr;
}

template <typename Tuple, std::size_t... Is>
std::optional<HostFuncError>
checkErrors(Tuple const& t, std::index_sequence<Is...>)
{
    std::optional<HostFuncError> err;
    ((err = err ? err
                : (!std::get<Is>(t) ? std::optional{std::get<Is>(t).error()}
                                    : std::nullopt)),
     ...);
    return err;
}

template <typename... Args, typename Func>
wasm_trap_t*
invokeHostFunc(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results,
    Func&& f)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* rt = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto expectedArgs = std::make_tuple(getData<Args>(rt, params, index)...);

    constexpr auto N = sizeof...(Args);
    auto err = checkErrors(expectedArgs, std::make_index_sequence<N>{});

    if (err)
        return hfResult(results, *err);

    auto unwrappedArgs =
        std::apply([](auto&&... e) { return std::tuple{*e...}; }, expectedArgs);

    auto result = std::apply(std::forward<Func>(f), unwrappedArgs);

    if (!result)
        return hfResult(results, result.error());

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
        return hfResult(
            results,
            setData(
                rt,
                params->data[0].of.i32,
                params->data[1].of.i32,
                reinterpret_cast<uint8_t const*>(&result),
                static_cast<int32_t>(sizeof(result))));
    }
    else
    {
        static_assert(
            [] { return false; }(), "Unhandled return type in invokeHostFunc");
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
    return hfResult(results, sqn.value());
}

#define HOST_FUNCTION(funcName, name, return, ...)                        \
    wasm_trap_t* funcName##_wrap(                                         \
        void* env, wasm_val_vec_t const* params, wasm_val_vec_t* results) \
    {                                                                     \
        auto* hf = reinterpret_cast<HostFunctions*>(env);                 \
        auto const* rt =                                                  \
            reinterpret_cast<InstanceWrapper const*>(hf->getRT());        \
    }

wasm_trap_t*
getLedgerSqn_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    return invokeHostFunc(
        env, params, results, [hf = reinterpret_cast<HostFunctions*>(env)]() {
            return hf->getLedgerSqn();
        });
}

wasm_trap_t*
getParentLedgerTime_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    return invokeHostFunc(
        env, params, results, [hf = reinterpret_cast<HostFunctions*>(env)]() {
            return hf->getParentLedgerTime();
        });
}

wasm_trap_t*
getParentLedgerHash_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    return invokeHostFunc(
        env, params, results, [hf = reinterpret_cast<HostFunctions*>(env)]() {
            return hf->getParentLedgerHash();
        });
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

    auto const r = getData<uint256>(rt, params, index);
    if (!r)
    {
        return hfResult(results, r.error());
    }
    auto const cache = getData<int32_t>(rt, params, index);
    if (!cache)
    {
        return hfResult(results, cache.error());
    }
    auto const cacheIndex =
        hf->cacheLedgerObj(keylet::unchecked(r.value()), cache.value());
    if (!cacheIndex)
    {
        return hfResult(results, cacheIndex.error());
    }
    return hfResult(results, cacheIndex.value());
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

    auto const& fname = getData<SFIELD_PARAM>(rt, params, index);
    if (!fname)
    {
        return hfResult(results, fname.error());
    }

    auto fieldData = hf->getTxField(fname.value());
    if (!fieldData)
    {
        return hfResult(results, fieldData.error());
    }

    return hfResult(
        results,
        setData(
            rt,
            params->data[index].of.i32,
            params->data[index + 1].of.i32,
            fieldData->data(),
            fieldData->size()));
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

    auto const& fname = getData<SFIELD_PARAM>(rt, params, index);
    if (!fname)
    {
        return hfResult(results, fname.error());
    }

    auto fieldData = hf->getCurrentLedgerObjField(fname.value());
    if (!fieldData)
    {
        return hfResult(results, fieldData.error());
    }

    return hfResult(
        results,
        setData(
            rt,
            params->data[index].of.i32,
            params->data[index + 1].of.i32,
            fieldData->data(),
            fieldData->size()));
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
    auto const& fname = getData<SFIELD_PARAM>(rt, params, index);
    if (!fname)
    {
        return hfResult(results, fname.error());
    }

    auto fieldData = hf->getLedgerObjField(cache.value(), fname.value());
    if (!fieldData)
    {
        return hfResult(results, fieldData.error());
    }

    return hfResult(
        results,
        setData(
            rt,
            params->data[index].of.i32,
            params->data[index + 1].of.i32,
            fieldData->data(),
            fieldData->size()));
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

    auto const r = getData<Bytes>(rt, params, index);
    if (!r)
    {
        return hfResult(results, r.error());
    }

    auto fieldData = hf->getTxNestedField(r.value());
    if (!fieldData)
    {
        return hfResult(results, fieldData.error());
    }

    return hfResult(
        results,
        setData(
            rt,
            params->data[index].of.i32,
            params->data[index + 1].of.i32,
            fieldData->data(),
            fieldData->size()));
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

    auto const r = getData<Bytes>(rt, params, index);
    if (!r)
    {
        return hfResult(results, r.error());
    }

    auto fieldData = hf->getCurrentLedgerObjNestedField(r.value());
    if (!fieldData)
    {
        return hfResult(results, fieldData.error());
    }

    return hfResult(
        results,
        setData(
            rt,
            params->data[index].of.i32,
            params->data[index + 1].of.i32,
            fieldData->data(),
            fieldData->size()));
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
    auto const r = getData<Bytes>(rt, params, index);
    if (!r)
    {
        return hfResult(results, r.error());
    }

    auto fieldData = hf->getLedgerObjNestedField(cache.value(), r.value());
    if (!fieldData)
    {
        return hfResult(results, fieldData.error());
    }

    return hfResult(
        results,
        setData(
            rt,
            params->data[index].of.i32,
            params->data[index + 1].of.i32,
            fieldData->data(),
            fieldData->size()));
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

    auto const& fname = getData<SFIELD_PARAM>(rt, params, index);
    if (!fname)
    {
        return hfResult(results, fname.error());
    }

    auto const sz = hf->getTxArrayLen(fname.value());
    if (!sz)
    {
        return hfResult(results, sz.error());
    }
    return hfResult(results, sz.value());
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

    auto const& fname = getData<SFIELD_PARAM>(rt, params, index);
    if (!fname)
    {
        return hfResult(results, fname.error());
    }

    auto const sz = hf->getCurrentLedgerObjArrayLen(fname.value());
    if (!sz)
    {
        return hfResult(results, sz.error());
    }
    return hfResult(results, sz.value());
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

    auto const& fname = getData<SFIELD_PARAM>(rt, params, index);
    if (!fname)
    {
        return hfResult(results, fname.error());
    }

    auto const sz = hf->getLedgerObjArrayLen(cache.value(), fname.value());
    if (!sz)
    {
        return hfResult(results, sz.error());
    }
    return hfResult(results, sz.value());
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

    auto const r = getData<Bytes>(rt, params, index);
    if (!r)
    {
        return hfResult(results, r.error());
    }

    auto const sz = hf->getTxNestedArrayLen(r.value());
    if (!sz)
    {
        return hfResult(results, sz.error());
    }
    return hfResult(results, sz.value());
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

    auto const r = getData<Bytes>(rt, params, index);
    if (!r)
    {
        return hfResult(results, r.error());
    }

    auto const sz = hf->getCurrentLedgerObjNestedArrayLen(r.value());
    if (!sz)
    {
        return hfResult(results, sz.error());
    }
    return hfResult(results, sz.value());
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
    auto const r = getData<Bytes>(rt, params, index);
    if (!r)
    {
        return hfResult(results, r.error());
    }

    auto const sz = hf->getLedgerObjNestedArrayLen(cache.value(), r.value());
    if (!sz)
    {
        return hfResult(results, sz.error());
    }
    return hfResult(results, sz.value());
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

    auto const r = getData<Bytes>(rt, params, index);
    if (!r)
    {
        return hfResult(results, r.error());
    }

    if (r->size() > maxWasmDataLength)
    {
        return hfResult(results, HF_ERR_DATA_FIELD_TOO_LARGE);
    }
    auto const result = hf->updateData(r.value());
    if (!result)
    {
        return hfResult(results, result.error());
    }

    return hfResult(results, result.value());
}

wasm_trap_t*
computeSha512HalfHash_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    return invokeHostFunc<Bytes>(
        env,
        params,
        results,
        [hf = reinterpret_cast<HostFunctions*>(env)](Bytes const& bytes) {
            return hf->computeSha512HalfHash(bytes);
        });
}

wasm_trap_t*
accountKeylet_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    return invokeHostFunc<AccountID>(
        env,
        params,
        results,
        [hf = reinterpret_cast<HostFunctions*>(env)](AccountID const& acc) {
            return hf->accountKeylet(acc);
        });
}

wasm_trap_t*
credentialKeylet_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    return invokeHostFunc<AccountID, AccountID, Bytes>(
        env,
        params,
        results,
        [hf = reinterpret_cast<HostFunctions*>(env)](
            AccountID const& subj,
            AccountID const& iss,
            Bytes const& credType) {
            return hf->credentialKeylet(subj, iss, credType);
        });
}

wasm_trap_t*
escrowKeylet_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    return invokeHostFunc<AccountID, int32_t>(
        env,
        params,
        results,
        [hf = reinterpret_cast<HostFunctions*>(env)](
            AccountID const& acc, int32_t seq) {
            return hf->escrowKeylet(acc, seq);
        });
}

wasm_trap_t*
oracleKeylet_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    return invokeHostFunc<AccountID, int32_t>(
        env,
        params,
        results,
        [hf = reinterpret_cast<HostFunctions*>(env)](
            AccountID const& acc, int32_t seq) {
            return hf->oracleKeylet(acc, seq);
        });
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

    auto const nftRaw = getData<Bytes>(rt, params, index);
    if (!nftRaw)
    {
        return hfResult(results, nftRaw.error());
    }

    if (nftRaw->size() != uint256::bytes)
    {
        hf->getJournal().trace()
            << "WAMR getNFT: Invalid NFT data size: " << nftRaw->size()
            << ", expected " << (uint256::bytes);
        return hfResult(results, HF_ERR_INVALID_PARAMS);
    }

    uint256 const ntfId(uint256::fromVoid(nftRaw->data()));
    auto const nftURI = hf->getNFT(acc.value(), ntfId);
    if (!nftURI)
    {
        return hfResult(results, nftURI.error());
    }

    return hfResult(
        results,
        setData(
            rt,
            params->data[index].of.i32,
            params->data[index + 1].of.i32,
            nftURI->data(),
            nftURI->size()));
}

wasm_trap_t*
trace_wrap(void* env, wasm_val_vec_t const* params, wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* rt = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const msg = getData<std::string>(rt, params, index);
    if (!msg)
    {
        return hfResult(results, msg.error());
    }

    auto const data = getData<Bytes>(rt, params, index);
    if (!data)
    {
        return hfResult(results, data.error());
    }

    auto const asHex = getData<int32_t>(rt, params, index);
    if (!asHex)
    {
        return hfResult(results, asHex.error());
    }

    auto const e = hf->trace(msg.value(), data.value(), asHex.value());
    if (!e)
    {
        return hfResult(results, e.error());
    }
    return hfResult(results, e.value());
}

wasm_trap_t*
traceNum_wrap(void* env, wasm_val_vec_t const* params, wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* rt = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const msg = getData<std::string>(rt, params, index);
    if (!msg)
    {
        return hfResult(results, msg.error());
    }

    auto const number = getData<int64_t>(rt, params, index);
    if (!number)
    {
        return hfResult(results, number.error());
    }

    auto const e = hf->traceNum(msg.value(), number.value());
    if (!e)
    {
        return hfResult(results, e.error());
    }
    return hfResult(results, e.value());
}

}  // namespace ripple
