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

void invoke() {
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
  WasmEdge_ModuleInstanceContext *HostModCxt =
      WasmEdge_ModuleInstanceCreate(ExportName);
  WasmEdge_ValType ParamList[2] = {WasmEdge_ValTypeGenI32(), WasmEdge_ValTypeGenI32()};
  WasmEdge_ValType ReturnList[1] = {WasmEdge_ValTypeGenI32()};
  WasmEdge_FunctionTypeContext *HostFType =
      WasmEdge_FunctionTypeCreate(ParamList, 2, ReturnList, 1);
  WasmEdge_FunctionInstanceContext *HostFunc =
      WasmEdge_FunctionInstanceCreate(HostFType, Add, NULL, 0);
  WasmEdge_FunctionTypeDelete(HostFType);
  WasmEdge_String HostFuncName = WasmEdge_StringCreateByCString("func-add");
  WasmEdge_ModuleInstanceAddFunction(HostModCxt, HostFuncName, HostFunc);
  WasmEdge_StringDelete(HostFuncName);

  WasmEdge_VMRegisterModuleFromImport(VMCxt, HostModCxt);

  /* The parameters and returns arrays. */
  WasmEdge_Value Params[2] = {WasmEdge_ValueGenI32(1234),
                              WasmEdge_ValueGenI32(5678)};
  WasmEdge_Value Returns[1];
  /* Function name. */
  WasmEdge_String FuncName = WasmEdge_StringCreateByCString("addTwo");
  /* Run the WASM function from buffer. */
  WasmEdge_Result Res = WasmEdge_VMRunWasmFromBuffer(
      VMCxt, WASM, sizeof(WASM), FuncName, Params, 2, Returns, 1);

  if (WasmEdge_ResultOK(Res)) {
    printf("Get the result: %d\n", WasmEdge_ValueGetI32(Returns[0]));
  } else {
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
    testInit()
    {
        testcase("Wasm invoke");
        invoke();
        BEAST_EXPECT(true);
    }

    void
    run() override
    {
        using namespace test::jtx;

        testInit();
    }
};

BEAST_DEFINE_TESTSUITE(Wasm, app, ripple);

}  // namespace test
}  // namespace ripple
