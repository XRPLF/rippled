# Lean 4 ↔ rippled C++ FFI Bridge

How to expose Lean 4 functions to rippled's C++ test suite for cross-validation.

## Overview

Lean 4 compiles to C, which produces object files that can be linked into any C/C++
binary. We use this to call Lean functions directly from rippled's `xrpld` unit tests,
comparing Lean and C++ outputs on the same inputs.

```
┌─────────────────────────────────────────────────┐
│  xrpld binary                                   │
│                                                  │
│  LeanNumber_test.cpp                             │
│    │                                             │
│    ├── calls C++ xrpl::Number::operator*()       │
│    │     (from libxrpl.a, already in xrpld)      │
│    │                                             │
│    └── calls lean_number_mul()                   │
│          (from libXRPL_XRPL.a, linked in)        │
│                                                  │
│    → compares results field by field             │
└─────────────────────────────────────────────────┘
```

## The FFI Problem

Lean 4 uses `Nat`, `Int`, `Bool` internally — these are heap-allocated `lean_object*`
pointers in the C ABI. Constructing them from C++ requires calling Lean runtime functions
(`lean_uint64_to_nat`, `lean_alloc_ctor`, etc.) and managing reference counts. This is
error-prone and couples the C++ code to Lean's internal object layout.

## All-Scalar Wrappers

Create Lean FFI modules with `@[export]` functions that:
- **Accept** only C-native scalar types (`UInt64`, `Int64`, `UInt8`)
- **Return** a struct containing only scalar fields
- Internally convert to/from the real Lean types

Shared structures + decoders/encoders live in `XRPL/FFI/CommonFFI.lean`; per-type
exports live in `XRPL/FFI/Protocol/<Type>FFI.lean` (e.g. `XRPL/FFI/Protocol/NumberFFI.lean`,
`XRPL/FFI/Protocol/STAmountFFI.lean`).

This way C++ never touches `lean_object*` for inputs, and only needs simple byte-offset
reads for outputs.

### Example: Number multiplication

**Lean side** (`XRPL/FFI/Protocol/NumberFFI.lean`):
```lean
structure FFINumberResult where
  mantissa : UInt64
  exponent : Int64
  status : UInt8      -- 0=ok, 1=error
  negative : UInt8

@[export lean_number_mul]
def lean_number_mul (neg1 : UInt8) (mant1 : UInt64) (exp1 : Int64)
    (neg2 : UInt8) (mant2 : UInt64) (exp2 : Int64) (mode : UInt8)
    : FFINumberResult :=
  let n1 := Number.unchecked (neg1 != 0) mant1.toNat exp1.toInt
  let n2 := Number.unchecked (neg2 != 0) mant2.toNat exp2.toInt
  encodeResult (Number.operator_mul n1 n2 largeRange (decodeMode mode))
```

**C++ side** (calling it):
```cpp
// Declare the symbol (Lean Int64 compiles to uint64_t in C ABI)
extern "C" lean_object* lean_number_mul(
    uint8_t neg1, uint64_t mant1, uint64_t exp1,
    uint8_t neg2, uint64_t mant2, uint64_t exp2,
    uint8_t mode);

// Call it
lean_object* obj = lean_number_mul(0, 2000000000000000000ULL, 0,
                                   0, 3000000000000000000ULL, 0, 0);

// Read the result struct (all scalar — just byte offsets)
uint64_t mantissa = lean_ctor_get_uint64(obj, 0);    // offset 0
int64_t  exponent = (int64_t)lean_ctor_get_uint64(obj, 8);  // offset 8
uint8_t  status   = lean_ctor_get_uint8(obj, 16);    // offset 16
uint8_t  negative = lean_ctor_get_uint8(obj, 17);    // offset 17
lean_dec(obj);  // free the heap-allocated result
```

## Object-Handle Wrappers (ledger types)

Ledger entries have too many fields to flatten into a scalar struct. So instead of
copying fields across, C++ asks Lean to build the value and keeps the returned
`lean_object*` as a handle it never looks inside — to read a field, it calls a getter.

For example, `XRPL/FFI/Protocol/AccountRootFFI.lean` exports a builder and getters:

```lean
@[export lean_account_root_build]
def lean_account_root_build (account : AccountID) (balance : STAmount) … : AccountRoot

@[export lean_account_root_balance]
def lean_account_root_balance (a : AccountRoot) : STAmount := a.balance
```

So C++ calls `lean_account_root_build(...)` to get a handle, then later
`lean_account_root_balance(handle)` to read the balance back.

The ledger works the same way — `XRPL/FFI/Ledger/LedgerFFI.lean` has
`lean_ledger_create_empty` and, per entry, `add` / `fetch` / `fetch_all`; modeled
functions live in `Helpers/TokenHelpersFFI.lean` (e.g. `lean_can_add_holding`).
Entries live in one `uint256 → entry` map (rippled's SHAMap), so `fetch` takes the
entry's key, not its identity fields. That key is the keylet, and Lean computes it
itself in `Protocol/Indexes.lean`.
When passing values in, ids and `Blob` go as `ByteArray`, `Option` stays an `Option`,
and a sum type is a `kind` byte plus one getter per case.

`XRPAmount` and `MPTAmount` are single-`Int64` structs, so Lean unboxes them: they
cross as a signed `int64_t` (drops / value), not a handle. `XRPAmountFFI`/`MPTAmountFFI`
just `build`/`read` that scalar.

On the C++ side (`src/test/lean4/ffi/`) each type gets a wrapper class that holds the
handle and offers `build()` / `read()` / getters, so the tests stay in plain C++.

## Calling C++ from Lean (just one function)

The traffic is almost all one way (Lean → C++). The one exception is hashing: every
keylet ends in a SHA-512Half, and instead of reimplementing it Lean declares
`@[extern "cpp_sha_512_half"]` (`Protocol/Digest.lean`) and rippled supplies the symbol
(`ffi/cpp/Sha512HalfFFI.cpp`).

## Lean Runtime Initialization

Before calling any exported function, initialize the Lean runtime once:

```cpp
// 1. Boot the runtime (GC, allocator)
extern "C" void lean_initialize_runtime_module(void);  // not in lean.h, but in libleanrt.a
lean_initialize_runtime_module();

// 2. Initialize your module (transitively inits all dependencies)
extern "C" lean_object* initialize_XRPL_XRPL(uint8_t, lean_object*);
lean_object* res = initialize_XRPL_XRPL(1, lean_io_mk_world());
lean_dec_ref(res);

// 3. Switch to normal execution mode
lean_io_mark_end_initialization();
```

The initializer name follows Lake convention: `initialize_{package}_{module_path}`.
The root `XRPL` module transitively initializes all per-type FFI submodules, so
`initialize_XRPL_XRPL` is sufficient. If you only need a single submodule, pick
the matching initializer (e.g. `initialize_XRPL_FFI_Protocol_NumberFFI` for
`XRPL.FFI.Protocol.NumberFFI`).

## Build Process

### Step 1: Build Lean static library

```bash
cd ~/Projects/xrpl/xrpl-lean4
lake build XRPL:static
```

Produces `libXRPL_XRPL.a` containing your exported functions.

Make sure the FFI modules are reachable from your root module (`XRPL.lean`) — it
imports `XRPL.FFI.FFI`, which pulls in every `*FFI.lean`. Note `lake build` (no
target) only builds the oleans; you need `XRPL:static` to (re)archive the `.a`.

### Step 2: Build combined dependency archive

Your Lean code imports Mathlib, which consists of thousands of individual object files.
Combine them into one archive:

```bash
cd ~/Projects/xrpl/xrpl-lean4
find .lake/packages -name "*.o.export" > /tmp/all_lean_deps.txt
libtool -static -o .lake/build/lib/libLeanDeps.a $(cat /tmp/all_lean_deps.txt)
```

Re-run only when Mathlib version changes.

### Step 3: Link into rippled

Already wired in `cmake/XrplLean4.cmake` (applied when `tests` are on and both the
toolchain headers and the static lib exist):

```cmake
set(LEAN_TOOLCHAIN "$ENV{HOME}/.elan/toolchains/leanprover--lean4---v4.28.0")
set(XRPL_LEAN4 "$ENV{HOME}/Projects/xrpl/xrpl-lean4")

target_include_directories(xrpld PRIVATE ${LEAN_TOOLCHAIN}/include)
target_link_libraries(xrpld
    ${XRPL_LEAN4}/.lake/build/lib/libXRPL_XRPL.a     # your Lean code
    ${XRPL_LEAN4}/.lake/build/lib/libLeanDeps.a       # Mathlib + packages
    ${LEAN_TOOLCHAIN}/lib/lean/libLake.a               # Lake utilities
    ${LEAN_TOOLCHAIN}/lib/lean/libleanshared.dylib     # Lean runtime
    /opt/local/lib/libgmp.a                            # GMP (for Lean's Nat/Int)
)
```

### Step 4: Write the test

Drop a `*_test.cpp` file in `src/test/lean4/` (or wherever). It gets auto-globbed into
`xrpld`. Use `beast::unit_test::suite` and `BEAST_DEFINE_TESTSUITE` to register it.

### Step 5: Build and run

```bash
cd ~/Projects/xrpl/rippled-private/.build
cmake --build . --target xrpld -j 6
./xrpld --unittest=lean4        # runs every Lean suite (LeanNumber, LeanLedger, …)
```

## Why Static Linking (Not Shared)

`lake build XRPL:shared` fails because Mathlib has 7600+ object files — the linker
command exceeds the OS argument length limit. Static linking via `lake build XRPL:static`
plus a manually-created `libLeanDeps.a` archive avoids this.

## Adding More Functions

1. Add `@[export lean_something]` in the appropriate `XRPL/FFI/Protocol/<Type>FFI.lean`
   (or add a new shared helper to `XRPL/FFI/CommonFFI.lean`)
2. `lake build XRPL:static`
3. Add `extern "C" lean_object* lean_something(...)` in your test
4. Rebuild rippled

## Key Gotchas

- **`lean_initialize_runtime_module`** is not declared in `lean.h` — add your own `extern "C"` declaration
- **Initializer naming**: `initialize_{package}_{module}` with dots replaced by underscores
- **Lean `Int64` → C `uint64_t`**: same bit pattern, cast to `int64_t` on the C++ side
- **Return structs are `lean_object*`**: even with all-scalar fields, Lean heap-allocates them — call `lean_dec()` after reading
- **Zero representation differs**: Lean zero exponent is `-32768`, C++ is `INT_MIN` — compare mantissa only for zero values
- **GMP dependency**: Lean's `Nat`/`Int` use GMP — link `libgmp.a` (MacPorts: `/opt/local/lib/`)
- **Thread-local state**: C++ `Number` uses thread-local rounding mode and mantissa scale — set them before calling C++ operations
