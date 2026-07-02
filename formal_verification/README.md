# XRPL formal verification (Lean 4)

The Lean 4 model of XRPL, the theorems about it, and the FFI exports that the
C++ cross-validation tests call. For how the model, proofs, FFI, and build all
fit together, see
[docs/formal-verification/README.md](../docs/formal-verification/README.md).

## Layout

- `XRPL/Model/`: the Lean model of the C++ types (e.g. `Number`).
- `XRPL/Properties/`: theorems about the model.
- `XRPL/FFI/`: the `@[export]` wrappers the C++ tests call.

## Building

Nothing here is built by hand for usual use. The C++ cross-validation tests
compile this model in-tree and link it into `xrpld`, gated by the
`formal_verification` option. The Lean toolchain comes from the one `lean4`
Conan package, so it needs no separate Lean or elan install.

## Working on the proofs directly

Only needed when editing the Lean model or proofs by hand. You can use lake binary in `.conan2` directory or install your own.

That requires [elan](https://github.com/leanprover/elan) (it provides the Lean
version pinned in `lean-toolchain`):

```bash
lake exe cache get
lake build
```

`lake build` is the proof check: it passes only if every file elaborates with no
`sorry` and standard axioms.

## Lean 4 introduction

Lean 4 is a functional programming language and a theorem prover. The
same compiler builds the code and checks the proofs.

### Pure functions

`def` defines a constant or a function. Every function is pure: no mutation, no
side effects, the result depends only on the arguments. `if` is an expression
with a value. `let` names an intermediate variable.

```lean
def maxRep : UInt64 := 9223372036854775807

def Number.toRat (n : Number) : ℚ :=
  let sign : Int := if n.negative_ then -1 else 1  -- if returns a value
  ...
```

`abbrev` is a transparent alias, like C++ `using`.

### Do notation

`do` opens a block that looks imperative: local mutation with `let mut`, `for`
loops, early `return`. It is syntax only. The compiler turns the block into the
same pure function calls, so nothing observable is mutated.

```lean
def sumOfSquares (xs : List Nat) : Nat := Id.run do  -- run the block as pure code
  let mut total := 0
  for x in xs do
    total := total + x * x
  return total
```

### Numbers

- `UInt64` wraps around, like `uint64_t`.
- `Int` and `Nat` (integers and naturals) are arbitrary precision and never
  overflow.
- `ℚ` is a rational number: an exact fraction of two integers.

The model mirrors the C++ fields with `UInt64`/`Int`. The specification
`Number.toRat` maps them into `ℚ`, where arithmetic is exact.

### Structures

A `structure` is a C++ struct: fields, a constructor, dot access. `deriving`
auto-generates code, here equality (`DecidableEq`) and debug printing (`Repr`).

```lean
structure Number where
  negative_ : Bool
  mantissa_ : UInt64
  exponent_ : Int
  deriving DecidableEq, Repr
```

A field can also hold a proof. An instance cannot be built without supplying
it, so every value satisfies the invariant by construction:

```lean
structure MantissaRange where
  min : UInt64
  max : UInt64
  hrange : max.toNat + 1 = 10 * min.toNat := by decide  -- proof as a field
```

### Inductive types

An `inductive` type lists all the ways a value can be built. A `structure` is the special case
with exactly one constructor. `Bool` itself is inductive, with constructors
`true` and `false`. Code takes values apart with `match`, and proofs do it with
case-splitting tactics like `rcases`.

```lean
inductive Sign where
  | negative
  | zero
  | positive
```

### Type classes

A type class is a compile-time interface, close to C++ concepts. An `instance`
implements it for a concrete type, and the compiler picks the instance from the
types at the call site. That is what `deriving DecidableEq, Repr` generates
above, and it is how operators work: `a ≤ b` is notation for `LE.le a b`,
resolved from the `LE` instance of the type of `a`.

```lean
class LE (α : Type) where  -- simplified from the library
  le : α → α → Prop
```

### Bool vs Prop

`Bool` is a runtime value code can branch on. `Prop` is a mathematical
statement to be proven. It never executes. The symbols read: `∧` and, `∨` or,
`¬` not, `↔` if-and-only-if.

```lean
def Number.isnormal (n : Number) : Prop :=
  n = Number.zero ∨
  (largeRange.min ≤ n.mantissa_ ∧ n.mantissa_ ≤ largeRange.max ∧ ...)
```

`isnormal` is a condition theorems assume, not code the model runs. That is why
it is a `Prop` and not a `Bool`.

### Theorems, lemmas and proofs

`theorem` and `lemma` mean the same thing (lemma is used for small helpers).
The statement is a `Prop`, hypotheses are named arguments, and what follows
`:=` (or `by`) is the proof. If the file compiles, the proof is correct: the
compiler is the proof checker, and there is nothing to run.

```lean
theorem operator_lt_iff (x y : Number)
    (hx : x.isNormalized) (hy : y.isNormalized) :  -- given these proofs
    x.operator_lt y = true ↔ x.toRat < y.toRat :=  -- this statement holds
  operator_lt_iff_proof x y hx hy                  -- the proof
```

### Tactics

`by` switches to tactic mode, where a proof is built step by step. The ones
used here:

- `rw`: rewrite the goal with a known equality.
- `simp`: apply a library of simplification rules.
- `unfold`: inline a definition.
- `by_cases`, `rcases`: split into cases.
- `linarith`, `omega`, `norm_num`: close arithmetic goals.
- `decide`: evaluate a finite statement outright (fills the `by decide` fields
  of `MantissaRange`).
- `calc`: chain equalities and inequalities like a paper computation.
- `sorry`: placeholder for a missing proof. It fails the build, and its absence
  is what "verified" means.

### Namespaces

`namespace X ... end X` works like C++. Naming a definition `Number.toRat`
enables the dot call `n.toRat`, like a member function.

### FFI

`@[export name]` gives a function a C symbol, so the C++ tests can declare it
`extern "C"` and call it. This is the only place Lean and C++ touch.

```lean
@[export lean_number_lt]  -- exported as the C symbol "lean_number_lt"
def lean_number_lt (neg1 : UInt8) (mant1 : UInt64) (exp1 : Int64) ...
```
