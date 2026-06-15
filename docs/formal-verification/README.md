# Formal Verification of XRPL in Lean 4

This directory documents how `rippled`'s formal-verification layer is built and
run. The work lives under `formal_verification/` (the Lean 4 project) and
`src/test/formal_verification/` (the C++ cross-validation tests).

## What we are doing, and why

`rippled` contains arithmetic and protocol logic that is easy to get subtly
wrong: rounding, overflow, sign handling. A unit test can only check the cases
someone thought to write. We want stronger guarantees, so we do two
complementary things:

1. **Model the C++ in Lean 4.** We re-implement a C++ algorithm as a Lean
   function, a faithful line-by-line translation. Lean is a theorem prover, so
   once the algorithm is expressed in Lean we can **prove properties about it**
   that hold for _all_ inputs (e.g. "the rounded result is within half a unit in
   the last place of the exact result").

2. **Cross-validate the model against the real C++.** A proof is only
   meaningful if the Lean model actually matches the shipping C++. So we run
   **random inputs through both and check they agree**.

The two halves reinforce each other. The proofs tell us the _modelled_
algorithm is correct and the cross-validation tells us the Lean model faithfully
represents the one from C++. A divergence in either is a real finding. When
upstream `rippled` fixed a `Number` comparison bug (#7406), our cross-validation
suite flagged the mismatch, because the Lean model still had the old behavior.

```
        ┌────────────────────────┐         proves            ┌──────────────────┐
        │  Lean model of Number  │ ───────────────────────▶  │    theorems      │
        │  (operator_mul, …)     │   "rounding is correct    │  (the Properties │
        │                        │    for all inputs"        │   modules)       │
        └───────────┬────────────┘                           └──────────────────┘
                    │ @[export] compiled to a C symbol
                    ▼
        ┌────────────────────────┐    fuzz: same inputs     ┌──────────────────┐
        │  lean_number_mul(...)  │ ◀─ assert fields equal ─▶│  C++ Number::    │
        │  (native, in xrpld)    │                          │  operator*       │
        └────────────────────────┘                          └──────────────────┘
```

---

## Terminology

| Term                      | Meaning                                                                                                           |
| ------------------------- | ----------------------------------------------------------------------------------------------------------------- |
| **Model**                 | A Lean function that re-implements a C++ algorithm (e.g. `Number.operator_mul` mirrors `Number::operator*`).      |
| **Property / theorem**    | A mathematical statement about the model, proved in Lean (e.g. a rounding bound). Lives under `XRPL/Properties/`. |
| **FFI**                   | Foreign Function Interface, the mechanism that lets C++ call the compiled Lean model.                             |
| **FFI export**            | A Lean function annotated `@[export name]`, which the Lean compiler emits as a C-callable symbol `name`.          |
| **Cross-validation test** | A C++ test that runs the same input through the Lean export and the C++ function and asserts they agree.          |
| **`lake`**                | Lean's build tool (the equivalent of `cmake`/`cargo` for Lean).                                                   |
| **`mathlib`**             | Lean's mathematics library, which the proofs depend on. Large and slow to compile.                                |

---

## Project structure

```
formal_verification/                  the Lean project (a lake package)
├── XRPL/
│   ├── Model/Protocol/Number.lean      the Lean model of C++ Number (the algorithm)
│   ├── FFI/
│   │   ├── CommonFFI.lean              shared encode/decode + the FFI result structs
│   │   └── Protocol/NumberFFI.lean     the @[export] wrappers C++ calls
│   └── Properties/…                    theorems about the model
├── lakefile.toml                       lake build config (defines the XRPL + XRPLModel libs)
├── lean-toolchain                      pins the Lean version (leanprover/lean4:v4.28.0)
└── scripts/build_lean.sh               compiles + links the model + mathlib into shared libs

external/lean4/conanfile.py             Conan recipe: the Lean toolchain (lean4)

src/test/formal_verification/           the C++ cross-validation tests
├── common/LeanSuite.h                  base test suite: Lean runtime init + threading guard
├── ffi/
│   ├── LeanObjectFFI.h                 the memory-management base class (the key abstraction)
│   └── protocol/NumberFFI.h            C++ wrapper around the Number Lean exports
└── protocol/
    ├── LeanNumber_test.cpp             the actual cross-validation suite (fuzzing + cases)
    └── helpers/                        Converters.h, Generators.h, Helpers.h

cmake/XrplLean4.cmake                    links the Lean libraries into xrpld (gated by an option)
```

The Lean side and the C++ side are deliberately separate trees. The Lean side
is the source of truth for the model and proofs and the C++ side only _consumes_
the compiled exports.

---

## The Lean model: translating C++ into something we can prove about

A model is a direct translation of the C++ algorithm:

```lean
-- formal_verification/XRPL/Model/Protocol/Number.lean

inductive rounding_mode where
  | to_nearest | towards_zero | downward | upward
  deriving DecidableEq, Repr

structure Number where
  negative_ : Bool
  mantissa_ : UInt64
  exponent_ : Int
  deriving DecidableEq, Repr
```

The operators are translated the same way. Multiplication, for example, follows
the C++ step for step: widen to 128 bits, add exponents, scale back down through
a rounding guard, normalize.

```lean
-- formal_verification/XRPL/Model/Protocol/Number.lean

def Number.operator_mul (x y : Number) (mode : rounding_mode) : Except String Number := do
  if x.operator_eq Number.zero then return x
  else if y.operator_eq Number.zero then return y
  else
    let zn := x.negative_ != y.negative_                         -- result sign
    let zm128 := toUInt128 x.mantissa_ * toUInt128 y.mantissa_   -- 128-bit product
    let ze := x.exponent_ + y.exponent_
    let g := if zn then Guard.new.set_negative else Guard.new
    let (zm, ze, g) := scaleDown128 zm128 ze g
    match g.doRoundUp zn zm ze largeRange.min largeRange.max mode "Number::multiplication overflow" with
    | .error err => .error err
    | .ok res => res.toNumber.normalize largeRange.min largeRange.max mode
```

Two things to notice:

- The result type is `Except String Number`. The C++ throws
  `std::overflow_error` on overflow; the Lean model returns an error string.
  This pairing matters later: the C++ test treats a Lean error and a C++ throw
  as the same outcome.
- `Number` uses **sign-magnitude** representation (a `Bool` sign plus an
  unsigned `UInt64` magnitude), exactly like the C++ class.

### What we prove

Once the algorithm is in Lean, we prove properties about it. There are
**theorems and lemmas** under `XRPL/Properties/`. They range from small
accessor facts to the headline rounding bounds. A representative one:

```lean
-- formal_verification/XRPL/Properties/Protocol/Number/Accessors.lean

/-- The external mantissa view as an integer:
`n.mantissa.toInt = sign · (post-`/10`-or-not internal magnitude)`. -/
lemma Number.mantissa_toInt (n : Number) :
    n.mantissa.toInt =
      (if n.negative_ then -1 else 1) *
        (if n.mantissa_ > maxRep then ((n.mantissa_.toNat / 10 : ℕ) : ℤ) else (n.mantissa_.toNat : ℤ)) := by
  unfold Number.mantissa
  …
```

Proving these is the work; `lake build` _is_ the check. If every file compiles
with no `sorry` and only standard axioms, the proofs are sound.

---

## The FFI bridge: how C++ calls the Lean model

Lean compiles to C, so each `@[export]` function is an ordinary C symbol that
C++ can call. Lean values live as heap-allocated `lean_object*` pointers in the
C ABI, and C++ works with them through those handles.

### Building and reading a value

`lean_number_build` constructs a `Number` from its fields and hands back the
handle and `lean_number_mantissa` / `_negative` / `_exponent` read it back out.

```lean
-- formal_verification/XRPL/FFI/Protocol/NumberFFI.lean

@[export lean_number_build]
def lean_number_build (negative : UInt8) (mantissa : UInt64) (exponent : Int64) : Number :=
  Number.unchecked (negative != 0) mantissa exponent.toInt

@[export lean_number_mantissa]
def lean_number_mantissa (n : Number) : UInt64 := n.mantissa_
```

On the C++ side, `NumberFFI` wraps the handle and exposes it as a plain
`Number`:

```cpp
// src/test/formal_verification/ffi/protocol/NumberFFI.h

class NumberFFI : public LeanObjectFFI
{
public:
    using CppType = Number;
    static NumberFFI build(Number const& n);   // C++ Number  -> Lean handle
    Number           read() const;             // Lean handle -> C++ Number
};
```

### Running an operation

An operation export takes the operands' fields, runs the proved algorithm, and
returns the result as a small Lean structure, folding the `Except` error channel
into a `status` field. `lean_number_mul` decodes its arguments into `Number`
values, calls `Number.operator_mul`, and encodes the outcome:

```lean
-- formal_verification/XRPL/FFI/CommonFFI.lean

structure FFINumberResult where
  mantissa : UInt64
  exponent : Int64
  status   : UInt8                  -- 0 = ok, 1 = error
  negative : UInt8

def decodeNumber (neg : UInt8) (mant : UInt64) (exp : Int64) : Number :=
  Number.unchecked (neg != 0) mant exp.toInt

def encodeResult (r : Except String Number) : FFINumberResult :=
  match r with
  | .ok n     => encodeNumber n
  | .error _  => ⟨0, 0, 1, 0⟩
```

```lean
-- formal_verification/XRPL/FFI/Protocol/NumberFFI.lean

@[export lean_number_mul]
def lean_number_mul (neg1 : UInt8) (mant1 : UInt64) (exp1 : Int64)
    (neg2 : UInt8) (mant2 : UInt64) (exp2 : Int64) (mode : UInt8) : FFINumberResult :=
  encodeResult (Number.operator_mul (decodeNumber neg1 mant1 exp1)
                                    (decodeNumber neg2 mant2 exp2)
                                    (decodeMode mode))
```

The returned structure is itself a `lean_object*`, which is where the next
section comes in.

### What happens when C++ calls `lean_number_mul`

```
C++ test                        Lean runtime (inside xrpld)
────────                        ───────────────────────────
lean_number_mul(0, 2e18, 0,
                0, 3e18, 0, 0)
      │
      ├─ decodeNumber ×2  ──────▶  build two Number values
      │                           (heap lean_objects, internally)
      ├─ decodeMode      ──────▶  0 becomes rounding_mode.to_nearest
      ├─ operator_mul    ──────▶  run the proved algorithm
      │                           giving Except String Number
      ├─ encodeResult    ──────▶  collapse to FFINumberResult
      │                           (one heap-allocated structure)
      ◀─ returns lean_object* ──  pointer to that result struct
      │
      ├─ read fields by byte offset (mantissa@0, exponent@8, status@16, negative@17)
      └─ lean_dec(obj)   ──────▶  free the result (refcount drops to 0)
```

### Memory management is hidden in a base class

The single most important ergonomic decision: **a test author never thinks
about Lean reference counting.** All of it lives in one base class,
`LeanObjectFFI`, which owns a `lean_object*` and exposes three intention-named
operations:

```cpp
// src/test/formal_verification/ffi/LeanObjectFFI.h

class LeanObjectFFI
{
    lean_object* o_ = nullptr;

public:
    lean_object* raw()    const noexcept { return o_; }              // inspect, no refcount change
    lean_object* borrow() const noexcept { lean_inc(o_); return o_; }// pass to a Lean call (it consumes a ref)
    lean_object* give()         noexcept { auto t = o_; o_ = nullptr; return t; } // transfer ownership out

    ~LeanObjectFFI() { if (o_) lean_dec(o_); }                       // freed automatically
};
```

The rule that makes this safe is encoded in the names: Lean `@[export]`
functions _consume_ their object arguments, so you `borrow()` for a read and
`give()` for a write, and you never call `lean_inc` / `lean_dec` yourself. A
typed wrapper builds on this so a test sees only ordinary C++ values, which is
exactly what `NumberFFI::build` / `read` above do.

An operation's result structure isn't even kept as a handle. A small RAII guard
frees it the moment the fields have been copied out:

```cpp
// src/test/formal_verification/protocol/helpers/Converters.h

struct LeanNumberResult : LeanNumber
{
    bool ok;
    static LeanNumberResult fromLean(lean_object* obj)
    {
        LeanObjOwner const guard{obj};         // lean_dec(obj) on scope exit
        LeanNumberResult r;
        r.mantissa = lean_ctor_get_uint64(obj, 0);
        r.exponent = lean_ctor_get_uint64(obj, 8);
        r.negative = lean_ctor_get_uint8(obj, 17);
        r.ok       = lean_ctor_get_uint8(obj, 16) == 0;
        return r;
    }
};
```

The upshot for whoever writes a test: **call a function, get a value.** No
`lean_inc`, no `lean_dec`, no leaks, no double-frees.

### Initializing the Lean runtime (once)

Before any export can be called, the Lean runtime and the module's initializers
must run. The base suite does this exactly once, behind a `std::once_flag`, and
serializes all Lean calls behind a mutex because the Lean runtime is
single-threaded:

```cpp
// src/test/formal_verification/common/LeanSuite.h

extern "C" void          lean_initialize_runtime_module(void);
extern "C" lean_object*  initialize_XRPL_XRPL_FFI_FFI(uint8_t builtin, lean_object* w);

class LeanSuite : public beast::unit_test::Suite
{
    static bool ensureLeanInit()
    {
        static std::once_flag flag;
        static bool ok = false;
        std::call_once(flag, [] {
            lean_initialize_runtime_module();
            lean_object* res = initialize_XRPL_XRPL_FFI_FFI(1, lean_io_mk_world());
            if (lean_io_result_is_ok(res)) { lean_dec_ref(res); lean_io_mark_end_initialization(); ok = true; }
            else lean_dec(res);
        });
        return ok;
    }
public:
    void run() final { std::lock_guard lock(leanMutex()); if (ensureLeanInit()) runTests(); else fail("Lean runtime failed to initialize"); }
};
```

Note the initializer is `initialize_XRPL_XRPL_FFI_FFI`, not the root
`initialize_XRPL_XRPL`. The `FFI.FFI` module's initializer pulls in exactly the
**model + FFI** closure and nothing else, deliberately not the proof modules.
That keeps the tests buildable and runnable even while the proofs are a work in
progress, and it is what lets the tests link against only the model library (see
the build section).

---

## The build process

The Lean side is heavy: it depends on `mathlib`, thousands of files that are
slow to compile. The strategy is to do that compile **once** and keep it warm,
with no Conan packaging of the Lean code at all, only the Lean toolchain comes
from Conan:

| Conan package | Recipe            | Provides                                              | Rebuilds when                   |
| ------------- | ----------------- | ----------------------------------------------------- | ------------------------------- |
| `lean4`       | `external/lean4/` | the Lean toolchain (`lean`, `lake`, runtime, headers) | the pinned Lean version changes |

Everything else (mathlib's native objects and our model) is built **in-tree** by
CMake when `formal_verification=ON`. `conan install` runs **once** (for the
toolchain and `rippled`'s own dependencies) and after that, editing a `.lean`
file and running `cmake --build` rebuilds only what changed.

### How the build stays fast

`lake exe cache get` downloads mathlib's _elaboration_ artifacts (`.olean`) but
**never the native object files**, so the objects are compiled locally (the slow
step). `scripts/build_lean.sh` (run by the `formal_verification` target) does
that once and links the model and the dependency objects into one shared
library:

```bash
lake exe cache get          # fetch mathlib's .olean (no native objects)
lake build Mathlib:static   # compile the ~8,000 dependency .o files (slow, once)
lake build XRPLModel:static # compile the model (fast)
# link the model + dependency objects into libXRPLModel.{dylib,so}  <- the linker, not `ar`
```

Two properties keep edits cheap:

- **lake is incremental** so editing the model rebuilds only the changed model
  modules; mathlib is never rebuilt.
- **the dependency objects are compiled once** so `build_lean.sh` compiles
  them only if absent, so a model edit just rebuilds the model and relinks
  the one library.

We link the objects into a **shared** library ourselves
(`clang -dynamiclib` / `gcc -shared`), passing the ~8,000 object paths in a file
instead of on the command line. That file is the key: 8,000 paths on one command
line overflow the OS limit (`ARG_MAX`), which is why both `ar` and lake's own
`:shared` facet fail on mathlib, so we do the link by hand.

The Lean build writes into the build directory (`.lake` is symlinked under it),
so a checkout never accumulates a `.lake/` directory and `rm -rf .build` cleans
everything. Building `xrpld` needs no Lean toolchain installed, Conan provides
it.

### Wiring into xrpld

Everything is gated behind one option, `formal_verification` (default
**off**, so a normal `rippled` build is completely unaffected):

```cmake
# cmake/XrplLean4.cmake  (included from XrplCore.cmake)

if(NOT formal_verification)
    return()
endif()

# Build the Lean library (build_lean.sh), make xrpld depend on it, and link it in
# with an @rpath into the build tree (lean4::lean4 adds lean.h + the runtime).
add_custom_target(formal_verification COMMAND ... scripts/build_lean.sh)
add_dependencies(xrpld formal_verification)
target_link_libraries(xrpld ${lean_lib} lean4::lean4)
```

Because xrpld depends on the `formal_verification` target, a single
`cmake --build --target xrpld` builds the Lean library first, then links xrpld
against it. That one command is the whole build.

`formal_verification` is just the build step xrpld depends on, not a target you
normally invoke on its own. When the `formal_verification` option is off,
`XrplLean4.cmake` returns early and the test sources under
`src/test/formal_verification/` are dropped from xrpld's source glob, so the
feature has zero footprint in a default build.

---

## Building and running the tests

From a fresh checkout (only `conan` + `cmake` + a compiler needed; the `lean4`
recipe provides Lean, `lake`, and the runtime, and Conan provides `gmp`):

```bash
mkdir .build && cd .build

# 1. Register the lean4 toolchain recipe in the Conan cache (once per machine).
conan export ../external/lean4

# 2. Resolve + build dependencies. Runs once and pulls the lean4 toolchain + gmp.
conan install .. --output-folder . --build missing --settings build_type=Release \
    -o '&:formal_verification=True' --lockfile-partial

# 3. Configure, then build. The formal_verification target compiles mathlib once
#    (slow first time) and links the Lean shared libraries. lake keeps later
#    builds incremental.
cmake -DCMAKE_TOOLCHAIN_FILE:FILEPATH=build/generators/conan_toolchain.cmake \
    -DCMAKE_BUILD_TYPE=Release -Dxrpld=ON -Dtests=ON -Dformal_verification=ON ..
cmake --build . --parallel N

# 4. Run the cross-validation suite.
./xrpld --unittest=formal_verification
```

`-o '&:formal_verification=True'` is the load-bearing flag: it pulls the
`lean4` toolchain into the graph, and the matching `-Dformal_verification=ON`
tells CMake to build + link the Lean side. `--lockfile-partial` lets Conan add
`lean4` and `gmp`, which are opt-in and not pinned in `conan.lock`.

**The fast loop.** After the first build, editing a `.lean` file needs only
`cmake --build . --target xrpld && ./xrpld --unittest=formal_verification` so no
`conan` step, because the Lean libraries build in-tree and lake is incremental.

**The one-time mathlib compile.** The first build compiles mathlib, which takes
a couple of minutes. Starting from a pre-built `.lake` cache skips it.

### Two independent signals

The proofs and the cross-validation are checked separately, because they answer
different questions:

| Question                        | How                                                               |
| ------------------------------- | ----------------------------------------------------------------- |
| "Are the proofs sound?"         | `cd formal_verification && lake build` (needs the Lean toolchain) |
| "Does the model match the C++?" | `./xrpld --unittest=formal_verification`                          |

Keeping them independent means that when the model is updated to track an
upstream C++ change, the cross-validation can go green immediately while the
dependent proofs are still being repaired. Two honest signals instead of one
entangled one.

---

## Worked example: validating `Number`

`Number` is the first type we modelled end to end, so it shows the whole method.
The C++ declarations the test calls are just the exported symbols:

```cpp
// src/test/formal_verification/protocol/LeanNumber_test.cpp

extern "C" {
lean_object* lean_number_mul(uint8_t, uint64_t, uint64_t, uint8_t, uint64_t, uint64_t, uint8_t);
// … add / sub / div / neg / normalize / signum / the six comparisons …
}
```

### Fuzzing: millions of random inputs, both implementations, every rounding mode

The core of the suite is a fuzzer. For each operation it generates random
operands, runs them through the Lean export and the C++ operator under all four
rounding modes, and asserts the results agree:

```cpp
// src/test/formal_verification/protocol/LeanNumber_test.cpp

void runFuzzBinOp(LeanBinOp leanOp, char opChar, CppBinOp cppOp)
{
    using namespace formal_verification;
    NumberMantissaScaleGuard sg(MantissaRange::MantissaScale::Large);
    for (auto mode : {Number::RoundingMode::ToNearest, Number::RoundingMode::TowardsZero,
                      Number::RoundingMode::Downward,  Number::RoundingMode::Upward})
    {
        SaveNumberRoundMode save{Number::setround(mode)};
        runFuzz(100'000, [&] {                       // 100k cases per mode, per operation
            auto a = randomPair(-96, 80);            // random sign, mantissa, exponent
            auto b = randomPair(-96, 80);
            return checkBinOp(/*label*/…, leanOp, cppOp, a, b, mode);
        });
    }
}
```

A single comparison does the actual work: call both sides, compare outcomes.

```cpp
bool checkBinOp(std::string const& label, LeanBinOp leanOp, CppBinOp cppOp,
                Pair const& a, Pair const& b, Number::RoundingMode mode)
{
    using namespace formal_verification;
    auto lean = LeanNumberResult::fromLean(leanOp(            // the Lean side
        a.leanNum.negative, a.leanNum.mantissa, a.leanNum.exponent,
        b.leanNum.negative, b.leanNum.mantissa, b.leanNum.exponent, toLeanMode(mode)));

    bool cppThrew = false;
    Number cpp;
    try            { cpp = cppOp(a.cppNum, b.cppNum); }       // the C++ side
    catch (std::overflow_error const&) { cppThrew = true; }

    return checkResult(label, lean, cpp, cppThrew);          // assert agreement
}
```

The whole suite runs about **2.4 million checks in roughly 13 seconds**.

### What a failure looks like

This is not hypothetical. Upstream commit #7406 fixed `Number`'s comparison for
two negative operands. When it landed here, the Lean model still had the old
behavior, and the suite reported about 616 failures, every one a
`<`/`<=`/`>`/`>=` between two negatives. The fix belongs upstream in the model
and until then, the red is the suite correctly reporting that the model and
the C++ disagree.
