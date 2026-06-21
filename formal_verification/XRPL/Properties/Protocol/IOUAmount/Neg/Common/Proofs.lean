import Mathlib.Tactic
import XRPL.Model.Protocol.IOUAmount
import XRPL.Properties.Protocol.Common.AmountArith
import XRPL.Properties.Protocol.Number.Common.Int64Lemmas
import XRPL.Properties.Protocol.STAmount.Add.Common.IOU
import XRPL.Properties.Protocol.STAmount.Mul.Common.IOU
import XRPL.Properties.Protocol.STAmount.Common.RoundToScaleHelpers

/-! # Proof bodies for the `IOUAmount` negation correctness headline.

`operator_neg` is **value-exact** on canonical inputs: negating the mantissa keeps it
canonical (`InRange16`), so the constructor's `normalize` is the identity. Supporting
`normalize`-identity / sign-magnitude lemmas live here. The thin headline lives in
`IOUAmount.Neg.Neg`. -/

namespace XRPL.Model.Protocol

/-- Sign-magnitude reconstruction: an in-range `Int64` is recovered from its sign and
magnitude. -/
private lemma IOUAmount.recompose_mantissa (sd : Int64) (h : sd.toInt.natAbs < 2 ^ 63) :
    (if decide (sd < 0) then -(sd.toInt.natAbs.toUInt64).toInt64
     else (sd.toInt.natAbs.toUInt64).toInt64) = sd := by
  have hlt64 : sd.toInt.natAbs < 2 ^ 64 := by omega
  have hmt : (sd.toInt.natAbs.toUInt64).toNat = sd.toInt.natAbs :=
    UInt64.toNat_ofNat_of_lt (by show sd.toInt.natAbs < 2 ^ 64; omega)
  have hlt63 : (sd.toInt.natAbs.toUInt64).toNat < 2 ^ 63 := by rw [hmt]; exact h
  rw [← Int64.toInt_inj,
      signed_mantissa_toInt (decide (sd < 0)) (sd.toInt.natAbs.toUInt64) hlt63, hmt]
  have h0 : (0 : Int64).toInt = 0 := by decide
  by_cases hs : sd < 0
  · have : sd.toInt < 0 := by rwa [Int64.lt_iff_toInt_lt, h0] at hs
    rw [decide_eq_true hs]; simp only [if_true]; omega
  · have : 0 ≤ sd.toInt := by
      rw [Int64.lt_iff_toInt_lt, h0] at hs; omega
    rw [decide_eq_false hs]; simp only [Bool.false_eq_true, if_false]; omega

/-- `normalize` is the identity on a canonical 16-digit `IOUAmount`. -/
lemma IOUAmount.normalize_canonical_id (a : IOUAmount) (mode : rounding_mode)
    (ha : a.InRange16) :
    IOUAmount.normalize a mode = .ok a := by
  have hfit : a.mantissa_.toInt.natAbs < 2 ^ 63 := by have := ha.mant_hi; omega
  set mant : UInt64 := a.mantissa_.toInt.natAbs.toUInt64 with hmant
  set neg : Bool := decide (a.mantissa_ < 0) with hneg
  have hmt : mant.toNat = a.mantissa_.toInt.natAbs :=
    UInt64.toNat_ofNat_of_lt (by show a.mantissa_.toInt.natAbs < 2 ^ 64; omega)
  have hrecompose : a.mantissa_ = if neg then -mant.toInt64 else mant.toInt64 :=
    (IOUAmount.recompose_mantissa a.mantissa_ hfit).symm
  have ha_eq : a = ⟨if neg then -mant.toInt64 else mant.toInt64, a.exponent_⟩ := by
    have hmk : (⟨if neg then -mant.toInt64 else mant.toInt64, a.exponent_⟩ : IOUAmount)
        = ⟨a.mantissa_, a.exponent_⟩ := by rw [hrecompose]
    rw [hmk]
  rw [ha_eq, IOUAmount.normalize_canonical16 mant a.exponent_ neg mode
      (by rw [hmt]; exact ha.mant_lo) (by rw [hmt]; exact ha.mant_hi)
      (by have := ha.exp_lo; have hm : minExponent = -32768 := rfl; omega)
      (by have := ha.exp_hi; have hM : maxExponent = 32768 := rfl; omega)]
  rw [if_neg (by have := ha.exp_hi; have hc : cMaxOffset = 80 := rfl; omega),
      if_neg (by have := ha.exp_lo; have hc : cMinOffset = -96 := rfl; omega)]

/-- For a canonical amount, negating the mantissa is exact (`Int64`, no overflow). -/
private lemma IOUAmount.neg_mantissa_toInt (x : IOUAmount) (hx : x.InRange16) :
    (-x.mantissa_).toInt = -(x.mantissa_.toInt) := by
  have hfit : x.mantissa_.toInt.natAbs < 10 ^ 16 := hx.mant_hi
  have hmin : Int64.minValue.toInt = (-9223372036854775808 : ℤ) := by decide
  have hmax : Int64.maxValue.toInt = (9223372036854775807 : ℤ) := by decide
  have h_lo : Int64.minValue.toInt ≤ -x.mantissa_.toInt := by rw [hmin]; omega
  have h_hi : -x.mantissa_.toInt ≤ Int64.maxValue.toInt := by rw [hmax]; omega
  rw [Int64.toInt_neg, AmountArith.toInt_bmod_self h_lo h_hi]

/-- The negation `⟨-mantissa, exponent⟩` of a canonical amount is canonical. -/
lemma IOUAmount.neg_InRange16 (x : IOUAmount) (hx : x.InRange16) :
    (⟨-x.mantissa_, x.exponent_⟩ : IOUAmount).InRange16 := by
  refine ⟨?_, ?_, hx.exp_lo, hx.exp_hi⟩
  · show 10 ^ 15 ≤ (-x.mantissa_).toInt.natAbs
    rw [IOUAmount.neg_mantissa_toInt x hx, Int.natAbs_neg]; exact hx.mant_lo
  · show (-x.mantissa_).toInt.natAbs < 10 ^ 16
    rw [IOUAmount.neg_mantissa_toInt x hx, Int.natAbs_neg]; exact hx.mant_hi

/-- **`operator_neg` is value-exact on canonical inputs**, returning a canonical amount. -/
theorem IOUAmount.operator_neg_exact_proof (x : IOUAmount) (mode : rounding_mode)
    (hx : x.InRange16) :
    ∃ r : IOUAmount, IOUAmount.operator_neg x mode = .ok r ∧ r.toRat = -x.toRat ∧ r.InRange16 := by
  refine ⟨⟨-x.mantissa_, x.exponent_⟩, ?_, ?_, IOUAmount.neg_InRange16 x hx⟩
  · unfold IOUAmount.operator_neg
    exact IOUAmount.normalize_canonical_id ⟨-x.mantissa_, x.exponent_⟩ mode
      (IOUAmount.neg_InRange16 x hx)
  · rw [IOUAmount.toRat_eq, IOUAmount.toRat_eq]
    show ((-x.mantissa_).toInt : ℚ) * (10 : ℚ) ^ x.exponent_
      = -((x.mantissa_.toInt : ℚ) * (10 : ℚ) ^ x.exponent_)
    rw [IOUAmount.neg_mantissa_toInt x hx]; push_cast; ring

end XRPL.Model.Protocol
