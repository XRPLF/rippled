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
getData(InstanceWrapper const* rt, wasm_val_vec_t const* params, int32_t src);

template <>
Expected<SFIELD_PARAM, HostFuncError>
getData<SFIELD_PARAM>(
    InstanceWrapper const* _rt,
    wasm_val_vec_t const* params,
    int32_t i)
{
    auto const& m = SField::getKnownCodeToField();
    auto const it = m.find(params->data[i].of.i32);
    if (it == m.end())
    {
        return Unexpected(HF_ERR_FIELD_NOT_FOUND);
    }
    return *it->second;
}

template <>
Expected<Bytes, HostFuncError>
getData<Bytes>(
    InstanceWrapper const* rt,
    wasm_val_vec_t const* params,
    int32_t i)
{
    auto const src = params->data[i].of.i32;
    auto const ssz = params->data[i + 1].of.i32;
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
Expected<AccountID, HostFuncError>
getData<AccountID>(
    InstanceWrapper const* rt,
    wasm_val_vec_t const* params,
    int32_t i)
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
    int32_t i)
{
    auto const src = params->data[i].of.i32;
    auto const ssz = params->data[i + 1].of.i32;
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

#define RET(x)                          \
    results->data[0] = WASM_I32_VAL(x); \
    results->num_elems = 1;             \
    return nullptr;

wasm_trap_t*
getLedgerSqnOld_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    // auto const* rt = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int32_t const sqn = hf->getLedgerSqn();
    RET(sqn);
}

wasm_trap_t*
getLedgerSqn_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* rt = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int32_t const sqn = hf->getLedgerSqn();

    RET(setData(
        rt,
        params->data[0].of.i32,
        params->data[1].of.i32,
        reinterpret_cast<uint8_t const*>(&sqn),
        static_cast<int32_t>(sizeof(sqn))));
}

wasm_trap_t*
getParentLedgerTime_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* rt = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int32_t const ltime = hf->getParentLedgerTime();
    RET(setData(
        rt,
        params->data[0].of.i32,
        params->data[1].of.i32,
        reinterpret_cast<uint8_t const*>(&ltime),
        static_cast<int32_t>(sizeof(ltime))));
}

wasm_trap_t*
getParentLedgerHash_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* rt = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    Hash const hash = hf->getParentLedgerHash();
    RET(setData(
        rt,
        params->data[0].of.i32,
        params->data[1].of.i32,
        hash.data(),
        static_cast<int32_t>(hash.size())));
}

wasm_trap_t*
cacheLedgerObj_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* rt = reinterpret_cast<InstanceWrapper const*>(hf->getRT());

    auto const r = getData<Bytes>(rt, params, 0);
    if (!r)
    {
        RET(r.error());
    }

    if (r->size() < uint256::bytes)
    {
        RET(HF_ERR_INVALID_PARAMS);
    }

    uint256 const key(uint256::fromVoid(r->data()));
    int32_t const idx =
        hf->cacheLedgerObj(keylet::unchecked(key), params->data[2].of.i32);
    RET(idx);
}

wasm_trap_t*
getTxField_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* rt = reinterpret_cast<InstanceWrapper const*>(hf->getRT());

    auto const& fname = getData<SFIELD_PARAM>(rt, params, 0);
    if (!fname)
    {
        RET(fname.error());
    }

    auto fieldData = hf->getTxField(fname.value());
    if (!fieldData)
    {
        RET(fieldData.error());
    }

    RET(setData(
        rt,
        params->data[1].of.i32,
        params->data[2].of.i32,
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

    auto const& fname = getData<SFIELD_PARAM>(rt, params, 0);
    if (!fname)
    {
        RET(fname.error());
    }

    auto fieldData = hf->getCurrentLedgerObjField(fname.value());
    if (!fieldData)
    {
        RET(fieldData.error());
    }

    RET(setData(
        rt,
        params->data[1].of.i32,
        params->data[2].of.i32,
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

    auto const& fname = getData<SFIELD_PARAM>(rt, params, 1);
    if (!fname)
    {
        RET(fname.error());
    }

    auto fieldData =
        hf->getLedgerObjField(params->data[0].of.i32, fname.value());
    if (!fieldData)
    {
        RET(fieldData.error());
    }

    RET(setData(
        rt,
        params->data[2].of.i32,
        params->data[3].of.i32,
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

    auto const r = getData<Bytes>(rt, params, 0);
    if (!r)
    {
        RET(r.error());
    }

    auto fieldData = hf->getTxNestedField(r.value());
    if (!fieldData)
    {
        RET(fieldData.error());
    }

    RET(setData(
        rt,
        params->data[2].of.i32,
        params->data[3].of.i32,
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

    auto const r = getData<Bytes>(rt, params, 0);
    if (!r)
    {
        RET(r.error());
    }

    auto fieldData = hf->getCurrentLedgerObjNestedField(r.value());
    if (!fieldData)
    {
        RET(fieldData.error());
    }

    RET(setData(
        rt,
        params->data[2].of.i32,
        params->data[3].of.i32,
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

    auto const r = getData<Bytes>(rt, params, 1);
    if (!r)
    {
        RET(r.error());
    }

    auto fieldData =
        hf->getLedgerObjNestedField(params->data[0].of.i32, r.value());
    if (!fieldData)
    {
        RET(fieldData.error());
    }

    RET(setData(
        rt,
        params->data[3].of.i32,
        params->data[4].of.i32,
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

    auto const& fname = getData<SFIELD_PARAM>(rt, params, 0);
    if (!fname)
    {
        RET(fname.error());
    }

    int32_t sz = hf->getTxArrayLen(fname.value());
    RET(sz);
}

wasm_trap_t*
getCurrentLedgerObjArrayLen_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* rt = reinterpret_cast<InstanceWrapper const*>(hf->getRT());

    auto const& fname = getData<SFIELD_PARAM>(rt, params, 0);
    if (!fname)
    {
        RET(fname.error());
    }

    int32_t sz = hf->getCurrentLedgerObjArrayLen(fname.value());
    RET(sz);
}

wasm_trap_t*
getLedgerObjArrayLen_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* rt = reinterpret_cast<InstanceWrapper const*>(hf->getRT());

    auto const& fname = getData<SFIELD_PARAM>(rt, params, 1);
    if (!fname)
    {
        RET(fname.error());
    }

    int32_t sz =
        hf->getLedgerObjArrayLen(params->data[0].of.i32, fname.value());
    RET(sz);
}

wasm_trap_t*
getTxNestedArrayLen_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* rt = reinterpret_cast<InstanceWrapper const*>(hf->getRT());

    auto const r = getData<Bytes>(rt, params, 0);
    if (!r)
    {
        RET(r.error());
    }

    int32_t sz = hf->getTxNestedArrayLen(r.value());
    RET(sz);
}

wasm_trap_t*
getCurrentLedgerObjNestedArrayLen_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* rt = reinterpret_cast<InstanceWrapper const*>(hf->getRT());

    auto const r = getData<Bytes>(rt, params, 0);
    if (!r)
    {
        RET(r.error());
    }

    int32_t sz = hf->getCurrentLedgerObjNestedArrayLen(r.value());
    RET(sz);
}

wasm_trap_t*
getLedgerObjNestedArrayLen_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* rt = reinterpret_cast<InstanceWrapper const*>(hf->getRT());

    auto const r = getData<Bytes>(rt, params, 1);
    if (!r)
    {
        RET(r.error());
    }

    int32_t sz =
        hf->getLedgerObjNestedArrayLen(params->data[0].of.i32, r.value());
    RET(sz);
}

wasm_trap_t*
updateData_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* rt = reinterpret_cast<InstanceWrapper const*>(hf->getRT());

    if (params->data[1].of.i32 > maxWasmDataLength)
    {
        RET(HF_ERR_DATA_FIELD_TOO_LARGE)
    }

    auto const r = getData<Bytes>(rt, params, 0);
    if (!r)
    {
        RET(r.error());
    }

    RET(hf->updateData(r.value()));
}

wasm_trap_t*
computeSha512HalfHash_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* rt = reinterpret_cast<InstanceWrapper const*>(hf->getRT());

    auto const r = getData<Bytes>(rt, params, 0);
    if (!r)
    {
        RET(r.error());
    }

    auto const hash = hf->computeSha512HalfHash(r.value());
    RET(setData(
        rt,
        params->data[2].of.i32,
        params->data[3].of.i32,
        hash.data(),
        hash.size()));
}

wasm_trap_t*
accountKeylet_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* rt = reinterpret_cast<InstanceWrapper const*>(hf->getRT());

    auto const acc = getData<AccountID>(rt, params, 0);
    if (!acc)
    {
        RET(acc.error());
    }

    auto const k = hf->accountKeylet(acc.value());
    if (!k)
    {
        RET(k.error());
    }

    RET(setData(
        rt,
        params->data[2].of.i32,
        params->data[3].of.i32,
        k->data(),
        k->size()));
}

wasm_trap_t*
credentialKeylet_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* rt = reinterpret_cast<InstanceWrapper const*>(hf->getRT());

    auto const subject = getData<AccountID>(rt, params, 0);
    if (!subject)
    {
        RET(subject.error());
    }

    auto const issuer = getData<AccountID>(rt, params, 2);
    if (!issuer)
    {
        RET(issuer.error());
    }

    auto const credType = getData<Bytes>(rt, params, 4);
    if (!credType)
    {
        RET(credType.error());
    }

    auto const k =
        hf->credentialKeylet(subject.value(), issuer.value(), credType.value());
    if (!k)
    {
        RET(k.error());
    }

    RET(setData(
        rt,
        params->data[6].of.i32,
        params->data[7].of.i32,
        k->data(),
        k->size()));
}

wasm_trap_t*
escrowKeylet_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* rt = reinterpret_cast<InstanceWrapper const*>(hf->getRT());

    auto const acc = getData<AccountID>(rt, params, 0);
    if (!acc)
    {
        RET(acc.error());
    }

    auto const k = hf->escrowKeylet(acc.value(), params->data[2].of.i32);
    if (!k)
    {
        RET(k.error());
    }

    RET(setData(
        rt,
        params->data[3].of.i32,
        params->data[4].of.i32,
        k->data(),
        k->size()));
}

wasm_trap_t*
oracleKeylet_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* rt = reinterpret_cast<InstanceWrapper const*>(hf->getRT());

    auto const acc = getData<AccountID>(rt, params, 0);
    if (!acc)
    {
        RET(acc.error());
    }

    auto const k = hf->oracleKeylet(acc.value(), params->data[2].of.i32);
    if (!k)
    {
        RET(k.error());
    }

    RET(setData(
        rt,
        params->data[3].of.i32,
        params->data[4].of.i32,
        k->data(),
        k->size()));
}

wasm_trap_t*
getNFT_wrap(void* env, wasm_val_vec_t const* params, wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* rt = reinterpret_cast<InstanceWrapper const*>(hf->getRT());

    auto const acc = getData<AccountID>(rt, params, 0);
    if (!acc)
    {
        RET(acc.error());
    }

    auto const nftRaw = getData<Bytes>(rt, params, 2);
    if (!nftRaw)
    {
        RET(nftRaw.error());
    }

    if (nftRaw->size() != uint256::bytes)
    {
        hf->getJournal().trace()
            << "WAMR getNFT: Invalid NFT data size: " << nftRaw->size()
            << ", expected " << (uint256::bytes);
        RET(HF_ERR_INVALID_PARAMS);
    }

    uint256 const ntfId(uint256::fromVoid(nftRaw->data()));
    auto const nftURI = hf->getNFT(acc.value(), ntfId);
    if (!nftURI)
    {
        RET(nftURI.error());
    }

    RET(setData(
        rt,
        params->data[4].of.i32,
        params->data[5].of.i32,
        nftURI->data(),
        nftURI->size()));
}

wasm_trap_t*
trace_wrap(void* env, wasm_val_vec_t const* params, wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* rt = reinterpret_cast<InstanceWrapper const*>(hf->getRT());

    auto const msg = getData<std::string>(rt, params, 0);
    if (!msg)
    {
        RET(msg.error());
    }

    auto const data = getData<Bytes>(rt, params, 2);
    if (!data)
    {
        RET(data.error());
    }

    auto const e = hf->trace(msg.value(), data.value(), params->data[4].of.i32);
    RET(e);
}

wasm_trap_t*
traceNum_wrap(void* env, wasm_val_vec_t const* params, wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* rt = reinterpret_cast<InstanceWrapper const*>(hf->getRT());

    auto const msg = getData<std::string>(rt, params, 0);
    if (!msg)
    {
        RET(msg.error());
    }

    auto const e = hf->traceNum(msg.value(), params->data[2].of.i64);
    RET(e);
}

}  // namespace ripple
