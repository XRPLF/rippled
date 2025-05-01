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

#include <xrpld/app/misc/WasmHostFuncWrapper.h>
#include <xrpld/app/tx/detail/NFTokenUtils.h>

#include <xrpl/protocol/digest.h>

namespace ripple {

wasm_trap_t*
getLedgerSqn_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    int32_t const sqn = hf->getLedgerSqn();
    results->data[0] = WASM_I32_VAL(sqn);

    return nullptr;
}

wasm_trap_t*
getParentLedgerTime_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    int32_t const ltime = hf->getParentLedgerTime();
    results->data[0] = WASM_I32_VAL(ltime);
    return nullptr;
}

static Expected<Bytes, std::string>
getParameterData(wasm_val_vec_t const* params, size_t index)
{
    auto& vm = WasmEngine::instance();
    auto fnameOffset = params->data[index].of.i32;
    auto fnameLen = params->data[index + 1].of.i32;
    auto mem = vm.getMem();
    if (!mem.s)
        return Unexpected<std::string>("No memory exported");

    if (mem.s <= fnameOffset + fnameLen)
        return Unexpected<std::string>("Memory access failed");
    Bytes fname(mem.p + fnameOffset, mem.p + fnameOffset + fnameLen);
    return fname;
}

static Expected<std::string, wasm_trap_t*>
getFieldName(wasm_val_vec_t const* params, size_t index)
{
    auto const dataRes = getParameterData(params, index);
    if (dataRes)
    {
        return std::string(dataRes->begin(), dataRes->end());
    }
    else
    {
        auto& vm = WasmEngine::instance();
        return Unexpected<wasm_trap_t*>(
            reinterpret_cast<wasm_trap_t*>(vm.newTrap(dataRes.error())));
    }
}

static Expected<int32_t, std::string>
setData(Bytes const& data)
{
    auto& vm = WasmEngine::instance();
    auto mem = vm.getMem();
    if (!mem.s)
        return Unexpected<std::string>("No memory exported");

    int32_t const dataLen = static_cast<int32_t>(data.size());
    int32_t const dataPtr = vm.allocate(dataLen);
    if (!dataPtr)
        return Unexpected<std::string>("Allocation error");
    memcpy(mem.p + dataPtr, data.data(), dataLen);

    auto retPtr = vm.allocate(8);
    if (!retPtr)
        return Unexpected<std::string>("Allocation error");
    int32_t* retData = reinterpret_cast<int32_t*>(mem.p + retPtr);
    retData[0] = dataPtr;
    retData[1] = dataLen;

    return retPtr;
}

wasm_trap_t*
getTxField_wrap(
    void* env,
    const wasm_val_vec_t* params,
    wasm_val_vec_t* results)
{
    auto& vm = WasmEngine::instance();
    auto* hf = reinterpret_cast<HostFunctions*>(env);

    auto fname = getFieldName(params, 0);
    if (!fname)
        return reinterpret_cast<wasm_trap_t*>(vm.newTrap());

    auto fieldData = hf->getTxField(fname.value());
    if (!fieldData)
        return reinterpret_cast<wasm_trap_t*>(vm.newTrap("Field not found"));

    auto pointer = setData(fieldData.value());
    if (!pointer)
        return reinterpret_cast<wasm_trap_t*>(vm.newTrap());

    results->data[0] = WASM_I32_VAL(pointer.value());
    //    out[1] = WasmEdge_ValueGenI32((int)fieldData.value().size());
    return nullptr;
}

wasm_trap_t*
getLedgerEntryField_wrap(
    void* env,
    const wasm_val_vec_t* params,
    wasm_val_vec_t* results)
{
    auto& vm = WasmEngine::instance();
    auto* hf = reinterpret_cast<HostFunctions*>(env);

    int32_t const type = params->data[0].of.i32;
    auto lkData = getParameterData(params, 1);
    if (!lkData)
        return reinterpret_cast<wasm_trap_t*>(vm.newTrap());

    auto fname = getFieldName(params, 3);
    if (!fname)
        return reinterpret_cast<wasm_trap_t*>(vm.newTrap());

    auto fieldData =
        hf->getLedgerEntryField(type, lkData.value(), fname.value());
    if (!fieldData)
        return reinterpret_cast<wasm_trap_t*>(vm.newTrap());
    auto pointer = setData(fieldData.value());
    if (!pointer)
        return reinterpret_cast<wasm_trap_t*>(vm.newTrap());

    results->data[0] = WASM_I32_VAL(pointer.value());
    //    out[1] = WasmEdge_ValueGenI32((int)fieldData.value().size());
    return nullptr;
}

wasm_trap_t*
getCurrentLedgerEntryField_wrap(
    void* env,
    const wasm_val_vec_t* params,
    wasm_val_vec_t* results)
{
    auto& vm = WasmEngine::instance();
    auto* hf = reinterpret_cast<HostFunctions*>(env);

    auto fname = getFieldName(params, 0);
    if (!fname)
        return reinterpret_cast<wasm_trap_t*>(vm.newTrap());

    auto fieldData = hf->getCurrentLedgerEntryField(fname.value());
    if (!fieldData)
        return reinterpret_cast<wasm_trap_t*>(vm.newTrap());

    auto pointer = setData(fieldData.value());
    if (!pointer)
        return reinterpret_cast<wasm_trap_t*>(vm.newTrap());

    results->data[0] = WASM_I32_VAL(pointer.value());
    //    out[1] = WasmEdge_ValueGenI32((int)fieldData.value().size());
    return nullptr;
}

wasm_trap_t*
getNFT_wrap(void* env, const wasm_val_vec_t* params, wasm_val_vec_t* results)
{
    auto& vm = WasmEngine::instance();
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto account = getFieldName(params, 0);
    if (!account)
        return reinterpret_cast<wasm_trap_t*>(vm.newTrap());

    auto nftId = getFieldName(params, 2);
    if (!nftId)
        return reinterpret_cast<wasm_trap_t*>(vm.newTrap());

    auto nftURI = hf->getNFT(account.value(), nftId.value());
    if (!nftURI)
        return reinterpret_cast<wasm_trap_t*>(vm.newTrap());

    auto pointer = setData(nftURI.value());
    if (!pointer)
        return reinterpret_cast<wasm_trap_t*>(vm.newTrap());

    results->data[0] = WASM_I32_VAL(pointer.value());
    //    out[1] = WasmEdge_ValueGenI32((int)nftURI.value().size());
    return nullptr;
}

wasm_trap_t*
accountKeylet_wrap(
    void* env,
    const wasm_val_vec_t* params,
    wasm_val_vec_t* results)
{
    auto& vm = WasmEngine::instance();
    auto* hf = reinterpret_cast<HostFunctions*>(env);

    auto account = getFieldName(params, 0);
    if (!account)
        return reinterpret_cast<wasm_trap_t*>(vm.newTrap());

    auto keylet = hf->accountKeylet(account.value());
    if (!keylet)
        return reinterpret_cast<wasm_trap_t*>(vm.newTrap());

    auto pointer = setData(keylet.value());
    if (!pointer)
        return reinterpret_cast<wasm_trap_t*>(vm.newTrap());

    results->data[0] = WASM_I32_VAL(pointer.value());
    //    out[1] = WasmEdge_ValueGenI32((int)nftURI.value().size());
    return nullptr;
}

wasm_trap_t*
credentialKeylet_wrap(
    void* env,
    const wasm_val_vec_t* params,
    wasm_val_vec_t* results)
{
    auto& vm = WasmEngine::instance();
    auto* hf = reinterpret_cast<HostFunctions*>(env);

    auto subject = getFieldName(params, 0);
    if (!subject)
        return reinterpret_cast<wasm_trap_t*>(vm.newTrap());

    auto issuer = getFieldName(params, 2);
    if (!issuer)
        return reinterpret_cast<wasm_trap_t*>(vm.newTrap());

    auto credentialType = getFieldName(params, 4);
    if (!credentialType)
        return reinterpret_cast<wasm_trap_t*>(vm.newTrap());

    auto keylet = hf->credentialKeylet(
        subject.value(), issuer.value(), credentialType.value());
    if (!keylet)
        return reinterpret_cast<wasm_trap_t*>(vm.newTrap());

    auto pointer = setData(keylet.value());
    if (!pointer)
        return reinterpret_cast<wasm_trap_t*>(vm.newTrap());

    results->data[0] = WASM_I32_VAL(pointer.value());
    //    out[1] = WasmEdge_ValueGenI32((int)nftURI.value().size());
    return nullptr;
}

wasm_trap_t*
escrowKeylet_wrap(
    void* env,
    const wasm_val_vec_t* params,
    wasm_val_vec_t* results)
{
    auto& vm = WasmEngine::instance();
    auto* hf = reinterpret_cast<HostFunctions*>(env);

    auto account = getFieldName(params, 0);
    if (!account)
        return reinterpret_cast<wasm_trap_t*>(vm.newTrap());

    int32_t const sequence = params->data[2].of.i32;

    auto keylet = hf->escrowKeylet(account.value(), sequence);
    if (!keylet)
        return reinterpret_cast<wasm_trap_t*>(vm.newTrap());

    auto pointer = setData(keylet.value());
    if (!pointer)
        return reinterpret_cast<wasm_trap_t*>(vm.newTrap());

    results->data[0] = WASM_I32_VAL(pointer.value());
    //    out[1] = WasmEdge_ValueGenI32((int)nftURI.value().size());
    return nullptr;
}

wasm_trap_t*
oracleKeylet_wrap(
    void* env,
    const wasm_val_vec_t* params,
    wasm_val_vec_t* results)
{
    auto& vm = WasmEngine::instance();
    auto* hf = reinterpret_cast<HostFunctions*>(env);

    auto account = getFieldName(params, 0);
    if (!account)
        return reinterpret_cast<wasm_trap_t*>(vm.newTrap());

    auto documentId = params->data[2].of.i32;

    auto keylet = hf->escrowKeylet(account.value(), documentId);
    if (!keylet)
        return reinterpret_cast<wasm_trap_t*>(vm.newTrap());

    auto pointer = setData(keylet.value());
    if (!pointer)
        return reinterpret_cast<wasm_trap_t*>(vm.newTrap());

    results->data[0] = WASM_I32_VAL(pointer.value());
    //    out[1] = WasmEdge_ValueGenI32((int)nftURI.value().size());
    return nullptr;
}

wasm_trap_t*
updateData_wrap(
    void* env,
    const wasm_val_vec_t* params,
    wasm_val_vec_t* results)
{
    auto& vm = WasmEngine::instance();
    auto* hf = reinterpret_cast<HostFunctions*>(env);

    auto fname = getParameterData(params, 0);
    if (!fname)
        return reinterpret_cast<wasm_trap_t*>(vm.newTrap());

    if (!hf->updateData(fname.value()))
        return reinterpret_cast<wasm_trap_t*>(vm.newTrap());

    return nullptr;
}

wasm_trap_t*
computeSha512HalfHash_wrap(
    void* env,
    const wasm_val_vec_t* params,
    wasm_val_vec_t* results)
{
    auto& vm = WasmEngine::instance();
    auto* hf = reinterpret_cast<HostFunctions*>(env);

    auto fname = getParameterData(params, 0);
    if (!fname)
        return reinterpret_cast<wasm_trap_t*>(vm.newTrap());

    auto hres = hf->computeSha512HalfHash(fname.value());
    Bytes digest{hres.begin(), hres.end()};
    auto pointer = setData(digest);
    if (!pointer)
        return reinterpret_cast<wasm_trap_t*>(vm.newTrap());

    results->data[0] = WASM_I32_VAL(pointer.value());
    //    out[1] = WasmEdge_ValueGenI32(32);
    return nullptr;
}

wasm_trap_t*
print_wrap(void* env, const wasm_val_vec_t* params, wasm_val_vec_t* results)
{
    auto& vm = WasmEngine::instance();
    // auto* hf = reinterpret_cast<HostFunctions*>(env);

    auto f = getParameterData(params, 0);
    if (!f)
        return reinterpret_cast<wasm_trap_t*>(vm.newTrap());
    std::string s(f->begin(), f->end());
    if (s.size() < 4096)
        std::cout << s << std::endl;
    return nullptr;
}

}  // namespace ripple
