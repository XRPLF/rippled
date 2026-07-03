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
same compiler builds the code and checks the proofs. Each Lean snippet below is
paired with the closest C++.

### Pure functions

`def` defines a constant or a function. Every function is pure: it takes values
in, returns a value, and does nothing else. It cannot change its arguments or
any outside state, and the same arguments always give the same result.
The body is a single expression, so `if` yields one of two values, like the
C++ conditional operator `?:`. `let` introduces a local name for an
intermediate result, like a `const` local variable in C++, and it cannot be
reassigned.

```lean
def maxRep : UInt64 := 9223372036854775807

def Number.toRat (n : Number) : ℚ :=
  let sign : Int := if n.negative then -1 else 1
  ...
```

```cpp
constexpr uint64_t maxRep = 9223372036854775807;

double toRat(Number const& n)
{
    int const sign = n.negative_ ? -1 : 1;  // the ternary mirrors Lean's if
    ...
}
```

`abbrev` is an alias, like C++ `using`.

### Do notation

Pure code has no assignment and no loops. When an algorithm is easier to write
with them, `do` gives the C++ style back: `let mut` declares a variable that
can be updated, `for` loops over a collection, and `return` exits early. The
compiler translates the block into pure code, so nothing changes for the
caller.

```lean
def sumOfSquares (xs : List Nat) : Nat := Id.run do  -- run the block as pure code
  let mut total := 0
  for x in xs do
    total := total + x * x
  return total
```

```cpp
uint64_t sumOfSquares(std::vector<uint64_t> const& xs)
{
    uint64_t total = 0;
    for (auto x : xs)
        total += x * x;
    return total;
}
```

### Numbers

- `UInt64` is a 64-bit unsigned integer, the same as `uint64_t`. Arithmetic
  that goes past the maximum wraps around, `UInt64` max + 1 gives 0.
- `Int` and `Nat` (integers and naturals) are arbitrary precision and never
  overflow.
- `ℚ` is a rational number: an exact fraction of two integers.

The model mirrors the C++ fields with `UInt64`/`Int`. The specification
`Number.toRat` maps them into `ℚ`, where arithmetic is exact.

### Structures

A `structure` is the Lean version of a C++ struct: named fields, a generated
constructor, and dot access like `n.mantissa`. The `deriving` line asks the
compiler to write boilerplate for the type. `DecidableEq` generates `==`, like
`operator== = default` in C++. `Repr` generates debug printing.

```lean
structure Number where
  negative : Bool
  mantissa : UInt64
  exponent : Int
  deriving DecidableEq, Repr
```

```cpp
struct Number
{
    bool negative_;
    std::uint64_t mantissa_;
    int exponent_;
    friend bool operator==(Number const&, Number const&) = default;  // ~ deriving
};
```

A field can also require a proof. In the structure below, `hrange` is not
data, it is a claim about `min` and `max` that must be proven when a value is
created. A range that violates the claim does not compile, so an invalid
`MantissaRange` can never exist:

```lean
structure MantissaRange where
  min : UInt64
  max : UInt64
  hrange : max.toNat + 1 = 10 * min.toNat := by decide  -- proof as a field
```

### Inductive types

An `inductive` type declares a fixed set of options, like a C++ `enum class`.
An option can also carry data, and the type then works like `std::variant`: a
value holds exactly one of the listed options. A `structure` is the special
case with exactly one option. `Bool` itself is inductive, with constructors
`true` and `false`. `match` branches over the options, like a `switch` that
can also read the carried data. Proofs do the same case split with the tactic
`rcases`.

```lean
inductive Sign where
  | negative
  | zero
  | positive

def signFactor (s : Sign) : Int :=
  match s with
  | .negative => -1
  | .zero => 0
  | .positive => 1
```

```cpp
enum class Sign { negative, zero, positive };

int signFactor(Sign s)
{
    switch (s)
    {
        case Sign::negative: return -1;
        case Sign::zero:     return 0;
        case Sign::positive: return 1;
    }
}
```

### Type classes

A type class declares an operation that many types can support. An `instance`
implements it for one concrete type, like overloading a function for that type
in C++. The compiler picks the right instance from the argument types at
compile time, the same way C++ picks an overload.

```lean
class ToText (T : Type) where
  toText : T → String  -- a function from T to String

instance : ToText Sign where  -- Sign can now be turned into text
  toText
    | .negative => "-"
    | .zero => "0"
    | .positive => "+"
```

```cpp
std::string toText(Sign s)  // the "instance" for Sign
{
    switch (s)
    {
        case Sign::negative: return "-";
        case Sign::zero:     return "0";
        case Sign::positive: return "+";
    }
}
```

### Bool vs Prop

`Bool` is the ordinary `bool`: a value computed while the program runs. `Prop`
is a statement about values, like "the mantissa is within bounds". A `Prop` is
not computed but proven, once, at compile time, and it costs nothing at
runtime. The symbols read: `∧` and, `∨` or, `¬` not, `↔` if-and-only-if.

```lean
def Number.isnormal (n : Number) : Prop :=
  n = Number.zero ∨
  (largeRange.min ≤ n.mantissa ∧ n.mantissa ≤ largeRange.max ∧ ...)
```

```cpp
bool isnormal() const;  // the C++ version checks one value at runtime
```

The model never calls its `isnormal`. Theorems take it as an assumption, "for
every normalized number ...", and stating an assumption is a job for a `Prop`,
not a `Bool`.

### Theorems, lemmas and proofs

A theorem is written like a function: the arguments are its assumptions, the
return type is the statement it claims, and the body after `:=` is the proof.
The compiler checks that the body really proves the statement, the same way it
checks that a function returns its declared type. Nothing is run. If the file
compiles, the theorem holds. `lemma` means the same, used for small helpers.

```lean
theorem operator_lt_iff (x y : Number)
    (hx : x.isNormalized) (hy : y.isNormalized) :  -- given these proofs
    x.operator_lt y = true ↔ x.toRat < y.toRat :=  -- this statement holds
  operator_lt_iff_proof x y hx hy                  -- the proof
```

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

```cpp
// the matching declaration on the C++ side
extern "C" uint8_t
lean_number_lt(uint8_t, uint64_t, uint64_t, uint8_t, uint64_t, uint64_t);
```
