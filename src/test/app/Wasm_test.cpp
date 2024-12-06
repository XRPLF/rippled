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

#include <test/jtx.h>
#include <iterator>
#include <wasmedge/wasmedge.h>
#include <xrpld/app/misc/WasmVM.h>

namespace ripple {
namespace test {

/* Host function body definition. */
WasmEdge_Result Add(void *Data,
                    const WasmEdge_CallingFrameContext *CallFrameCxt,
                    const WasmEdge_Value *In, WasmEdge_Value *Out) {
    int32_t Val1 = WasmEdge_ValueGetI32(In[0]);
    int32_t Val2 = WasmEdge_ValueGetI32(In[1]);
    printf("Host function \"Add\": %d + %d\n", Val1, Val2);
    Out[0] = WasmEdge_ValueGenI32(Val1 + Val2);
    return WasmEdge_Result_Success;
}

void invokeAdd() {
    /* Create the VM context. */
    WasmEdge_VMContext *VMCxt = WasmEdge_VMCreate(NULL, NULL);

    /* The WASM module buffer. */
    uint8_t WASM[] = {/* WASM header */
                    0x00, 0x61, 0x73, 0x6D, 0x01, 0x00, 0x00, 0x00,
                    /* Type section */
                    0x01, 0x07, 0x01,
                    /* function type {i32, i32} -> {i32} */
                    0x60, 0x02, 0x7F, 0x7F, 0x01, 0x7F,
                    /* Import section */
                    0x02, 0x13, 0x01,
                    /* module name: "extern" */
                    0x06, 0x65, 0x78, 0x74, 0x65, 0x72, 0x6E,
                    /* extern name: "func-add" */
                    0x08, 0x66, 0x75, 0x6E, 0x63, 0x2D, 0x61, 0x64, 0x64,
                    /* import desc: func 0 */
                    0x00, 0x00,
                    /* Function section */
                    0x03, 0x02, 0x01, 0x00,
                    /* Export section */
                    0x07, 0x0A, 0x01,
                    /* export name: "addTwo" */
                    0x06, 0x61, 0x64, 0x64, 0x54, 0x77, 0x6F,
                    /* export desc: func 0 */
                    0x00, 0x01,
                    /* Code section */
                    0x0A, 0x0A, 0x01,
                    /* code body */
                    0x08, 0x00, 0x20, 0x00, 0x20, 0x01, 0x10, 0x00, 0x0B};

    /* Create the module instance. */
    WasmEdge_String ExportName = WasmEdge_StringCreateByCString("extern");
    WasmEdge_ModuleInstanceContext* HostModCxt =
        WasmEdge_ModuleInstanceCreate(ExportName);
    WasmEdge_ValType ParamList[2] = {
        WasmEdge_ValTypeGenI32(), WasmEdge_ValTypeGenI32()};
    WasmEdge_ValType ReturnList[1] = {WasmEdge_ValTypeGenI32()};
    WasmEdge_FunctionTypeContext* HostFType =
        WasmEdge_FunctionTypeCreate(ParamList, 2, ReturnList, 1);
    WasmEdge_FunctionInstanceContext* HostFunc =
        WasmEdge_FunctionInstanceCreate(HostFType, Add, NULL, 0);
    WasmEdge_FunctionTypeDelete(HostFType);
    WasmEdge_String HostFuncName = WasmEdge_StringCreateByCString("func-add");
    WasmEdge_ModuleInstanceAddFunction(HostModCxt, HostFuncName, HostFunc);
    WasmEdge_StringDelete(HostFuncName);

    WasmEdge_VMRegisterModuleFromImport(VMCxt, HostModCxt);

    /* The parameters and returns arrays. */
    WasmEdge_Value Params[2] = {
        WasmEdge_ValueGenI32(1234), WasmEdge_ValueGenI32(5678)};
    WasmEdge_Value Returns[1];
    /* Function name. */
    WasmEdge_String FuncName = WasmEdge_StringCreateByCString("addTwo");
    /* Run the WASM function from buffer. */
    WasmEdge_Result Res = WasmEdge_VMRunWasmFromBuffer(
        VMCxt, WASM, sizeof(WASM), FuncName, Params, 2, Returns, 1);

    if (WasmEdge_ResultOK(Res))
    {
        printf("Get the result: %d\n", WasmEdge_ValueGetI32(Returns[0]));
    }
    else
    {
        printf("Error message: %s\n", WasmEdge_ResultGetMessage(Res));
    }

    /* Resources deallocations. */
    WasmEdge_VMDelete(VMCxt);
    WasmEdge_StringDelete(FuncName);
    WasmEdge_ModuleInstanceDelete(HostModCxt);
}

struct Wasm_test : public beast::unit_test::suite
{
    void
    testWasmtimeLib()
    {
        testcase("wasmtime lib test");
        invokeAdd();
        BEAST_EXPECT(true);
    }

    void
    testEscrowWasm()
    {
        testcase("escrow wasm test");
        auto wasmHex =
            "0061736d0100000001090260017f017f6000000305040001010004050170010101"
            "05030100100609017f01418080c0000b071802066d656d6f727902000b6d6f636b"
            "5f657363726f7700030a25040800200041056f450b02000b0e0010818080800010"
            "81808080000b08002000100010020b0072046e616d650011106d6f636b5f657363"
            "726f772e7761736d014404000b6d6f636b5f657363726f77010564756d6d790211"
            "5f5f7761736d5f63616c6c5f64746f7273031a6d6f636b5f657363726f772e636f"
            "6d6d616e645f6578706f7274071201000f5f5f737461636b5f706f696e74657200"
            "c0010970726f64756365727302086c616e67756167650204527573740003433131"
            "000c70726f6365737365642d62790205727573746325312e38332e302d6e696768"
            "746c79202863326637346333663920323032342d30392d30392905636c616e675f"
            "31382e312e322d776173692d73646b202868747470733a2f2f6769746875622e63"
            "6f6d2f6c6c766d2f6c6c766d2d70726f6a65637420323661316436363031643732"
            "376139366634333031643064383634376235613432373630616530632900560f74"
            "61726765745f6665617475726573052b0b62756c6b2d6d656d6f72792b0a6d756c"
            "746976616c75652b0f6d757461626c652d676c6f62616c732b0f7265666572656e"
            "63652d74797065732b087369676e2d657874";
        auto wasmStr = boost::algorithm::unhex(std::string(wasmHex));
        std::vector<uint8_t> wasm(wasmStr.begin(), wasmStr.end());
        std::string funcName("mock_escrow");
        auto re = runEscrowWasm(wasm, funcName, 15);
        if (BEAST_EXPECT(re.has_value()))
            BEAST_EXPECT(re.value());

        re = runEscrowWasm(wasm, funcName, 11);
        if (BEAST_EXPECT(re.has_value()))
            BEAST_EXPECT(!re.value());
    }

    void
    testBadWasm()
    {
        testcase("bad wasm test");
        auto wasmHex = "00000000";
        auto wasmStr = boost::algorithm::unhex(std::string(wasmHex));
        std::vector<uint8_t> wasm(wasmStr.begin(), wasmStr.end());
        std::string funcName("mock_escrow");
        auto re = runEscrowWasm(wasm, funcName, 15);
        BEAST_EXPECT(re.error());
    }

    void
    run() override
    {
        using namespace test::jtx;

        testWasmtimeLib();
        testEscrowWasm();
        testBadWasm();
    }
};

BEAST_DEFINE_TESTSUITE(Wasm, app, ripple);

}  // namespace test
}  // namespace ripple
