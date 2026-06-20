import XRPL.Properties.Protocol.Number.Common.Notation
import Mathlib.Data.Int.Log

import XRPL.Properties.Protocol.Number.Common.Constants
import XRPL.Properties.Protocol.Number.Common.Closest.Helpers


namespace XRPL.Model.Protocol

/-! ## Number.upper and Number.lower (positive case helpers) -/

/-- For positive `q`, the candidate mantissa is `q · 10^(-(Int.log 10 q - mantissaLog))`.
This lands in `[10^18, 10^19)` by construction. The integer ceiling is then
bumped to a valid mantissa.

Corner cases:
- `e_q < minExponent`: q is below the smallest positive representable. Return
  the smallest positive representable (`largeRange.min` at `minExponent`).
- `e_q > maxExponent`: q is above the largest positive representable. Return
  `none` (genuinely no representable ≥ q). -/
def upperPosAux (q : ℚ) : Option Number :=
  let e_q : ℤ := Int.log 10 q - mantissaLog
  let m_real : ℚ := q * (10 : ℚ)^(-e_q)
  let m_ceil_nat : ℕ := ⌈m_real⌉₊
  match bumpToValidMantissa m_ceil_nat with
  | some m =>
    if h_exp : minExponent ≤ e_q ∧ e_q ≤ maxExponent ∧ m < 2^64 then
      some ⟨false, ⟨m, by omega⟩, e_q⟩
    else if e_q < minExponent then
        some ⟨false, largeRange.min, minExponent⟩
    else none
  | none =>
    if h_exp : minExponent ≤ e_q + 1 ∧ e_q + 1 ≤ maxExponent then
      some ⟨false, largeRange.min, e_q + 1⟩
    else if e_q + 1 < minExponent then
      some ⟨false, largeRange.min, minExponent⟩
    else none

/-- For positive `q`, the largest representable Number ≤ q.

Corner cases:
- `e_q < minExponent`: no positive normalized representable ≤ q exists, but
  `Number.zero.toRat = 0 ≤ q` (since `q > 0`), so return `Number.zero`.
- `e_q > maxExponent`: q is above the largest positive representable. Return
  `none` (handled separately by the maxExponent corner). -/
def lowerPosAux (q : ℚ) : Option Number :=
  let e_q : ℤ := Int.log 10 q - mantissaLog
  let m_real : ℚ := q * (10 : ℚ)^(-e_q)
  let m_floor_nat : ℕ := ⌊m_real⌋₊
  let m := truncToValidMantissa m_floor_nat
  if h_pos : m ≥ largeRange.min.toNat ∧ m < 2^64 then
    if minExponent ≤ e_q ∧ e_q ≤ maxExponent then
      some ⟨false, ⟨m, by omega⟩, e_q⟩
    else if e_q < minExponent then
      some Number.zero
    else none
  else
    if minExponent ≤ e_q - 1 ∧ e_q - 1 ≤ maxExponent then
      some ⟨false, ⟨maxMul10Witness, by decide⟩, e_q - 1⟩
    else if e_q - 1 < minExponent then
      some Number.zero
    else none

/-! ## Top-level Number.upper / Number.lower -/

/-- Smallest representable Number `n` with `n.toRat ≥ q`, if it exists.
For `q < 0`, uses `upper(q) = -lower(-q)` with a `Number.zero` short-circuit. -/
def Number.upper (q : ℚ) : Option Number :=
  if q = 0 then some Number.zero
  else if q < 0 then
    (lowerPosAux (-q)).map (fun n =>
      if n = Number.zero then Number.zero
      else { n with negative_ := true })
  else
    upperPosAux q

/-- Largest representable Number `n` with `n.toRat ≤ q`, if it exists.
For `q < 0`, uses `lower(q) = -upper(-q)`. -/
def Number.lower (q : ℚ) : Option Number :=
  if q = 0 then some Number.zero
  else if q < 0 then
    (upperPosAux (-q)).map (fun n => { n with negative_ := true })
  else
    lowerPosAux q

/-! ## Zero corners: values below the smallest representable magnitude -/

/-- For `0 < q` below the smallest positive representable, the largest
representable `≤ q` is `Number.zero`. -/
theorem Number.lower_eq_zero_of_pos_small (q : ℚ) (hq : 0 < q)
    (h_small : q < (10 : ℚ) ^ (18 : ℕ) * (10 : ℚ) ^ (minExponent : ℤ)) :
    Number.lower q = some Number.zero := by
  have h_e_q : Int.log 10 q - mantissaLog < minExponent := by
    by_contra h_not
    push_neg at h_not
    have h_log_ge : minExponent + 18 ≤ Int.log 10 q := by omega
    have h_pow_mono : (10 : ℚ) ^ (minExponent + 18) ≤ (10 : ℚ) ^ (Int.log 10 q) :=
      zpow_le_zpow_right₀ (by norm_num) h_log_ge
    have h_log_le : (10 : ℚ) ^ (Int.log 10 q) ≤ q :=
      Int.zpow_log_le_self (by norm_num : (1 : ℕ) < 10) hq
    have h_pow_form : (10 : ℚ) ^ (18 : ℕ) * (10 : ℚ) ^ (minExponent : ℤ)
        = (10 : ℚ) ^ (minExponent + 18) := by
      rw [show (minExponent + 18 : ℤ) = (18 : ℕ) + minExponent from by push_cast; ring,
          zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0), zpow_natCast]
    rw [h_pow_form] at h_small
    linarith [le_trans h_pow_mono h_log_le]
  unfold Number.lower
  rw [if_neg (ne_of_gt hq), if_neg (not_lt.mpr (le_of_lt hq))]
  unfold lowerPosAux
  simp only []
  split_ifs with h1 h2 h3 h4 <;> first
    | rfl
    | (exfalso; omega)

/-- For `q < 0` with magnitude below the smallest positive representable, the
smallest representable `≥ q` is `Number.zero`. -/
theorem Number.upper_eq_zero_of_neg_small (q : ℚ) (hq : q < 0)
    (h_small : -q < (10 : ℚ) ^ (18 : ℕ) * (10 : ℚ) ^ (minExponent : ℤ)) :
    Number.upper q = some Number.zero := by
  have h_lower_neg : lowerPosAux (-q) = some Number.zero := by
    have hq' : 0 < -q := by linarith
    have h := Number.lower_eq_zero_of_pos_small (-q) hq' h_small
    unfold Number.lower at h
    rwa [if_neg (ne_of_gt hq'), if_neg (not_lt.mpr (le_of_lt hq'))] at h
  unfold Number.upper
  rw [if_neg (ne_of_lt hq), if_pos hq, h_lower_neg]
  simp only [Option.map_some]
  rw [if_true]

end XRPL.Model.Protocol
