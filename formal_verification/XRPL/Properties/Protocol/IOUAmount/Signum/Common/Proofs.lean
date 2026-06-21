import Mathlib.Tactic
import XRPL.Model.Protocol.IOUAmount
import XRPL.Properties.Protocol.IOUAmount.Common.ToRatLemmas

/-! # Proof bodies for the `IOUAmount.signum` correctness headlines.

`IOUAmount.signum` returns the sign of the value. Since `toRat = mantissa · 10^exponent`
with `10^exponent > 0`, the sign of `toRat` is the sign of the mantissa, so the sign tests
reduce to integer sign tests on `mantissa_.toInt`. The thin headlines live in
`IOUAmount.Signum.Signum`. -/

namespace XRPL.Model.Protocol

/-- `signum` rewritten with its sign tests on `mantissa_.toInt`. -/
private lemma IOUAmount.signum_eq_int (a : IOUAmount) :
    a.signum = if a.mantissa_.toInt < 0 then -1 else if 0 < a.mantissa_.toInt then 1 else 0 := by
  have hz : ((0 : Int64)).toInt = 0 := by decide
  have hneg : (a.mantissa_ < 0) ↔ (a.mantissa_.toInt < 0) := by rw [Int64.lt_iff_toInt_lt, hz]
  have hne : ((a.mantissa_ != 0) = true) ↔ (a.mantissa_.toInt ≠ 0) := by
    rw [bne_iff_ne]
    constructor
    · intro h hh; exact h (by rw [← Int64.toInt_inj, hz]; exact hh)
    · intro h hh; exact h (by rw [hh, hz])
  unfold IOUAmount.signum
  by_cases h1 : a.mantissa_ < 0
  · rw [if_pos h1, if_pos (hneg.mp h1)]
  · rw [if_neg h1, if_neg (fun h => h1 (hneg.mpr h))]
    have h1' : ¬ a.mantissa_.toInt < 0 := fun h => h1 (hneg.mpr h)
    by_cases h2 : (a.mantissa_ != 0) = true
    · rw [if_pos h2, if_pos (by have := hne.mp h2; omega)]
    · rw [if_neg h2]
      have hzero : a.mantissa_.toInt = 0 := by by_contra hh; exact h2 (hne.mpr hh)
      rw [if_neg (by omega)]

/-- `toRat < 0` exactly when the mantissa is negative (`10^exponent > 0`). -/
private lemma IOUAmount.toRat_neg_iff (a : IOUAmount) : a.toRat < 0 ↔ a.mantissa_.toInt < 0 := by
  have h10 : (0 : ℚ) < (10 : ℚ) ^ a.exponent_ := by positivity
  rw [IOUAmount.toRat_eq]
  constructor
  · intro h; by_contra hh; push_neg at hh
    exact absurd h (not_lt.mpr (mul_nonneg (by exact_mod_cast hh) (le_of_lt h10)))
  · intro h; exact mul_neg_of_neg_of_pos (by exact_mod_cast h) h10

/-- `0 < toRat` exactly when the mantissa is positive. -/
private lemma IOUAmount.toRat_pos_iff (a : IOUAmount) : 0 < a.toRat ↔ 0 < a.mantissa_.toInt := by
  have h10 : (0 : ℚ) < (10 : ℚ) ^ a.exponent_ := by positivity
  rw [IOUAmount.toRat_eq, mul_pos_iff_of_pos_right h10, Int.cast_pos]

/-- **`signum` returns the sign of `toRat`.** -/
theorem IOUAmount.signum_eq_proof (a : IOUAmount) :
    a.signum = if 0 < a.toRat then 1 else if a.toRat < 0 then -1 else 0 := by
  have hpos := IOUAmount.toRat_pos_iff a
  have hnegR := IOUAmount.toRat_neg_iff a
  rw [IOUAmount.signum_eq_int]
  by_cases h1 : a.mantissa_.toInt < 0
  · rw [if_pos h1, if_neg (fun h => absurd (hpos.mp h) (by omega)), if_pos (hnegR.mpr h1)]
  · rw [if_neg h1]
    by_cases h2 : 0 < a.mantissa_.toInt
    · rw [if_pos h2, if_pos (hpos.mpr h2)]
    · rw [if_neg h2, if_neg (fun h => h2 (hpos.mp h)), if_neg (fun h => h1 (hnegR.mp h))]

/-- **`signum = 1 ↔ value is positive`.** -/
theorem IOUAmount.signum_eq_one_iff_proof (a : IOUAmount) : a.signum = 1 ↔ 0 < a.toRat := by
  rw [IOUAmount.signum_eq_int, IOUAmount.toRat_pos_iff]
  constructor <;> intro h <;> split_ifs at h ⊢ <;> omega

/-- **`signum = -1 ↔ value is negative`.** -/
theorem IOUAmount.signum_eq_neg_one_iff_proof (a : IOUAmount) : a.signum = -1 ↔ a.toRat < 0 := by
  rw [IOUAmount.signum_eq_int, IOUAmount.toRat_neg_iff]
  constructor <;> intro h <;> split_ifs at h ⊢ <;> omega

/-- **`signum = 0 ↔ value is zero`.** -/
theorem IOUAmount.signum_eq_zero_iff_proof (a : IOUAmount) : a.signum = 0 ↔ a.toRat = 0 := by
  have h10 : (10 : ℚ) ^ a.exponent_ ≠ 0 := by positivity
  rw [IOUAmount.signum_eq_int,
      show (a.toRat = 0) ↔ (a.mantissa_.toInt = 0) from by
        rw [IOUAmount.toRat_eq, mul_eq_zero, or_iff_left h10, Int.cast_eq_zero]]
  constructor <;> intro h <;> split_ifs at h ⊢ <;> omega

end XRPL.Model.Protocol
