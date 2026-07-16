# A gentle introduction to Lean 4

In [README.md](README.md) we explain the need for formal verification and its benefits.
This document provides an introduction to Lean 4, the language we use to model XRPL components.

Lean 4 is a functional programming language and a theorem prover. This means it can be used as a general-purpose programming language, but it also lets us write mathematical theorems and proofs.

A function is defined as: `def functionName (parameters) : ReturnType := body`. For example:

```lean
def isEven (n : UInt64) : Bool :=
  n % 2 == 0
```

A function is called by passing arguments after the function name:

```lean
#eval isEven 4 -- evaluates to true
#eval isEven 3 -- evaluates to false
```

We can now make a claim about the function we wrote:

```lean
theorem mul_of_two_is_even (m : UInt64) : isEven (m * 2) = true := by
  unfold isEven
  grind
```

A theorem is defined as: `theorem theoremName (parameters) : Claim := proof`. The compiler checks the proof, so a theorem that compiles is a proven claim.
We now know for a fact that `isEven` returns `true` for any `UInt64` multiplied by two.

## Data types

Some types are easy to recognize:

```lean
def a : UInt64 := 42
def b : Bool := true
def s : String := "hello"
def i : Int := -5
def n : Nat := 5
```

The last one stands out: `Nat` represents a natural number, a non-negative integer. A common library used with Lean 4 is Mathlib. It provides Unicode notation for some commonly used number types:

```lean
import Mathlib

def a : Nat := 5
def b : ℕ := 5
def c : Int := -3
def d : ℤ := -3
def e : Rat := 3 / 2
def f : ℚ := 3 / 2
```

Compound types can be expressed as structures:

```lean
structure Rectangle where
  a : Rat
  b : Rat

def Rectangle.area (r : Rectangle) : Rat :=
  r.a * r.b
```

## Pure functions, do notation and flow control

`def` defines a constant or a function. In Lean 4 every function is pure: it cannot produce side effects.

Take a function like this in C++:

```cpp
int clampScore(int x, int y)
{
    int const total = x + y;

    if (total > 100) {
        return 100;
    } else if (total < 10) {
        return 10;
    }

    return total;
}
```

In Lean 4, it would be rewritten as:

```lean
def clampScore (x y : Int) : Int :=
  let total := x + y

  if total > 100 then
    100
  else
    if total < 10 then
      10
    else
      total
```

Regular functions cannot mutate a variable, every `if` expression has to have an `else` branch, and there are no loops, only recursion. Lean 4 provides `do` notation, which is syntactic sugar that lets us write imperative-looking code.

```lean
def clampScore (x y : Int) : Int := Id.run do
  let total := x + y

  if total > 100 then
    return 100
  else if total < 10 then
    return 10

  return total
```

Among other things, `do` notation also supports `while` loops and mutable variables.

## Inductive types

An `inductive` type declares a fixed set of alternatives, like a C++ `enum class`.
(Its constructors can also carry data, which makes it closer to a `std::variant`,
but the simple form is enough here.) For example, this C++ code:

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

would be written as:

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

## Type classes

A type class declares an operation that many types can support. An `instance`
implements it for one concrete type, like overloading a function for that type
in C++. The compiler picks the right instance from the argument types at
compile time, the same way C++ picks an overload.

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

would become:

```lean
class ToText (T : Type) where
  toText : T → String  -- a function from T to String

instance : ToText Sign where  -- Sign can now be turned into text
  toText
    | .negative => "-"
    | .zero => "0"
    | .positive => "+"
```

## FFI

`@[export name]` gives a function a C symbol, so the C++ code can declare it
`extern "C"` and call it.

```lean
@[export lean_number_lt]  -- exported as the C symbol "lean_number_lt"
def lean_number_lt (neg1 : UInt8) (mant1 : UInt64) (exp1 : Int64) ...
```

```cpp
// the matching declaration on the C++ side
extern "C" uint8_t
lean_number_lt(uint8_t, uint64_t, uint64_t, uint8_t, uint64_t, uint64_t);
```

## Where next?

For a crash course beyond this document, we recommend the learning platform [lean4.dev](https://lean4.dev/).

If you care about functional programming in Lean 4, we recommend the excellent book [Functional Programming in Lean](https://lean-lang.org/functional_programming_in_lean/) by David Thrane Christiansen.

If you want to dabble in theorems and proofs, we recommend [Theorem Proving in Lean 4](https://lean-lang.org/theorem_proving_in_lean4/).
