import Mathlib.Tactic

import XRPL.Model.Protocol.Number
import XRPL.Properties.Protocol.Number.Common.Constants


namespace XRPL.Model.Protocol
/-- Extract mantissa bounds from `isNormalized` when mantissa is nonzero. -/
lemma Number.isNormalized.mantissaBounds {n : Number}
    (h : n.isNormalized) (hne : n.mantissa_ ≠ 0) :
    largeRange.min ≤ n.mantissa_ ∧ n.mantissa_ ≤ largeRange.max := by
  rcases h with h_zero | ⟨hmin, hmax, _, _, _⟩
  · exfalso; apply hne; rw [h_zero]; rfl
  · exact ⟨hmin, hmax⟩

/-- `|n.toRat| = n.mantissa_.toNat * 10^n.exponent_`. -/
lemma abs_toRat_eq (n : Number) :
    |n.toRat| = (n.mantissa_.toNat : ℚ) * 10 ^ n.exponent_ := by
  unfold Number.toRat
  split_ifs with hneg hexp hexp
  · rw [Rat.mkRat_one]
    have h_to : n.exponent_ = (n.exponent_.toNat : ℤ) := (Int.toNat_of_nonneg hexp).symm
    have h_neg_val : ((-1 * (n.mantissa_.toNat : Int) * (10 : Int) ^ n.exponent_.toNat : Int) : ℚ)
        = -((n.mantissa_.toNat : ℚ) * (10 : ℚ) ^ n.exponent_) := by
      push_cast
      conv_rhs => rw [h_to]; rw [zpow_natCast]
      ring
    rw [h_neg_val, abs_neg]
    rw [abs_of_nonneg (by positivity)]
  · have hneg_exp : n.exponent_ < 0 := not_le.mp hexp
    have h_pow : (0 : ℚ) < (10 : ℚ) ^ n.exponent_ := zpow_pos (by norm_num) _
    rw [Rat.mkRat_eq_div]
    push_cast
    have h_neg_exp : n.exponent_ = -((-n.exponent_).toNat : ℤ) := by
      rw [Int.toNat_of_nonneg (by omega : (0 : ℤ) ≤ -n.exponent_)]; ring
    have h_val : (-1 * (n.mantissa_.toNat : ℚ)) / (10 : ℚ) ^ (-n.exponent_).toNat
        = -((n.mantissa_.toNat : ℚ) * (10 : ℚ) ^ n.exponent_) := by
      conv_rhs => rw [h_neg_exp, zpow_neg, zpow_natCast]
      field_simp
    rw [h_val, abs_neg, abs_of_nonneg (by positivity)]
  · rw [Rat.mkRat_one]
    have h_to : n.exponent_ = (n.exponent_.toNat : ℤ) := (Int.toNat_of_nonneg hexp).symm
    have h_val : ((1 * (n.mantissa_.toNat : Int) * (10 : Int) ^ n.exponent_.toNat : Int) : ℚ)
        = (n.mantissa_.toNat : ℚ) * (10 : ℚ) ^ n.exponent_ := by
      push_cast
      conv_rhs => rw [h_to]; rw [zpow_natCast]
      ring
    rw [h_val, abs_of_nonneg (by positivity)]
  · have hneg_exp : n.exponent_ < 0 := not_le.mp hexp
    rw [Rat.mkRat_eq_div]
    push_cast
    have h_neg_exp : n.exponent_ = -((-n.exponent_).toNat : ℤ) := by
      rw [Int.toNat_of_nonneg (by omega : (0 : ℤ) ≤ -n.exponent_)]; ring
    have h_val : (1 * (n.mantissa_.toNat : ℚ)) / (10 : ℚ) ^ (-n.exponent_).toNat
        = (n.mantissa_.toNat : ℚ) * (10 : ℚ) ^ n.exponent_ := by
      conv_rhs => rw [h_neg_exp, zpow_neg, zpow_natCast]
      field_simp
    rw [h_val, abs_of_nonneg (by positivity)]

/-- `n.toRat ≤ 0` when `negative_ = true`. -/
lemma Number.toRat_nonpos_of_negative (n : Number) (hneg : n.negative_ = true) :
    n.toRat ≤ 0 := by
  unfold Number.toRat
  rw [hneg]; simp only [if_true]
  split_ifs with hexp
  · rw [Rat.mkRat_one]; push_cast
    have : (0 : ℚ) ≤ (n.mantissa_.toNat : ℚ) * (10 : ℚ) ^ n.exponent_.toNat := by positivity
    linarith
  · rw [Rat.mkRat_eq_div]; push_cast
    have h_m_nn : (0 : ℚ) ≤ (n.mantissa_.toNat : ℚ) := Nat.cast_nonneg _
    have h_pow_pos : (0 : ℚ) < (10 : ℚ) ^ (-n.exponent_).toNat := by positivity
    have h_frac_nn : (0 : ℚ) ≤ (n.mantissa_.toNat : ℚ) / (10 : ℚ) ^ (-n.exponent_).toNat :=
      div_nonneg h_m_nn (le_of_lt h_pow_pos)
    have : -(1 : ℚ) * (n.mantissa_.toNat : ℚ) / (10 : ℚ) ^ (-n.exponent_).toNat
        = -((n.mantissa_.toNat : ℚ) / (10 : ℚ) ^ (-n.exponent_).toNat) := by
      field_simp
    rw [this]; linarith

/-- `0 ≤ n.toRat` when `negative_ = false`. -/
lemma Number.toRat_nonneg_of_nonnegative (n : Number) (hneg : n.negative_ = false) :
    0 ≤ n.toRat := by
  unfold Number.toRat
  rw [hneg]; simp only [Bool.false_eq_true, if_false]
  split_ifs
  · rw [Rat.mkRat_one]; push_cast; positivity
  · rw [Rat.mkRat_eq_div]; push_cast; positivity

/-- `n.toRat = 0` iff `n.mantissa_ = 0`. -/
lemma Number.toRat_eq_zero_iff {n : Number} :
    n.toRat = 0 ↔ n.mantissa_ = 0 := by
  constructor
  · intro h
    have habs := abs_toRat_eq n
    rw [h, abs_zero] at habs
    have h10_pos : (0 : ℚ) < 10 ^ n.exponent_ := zpow_pos (by norm_num) _
    have hm_zero : (n.mantissa_.toNat : ℚ) = 0 := by
      nlinarith [Nat.cast_nonneg (α := ℚ) n.mantissa_.toNat]
    have : n.mantissa_.toNat = 0 := by exact_mod_cast hm_zero
    exact UInt64.ext this
  · intro h
    have hm : n.mantissa_.toNat = 0 := by rw [h]; rfl
    unfold Number.toRat
    simp only [hm, Nat.cast_zero]
    split_ifs <;> simp

end XRPL.Model.Protocol
