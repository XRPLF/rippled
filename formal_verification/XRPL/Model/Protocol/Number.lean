import Mathlib.Tactic

set_option linter.style.nativeDecide false

namespace XRPL.Model.Protocol

def cMaxValue : UInt64 := 9999999999999999

structure MantissaRange where
  min : UInt64
  max : UInt64
  hrange : max.toNat + 1 = 10 * min.toNat := by decide
  hfit : cMaxValue.toNat ≤ max.toNat := by decide

def largeRange : MantissaRange := { min := 1000000000000000000, max := 9999999999999999999 }
def minExponent : Int := -32768
def maxExponent : Int := 32768
def maxRep : UInt64 := 9223372036854775807

structure Number where
  negative_ : Bool
  mantissa_ : UInt64
  exponent_ : Int
  deriving DecidableEq, Repr

def Number.zero : Number :=
  { negative_ := false, mantissa_ := 0, exponent_ := -2147483648 }

def Number.unchecked (negative : Bool) (mantissa : UInt64) (exponent : Int) : Number :=
  { negative_ := negative, mantissa_ := mantissa, exponent_ := exponent }

def Number.isnormal (n : Number) : Prop :=
  n = Number.zero ∨
  (largeRange.min ≤ n.mantissa_ ∧ n.mantissa_ ≤ largeRange.max ∧
   (n.mantissa_ ≤ maxRep ∨ n.mantissa_.toNat % 10 = 0) ∧
   minExponent ≤ n.exponent_ ∧ n.exponent_ ≤ maxExponent)

abbrev Number.isNormalized (n : Number) : Prop := n.isnormal

def Number.toRat (n : Number) : ℚ :=
  let sign : Int := if n.negative_ then -1 else 1
  let m : Int := n.mantissa_.toNat
  if n.exponent_ ≥ 0 then
    mkRat (sign * m * (10 : Int) ^ n.exponent_.toNat) 1
  else
    mkRat (sign * m) ((10 : Nat) ^ (-n.exponent_).toNat)

def Number.operator_lt (l r : Number) : Bool :=
  let lneg := l.negative_
  let rneg := r.negative_
  if lneg != rneg then lneg
  else if l.mantissa_ == 0 then r.mantissa_ > 0
  else if r.mantissa_ == 0 then false
  else if l.exponent_ > r.exponent_ then lneg
  else if l.exponent_ < r.exponent_ then !lneg
  else if lneg then l.mantissa_ > r.mantissa_
  else l.mantissa_ < r.mantissa_


end XRPL.Model.Protocol
