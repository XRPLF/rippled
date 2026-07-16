import Mathlib.Tactic

import XRPL.Model.Protocol.Number
import XRPL.Properties.Protocol.Number.Common.Constants

/-!
The base layer for reasoning about `Number.toRat`. Downstream proofs go through
these lemmas instead of unfolding `toRat` themselves.
-/

namespace XRPL.Model.Protocol

/-- A normalized number with a nonzero mantissa has its mantissa inside `largeRange`. Only
the canonical zero is exempt from the bounds. -/
lemma Number.isNormalized.mantissaBounds {n : Number}
    (h : n.isNormalized) (hne : n.mantissa ≠ 0) :
    largeRange.min ≤ n.mantissa ∧ n.mantissa ≤ largeRange.max := by
  rcases h with h_zero | ⟨hmin, hmax, _, _, _⟩
  · exfalso; apply hne; rw [h_zero]; rfl
  · exact ⟨hmin, hmax⟩

/-- Closed form of `toRat`: the sign factor times `mantissa * 10^exponent`, as `ℚ`. -/
lemma Number.toRat_eq (n : Number) :
    n.toRat = (if n.negative = true then (-1 : ℚ) else 1) * (n.mantissa.toNat : ℚ)
      * 10 ^ n.exponent := by
  unfold Number.toRat
  split_ifs with hneg hexp hexp
  · rw [Rat.mkRat_one]
    have h_to : n.exponent = (n.exponent.toNat : ℤ) := (Int.toNat_of_nonneg hexp).symm
    push_cast
    conv_rhs => rw [h_to, zpow_natCast]
  · rw [Rat.mkRat_eq_div]
    have h_neg_exp : n.exponent = -((-n.exponent).toNat : ℤ) := by
      rw [Int.toNat_of_nonneg (by omega : (0 : ℤ) ≤ -n.exponent)]; ring
    push_cast
    conv_rhs => rw [h_neg_exp, zpow_neg, zpow_natCast]
    rw [div_eq_mul_inv]
  · rw [Rat.mkRat_one]
    have h_to : n.exponent = (n.exponent.toNat : ℤ) := (Int.toNat_of_nonneg hexp).symm
    push_cast
    conv_rhs => rw [h_to, zpow_natCast]
  · rw [Rat.mkRat_eq_div]
    have h_neg_exp : n.exponent = -((-n.exponent).toNat : ℤ) := by
      rw [Int.toNat_of_nonneg (by omega : (0 : ℤ) ≤ -n.exponent)]; ring
    push_cast
    conv_rhs => rw [h_neg_exp, zpow_neg, zpow_natCast]
    rw [div_eq_mul_inv]

/-- The magnitude of the value is `mantissa * 10^exponent`, whatever the sign flag says.
Later proofs reason about magnitudes and reattach the sign at the end. -/
lemma abs_toRat_eq (n : Number) :
    |n.toRat| = (n.mantissa.toNat : ℚ) * 10 ^ n.exponent := by
  rw [Number.toRat_eq]
  split_ifs
  · rw [neg_one_mul, neg_mul, abs_neg, abs_of_nonneg (by positivity)]
  · rw [one_mul, abs_of_nonneg (by positivity)]

/-- With the negative flag set the value is at most 0. Only `≤`, since a zero mantissa
still gives value 0. -/
lemma Number.toRat_nonpos_of_negative (n : Number) (hneg : n.negative = true) :
    n.toRat ≤ 0 := by
  rw [Number.toRat_eq, if_pos hneg, neg_one_mul, neg_mul]
  exact neg_nonpos.mpr (by positivity)

/-- With the negative flag clear the value is at least 0. -/
lemma Number.toRat_nonneg_of_nonnegative (n : Number) (hneg : n.negative = false) :
    0 ≤ n.toRat := by
  rw [Number.toRat_eq, if_neg (by rw [hneg]; exact Bool.false_ne_true), one_mul]
  positivity

/-- The value is 0 exactly when the mantissa is 0. -/
lemma Number.toRat_eq_zero_iff {n : Number} :
    n.toRat = 0 ↔ n.mantissa = 0 := by
  rw [← abs_eq_zero, abs_toRat_eq, mul_eq_zero]
  constructor
  · rintro (hm | hpow)
    · have hm' : n.mantissa.toNat = 0 := by exact_mod_cast hm
      exact UInt64.ext hm'
    · exact absurd hpow (zpow_ne_zero _ (by norm_num))
  · intro h
    exact Or.inl (Nat.cast_eq_zero.mpr (by rw [h]; rfl))

end XRPL.Model.Protocol
