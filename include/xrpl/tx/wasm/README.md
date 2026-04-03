# WASM Module for Programmable Escrows

This module provides WebAssembly (WASM) execution capabilities for programmable
escrows on the XRP Ledger. When an escrow is finished, the WASM code runs to
determine whether the escrow conditions are met, enabling custom programmable
logic for escrow release conditions.

For the full specification, see
[XLS-0102: WASM VM](https://xls.xrpl.org/xls/XLS-0102-wasm-vm.html).

## Architecture

The module follows a layered architecture:

```
┌─────────────────────────────────────────────────────────────┐
│                 WasmEngine (WasmVM.h)                       │
│         runEscrowWasm(), preflightEscrowWasm()              │
│              Host function registration                     │
├─────────────────────────────────────────────────────────────┤
│                 WasmiEngine (WasmiVM.h)                     │
│            Low-level wasmi interpreter integration          │
├─────────────────────────────────────────────────────────────┤
│    HostFuncWrapper          │       HostFuncImpl            │
│   C-style WASM bridges      │   C++ implementations         │
├─────────────────────────────────────────────────────────────┤
│                   HostFunc (Interface)                      │
│            Abstract base class for host functions           │
└─────────────────────────────────────────────────────────────┘
```

### Key Components

- **`WasmVM.h` / `detail/WasmVM.cpp`** - High-level facade providing:
  - `WasmEngine` singleton that wraps the underlying WASM interpreter
  - `runEscrowWasm()` - Execute WASM code for escrow finish
  - `preflightEscrowWasm()` - Validate WASM code during preflight
  - `createWasmImport()` - Register all host functions

- **`WasmiVM.h` / `detail/WasmiVM.cpp`** - Low-level integration with the
  [wasmi](https://github.com/wasmi-labs/wasmi) WebAssembly interpreter:
  - `WasmiEngine` - Manages WASM modules, instances, and execution
  - Memory management and gas metering
  - Function invocation and result handling

- **`HostFunc.h`** - Abstract `HostFunctions` base class defining the interface
  for all callable host functions. Each method returns
  `Expected<T, HostFunctionError>`.

- **`HostFuncImpl.h` / `detail/HostFuncImpl*.cpp`** - Concrete
  `WasmHostFunctionsImpl` class that implements host functions with access to
  `ApplyContext` for ledger state queries. Implementation split across files:
  - `HostFuncImpl.cpp` - Core utilities (updateData, checkSignature, etc.)
  - `HostFuncImplFloat.cpp` - Float/number arithmetic operations
  - `HostFuncImplGetter.cpp` - Field access (transaction, ledger objects)
  - `HostFuncImplKeylet.cpp` - Keylet construction functions
  - `HostFuncImplLedgerHeader.cpp` - Ledger header info access
  - `HostFuncImplNFT.cpp` - NFT-related queries
  - `HostFuncImplTrace.cpp` - Debugging/tracing functions

- **`HostFuncWrapper.h` / `detail/HostFuncWrapper.cpp`** - C-style wrapper
  functions that bridge WASM calls to C++ `HostFunctions` methods. Each host
  function has:
  - A `_proto` type alias defining the function signature
  - A `_wrap` function that extracts parameters and calls the implementation

- **`ParamsHelper.h`** - Utilities for WASM parameter handling:
  - `WASM_IMPORT_FUNC` / `WASM_IMPORT_FUNC2` macros for registration
  - `wasmParams()` helper for building parameter vectors
  - Type conversion between WASM and C++ types

## Host Functions

Host functions allow WASM code to interact with the XRP Ledger. They are
organized into categories:

- **Ledger Information** - Access ledger sequence, timestamps, hashes, fees
- **Transaction & Ledger Object Access** - Read fields from the transaction
  and ledger objects (including the current escrow object)
- **Keylet Construction** - Build keylets to look up various ledger object types
- **Cryptography** - Signature verification and hashing
- **Float Arithmetic** - Mathematical operations for amount calculations
- **NFT Operations** - Query NFT properties
- **Tracing/Debugging** - Log messages for debugging

For the complete list of available host functions, their WASM names, and gas
costs, see the [XLS-0102 specification](https://xls.xrpl.org/xls/XLS-0102-wasm-vm.html)
or `detail/WasmVM.cpp` where they are registered via `WASM_IMPORT_FUNC2` macros.
For method signatures, see `HostFunc.h`.

## Gas Model

Each host function has an associated gas cost. The gas cost is specified when
registering the function in `detail/WasmVM.cpp`:

```cpp
WASM_IMPORT_FUNC2(i, getLedgerSqn, "get_ledger_sqn", hfs, 60);
//                                                        ^^ gas cost
```

WASM execution is metered, and if the gas limit is exceeded, execution fails.

## Entry Point

The WASM module must export a function with the name defined by
`ESCROW_FUNCTION_NAME` (currently `"finish"`). This function:

- Takes no parameters (or parameters passed via host function calls)
- Returns an `int32_t`:
  - `1` (or positive): Escrow conditions are met, allow finish
  - `0` (or negative): Escrow conditions are not met, reject finish

## Adding a New Host Function

To add a new host function, follow these steps:

### 1. Add to HostFunc.h (Base Class)

Add a virtual method declaration with a default implementation that returns an
error:

```cpp
virtual Expected<ReturnType, HostFunctionError>
myNewFunction(ParamType1 param1, ParamType2 param2)
{
    return Unexpected(HostFunctionError::INTERNAL);
}
```

### 2. Add to HostFuncImpl.h (Declaration)

Add the method override declaration in `WasmHostFunctionsImpl`:

```cpp
Expected<ReturnType, HostFunctionError>
myNewFunction(ParamType1 param1, ParamType2 param2) override;
```

### 3. Implement in detail/HostFuncImpl\*.cpp

Add the implementation in the appropriate file:

```cpp
Expected<ReturnType, HostFunctionError>
WasmHostFunctionsImpl::myNewFunction(ParamType1 param1, ParamType2 param2)
{
    // Implementation using ctx (ApplyContext) for ledger access
    return result;
}
```

### 4. Add Wrapper to HostFuncWrapper.h

Add the prototype and wrapper declaration:

```cpp
using myNewFunction_proto = int32_t(uint8_t const*, int32_t, ...);
wasm_trap_t*
myNewFunction_wrap(void* env, wasm_val_vec_t const* params, wasm_val_vec_t* results);
```

### 5. Implement Wrapper in detail/HostFuncWrapper.cpp

Implement the C-style wrapper that bridges WASM to C++:

```cpp
wasm_trap_t*
myNewFunction_wrap(void* env, wasm_val_vec_t const* params, wasm_val_vec_t* results)
{
    // Extract parameters from params
    // Call hfs->myNewFunction(...)
    // Set results and return
}
```

### 6. Register in WasmVM.cpp

Add the function registration in `setCommonHostFunctions()` or
`createWasmImport()`:

```cpp
WASM_IMPORT_FUNC2(i, myNewFunction, "my_new_function", hfs, 100);
//                                   ^^ WASM name       ^^ gas cost
```

> [!IMPORTANT]
> New host functions MUST be amendment-gated in `WasmVM.cpp`.
> Wrap the registration in an amendment check to ensure the function is only
> available after the corresponding amendment is enabled on the network.
