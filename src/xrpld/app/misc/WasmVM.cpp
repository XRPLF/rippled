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

}  // namespace ripple
