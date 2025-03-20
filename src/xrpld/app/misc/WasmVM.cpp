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

// WasmVM::WasmVM(beast::Journal j)
//     : j_(j)
//{
// }

namespace ripple {
Expected<bool, TER>
runEscrowWasm(
    std::vector<uint8_t> const& wasmCode,
    std::string const& funcName,
    int32_t input)
{
    WasmEdge_VMContext* VMCxt = WasmEdge_VMCreate(NULL, NULL);
    WasmEdge_Value Params[1] = {WasmEdge_ValueGenI32(input)};
    WasmEdge_Value Returns[1];
    WasmEdge_String FuncName = WasmEdge_StringCreateByCString(funcName.c_str());
    WasmEdge_Result Res = WasmEdge_VMRunWasmFromBuffer(
        VMCxt,
        wasmCode.data(),
        wasmCode.size(),
        FuncName,
        Params,
        1,
        Returns,
        1);

    bool ok = WasmEdge_ResultOK(Res);
    bool re = false;
    if (ok)
    {
        auto result = WasmEdge_ValueGetI32(Returns[0]);
        // printf("Get the result: %d\n", result);
        if (result != 0)
            re = true;
    }
    else
    {
        printf("Error message: %s\n", WasmEdge_ResultGetMessage(Res));
    }

    WasmEdge_VMDelete(VMCxt);
    WasmEdge_StringDelete(FuncName);
    if (ok)
        return re;
    else
        return Unexpected<TER>(tecFAILED_PROCESSING);
}

Expected<bool, TER>
runEscrowWasm(
    std::vector<uint8_t> const& wasmCode,
    std::string const& funcName,
    std::vector<uint8_t> const& accountID)
{
    auto dataLen = (int32_t)accountID.size();
    // printf("accountID size: %d\n", dataLen);
    WasmEdge_VMContext* VMCxt = WasmEdge_VMCreate(NULL, NULL);

    WasmEdge_Value allocParams[1] = {WasmEdge_ValueGenI32(dataLen)};
    WasmEdge_Value allocReturns[1];
    WasmEdge_String allocFunc = WasmEdge_StringCreateByCString("allocate");
    WasmEdge_Result allocRes = WasmEdge_VMRunWasmFromBuffer(
        VMCxt,
        wasmCode.data(),
        wasmCode.size(),
        allocFunc,
        allocParams,
        1,
        allocReturns,
        1);

    bool ok = WasmEdge_ResultOK(allocRes);
    bool re = false;
    if (ok)
    {
        auto pointer = WasmEdge_ValueGetI32(allocReturns[0]);
        // printf("Alloc pointer: %d\n", pointer);

        const WasmEdge_ModuleInstanceContext* m =
            WasmEdge_VMGetActiveModule(VMCxt);
        WasmEdge_String mName = WasmEdge_StringCreateByCString("memory");
        WasmEdge_MemoryInstanceContext* mi =
            WasmEdge_ModuleInstanceFindMemory(m, mName);
        WasmEdge_Result setRes = WasmEdge_MemoryInstanceSetData(
            mi, accountID.data(), pointer, dataLen);

        ok = WasmEdge_ResultOK(setRes);
        if (ok)
        {
            // printf("Set data ok\n");

            WasmEdge_Value params[2] = {
                WasmEdge_ValueGenI32(pointer), WasmEdge_ValueGenI32(dataLen)};
            WasmEdge_Value returns[1];
            WasmEdge_String func =
                WasmEdge_StringCreateByCString(funcName.c_str());
            WasmEdge_Result funcRes =
                WasmEdge_VMExecute(VMCxt, func, params, 2, returns, 1);

            ok = WasmEdge_ResultOK(funcRes);
            if (ok)
            {
                // printf("func ok\n");
                re = (WasmEdge_ValueGetI32(returns[0]) == 1);
            }
            else
            {
                printf(
                    "Func message: %s\n", WasmEdge_ResultGetMessage(funcRes));
            }
        }
        else
        {
            printf(
                "Set error message: %s\n", WasmEdge_ResultGetMessage(setRes));
        }
    }
    else
    {
        printf(
            "Alloc error message: %s\n", WasmEdge_ResultGetMessage(allocRes));
    }

    WasmEdge_VMDelete(VMCxt);
    // TODO free everything
    //    WasmEdge_StringDelete(FuncName);
    if (ok)
    {
        // printf("runEscrowWasm ok, result %d\n", re);
        return re;
    }
    else
        return Unexpected<TER>(tecFAILED_PROCESSING);
}

Expected<bool, TER>
runEscrowWasm(
    std::vector<uint8_t> const& wasmCode,
    std::string const& funcName,
    std::vector<uint8_t> const& escrow_tx_json_data,
    std::vector<uint8_t> const& escrow_lo_json_data)
{
    WasmEdge_VMContext* VMCxt = WasmEdge_VMCreate(NULL, NULL);

    WasmEdge_Result loadRes =
        WasmEdge_VMLoadWasmFromBuffer(VMCxt, wasmCode.data(), wasmCode.size());
    if (!WasmEdge_ResultOK(loadRes))
    {
        printf("load error\n");
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

    auto wasmAlloc = [VMCxt](std::vector<uint8_t> const& data) -> int32_t {
        auto dataLen = (int32_t)data.size();
        WasmEdge_Value allocParams[1] = {WasmEdge_ValueGenI32(dataLen)};
        WasmEdge_Value allocReturns[1];
        WasmEdge_String allocFunc = WasmEdge_StringCreateByCString("allocate");

        WasmEdge_Result allocRes = WasmEdge_VMExecute(
            VMCxt, allocFunc, allocParams, 1, allocReturns, 1);

        if (WasmEdge_ResultOK(allocRes))
        {
            auto pointer = WasmEdge_ValueGetI32(allocReturns[0]);
            //            printf("alloc ptr %d, len %d\n", pointer, dataLen);
            const WasmEdge_ModuleInstanceContext* m =
                WasmEdge_VMGetActiveModule(VMCxt);
            WasmEdge_String mName = WasmEdge_StringCreateByCString("memory");
            WasmEdge_MemoryInstanceContext* mi =
                WasmEdge_ModuleInstanceFindMemory(m, mName);
            WasmEdge_Result setRes = WasmEdge_MemoryInstanceSetData(
                mi, data.data(), pointer, dataLen);
            if (WasmEdge_ResultOK(setRes))
            {
                return pointer;
            }
        }

        return 0;
    };

    auto tx_ptr = wasmAlloc(escrow_tx_json_data);
    auto lo_ptr = wasmAlloc(escrow_lo_json_data);
    if (tx_ptr == 0 || lo_ptr == 0)
    {
        printf("data error\n");
        return Unexpected<TER>(tecFAILED_PROCESSING);
    }

    auto txLen = (int32_t)escrow_tx_json_data.size();
    auto loLen = (int32_t)escrow_lo_json_data.size();

    WasmEdge_Value params[4] = {
        WasmEdge_ValueGenI32(tx_ptr),
        WasmEdge_ValueGenI32(txLen),
        WasmEdge_ValueGenI32(lo_ptr),
        WasmEdge_ValueGenI32(loLen)};
    WasmEdge_Value returns[1];
    WasmEdge_String func = WasmEdge_StringCreateByCString(funcName.c_str());
    WasmEdge_Result funcRes =
        WasmEdge_VMExecute(VMCxt, func, params, 4, returns, 1);

    if (WasmEdge_ResultOK(funcRes))
    {
        // printf("func ok\n");
        return WasmEdge_ValueGetI32(returns[0]) == 1;
    }
    else
    {
        printf("Func message: %s\n", WasmEdge_ResultGetMessage(funcRes));
        return Unexpected<TER>(tecFAILED_PROCESSING);
    }
}

Expected<std::pair<bool, std::string>, TER>
runEscrowWasmP4(
    std::vector<uint8_t> const& wasmCode,
    std::string const& funcName,
    std::vector<uint8_t> const& escrow_tx_json_data,
    std::vector<uint8_t> const& escrow_lo_json_data)
{
    WasmEdge_VMContext* VMCxt = WasmEdge_VMCreate(NULL, NULL);

    WasmEdge_Result loadRes =
        WasmEdge_VMLoadWasmFromBuffer(VMCxt, wasmCode.data(), wasmCode.size());
    if (!WasmEdge_ResultOK(loadRes))
    {
        printf("load error\n");
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

    auto wasmAlloc = [VMCxt](std::vector<uint8_t> const& data) -> int32_t {
        auto dataLen = (int32_t)data.size();
        WasmEdge_Value allocParams[1] = {WasmEdge_ValueGenI32(dataLen)};
        WasmEdge_Value allocReturns[1];
        WasmEdge_String allocFunc = WasmEdge_StringCreateByCString("allocate");

        WasmEdge_Result allocRes = WasmEdge_VMExecute(
            VMCxt, allocFunc, allocParams, 1, allocReturns, 1);

        if (WasmEdge_ResultOK(allocRes))
        {
            auto pointer = WasmEdge_ValueGetI32(allocReturns[0]);
            //            printf("alloc ptr %d, len %d\n", pointer, dataLen);
            const WasmEdge_ModuleInstanceContext* m =
                WasmEdge_VMGetActiveModule(VMCxt);
            WasmEdge_String mName = WasmEdge_StringCreateByCString("memory");
            WasmEdge_MemoryInstanceContext* mi =
                WasmEdge_ModuleInstanceFindMemory(m, mName);
            WasmEdge_Result setRes = WasmEdge_MemoryInstanceSetData(
                mi, data.data(), pointer, dataLen);
            if (WasmEdge_ResultOK(setRes))
            {
                return pointer;
            }
        }

        return 0;
    };

    auto tx_ptr = wasmAlloc(escrow_tx_json_data);
    auto lo_ptr = wasmAlloc(escrow_lo_json_data);
    if (tx_ptr == 0 || lo_ptr == 0)
    {
        printf("data error\n");
        return Unexpected<TER>(tecFAILED_PROCESSING);
    }

    auto txLen = (int32_t)escrow_tx_json_data.size();
    auto loLen = (int32_t)escrow_lo_json_data.size();

    WasmEdge_Value params[4] = {
        WasmEdge_ValueGenI32(tx_ptr),
        WasmEdge_ValueGenI32(txLen),
        WasmEdge_ValueGenI32(lo_ptr),
        WasmEdge_ValueGenI32(loLen)};
    WasmEdge_Value returns[1];
    WasmEdge_String func = WasmEdge_StringCreateByCString(funcName.c_str());
    WasmEdge_Result funcRes =
        WasmEdge_VMExecute(VMCxt, func, params, 4, returns, 1);

    if (WasmEdge_ResultOK(funcRes))
    {
        auto pointer = WasmEdge_ValueGetI32(returns[0]);
        const WasmEdge_ModuleInstanceContext* m =
            WasmEdge_VMGetActiveModule(VMCxt);
        WasmEdge_String mName = WasmEdge_StringCreateByCString("memory");
        WasmEdge_MemoryInstanceContext* mi =
            WasmEdge_ModuleInstanceFindMemory(m, mName);
        uint8_t buff[9];
        WasmEdge_Result getRes =
            WasmEdge_MemoryInstanceGetData(mi, buff, pointer, 9);
        if (!WasmEdge_ResultOK(getRes))
        {
            printf(
                "re mem get message: %s\n", WasmEdge_ResultGetMessage(getRes));
            return Unexpected<TER>(tecFAILED_PROCESSING);
        }
        auto flag = buff[0];

        auto leToInt32 = [](const uint8_t* d) -> uint32_t {
            uint32_t r = 0;
            for (int i = 0; i < 4; ++i)
            {
                r |= static_cast<uint32_t>(d[i]) << (i * 8);
                //                printf("leToInt32 %d\n", r);
            }
            return r;
        };
        auto ret_pointer =
            leToInt32(reinterpret_cast<const uint8_t*>(&buff[1]));
        auto ret_len = leToInt32(reinterpret_cast<const uint8_t*>(&buff[5]));
        //        printf("re flag %d, ptr %d, len %d\n", flag, ret_pointer,
        //        ret_len);

        std::vector<uint8_t> buff2(ret_len);
        getRes = WasmEdge_MemoryInstanceGetData(
            mi, buff2.data(), ret_pointer, ret_len);
        if (!WasmEdge_ResultOK(getRes))
        {
            printf(
                "re 2 mem get message: %s\n",
                WasmEdge_ResultGetMessage(getRes));
            return Unexpected<TER>(tecFAILED_PROCESSING);
        }

        std::string newData(buff2.begin(), buff2.end());

        // free
        WasmEdge_String freeFunc = WasmEdge_StringCreateByCString("deallocate");
        WasmEdge_Value freeParams[2] = {
            WasmEdge_ValueGenI32(ret_pointer), WasmEdge_ValueGenI32(ret_len)};
        WasmEdge_Value freeReturns[0];
        WasmEdge_VMExecute(VMCxt, freeFunc, freeParams, 2, freeReturns, 0);
        // free pointer too, with len = 9 too
        freeParams[0] = WasmEdge_ValueGenI32(pointer);
        freeParams[1] = WasmEdge_ValueGenI32(9);
        WasmEdge_VMExecute(VMCxt, freeFunc, freeParams, 2, freeReturns, 0);

        return std::pair<bool, std::string>(flag == 1, newData);
    }
    else
    {
        printf("Func message: %s\n", WasmEdge_ResultGetMessage(funcRes));
        return Unexpected<TER>(tecFAILED_PROCESSING);
    }
}

WasmEdge_Result
get_ledger_sqn(
    void* data,
    const WasmEdge_CallingFrameContext*,
    const WasmEdge_Value* In,
    WasmEdge_Value* Out)
{
    Out[0] =
        WasmEdge_ValueGenI32(((LedgerDataProvider*)data)->get_ledger_sqn());
    return WasmEdge_Result_Success;
}

Expected<bool, TER>
runEscrowWasm(
    std::vector<uint8_t> const& wasmCode,
    std::string const& funcName,
    LedgerDataProvider* ledgerDataProvider)
{
    WasmEdge_VMContext* VMCxt = WasmEdge_VMCreate(NULL, NULL);
    {  // register host function
        WasmEdge_ValType ReturnList[1] = {WasmEdge_ValTypeGenI32()};
        WasmEdge_FunctionTypeContext* HostFType =
            WasmEdge_FunctionTypeCreate(NULL, 0, ReturnList, 1);
        WasmEdge_FunctionInstanceContext* HostFunc =
            WasmEdge_FunctionInstanceCreate(
                HostFType, get_ledger_sqn, ledgerDataProvider, 0);
        WasmEdge_FunctionTypeDelete(HostFType);

        WasmEdge_String HostName = WasmEdge_StringCreateByCString("host_lib");
        WasmEdge_ModuleInstanceContext* HostMod =
            WasmEdge_ModuleInstanceCreate(HostName);
        WasmEdge_StringDelete(HostName);

        WasmEdge_String HostFuncName =
            WasmEdge_StringCreateByCString("get_ledger_sqn");
        WasmEdge_ModuleInstanceAddFunction(HostMod, HostFuncName, HostFunc);
        WasmEdge_StringDelete(HostFuncName);

        WasmEdge_Result regRe =
            WasmEdge_VMRegisterModuleFromImport(VMCxt, HostMod);
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
        printf("load error\n");
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
    bool re = false;
    if (ok)
    {
        auto result = WasmEdge_ValueGetI32(funcReturns[0]);
        if (result != 0)
            re = true;
    }
    else
    {
        printf("Error message: %s\n", WasmEdge_ResultGetMessage(funcRes));
    }

    WasmEdge_VMDelete(VMCxt);
    WasmEdge_StringDelete(func);
    if (ok)
        return re;
    else
        return Unexpected<TER>(tecFAILED_PROCESSING);
}

WasmEdge_Result
constInt(
    void* data,
    const WasmEdge_CallingFrameContext*,
    const WasmEdge_Value* In,
    WasmEdge_Value* Out)
{
    Out[0] = WasmEdge_ValueGenI32(5);
    return WasmEdge_Result_Success;
}

Expected<EscrowResultP6, TER>
runEscrowWasm(
    std::vector<uint8_t> const& wasmCode,
    std::string const& funcName,
    uint32_t gasLimit,
    int32_t input)
{
    WasmEdge_ConfigureContext* conf = WasmEdge_ConfigureCreate();
    WasmEdge_ConfigureStatisticsSetInstructionCounting(conf, true);
    WasmEdge_ConfigureStatisticsSetCostMeasuring(conf, true);

    WasmEdge_VMContext* VMCxt = WasmEdge_VMCreate(conf, NULL);
    WasmEdge_StatisticsContext* StatCxt =
        WasmEdge_VMGetStatisticsContext(VMCxt);
    WasmEdge_StatisticsSetCostLimit(StatCxt, gasLimit);

    {  // register host function
        WasmEdge_ValType ReturnList[1] = {WasmEdge_ValTypeGenI32()};
        WasmEdge_FunctionTypeContext* HostFType =
            WasmEdge_FunctionTypeCreate(NULL, 0, ReturnList, 1);
        WasmEdge_FunctionInstanceContext* HostFunc =
            WasmEdge_FunctionInstanceCreate(HostFType, constInt, nullptr, 100);
        WasmEdge_FunctionTypeDelete(HostFType);

        WasmEdge_String HostName = WasmEdge_StringCreateByCString("host_lib");
        WasmEdge_ModuleInstanceContext* HostMod =
            WasmEdge_ModuleInstanceCreate(HostName);
        WasmEdge_StringDelete(HostName);

        WasmEdge_String HostFuncName =
            WasmEdge_StringCreateByCString("constInt");
        WasmEdge_ModuleInstanceAddFunction(HostMod, HostFuncName, HostFunc);
        WasmEdge_StringDelete(HostFuncName);

        WasmEdge_Result regRe =
            WasmEdge_VMRegisterModuleFromImport(VMCxt, HostMod);
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
        printf("load error\n");
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

    WasmEdge_String func = WasmEdge_StringCreateByCString(funcName.c_str());
    WasmEdge_Value Params[1] = {WasmEdge_ValueGenI32(input)};
    WasmEdge_Result funcRes =
        WasmEdge_VMExecute(VMCxt, func, Params, 1, NULL, 0);

    bool ok = WasmEdge_ResultOK(funcRes);
    EscrowResultP6 re;
    if (ok)
    {
        auto sc = WasmEdge_VMGetStatisticsContext(VMCxt);
        re.cost = WasmEdge_StatisticsGetTotalCost(sc);
        // WasmEdge_StatisticsGetTotalCost, WasmEdge_StatisticsGetInstrCount
        re.result = true;
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

/////////////// Devnet 1 /////////////////

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
    // TODO deletes
    //  create VM and set cost limit
    WasmEdge_ConfigureContext* conf = WasmEdge_ConfigureCreate();
    WasmEdge_ConfigureStatisticsSetInstructionCounting(conf, true);
    WasmEdge_ConfigureStatisticsSetCostMeasuring(conf, true);
    WasmEdge_ConfigureSetMaxMemoryPage(conf, 128);  // 8MB = 64KB*128

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
