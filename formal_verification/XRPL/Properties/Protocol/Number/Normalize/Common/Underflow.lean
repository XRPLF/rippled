import XRPL.Properties.Protocol.Number.Common.Notation
import Mathlib.Tactic

import XRPL.Properties.Protocol.Number.Normalize.Common.ToNearest.AlgorithmicFacts


namespace XRPL.Model.Protocol

/-! # Underflow characterization for `Number.normalize`

A zero result mantissa (with a nonzero input mantissa) means either the
explicit zero branch fired (`e2 < minExponent ∨ m2 < minMantissa` after the
scaling stages) or the final `doRoundUp` flushed. All paths pin the input
value strictly below the smallest positive representable
`10^18 · 10^minExponent`. The hypothesis-minimal discrete-rounding theorems
consume this in their underflow branches. -/

/-- A fired `doNormalize_scaleDown` exits at or above `(maxMantissa+1)/10`:
with `maxMantissa = largeRange.max = 10^19 − 1` the output is `≥ 10^18`. -/
lemma doNormalize_scaleDown_fired_output_ge
    (m : UInt64) (e : Int) (g : Guard) (m' : UInt64) (e' : Int) (g' : Guard)
    (hgt : m > largeRange.max)
    (h : doNormalize_scaleDown largeRange.max m e g = .ok (m', e', g')) :
    1000000000000000000 ≤ m'.toNat := by
  induction m, e, g using doNormalize_scaleDown.induct (maxMantissa := largeRange.max)
    generalizing m' e' g' with
  | case1 m e g hgt' hmax =>
    rw [doNormalize_scaleDown, dif_pos hgt', if_pos hmax] at h
    exact absurd h (by intro hh; cases hh)
  | case2 m e g hgt' hmax IH =>
    rw [doNormalize_scaleDown, dif_pos hgt', if_neg hmax] at h
    by_cases hgt2 : m / 10 > largeRange.max
    · exact IH m' e' g' hgt2 h
    · rw [doNormalize_scaleDown, dif_neg hgt2] at h
      have hm'_eq : m / 10 = m' := (Prod.mk.inj (Except.ok.inj h)).1
      have hgt_nat : largeRange.max.toNat < m.toNat := UInt64.lt_iff_toNat_lt.mp hgt'
      rw [largeRange_max_val] at hgt_nat
      rw [← hm'_eq, UInt64.toNat_div, show (10 : UInt64).toNat = 10 from rfl]
      omega
  | case3 m e g hgt' =>
    exact absurd hgt hgt'

/-- If `doNormalize_scaleUp` exits below the minimum mantissa, it exhausted the
exponent range: the exit exponent is `≤ minExponent`. -/
lemma doNormalize_scaleUp_exit_exp (minMant m : UInt64) (e : Int) :
    (doNormalize_scaleUp minMant m e).1 < minMant →
    (doNormalize_scaleUp minMant m e).2 ≤ minExponent := by
  induction m, e using doNormalize_scaleUp.induct (minMantissa := minMant) with
  | case1 m e hcond IH =>
    rw [doNormalize_scaleUp, if_pos hcond]
    exact IH
  | case2 m e hcond =>
    rw [doNormalize_scaleUp, if_neg hcond]
    intro hlt
    by_contra hgt
    push_neg at hgt
    exact hcond ⟨hlt, hgt⟩

set_option maxHeartbeats 800000 in
-- unfolds the full normalize pipeline (scaleUp/scaleDown/doRoundUp) zero paths
/-- A zero result mantissa from `Number.normalize` (nonzero input mantissa)
forces the input value strictly below the smallest positive representable. -/
theorem normalize_underflow_truth_small (n result : Number) (mode : rounding_mode)
    (hn_mant_ne : n.mantissa_ ≠ 0)
    (hok : n.normalize largeRange.min largeRange.max mode = .ok result)
    (hres0 : result.mantissa_ = 0) :
    |n.toRat| < (10 : ℚ) ^ (18 : ℕ) * (10 : ℚ) ^ (minExponent : ℤ) := by
  have habs_n : |n.toRat| = (n.mantissa_.toNat : ℚ) * 10 ^ n.exponent_ := abs_toRat_eq n
  unfold Number.normalize doNormalize at hok
  rw [beq_eq_false_iff_ne.mpr hn_mant_ne] at hok
  simp only [Bool.false_eq_true, if_false] at hok
  set su := doNormalize_scaleUp largeRange.min n.mantissa_ n.exponent_ with hsu_def
  set m1 : UInt64 := su.1 with hm1_def
  set e1 : Int := su.2 with he1_def
  set g0 : Guard := if n.negative_ then Guard.new.set_negative else Guard.new with hg0_def
  have hg0_rep : represents g0 0 := g0_represents_zero n.negative_
  obtain ⟨m2, e2, g2, hsd⟩ : ∃ m2 e2 g2,
      doNormalize_scaleDown largeRange.max m1 e1 g0 = .ok (m2, e2, g2) := by
    match hsd : doNormalize_scaleDown largeRange.max m1 e1 g0 with
    | .error e => rw [hsd] at hok; exact absurd hok (by intro h; cases h)
    | .ok (m2, e2, g2) => exact ⟨m2, e2, g2, rfl⟩
  rw [hsd] at hok
  simp only at hok
  have hsu_val := doNormalize_scaleUp_value largeRange.min n.mantissa_ n.exponent_
    (by rw [largeRange_min_val]; norm_num)
  have hsu_val' : (m1.toNat : ℚ) * 10 ^ e1 = (n.mantissa_.toNat : ℚ) * 10 ^ n.exponent_ := by
    have h1 : (doNormalize_scaleUp largeRange.min n.mantissa_ n.exponent_).1 = m1 := rfl
    have h2 : (doNormalize_scaleUp largeRange.min n.mantissa_ n.exponent_).2 = e1 := rfl
    simpa [h1, h2] using hsu_val
  obtain ⟨k2, f2, he2_eq, hm2_le_maxMant, hval2, hrep2⟩ :=
    doNormalize_scaleDown_correct largeRange.max m1 e1 g0 0 hg0_rep m2 e2 g2 hsd
  have hf2_nn : 0 ≤ f2 := represents_nonneg hrep2
  have hf2_lt : f2 < 1 := represents_lt_one hrep2
  have hval_n2 : |n.toRat| = ((m2.toNat : ℚ) + f2) * 10 ^ e2 := by
    rw [habs_n, ← hsu_val']
    calc (m1.toNat : ℚ) * 10 ^ e1
        = ((m1.toNat : ℚ) + 0) * 10 ^ e1 := by ring
      _ = ((m2.toNat : ℚ) + f2) * 10 ^ e2 := hval2
  have h10e2_pos : (0 : ℚ) < (10 : ℚ) ^ e2 := zpow_pos (by norm_num) _
  have h18_q : (10 : ℚ) ^ (18 : ℕ) = 1000000000000000000 := by norm_num
  by_cases hzero : e2 < minExponent ∨ m2 < largeRange.min
  · -- Explicit zero branch: bound the captured value directly.
    rcases hzero with he2 | hm2
    · -- e2 ≤ minExponent − 1 with m2 ≤ 10^19 − 1.
      have hm2_le : m2.toNat ≤ 9999999999999999999 := by
        have h := hm2_le_maxMant
        rw [largeRange_max_val] at h
        exact h
      have h19_q : (10 : ℚ) ^ (19 : ℕ) = 10000000000000000000 := by norm_num
      have h1 : |n.toRat| < ((m2.toNat : ℚ) + 1) * 10 ^ e2 := by
        rw [hval_n2]
        exact mul_lt_mul_of_pos_right (by linarith) h10e2_pos
      have h2 : ((m2.toNat : ℚ) + 1) * 10 ^ e2 ≤ (10 : ℚ) ^ (19 : ℕ) * 10 ^ e2 := by
        apply mul_le_mul_of_nonneg_right _ (le_of_lt h10e2_pos)
        rw [h19_q]
        have : (m2.toNat : ℚ) ≤ 9999999999999999999 := by exact_mod_cast hm2_le
        linarith
      have h3 : (10 : ℚ) ^ (19 : ℕ) * (10 : ℚ) ^ e2
          ≤ (10 : ℚ) ^ (19 : ℕ) * (10 : ℚ) ^ ((minExponent : ℤ) - 1) := by
        apply mul_le_mul_of_nonneg_left _ (by norm_num)
        apply zpow_le_zpow_right₀ (by norm_num)
        omega
      have h4 : (10 : ℚ) ^ (19 : ℕ) * (10 : ℚ) ^ ((minExponent : ℤ) - 1)
          = (10 : ℚ) ^ (18 : ℕ) * (10 : ℚ) ^ (minExponent : ℤ) := by
        have h19 : (10 : ℚ) ^ (19 : ℕ) = (10 : ℚ) ^ (18 : ℕ) * 10 := by norm_num
        have hsplit : (10 : ℚ) ^ ((minExponent : ℤ) - 1) * 10
            = (10 : ℚ) ^ (minExponent : ℤ) := by
          rw [← zpow_add_one₀ (by norm_num : (10 : ℚ) ≠ 0)]
          norm_num
        rw [h19]
        calc (10 : ℚ) ^ (18 : ℕ) * 10 * (10 : ℚ) ^ ((minExponent : ℤ) - 1)
            = (10 : ℚ) ^ (18 : ℕ) * ((10 : ℚ) ^ ((minExponent : ℤ) - 1) * 10) := by ring
          _ = (10 : ℚ) ^ (18 : ℕ) * (10 : ℚ) ^ (minExponent : ℤ) := by rw [hsplit]
      calc |n.toRat| < ((m2.toNat : ℚ) + 1) * 10 ^ e2 := h1
        _ ≤ (10 : ℚ) ^ (19 : ℕ) * 10 ^ e2 := h2
        _ ≤ (10 : ℚ) ^ (19 : ℕ) * (10 : ℚ) ^ ((minExponent : ℤ) - 1) := h3
        _ = (10 : ℚ) ^ (18 : ℕ) * (10 : ℚ) ^ (minExponent : ℤ) := h4
    · -- m2 < 10^18: scaleDown was the identity, so e2 = e1 ≤ minExponent.
      have hm2_lt : m2.toNat < 1000000000000000000 := by
        have h := UInt64.lt_iff_toNat_lt.mp hm2
        rw [largeRange_min_val] at h
        exact h
      have he2_le : e2 ≤ minExponent := by
        by_cases hm1_gt : m1 > largeRange.max
        · exfalso
          have hge := doNormalize_scaleDown_fired_output_ge m1 e1 g0 m2 e2 g2 hm1_gt hsd
          omega
        · have hm1_le : m1 ≤ largeRange.max := by
            rw [UInt64.le_iff_toNat_le]
            by_contra hc
            push_neg at hc
            exact hm1_gt (UInt64.lt_iff_toNat_lt.mpr hc)
          have hid := doNormalize_scaleDown_id largeRange.max m1 e1 g0 hm1_le
          rw [hid] at hsd
          have hpair := Except.ok.inj hsd
          have hm21 : m2 = m1 := ((Prod.mk.inj hpair).1).symm
          have he21 : e2 = e1 := ((Prod.mk.inj (Prod.mk.inj hpair).2).1).symm
          have hm1_lt : m1 < largeRange.min := by rw [hm21] at hm2; exact hm2
          have he1_le : e1 ≤ minExponent :=
            doNormalize_scaleUp_exit_exp largeRange.min n.mantissa_ n.exponent_ hm1_lt
          rw [he21]
          exact he1_le
      have h1 : |n.toRat| < ((m2.toNat : ℚ) + 1) * 10 ^ e2 := by
        rw [hval_n2]
        exact mul_lt_mul_of_pos_right (by linarith) h10e2_pos
      have h2 : ((m2.toNat : ℚ) + 1) * 10 ^ e2 ≤ (10 : ℚ) ^ (18 : ℕ) * 10 ^ e2 := by
        apply mul_le_mul_of_nonneg_right _ (le_of_lt h10e2_pos)
        rw [h18_q]
        have : (m2.toNat : ℚ) + 1 ≤ 1000000000000000000 := by
          have h : m2.toNat + 1 ≤ 1000000000000000000 := by omega
          exact_mod_cast h
        linarith
      have h3 : (10 : ℚ) ^ (18 : ℕ) * (10 : ℚ) ^ e2
          ≤ (10 : ℚ) ^ (18 : ℕ) * (10 : ℚ) ^ (minExponent : ℤ) := by
        apply mul_le_mul_of_nonneg_left _ (by norm_num)
        exact zpow_le_zpow_right₀ (by norm_num) he2_le
      -- The mantissa bound is strict only when m2 + 1 < 10^18 or f2 < 1 bites;
      -- combine the strict first step with the two ≤ steps.
      calc |n.toRat| < ((m2.toNat : ℚ) + 1) * 10 ^ e2 := h1
        _ ≤ (10 : ℚ) ^ (18 : ℕ) * 10 ^ e2 := h2
        _ ≤ (10 : ℚ) ^ (18 : ℕ) * (10 : ℚ) ^ (minExponent : ℤ) := h3
  · -- Round-stage flush: `doRoundUp_flush_value_small` pins the frame.
    push_neg at hzero
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
    have hres_mant0 : res.mantissa_ = 0 := by
      have h : result.mantissa_ = res.mantissa_ := by rw [hresult_eq]; rfl
      rw [← h]
      exact hres0
    obtain ⟨f3, hm3_le_maxRep, hval3, hrep3, _⟩ :=
      doNormalize_capAtMaxRep_correct m2 e2 g2 f2 hrep2 m3 e3 g3 hcap
    have hf3_nn : 0 ≤ f3 := represents_nonneg hrep3
    have hf3_lt : f3 < 1 := represents_lt_one hrep3
    have hval_n3 : |n.toRat| = ((m3.toNat : ℚ) + f3) * 10 ^ e3 := by
      rw [hval_n2]
      exact hval3
    have hm2_ge_min : largeRange.min.toNat ≤ m2.toNat := by
      rw [UInt64.lt_iff_toNat_lt] at hm2_ge
      omega
    have hminMant_v : largeRange.min.toNat = 1000000000000000000 := largeRange_min_val
    have hm3_ge_floor : mantissaFloor ≤ m3.toNat := by
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
    have hflush := doRoundUp_flush_value_small g3 n.negative_ m3 e3 mode
      hm3_ge_floor hm3_le_maxRep "Number::normalize 2" res hrup hres_mant0
    have h10e3_pos : (0 : ℚ) < (10 : ℚ) ^ e3 := zpow_pos (by norm_num) _
    calc |n.toRat| = ((m3.toNat : ℚ) + f3) * 10 ^ e3 := hval_n3
      _ < ((m3.toNat : ℚ) + 1) * 10 ^ e3 :=
          mul_lt_mul_of_pos_right (by linarith only [hf3_lt]) h10e3_pos
      _ ≤ (10 : ℚ) ^ (18 : ℕ) * (10 : ℚ) ^ (minExponent : ℤ) := hflush

end XRPL.Model.Protocol
