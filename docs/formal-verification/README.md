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
   meaningful if the Lean model actually matches the shipping C++. So we compile
   the Lean model to a native library, link it into `xrpld`'s test binary, and
   run **millions of randomized inputs through both the Lean function and the
   C++ function**, asserting they agree field-for-field.

The two halves reinforce each other. The proofs tell us the _modelled_
algorithm is correct; the cross-validation tells us the model _is_ the C++. A
divergence in either is a real finding. When upstream `rippled` fixed a `Number`
comparison bug (#7406), our cross-validation suite flagged the mismatch within
seconds, because the Lean model still encoded the old behavior.

```
        ┌────────────────────────┐         proves           ┌──────────────────┐
        │  Lean model of Number  │ ───────────────────────▶ │  447 theorems    │
        │  (operator_mul, …)     │   "rounding is correct    │  (the Properties │
        │                        │    for all inputs"        │   modules)       │
        └───────────┬────────────┘                           └──────────────────┘
                    │ @[export] compiled to a C symbol
                    ▼
        ┌────────────────────────┐    fuzz: same inputs     ┌──────────────────┐
        │  lean_number_mul(...)   │ ◀──── 2.4M cases ──────▶ │  C++ Number::    │
        │  (native, in xrpld)     │    assert fields equal   │  operator*       │
        └────────────────────────┘                           └──────────────────┘
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
│   ├── Model/Protocol/Number.lean     the Lean model of C++ Number (the algorithm)
│   ├── FFI/
│   │   ├── CommonFFI.lean              shared encode/decode + the FFI result structs
│   │   └── Protocol/NumberFFI.lean     the @[export] wrappers C++ calls
│   └── Properties/…                    447 theorems about the model
├── lakefile.toml                       lake build config (defines the XRPL + XRPLModel libs)
├── lean-toolchain                      pins the Lean version (leanprover/lean4:v4.28.0)
├── conanfile.py                        Conan recipe: the model library  (xrpl-lean4)
├── deps/conanfile.py                   Conan recipe: the mathlib objects (xrpl-lean4-deps)
└── scripts/bundle_lean_deps.sh         bundles mathlib's native objects into one archive

external/lean4/conanfile.py             Conan recipe: the Lean toolchain  (lean4)

src/test/formal_verification/          the C++ cross-validation tests
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
is the source of truth for the model and proofs; the C++ side only _consumes_
the compiled exports.

---

## The Lean model: translating C++ into something we can prove about

A model is a direct translation of the C++ algorithm. We keep the translation
honest by annotating each piece with the C++ source location it mirrors, so a
reviewer can diff the two by eye:

```lean
-- formal_verification/XRPL/Model/Protocol/Number.lean

-- Number.h:304-308
inductive rounding_mode where
  | to_nearest | towards_zero | downward | upward
  deriving DecidableEq, Repr

structure Number where
  negative_ : Bool      -- sign-magnitude: a separate sign bool,
  mantissa_ : UInt64    -- an unsigned magnitude,
  exponent_ : Int       -- and a base-10 exponent.
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
    let zn := x.negative_ != y.negative_              -- result sign
    let zm128 := toUInt128 x.mantissa_ * toUInt128 y.mantissa_   -- 128-bit product
    let ze := x.exponent_ + y.exponent_
    let g := if zn then Guard.new.set_negative else Guard.new
    let (zm, ze, g) := scaleDown128 zm128 ze g
    match g.doRoundUp zn zm ze largeRange.min largeRange.max mode "Number::multiplication overflow" with
    | .error err => .error err
    | .ok res => res.toNumber.normalize largeRange.min largeRange.max mode
```

Two things to notice:

- The result type is `Except String Number`. The C++ throws `std::overflow_error`
  on overflow; the Lean model returns an error string. This pairing matters
  later: the C++ test treats a Lean error and a C++ throw as the same outcome.
- `Number` uses **sign-magnitude** representation (a `Bool` sign plus an
  unsigned `UInt64` magnitude), exactly like the C++ class. The external
  `mantissa()` view folds the sign back into a signed integer, a detail the
  cross-validation has to account for (see the worked example).

### What we prove

Once the algorithm is in Lean, we prove properties about it. There are
**447 theorems and lemmas** under `XRPL/Properties/`. They range from small
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
with no `sorry` and only standard axioms, the proofs are sound. Lean's type
checker is the verifier.

---

## The FFI bridge: how C++ calls the Lean model

Lean compiles to C, so each `@[export]` function is an ordinary C symbol that
C++ can call. Lean values live as heap-allocated `lean_object*` pointers in the
C ABI, and C++ works with them through those handles.

### Building and reading a value

`lean_number_build` constructs a `Number` from its fields and hands back the
handle; `lean_number_mantissa` / `_negative` / `_exponent` read it back out.

```lean
-- formal_verification/XRPL/FFI/Protocol/NumberFFI.lean

@[export lean_number_build]
def lean_number_build (negative : UInt8) (mantissa : UInt64) (exponent : Int64) : Number :=
  Number.unchecked (negative != 0) mantissa exponent.toInt

@[export lean_number_mantissa]
def lean_number_mantissa (n : Number) : UInt64 := n.mantissa_
```

On the C++ side, `NumberFFI` wraps the handle and exposes it as a plain `Number`:

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

    ~LeanObjectFFI() { if (o_) lean_dec(o_); }                       // RAII: freed automatically
    // move-only: copying a handle would double-free
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

The Lean side is heavy: it depends on `mathlib`, thousands of files that are slow
to compile. To keep that cost out of every build, the Lean build is packaged with
**Conan** (the dependency manager `rippled` already uses) as three packages,
split by how often each changes:

| Conan package     | Recipe                      | Produces                                                | Rebuilds when                   |
| ----------------- | --------------------------- | ------------------------------------------------------- | ------------------------------- |
| `lean4`           | `external/lean4/`           | the Lean toolchain (compiler, `lake`, runtime, headers) | the pinned Lean version changes |
| `xrpl-lean4-deps` | `formal_verification/deps/` | `libLeanDeps.a`, mathlib's compiled native objects      | the mathlib pin changes         |
| `xrpl-lean4`      | `formal_verification/`      | `libXRPL_XRPLModel.a`, our model + FFI exports          | the Lean **model** changes      |

### Why three packages, not one

The split exists for one reason: **editing the model must not recompile
mathlib.** That falls out of how each package is keyed:

- **`xrpl-lean4-deps` is keyed only on the dependency pins**
  (`lakefile.toml`, `lake-manifest.json`, `lean-toolchain`). It does _not_
  include the `XRPL/` model sources, so editing the model leaves it untouched,
  served straight from the Conan cache.
- **`xrpl-lean4` is keyed on the model sources.** Editing the model rebuilds
  only this package, which just runs:

  ```bash
  lake build XRPLModel:static
  ```

- The fast path is therefore a property of the **dependency graph**, not of
  incremental-build luck, so it holds in CI too, where there is no warm build
  folder to rely on.

One subtlety the split has to handle: `lake exe cache get` downloads mathlib's
_elaboration_ artifacts but **never the native object files**, so the `.o` files
are compiled locally and merged into one archive by
`scripts/bundle_lean_deps.sh`:

```bash
lake exe cache get        # fetch mathlib's .olean / .c (no native objects)
lake build Mathlib:static # compile the dependency .o files locally
# then bundle the ~7,600 objects into libLeanDeps.a
```

The bundle feeds the object list to `ar` through a file rather than a command
line, for two reasons:

- the command line would otherwise exceed the OS argument-length limit, and
- some mathlib module names contain characters (an apostrophe in
  `LinearCombination'`) that the archiver's other input modes mishandle.

All of this runs **inside the Conan cache**, so a checkout never contains a
`.lake/` directory. You only need Lean and `elan` installed to work on the model
itself, not to build `xrpld`.

### Wiring into xrpld

Everything is gated behind one option, `formal_verification_tests` (default
**off**, so a normal `rippled` build is completely unaffected):

```cmake
# cmake/XrplLean4.cmake  (included from XrplCore.cmake)

if(NOT formal_verification_tests)
    return()
endif()

find_package(xrpl-lean4 REQUIRED)                 # pulls deps + lean4 transitively
target_link_libraries(xrpld xrpl-lean4::xrpl-lean4)
```

When the option is off, the test sources under `src/test/formal_verification/`
are also excluded from xrpld's source glob, so the feature has zero footprint in
a default build.

---

## Building and running the tests

From a fresh checkout (only `conan` + `cmake` + a compiler needed; the recipes
provide Lean, `lake`, mathlib, and gmp):

```bash
mkdir .build && cd .build

# 1. Register the three local Lean recipes in the Conan cache (once per machine).
conan export ../external/lean4
conan export ../formal_verification/deps
conan export ../formal_verification

# 2. Resolve + build dependencies. First run builds mathlib objects (slow,
#    cached afterwards); later runs reuse them.
conan install .. --output-folder . --build missing --settings build_type=Release \
    --options:host '&:xrpld=True' \
    --options:host '&:tests=True' \
    --options:host '&:formal_verification_tests=True'

# 3. Configure and build xrpld (the test sources compile into it).
cmake -DCMAKE_TOOLCHAIN_FILE:FILEPATH=build/generators/conan_toolchain.cmake \
    -DCMAKE_BUILD_TYPE=Release -Dxrpld=ON -Dtests=ON -Dformal_verification_tests=ON ..
cmake --build . --target xrpld --parallel 8

# 4. Run the cross-validation suite.
./xrpld --unittest=formal_verification
```

`-o '&:formal_verification_tests=True'` is the load-bearing flag: it tells Conan
to pull the Lean packages into the dependency graph; the matching
`-Dformal_verification_tests=ON` tells CMake to link them in.

**Skipping step 1.** The three `conan export` lines just register the recipes in
your local cache. Publish them to a Conan remote (for example `conan.ripplex.io`,
which `rippled` already uses for its other dependencies) and step 1 disappears:
`conan install` resolves the three packages from the remote, downloading a
prebuilt `xrpl-lean4-deps` instead of compiling mathlib locally. A fresh checkout
then runs only steps 2 to 4.

### Two independent signals

The proofs and the cross-validation are checked separately, because they answer
different questions:

| Question                        | How                                                   | Where                 |
| ------------------------------- | ----------------------------------------------------- | --------------------- |
| "Are the proofs sound?"         | `cd formal_verification && lake build` (needs `elan`) | CI job `check-proofs` |
| "Does the model match the C++?" | `./xrpld --unittest=formal_verification`              | CI job `build-test`   |

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

### Three honest quirks the comparison has to handle

Real cross-validation runs into representation mismatches that are _not_ bugs.
The test encodes them explicitly rather than hiding them:

- **Error vs exception.** The Lean model returns `status = 1` where the C++
  throws `std::overflow_error`. `checkResult` treats "Lean errored" and "C++
  threw" as the same outcome: overflow must show up on _both_ sides, or it is a
  mismatch.

- **Two zeros.** Lean represents zero with exponent `-32768`; C++ uses
  `INT_MIN`. For results that cancel to zero, comparing the exponent would
  falsely fail, so a dedicated `checkZero` compares only the mantissa.

- **Sign-magnitude vs signed mantissa.** Lean keeps `(negative, magnitude)`
  separately; C++'s `mantissa()` returns a signed value. `fieldsEqual` folds one
  into the other before comparing.

Beyond the fuzzer there are hand-written `known_values`, `known_comparison`, and
`extreme_values` cases that pin down specific edges: the mantissa cusp at
`maxRep`, near-overflow exponents, self-cancellation, division by the canonical
zero. These are the inputs a uniform random generator would essentially never
hit.

### What a failure looks like

This is not hypothetical. Upstream commit #7406 fixed `Number`'s comparison for
two negative operands. When it landed here, the Lean model still encoded the old
behavior, and the suite reported about 616 failures, every one a `<`/`<=`/`>`/`>=`
between two negatives. The fix belongs upstream in the model; until then, the red
is the suite correctly reporting that the model and the C++ disagree.

---

## Design decisions, in one place

| Decision                                                       | Why                                                                                                                                                                                             |
| -------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Model the C++ in Lean, then prove + cross-validate**         | Proofs cover all inputs but only of the _model_; cross-validation confirms the model _is_ the C++. Together they are far stronger than either alone.                                            |
| **Values cross the FFI as scalars or object handles**          | Each type crosses in whichever shape fits: a small flat value as scalars, a value read back through getters as a handle. C++ never depends on Lean's internal object layout.                    |
| **Memory management in `LeanObjectFFI`**                       | Reference counting is the easiest thing to get wrong across an FFI. Confining it to one RAII base class means test authors call functions and handle plain values, never `lean_inc`/`lean_dec`. |
| **Initialize via `…FFI_FFI`, not the root module**             | Links and runs the tests against the model alone, independent of proof state.                                                                                                                   |
| **Three Conan packages (toolchain / mathlib objects / model)** | Editing the model must not recompile mathlib. The split makes the fast path a property of the dependency graph, locally and in CI.                                                              |
| **Lean build runs inside the Conan cache**                     | No `.lake/` in the checkout; building `xrpld` needs no Lean toolchain installed.                                                                                                                |
| **One default-off option, tests glob-excluded when off**       | The feature has zero footprint on a normal `rippled` build.                                                                                                                                     |
| **Proofs and cross-validation as separate CI signals**         | They answer different questions and can be green or red independently, for example while a model is re-synced to an upstream C++ fix.                                                           |
