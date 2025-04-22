//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2020 Ripple Labs Inc.

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

#include <xrpld/app/misc/WasmVM.h>

#include "xrpl/protocol/AccountID.h"
#include "xrpl/protocol/LedgerFormats.h"

namespace ripple {

WasmEdge_Result
getLedgerSqn(
    void* data,
    const WasmEdge_CallingFrameContext*,
    const WasmEdge_Value*,
    WasmEdge_Value* out)
{
    out[0] = WasmEdge_ValueGenI32(((HostFunctions*)data)->getLedgerSqn());
    return WasmEdge_Result_Success;
}

WasmEdge_Result
getParentLedgerTime(
    void* data,
    const WasmEdge_CallingFrameContext*,
    const WasmEdge_Value*,
    WasmEdge_Value* out)
{
    out[0] =
        WasmEdge_ValueGenI32(((HostFunctions*)data)->getParentLedgerTime());
    return WasmEdge_Result_Success;
}

Expected<Bytes, WasmEdge_Result>
getParameterData(
    const WasmEdge_CallingFrameContext* fm,
    const WasmEdge_Value* in,
    size_t index)
{
    auto fnameOffset = (uint32_t)WasmEdge_ValueGetI32(in[index]);
    auto fnameLen = (uint32_t)WasmEdge_ValueGetI32(in[index + 1]);
    Bytes fname(fnameLen, char{0});
    WasmEdge_MemoryInstanceContext* mem =
        WasmEdge_CallingFrameGetMemoryInstance(fm, 0);
    WasmEdge_Result Res = WasmEdge_MemoryInstanceGetData(
        mem, (uint8_t*)(fname.data()), fnameOffset, fnameLen);
    if (WasmEdge_ResultOK(Res))
    {
        return fname;
    }
    else
    {
        return Unexpected<WasmEdge_Result>(Res);
    }
}

Expected<std::string, WasmEdge_Result>
getFieldName(
    const WasmEdge_CallingFrameContext* fm,
    const WasmEdge_Value* in,
    size_t index)
{
    auto dataRes = getParameterData(fm, in, index);
    if (dataRes)
    {
        return std::string(dataRes.value().begin(), dataRes->end());
    }
    else
    {
        return Unexpected<WasmEdge_Result>(dataRes.error());
    }
}

Expected<WasmEdge_Value, WasmEdge_Result>
setData(const WasmEdge_CallingFrameContext* fm, Bytes const& data)
{
    auto alloc = [fm](int32_t dataLen) -> int32_t {
        WasmEdge_String allocFunc = WasmEdge_StringCreateByCString("allocate");
        auto mod = WasmEdge_CallingFrameGetModuleInstance(fm);
        WasmEdge_FunctionInstanceContext* func =
            WasmEdge_ModuleInstanceFindFunction(mod, allocFunc);
        WasmEdge_Value allocParams[1] = {
            WasmEdge_ValueGenI32(dataLen)};  // 4 for prepend the data size
        WasmEdge_Value allocReturns[1];
        auto executor = WasmEdge_CallingFrameGetExecutor(fm);
        auto res = WasmEdge_ExecutorInvoke(
            executor, func, allocParams, 1, allocReturns, 1);
        if (WasmEdge_ResultOK(res))
        {
            return WasmEdge_ValueGetI32(allocReturns[0]);
        }
        else
        {
            return 0;
        }
    };

    auto dataLen = (int32_t)data.size();
    auto dataPtr = alloc(dataLen);
    auto retPtr = alloc(8);
    if (dataPtr && retPtr)
    {
        auto mem = WasmEdge_CallingFrameGetMemoryInstance(fm, 0);
        auto res =
            WasmEdge_MemoryInstanceSetData(mem, data.data(), dataPtr, dataLen);
        if (WasmEdge_ResultOK(res))
        {
            unsigned char intBuf[8];  // little-endian
            for (size_t i = 0; i < 4; ++i)
            {
                intBuf[i] = (dataPtr >> (i * 8)) & 0xFF;
            }
            for (size_t i = 0; i < 4; ++i)
            {
                intBuf[i + 4] = (dataLen >> (i * 8)) & 0xFF;
            }

            res = WasmEdge_MemoryInstanceSetData(mem, intBuf, retPtr, 8);
            if (WasmEdge_ResultOK(res))
            {
                return WasmEdge_ValueGenI32(retPtr);
            }
        }
    }
    return Unexpected<WasmEdge_Result>(WasmEdge_Result_Fail);
}

WasmEdge_Result
getTxField(
    void* data,
    const WasmEdge_CallingFrameContext* fm,
    const WasmEdge_Value* in,
    WasmEdge_Value* out)
{
    auto fname = getFieldName(fm, in, 0);
    if (!fname)
        return fname.error();

    auto fieldData = ((HostFunctions*)data)->getTxField(fname.value());
    if (!fieldData)
        return WasmEdge_Result_Fail;

    auto pointer = setData(fm, fieldData.value());
    if (!pointer)
        return pointer.error();

    out[0] = pointer.value();
    //    out[1] = WasmEdge_ValueGenI32((int)fieldData.value().size());
    return WasmEdge_Result_Success;
}

WasmEdge_Result
getLedgerEntryField(
    void* data,
    const WasmEdge_CallingFrameContext* fm,
    const WasmEdge_Value* in,
    WasmEdge_Value* out)
{
    auto type = WasmEdge_ValueGetI32(in[0]);
    auto lkData = getParameterData(fm, in, 1);
    if (!lkData)
        return lkData.error();

    auto fname = getFieldName(fm, in, 3);
    if (!fname)
        return fname.error();

    auto fieldData =
        ((HostFunctions*)data)
            ->getLedgerEntryField(type, lkData.value(), fname.value());
    if (!fieldData)
        return WasmEdge_Result_Fail;
    auto pointer = setData(fm, fieldData.value());
    if (!pointer)
        return pointer.error();

    out[0] = pointer.value();
    //    out[1] = WasmEdge_ValueGenI32((int)fieldData.value().size());
    return WasmEdge_Result_Success;
}

WasmEdge_Result
getCurrentLedgerEntryField(
    void* data,
    const WasmEdge_CallingFrameContext* fm,
    const WasmEdge_Value* in,
    WasmEdge_Value* out)
{
    auto fname = getFieldName(fm, in, 0);
    if (!fname)
        return fname.error();

    auto fieldData =
        ((HostFunctions*)data)->getCurrentLedgerEntryField(fname.value());
    if (!fieldData)
        return WasmEdge_Result_Fail;

    auto pointer = setData(fm, fieldData.value());
    if (!pointer)
        return pointer.error();

    out[0] = pointer.value();
    //    out[1] = WasmEdge_ValueGenI32((int)fieldData.value().size());
    return WasmEdge_Result_Success;
}

WasmEdge_Result
getNFT(
    void* data,
    const WasmEdge_CallingFrameContext* fm,
    const WasmEdge_Value* in,
    WasmEdge_Value* out)
{
    auto account = getFieldName(fm, in, 0);
    if (!account)
        return account.error();

    auto nftId = getFieldName(fm, in, 2);
    if (!nftId)
        return nftId.error();

    auto nftURI =
        ((HostFunctions*)data)->getNFT(account.value(), nftId.value());
    if (!nftURI)
        return WasmEdge_Result_Fail;

    auto pointer = setData(fm, nftURI.value());
    if (!pointer)
        return pointer.error();

    out[0] = pointer.value();
    //    out[1] = WasmEdge_ValueGenI32((int)nftURI.value().size());
    return WasmEdge_Result_Success;
}

WasmEdge_Result
accountKeylet(
    void* data,
    const WasmEdge_CallingFrameContext* fm,
    const WasmEdge_Value* in,
    WasmEdge_Value* out)
{
    auto account = getFieldName(fm, in, 0);
    if (!account)
        return account.error();

    auto keylet = ((HostFunctions*)data)->accountKeylet(account.value());
    if (!keylet)
        return WasmEdge_Result_Fail;

    auto pointer = setData(fm, keylet.value());
    if (!pointer)
        return pointer.error();

    out[0] = pointer.value();
    //    out[1] = WasmEdge_ValueGenI32((int)nftURI.value().size());
    return WasmEdge_Result_Success;
}

WasmEdge_Result
credentialKeylet(
    void* data,
    const WasmEdge_CallingFrameContext* fm,
    const WasmEdge_Value* in,
    WasmEdge_Value* out)
{
    auto subject = getFieldName(fm, in, 0);
    if (!subject)
        return subject.error();

    auto issuer = getFieldName(fm, in, 2);
    if (!issuer)
        return issuer.error();

    auto credentialType = getFieldName(fm, in, 4);
    if (!credentialType)
        return credentialType.error();

    auto keylet =
        ((HostFunctions*)data)
            ->credentialKeylet(
                subject.value(), issuer.value(), credentialType.value());
    if (!keylet)
        return WasmEdge_Result_Fail;

    auto pointer = setData(fm, keylet.value());
    if (!pointer)
        return pointer.error();

    out[0] = pointer.value();
    //    out[1] = WasmEdge_ValueGenI32((int)nftURI.value().size());
    return WasmEdge_Result_Success;
}

WasmEdge_Result
escrowKeylet(
    void* data,
    const WasmEdge_CallingFrameContext* fm,
    const WasmEdge_Value* in,
    WasmEdge_Value* out)
{
    auto account = getFieldName(fm, in, 0);
    if (!account)
        return account.error();

    auto sequence = WasmEdge_ValueGetI32(in[2]);

    auto keylet =
        ((HostFunctions*)data)->escrowKeylet(account.value(), sequence);
    if (!keylet)
        return WasmEdge_Result_Fail;

    auto pointer = setData(fm, keylet.value());
    if (!pointer)
        return pointer.error();

    out[0] = pointer.value();
    //    out[1] = WasmEdge_ValueGenI32((int)nftURI.value().size());
    return WasmEdge_Result_Success;
}

WasmEdge_Result
oracleKeylet(
    void* data,
    const WasmEdge_CallingFrameContext* fm,
    const WasmEdge_Value* in,
    WasmEdge_Value* out)
{
    auto account = getFieldName(fm, in, 0);
    if (!account)
        return account.error();

    auto documentId = WasmEdge_ValueGetI32(in[2]);

    auto keylet =
        ((HostFunctions*)data)->escrowKeylet(account.value(), documentId);
    if (!keylet)
        return WasmEdge_Result_Fail;

    auto pointer = setData(fm, keylet.value());
    if (!pointer)
        return pointer.error();

    out[0] = pointer.value();
    //    out[1] = WasmEdge_ValueGenI32((int)nftURI.value().size());
    return WasmEdge_Result_Success;
}

WasmEdge_Result
updateData(
    void* data,
    const WasmEdge_CallingFrameContext* fm,
    const WasmEdge_Value* in,
    WasmEdge_Value* out)
{
    auto fname = getParameterData(fm, in, 0);
    if (!fname)
        return fname.error();

    if (((HostFunctions*)data)->updateData(fname.value()))
        return WasmEdge_Result_Success;
    else
        return WasmEdge_Result_Fail;
}

WasmEdge_Result
computeSha512HalfHash(
    void* data,
    const WasmEdge_CallingFrameContext* fm,
    const WasmEdge_Value* in,
    WasmEdge_Value* out)
{
    auto fname = getParameterData(fm, in, 0);
    if (!fname)
        return fname.error();

    auto hres = ((HostFunctions*)data)->computeSha512HalfHash(fname.value());
    Bytes digest{hres.begin(), hres.end()};
    auto pointer = setData(fm, digest);
    if (!pointer)
        return pointer.error();

    out[0] = pointer.value();
    //    out[1] = WasmEdge_ValueGenI32(32);
    return WasmEdge_Result_Success;
}

WasmEdge_Result
print(
    void* data,
    const WasmEdge_CallingFrameContext* fm,
    const WasmEdge_Value* in,
    WasmEdge_Value* out)
{
    auto f = getParameterData(fm, in, 0);
    if (!f)
        return f.error();
    std::string s(f.value().begin(), f.value().end());
    std::cout << s << std::endl;
    return WasmEdge_Result_Success;
}

Expected<EscrowResult, TER>
runEscrowWasm(
    std::vector<uint8_t> const& wasmCode,
    std::string const& funcName,
    HostFunctions* hfs,
    uint64_t gasLimit)
{
    //  WasmEdge_LogOff();
    // TODO deletes
    //  create VM and set cost limit
    WasmEdge_ConfigureContext* conf = WasmEdge_ConfigureCreate();
    WasmEdge_ConfigureStatisticsSetInstructionCounting(conf, true);
    WasmEdge_ConfigureStatisticsSetCostMeasuring(conf, true);
    WasmEdge_ConfigureSetMaxMemoryPage(conf, MAX_PAGES);

    WasmEdge_VMContext* VMCxt = WasmEdge_VMCreate(conf, NULL);
    WasmEdge_StatisticsContext* StatCxt =
        WasmEdge_VMGetStatisticsContext(VMCxt);
    WasmEdge_StatisticsSetCostLimit(StatCxt, gasLimit);

    {  // register host function
        // module
        WasmEdge_String libName = WasmEdge_StringCreateByCString("host_lib");
        WasmEdge_ModuleInstanceContext* hostMod =
            WasmEdge_ModuleInstanceCreate(libName);
        WasmEdge_StringDelete(libName);

        // getLedgerSqn
        {
            WasmEdge_ValType returnList[1] = {WasmEdge_ValTypeGenI32()};
            WasmEdge_FunctionTypeContext* hostFuncType =
                WasmEdge_FunctionTypeCreate(NULL, 0, returnList, 1);
            WasmEdge_FunctionInstanceContext* hostFunc =
                WasmEdge_FunctionInstanceCreate(
                    hostFuncType, getLedgerSqn, hfs, 100);
            //            WasmEdge_FunctionTypeDelete(hostFuncType);

            WasmEdge_String fName =
                WasmEdge_StringCreateByCString("getLedgerSqn");
            WasmEdge_ModuleInstanceAddFunction(hostMod, fName, hostFunc);
            //            WasmEdge_StringDelete(fName);
            //            WasmEdge_FunctionInstanceDelete(hostFunc);
        }

        // getParentLedgerTime
        {
            WasmEdge_ValType returnList[1] = {WasmEdge_ValTypeGenI32()};
            WasmEdge_FunctionTypeContext* hostFuncType =
                WasmEdge_FunctionTypeCreate(NULL, 0, returnList, 1);
            WasmEdge_FunctionInstanceContext* hostFunc =
                WasmEdge_FunctionInstanceCreate(
                    hostFuncType, getParentLedgerTime, hfs, 100);
            //            WasmEdge_FunctionTypeDelete(hostFuncType);

            WasmEdge_String fName =
                WasmEdge_StringCreateByCString("getParentLedgerTime");
            WasmEdge_ModuleInstanceAddFunction(hostMod, fName, hostFunc);
            //            WasmEdge_StringDelete(fName);
            //            WasmEdge_FunctionInstanceDelete(hostFunc);
        }

        // getTxField
        {
            WasmEdge_ValType inputList[2] = {
                WasmEdge_ValTypeGenI32(), WasmEdge_ValTypeGenI32()};
            WasmEdge_ValType returnList[1] = {WasmEdge_ValTypeGenI32()};
            WasmEdge_FunctionTypeContext* hostFuncType =
                WasmEdge_FunctionTypeCreate(inputList, 2, returnList, 1);
            WasmEdge_FunctionInstanceContext* hostFunc =
                WasmEdge_FunctionInstanceCreate(
                    hostFuncType, getTxField, hfs, 100);
            //            WasmEdge_FunctionTypeDelete(hostFuncType);
            //            WasmEdge_FunctionInstanceDelete(hostFunc);

            WasmEdge_String fName =
                WasmEdge_StringCreateByCString("getTxField");
            WasmEdge_ModuleInstanceAddFunction(hostMod, fName, hostFunc);
            //            WasmEdge_StringDelete(fName);
        }
        // getLedgerEntryField
        {
            WasmEdge_ValType inputList[5] = {
                WasmEdge_ValTypeGenI32(),
                WasmEdge_ValTypeGenI32(),
                WasmEdge_ValTypeGenI32(),
                WasmEdge_ValTypeGenI32(),
                WasmEdge_ValTypeGenI32()};
            WasmEdge_ValType returnList[1] = {WasmEdge_ValTypeGenI32()};
            WasmEdge_FunctionTypeContext* hostFuncType =
                WasmEdge_FunctionTypeCreate(inputList, 5, returnList, 1);
            WasmEdge_FunctionInstanceContext* hostFunc =
                WasmEdge_FunctionInstanceCreate(
                    hostFuncType, getLedgerEntryField, hfs, 100);
            //            WasmEdge_FunctionTypeDelete(hostFuncType);
            //            WasmEdge_FunctionInstanceDelete(hostFunc);

            WasmEdge_String fName =
                WasmEdge_StringCreateByCString("getLedgerEntryField");
            WasmEdge_ModuleInstanceAddFunction(hostMod, fName, hostFunc);
            //            WasmEdge_StringDelete(fName);
        }
        // getCurrentLedgerEntryField
        {
            WasmEdge_ValType inputList[2] = {
                WasmEdge_ValTypeGenI32(), WasmEdge_ValTypeGenI32()};
            WasmEdge_ValType returnList[1] = {WasmEdge_ValTypeGenI32()};
            WasmEdge_FunctionTypeContext* hostFuncType =
                WasmEdge_FunctionTypeCreate(inputList, 2, returnList, 1);
            WasmEdge_FunctionInstanceContext* hostFunc =
                WasmEdge_FunctionInstanceCreate(
                    hostFuncType, getCurrentLedgerEntryField, hfs, 100);
            //            WasmEdge_FunctionTypeDelete(hostFuncType);
            //            WasmEdge_FunctionInstanceDelete(hostFunc);

            WasmEdge_String fName =
                WasmEdge_StringCreateByCString("getCurrentLedgerEntryField");
            WasmEdge_ModuleInstanceAddFunction(hostMod, fName, hostFunc);
            //            WasmEdge_StringDelete(fName);
        }
        // getNFT
        {
            WasmEdge_ValType inputList[4] = {
                WasmEdge_ValTypeGenI32(),
                WasmEdge_ValTypeGenI32(),
                WasmEdge_ValTypeGenI32(),
                WasmEdge_ValTypeGenI32()};
            WasmEdge_ValType returnList[1] = {WasmEdge_ValTypeGenI32()};
            WasmEdge_FunctionTypeContext* hostFuncType =
                WasmEdge_FunctionTypeCreate(inputList, 2, returnList, 1);
            WasmEdge_FunctionInstanceContext* hostFunc =
                WasmEdge_FunctionInstanceCreate(hostFuncType, getNFT, hfs, 100);
            //            WasmEdge_FunctionTypeDelete(hostFuncType);
            //            WasmEdge_FunctionInstanceDelete(hostFunc);

            WasmEdge_String fName = WasmEdge_StringCreateByCString("getNFT");
            WasmEdge_ModuleInstanceAddFunction(hostMod, fName, hostFunc);
            //            WasmEdge_StringDelete(fName);
        }
        // updateData
        {
            WasmEdge_ValType inputList[2] = {
                WasmEdge_ValTypeGenI32(), WasmEdge_ValTypeGenI32()};
            WasmEdge_FunctionTypeContext* hostFuncType =
                WasmEdge_FunctionTypeCreate(inputList, 2, NULL, 0);
            WasmEdge_FunctionInstanceContext* hostFunc =
                WasmEdge_FunctionInstanceCreate(
                    hostFuncType, updateData, hfs, 100);
            //            WasmEdge_FunctionTypeDelete(hostFuncType);
            //            WasmEdge_FunctionInstanceDelete(hostFunc);

            WasmEdge_String fName =
                WasmEdge_StringCreateByCString("updateData");
            WasmEdge_ModuleInstanceAddFunction(hostMod, fName, hostFunc);
            //            WasmEdge_StringDelete(fName);
        }
        // computeSha512HalfHash
        {
            WasmEdge_ValType inputList[2] = {
                WasmEdge_ValTypeGenI32(), WasmEdge_ValTypeGenI32()};
            WasmEdge_ValType returnList[1] = {WasmEdge_ValTypeGenI32()};
            WasmEdge_FunctionTypeContext* hostFuncType =
                WasmEdge_FunctionTypeCreate(inputList, 2, returnList, 1);
            WasmEdge_FunctionInstanceContext* hostFunc =
                WasmEdge_FunctionInstanceCreate(
                    hostFuncType, computeSha512HalfHash, hfs, 100);
            //            WasmEdge_FunctionTypeDelete(hostFuncType);
            //            WasmEdge_FunctionInstanceDelete(hostFunc);

            WasmEdge_String fName =
                WasmEdge_StringCreateByCString("computeSha512HalfHash");
            WasmEdge_ModuleInstanceAddFunction(hostMod, fName, hostFunc);
            //            WasmEdge_StringDelete(fName);
        }
        // accountKeylet
        {
            WasmEdge_ValType inputList[2] = {
                WasmEdge_ValTypeGenI32(), WasmEdge_ValTypeGenI32()};
            WasmEdge_ValType returnList[1] = {WasmEdge_ValTypeGenI32()};
            WasmEdge_FunctionTypeContext* hostFuncType =
                WasmEdge_FunctionTypeCreate(inputList, 2, returnList, 1);
            WasmEdge_FunctionInstanceContext* hostFunc =
                WasmEdge_FunctionInstanceCreate(
                    hostFuncType, accountKeylet, hfs, 100);
            //            WasmEdge_FunctionTypeDelete(hostFuncType);
            //            WasmEdge_FunctionInstanceDelete(hostFunc);

            WasmEdge_String fName =
                WasmEdge_StringCreateByCString("accountKeylet");
            WasmEdge_ModuleInstanceAddFunction(hostMod, fName, hostFunc);
            //            WasmEdge_StringDelete(fName);
        }
        // credentialKeylet
        {
            WasmEdge_ValType inputList[6] = {
                WasmEdge_ValTypeGenI32(),
                WasmEdge_ValTypeGenI32(),
                WasmEdge_ValTypeGenI32(),
                WasmEdge_ValTypeGenI32(),
                WasmEdge_ValTypeGenI32(),
                WasmEdge_ValTypeGenI32()};
            WasmEdge_ValType returnList[1] = {WasmEdge_ValTypeGenI32()};
            WasmEdge_FunctionTypeContext* hostFuncType =
                WasmEdge_FunctionTypeCreate(inputList, 6, returnList, 1);
            WasmEdge_FunctionInstanceContext* hostFunc =
                WasmEdge_FunctionInstanceCreate(
                    hostFuncType, credentialKeylet, hfs, 100);
            //            WasmEdge_FunctionTypeDelete(hostFuncType);
            //            WasmEdge_FunctionInstanceDelete(hostFunc);
            WasmEdge_String fName =
                WasmEdge_StringCreateByCString("credentialKeylet");
            WasmEdge_ModuleInstanceAddFunction(hostMod, fName, hostFunc);
            //            WasmEdge_StringDelete(fName);
        }
        // escrowKeylet
        {
            WasmEdge_ValType inputList[4] = {
                WasmEdge_ValTypeGenI32(),
                WasmEdge_ValTypeGenI32(),
                WasmEdge_ValTypeGenI32()};
            WasmEdge_ValType returnList[1] = {WasmEdge_ValTypeGenI32()};
            WasmEdge_FunctionTypeContext* hostFuncType =
                WasmEdge_FunctionTypeCreate(inputList, 3, returnList, 1);
            WasmEdge_FunctionInstanceContext* hostFunc =
                WasmEdge_FunctionInstanceCreate(
                    hostFuncType, escrowKeylet, hfs, 100);
            //            WasmEdge_FunctionTypeDelete(hostFuncType);
            //            WasmEdge_FunctionInstanceDelete(hostFunc);

            WasmEdge_String fName =
                WasmEdge_StringCreateByCString("escrowKeylet");
            WasmEdge_ModuleInstanceAddFunction(hostMod, fName, hostFunc);
            //            WasmEdge_StringDelete(fName);
        }
        // oracleKeylet
        {
            WasmEdge_ValType inputList[3] = {
                WasmEdge_ValTypeGenI32(),
                WasmEdge_ValTypeGenI32(),
                WasmEdge_ValTypeGenI32()};
            WasmEdge_ValType returnList[1] = {WasmEdge_ValTypeGenI32()};
            WasmEdge_FunctionTypeContext* hostFuncType =
                WasmEdge_FunctionTypeCreate(inputList, 3, returnList, 1);
            WasmEdge_FunctionInstanceContext* hostFunc =
                WasmEdge_FunctionInstanceCreate(
                    hostFuncType, oracleKeylet, hfs, 100);
            //            WasmEdge_FunctionTypeDelete(hostFuncType);
            //            WasmEdge_FunctionInstanceDelete(hostFunc);

            WasmEdge_String fName =
                WasmEdge_StringCreateByCString("oracleKeylet");
            WasmEdge_ModuleInstanceAddFunction(hostMod, fName, hostFunc);
            //            WasmEdge_StringDelete(fName);
        }
        // print
        {
            WasmEdge_ValType inputList[2] = {
                WasmEdge_ValTypeGenI32(), WasmEdge_ValTypeGenI32()};
            WasmEdge_FunctionTypeContext* hostFuncType =
                WasmEdge_FunctionTypeCreate(inputList, 2, NULL, 0);
            WasmEdge_FunctionInstanceContext* hostFunc =
                WasmEdge_FunctionInstanceCreate(hostFuncType, print, hfs, 100);
            //            WasmEdge_FunctionTypeDelete(hostFuncType);
            //            WasmEdge_FunctionInstanceDelete(hostFunc);

            WasmEdge_String fName = WasmEdge_StringCreateByCString("print");
            WasmEdge_ModuleInstanceAddFunction(hostMod, fName, hostFunc);
            //            WasmEdge_StringDelete(fName);
        }
        WasmEdge_Result regRe =
            WasmEdge_VMRegisterModuleFromImport(VMCxt, hostMod);
        if (!WasmEdge_ResultOK(regRe))
        {
            printf("host func reg error\n");
            return Unexpected<TER>(tecFAILED_PROCESSING);
        }
    }

    WasmEdge_Result loadRes =
        WasmEdge_VMLoadWasmFromBuffer(VMCxt, wasmCode.data(), wasmCode.size());
    if (!WasmEdge_ResultOK(loadRes))
    {
        printf("load error, %p, %d\n", wasmCode.data(), wasmCode.size());
        return Unexpected<TER>(tecFAILED_PROCESSING);
    }
    WasmEdge_Result validateRes = WasmEdge_VMValidate(VMCxt);
    if (!WasmEdge_ResultOK(validateRes))
    {
        printf("validate error\n");
        return Unexpected<TER>(tecFAILED_PROCESSING);
    }
    WasmEdge_Result instantiateRes = WasmEdge_VMInstantiate(VMCxt);
    if (!WasmEdge_ResultOK(instantiateRes))
    {
        printf("instantiate error\n");
        return Unexpected<TER>(tecFAILED_PROCESSING);
    }
    WasmEdge_Value funcReturns[1];
    WasmEdge_String func = WasmEdge_StringCreateByCString(funcName.c_str());
    WasmEdge_Result funcRes =
        WasmEdge_VMExecute(VMCxt, func, NULL, 0, funcReturns, 1);

    bool ok = WasmEdge_ResultOK(funcRes);
    EscrowResult re;
    if (ok)
    {
        auto sc = WasmEdge_VMGetStatisticsContext(VMCxt);
        re.cost = WasmEdge_StatisticsGetTotalCost(sc);
        // WasmEdge_StatisticsGetTotalCost, WasmEdge_StatisticsGetInstrCount
        auto result = WasmEdge_ValueGetI32(funcReturns[0]);
        if (result != 0)
            re.result = true;
        else
            re.result = false;
    }
    else
    {
        printf("Error message: %s\n", WasmEdge_ResultGetMessage(funcRes));
    }

    WasmEdge_VMDelete(VMCxt);
    WasmEdge_StringDelete(func);
    // delete other obj allocated
    if (ok)
        return re;
    else
        return Unexpected<TER>(tecFAILED_PROCESSING);
}
}  // namespace ripple
