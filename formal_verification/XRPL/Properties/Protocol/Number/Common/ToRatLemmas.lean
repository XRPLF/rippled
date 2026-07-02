import Mathlib.Tactic

import XRPL.Model.Protocol.Number
import XRPL.Properties.Protocol.Number.Common.Constants


namespace XRPL.Model.Protocol

/-- A normalized number with a nonzero mantissa has its mantissa inside `largeRange`. Only
the canonical zero is exempt from the bounds. -/
lemma Number.isNormalized.mantissaBounds {n : Number}
    (h : n.isNormalized) (hne : n.mantissa ≠ 0) :
    largeRange.min ≤ n.mantissa ∧ n.mantissa ≤ largeRange.max := by
  rcases h with h_zero | ⟨hmin, hmax, _, _, _⟩
  · exfalso; apply hne; rw [h_zero]; rfl
  · exact ⟨hmin, hmax⟩

/-- The magnitude of the value is `mantissa * 10^exponent`, whatever the sign flag says.
Later proofs reason about magnitudes and reattach the sign at the end. -/
lemma abs_toRat_eq (n : Number) :
    |n.toRat| = (n.mantissa.toNat : ℚ) * 10 ^ n.exponent := by
  unfold Number.toRat
  split_ifs with hneg hexp hexp
  · rw [Rat.mkRat_one]
    have h_to : n.exponent = (n.exponent.toNat : ℤ) := (Int.toNat_of_nonneg hexp).symm
    have h_neg_val : ((-1 * (n.mantissa.toNat : Int) * (10 : Int) ^ n.exponent.toNat : Int) : ℚ)
        = -((n.mantissa.toNat : ℚ) * (10 : ℚ) ^ n.exponent) := by
      push_cast
      conv_rhs => rw [h_to]; rw [zpow_natCast]
      ring
    rw [h_neg_val, abs_neg]
    rw [abs_of_nonneg (by positivity)]
  · have hneg_exp : n.exponent < 0 := not_le.mp hexp
    have h_pow : (0 : ℚ) < (10 : ℚ) ^ n.exponent := zpow_pos (by norm_num) _
    rw [Rat.mkRat_eq_div]
    push_cast
    have h_neg_exp : n.exponent = -((-n.exponent).toNat : ℤ) := by
      rw [Int.toNat_of_nonneg (by omega : (0 : ℤ) ≤ -n.exponent)]; ring
    have h_val : (-1 * (n.mantissa.toNat : ℚ)) / (10 : ℚ) ^ (-n.exponent).toNat
        = -((n.mantissa.toNat : ℚ) * (10 : ℚ) ^ n.exponent) := by
      conv_rhs => rw [h_neg_exp, zpow_neg, zpow_natCast]
      field_simp
    rw [h_val, abs_neg, abs_of_nonneg (by positivity)]
  · rw [Rat.mkRat_one]
    have h_to : n.exponent = (n.exponent.toNat : ℤ) := (Int.toNat_of_nonneg hexp).symm
    have h_val : ((1 * (n.mantissa.toNat : Int) * (10 : Int) ^ n.exponent.toNat : Int) : ℚ)
        = (n.mantissa.toNat : ℚ) * (10 : ℚ) ^ n.exponent := by
      push_cast
      conv_rhs => rw [h_to]; rw [zpow_natCast]
      ring
    rw [h_val, abs_of_nonneg (by positivity)]
  · have hneg_exp : n.exponent < 0 := not_le.mp hexp
    rw [Rat.mkRat_eq_div]
    push_cast
    have h_neg_exp : n.exponent = -((-n.exponent).toNat : ℤ) := by
      rw [Int.toNat_of_nonneg (by omega : (0 : ℤ) ≤ -n.exponent)]; ring
    have h_val : (1 * (n.mantissa.toNat : ℚ)) / (10 : ℚ) ^ (-n.exponent).toNat
        = (n.mantissa.toNat : ℚ) * (10 : ℚ) ^ n.exponent := by
      conv_rhs => rw [h_neg_exp, zpow_neg, zpow_natCast]
      field_simp
    rw [h_val, abs_of_nonneg (by positivity)]

/-- With the negative flag set the value is at most 0. Only `≤`, since a zero mantissa
still gives value 0. -/
lemma Number.toRat_nonpos_of_negative (n : Number) (hneg : n.negative = true) :
    n.toRat ≤ 0 := by
  unfold Number.toRat
  rw [hneg]; simp only [if_true]
  split_ifs with hexp
  · rw [Rat.mkRat_one]; push_cast
    have : (0 : ℚ) ≤ (n.mantissa.toNat : ℚ) * (10 : ℚ) ^ n.exponent.toNat := by positivity
    linarith
  · rw [Rat.mkRat_eq_div]; push_cast
    have h_m_nn : (0 : ℚ) ≤ (n.mantissa.toNat : ℚ) := Nat.cast_nonneg _
    have h_pow_pos : (0 : ℚ) < (10 : ℚ) ^ (-n.exponent).toNat := by positivity
    have h_frac_nn : (0 : ℚ) ≤ (n.mantissa.toNat : ℚ) / (10 : ℚ) ^ (-n.exponent).toNat :=
      div_nonneg h_m_nn (le_of_lt h_pow_pos)
    have : -(1 : ℚ) * (n.mantissa.toNat : ℚ) / (10 : ℚ) ^ (-n.exponent).toNat
        = -((n.mantissa.toNat : ℚ) / (10 : ℚ) ^ (-n.exponent).toNat) := by
      field_simp
    rw [this]; linarith

/-- With the negative flag clear the value is at least 0. -/
lemma Number.toRat_nonneg_of_nonnegative (n : Number) (hneg : n.negative = false) :
    0 ≤ n.toRat := by
  unfold Number.toRat
  rw [hneg]; simp only [Bool.false_eq_true, if_false]
  split_ifs
  · rw [Rat.mkRat_one]; push_cast; positivity
  · rw [Rat.mkRat_eq_div]; push_cast; positivity

/-- The value is 0 exactly when the mantissa is 0. -/
lemma Number.toRat_eq_zero_iff {n : Number} :
    n.toRat = 0 ↔ n.mantissa = 0 := by
  constructor
  · intro h
    have habs := abs_toRat_eq n
    rw [h, abs_zero] at habs
    have h10_pos : (0 : ℚ) < 10 ^ n.exponent := zpow_pos (by norm_num) _
    have hm_zero : (n.mantissa.toNat : ℚ) = 0 := by
      nlinarith [Nat.cast_nonneg (α := ℚ) n.mantissa.toNat]
    have : n.mantissa.toNat = 0 := by exact_mod_cast hm_zero
    exact UInt64.ext this
  · intro h
    have hm : n.mantissa.toNat = 0 := by rw [h]; rfl
    unfold Number.toRat
    simp only [hm, Nat.cast_zero]
    split_ifs <;> simp

end XRPL.Model.Protocol
