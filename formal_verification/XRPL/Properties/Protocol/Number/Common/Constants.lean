import Mathlib.Tactic

import XRPL.Properties.Protocol.Number.Common.Notation
import XRPL.Model.Protocol.Number


namespace XRPL.Model.Protocol

/-! # Concrete values of the `Number` magic constants

These `decide`/`rfl` facts are re-derived inline all over the proof tree (each
`largeRange` `decide` is a slow 19-digit `BitVec` kernel evaluation). Centralizing
them here — a tiny file importing only the model — lets both the `Rounding` layer
and the `Closest` layer share one cached copy instead of recomputing. -/

/-- `maxRep.toNat = 2^63 - 1`. -/
lemma maxRep_val : maxRep.toNat = maxRepNat := by decide

/-- `largeRange.min.toNat = 10^18`. -/
lemma largeRange_min_val : largeRange.min.toNat = 1000000000000000000 := by decide

/-- `largeRange.max.toNat = 10^19 - 1`. -/
lemma largeRange_max_val : largeRange.max.toNat = 9999999999999999999 := by decide

/-- `(10 : UInt64).toNat = 10`. -/
lemma uint64_ten_toNat : (10 : UInt64).toNat = 10 := rfl

/-- `UInt64.size = 2^64` as a concrete numeral. -/
lemma uint64_size_val : UInt64.size = 18446744073709551616 := rfl

/-- A normalized `Number` with zero mantissa is `Number.zero`. Re-derived inline
all over the operator-rounding tree; centralized here so every layer shares it. -/
lemma Number.eq_zero_of_mantissa_zero (n : Number) (hn : n.isNormalized)
    (h : n.mantissa_ = 0) : n = Number.zero := by
  rcases hn with h_zero | ⟨hmin, _, _, _, _⟩
  · exact h_zero
  · exfalso
    have h' : n.mantissa_.toNat = 0 := by rw [h]; rfl
    have hmin' := UInt64.le_iff_toNat_le.mp hmin
    rw [largeRange_min_val] at hmin'
    omega

lemma cusp_zm_qbounds {zm : UInt64} (h_lo : maxRep.toNat < zm.toNat)
    (h_hi : zm.toNat ≤ maxRepUp.toNat) :
    (maxRepNat : ℚ) < (zm.toNat : ℚ) ∧ (zm.toNat : ℚ) ≤ maxRepNat + 3 := by
  refine ⟨?_, ?_⟩
  · have : (maxRepNat : ℕ) < zm.toNat := by rw [← maxRep_val]; exact h_lo
    exact_mod_cast this
  · have h1 : zm.toNat ≤ 9223372036854775810 := by
      rw [show maxRepUp.toNat = maxRepUpNat from rfl] at h_hi
      exact h_hi
    calc (zm.toNat : ℚ) ≤ ((9223372036854775810 : ℕ) : ℚ) := by exact_mod_cast h1
      _ = maxRepNat + 3 := by norm_num

lemma one_sub_f_lt_releps {zm : UInt64} {f : ℚ}
    (h_zm_lo : mantissaFloor ≤ zm.toNat)
    (h_floor_cusp : zm.toNat = mantissaFloor → (8 : ℚ) / 10 ≤ f)
    (hf_pos : 0 < f) (hf_lt1 : f < 1) :
    (1 - f) < ((zm.toNat : ℚ) + f) * (10 / ((2 ^ 63 + 2 : ℚ))) := by
  rw [show ((zm.toNat : ℚ) + f) * (10 / ((2 ^ 63 + 2 : ℚ)))
        = 10 * ((zm.toNat : ℚ) + f) / (2 ^ 63 + 2 : ℚ) by ring]
  rw [lt_div_iff₀ (by norm_num : (0 : ℚ) < 2 ^ 63 + 2)]
  by_cases h_floor : zm.toNat = mantissaFloor
  · have hf_ge := h_floor_cusp h_floor
    have hq : (zm.toNat : ℚ) = mantissaFloor := by rw [h_floor]; norm_num
    rw [hq]; nlinarith [hf_ge, hf_lt1]
  · have h_gt : mantissaFloorSucc ≤ zm.toNat := by omega
    have hq : (mantissaFloorSucc : ℚ) ≤ (zm.toNat : ℚ) := by exact_mod_cast h_gt
    nlinarith [hq, hf_pos, hf_lt1]

lemma f_lt_releps {zm : UInt64} {f : ℚ}
    (hzm_q_ge : (mantissaFloor : ℚ) ≤ (zm.toNat : ℚ)) (hf_lt1 : f < 1) :
    f < ((zm.toNat : ℚ) + f) * (10 / ((2 ^ 63 + 2 : ℚ))) := by
  rw [show ((zm.toNat : ℚ) + f) * (10 / ((2 ^ 63 + 2 : ℚ)))
        = 10 * ((zm.toNat : ℚ) + f) / (2 ^ 63 + 2 : ℚ) by ring]
  rw [lt_div_iff₀ (by norm_num : (0 : ℚ) < 2 ^ 63 + 2)]
  have h_key : f * (mantissaFloor : ℚ) < (zm.toNat : ℚ) := by
    nlinarith [hf_lt1, hzm_q_ge]
  nlinarith [h_key]

lemma zn_eq_false_of_pos {zn : Bool} {t : ℚ} (h : zn = true → t ≤ 0) (h_pos : 0 < t) :
    zn = false := by
  rcases hzn : zn with _ | _
  · rfl
  · exact absurd (h hzn) (not_le.mpr h_pos)

lemma zn_eq_true_of_neg {zn : Bool} {t : ℚ} (h : zn = false → 0 ≤ t) (h_neg : t < 0) :
    zn = true := by
  rcases hzn : zn with _ | _
  · exact absurd (h hzn) (not_le.mpr h_neg)
  · rfl

lemma releps_lift {a b c p : ℚ} (h : a < b * c) (hp : 0 < p) : a * p < b * p * c := by
  calc a * p < b * c * p := mul_lt_mul_of_pos_right h hp
    _ = b * p * c := by ring

lemma cusp_err_lt_releps {q f : ℚ} (h_le3 : q ≤ maxRepNat + 3) (hf_lt1 : f < 1) :
    (q + f) - maxRepNat < (q + f) * (10 / ((2 ^ 63 + 2 : ℚ))) := by
  rw [show (q + f) * (10 / ((2 ^ 63 + 2 : ℚ))) = 10 * (q + f) / (2 ^ 63 + 2 : ℚ) by ring,
      lt_div_iff₀ (by norm_num : (0 : ℚ) < 2 ^ 63 + 2)]
  nlinarith [h_le3, hf_lt1]

end XRPL.Model.Protocol
