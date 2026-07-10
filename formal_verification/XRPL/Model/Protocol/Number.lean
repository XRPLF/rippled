import Mathlib.Tactic

/-!
Lean model of xrpld's `Number`.

For simplicity, and to demonstrate the approach, we model just one small function here:
`operator<`, along with the constants and the normalization check the proofs need.

Names, constants, and the branching of the function bodies are kept relatively faithful
to C++ in this instance - as we wanted to make sure we capture every branch of the code.

`Number.operator_lt` is a named function, instead of Lean's overloaded `<`. We chose this
approach because for other operators (+, -, *, /), we also need the third parameter:
rounding mode.

Constants are spelled out in full digits, never as expressions like `10^19 - 1`, because
proofs run faster without having to evaluate expressions.

The one definition with no C++ counterpart is `toRat`, which gives the exact rational
value a `Number` represents. It exists only to state and prove properties.
-/

namespace XRPL.Model.Protocol

structure Number where
  negative : Bool
  mantissa : UInt64
  exponent : Int
  deriving DecidableEq, Repr

def cMaxValue : UInt64 := 9999999999999999

structure MantissaRange where
  min : UInt64
  max : UInt64
  /-- The range spans exactly one decade. -/
  hrange : max.toNat + 1 = 10 * min.toNat := by decide
  /-- The range can hold the largest IOU mantissa. -/
  hfit : cMaxValue.toNat ≤ max.toNat := by decide

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

def largeRange : MantissaRange := { min := 1000000000000000000, max := 9999999999999999999 }

def minExponent : Int := -32768

def maxExponent : Int := 32768

def maxRep : UInt64 := 9223372036854775807

def Number.zero : Number :=
  { negative := false, mantissa := 0, exponent := -2147483648 }

def Number.unchecked (negative : Bool) (mantissa : UInt64) (exponent : Int) : Number :=
  { negative := negative, mantissa := mantissa, exponent := exponent }

def Number.isNormalized (n : Number) : Prop :=
  n = Number.zero ∨
  (largeRange.min ≤ n.mantissa ∧ n.mantissa ≤ largeRange.max ∧
   (n.mantissa ≤ maxRep ∨ n.mantissa.toNat % 10 = 0) ∧
   minExponent ≤ n.exponent ∧ n.exponent ≤ maxExponent)

def Number.toRat (n : Number) : ℚ :=
  let sign : Int := if n.negative then -1 else 1
  let m : Int := n.mantissa.toNat
  if n.exponent ≥ 0 then
    mkRat (sign * m * (10 : Int) ^ n.exponent.toNat) 1
  else
    mkRat (sign * m) ((10 : Nat) ^ (-n.exponent).toNat)

end XRPL.Model.Protocol
