import XRPL.Properties.Protocol.Number.Common.Notation
import Mathlib.Tactic

import XRPL.Properties.Protocol.Number.Common.Constants
import XRPL.Properties.Protocol.Number.Common.ToRatLemmas
import XRPL.Properties.Protocol.Number.Common.Rounding.ScaleDown

set_option linter.unusedTactic false
set_option linter.unusedSimpArgs false
set_option linter.unreachableTactic false

namespace XRPL.Model.Protocol

/-! # Error bound for `Guard.doRoundUp`

Absolute error of `doRoundUp` is at most `8 · 10^e`. The cusp case
(overflow + rescale at `m = maxRep`) dominates; non-cusp branches are ≤ `(1/2) · 10^e`.
-/

/-- `RoundResult` value as a rational. -/
noncomputable def RoundResult.toRat (r : RoundResult) : ℚ := r.toNumber.toRat

/-! ## Helper facts -/

/-- `m + 1` does not overflow UInt64 when `m ≤ maxRep`. -/
lemma m_add_one_no_overflow {m : UInt64} (h : m.toNat ≤ maxRep.toNat) :
    (m + 1).toNat = m.toNat + 1 := by
  rw [UInt64.toNat_add]
  have h1 : (1 : UInt64).toNat = 1 := rfl
  rw [h1]
  have : m.toNat + 1 < 2 ^ 64 := by
    rw [maxRep_val] at h; omega
  exact Nat.mod_eq_of_lt this

/-- `m * 10` does not overflow UInt64 when `m < largeRange.min`. -/
lemma m_mul_ten_no_overflow {m : UInt64}
    (h : m.toNat < largeRange.min.toNat) :
    (m * 10).toNat = m.toNat * 10 := by
  rw [UInt64.toNat_mul]
  have h10 : (10 : UInt64).toNat = 10 := uint64_ten_toNat
  rw [h10]
  have : m.toNat * 10 < 2 ^ 64 := by
    rw [largeRange_min_val] at h
    calc m.toNat * 10 < 1000000000000000000 * 10 := by
          exact (Nat.mul_lt_mul_right (by norm_num : 0 < 10)).mpr h
      _ = tenPow19 := by norm_num
      _ < 2 ^ 64 := by norm_num
  exact Nat.mod_eq_of_lt this

/-- The `×1000` mantissa lift does not overflow for a 16-digit mantissa
(`m.toNat < 10^16`): `(m*10*10*10).toNat = m.toNat * 1000`. -/
lemma m_mul_thousand_no_overflow {m : UInt64} (h : m.toNat < 10 ^ 16) :
    (m * 10 * 10 * 10).toNat = m.toNat * 1000 := by
  have hmin : largeRange.min.toNat = 1000000000000000000 := largeRange_min_val
  have hb : m.toNat < 10000000000000000 := by
    have h16 : (10 : ℕ) ^ 16 = 10000000000000000 := by norm_num
    omega
  have h1 : (m * 10).toNat = m.toNat * 10 := m_mul_ten_no_overflow (by rw [hmin]; omega)
  have h2 : (m * 10 * 10).toNat = m.toNat * 100 := by
    rw [m_mul_ten_no_overflow (by rw [hmin, h1]; omega), h1]; ring
  rw [m_mul_ten_no_overflow (by rw [hmin, h2]; omega), h2]; ring

/-- The `÷1000` mantissa drop: `(m/10/10/10).toNat = m.toNat / 1000` (no wrap). -/
lemma m_div_thousand_toNat (m : UInt64) :
    (m / 10 / 10 / 10).toNat = m.toNat / 1000 := by
  rw [UInt64.toNat_div, UInt64.toNat_div, UInt64.toNat_div, uint64_ten_toNat,
      Nat.div_div_eq_div_mul, Nat.div_div_eq_div_mul]

/-! ## Main error bound -/
/-! ## Facts about `represents` -/

/-- `represents g f` implies `0 ≤ f`. -/
lemma represents_nonneg {g : Guard} {f : ℚ} (hrep : represents g f) : 0 ≤ f := by
  obtain ⟨x, hx_nn, _, hf_eq, _, _⟩ := hrep
  rw [hf_eq]
  have : (0 : ℚ) ≤ (decimalValue g.digits_ : ℚ) / 10 ^ 16 := by positivity
  linarith

/-- `represents g f` implies `f < 1`. -/
lemma represents_lt_one {g : Guard} {f : ℚ} (hrep : represents g f) : f < 1 := by
  obtain ⟨x, hx_nn, hx_lt, hf_eq, _, hall⟩ := hrep
  have hdv_lt : decimalValue g.digits_ < 10 ^ 16 := by
    unfold decimalValue
    have hbound : ∀ p ∈ Finset.range 16, nibble g.digits_ p * 10 ^ p ≤ 9 * 10 ^ p := by
      intro p hp
      have : nibble g.digits_ p ≤ 9 := hall ⟨p, Finset.mem_range.mp hp⟩
      exact Nat.mul_le_mul_right _ this
    calc ∑ p ∈ Finset.range 16, nibble g.digits_ p * 10 ^ p
        ≤ ∑ p ∈ Finset.range 16, 9 * 10 ^ p := Finset.sum_le_sum hbound
      _ < 10 ^ 16 := by decide
  rw [hf_eq]
  have hdv_q : (decimalValue g.digits_ : ℚ) < 10 ^ 16 := by exact_mod_cast hdv_lt
  have h16_pos : (0 : ℚ) < 10 ^ 16 := by positivity
  have h_top : (decimalValue g.digits_ : ℚ) / 10 ^ 16 < 1 := by
    rw [div_lt_iff₀ h16_pos]; linarith
  have hx_lt_q : x < 1 / 10 ^ 16 := hx_lt
  have : (decimalValue g.digits_ : ℚ) / 10 ^ 16 + x < 1 / 10 ^ 16 + (1 - 1 / 10 ^ 16) := by
    have hdv_lt' : (decimalValue g.digits_ : ℚ) / 10 ^ 16 ≤ 1 - 1 / 10 ^ 16 := by
      have hdv_le : decimalValue g.digits_ + 1 ≤ 10 ^ 16 := by omega
      have hdv_q' : ((decimalValue g.digits_ + 1 : ℕ) : ℚ) ≤ ((10 ^ 16 : ℕ) : ℚ) := by
        exact_mod_cast hdv_le
      push_cast at hdv_q'
      have : (decimalValue g.digits_ : ℚ) ≤ 10 ^ 16 - 1 := by linarith
      rw [div_le_iff₀ h16_pos]
      have : (10 : ℚ) ^ 16 * (1 - 1 / 10 ^ 16) = 10 ^ 16 - 1 := by
        field_simp
      linarith
    linarith
  linarith

/-! ## Helper lemmas for the new model -/

/-- `Guard.pushOverflow` is a no-op when `mantissa < maxRep` (strictly below the cusp). -/
lemma pushOverflow_noop_of_lt_maxRep {m : UInt64} (hlt : m.toNat < maxRep.toNat)
    (g : Guard) (mode : rounding_mode) :
    g.pushOverflow m mode = g := by
  unfold Guard.pushOverflow
  rw [if_neg]
  intro ⟨h_ge, _⟩
  have : maxRep.toNat ≤ m.toNat := UInt64.le_iff_toNat_le.mp h_ge
  omega

/-- With an empty guard `pushOverflow` is a no-op for any `mantissa ≤ maxRep` -/
lemma pushOverflow_noop_of_le_maxRep_of_empty {m : UInt64} (hle : m.toNat ≤ maxRep.toNat)
    (g : Guard) (mode : rounding_mode) (hempty : g.empty = true) :
    g.pushOverflow m mode = g := by
  rcases Nat.lt_or_ge m.toNat maxRep.toNat with hlt | hge
  · exact pushOverflow_noop_of_lt_maxRep hlt g mode
  · have hm : m = maxRep := UInt64.toNat_inj.mp (by omega)
    subst hm
    unfold Guard.pushOverflow
    rw [if_pos (by decide : maxRep ≤ maxRep ∧ maxRep < maxRepUp)]
    have hround : g.round mode = -2 := by unfold Guard.round; rw [if_pos hempty]
    simp only [hround]
    rfl

/-- `Guard.pushOverflow` is a no-op for any `mantissa ≤ maxRep` when `g.round mode ≠ 1` -/
lemma pushOverflow_noop_of_le_maxRep_of_round_ne_one {m : UInt64} (hle : m.toNat ≤ maxRep.toNat)
    (g : Guard) (mode : rounding_mode) (hr : g.round mode ≠ 1) :
    g.pushOverflow m mode = g := by
  rcases Nat.lt_or_ge m.toNat maxRep.toNat with hlt | hge
  · exact pushOverflow_noop_of_lt_maxRep hlt g mode
  · have hm : m = maxRep := UInt64.toNat_inj.mp (by omega)
    subst hm
    unfold Guard.pushOverflow
    rw [if_pos (by decide : maxRep ≤ maxRep ∧ maxRep < maxRepUp)]
    have hr_ne : (g.round mode == 1) = false := beq_eq_false_iff_ne.mpr hr
    have hmid : maxRep + (maxRepUp - maxRep) / 2 = (maxRep + 1 : UInt64) := by decide
    simp only [show maxRep % (10 : UInt64) < 9 from by decide, hmid, hr_ne,
               Bool.false_or]
    have hne : (maxRep == (maxRep + 1 : UInt64)) = false := by decide
    simp only [hne, Bool.and_false, ite_false]
    simp only [show (false : Bool) = true ↔ False from by decide, if_false,
               show (maxRep == maxRep) = true from rfl, ite_true]

/-- For an empty guard `pushOverflow` is a no-op whenever `mantissa` is NOT strictly in
the cusp interior `(maxRep, maxRepUp)`. -/
lemma pushOverflow_noop_of_empty {m : UInt64} (g : Guard) (mode : rounding_mode)
    (hempty : g.empty = true) (h_no_push : ¬ (maxRep < m ∧ m < maxRepUp)) :
    g.pushOverflow m mode = g := by
  unfold Guard.pushOverflow
  by_cases hc : maxRep ≤ m ∧ m < maxRepUp
  · rw [if_pos hc]
    have hm_eq : m = maxRep := by
      have h1 : ¬ (maxRep.toNat < m.toNat) :=
        fun h => h_no_push ⟨UInt64.lt_iff_toNat_lt.mpr h, hc.2⟩
      have h2 : maxRep.toNat ≤ m.toNat := UInt64.le_iff_toNat_le.mp hc.1
      exact UInt64.toNat_inj.mp (by omega)
    subst hm_eq
    have hround : g.round mode = -2 := by unfold Guard.round; rw [if_pos hempty]
    simp only [hround]
    rfl
  · rw [if_neg hc]

-- bringIntoRange helper lemmas.

/-- When `m < minM ∧ m ≠ 0`, `bringIntoRange` rescales once: result uses `m*10` and `e-1`. -/
lemma bringIntoRange_rescale_result {m : UInt64} {e : Int} {neg : Bool} {minM : UInt64}
    (hresc : m < minM) (hm_ne : m ≠ 0) :
    Guard.bringIntoRange neg m e minM =
    if e - 1 < minExponent ∨ m * 10 = 0 then
      { negative_ := false, mantissa_ := 0, exponent_ := -2147483648 }
    else
      { negative_ := neg, mantissa_ := m * 10, exponent_ := e - 1 } := by
  unfold Guard.bringIntoRange
  rw [if_pos (And.intro hresc hm_ne)]

/-- When `¬ (m < minM ∧ m ≠ 0)`, `bringIntoRange` keeps `m` and `e`. -/
lemma bringIntoRange_noscale_result {m : UInt64} {e : Int} {neg : Bool} {minM : UInt64}
    (hnresc : ¬ (m < minM ∧ m ≠ 0)) :
    Guard.bringIntoRange neg m e minM =
    if e < minExponent ∨ m = 0 then
      { negative_ := false, mantissa_ := 0, exponent_ := -2147483648 }
    else
      { negative_ := neg, mantissa_ := m, exponent_ := e } := by
  unfold Guard.bringIntoRange
  simp only [if_neg hnresc]

/-! ## Structural helpers for `Guard.doRoundUp` -/

/-- If the output mantissa is non-zero, the output sign equals the input `zn`. -/
lemma doRoundUp_negative_of_mant_ne
    (g : Guard) (zn : Bool) (m : UInt64) (e : Int)
    (minMant maxMant : UInt64) (mode : rounding_mode)
    (loc : String) (res : RoundResult)
    (hok : g.doRoundUp zn m e minMant maxMant mode loc = .ok res)
    (hne : res.mantissa_ ≠ 0) :
    res.negative_ = zn := by
  unfold Guard.doRoundUp Guard.bringIntoRange at hok
  simp only [Guard.doDropDigit] at hok
  split_ifs at hok with h1 h2 h3 h4 h5 h6 h7 h8 h9 h10 <;>
    (try (simp only [Except.ok.injEq] at hok)) <;>
    (try (simp only [reduceCtorEq] at hok)) <;>
    first
    | (subst hok; exact absurd rfl hne)
    | (subst hok; rfl)

set_option maxHeartbeats 1600000 in
-- split_ifs at hok ⊢ with 10 conditions creates ~2^10 cases; extra heartbeats needed
lemma doRoundUp_false_from_ok
    (g : Guard) (zn : Bool) (m : UInt64) (e : Int) (mode : rounding_mode)
    (loc : String) (res : RoundResult)
    (hok : g.doRoundUp zn m e largeRange.min largeRange.max mode loc = .ok res) :
    g.doRoundUp false m e largeRange.min largeRange.max mode loc =
      .ok { negative_ := false, mantissa_ := res.mantissa_, exponent_ := res.exponent_ } := by
  -- The output mantissa/exponent are independent of `negative_`, so the `false`
  -- case succeeds with the same value. Proved by case analysis on the branches.
  unfold Guard.doRoundUp Guard.bringIntoRange at hok ⊢
  simp only [Guard.doDropDigit] at hok ⊢
  split_ifs at hok ⊢ with h1 h2 h3 h4 h5 h6 h7 h8 h9 h10 <;>
    (try (simp only [reduceCtorEq] at hok)) <;>
    (try (simp only [Except.ok.injEq] at hok)) <;>
    (try (obtain rfl := Except.ok.inj hok)) <;>
    (try subst hok) <;>
    (try rfl) <;>
    (try ring_nf)

end XRPL.Model.Protocol
