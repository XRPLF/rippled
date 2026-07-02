import Mathlib.Tactic

/-!
Lean model of rippled's `Number` (`include/xrpl/basics/Number.h`), a decimal floating point
type used for XRPL amount arithmetic.
-/

set_option linter.style.nativeDecide false

namespace XRPL.Model.Protocol

/-- Largest mantissa of an IOU `STAmount`, `10^16 - 1`. -/
def cMaxValue : UInt64 := 9999999999999999

/-- Mantissa range of a normalized `Number`, mirroring the C++ `MantissaRange` where
every normalized mantissa has the same digit count.
The two proof fields make any instance a valid range by construction. -/
structure MantissaRange where
  min : UInt64
  max : UInt64
  /-- The range spans exactly one decade. -/
  hrange : max.toNat + 1 = 10 * min.toNat := by decide
  /-- The range can hold the largest IOU mantissa. -/
  hfit : cMaxValue.toNat ≤ max.toNat := by decide

/-- The C++ `MantissaScale::Large` range, `[10^18, 10^19 - 1]`. The model and all proofs assume this scale. -/
def largeRange : MantissaRange := { min := 1000000000000000000, max := 9999999999999999999 }

/-- Smallest exponent of a normalized `Number` (C++ `kMinExponent`). -/
def minExponent : Int := -32768

/-- Largest exponent of a normalized `Number` (C++ `kMaxExponent`). -/
def maxExponent : Int := 32768

/-- Largest mantissa the C++ signed 64-bit rep can hold, `2^63 - 1` (C++ `kMaxRep`). -/
def maxRep : UInt64 := 9223372036854775807

/-- Mirror of the C++ `Number`. The value it represents is `(-1)^negative * mantissa * 10^exponent`.
Field names match the C++ members, without the C++ trailing underscore. -/
structure Number where
  negative : Bool
  mantissa : UInt64
  exponent : Int
  deriving DecidableEq, Repr

/-- The canonical zero, what C++ `Number{}` default-constructs: positive, mantissa 0, and
the smallest `int` value as exponent. -/
def Number.zero : Number :=
  { negative := false, mantissa := 0, exponent := -2147483648 }

/-- Constructs a `Number` from raw fields without normalizing, like the C++ `Unchecked`
constructor tag. This is how the FFI and the tests construct arbitrary numbers. -/
def Number.unchecked (negative : Bool) (mantissa : UInt64) (exponent : Int) : Number :=
  { negative := negative, mantissa := mantissa, exponent := exponent }

/-- The C++ `isnormal()` check -/
def Number.isnormal (n : Number) : Prop :=
  n = Number.zero ∨
  (largeRange.min ≤ n.mantissa ∧ n.mantissa ≤ largeRange.max ∧
   (n.mantissa ≤ maxRep ∨ n.mantissa.toNat % 10 = 0) ∧
   minExponent ≤ n.exponent ∧ n.exponent ≤ maxExponent)

/-- Alias of `isnormal`, the spelling used in property statements. -/
abbrev Number.isNormalized (n : Number) : Prop := n.isnormal

/-- The exact value a `Number` stands for: `sign * mantissa * 10^exponent`, as a rational.

`ℚ` is Lean's rational number type: a fraction of two integers, always exact.
Unlike a C++ double it represents `10^exponent` exactly even for negative exponents.

`mkRat a b` builds the fraction `a / b`. An exponent `≥ 0` scales the mantissa up into a
whole number (denominator 1). An exponent `< 0` puts `10^(-exponent)` in the denominator.
The sign flag negates the result.

This is a proof-only specification. The C++ has no equivalent for this. -/
def Number.toRat (n : Number) : ℚ :=
  let sign : Int := if n.negative then -1 else 1
  let m : Int := n.mantissa.toNat
  if n.exponent ≥ 0 then
    mkRat (sign * m * (10 : Int) ^ n.exponent.toNat) 1
  else
    mkRat (sign * m) ((10 : Nat) ^ (-n.exponent).toNat)

/-- The C++ `operator<(Number, Number)`.

Lean can overload `<` just like C++ does:

```lean
instance : LT Number where
  lt l r :=   -- `x < y` runs the body written here
    if l.negative != r.negative then l.negative
    else ...
```

We use a named function instead, for two reasons. An instance is locked to exactly two
arguments, and some C++ helpers we model need more, like an asset or a rounding mode, so
named functions keep all models uniform. And the C++ name tells a C++ developer exactly
which function is being modeled. -/
def Number.operator_lt (l r : Number) : Bool :=
  let lneg := l.negative
  let rneg := r.negative
  if lneg != rneg then lneg
  else if l.mantissa == 0 then r.mantissa > 0
  else if r.mantissa == 0 then false
  else if l.exponent > r.exponent then lneg
  else if l.exponent < r.exponent then !lneg
  else if lneg then l.mantissa > r.mantissa
  else l.mantissa < r.mantissa


end XRPL.Model.Protocol
