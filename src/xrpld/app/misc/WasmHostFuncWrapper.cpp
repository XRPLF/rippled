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

static int32_t
setData(
    InstanceWrapper const* rt,
    int32_t dst,
    int32_t dsz,
    uint8_t const* src,
    int32_t ssz)
{
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

static Expected<Bytes, int32_t>
getData(InstanceWrapper const* rt, int32_t src, int32_t ssz)
{
    auto mem = rt ? rt->getMem() : wmem();
    if (!mem.s)
        return Unexpected(HF_ERR_NO_MEM_EXPORTED);

    if (src + ssz > mem.s)
        return Unexpected(HF_ERR_OUT_OF_BOUNDS);

    Bytes data(mem.p + src, mem.p + src + ssz);
    return data;
}

static Expected<AccountID, int32_t>
getDataAccount(InstanceWrapper const* rt, int32_t ptr, int32_t sz)
{
    auto const r = getData(rt, ptr, sz);
    if (r->size() < AccountID::bytes)
        return Unexpected(HF_ERR_INVALID_PARAMS);

    return AccountID::fromVoid(r->data());
}

static Expected<std::string, int32_t>
getDataString(InstanceWrapper const* rt, int32_t src, int32_t ssz)
{
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

    auto const r = getData(rt, params->data[0].of.i32, params->data[1].of.i32);
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

    auto const& m = SField::getKnownCodeToField();
    auto const it = m.find(params->data[0].of.i32);
    if (it == m.end())
    {
        RET(HF_ERR_FIELD_NOT_FOUND);
    }
    auto const& fname(*it->second);

    auto fieldData = hf->getTxField(fname);
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

    auto const& m = SField::getKnownCodeToField();
    auto const it = m.find(params->data[0].of.i32);
    if (it == m.end())
    {
        RET(HF_ERR_FIELD_NOT_FOUND);
    }
    auto const& fname(*it->second);

    auto fieldData = hf->getCurrentLedgerObjField(fname);
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

    auto const& m = SField::getKnownCodeToField();
    auto const it = m.find(params->data[1].of.i32);
    if (it == m.end())
    {
        RET(HF_ERR_FIELD_NOT_FOUND);
    }
    auto const& fname(*it->second);

    auto fieldData = hf->getLedgerObjField(params->data[0].of.i32, fname);
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

    auto const r = getData(rt, params->data[0].of.i32, params->data[1].of.i32);
    if (!r)
    {
        RET(r.error());
    }

    auto fieldData = hf->getTxNestedField(makeSlice(r.value()));
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

    auto const r = getData(rt, params->data[0].of.i32, params->data[1].of.i32);
    if (!r)
    {
        RET(r.error());
    }

    auto fieldData = hf->getCurrentLedgerObjNestedField(makeSlice(r.value()));
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

    auto const r = getData(rt, params->data[1].of.i32, params->data[2].of.i32);
    if (!r)
    {
        RET(r.error());
    }

    auto fieldData = hf->getLedgerObjNestedField(
        params->data[0].of.i32, makeSlice(r.value()));
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
    // auto const* rt = reinterpret_cast<InstanceWrapper const*>(hf->getRT());

    auto const& m = SField::getKnownCodeToField();
    auto const it = m.find(params->data[0].of.i32);
    if (it == m.end())
    {
        RET(HF_ERR_FIELD_NOT_FOUND);
    }
    auto const& fname(*it->second);

    int32_t sz = hf->getTxArrayLen(fname);
    RET(sz);
}

wasm_trap_t*
getCurrentLedgerObjArrayLen_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    // auto const* rt = reinterpret_cast<InstanceWrapper const*>(hf->getRT());

    auto const& m = SField::getKnownCodeToField();
    auto const it = m.find(params->data[0].of.i32);
    if (it == m.end())
    {
        RET(HF_ERR_FIELD_NOT_FOUND);
    }
    auto const& fname(*it->second);

    int32_t sz = hf->getCurrentLedgerObjArrayLen(fname);
    RET(sz);
}

wasm_trap_t*
getLedgerObjArrayLen_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    // auto const* rt = reinterpret_cast<InstanceWrapper const*>(hf->getRT());

    auto const& m = SField::getKnownCodeToField();
    auto const it = m.find(params->data[1].of.i32);
    if (it == m.end())
    {
        RET(HF_ERR_FIELD_NOT_FOUND);
    }
    auto const& fname(*it->second);

    int32_t sz = hf->getLedgerObjArrayLen(params->data[0].of.i32, fname);
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

    auto const r = getData(rt, params->data[0].of.i32, params->data[1].of.i32);
    if (!r)
    {
        RET(r.error());
    }

    int32_t sz = hf->getTxNestedArrayLen(makeSlice(r.value()));
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

    auto const r = getData(rt, params->data[0].of.i32, params->data[1].of.i32);
    if (!r)
    {
        RET(r.error());
    }

    int32_t sz = hf->getCurrentLedgerObjNestedArrayLen(makeSlice(r.value()));
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

    auto const r = getData(rt, params->data[1].of.i32, params->data[2].of.i32);
    if (!r)
    {
        RET(r.error());
    }

    int32_t sz = hf->getLedgerObjNestedArrayLen(
        params->data[0].of.i32, makeSlice(r.value()));
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

    auto const r = getData(rt, params->data[0].of.i32, params->data[1].of.i32);
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

    auto const r = getData(rt, params->data[0].of.i32, params->data[1].of.i32);
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

    auto const acc =
        getDataAccount(rt, params->data[0].of.i32, params->data[1].of.i32);
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

    auto const subject =
        getDataAccount(rt, params->data[0].of.i32, params->data[1].of.i32);
    if (!subject)
    {
        RET(subject.error());
    }

    auto const issuer =
        getDataAccount(rt, params->data[2].of.i32, params->data[3].of.i32);
    if (!issuer)
    {
        RET(issuer.error());
    }

    auto const credType =
        getData(rt, params->data[4].of.i32, params->data[5].of.i32);
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

    auto const acc =
        getDataAccount(rt, params->data[0].of.i32, params->data[1].of.i32);
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

    auto const acc =
        getDataAccount(rt, params->data[0].of.i32, params->data[1].of.i32);
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

    auto const acc =
        getDataAccount(rt, params->data[0].of.i32, params->data[1].of.i32);
    if (!acc)
    {
        RET(acc.error());
    }

    auto const nftRaw =
        getData(rt, params->data[2].of.i32, params->data[3].of.i32);
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

    auto const msg =
        getDataString(rt, params->data[0].of.i32, params->data[1].of.i32);
    if (!msg)
    {
        RET(msg.error());
    }

    auto const data =
        getData(rt, params->data[2].of.i32, params->data[3].of.i32);
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

    auto const msg =
        getDataString(rt, params->data[0].of.i32, params->data[1].of.i32);
    if (!msg)
    {
        RET(msg.error());
    }

    auto const e = hf->traceNum(msg.value(), params->data[2].of.i64);
    RET(e);
}

}  // namespace ripple
