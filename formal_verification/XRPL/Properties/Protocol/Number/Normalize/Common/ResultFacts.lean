import XRPL.Properties.Protocol.Number.Common.Notation
import Mathlib.Tactic

import XRPL.Properties.Protocol.Number.Normalize.Common.ToNearest.AlgorithmicFacts


namespace XRPL.Model.Protocol

/-! # Result-shape facts for `Number.normalize`

`Number.normalize` ends in `doRoundUp` with **no** re-normalize stage, so the
discrete-rounding consumers need direct proofs that the result is normalized
and that the mantissa stays `≤ maxRepUp` at the top exponent. Both follow from
a mode-generic pipeline walk (`normalize_doRoundUp_stage`) plus per-mode
`doRoundUp` output invariants. -/

/-- `doRoundUp` succeeds only with exponent `≤ maxExponent` (the final overflow
check errors otherwise). -/
lemma doRoundUp_exponent_le_max
    (g : Guard) (neg : Bool) (m : UInt64) (e : Int) (mode : rounding_mode) (loc : String)
    (res : RoundResult)
    (hok : g.doRoundUp neg m e largeRange.min largeRange.max mode loc = .ok res) :
    res.exponent_ ≤ maxExponent := by
  unfold Guard.doRoundUp at hok
  simp only [Guard.doDropDigit] at hok
  set gP : Guard := g.pushOverflow m mode with hgP_def
  have h_extract : ∀ r' : RoundResult,
      (if r'.exponent_ > maxExponent then (.error loc : Except String RoundResult)
       else .ok r') = .ok res → res.exponent_ ≤ maxExponent := by
    intro r' h
    by_cases h_ovf : r'.exponent_ > maxExponent
    · rw [if_pos h_ovf] at h; cases h
    · rw [if_neg h_ovf] at h
      rw [← Except.ok.inj h]
      exact not_lt.mp h_ovf
  by_cases hb : ((gP.round mode == 1) || ((gP.round mode == 0) && (m % 2 == 1))) = true
  · rw [if_pos hb] at hok
    by_cases hC1 : m < largeRange.max ∧ m < maxRep
    · rw [if_pos hC1] at hok
      exact h_extract _ hok
    · rw [if_neg hC1] at hok
      by_cases hC2 : maxRep < m ∧ m < maxRepUp
      · rw [if_pos hC2] at hok
        exact h_extract _ hok
      · rw [if_neg hC2] at hok
        exact h_extract _ hok
  · rw [if_neg hb] at hok
    by_cases hC2 : maxRep < m ∧ m < maxRepUp
    · rw [if_pos hC2] at hok
      exact h_extract _ hok
    · rw [if_neg hC2] at hok
      exact h_extract _ hok

set_option maxHeartbeats 800000 in
-- full 6-leaf doRoundUp navigation with per-leaf bringIntoRange case analysis
/-- At the top exponent the `doRoundUp` output mantissa stays `≤ maxRepUp`.
The two dangerous paths are dead: the drop-digit leg needs a fired round
decision with `m ∈ {maxRep, maxRepUp}`, where the guard is empty (so the
decision is the sentinel `-2`); the `bringIntoRange` rescale path lowers the
exponent below `maxExponent` (its input mantissa `< 10^18` certifies, via
`h_cap_exp`, that the incoming exponent was already `≤ maxExponent`). -/
lemma doRoundUp_mantissa_le_maxRepUp_at_maxExp
    (g : Guard) (neg : Bool) (m : UInt64) (e : Int) (mode : rounding_mode) (loc : String)
    (h_m_le : m.toNat ≤ maxRepUp.toNat)
    (h_cap_exp : m.toNat < 1000000000000000000 → e ≤ maxExponent)
    (h_empty_above : 1844674407370955161 < m.toNat → g.empty = true)
    (res : RoundResult)
    (hok : g.doRoundUp neg m e largeRange.min largeRange.max mode loc = .ok res)
    (h_exp_ge : res.exponent_ ≥ maxExponent) :
    res.mantissa_ ≤ maxRepUp := by
  have hmaxRepUp_toNat : maxRepUp.toNat = maxRepUpNat := rfl
  have hmaxRep_toNat : maxRep.toNat = 9223372036854775807 := maxRep_val
  -- Leaf analysis: a `bringIntoRange` result is a flush, a kept mantissa at the
  -- same exponent, or a rescaled small mantissa at the decremented exponent.
  have h_leaf : ∀ (m' : UInt64) (e' : Int),
      res = Guard.bringIntoRange neg m' e' largeRange.min →
      res.mantissa_.toNat = 0 ∨
      (res.mantissa_.toNat = m'.toNat ∧ res.exponent_ = e') ∨
      (m'.toNat < 10 ^ 18 ∧ res.exponent_ = e' - 1) := by
    intro m' e' hres_eq
    have hmin_v : largeRange.min.toNat = 1000000000000000000 := largeRange_min_val
    by_cases hm0 : m' = 0
    · rw [bringIntoRange_noscale_result (by intro ⟨_, hne⟩; exact hne hm0),
          if_pos (Or.inr hm0)] at hres_eq
      subst hres_eq
      left
      rfl
    by_cases hresc : m' < largeRange.min
    · rw [bringIntoRange_rescale_result hresc hm0] at hres_eq
      by_cases h_under : e' - 1 < minExponent ∨ m' * 10 = 0
      · rw [if_pos h_under] at hres_eq
        subst hres_eq
        left
        rfl
      · rw [if_neg h_under] at hres_eq
        subst hres_eq
        right; right
        refine ⟨?_, rfl⟩
        have h1 := UInt64.lt_iff_toNat_lt.mp hresc
        omega
    · rw [bringIntoRange_noscale_result (by intro ⟨h, _⟩; exact hresc h)] at hres_eq
      by_cases h_under : e' < minExponent ∨ m' = 0
      · rw [if_pos h_under] at hres_eq
        subst hres_eq
        left
        rfl
      · rw [if_neg h_under] at hres_eq
        subst hres_eq
        right; left
        exact ⟨rfl, rfl⟩
  unfold Guard.doRoundUp at hok
  simp only [Guard.doDropDigit] at hok
  set gP : Guard := g.pushOverflow m mode with hgP_def
  have h_extract : ∀ r' : RoundResult,
      (if r'.exponent_ > maxExponent then (.error loc : Except String RoundResult)
       else .ok r') = .ok res → res = r' := by
    intro r' h
    by_cases h_ovf : r'.exponent_ > maxExponent
    · rw [if_pos h_ovf] at h; cases h
    · rw [if_neg h_ovf] at h; exact (Except.ok.inj h).symm
  by_cases hb : ((gP.round mode == 1) || ((gP.round mode == 0) && (m % 2 == 1))) = true
  · rw [if_pos hb] at hok
    by_cases hC1 : m < largeRange.max ∧ m < maxRep
    · -- round-up, in-range: bringIntoRange (m+1).
      rw [if_pos hC1] at hok
      have hres_eq := h_extract _ hok
      have hzm_lt : m.toNat < maxRep.toNat := UInt64.lt_iff_toNat_lt.mp hC1.2
      have hadd : (m + 1).toNat = m.toNat + 1 := m_add_one_no_overflow (le_of_lt hzm_lt)
      rcases h_leaf _ _ hres_eq with h0 | ⟨hm, _⟩ | ⟨hsmall, hexp⟩
      · rw [UInt64.le_iff_toNat_le]; omega
      · rw [UInt64.le_iff_toNat_le]; omega
      · exfalso
        have hsm : m.toNat < 1000000000000000000 := by omega
        have := h_cap_exp hsm
        omega
    · rw [if_neg hC1] at hok
      by_cases hC2 : maxRep < m ∧ m < maxRepUp
      · -- round-up, cusp clamp to maxRepUp.
        rw [if_pos hC2] at hok
        have hres_eq := h_extract _ hok
        rcases h_leaf _ _ hres_eq with h0 | ⟨hm, _⟩ | ⟨hsmall, _⟩
        · rw [UInt64.le_iff_toNat_le]; omega
        · rw [UInt64.le_iff_toNat_le]; omega
        · exfalso; omega
      · -- drop-digit leg: m ∈ {maxRep, maxRepUp}, where the guard is empty and
        -- the round decision cannot have fired.
        exfalso
        have hzm_lt_max : m < largeRange.max := by
          rw [UInt64.lt_iff_toNat_lt, largeRange_max_val]
          omega
        have hzm_ge_rep : maxRep.toNat ≤ m.toNat := by
          by_contra hlt
          push_neg at hlt
          exact hC1 ⟨hzm_lt_max, UInt64.lt_iff_toNat_lt.mpr hlt⟩
        have hzm_cases : m.toNat = maxRep.toNat ∨ m.toNat = maxRepUp.toNat := by
          by_contra hno
          push_neg at hno
          apply hC2
          constructor
          · rw [UInt64.lt_iff_toNat_lt]; omega
          · rw [UInt64.lt_iff_toNat_lt]; omega
        have hempty : g.empty = true := h_empty_above (by omega)
        have hgP_eq : gP = g := by
          rcases hzm_cases with h | h
          · rw [hgP_def]
            exact pushOverflow_noop_of_le_maxRep_of_empty (by omega) g mode hempty
          · rw [hgP_def]
            unfold Guard.pushOverflow
            rw [if_neg]
            intro ⟨_, h2⟩
            have := UInt64.lt_iff_toNat_lt.mp h2; omega
        rw [hgP_eq, empty_guard_round_neg_two g mode hempty] at hb
        simp at hb
  · rw [if_neg hb] at hok
    by_cases hC2 : maxRep < m ∧ m < maxRepUp
    · -- truncate, cusp clamp to maxRep.
      rw [if_pos hC2] at hok
      have hres_eq := h_extract _ hok
      rcases h_leaf _ _ hres_eq with h0 | ⟨hm, _⟩ | ⟨hsmall, _⟩
      · rw [UInt64.le_iff_toNat_le]; omega
      · rw [UInt64.le_iff_toNat_le]; omega
      · exfalso; omega
    · -- truncate, identity.
      rw [if_neg hC2] at hok
      have hres_eq := h_extract _ hok
      rcases h_leaf _ _ hres_eq with h0 | ⟨hm, _⟩ | ⟨hsmall, hexp⟩
      · rw [UInt64.le_iff_toNat_le]; omega
      · rw [UInt64.le_iff_toNat_le]; omega
      · exfalso
        have hsm : m.toNat < 1000000000000000000 := by omega
        have := h_cap_exp hsm
        omega

set_option maxHeartbeats 800000 in
-- full 6-leaf doRoundUp navigation including the live drop-digit leg
/-- Weak top-exponent mantissa cap, without any guard-emptiness hypothesis:
the `doRoundUp` output mantissa stays `≤ maxRepNat + 13` (`= 9223372036854775820`)
at the top exponent. The drop-digit leg can fire here (a `doNormalize128`
caller seeds a sticky guard), but its rescaled output is at most
`(maxRepUp/10 + 1)·10`. -/
lemma doRoundUp_mantissa_le_cuspTop_at_maxExp
    (g : Guard) (neg : Bool) (m : UInt64) (e : Int) (mode : rounding_mode) (loc : String)
    (h_m_le : m.toNat ≤ maxRepUp.toNat)
    (h_cap_exp : m.toNat < 1000000000000000000 → e ≤ maxExponent)
    (res : RoundResult)
    (hok : g.doRoundUp neg m e largeRange.min largeRange.max mode loc = .ok res)
    (h_exp_ge : res.exponent_ ≥ maxExponent) :
    res.mantissa_.toNat ≤ 9223372036854775820 := by
  have hmaxRepUp_toNat : maxRepUp.toNat = maxRepUpNat := rfl
  have hmaxRep_toNat : maxRep.toNat = 9223372036854775807 := maxRep_val
  -- Extended leaf analysis: the rescale arm also reports the tenfold mantissa.
  have h_leaf : ∀ (m' : UInt64) (e' : Int),
      res = Guard.bringIntoRange neg m' e' largeRange.min →
      res.mantissa_.toNat = 0 ∨
      (res.mantissa_.toNat = m'.toNat ∧ res.exponent_ = e') ∨
      (res.mantissa_.toNat = m'.toNat * 10 ∧ m'.toNat < 10 ^ 18 ∧ res.exponent_ = e' - 1) := by
    intro m' e' hres_eq
    have hmin_v : largeRange.min.toNat = 1000000000000000000 := largeRange_min_val
    by_cases hm0 : m' = 0
    · rw [bringIntoRange_noscale_result (by intro ⟨_, hne⟩; exact hne hm0),
          if_pos (Or.inr hm0)] at hres_eq
      subst hres_eq
      left
      rfl
    by_cases hresc : m' < largeRange.min
    · have hm_lt18 : m'.toNat < 10 ^ 18 := by
        have h1 := UInt64.lt_iff_toNat_lt.mp hresc
        omega
      rw [bringIntoRange_rescale_result hresc hm0] at hres_eq
      by_cases h_under : e' - 1 < minExponent ∨ m' * 10 = 0
      · rw [if_pos h_under] at hres_eq
        subst hres_eq
        left
        rfl
      · rw [if_neg h_under] at hres_eq
        subst hres_eq
        right; right
        exact ⟨m_mul_ten_no_overflow hm_lt18, hm_lt18, rfl⟩
    · rw [bringIntoRange_noscale_result (by intro ⟨h, _⟩; exact hresc h)] at hres_eq
      by_cases h_under : e' < minExponent ∨ m' = 0
      · rw [if_pos h_under] at hres_eq
        subst hres_eq
        left
        rfl
      · rw [if_neg h_under] at hres_eq
        subst hres_eq
        right; left
        exact ⟨rfl, rfl⟩
  unfold Guard.doRoundUp at hok
  simp only [Guard.doDropDigit] at hok
  set gP : Guard := g.pushOverflow m mode with hgP_def
  have h_extract : ∀ r' : RoundResult,
      (if r'.exponent_ > maxExponent then (.error loc : Except String RoundResult)
       else .ok r') = .ok res → res = r' := by
    intro r' h
    by_cases h_ovf : r'.exponent_ > maxExponent
    · rw [if_pos h_ovf] at h; cases h
    · rw [if_neg h_ovf] at h; exact (Except.ok.inj h).symm
  by_cases hb : ((gP.round mode == 1) || ((gP.round mode == 0) && (m % 2 == 1))) = true
  · rw [if_pos hb] at hok
    by_cases hC1 : m < largeRange.max ∧ m < maxRep
    · rw [if_pos hC1] at hok
      have hres_eq := h_extract _ hok
      have hzm_lt : m.toNat < maxRep.toNat := UInt64.lt_iff_toNat_lt.mp hC1.2
      have hadd : (m + 1).toNat = m.toNat + 1 := m_add_one_no_overflow (le_of_lt hzm_lt)
      rcases h_leaf _ _ hres_eq with h0 | ⟨hm, _⟩ | ⟨_, hsmall, hexp⟩
      · omega
      · omega
      · exfalso
        have := h_cap_exp (by omega)
        omega
    · rw [if_neg hC1] at hok
      by_cases hC2 : maxRep < m ∧ m < maxRepUp
      · rw [if_pos hC2] at hok
        have hres_eq := h_extract _ hok
        rcases h_leaf _ _ hres_eq with h0 | ⟨hm, _⟩ | ⟨_, hsmall, _⟩
        · omega
        · omega
        · exfalso; omega
      · -- drop-digit leg.
        rw [if_neg hC2] at hok
        have h_div_toNat : (m / 10).toNat = m.toNat / 10 := by
          rw [UInt64.toNat_div]; rfl
        by_cases hb' : (((gP.push (m % 10)).round mode == 1)
            || (((gP.push (m % 10)).round mode == 0) && (m / 10 % 2 == 1))) = true
        · rw [if_pos hb'] at hok
          have hres_eq := h_extract _ hok
          have h_toNat : (m / 10 + 1).toNat = m.toNat / 10 + 1 := by
            rw [UInt64.toNat_add, h_div_toNat]
            rw [Nat.mod_eq_of_lt (by simpa using (by omega : m.toNat / 10 + 1 < 2 ^ 64))]
            rfl
          rcases h_leaf _ _ hres_eq with h0 | ⟨hm, _⟩ | ⟨hm, _, _⟩
          · omega
          · omega
          · omega
        · rw [if_neg hb'] at hok
          have hres_eq := h_extract _ hok
          rcases h_leaf _ _ hres_eq with h0 | ⟨hm, _⟩ | ⟨hm, _, _⟩
          · omega
          · omega
          · omega
  · rw [if_neg hb] at hok
    by_cases hC2 : maxRep < m ∧ m < maxRepUp
    · rw [if_pos hC2] at hok
      have hres_eq := h_extract _ hok
      rcases h_leaf _ _ hres_eq with h0 | ⟨hm, _⟩ | ⟨_, hsmall, _⟩
      · omega
      · omega
      · exfalso; omega
    · rw [if_neg hC2] at hok
      have hres_eq := h_extract _ hok
      rcases h_leaf _ _ hres_eq with h0 | ⟨hm, _⟩ | ⟨_, hsmall, hexp⟩
      · omega
      · omega
      · exfalso
        have := h_cap_exp (by omega)
        omega

/-- A successful `doRoundUp` at an exponent already above `maxExponent` with a
nonzero result forces the input mantissa below `10^18`: only the
`bringIntoRange` rescale arm (which lowers the exponent back) survives the
final overflow check. Same-sign addition's drop-digit leg hits this at
`e_common = maxExponent`. -/
lemma doRoundUp_ok_high_exp_mantissa_small
    (g : Guard) (neg : Bool) (m : UInt64) (e : Int) (mode : rounding_mode) (loc : String)
    (h_e_gt : maxExponent < e)
    (res : RoundResult)
    (hok : g.doRoundUp neg m e largeRange.min largeRange.max mode loc = .ok res)
    (h_mant_ne : res.mantissa_ ≠ 0) :
    m.toNat < 10 ^ 18 := by
  have hmaxRepUp_toNat : maxRepUp.toNat = maxRepUpNat := rfl
  have hmaxRep_toNat : maxRep.toNat = 9223372036854775807 := maxRep_val
  have h_mant_ne_nat : res.mantissa_.toNat ≠ 0 := by
    intro h0
    apply h_mant_ne
    rw [← UInt64.toNat_inj, h0]
    rfl
  have h_leaf : ∀ (m' : UInt64) (e' : Int),
      res = Guard.bringIntoRange neg m' e' largeRange.min →
      res.mantissa_.toNat = 0 ∨
      (res.mantissa_.toNat = m'.toNat ∧ res.exponent_ = e') ∨
      (res.mantissa_.toNat = m'.toNat * 10 ∧ m'.toNat < 10 ^ 18 ∧ res.exponent_ = e' - 1) := by
    intro m' e' hres_eq
    have hmin_v : largeRange.min.toNat = 1000000000000000000 := largeRange_min_val
    by_cases hm0 : m' = 0
    · rw [bringIntoRange_noscale_result (by intro ⟨_, hne⟩; exact hne hm0),
          if_pos (Or.inr hm0)] at hres_eq
      subst hres_eq
      left
      rfl
    by_cases hresc : m' < largeRange.min
    · have hm_lt18 : m'.toNat < 10 ^ 18 := by
        have h1 := UInt64.lt_iff_toNat_lt.mp hresc
        omega
      rw [bringIntoRange_rescale_result hresc hm0] at hres_eq
      by_cases h_under : e' - 1 < minExponent ∨ m' * 10 = 0
      · rw [if_pos h_under] at hres_eq
        subst hres_eq
        left
        rfl
      · rw [if_neg h_under] at hres_eq
        subst hres_eq
        right; right
        exact ⟨m_mul_ten_no_overflow hm_lt18, hm_lt18, rfl⟩
    · rw [bringIntoRange_noscale_result (by intro ⟨h, _⟩; exact hresc h)] at hres_eq
      by_cases h_under : e' < minExponent ∨ m' = 0
      · rw [if_pos h_under] at hres_eq
        subst hres_eq
        left
        rfl
      · rw [if_neg h_under] at hres_eq
        subst hres_eq
        right; left
        exact ⟨rfl, rfl⟩
  unfold Guard.doRoundUp at hok
  simp only [Guard.doDropDigit] at hok
  set gP : Guard := g.pushOverflow m mode with hgP_def
  have h_extract : ∀ r' : RoundResult,
      (if r'.exponent_ > maxExponent then (.error loc : Except String RoundResult)
       else .ok r') = .ok res → res = r' ∧ ¬ (res.exponent_ > maxExponent) := by
    intro r' h
    by_cases h_ovf : r'.exponent_ > maxExponent
    · rw [if_pos h_ovf] at h; cases h
    · rw [if_neg h_ovf] at h
      have heq := (Except.ok.inj h).symm
      refine ⟨heq, ?_⟩
      rw [heq]
      exact h_ovf
  by_cases hb : ((gP.round mode == 1) || ((gP.round mode == 0) && (m % 2 == 1))) = true
  · rw [if_pos hb] at hok
    by_cases hC1 : m < largeRange.max ∧ m < maxRep
    · rw [if_pos hC1] at hok
      obtain ⟨hres_eq, h_no_ovf⟩ := h_extract _ hok
      have hzm_lt : m.toNat < maxRep.toNat := UInt64.lt_iff_toNat_lt.mp hC1.2
      have hadd : (m + 1).toNat = m.toNat + 1 := m_add_one_no_overflow (le_of_lt hzm_lt)
      rcases h_leaf _ _ hres_eq with h0 | ⟨_, hexp⟩ | ⟨_, hsmall, _⟩
      · omega
      · exfalso; rw [hexp] at h_no_ovf; omega
      · omega
    · rw [if_neg hC1] at hok
      by_cases hC2 : maxRep < m ∧ m < maxRepUp
      · rw [if_pos hC2] at hok
        obtain ⟨hres_eq, h_no_ovf⟩ := h_extract _ hok
        rcases h_leaf _ _ hres_eq with h0 | ⟨_, hexp⟩ | ⟨_, hsmall, _⟩
        · omega
        · exfalso; rw [hexp] at h_no_ovf; omega
        · exfalso; omega
      · -- drop-digit leg: both rescale (exp e) and noscale (exp e+1) overflow.
        rw [if_neg hC2] at hok
        by_cases hb' : (((gP.push (m % 10)).round mode == 1)
            || (((gP.push (m % 10)).round mode == 0) && (m / 10 % 2 == 1))) = true
        · rw [if_pos hb'] at hok
          obtain ⟨hres_eq, h_no_ovf⟩ := h_extract _ hok
          rcases h_leaf _ _ hres_eq with h0 | ⟨_, hexp⟩ | ⟨_, _, hexp⟩
          · omega
          · exfalso; rw [hexp] at h_no_ovf; omega
          · exfalso; rw [hexp] at h_no_ovf; omega
        · rw [if_neg hb'] at hok
          obtain ⟨hres_eq, h_no_ovf⟩ := h_extract _ hok
          rcases h_leaf _ _ hres_eq with h0 | ⟨_, hexp⟩ | ⟨_, _, hexp⟩
          · omega
          · exfalso; rw [hexp] at h_no_ovf; omega
          · exfalso; rw [hexp] at h_no_ovf; omega
  · rw [if_neg hb] at hok
    by_cases hC2 : maxRep < m ∧ m < maxRepUp
    · rw [if_pos hC2] at hok
      obtain ⟨hres_eq, h_no_ovf⟩ := h_extract _ hok
      rcases h_leaf _ _ hres_eq with h0 | ⟨_, hexp⟩ | ⟨_, hsmall, _⟩
      · omega
      · exfalso; rw [hexp] at h_no_ovf; omega
      · exfalso; omega
    · rw [if_neg hC2] at hok
      obtain ⟨hres_eq, h_no_ovf⟩ := h_extract _ hok
      rcases h_leaf _ _ hres_eq with h0 | ⟨_, hexp⟩ | ⟨_, hsmall, _⟩
      · omega
      · exfalso; rw [hexp] at h_no_ovf; omega
      · omega

/-- Truth-top bound from a surviving `doRoundUp` stage: if
`|truth| = (zm + f)·10^ze'` with `zm ≤ maxRepUp`, `f < 1`, and the stage's
exponent is at most `maxExponent + 1`, then `|truth| < 10^19 · 10^maxExponent`.
At `ze' = maxExponent + 1` (addition's drop-digit leg at the top exponent) the
mantissa must have been below `10^18` for `doRoundUp` to succeed at all. -/
lemma doRoundUp_stage_truth_top (truth : ℚ) (g : Guard) (neg : Bool) (zm : UInt64) (ze' : Int)
    (f : ℚ) (mode : rounding_mode) (loc : String) (res : RoundResult)
    (habs : |truth| = ((zm.toNat : ℚ) + f) * 10 ^ ze')
    (hzm_le : zm.toNat ≤ maxRepUp.toNat)
    (_hf_nn : 0 ≤ f) (hf_lt : f < 1)
    (hze_le : ze' ≤ maxExponent + 1)
    (h_rup : g.doRoundUp neg zm ze' largeRange.min largeRange.max mode loc = .ok res)
    (h_mant_ne : res.mantissa_ ≠ 0) :
    |truth| < 10 ^ 19 * (10 : ℚ) ^ (maxExponent : ℤ) := by
  have h_pow_pos_ze : (0 : ℚ) < (10 : ℚ) ^ ze' := zpow_pos (by norm_num) _
  by_cases h_ze_top : maxExponent < ze'
  · -- Above the top: only the rescale arm survives, so zm < 10^18.
    have h_small : zm.toNat < 10 ^ 18 :=
      doRoundUp_ok_high_exp_mantissa_small g neg zm ze' mode loc h_ze_top res h_rup h_mant_ne
    have h_le_nat : zm.toNat ≤ 10 ^ 18 - 1 := by omega
    have h_cast : (zm.toNat : ℚ) ≤ ((10 ^ 18 - 1 : ℕ) : ℚ) := by exact_mod_cast h_le_nat
    have h_val : ((10 ^ 18 - 1 : ℕ) : ℚ) = 10 ^ 18 - 1 := by norm_num
    have h_zm_q : (zm.toNat : ℚ) + f < 10 ^ 18 := by
      rw [h_val] at h_cast
      linarith
    have h_pow_le : (10 : ℚ) ^ ze' ≤ (10 : ℚ) ^ (maxExponent + 1 : ℤ) :=
      zpow_le_zpow_right₀ (by norm_num) hze_le
    have h_lt : |truth| < 10 ^ 18 * (10 : ℚ) ^ (maxExponent + 1 : ℤ) := by
      rw [habs]
      calc ((zm.toNat : ℚ) + f) * 10 ^ ze'
          < 10 ^ 18 * 10 ^ ze' := mul_lt_mul_of_pos_right h_zm_q h_pow_pos_ze
        _ ≤ 10 ^ 18 * (10 : ℚ) ^ (maxExponent + 1 : ℤ) :=
            mul_le_mul_of_nonneg_left h_pow_le (by norm_num)
    have h_eq : (10 : ℚ) ^ 18 * (10 : ℚ) ^ (maxExponent + 1 : ℤ)
        = 10 ^ 19 * (10 : ℚ) ^ (maxExponent : ℤ) := by
      rw [zpow_add_one₀ (by norm_num : (10 : ℚ) ≠ 0)]
      ring
    rw [h_eq] at h_lt
    exact h_lt
  · push_neg at h_ze_top
    have h1 : zm.toNat ≤ 9223372036854775810 := by
      have h_v : maxRepUp.toNat = maxRepUpNat := rfl
      omega
    have h_cast : (zm.toNat : ℚ) ≤ (9223372036854775810 : ℚ) := by exact_mod_cast h1
    have h_zm_q : (zm.toNat : ℚ) + f < 10 ^ 19 := by
      have h_num : (9223372036854775810 : ℚ) + 1 < 10 ^ 19 := by norm_num
      linarith
    have h_pow_le : (10 : ℚ) ^ ze' ≤ (10 : ℚ) ^ (maxExponent : ℤ) :=
      zpow_le_zpow_right₀ (by norm_num) h_ze_top
    rw [habs]
    calc ((zm.toNat : ℚ) + f) * 10 ^ ze'
        < 10 ^ 19 * 10 ^ ze' := mul_lt_mul_of_pos_right h_zm_q h_pow_pos_ze
      _ ≤ 10 ^ 19 * (10 : ℚ) ^ (maxExponent : ℤ) :=
          mul_le_mul_of_nonneg_left h_pow_le (by norm_num)

/-- Mode-generic pipeline walk: a successful `normalize` with nonzero result
exposes the `doRoundUp` stage together with the input-side facts the result
lemmas need. -/
theorem normalize_doRoundUp_stage (n result : Number) (mode : rounding_mode)
    (hn_mant_ne : n.mantissa_ ≠ 0)
    (hok : n.normalize largeRange.min largeRange.max mode = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    ∃ (m3 : UInt64) (e3 : Int) (g3 : Guard) (res : RoundResult),
      g3.doRoundUp n.negative_ m3 e3 largeRange.min largeRange.max mode
        "Number::normalize 2" = .ok res ∧
      result = res.toNumber ∧
      mantissaFloor ≤ m3.toNat ∧
      m3.toNat ≤ maxRepUp.toNat ∧
      (m3.toNat < 1000000000000000000 → e3 ≤ maxExponent) ∧
      (1844674407370955161 < m3.toNat → g3.empty = true) := by
  unfold Number.normalize doNormalize at hok
  rw [beq_eq_false_iff_ne.mpr hn_mant_ne] at hok
  simp only [Bool.false_eq_true, if_false] at hok
  set su := doNormalize_scaleUp largeRange.min n.mantissa_ n.exponent_ with hsu_def
  set m1 : UInt64 := su.1 with hm1_def
  set e1 : Int := su.2 with he1_def
  set g0 : Guard := if n.negative_ then Guard.new.set_negative else Guard.new with hg0_def
  obtain ⟨m2, e2, g2, hsd⟩ : ∃ m2 e2 g2,
      doNormalize_scaleDown largeRange.max m1 e1 g0 = .ok (m2, e2, g2) := by
    match hsd : doNormalize_scaleDown largeRange.max m1 e1 g0 with
    | .error e => rw [hsd] at hok; exact absurd hok (by intro h; cases h)
    | .ok (m2, e2, g2) => exact ⟨m2, e2, g2, rfl⟩
  rw [hsd] at hok
  simp only at hok
  by_cases hzero : e2 < minExponent ∨ m2 < largeRange.min
  · rw [show (decide (e2 < minExponent) || decide (m2 < largeRange.min)) = true from by
      rcases hzero with h | h
      · simp [h]
      · simp [h]] at hok
    simp only [if_true] at hok
    have : Number.zero = result := Except.ok.inj hok
    rw [← this] at hresult
    exact absurd rfl hresult
  · push_neg at hzero
    obtain ⟨he2_ge, hm2_ge⟩ := hzero
    rw [show (decide (e2 < minExponent) || decide (m2 < largeRange.min)) = false from by
      rw [Bool.or_eq_false_iff]
      exact ⟨decide_eq_false (not_lt.mpr he2_ge), decide_eq_false hm2_ge⟩] at hok
    simp only [Bool.false_eq_true, if_false] at hok
    obtain ⟨m3, e3, g3, hcap⟩ : ∃ m3 e3 g3,
        doNormalize_capAtMaxRep m2 e2 g2 = .ok (m3, e3, g3) := by
      match hcap : doNormalize_capAtMaxRep m2 e2 g2 with
      | .error e => rw [hcap] at hok; exact absurd hok (by intro h; cases h)
      | .ok (m3, e3, g3) => exact ⟨m3, e3, g3, rfl⟩
    rw [hcap] at hok
    simp only at hok
    obtain ⟨res, hrup⟩ : ∃ res,
        g3.doRoundUp n.negative_ m3 e3 largeRange.min largeRange.max mode
          "Number::normalize 2" = .ok res := by
      match hrup : g3.doRoundUp n.negative_ m3 e3 largeRange.min largeRange.max mode
          "Number::normalize 2" with
      | .error e => rw [hrup] at hok; exact absurd hok (by intro h; cases h)
      | .ok res => exact ⟨res, rfl⟩
    rw [hrup] at hok
    simp only at hok
    have hresult_eq : result = res.toNumber := (Except.ok.inj hok).symm
    have hm2_ge_min : largeRange.min.toNat ≤ m2.toNat := by
      rw [UInt64.lt_iff_toNat_lt] at hm2_ge
      omega
    have hminMant_v : largeRange.min.toNat = 1000000000000000000 := largeRange_min_val
    have h64 : m2.toNat < 2 ^ 64 := UInt64.toNat_lt_size m2
    refine ⟨m3, e3, g3, res, hrup, hresult_eq, ?_, ?_, ?_, ?_⟩
    · -- mantissaFloor ≤ m3.
      unfold doNormalize_capAtMaxRep at hcap
      by_cases hmr : m2 > maxRepUp
      · rw [if_pos hmr] at hcap
        by_cases hexp3 : e2 ≥ maxExponent
        · rw [if_pos hexp3] at hcap; exact absurd hcap (by intro h; cases h)
        · rw [if_neg hexp3] at hcap
          simp only [divu10] at hcap
          have hm3_eq : m3 = m2 / 10 := ((Prod.mk.inj (Except.ok.inj hcap)).1).symm
          rw [hm3_eq, UInt64.toNat_div, show (10 : UInt64).toNat = 10 from rfl]
          have hmr_nat : maxRepUp.toNat < m2.toNat := UInt64.lt_iff_toNat_lt.mp hmr
          rw [show maxRepUp.toNat = maxRepUpNat from rfl] at hmr_nat
          omega
      · rw [if_neg hmr] at hcap
        have hm3_eq : m3 = m2 := ((Prod.mk.inj (Except.ok.inj hcap)).1).symm
        rw [hm3_eq]
        rw [hminMant_v] at hm2_ge_min
        omega
    · -- m3 ≤ maxRepUp.
      unfold doNormalize_capAtMaxRep at hcap
      by_cases hmr : m2 > maxRepUp
      · rw [if_pos hmr] at hcap
        by_cases hexp3 : e2 ≥ maxExponent
        · rw [if_pos hexp3] at hcap; exact absurd hcap (by intro h; cases h)
        · rw [if_neg hexp3] at hcap
          simp only [divu10] at hcap
          have hm3_eq : m3 = m2 / 10 := ((Prod.mk.inj (Except.ok.inj hcap)).1).symm
          rw [hm3_eq, UInt64.toNat_div, show (10 : UInt64).toNat = 10 from rfl,
              show maxRepUp.toNat = maxRepUpNat from rfl]
          omega
      · rw [if_neg hmr] at hcap
        have hm3_eq : m3 = m2 := ((Prod.mk.inj (Except.ok.inj hcap)).1).symm
        have hmr_nat : ¬ maxRepUp.toNat < m2.toNat := fun h =>
          hmr (UInt64.lt_iff_toNat_lt.mpr h)
        rw [hm3_eq]
        omega
    · -- m3 < 10^18 → e3 ≤ maxExponent (only the cap divide can shrink below 10^18).
      intro hm3_lt
      unfold doNormalize_capAtMaxRep at hcap
      by_cases hmr : m2 > maxRepUp
      · rw [if_pos hmr] at hcap
        by_cases hexp3 : e2 ≥ maxExponent
        · rw [if_pos hexp3] at hcap; exact absurd hcap (by intro h; cases h)
        · rw [if_neg hexp3] at hcap
          simp only [divu10] at hcap
          have he3_eq : e3 = e2 + 1 :=
            ((Prod.mk.inj (Prod.mk.inj (Except.ok.inj hcap)).2).1).symm
          omega
      · rw [if_neg hmr] at hcap
        have hm3_eq : m3 = m2 := ((Prod.mk.inj (Except.ok.inj hcap)).1).symm
        exfalso
        rw [hm3_eq] at hm3_lt
        rw [hminMant_v] at hm2_ge_min
        omega
    · -- m3 above the scaleDown/cap output ceilings → both stages were identity.
      intro hgt
      have hcap_id : m3 = m2 ∧ g3 = g2 := by
        unfold doNormalize_capAtMaxRep at hcap
        by_cases hmr : m2 > maxRepUp
        · exfalso
          rw [if_pos hmr] at hcap
          by_cases hexp3 : e2 ≥ maxExponent
          · rw [if_pos hexp3] at hcap; exact absurd hcap (by intro h; cases h)
          · rw [if_neg hexp3] at hcap
            simp only [divu10] at hcap
            have hm3_eq : m3 = m2 / 10 := ((Prod.mk.inj (Except.ok.inj hcap)).1).symm
            have hdiv : m3.toNat = m2.toNat / 10 := by
              rw [hm3_eq, UInt64.toNat_div, show (10 : UInt64).toNat = 10 from rfl]
            omega
        · rw [if_neg hmr] at hcap
          have h1 : m3 = m2 := ((Prod.mk.inj (Except.ok.inj hcap)).1).symm
          have h2 : g3 = g2 := (Prod.mk.inj (Prod.mk.inj (Except.ok.inj hcap)).2).2.symm
          exact ⟨h1, h2⟩
      obtain ⟨hm32, hg32⟩ := hcap_id
      have hsd_id : g2 = g0 := by
        by_cases hm1_gt : m1 > largeRange.max
        · exfalso
          have hb := doNormalize_scaleDown_fired_output_le largeRange.max m1 e1 g0
            m2 e2 g2 hm1_gt hsd
          have hm23 : m2.toNat = m3.toNat := by rw [hm32]
          omega
        · rw [doNormalize_scaleDown, dif_neg hm1_gt] at hsd
          exact ((Prod.mk.inj (Prod.mk.inj (Except.ok.inj hsd)).2).2).symm
      have hg3_g0 : g3 = g0 := by rw [hg32, hsd_id]
      rw [hg3_g0, hg0_def]
      by_cases hn : n.negative_
      · rw [if_pos hn]; decide
      · rw [if_neg hn]; decide

/-- A zero mantissa forces `toRat = 0` (any sign/exponent). -/
lemma Number.toRat_eq_zero_of_mantissa_zero (n : Number) (h : n.mantissa_ = 0) :
    n.toRat = 0 := by
  have habs := abs_toRat_eq n
  rw [show (n.mantissa_.toNat : ℚ) = 0 from by
        rw [show n.mantissa_.toNat = 0 from by rw [h]; rfl]; norm_num,
      zero_mul] at habs
  exact abs_eq_zero.mp habs

/-- A nonzero mantissa forces `toRat ≠ 0` (any sign/exponent). -/
lemma Number.toRat_ne_zero_of_mantissa_ne_zero (n : Number) (h : n.mantissa_ ≠ 0) :
    n.toRat ≠ 0 := by
  intro h0
  have habs := abs_toRat_eq n
  rw [h0, abs_zero] at habs
  have h10 : (0 : ℚ) < (10 : ℚ) ^ n.exponent_ := zpow_pos (by norm_num) _
  have hm_pos : (0 : ℚ) < (n.mantissa_.toNat : ℚ) := by
    have hp : 0 < n.mantissa_.toNat := by
      rcases Nat.eq_zero_or_pos n.mantissa_.toNat with hz | hp
      · exact absurd (UInt64.toNat_inj.mp (by rw [hz]; rfl)) h
      · exact hp
    exact_mod_cast hp
  exact absurd habs.symm (ne_of_gt (mul_pos hm_pos h10))

/-- The result of a successful `normalize` with nonzero mantissa is
normalized. -/
theorem normalize_result_isNormalized (n result : Number) (mode : rounding_mode)
    (hn_mant_ne : n.mantissa_ ≠ 0)
    (hok : n.normalize largeRange.min largeRange.max mode = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    result.isNormalized := by
  obtain ⟨m3, e3, g3, res, hrup, hresult_eq, hfloor, hle, _, _⟩ :=
    normalize_doRoundUp_stage n result mode hn_mant_ne hok hresult
  have hres_ne : res.mantissa_ ≠ 0 := by
    rw [hresult_eq] at hresult
    exact hresult
  obtain ⟨h1, h2, h3, h4⟩ :=
    doRoundUp_output_invariants_upTo_maxRepUp_anyMode g3 n.negative_ m3 e3 mode
      hfloor hle "Number::normalize 2" res hrup hres_ne
  have h_exp_le : res.exponent_ ≤ maxExponent :=
    doRoundUp_exponent_le_max g3 n.negative_ m3 e3 mode "Number::normalize 2" res hrup
  right
  rw [hresult_eq]
  refine ⟨UInt64.le_iff_toNat_le.mpr h1, UInt64.le_iff_toNat_le.mpr h2, ?_, h3, h_exp_le⟩
  by_cases hcase : res.mantissa_.toNat ≤ maxRep.toNat
  · left
    exact UInt64.le_iff_toNat_le.mpr hcase
  · right
    exact h4 (by omega)

/-- A successful `normalize` with nonzero result mantissa forces a nonzero
operand mantissa: `doNormalize` short-circuits a zero-mantissa input to
`Number.zero` (whose mantissa is `0`), contradicting `hresult`. -/
theorem normalize_operand_ne_zero (n result : Number) (mode : rounding_mode)
    (hok : n.normalize largeRange.min largeRange.max mode = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    n.mantissa_ ≠ 0 := by
  intro hn0
  unfold Number.normalize doNormalize at hok
  rw [if_pos (by rw [beq_iff_eq]; exact hn0)] at hok
  have : Number.zero = result := Except.ok.inj hok
  rw [← this] at hresult
  exact absurd rfl hresult

/-- At the top exponent the `normalize` result mantissa stays `≤ maxRepUp`. -/
theorem normalize_no_overflow_mantissa (n result : Number) (mode : rounding_mode)
    (hn_mant_ne : n.mantissa_ ≠ 0)
    (hok : n.normalize largeRange.min largeRange.max mode = .ok result)
    (hresult : result.mantissa_ ≠ 0)
    (h_exp_ge : result.exponent_ ≥ maxExponent) :
    result.mantissa_ ≤ maxRepUp := by
  obtain ⟨m3, e3, g3, res, hrup, hresult_eq, _, hle, hcap_exp, hempty⟩ :=
    normalize_doRoundUp_stage n result mode hn_mant_ne hok hresult
  rw [hresult_eq] at h_exp_ge ⊢
  exact doRoundUp_mantissa_le_maxRepUp_at_maxExp g3 n.negative_ m3 e3 mode
    "Number::normalize 2" hle hcap_exp hempty res hrup h_exp_ge

end XRPL.Model.Protocol
