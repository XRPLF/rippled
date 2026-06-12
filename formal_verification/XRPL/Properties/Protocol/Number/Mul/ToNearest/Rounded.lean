import XRPL.Properties.Protocol.Number.Common.Notation
-- Discrete-grid rounding identification for `operator_mul` under `.to_nearest`.
import XRPL.Properties.Protocol.Number.Mul.ToNearest.AlgorithmicFacts
import XRPL.Properties.Protocol.Number.Mul.ToNearest.BoundProof

set_option linter.style.longLine false
set_option linter.style.emptyLine false

namespace XRPL.Model.Protocol

/-! ## No-inbetween dischargers for `operator_mul_rounded` -/

/-- No normalized Number sits strictly between `result.toRat` and `truth` when
`result.toRat ≤ truth`. -/
theorem operator_mul_no_inbetween_below (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (hok : Number.operator_mul x y .to_nearest = .ok result)
    (hresult : result.mantissa_ ≠ 0)
    (h_le : result.toRat ≤ x.toRat * y.toRat) :
    ∀ m : Number, m.isNormalized →
      result.toRat < m.toRat → ¬ (m.toRat ≤ x.toRat * y.toRat) := by
  set truth : ℚ := x.toRat * y.toRat with htruth_def
  obtain ⟨zm, ze', f, g, res_pos, hzm_ge, hzm_le, hf_nn, hf_lt, hfloor_constr, h_truth_abs,
          h_rup_pos, h_result_abs, hres_pos_mant_ne, hf_rep, h_sign⟩ :=
    operator_mul_algorithmic_facts x y result hx hy hx_mant_ne hy_mant_ne hok hresult
  simp only [← htruth_def] at h_truth_abs
  have h10ze_pos : (0 : ℚ) < (10 : ℚ) ^ ze' := zpow_pos (by norm_num) _
  have h_zm_pos_q : (0 : ℚ) < (zm.toNat : ℚ) := by
    have : (0 : ℕ) < zm.toNat := by omega
    exact_mod_cast this
  have h_zm_lt : zm.toNat < (10 : ℕ) ^ 19 := by
    have h_maxR : maxRep.toNat = maxRepNat := by decide
    have h_pow19 : (10 : ℕ) ^ 19 = tenPow19 := by decide
    omega
  have h_zm_f_pos : (0 : ℚ) < (zm.toNat : ℚ) + f := by linarith
  have h_abs_truth_pos : (0 : ℚ) < |truth| := by
    rw [h_truth_abs]; exact mul_pos h_zm_f_pos h10ze_pos
  have h_truth_ne : truth ≠ 0 := by
    intro h; rw [h] at h_abs_truth_pos; simp at h_abs_truth_pos
  intro m h_norm h_lt_m h_m_le_truth
  rcases lt_or_ge truth 0 with h_truth_neg | h_truth_nn
  · have h_result_neg : result.toRat < 0 := lt_of_le_of_lt h_le h_truth_neg
    have h_m_neg : m.toRat < 0 := lt_of_le_of_lt h_m_le_truth h_truth_neg
    have h_truth_abs_eq : |truth| = -truth := abs_of_neg h_truth_neg
    have h_result_abs_eq : |result.toRat| = -result.toRat := abs_of_neg h_result_neg
    rw [h_truth_abs_eq] at h_truth_abs
    rw [h_result_abs_eq] at h_result_abs
    by_cases h_ru : g.shouldRoundUp zm
    · have h_f_ge_half : (1 : ℚ) / 2 ≤ f := by
        rcases h_ru with h_round_1 | ⟨h_round_0, _⟩
        · have h_gt : (1 : ℚ) / 2 < f := (round_correct hf_rep).1.mp h_round_1
          linarith
        · have h_eq : f = (1 : ℚ) / 2 := (round_correct hf_rep).2.2.mp h_round_0
          linarith
      by_cases h_cusp : zm.toNat + 1 ≤ maxRep.toNat
      · have h_val := doRoundUp_value_roundUp_noCusp g zm ze' h_ru h_cusp "Number::multiplication overflow" res_pos h_rup_pos hres_pos_mant_ne
        simp only at h_val
        rw [h_val] at h_result_abs
        have h_neg_m_lo : -(((zm.toNat : ℚ) + 1) * 10 ^ ze') < m.toRat := by
          have : -result.toRat = ((zm.toNat : ℚ) + 1) * 10 ^ ze' := h_result_abs
          have h_result_eq : result.toRat = -(((zm.toNat : ℚ) + 1) * 10 ^ ze') := by linarith
          rw [h_result_eq] at h_lt_m
          exact h_lt_m
        have h_neg_m_hi : m.toRat < -((zm.toNat : ℚ) * 10 ^ ze') := by
          calc m.toRat ≤ truth := h_m_le_truth
            _ = -(((zm.toNat : ℚ) + f) * 10 ^ ze') := by linarith [h_truth_abs]
            _ < -((zm.toNat : ℚ) * 10 ^ ze') := by
                have h_compare : (zm.toNat : ℚ) < (zm.toNat : ℚ) + f := by linarith
                have h_compare_mul : (zm.toNat : ℚ) * 10 ^ ze' < ((zm.toNat : ℚ) + f) * 10 ^ ze' :=
                  mul_lt_mul_of_pos_right h_compare h10ze_pos
                linarith
        by_cases h_zm_floor : zm.toNat = mantissaFloor
        · have h_f_ge : (8 : ℚ) / 10 ≤ f := hfloor_constr h_zm_floor
          have h_m_le_strong : m.toRat ≤ -(((zm.toNat : ℚ) + 8/10) * 10 ^ ze') := by
            calc m.toRat ≤ truth := h_m_le_truth
              _ = -(((zm.toNat : ℚ) + f) * 10 ^ ze') := by linarith [h_truth_abs]
              _ ≤ -(((zm.toNat : ℚ) + 8/10) * 10 ^ ze') := by
                  have : (zm.toNat : ℚ) + 8/10 ≤ (zm.toNat : ℚ) + f := by linarith
                  have h_mul : ((zm.toNat : ℚ) + 8/10) * 10 ^ ze' ≤ ((zm.toNat : ℚ) + f) * 10 ^ ze' :=
                    mul_le_mul_of_nonneg_right this (le_of_lt h10ze_pos)
                  linarith
          exact no_normalized_in_ulp_gap_at_floor_neg ze' zm.toNat h_zm_floor h_zm_lt
            m h_norm h_m_neg h_neg_m_lo h_m_le_strong
        · have h_zm_ge_strict : mantissaFloorSucc ≤ zm.toNat := by omega
          exact no_normalized_in_open_ulp_gap_neg_zm ze' zm.toNat
            h_zm_ge_strict h_zm_lt m h_norm h_m_neg h_neg_m_lo h_neg_m_hi
      · push_neg at h_cusp
        have h_zm_eq : zm = maxRep := by
          have h_eq_nat : zm.toNat = maxRep.toNat := by omega
          exact UInt64.toNat.inj h_eq_nat
        have h_val := doRoundUp_value_roundUp_cusp g zm ze' h_zm_eq h_ru "Number::multiplication overflow" res_pos h_rup_pos hres_pos_mant_ne
        simp only at h_val
        rw [h_val] at h_result_abs
        have h_neg_m_lo : -((maxRepCuspTarget : ℚ) * 10 ^ ze') < m.toRat := by
          have h_result_eq : result.toRat = -((maxRepCuspTarget : ℚ) * 10 ^ ze') := by
            linarith [h_result_abs]
          rw [h_result_eq] at h_lt_m; exact h_lt_m
        have h_neg_m_hi : m.toRat < -((maxRep.toNat : ℚ) * 10 ^ ze') := by
          have h_zm_eq_q : (zm.toNat : ℚ) = (maxRep.toNat : ℚ) := by rw [h_zm_eq]
          calc m.toRat ≤ truth := h_m_le_truth
            _ = -(((zm.toNat : ℚ) + f) * 10 ^ ze') := by linarith [h_truth_abs]
            _ < -((maxRep.toNat : ℚ) * 10 ^ ze') := by
                rw [h_zm_eq_q]
                have h_compare : (maxRep.toNat : ℚ) < (maxRep.toNat : ℚ) + f := by linarith
                have h_compare_mul : (maxRep.toNat : ℚ) * 10 ^ ze' < ((maxRep.toNat : ℚ) + f) * 10 ^ ze' :=
                  mul_lt_mul_of_pos_right h_compare h10ze_pos
                linarith
        exact no_normalized_in_cusp_gap_neg ze' m h_norm h_m_neg h_neg_m_lo h_neg_m_hi
    · -- Empty interval: (zm+f)*10^ze' ≤ -m < zm*10^ze' is impossible since f ≥ 0.
      exfalso
      have h_val := doRoundUp_value_no_roundUp g zm ze' h_ru "Number::multiplication overflow" res_pos h_rup_pos hres_pos_mant_ne
      simp only at h_val
      rw [h_val] at h_result_abs
      have h_neg_m_ge : ((zm.toNat : ℚ) + f) * 10 ^ ze' ≤ -m.toRat := by
        have := h_m_le_truth
        linarith [h_truth_abs]
      have h_neg_m_lt : -m.toRat < (zm.toNat : ℚ) * 10 ^ ze' := by
        have := h_lt_m
        linarith [h_result_abs]
      have h_chain : ((zm.toNat : ℚ) + f) * 10 ^ ze' < (zm.toNat : ℚ) * 10 ^ ze' := by linarith
      have h_compare : (zm.toNat : ℚ) ≤ (zm.toNat : ℚ) + f := by linarith
      have h_compare_mul : (zm.toNat : ℚ) * 10 ^ ze' ≤ ((zm.toNat : ℚ) + f) * 10 ^ ze' :=
        mul_le_mul_of_nonneg_right h_compare (le_of_lt h10ze_pos)
      linarith
  · have h_truth_pos : 0 < truth := lt_of_le_of_ne h_truth_nn (Ne.symm h_truth_ne)
    have h_truth_abs_eq : |truth| = truth := abs_of_nonneg h_truth_nn
    rw [h_truth_abs_eq] at h_truth_abs
    have h_sign_xy := toRat_mul_sign x y
    have h_xy_same_sign : x.negative_ = y.negative_ := by
      by_contra h_diff
      have h_xy_neg : x.toRat * y.toRat ≤ 0 := by
        rcases hx : x.negative_ with _ | _
        · rcases hy : y.negative_ with _ | _
          · exfalso; apply h_diff; rw [hx, hy]
          · have hx_nn : (0 : ℚ) ≤ x.toRat := Number.toRat_nonneg_of_nonnegative x hx
            have hy_np : y.toRat ≤ 0 := Number.toRat_nonpos_of_negative y hy
            exact mul_nonpos_of_nonneg_of_nonpos hx_nn hy_np
        · rcases hy : y.negative_ with _ | _
          · have hx_np : x.toRat ≤ 0 := Number.toRat_nonpos_of_negative x hx
            have hy_nn : (0 : ℚ) ≤ y.toRat := Number.toRat_nonneg_of_nonnegative y hy
            exact mul_nonpos_of_nonpos_of_nonneg hx_np hy_nn
          · exfalso; apply h_diff; rw [hx, hy]
      change False; linarith
    have h_zn_false : (x.negative_ != y.negative_) = false := by
      rw [h_xy_same_sign]; simp [bne]
    have h_result_neg_false : result.negative_ = false := by rw [h_sign, h_zn_false]
    have h_result_nn : (0 : ℚ) ≤ result.toRat :=
      Number.toRat_nonneg_of_nonnegative result h_result_neg_false
    have h_result_abs_eq : |result.toRat| = result.toRat := abs_of_nonneg h_result_nn
    rw [h_result_abs_eq] at h_result_abs
    by_cases h_ru : g.shouldRoundUp zm
    · -- shouldRoundUp contradicts result ≤ truth: result ≥ (zm+1)*10^ze' > truth.
      exfalso
      have h_result_ge : ((zm.toNat : ℚ) + 1) * 10 ^ ze' ≤ result.toRat := by
        by_cases h_cusp : zm.toNat + 1 ≤ maxRep.toNat
        · have h_val := doRoundUp_value_roundUp_noCusp g zm ze' h_ru h_cusp "Number::multiplication overflow" res_pos h_rup_pos hres_pos_mant_ne
          simp only at h_val
          rw [h_val] at h_result_abs; rw [← h_result_abs]
        · push_neg at h_cusp
          have h_zm_eq : zm = maxRep := UInt64.toNat.inj (by omega)
          have h_val := doRoundUp_value_roundUp_cusp g zm ze' h_zm_eq h_ru "Number::multiplication overflow" res_pos h_rup_pos hres_pos_mant_ne
          simp only at h_val
          rw [h_val] at h_result_abs
          have h_maxR_v : maxRep.toNat = maxRepNat := by decide
          have h_le_810 : (zm.toNat : ℚ) + 1 ≤ maxRepCuspTarget := by
            rw [show zm.toNat = maxRep.toNat from by rw [h_zm_eq], h_maxR_v]; norm_num
          calc ((zm.toNat : ℚ) + 1) * 10 ^ ze'
              ≤ maxRepCuspTarget * 10 ^ ze' :=
                mul_le_mul_of_nonneg_right h_le_810 (le_of_lt h10ze_pos)
            _ = result.toRat := h_result_abs.symm
      have h_compare : (zm.toNat : ℚ) + f < (zm.toNat : ℚ) + 1 := by linarith
      have h_compare_mul : ((zm.toNat : ℚ) + f) * 10 ^ ze' < ((zm.toNat : ℚ) + 1) * 10 ^ ze' :=
        mul_lt_mul_of_pos_right h_compare h10ze_pos
      linarith
    · have h_val := doRoundUp_value_no_roundUp g zm ze' h_ru "Number::multiplication overflow" res_pos h_rup_pos hres_pos_mant_ne
      simp only at h_val
      rw [h_val] at h_result_abs
      have h_m_pos : 0 < m.toRat := lt_of_le_of_lt h_result_nn h_lt_m
      have h_m_lo : (zm.toNat : ℚ) * 10 ^ ze' < m.toRat := by
        rw [← h_result_abs]; exact h_lt_m
      have h_m_hi : m.toRat < ((zm.toNat : ℚ) + 1) * 10 ^ ze' := by
        calc m.toRat ≤ truth := h_m_le_truth
          _ = ((zm.toNat : ℚ) + f) * 10 ^ ze' := h_truth_abs
          _ < ((zm.toNat : ℚ) + 1) * 10 ^ ze' := by
              apply mul_lt_mul_of_pos_right _ h10ze_pos; linarith
      by_cases h_zm_floor : zm.toNat = mantissaFloor
      · -- At floor, f ≥ 8/10 forces g.round = 1, contradicting shouldRoundUp = false.
        exfalso
        have h_f_ge : (8 : ℚ) / 10 ≤ f := hfloor_constr h_zm_floor
        have h_f_gt_half : (1 : ℚ) / 2 < f := by linarith
        have h_round_eq_1 : g.round .to_nearest = 1 := (round_correct hf_rep).1.mpr h_f_gt_half
        apply h_ru; left; exact h_round_eq_1
      · have h_zm_ge_strict : mantissaFloorSucc ≤ zm.toNat := by omega
        exact no_normalized_in_open_ulp_gap_pos_zm ze' zm.toNat
          h_zm_ge_strict h_zm_lt m h_norm h_m_pos h_m_lo h_m_hi

/-- Helper: when `truth ≤ result.toRat`, no normalized Number sits strictly between
`truth` and `result.toRat`. -/
theorem operator_mul_no_inbetween_above (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (hok : Number.operator_mul x y .to_nearest = .ok result)
    (hresult : result.mantissa_ ≠ 0)
    (h_ge : x.toRat * y.toRat ≤ result.toRat) :
    ∀ m : Number, m.isNormalized →
      m.toRat < result.toRat → ¬ (x.toRat * y.toRat ≤ m.toRat) := by
  set truth : ℚ := x.toRat * y.toRat with htruth_def
  obtain ⟨zm, ze', f, g, res_pos, hzm_ge, hzm_le, hf_nn, hf_lt, hfloor_constr, h_truth_abs,
          h_rup_pos, h_result_abs, hres_pos_mant_ne, hf_rep, h_sign⟩ :=
    operator_mul_algorithmic_facts x y result hx hy hx_mant_ne hy_mant_ne hok hresult
  simp only [← htruth_def] at h_truth_abs
  have h10ze_pos : (0 : ℚ) < (10 : ℚ) ^ ze' := zpow_pos (by norm_num) _
  have h_zm_pos_q : (0 : ℚ) < (zm.toNat : ℚ) := by
    have : (0 : ℕ) < zm.toNat := by omega
    exact_mod_cast this
  have h_zm_lt : zm.toNat < (10 : ℕ) ^ 19 := by
    have h_maxR : maxRep.toNat = maxRepNat := by decide
    have h_pow19 : (10 : ℕ) ^ 19 = tenPow19 := by decide
    omega
  have h_zm_f_pos : (0 : ℚ) < (zm.toNat : ℚ) + f := by linarith
  have h_abs_truth_pos : (0 : ℚ) < |truth| := by
    rw [h_truth_abs]; exact mul_pos h_zm_f_pos h10ze_pos
  have h_truth_ne : truth ≠ 0 := by
    intro h; rw [h] at h_abs_truth_pos; simp at h_abs_truth_pos
  intro m h_norm h_m_lt h_truth_le_m
  rcases lt_or_ge truth 0 with h_truth_neg | h_truth_nn
  · have h_sign_xy := toRat_mul_sign x y
    have h_xy_diff_sign : x.negative_ ≠ y.negative_ := by
      intro h_same
      have h_xy_nn : 0 ≤ x.toRat * y.toRat := by
        rcases hx : x.negative_ with _ | _
        · have hy_same : y.negative_ = false := by rw [← h_same, hx]
          have hx_nn : (0 : ℚ) ≤ x.toRat := Number.toRat_nonneg_of_nonnegative x hx
          have hy_nn : (0 : ℚ) ≤ y.toRat := Number.toRat_nonneg_of_nonnegative y hy_same
          exact mul_nonneg hx_nn hy_nn
        · have hy_same : y.negative_ = true := by rw [← h_same, hx]
          have hx_np : x.toRat ≤ 0 := Number.toRat_nonpos_of_negative x hx
          have hy_np : y.toRat ≤ 0 := Number.toRat_nonpos_of_negative y hy_same
          exact mul_nonneg_of_nonpos_of_nonpos hx_np hy_np
      linarith
    have h_zn_true : (x.negative_ != y.negative_) = true := by
      rcases hx : x.negative_ with _ | _ <;> rcases hy : y.negative_ with _ | _
      all_goals first
        | rfl
        | (exfalso; apply h_xy_diff_sign; rw [hx, hy])
    have h_result_neg_true : result.negative_ = true := by rw [h_sign, h_zn_true]
    have h_result_np : result.toRat ≤ 0 :=
      Number.toRat_nonpos_of_negative result h_result_neg_true
    have h_m_np : m.toRat ≤ 0 := le_of_lt (lt_of_lt_of_le h_m_lt (by linarith : result.toRat ≤ 0))
    have h_truth_abs_eq : |truth| = -truth := abs_of_neg h_truth_neg
    have h_result_abs_eq : |result.toRat| = -result.toRat := by
      apply abs_of_nonpos h_result_np
    rw [h_truth_abs_eq] at h_truth_abs
    rw [h_result_abs_eq] at h_result_abs
    by_cases h_ru : g.shouldRoundUp zm
    · -- shouldRoundUp contradicts truth ≤ result: |result| ≥ (zm+1)*10^ze' > |truth|.
      exfalso
      have h_result_ge : ((zm.toNat : ℚ) + 1) * 10 ^ ze' ≤ |result.toRat| := by
        by_cases h_cusp : zm.toNat + 1 ≤ maxRep.toNat
        · have h_val := doRoundUp_value_roundUp_noCusp g zm ze' h_ru h_cusp "Number::multiplication overflow" res_pos h_rup_pos hres_pos_mant_ne
          simp only at h_val
          rw [h_result_abs_eq, h_result_abs, h_val]
        · push_neg at h_cusp
          have h_zm_eq : zm = maxRep := UInt64.toNat.inj (by omega)
          have h_val := doRoundUp_value_roundUp_cusp g zm ze' h_zm_eq h_ru "Number::multiplication overflow" res_pos h_rup_pos hres_pos_mant_ne
          simp only at h_val
          rw [h_result_abs_eq, h_result_abs, h_val]
          have h_maxR_v : maxRep.toNat = maxRepNat := by decide
          have h_le_810 : (zm.toNat : ℚ) + 1 ≤ maxRepCuspTarget := by
            rw [show zm.toNat = maxRep.toNat from by rw [h_zm_eq], h_maxR_v]; norm_num
          exact mul_le_mul_of_nonneg_right h_le_810 (le_of_lt h10ze_pos)
      have h_compare : (zm.toNat : ℚ) + f < (zm.toNat : ℚ) + 1 := by linarith
      have h_compare_mul : ((zm.toNat : ℚ) + f) * 10 ^ ze' < ((zm.toNat : ℚ) + 1) * 10 ^ ze' :=
        mul_lt_mul_of_pos_right h_compare h10ze_pos
      have h_abs_compare : |truth| < |result.toRat| := by
        rw [h_truth_abs_eq, h_truth_abs]; linarith
      have h_neg_compare : -truth < -result.toRat := by
        rw [← h_truth_abs_eq, ← h_result_abs_eq]; exact h_abs_compare
      linarith
    · have h_val := doRoundUp_value_no_roundUp g zm ze' h_ru "Number::multiplication overflow" res_pos h_rup_pos hres_pos_mant_ne
      simp only at h_val
      rw [h_val] at h_result_abs
      have h_neg_m_lo : -(((zm.toNat : ℚ) + 1) * 10 ^ ze') < m.toRat := by
        have h_compare : (zm.toNat : ℚ) + f < (zm.toNat : ℚ) + 1 := by linarith
        have h_compare_mul : ((zm.toNat : ℚ) + f) * 10 ^ ze' < ((zm.toNat : ℚ) + 1) * 10 ^ ze' :=
          mul_lt_mul_of_pos_right h_compare h10ze_pos
        have h_truth_gt : -(((zm.toNat : ℚ) + 1) * 10 ^ ze') < truth := by
          have : -truth = ((zm.toNat : ℚ) + f) * 10 ^ ze' := h_truth_abs
          linarith
        linarith
      have h_neg_m_hi : m.toRat < -((zm.toNat : ℚ) * 10 ^ ze') := by
        have : -result.toRat = (zm.toNat : ℚ) * 10 ^ ze' := h_result_abs
        have h_result_eq : result.toRat = -((zm.toNat : ℚ) * 10 ^ ze') := by linarith
        rw [h_result_eq] at h_m_lt; exact h_m_lt
      have h_m_neg_strict : m.toRat < 0 := by
        calc m.toRat < -((zm.toNat : ℚ) * 10 ^ ze') := h_neg_m_hi
          _ < 0 := by
              have : (0 : ℚ) < (zm.toNat : ℚ) * 10 ^ ze' := mul_pos h_zm_pos_q h10ze_pos
              linarith
      by_cases h_zm_floor : zm.toNat = mantissaFloor
      · -- At floor, f ≥ 8/10 forces g.round = 1, contradicting shouldRoundUp = false.
        exfalso
        have h_f_ge : (8 : ℚ) / 10 ≤ f := hfloor_constr h_zm_floor
        have h_f_gt_half : (1 : ℚ) / 2 < f := by linarith
        have h_round_eq_1 : g.round .to_nearest = 1 := (round_correct hf_rep).1.mpr h_f_gt_half
        apply h_ru; left; exact h_round_eq_1
      · have h_zm_ge_strict : mantissaFloorSucc ≤ zm.toNat := by omega
        exact no_normalized_in_open_ulp_gap_neg_zm ze' zm.toNat
          h_zm_ge_strict h_zm_lt m h_norm h_m_neg_strict h_neg_m_lo h_neg_m_hi
  · have h_truth_pos : 0 < truth := lt_of_le_of_ne h_truth_nn (Ne.symm h_truth_ne)
    have h_truth_abs_eq : |truth| = truth := abs_of_nonneg h_truth_nn
    rw [h_truth_abs_eq] at h_truth_abs
    have h_sign_xy := toRat_mul_sign x y
    have h_xy_same_sign : x.negative_ = y.negative_ := by
      by_contra h_diff
      have h_xy_neg : x.toRat * y.toRat ≤ 0 := by
        rcases hx : x.negative_ with _ | _
        · rcases hy : y.negative_ with _ | _
          · exfalso; apply h_diff; rw [hx, hy]
          · have hx_nn : (0 : ℚ) ≤ x.toRat := Number.toRat_nonneg_of_nonnegative x hx
            have hy_np : y.toRat ≤ 0 := Number.toRat_nonpos_of_negative y hy
            exact mul_nonpos_of_nonneg_of_nonpos hx_nn hy_np
        · rcases hy : y.negative_ with _ | _
          · have hx_np : x.toRat ≤ 0 := Number.toRat_nonpos_of_negative x hx
            have hy_nn : (0 : ℚ) ≤ y.toRat := Number.toRat_nonneg_of_nonnegative y hy
            exact mul_nonpos_of_nonpos_of_nonneg hx_np hy_nn
          · exfalso; apply h_diff; rw [hx, hy]
      change False; linarith
    have h_zn_false : (x.negative_ != y.negative_) = false := by
      rw [h_xy_same_sign]; simp [bne]
    have h_result_neg_false : result.negative_ = false := by rw [h_sign, h_zn_false]
    have h_result_nn : (0 : ℚ) ≤ result.toRat :=
      Number.toRat_nonneg_of_nonnegative result h_result_neg_false
    have h_result_abs_eq : |result.toRat| = result.toRat := abs_of_nonneg h_result_nn
    rw [h_result_abs_eq] at h_result_abs
    have h_m_pos : 0 < m.toRat := lt_of_lt_of_le h_truth_pos h_truth_le_m
    by_cases h_ru : g.shouldRoundUp zm
    · have h_f_ge_half : (1 : ℚ) / 2 ≤ f := by
        rcases h_ru with h_round_1 | ⟨h_round_0, _⟩
        · have h_gt : (1 : ℚ) / 2 < f := (round_correct hf_rep).1.mp h_round_1
          linarith
        · have h_eq : f = (1 : ℚ) / 2 := (round_correct hf_rep).2.2.mp h_round_0
          linarith
      by_cases h_cusp : zm.toNat + 1 ≤ maxRep.toNat
      · have h_val := doRoundUp_value_roundUp_noCusp g zm ze' h_ru h_cusp "Number::multiplication overflow" res_pos h_rup_pos hres_pos_mant_ne
        simp only at h_val
        rw [h_val] at h_result_abs
        have h_m_lo : (zm.toNat : ℚ) * 10 ^ ze' < m.toRat := by
          have h_compare : (zm.toNat : ℚ) < (zm.toNat : ℚ) + f := by linarith
          have h_compare_mul : (zm.toNat : ℚ) * 10 ^ ze' < ((zm.toNat : ℚ) + f) * 10 ^ ze' :=
            mul_lt_mul_of_pos_right h_compare h10ze_pos
          have : ((zm.toNat : ℚ) + f) * 10 ^ ze' = truth := by linarith
          linarith
        have h_m_hi : m.toRat < ((zm.toNat : ℚ) + 1) * 10 ^ ze' := by
          rw [← h_result_abs]; exact h_m_lt
        by_cases h_zm_floor : zm.toNat = mantissaFloor
        · have h_f_ge : (8 : ℚ) / 10 ≤ f := hfloor_constr h_zm_floor
          have h_m_strong : ((zm.toNat : ℚ) + 8/10) * 10 ^ ze' ≤ m.toRat := by
            calc ((zm.toNat : ℚ) + 8/10) * 10 ^ ze'
                ≤ ((zm.toNat : ℚ) + f) * 10 ^ ze' := by
                  apply mul_le_mul_of_nonneg_right _ (le_of_lt h10ze_pos)
                  linarith
              _ = truth := by linarith
              _ ≤ m.toRat := h_truth_le_m
          exact no_normalized_in_ulp_gap_at_floor_pos ze' zm.toNat h_zm_floor h_zm_lt
            m h_norm h_m_pos h_m_strong h_m_hi
        · have h_zm_ge_strict : mantissaFloorSucc ≤ zm.toNat := by omega
          exact no_normalized_in_open_ulp_gap_pos_zm ze' zm.toNat
            h_zm_ge_strict h_zm_lt m h_norm h_m_pos h_m_lo h_m_hi
      · push_neg at h_cusp
        have h_zm_eq : zm = maxRep := UInt64.toNat.inj (by omega)
        have h_val := doRoundUp_value_roundUp_cusp g zm ze' h_zm_eq h_ru "Number::multiplication overflow" res_pos h_rup_pos hres_pos_mant_ne
        simp only at h_val
        rw [h_val] at h_result_abs
        have h_m_lo : (maxRep.toNat : ℚ) * 10 ^ ze' < m.toRat := by
          have h_zm_eq_q : (zm.toNat : ℚ) = (maxRep.toNat : ℚ) := by rw [h_zm_eq]
          have h_compare : (maxRep.toNat : ℚ) < (maxRep.toNat : ℚ) + f := by linarith
          have h_compare_mul : (maxRep.toNat : ℚ) * 10 ^ ze' < ((maxRep.toNat : ℚ) + f) * 10 ^ ze' :=
            mul_lt_mul_of_pos_right h_compare h10ze_pos
          calc (maxRep.toNat : ℚ) * 10 ^ ze'
              < ((maxRep.toNat : ℚ) + f) * 10 ^ ze' := h_compare_mul
            _ = ((zm.toNat : ℚ) + f) * 10 ^ ze' := by rw [h_zm_eq_q]
            _ = truth := by linarith
            _ ≤ m.toRat := h_truth_le_m
        have h_m_hi : m.toRat < (maxRepCuspTarget : ℚ) * 10 ^ ze' := by
          rw [← h_result_abs]; exact h_m_lt
        exact no_normalized_in_cusp_gap_pos ze' m h_norm h_m_pos h_m_lo h_m_hi
    · -- No round-up + truth ≤ result forces truth = result, making truth ≤ m < result empty.
      exfalso
      have h_val := doRoundUp_value_no_roundUp g zm ze' h_ru "Number::multiplication overflow" res_pos h_rup_pos hres_pos_mant_ne
      simp only at h_val
      rw [h_val] at h_result_abs
      have h_compare : (zm.toNat : ℚ) ≤ (zm.toNat : ℚ) + f := by linarith
      have h_compare_mul : (zm.toNat : ℚ) * 10 ^ ze' ≤ ((zm.toNat : ℚ) + f) * 10 ^ ze' :=
        mul_le_mul_of_nonneg_right h_compare (le_of_lt h10ze_pos)
      have h_result_le_truth : result.toRat ≤ truth := by linarith [h_result_abs, h_truth_abs]
      have h_eq : result.toRat = truth := le_antisymm h_result_le_truth h_ge
      rw [h_eq] at h_m_lt
      linarith

/-! ## Branch A: round-down (no round-up fires) — result = lower(truth) -/

/-- When round-up does not fire (`result.toRat ≤ truth`) and no representable lies
strictly between `result` and `truth`, the result equals `Number.lower(truth)`. -/
theorem operator_mul_rounded_branchA (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (hok : Number.operator_mul x y .to_nearest = .ok result)
    (hresult : result.mantissa_ ≠ 0)
    (h_round_down : result.toRat ≤ x.toRat * y.toRat)
    (h_no_inbetween : ∀ m : Number, m.isNormalized →
                       result.toRat < m.toRat → ¬ (m.toRat ≤ x.toRat * y.toRat)) :
    ∃ n_lo : Number, Number.lower (x.toRat * y.toRat) = some n_lo ∧
                     result.toRat = n_lo.toRat := by
  set truth : ℚ := x.toRat * y.toRat with htruth_def
  have h_result_norm : result.isNormalized :=
    operator_mul_result_isNormalized x y result hx hy hx_mant_ne hy_mant_ne hok hresult
  have h_truth_ne : truth ≠ 0 := toRat_mul_ne_zero_of_normalized x y hx hy hx_mant_ne hy_mant_ne
  rcases lt_trichotomy truth 0 with h_truth_neg | h_truth_zero | h_truth_pos
  · set q : ℚ := -truth with hq_def
    have hq_pos : 0 < q := by rw [hq_def]; linarith
    have h_result_neg_le : result.toRat ≤ truth := h_round_down
    have h_result_neg : result.toRat < 0 := lt_of_le_of_lt h_result_neg_le h_truth_neg
    have h_result_neg_true : result.negative_ = true := by
      by_contra hh
      have h_nn : result.negative_ = false := by
        cases hN : result.negative_
        · rfl
        · exact absurd hN hh
      have : 0 ≤ result.toRat := Number.toRat_nonneg_of_nonnegative result h_nn
      linarith
    set r_pos : Number := ({result with negative_ := false} : Number) with hr_pos_def
    have h_r_pos_toRat : r_pos.toRat = -result.toRat := by
      rw [hr_pos_def]
      exact Number.toRat_set_neg_false_of_neg result h_result_neg_true
    have h_r_pos_pos : 0 < r_pos.toRat := by rw [h_r_pos_toRat]; linarith
    have h_r_pos_norm : r_pos.isNormalized := by
      rcases h_result_norm with hz | ⟨h_rmin, h_rmax, h_rcusp, h_remin, h_remax⟩
      · exfalso; apply hresult; rw [hz]; rfl
      right; exact ⟨h_rmin, h_rmax, h_rcusp, h_remin, h_remax⟩
    have h_r_pos_ge : q ≤ r_pos.toRat := by
      rw [hq_def, h_r_pos_toRat]; linarith
    set smallest_pos_rep : ℚ := (largeRange.min.toNat : ℚ) * (10 : ℚ) ^ minExponent
        with hspr_def
    have h_spr_eq : smallest_pos_rep = (10 : ℚ)^18 * (10 : ℚ) ^ minExponent := by
      rw [hspr_def]
      have h_min_eq : largeRange.min.toNat = 10^18 := by decide
      rw [h_min_eq]; norm_cast
    have h_spr_pos : 0 < smallest_pos_rep := by
      rw [hspr_def]
      have h_min_eq : largeRange.min.toNat = 10^18 := by decide
      have : (0 : ℚ) < (largeRange.min.toNat : ℚ) := by rw [h_min_eq]; norm_num
      have h_pow_pos : (0 : ℚ) < (10 : ℚ) ^ minExponent := zpow_pos (by norm_num) _
      positivity
    rcases h_r_pos_norm_disj : h_r_pos_norm with hz | ⟨h_rmin, h_rmax, h_rcusp, h_remin, h_remax⟩
    · exfalso
      have h_r_pos_zero : r_pos.toRat = 0 := by rw [hz, Number.toRat_zero]
      linarith
    by_cases h_exp_gt : minExponent < r_pos.exponent_
    · have h_lower_exists : ∃ n_lo_pos : Number, n_lo_pos.isNormalized ∧
                            0 < n_lo_pos.toRat ∧ n_lo_pos.toRat ≤ q := by
        let n_lo_pos : Number :=
          { negative_ := r_pos.negative_, mantissa_ := r_pos.mantissa_,
            exponent_ := r_pos.exponent_ - 1 }
        have h_n_lo_norm : n_lo_pos.isNormalized := by
          right
          refine ⟨h_rmin, h_rmax, h_rcusp, ?_, ?_⟩
          · change minExponent ≤ r_pos.exponent_ - 1
            linarith
          · change r_pos.exponent_ - 1 ≤ maxExponent
            linarith
        have h_n_lo_neg_false : n_lo_pos.negative_ = false := by
          change r_pos.negative_ = false
          rw [hr_pos_def]
        have h_n_lo_nn : 0 ≤ n_lo_pos.toRat :=
          Number.toRat_nonneg_of_nonnegative n_lo_pos h_n_lo_neg_false
        have h_n_lo_abs : |n_lo_pos.toRat| = (r_pos.mantissa_.toNat : ℚ) * 10 ^ (r_pos.exponent_ - 1) := by
          have := abs_toRat_eq n_lo_pos
          change |n_lo_pos.toRat| = _
          convert this using 2
        have h_r_pos_abs : |r_pos.toRat| = (r_pos.mantissa_.toNat : ℚ) * 10 ^ r_pos.exponent_ :=
          abs_toRat_eq r_pos
        have h_n_lo_eq : n_lo_pos.toRat = r_pos.toRat / 10 := by
          have habs : n_lo_pos.toRat = |n_lo_pos.toRat| := (abs_of_nonneg h_n_lo_nn).symm
          rw [habs, h_n_lo_abs]
          have h_r_pos_eq : r_pos.toRat = (r_pos.mantissa_.toNat : ℚ) * 10 ^ r_pos.exponent_ := by
            have := abs_of_pos h_r_pos_pos
            rw [← this, h_r_pos_abs]
          rw [h_r_pos_eq]
          have h_exp_split : (10 : ℚ) ^ r_pos.exponent_ = (10 : ℚ) ^ (r_pos.exponent_ - 1) * 10 := by
            rw [show r_pos.exponent_ = (r_pos.exponent_ - 1) + 1 from by ring,
                zpow_add_one₀ (by norm_num : (10 : ℚ) ≠ 0)]
            ring_nf
          rw [h_exp_split]
          field_simp
        have h_n_lo_pos : 0 < n_lo_pos.toRat := by rw [h_n_lo_eq]; positivity
        have h_bound := operator_mul_rounding_bound x y result hx hy hx_mant_ne hy_mant_ne hok hresult
        rw [← htruth_def] at h_bound
        have h_truth_abs : |truth| = -truth := abs_of_neg h_truth_neg
        have h_diff_le : |result.toRat - truth| ≤ -truth * (5 / (2 ^ 63 + 7 : ℚ)) := by
          have := h_bound; rw [h_truth_abs] at this; exact this
        have h_diff_eq : |result.toRat - truth| = truth - result.toRat := by
          rw [abs_of_nonpos (by linarith : result.toRat - truth ≤ 0)]; ring
        rw [h_diff_eq] at h_diff_le
        have h_r_pos_le_q : r_pos.toRat ≤ q + q * (5 / (2 ^ 63 + 7 : ℚ)) := by
          have h3 : r_pos.toRat - q = truth - result.toRat := by
            rw [h_r_pos_toRat, hq_def]; ring
          rw [show q + q * (5 / (2 ^ 63 + 7 : ℚ)) = q + (-truth) * (5 / (2 ^ 63 + 7 : ℚ)) from by rw [hq_def]]
          linarith
        have h_n_lo_le_q : n_lo_pos.toRat ≤ q := by
          rw [h_n_lo_eq]
          have h_eps_small : (5 / (2 ^ 63 + 7 : ℚ)) < 9 := by norm_num
          have h_mul_lt : q * (5 / (2 ^ 63 + 7 : ℚ)) < q * 9 :=
            mul_lt_mul_of_pos_left h_eps_small hq_pos
          have h_r_pos_le : r_pos.toRat ≤ q + q * 9 := by linarith
          have h_r_pos_le10 : r_pos.toRat ≤ 10 * q := by linarith
          linarith
        exact ⟨n_lo_pos, h_n_lo_norm, h_n_lo_pos, h_n_lo_le_q⟩
      obtain ⟨n_lo_pos, h_n_lo_pos_norm, h_n_lo_pos_pos, h_n_lo_pos_le⟩ := h_lower_exists
      have h_upper_exists := Number.upper_some_of_pos_witnesses q hq_pos
        n_lo_pos h_n_lo_pos_norm h_n_lo_pos_pos h_n_lo_pos_le
        r_pos h_r_pos_norm h_r_pos_ge
      obtain ⟨n_pos, h_n_pos_eq⟩ := h_upper_exists
      have hq_not_neg : ¬ (q < 0) := not_lt.mpr (le_of_lt hq_pos)
      have hq_ne : q ≠ 0 := ne_of_gt hq_pos
      have h_upperPosAux_eq : upperPosAux q = some n_pos := by
        have h_unfold : Number.upper q = upperPosAux q := by
          unfold Number.upper; rw [if_neg hq_ne, if_neg hq_not_neg]
        rw [h_unfold] at h_n_pos_eq; exact h_n_pos_eq
      have h_lower_eq : Number.lower truth = some ({n_pos with negative_ := true} : Number) := by
        rw [Number.lower_neg_eq truth h_truth_neg]
        have h_neg_truth_eq : (-truth) = q := by rw [hq_def]
        rw [h_neg_truth_eq, h_upperPosAux_eq]
        rfl
      refine ⟨({n_pos with negative_ := true} : Number), h_lower_eq, ?_⟩
      have h_min : ∀ m : Number, m.isNormalized → q ≤ m.toRat → r_pos.toRat ≤ m.toRat := by
        intro m h_norm h_m_ge
        by_contra h_not
        push_neg at h_not
        have h_m_pos : 0 < m.toRat := lt_of_lt_of_le hq_pos h_m_ge
        have h_m_neg_false : m.negative_ = false := by
          by_contra hh
          have h_mn : m.negative_ = true := by
            cases hN : m.negative_
            · exact absurd hN hh
            · rfl
          have : m.toRat ≤ 0 := Number.toRat_nonpos_of_negative m h_mn
          linarith
        set m_neg : Number := ({m with negative_ := true} : Number) with hm_neg_def
        have h_m_neg_toRat : m_neg.toRat = -m.toRat := by
          rw [hm_neg_def]; exact Number.toRat_set_neg_true_of_nn m h_m_neg_false
        have h_m_neg_norm : m_neg.isNormalized := by
          rcases h_norm with hz | ⟨hmin, hmax, hv, hemin, hemax⟩
          · exfalso
            have h_zero : m.toRat = 0 := by rw [hz, Number.toRat_zero]
            linarith
          right; exact ⟨hmin, hmax, hv, hemin, hemax⟩
        have h_m_neg_le_truth : m_neg.toRat ≤ truth := by
          rw [h_m_neg_toRat, ← hq_def] at *
          have : -m.toRat ≤ -q := by linarith
          linarith
        have h_result_lt_m_neg : result.toRat < m_neg.toRat := by
          rw [h_m_neg_toRat]
          have h_neg_lt : -r_pos.toRat < -m.toRat := by linarith
          rw [h_r_pos_toRat] at h_neg_lt
          linarith
        exact (h_no_inbetween m_neg h_m_neg_norm h_result_lt_m_neg) h_m_neg_le_truth
      have h_r_pos_eq_n_pos : r_pos.toRat = n_pos.toRat :=
        Number.toRat_eq_upper_of_min q r_pos n_pos h_n_pos_eq h_r_pos_norm h_r_pos_ge h_min
      have h_n_pos_neg_false : n_pos.negative_ = false := by
        unfold upperPosAux at h_upperPosAux_eq
        simp only at h_upperPosAux_eq
        rcases h_bump : bumpToValidMantissa
            ⌈q * (10 : ℚ)^(-(Int.log 10 q - mantissaLog))⌉₊ with _ | mm
        · rw [h_bump] at h_upperPosAux_eq; simp only at h_upperPosAux_eq
          split_ifs at h_upperPosAux_eq with h1 h2
          · exact congrArg (·.negative_) (Option.some.inj h_upperPosAux_eq).symm
          · exact congrArg (·.negative_) (Option.some.inj h_upperPosAux_eq).symm
        · rw [h_bump] at h_upperPosAux_eq; simp only at h_upperPosAux_eq
          split_ifs at h_upperPosAux_eq with h1 h2
          · exact congrArg (·.negative_) (Option.some.inj h_upperPosAux_eq).symm
          · exact congrArg (·.negative_) (Option.some.inj h_upperPosAux_eq).symm
      have h_flip_n_pos : ({n_pos with negative_ := true} : Number).toRat = -n_pos.toRat :=
        Number.toRat_set_neg_true_of_nn n_pos h_n_pos_neg_false
      rw [h_flip_n_pos]
      have : result.toRat = -r_pos.toRat := by rw [h_r_pos_toRat]; ring
      rw [this, h_r_pos_eq_n_pos]
    · push_neg at h_exp_gt
      have h_exp_eq : r_pos.exponent_ = minExponent := le_antisymm h_exp_gt h_remin
      have h_bound := operator_mul_rounding_bound x y result hx hy hx_mant_ne hy_mant_ne hok hresult
      rw [← htruth_def] at h_bound
      have h_truth_abs : |truth| = -truth := abs_of_neg h_truth_neg
      have h_diff_le : |result.toRat - truth| ≤ -truth * (5 / (2 ^ 63 + 7 : ℚ)) := by
        have := h_bound; rw [h_truth_abs] at this; exact this
      have h_diff_eq : |result.toRat - truth| = truth - result.toRat := by
        rw [abs_of_nonpos (by linarith : result.toRat - truth ≤ 0)]; ring
      rw [h_diff_eq] at h_diff_le
      have h_pow_min_pos : (0 : ℚ) < (10 : ℚ) ^ minExponent := zpow_pos (by norm_num) _
      have h_r_pos_neg_false : r_pos.negative_ = false := by rw [hr_pos_def]
      have h_r_pos_eq_form :
          r_pos.toRat = (r_pos.mantissa_.toNat : ℚ) * (10 : ℚ) ^ minExponent := by
        rw [Number.toRat_of_nonneg r_pos h_r_pos_neg_false, h_exp_eq]
      have h_r_pos_ge_spr : smallest_pos_rep ≤ r_pos.toRat := by
        rw [h_r_pos_eq_form, hspr_def]
        have h_min_nat : largeRange.min.toNat ≤ r_pos.mantissa_.toNat :=
          UInt64.le_iff_toNat_le.mp h_rmin
        have h_cast : (largeRange.min.toNat : ℚ) ≤ (r_pos.mantissa_.toNat : ℚ) := by
          exact_mod_cast h_min_nat
        exact mul_le_mul_of_nonneg_right h_cast (le_of_lt h_pow_min_pos)
      have h_r_pos_q_diff : r_pos.toRat - q = truth - result.toRat := by
        rw [h_r_pos_toRat, hq_def]; ring
      have h_r_pos_le_q_plus : r_pos.toRat ≤ q + q * (5 / (2 ^ 63 + 7 : ℚ)) := by
        have h_step : truth - result.toRat ≤ q * (5 / (2 ^ 63 + 7 : ℚ)) := by
          rw [hq_def]; linarith
        linarith [h_r_pos_q_diff]
      by_cases h_q_ge : smallest_pos_rep ≤ q
      · have h_lower_exists : ∃ n_lo_pos : Number, n_lo_pos.isNormalized ∧
                              0 < n_lo_pos.toRat ∧ n_lo_pos.toRat ≤ q := by
          let n_lo_pos : Number :=
            { negative_ := false, mantissa_ := largeRange.min, exponent_ := minExponent }
          have h_n_lo_norm : n_lo_pos.isNormalized := by
            right
            refine ⟨?_, ?_, ?_, ?_, ?_⟩
            · change largeRange.min ≤ largeRange.min
              rw [UInt64.le_iff_toNat_le]
            · change largeRange.min ≤ largeRange.max
              rw [UInt64.le_iff_toNat_le]
              have hmin_eq : largeRange.min.toNat = 1000000000000000000 := by decide
              have hmax_eq : largeRange.max.toNat = 9999999999999999999 := by decide
              omega
            · right
              change largeRange.min.toNat % 10 = 0
              decide
            · change minExponent ≤ minExponent; rfl
            · change minExponent ≤ maxExponent; decide
          have h_n_lo_neg_false : n_lo_pos.negative_ = false := rfl
          have h_n_lo_eq : n_lo_pos.toRat = smallest_pos_rep := by
            rw [Number.toRat_of_nonneg n_lo_pos h_n_lo_neg_false]
          have h_n_lo_pos : 0 < n_lo_pos.toRat := by rw [h_n_lo_eq]; exact h_spr_pos
          have h_n_lo_le : n_lo_pos.toRat ≤ q := by rw [h_n_lo_eq]; exact h_q_ge
          exact ⟨n_lo_pos, h_n_lo_norm, h_n_lo_pos, h_n_lo_le⟩
        obtain ⟨n_lo_pos, h_n_lo_pos_norm, h_n_lo_pos_pos, h_n_lo_pos_le⟩ := h_lower_exists
        have h_upper_exists := Number.upper_some_of_pos_witnesses q hq_pos
          n_lo_pos h_n_lo_pos_norm h_n_lo_pos_pos h_n_lo_pos_le
          r_pos h_r_pos_norm h_r_pos_ge
        obtain ⟨n_pos, h_n_pos_eq⟩ := h_upper_exists
        have hq_not_neg : ¬ (q < 0) := not_lt.mpr (le_of_lt hq_pos)
        have hq_ne : q ≠ 0 := ne_of_gt hq_pos
        have h_upperPosAux_eq : upperPosAux q = some n_pos := by
          have h_unfold : Number.upper q = upperPosAux q := by
            unfold Number.upper; rw [if_neg hq_ne, if_neg hq_not_neg]
          rw [h_unfold] at h_n_pos_eq; exact h_n_pos_eq
        have h_lower_eq : Number.lower truth = some ({n_pos with negative_ := true} : Number) := by
          rw [Number.lower_neg_eq truth h_truth_neg]
          have h_neg_truth_eq : (-truth) = q := by rw [hq_def]
          rw [h_neg_truth_eq, h_upperPosAux_eq]
          rfl
        refine ⟨({n_pos with negative_ := true} : Number), h_lower_eq, ?_⟩
        have h_min : ∀ m : Number, m.isNormalized → q ≤ m.toRat → r_pos.toRat ≤ m.toRat := by
          intro m h_norm h_m_ge
          by_contra h_not
          push_neg at h_not
          have h_m_pos : 0 < m.toRat := lt_of_lt_of_le hq_pos h_m_ge
          have h_m_neg_false : m.negative_ = false := by
            by_contra hh
            have h_mn : m.negative_ = true := by
              cases hN : m.negative_
              · exact absurd hN hh
              · rfl
            have : m.toRat ≤ 0 := Number.toRat_nonpos_of_negative m h_mn
            linarith
          set m_neg : Number := ({m with negative_ := true} : Number) with hm_neg_def
          have h_m_neg_toRat : m_neg.toRat = -m.toRat := by
            rw [hm_neg_def]; exact Number.toRat_set_neg_true_of_nn m h_m_neg_false
          have h_m_neg_norm : m_neg.isNormalized := by
            rcases h_norm with hz | ⟨hmin, hmax, hv, hemin, hemax⟩
            · exfalso
              have h_zero : m.toRat = 0 := by rw [hz, Number.toRat_zero]
              linarith
            right; exact ⟨hmin, hmax, hv, hemin, hemax⟩
          have h_m_neg_le_truth : m_neg.toRat ≤ truth := by
            rw [h_m_neg_toRat, ← hq_def] at *
            have : -m.toRat ≤ -q := by linarith
            linarith
          have h_result_lt_m_neg : result.toRat < m_neg.toRat := by
            rw [h_m_neg_toRat]
            have h_neg_lt : -r_pos.toRat < -m.toRat := by linarith
            rw [h_r_pos_toRat] at h_neg_lt
            linarith
          exact (h_no_inbetween m_neg h_m_neg_norm h_result_lt_m_neg) h_m_neg_le_truth
        have h_r_pos_eq_n_pos : r_pos.toRat = n_pos.toRat :=
          Number.toRat_eq_upper_of_min q r_pos n_pos h_n_pos_eq h_r_pos_norm h_r_pos_ge h_min
        have h_n_pos_neg_false : n_pos.negative_ = false := by
          unfold upperPosAux at h_upperPosAux_eq
          simp only at h_upperPosAux_eq
          rcases h_bump : bumpToValidMantissa
              ⌈q * (10 : ℚ)^(-(Int.log 10 q - mantissaLog))⌉₊ with _ | mm
          · rw [h_bump] at h_upperPosAux_eq; simp only at h_upperPosAux_eq
            split_ifs at h_upperPosAux_eq with h1 h2
            · exact congrArg (·.negative_) (Option.some.inj h_upperPosAux_eq).symm
            · exact congrArg (·.negative_) (Option.some.inj h_upperPosAux_eq).symm
          · rw [h_bump] at h_upperPosAux_eq; simp only at h_upperPosAux_eq
            split_ifs at h_upperPosAux_eq with h1 h2
            · exact congrArg (·.negative_) (Option.some.inj h_upperPosAux_eq).symm
            · exact congrArg (·.negative_) (Option.some.inj h_upperPosAux_eq).symm
        have h_flip_n_pos : ({n_pos with negative_ := true} : Number).toRat = -n_pos.toRat :=
          Number.toRat_set_neg_true_of_nn n_pos h_n_pos_neg_false
        rw [h_flip_n_pos]
        have : result.toRat = -r_pos.toRat := by rw [h_r_pos_toRat]; ring
        rw [this, h_r_pos_eq_n_pos]
      · -- q < smallest_pos_rep: r_pos.mantissa_ is forced to 10^18.
        push_neg at h_q_ge
        have h_eps_pos : (0 : ℚ) < 5 / (2^63 + 7 : ℚ) := by norm_num
        have h_q_eps_lt : q * (5 / (2^63 + 7 : ℚ)) < smallest_pos_rep * (5 / (2^63 + 7 : ℚ)) :=
          mul_lt_mul_of_pos_right h_q_ge h_eps_pos
        have h_r_pos_lt_bound : r_pos.toRat < smallest_pos_rep + smallest_pos_rep * (5 / (2^63 + 7 : ℚ)) := by
          linarith
        have h_bound_mant : (r_pos.mantissa_.toNat : ℚ) <
            (10 : ℚ)^18 + (10 : ℚ)^18 * (5 / (2^63 + 7 : ℚ)) := by
          have h1 : (r_pos.mantissa_.toNat : ℚ) * (10 : ℚ) ^ minExponent <
              ((10 : ℚ)^18 + (10 : ℚ)^18 * (5 / (2^63 + 7 : ℚ))) * (10 : ℚ) ^ minExponent := by
            calc (r_pos.mantissa_.toNat : ℚ) * (10 : ℚ) ^ minExponent
                = r_pos.toRat := h_r_pos_eq_form.symm
              _ < smallest_pos_rep + smallest_pos_rep * (5 / (2^63 + 7 : ℚ)) := h_r_pos_lt_bound
              _ = ((10 : ℚ)^18 + (10 : ℚ)^18 * (5 / (2^63 + 7 : ℚ))) * (10 : ℚ) ^ minExponent := by
                  rw [h_spr_eq]; ring
          exact lt_of_mul_lt_mul_of_nonneg_right h1 (le_of_lt h_pow_min_pos)
        have h_rhs_lt : (10 : ℚ)^18 + (10 : ℚ)^18 * (5 / (2^63 + 7 : ℚ)) < (10 : ℚ)^18 + 1 := by
          have h_denom_pos : (0 : ℚ) < 2^63 + 7 := by norm_num
          have h_eps_small : (10 : ℚ)^18 * (5 / (2^63 + 7 : ℚ)) < 1 := by
            rw [mul_div_assoc']
            rw [div_lt_iff₀ h_denom_pos]
            norm_num
          linarith
        have h_mant_lt : (r_pos.mantissa_.toNat : ℚ) < (10 : ℚ)^18 + 1 := by linarith
        have h_mant_le_pow18 : r_pos.mantissa_.toNat ≤ 10^18 := by
          have h_lt_nat : (r_pos.mantissa_.toNat : ℚ) < ((10^18 + 1 : ℕ) : ℚ) := by
            push_cast; linarith
          have : r_pos.mantissa_.toNat < 10^18 + 1 := by exact_mod_cast h_lt_nat
          omega
        have h_mant_ge_pow18 : 10^18 ≤ r_pos.mantissa_.toNat := by
          have h_min_nat : largeRange.min.toNat ≤ r_pos.mantissa_.toNat :=
            UInt64.le_iff_toNat_le.mp h_rmin
          have h_min_eq : largeRange.min.toNat = 10^18 := by decide
          omega
        have h_mant_eq_pow18 : r_pos.mantissa_.toNat = 10^18 := by omega
        have h_mant_eq_min : r_pos.mantissa_.toNat = largeRange.min.toNat := by
          have h_min_eq : largeRange.min.toNat = 10^18 := by decide
          omega
        have h_r_pos_eq_spr : r_pos.toRat = smallest_pos_rep := by
          rw [h_r_pos_eq_form, hspr_def]
          congr 1
          exact_mod_cast h_mant_eq_min
        have h_q_ne : q ≠ 0 := ne_of_gt hq_pos
        have h_q_not_neg : ¬ (q < 0) := not_lt.mpr (le_of_lt hq_pos)
        have h_upper_unfold : Number.upper q = upperPosAux q := by
          unfold Number.upper; rw [if_neg h_q_ne, if_neg h_q_not_neg]
        have h_q_lt_pow : q < (10 : ℚ) ^ (minExponent + 18) := by
          have h_pow_form : smallest_pos_rep = (10 : ℚ) ^ (minExponent + 18) := by
            rw [h_spr_eq]
            rw [show (minExponent + 18 : ℤ) = 18 + minExponent from by ring,
                zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0)]
            congr 1
          rw [← h_pow_form]; exact h_q_ge
        have h_e_q_lt_min : Int.log 10 q - mantissaLog < minExponent := by
          by_contra h_not
          push_neg at h_not
          have h_log_ge : minExponent + 18 ≤ Int.log 10 q := by linarith
          have h_pow_mono : (10 : ℚ) ^ (minExponent + 18) ≤ (10 : ℚ) ^ (Int.log 10 q) :=
            zpow_le_zpow_right₀ (by norm_num) h_log_ge
          have h_log_le : (10 : ℚ) ^ (Int.log 10 q) ≤ q :=
            Int.zpow_log_le_self (by norm_num : (1 : ℕ) < 10) hq_pos
          have h_chain : (10 : ℚ) ^ (minExponent + 18) ≤ q := le_trans h_pow_mono h_log_le
          linarith
        let n_up_target : Number := ⟨false, largeRange.min, minExponent⟩
        have h_n_up_target_toRat : n_up_target.toRat = smallest_pos_rep := by
          have h_neg : n_up_target.negative_ = false := rfl
          rw [Number.toRat_of_nonneg n_up_target h_neg]
        have h_upperPos_eq : upperPosAux q = some n_up_target := by
          unfold upperPosAux
          simp only
          set e_q : ℤ := Int.log 10 q - mantissaLog
          set m_real : ℚ := q * (10 : ℚ)^(-e_q)
          set m_ceil_nat : ℕ := ⌈m_real⌉₊
          have h_e_q_lt_min' : e_q < minExponent := h_e_q_lt_min
          rcases h_bump : bumpToValidMantissa m_ceil_nat with _ | m_b
          · change (if h_exp : minExponent ≤ e_q + 1 ∧ e_q + 1 ≤ maxExponent then
                    some (⟨false, largeRange.min, e_q + 1⟩ : Number)
                  else if e_q + 1 < minExponent then
                    some (⟨false, largeRange.min, minExponent⟩ : Number)
                  else none) = some n_up_target
            have h_e_q1_le_min : e_q + 1 ≤ minExponent := by linarith
            by_cases h_dif : minExponent ≤ e_q + 1 ∧ e_q + 1 ≤ maxExponent
            · have h_e_q1_eq : e_q + 1 = minExponent := le_antisymm h_e_q1_le_min h_dif.1
              rw [dif_pos h_dif]
              change (some (⟨false, largeRange.min, e_q + 1⟩ : Number) : Option Number) = some n_up_target
              rw [h_e_q1_eq]
            · rw [dif_neg h_dif]
              have h_e_q1_lt : e_q + 1 < minExponent := by
                rcases lt_or_eq_of_le h_e_q1_le_min with h | h
                · exact h
                · exfalso; apply h_dif
                  refine ⟨h.ge, ?_⟩
                  have h_min_eq : minExponent = -32768 := rfl
                  have h_max_eq : maxExponent = 32768 := rfl
                  omega
              rw [if_pos h_e_q1_lt]
          · change (if h_exp : minExponent ≤ e_q ∧ e_q ≤ maxExponent ∧ m_b < 2^64 then
                    some (⟨false, ⟨m_b, h_exp.2.2⟩, e_q⟩ : Number)
                  else if e_q < minExponent then
                    some (⟨false, largeRange.min, minExponent⟩ : Number)
                  else none) = some n_up_target
            have h_not_in : ¬ (minExponent ≤ e_q ∧ e_q ≤ maxExponent ∧ m_b < 2^64) := by
              intro ⟨h1, _, _⟩; linarith
            rw [dif_neg h_not_in]
            rw [if_pos h_e_q_lt_min']
        have h_upper_q_target : Number.upper q = some n_up_target := by
          rw [h_upper_unfold]; exact h_upperPos_eq
        have h_lower_eq : Number.lower truth = some ({n_up_target with negative_ := true} : Number) := by
          rw [Number.lower_neg_eq truth h_truth_neg]
          have h_neg_truth_eq : (-truth) = q := by rw [hq_def]
          rw [h_neg_truth_eq, h_upperPos_eq]
          rfl
        refine ⟨({n_up_target with negative_ := true} : Number), h_lower_eq, ?_⟩
        have h_n_up_target_neg : n_up_target.negative_ = false := rfl
        have h_flip : ({n_up_target with negative_ := true} : Number).toRat = -n_up_target.toRat :=
          Number.toRat_set_neg_true_of_nn n_up_target h_n_up_target_neg
        rw [h_flip, h_n_up_target_toRat]
        have h_res_eq : result.toRat = -r_pos.toRat := by rw [h_r_pos_toRat]; ring
        rw [h_res_eq, h_r_pos_eq_spr]
  · exact absurd h_truth_zero h_truth_ne
  · have h_sign := toRat_mul_sign x y
    have h_xy_same_sign : x.negative_ = y.negative_ := by
      by_contra h_diff
      have : truth ≤ 0 := h_sign.2 h_diff
      linarith
    have h_zn_false : (x.negative_ != y.negative_) = false := by
      simp [bne, h_xy_same_sign]
    have h_result_pos : 0 < result.toRat := by
      have h_abs_result : |result.toRat| = (result.mantissa_.toNat : ℚ) * 10 ^ result.exponent_ :=
        abs_toRat_eq result
      have h_mant_pos : 0 < (result.mantissa_.toNat : ℚ) := by
        have h_bounds := h_result_norm.mantissaBounds hresult
        have h_min : largeRange.min.toNat ≤ result.mantissa_.toNat :=
          UInt64.le_iff_toNat_le.mp h_bounds.1
        have h_min_val : largeRange.min.toNat = 10^18 := by decide
        rw [h_min_val] at h_min
        have : 0 < result.mantissa_.toNat := by omega
        exact_mod_cast this
      have h_pow_pos : (0 : ℚ) < (10 : ℚ) ^ result.exponent_ := zpow_pos (by norm_num) _
      have h_abs_pos : 0 < |result.toRat| := by
        rw [h_abs_result]; positivity
      -- Contradiction via rounding bound: |result - truth| ≤ |truth| * ε < |truth|.
      by_contra h_not_pos
      push_neg at h_not_pos
      have h_lt : result.toRat < 0 := by
        rcases lt_or_eq_of_le h_not_pos with h | h
        · exact h
        · exfalso; rw [h] at h_abs_pos; simp at h_abs_pos
      have h_result_neg_true : result.negative_ = true := by
        by_contra h_not
        have h_result_nn : result.negative_ = false := by
          cases h : result.negative_
          · rfl
          · exact absurd h h_not
        have : 0 ≤ result.toRat := Number.toRat_nonneg_of_nonnegative result h_result_nn
        linarith
      have h_bound := operator_mul_rounding_bound x y result hx hy hx_mant_ne hy_mant_ne hok hresult
      rw [← htruth_def] at h_bound
      have h_abs_truth_pos : 0 < |truth| := abs_pos.mpr h_truth_ne
      have h_eps_small : (5 / (2 ^ 63 + 7 : ℚ)) < 1 := by norm_num
      have h_rhs_lt : |truth| * (5 / (2 ^ 63 + 7 : ℚ)) < |truth| := by
        have : |truth| * (5 / (2 ^ 63 + 7 : ℚ)) < |truth| * 1 :=
          mul_lt_mul_of_pos_left h_eps_small h_abs_truth_pos
        linarith
      have h_truth_abs : |truth| = truth := abs_of_pos h_truth_pos
      have h_diff_eq : |result.toRat - truth| = truth - result.toRat := by
        rw [abs_of_nonpos (by linarith : result.toRat - truth ≤ 0)]
        ring
      rw [h_diff_eq] at h_bound
      rw [h_truth_abs] at h_bound h_rhs_lt
      linarith
    have h_upper_exists : ∃ n_hi : Number, n_hi.isNormalized ∧ truth ≤ n_hi.toRat := by
      have h_result_neg_false : result.negative_ = false := by
        by_contra hh
        have hh' : result.negative_ = true := by
          cases hN : result.negative_
          · exact absurd hN hh
          · rfl
        have : result.toRat ≤ 0 := Number.toRat_nonpos_of_negative result hh'
        linarith
      have h_result_bounds := h_result_norm.mantissaBounds hresult
      rcases h_result_norm with hz | ⟨h_rmin, h_rmax, h_rcusp, h_remin, h_remax⟩
      · exfalso; apply hresult; rw [hz]; rfl
      by_cases h_exp_lt : result.exponent_ < maxExponent
      · let n_hi : Number := { result with exponent_ := result.exponent_ + 1 }
        have h_n_hi_norm : n_hi.isNormalized := by
          right
          refine ⟨h_rmin, h_rmax, h_rcusp, ?_, ?_⟩
          · change minExponent ≤ result.exponent_ + 1
            linarith
          · change result.exponent_ + 1 ≤ maxExponent
            linarith
        have h_n_hi_abs : |n_hi.toRat| = (result.mantissa_.toNat : ℚ) * 10 ^ (result.exponent_ + 1) := by
          have := abs_toRat_eq n_hi
          change |n_hi.toRat| = _
          convert this using 2
        have h_result_abs : |result.toRat| = (result.mantissa_.toNat : ℚ) * 10 ^ result.exponent_ :=
          abs_toRat_eq result
        have h_n_hi_neg_false : n_hi.negative_ = false := h_result_neg_false
        have h_n_hi_nn : 0 ≤ n_hi.toRat := Number.toRat_nonneg_of_nonnegative n_hi h_n_hi_neg_false
        have h_n_hi_eq : n_hi.toRat = 10 * result.toRat := by
          have hpos : 0 ≤ n_hi.toRat := h_n_hi_nn
          have habs : n_hi.toRat = |n_hi.toRat| := (abs_of_nonneg hpos).symm
          rw [habs, h_n_hi_abs]
          have h_result_eq : result.toRat = (result.mantissa_.toNat : ℚ) * 10 ^ result.exponent_ := by
            have := abs_of_pos h_result_pos
            rw [← this, h_result_abs]
          rw [h_result_eq]
          rw [zpow_add_one₀ (by norm_num : (10 : ℚ) ≠ 0)]
          ring
        have h_bound := operator_mul_rounding_bound x y result hx hy hx_mant_ne hy_mant_ne hok hresult
        rw [← htruth_def] at h_bound
        have h_eps_lt : (5 / (2 ^ 63 + 7 : ℚ)) < 1 / 10 := by norm_num
        have h_truth_abs : |truth| = truth := abs_of_pos h_truth_pos
        have h_diff_le : |result.toRat - truth| ≤ truth * (5 / (2 ^ 63 + 7 : ℚ)) := by
          have := h_bound; rw [h_truth_abs] at this; exact this
        have h_diff_eq : |result.toRat - truth| = truth - result.toRat := by
          rw [abs_of_nonpos (by linarith : result.toRat - truth ≤ 0)]; ring
        rw [h_diff_eq] at h_diff_le
        have h_truth_pos' : 0 < truth := h_truth_pos
        have h_eps_mul : truth * (5 / (2 ^ 63 + 7 : ℚ)) < truth * (1 / 10) :=
          mul_lt_mul_of_pos_left h_eps_lt h_truth_pos'
        have h_truth_le : truth - result.toRat < truth * (1 / 10) := by linarith
        have h_ten_result : 9 * truth < 10 * result.toRat := by linarith
        have h_final : truth ≤ 10 * result.toRat := by linarith
        refine ⟨n_hi, h_n_hi_norm, ?_⟩
        rw [h_n_hi_eq]; exact h_final
      · -- result.exponent_ = maxExponent: derive truth < 10^(maxExp+19) via rounding bound.
        push_neg at h_exp_lt
        have h_exp_eq : result.exponent_ = maxExponent := le_antisymm h_remax h_exp_lt
        have h_e_ge_maxE : result.exponent_ ≥ maxExponent := by rw [h_exp_eq]
        have h_mant_le_maxRep_64 : result.mantissa_ ≤ maxRep :=
          operator_mul_no_overflow_mantissa x y result hx hy hx_mant_ne hy_mant_ne hok hresult h_e_ge_maxE
        have h_mant_le_maxRep : result.mantissa_.toNat ≤ maxRep.toNat :=
          UInt64.le_iff_toNat_le.mp h_mant_le_maxRep_64
        have h_result_neg_false_2 : result.negative_ = false := by
          by_contra hh
          have hh' : result.negative_ = true := by
            cases hN : result.negative_
            · exact absurd hN hh
            · rfl
          have : result.toRat ≤ 0 := Number.toRat_nonpos_of_negative result hh'
          linarith
        have h_result_eq_form : result.toRat = (result.mantissa_.toNat : ℚ) * 10^maxExponent := by
          rw [Number.toRat_of_nonneg result h_result_neg_false_2, h_exp_eq]
        have h_pow_max_pos : (0 : ℚ) < 10 ^ maxExponent := zpow_pos (by norm_num) _
        have h_maxR_val : maxRep.toNat = maxRepNat := by decide
        have h_result_le_bound : result.toRat ≤ maxRepNat * 10^maxExponent := by
          rw [h_result_eq_form]
          have : (result.mantissa_.toNat : ℚ) ≤ ((maxRep.toNat : ℕ) : ℚ) := by exact_mod_cast h_mant_le_maxRep
          have h_maxR_cast : ((maxRep.toNat : ℕ) : ℚ) = (maxRepNat : ℚ) := by
            rw [h_maxR_val]; norm_cast
          rw [h_maxR_cast] at this
          exact mul_le_mul_of_nonneg_right this (le_of_lt h_pow_max_pos)
        have h_bound := operator_mul_rounding_bound x y result hx hy hx_mant_ne hy_mant_ne hok hresult
        rw [← htruth_def] at h_bound
        have h_truth_abs : |truth| = truth := abs_of_pos h_truth_pos
        have h_diff_le : |result.toRat - truth| ≤ truth * (5 / (2 ^ 63 + 7 : ℚ)) := by
          have := h_bound; rw [h_truth_abs] at this; exact this
        have h_diff_eq : |result.toRat - truth| = truth - result.toRat := by
          rw [abs_of_nonpos (by linarith : result.toRat - truth ≤ 0)]; ring
        rw [h_diff_eq] at h_diff_le
        have h_denom_pos : (0 : ℚ) < 2^63 + 7 := by norm_num
        have h_truth_scaled : truth * ((2^63 + 2 : ℚ) / (2^63 + 7)) ≤ result.toRat := by
          have h_step : truth - truth * (5 / (2^63 + 7 : ℚ)) ≤ result.toRat := by linarith
          have h_eq : truth - truth * (5 / (2^63 + 7 : ℚ)) = truth * ((2^63 + 2 : ℚ) / (2^63 + 7)) := by
            field_simp
            ring
          rw [h_eq] at h_step; exact h_step
        have h_factor_pos : (0 : ℚ) < (2^63 + 2 : ℚ) / (2^63 + 7) := by
          apply div_pos
          · norm_num
          · norm_num
        have h_truth_le_result_factor : truth ≤ result.toRat * ((2^63 + 7 : ℚ) / (2^63 + 2)) := by
          have h_pos : (0 : ℚ) < (2^63 + 7 : ℚ) / (2^63 + 2) := by
            apply div_pos
            · norm_num
            · norm_num
          have h_inv : (2^63 + 7 : ℚ) / (2^63 + 2) = ((2^63 + 2 : ℚ) / (2^63 + 7))⁻¹ := by
            field_simp
          rw [h_inv]
          rw [le_mul_inv_iff₀ h_factor_pos]
          linarith
        have h_truth_bound1 : truth ≤ maxRepNat * 10^maxExponent * ((2^63 + 7 : ℚ) / (2^63 + 2)) := by
          have h_factor_pos2 : (0 : ℚ) < (2^63 + 7 : ℚ) / (2^63 + 2) := by
            apply div_pos <;> norm_num
          have := mul_le_mul_of_nonneg_right h_result_le_bound (le_of_lt h_factor_pos2)
          linarith
        have h_numeric : maxRepNat * ((2^63 + 7 : ℚ) / (2^63 + 2)) < 10^19 := by
          have h_denom : (0 : ℚ) < 2^63 + 2 := by norm_num
          have h_rewrite : maxRepNat * ((2^63 + 7 : ℚ) / (2^63 + 2)) = (maxRepNat * (2^63 + 7 : ℚ)) / (2^63 + 2) := by
            ring
          rw [h_rewrite, div_lt_iff₀ h_denom]
          have h_2_63 : (2^63 : ℚ) = 9223372036854775808 := by norm_num
          rw [h_2_63]
          norm_num
        have h_truth_lt_pow : truth < 10^19 * 10^maxExponent := by
          have h_left : maxRepNat * 10^maxExponent * ((2^63 + 7 : ℚ) / (2^63 + 2))
              = maxRepNat * ((2^63 + 7 : ℚ) / (2^63 + 2)) * 10^maxExponent := by ring
          have h_mul_lt : maxRepNat * ((2^63 + 7 : ℚ) / (2^63 + 2)) * 10^maxExponent
              < 10^19 * 10^maxExponent :=
            mul_lt_mul_of_pos_right h_numeric h_pow_max_pos
          rw [h_left] at h_truth_bound1
          linarith
        have h_pow_eq : (10 : ℚ)^19 * 10^maxExponent = 10^(maxExponent + 19) := by
          rw [show (10 : ℚ) ^ 19 = (10 : ℚ) ^ (19 : ℤ) from by norm_num,
              ← zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0)]
          ring_nf
        rw [h_pow_eq] at h_truth_lt_pow
        have h_log_lt : (10 : ℚ) ^ (Int.log 10 truth) ≤ truth :=
          Int.zpow_log_le_self (by norm_num : (1 : ℕ) < 10) h_truth_pos
        have h_log_le_max : Int.log 10 truth ≤ maxExponent + 18 := by
          by_contra h_not
          push_neg at h_not
          have h_log_ge : maxExponent + 19 ≤ Int.log 10 truth := by linarith
          have h_pow_mono : (10 : ℚ) ^ (maxExponent + 19) ≤ (10 : ℚ) ^ (Int.log 10 truth) :=
            zpow_le_zpow_right₀ (by norm_num) h_log_ge
          have h_chain : (10 : ℚ) ^ (maxExponent + 19) ≤ truth := le_trans h_pow_mono h_log_lt
          linarith
        have h_n_hi_m_lt_64 : (maxMul10Witness : ℕ) < 2^64 := by decide
        let n_hi : Number := ⟨false, ⟨maxMul10Witness, h_n_hi_m_lt_64⟩, maxExponent⟩
        have h_n_hi_toNat : ((⟨⟨⟨maxMul10Witness, h_n_hi_m_lt_64⟩⟩⟩ : UInt64)).toNat = maxMul10Witness := rfl
        have h_n_hi_norm : n_hi.isNormalized := by
          right
          refine ⟨?_, ?_, ?_, ?_, ?_⟩
          · change largeRange.min ≤ (⟨⟨⟨maxMul10Witness, h_n_hi_m_lt_64⟩⟩⟩ : UInt64)
            rw [UInt64.le_iff_toNat_le, h_n_hi_toNat]
            have : largeRange.min.toNat = 1000000000000000000 := by decide
            omega
          · change (⟨⟨⟨maxMul10Witness, h_n_hi_m_lt_64⟩⟩⟩ : UInt64) ≤ largeRange.max
            rw [UInt64.le_iff_toNat_le, h_n_hi_toNat]
            have : largeRange.max.toNat = 9999999999999999999 := by decide
            omega
          · right
            change ((⟨⟨⟨maxMul10Witness, h_n_hi_m_lt_64⟩⟩⟩ : UInt64).toNat) % 10 = 0
            rw [h_n_hi_toNat]
          · change minExponent ≤ maxExponent; decide
          · change maxExponent ≤ maxExponent; rfl
        have h_n_hi_neg_false : n_hi.negative_ = false := rfl
        have h_n_hi_ge : truth ≤ n_hi.toRat := by
          rw [Number.toRat_of_nonneg n_hi h_n_hi_neg_false]
          change truth ≤ ((⟨⟨⟨maxMul10Witness, h_n_hi_m_lt_64⟩⟩⟩ : UInt64).toNat : ℚ) * (10 : ℚ) ^ maxExponent
          rw [h_n_hi_toNat]
          have h_cast : ((maxMul10Witness : ℕ) : ℚ) = maxMul10Witness := by norm_cast
          rw [h_cast]
          have h_factor_bound : maxRepNat * ((2^63 + 7 : ℚ) / (2^63 + 2)) ≤ maxMul10Witness := by
            have h_denom : (0 : ℚ) < 2^63 + 2 := by norm_num
            have h_rewrite : maxRepNat * ((2^63 + 7 : ℚ) / (2^63 + 2)) = (maxRepNat * (2^63 + 7 : ℚ)) / (2^63 + 2) := by
              ring
            rw [h_rewrite, div_le_iff₀ h_denom]
            have h_2_63 : (2^63 : ℚ) = 9223372036854775808 := by norm_num
            rw [h_2_63]
            norm_num
          calc truth ≤ maxRepNat * 10^maxExponent * ((2^63 + 7 : ℚ) / (2^63 + 2)) := h_truth_bound1
            _ = maxRepNat * ((2^63 + 7 : ℚ) / (2^63 + 2)) * 10^maxExponent := by ring
            _ ≤ maxMul10Witness * 10^maxExponent :=
                mul_le_mul_of_nonneg_right h_factor_bound (le_of_lt h_pow_max_pos)
        exact ⟨n_hi, h_n_hi_norm, h_n_hi_ge⟩
    obtain ⟨n_hi, h_hi_norm, h_hi_ge⟩ := h_upper_exists
    have h_lower_exists := Number.lower_some_of_pos_witnesses truth h_truth_pos
      result h_result_norm h_result_pos h_round_down
      n_hi h_hi_norm h_hi_ge
    obtain ⟨n_lo, h_lo_eq⟩ := h_lower_exists
    refine ⟨n_lo, h_lo_eq, ?_⟩
    have h_max : ∀ m : Number, m.isNormalized → m.toRat ≤ truth → m.toRat ≤ result.toRat := by
      intro m h_norm h_m_le
      by_contra h_not
      push_neg at h_not
      exact (h_no_inbetween m h_norm h_not) h_m_le
    exact Number.toRat_eq_lower_of_max truth result n_lo h_lo_eq
      h_result_norm h_round_down h_max

/-! ## Branch B: round-up (no cusp overflow) — result = upper(truth) -/

/-- When round-up fires (`truth ≤ result.toRat`) and no representable lies strictly
between `truth` and `result`, the result equals `Number.upper(truth)`. -/
theorem operator_mul_rounded_branchB (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (hok : Number.operator_mul x y .to_nearest = .ok result)
    (hresult : result.mantissa_ ≠ 0)
    (h_round_up : x.toRat * y.toRat ≤ result.toRat)
    (h_no_inbetween : ∀ m : Number, m.isNormalized →
                       m.toRat < result.toRat → ¬ (x.toRat * y.toRat ≤ m.toRat)) :
    ∃ n_up : Number, Number.upper (x.toRat * y.toRat) = some n_up ∧
                     result.toRat = n_up.toRat := by
  set truth : ℚ := x.toRat * y.toRat with htruth_def
  have h_result_norm : result.isNormalized :=
    operator_mul_result_isNormalized x y result hx hy hx_mant_ne hy_mant_ne hok hresult
  have h_truth_ne : truth ≠ 0 := toRat_mul_ne_zero_of_normalized x y hx hy hx_mant_ne hy_mant_ne
  rcases lt_trichotomy truth 0 with h_truth_neg | h_truth_zero | h_truth_pos
  · set q : ℚ := -truth with hq_def
    have hq_pos : 0 < q := by rw [hq_def]; linarith
    have h_result_neg_val : result.toRat < 0 := by
      by_contra h_not
      push_neg at h_not
      have h_bound := operator_mul_rounding_bound x y result hx hy hx_mant_ne hy_mant_ne hok hresult
      rw [← htruth_def] at h_bound
      have h_truth_abs : |truth| = -truth := abs_of_neg h_truth_neg
      have h_eps_small : (5 / (2 ^ 63 + 7 : ℚ)) < 1 := by norm_num
      have h_abs_truth_pos : 0 < |truth| := abs_pos.mpr h_truth_ne
      have h_rhs_lt : |truth| * (5 / (2 ^ 63 + 7 : ℚ)) < |truth| := by
        have : |truth| * (5 / (2 ^ 63 + 7 : ℚ)) < |truth| * 1 :=
          mul_lt_mul_of_pos_left h_eps_small h_abs_truth_pos
        linarith
      have h_diff_eq : |result.toRat - truth| = result.toRat - truth := by
        rw [abs_of_nonneg (by linarith : 0 ≤ result.toRat - truth)]
      rw [h_diff_eq] at h_bound
      rw [h_truth_abs] at h_bound h_rhs_lt
      linarith
    have h_result_neg_true : result.negative_ = true := by
      by_contra hh
      have h_nn : result.negative_ = false := by
        cases hN : result.negative_
        · rfl
        · exact absurd hN hh
      have : 0 ≤ result.toRat := Number.toRat_nonneg_of_nonnegative result h_nn
      linarith
    set r_pos : Number := ({result with negative_ := false} : Number) with hr_pos_def
    have h_r_pos_toRat : r_pos.toRat = -result.toRat := by
      rw [hr_pos_def]
      exact Number.toRat_set_neg_false_of_neg result h_result_neg_true
    have h_r_pos_pos : 0 < r_pos.toRat := by rw [h_r_pos_toRat]; linarith
    have h_r_pos_norm : r_pos.isNormalized := by
      rcases h_result_norm with hz | ⟨h_rmin, h_rmax, h_rcusp, h_remin, h_remax⟩
      · exfalso; apply hresult; rw [hz]; rfl
      right; exact ⟨h_rmin, h_rmax, h_rcusp, h_remin, h_remax⟩
    have h_r_pos_le_q : r_pos.toRat ≤ q := by
      rw [hq_def, h_r_pos_toRat]; linarith
    have h_upper_exists : ∃ n_hi_pos : Number, n_hi_pos.isNormalized ∧ q ≤ n_hi_pos.toRat := by
      rcases h_r_pos_norm with hz | ⟨h_rmin, h_rmax, h_rcusp, h_remin, h_remax⟩
      · exfalso
        have h_r_pos_zero : r_pos.toRat = 0 := by rw [hz, Number.toRat_zero]
        linarith
      by_cases h_exp_lt : r_pos.exponent_ < maxExponent
      · let n_hi_pos : Number :=
          { negative_ := r_pos.negative_, mantissa_ := r_pos.mantissa_,
            exponent_ := r_pos.exponent_ + 1 }
        have h_n_hi_norm : n_hi_pos.isNormalized := by
          right
          refine ⟨h_rmin, h_rmax, h_rcusp, ?_, ?_⟩
          · change minExponent ≤ r_pos.exponent_ + 1
            linarith
          · change r_pos.exponent_ + 1 ≤ maxExponent
            linarith
        have h_n_hi_neg_false : n_hi_pos.negative_ = false := by
          change r_pos.negative_ = false
          rw [hr_pos_def]
        have h_n_hi_nn : 0 ≤ n_hi_pos.toRat :=
          Number.toRat_nonneg_of_nonnegative n_hi_pos h_n_hi_neg_false
        have h_n_hi_abs : |n_hi_pos.toRat| = (r_pos.mantissa_.toNat : ℚ) * 10 ^ (r_pos.exponent_ + 1) := by
          have := abs_toRat_eq n_hi_pos
          change |n_hi_pos.toRat| = _
          convert this using 2
        have h_r_pos_abs : |r_pos.toRat| = (r_pos.mantissa_.toNat : ℚ) * 10 ^ r_pos.exponent_ :=
          abs_toRat_eq r_pos
        have h_n_hi_eq : n_hi_pos.toRat = 10 * r_pos.toRat := by
          have habs : n_hi_pos.toRat = |n_hi_pos.toRat| := (abs_of_nonneg h_n_hi_nn).symm
          rw [habs, h_n_hi_abs]
          have h_r_pos_eq : r_pos.toRat = (r_pos.mantissa_.toNat : ℚ) * 10 ^ r_pos.exponent_ := by
            have := abs_of_pos h_r_pos_pos
            rw [← this, h_r_pos_abs]
          rw [h_r_pos_eq]
          rw [zpow_add_one₀ (by norm_num : (10 : ℚ) ≠ 0)]
          ring
        have h_bound := operator_mul_rounding_bound x y result hx hy hx_mant_ne hy_mant_ne hok hresult
        rw [← htruth_def] at h_bound
        have h_truth_abs : |truth| = -truth := abs_of_neg h_truth_neg
        have h_diff_le : |result.toRat - truth| ≤ -truth * (5 / (2 ^ 63 + 7 : ℚ)) := by
          have := h_bound; rw [h_truth_abs] at this; exact this
        have h_diff_eq : |result.toRat - truth| = result.toRat - truth := by
          rw [abs_of_nonneg (by linarith : 0 ≤ result.toRat - truth)]
        rw [h_diff_eq] at h_diff_le
        have h_q_minus_r : q - r_pos.toRat = result.toRat - truth := by
          rw [hq_def, h_r_pos_toRat]; ring
        have h_q_le_bound : q - r_pos.toRat ≤ q * (5 / (2 ^ 63 + 7 : ℚ)) := by
          rw [h_q_minus_r, hq_def] at *; linarith
        have h_eps_lt : (5 / (2 ^ 63 + 7 : ℚ)) < 9/10 := by norm_num
        have h_eps_mul : q * (5 / (2 ^ 63 + 7 : ℚ)) < q * (9/10) :=
          mul_lt_mul_of_pos_left h_eps_lt hq_pos
        have h_q_le_10_rpos : q ≤ 10 * r_pos.toRat := by linarith
        refine ⟨n_hi_pos, h_n_hi_norm, ?_⟩
        rw [h_n_hi_eq]; exact h_q_le_10_rpos
      · push_neg at h_exp_lt
        have h_exp_eq : r_pos.exponent_ = maxExponent := le_antisymm h_remax h_exp_lt
        have h_r_pos_exp : r_pos.exponent_ = result.exponent_ := by rw [hr_pos_def]
        have h_result_exp_eq : result.exponent_ = maxExponent := by rw [← h_r_pos_exp]; exact h_exp_eq
        have h_e_ge_maxE : result.exponent_ ≥ maxExponent := by rw [h_result_exp_eq]
        have h_mant_le_maxRep_64 : result.mantissa_ ≤ maxRep :=
          operator_mul_no_overflow_mantissa x y result hx hy hx_mant_ne hy_mant_ne hok hresult h_e_ge_maxE
        have h_mant_le_maxRep : result.mantissa_.toNat ≤ maxRep.toNat :=
          UInt64.le_iff_toNat_le.mp h_mant_le_maxRep_64
        have h_r_pos_mant : r_pos.mantissa_ = result.mantissa_ := by rw [hr_pos_def]
        have h_pow_max_pos : (0 : ℚ) < (10 : ℚ) ^ maxExponent := zpow_pos (by norm_num) _
        have h_r_pos_form : r_pos.toRat = (r_pos.mantissa_.toNat : ℚ) * 10^maxExponent := by
          have h_neg : r_pos.negative_ = false := by rw [hr_pos_def]
          rw [Number.toRat_of_nonneg r_pos h_neg, h_exp_eq]
        have h_r_pos_le_bound : r_pos.toRat ≤ maxRepNat * 10^maxExponent := by
          rw [h_r_pos_form, h_r_pos_mant]
          have h_maxR_val : maxRep.toNat = maxRepNat := by decide
          have : (result.mantissa_.toNat : ℚ) ≤ ((maxRep.toNat : ℕ) : ℚ) := by exact_mod_cast h_mant_le_maxRep
          have h_maxR_cast : ((maxRep.toNat : ℕ) : ℚ) = (maxRepNat : ℚ) := by
            rw [h_maxR_val]; norm_cast
          rw [h_maxR_cast] at this
          exact mul_le_mul_of_nonneg_right this (le_of_lt h_pow_max_pos)
        have h_bound := operator_mul_rounding_bound x y result hx hy hx_mant_ne hy_mant_ne hok hresult
        rw [← htruth_def] at h_bound
        have h_truth_abs : |truth| = -truth := abs_of_neg h_truth_neg
        have h_diff_le : |result.toRat - truth| ≤ -truth * (5 / (2 ^ 63 + 7 : ℚ)) := by
          have := h_bound; rw [h_truth_abs] at this; exact this
        have h_diff_eq : |result.toRat - truth| = result.toRat - truth := by
          rw [abs_of_nonneg (by linarith : 0 ≤ result.toRat - truth)]
        rw [h_diff_eq] at h_diff_le
        have h_q_minus_r : q - r_pos.toRat = result.toRat - truth := by
          rw [hq_def, h_r_pos_toRat]; ring
        have h_denom_pos : (0 : ℚ) < 2^63 + 2 := by norm_num
        have h_q_scaled : q * ((2^63 + 2 : ℚ) / (2^63 + 7)) ≤ r_pos.toRat := by
          have h_step : q - q * (5 / (2^63 + 7 : ℚ)) ≤ r_pos.toRat := by
            have : q - r_pos.toRat ≤ q * (5 / (2 ^ 63 + 7 : ℚ)) := by
              rw [h_q_minus_r, hq_def]; linarith
            linarith
          have h_eq : q - q * (5 / (2^63 + 7 : ℚ)) = q * ((2^63 + 2 : ℚ) / (2^63 + 7)) := by
            field_simp; ring
          rw [h_eq] at h_step; exact h_step
        have h_factor_pos : (0 : ℚ) < (2^63 + 2 : ℚ) / (2^63 + 7) := by apply div_pos <;> norm_num
        have h_q_le_r_pos_factor : q ≤ r_pos.toRat * ((2^63 + 7 : ℚ) / (2^63 + 2)) := by
          have h_pos : (0 : ℚ) < (2^63 + 7 : ℚ) / (2^63 + 2) := by apply div_pos <;> norm_num
          have h_inv : (2^63 + 7 : ℚ) / (2^63 + 2) = ((2^63 + 2 : ℚ) / (2^63 + 7))⁻¹ := by
            field_simp
          rw [h_inv]
          rw [le_mul_inv_iff₀ h_factor_pos]
          linarith
        have h_q_bound1 : q ≤ maxRepNat * 10^maxExponent * ((2^63 + 7 : ℚ) / (2^63 + 2)) := by
          have h_factor_pos2 : (0 : ℚ) < (2^63 + 7 : ℚ) / (2^63 + 2) := by
            apply div_pos <;> norm_num
          have := mul_le_mul_of_nonneg_right h_r_pos_le_bound (le_of_lt h_factor_pos2)
          linarith
        have h_n_hi_m_lt_64 : (maxMul10Witness : ℕ) < 2^64 := by decide
        let n_hi_pos : Number := ⟨false, ⟨maxMul10Witness, h_n_hi_m_lt_64⟩, maxExponent⟩
        have h_n_hi_toNat : ((⟨⟨⟨maxMul10Witness, h_n_hi_m_lt_64⟩⟩⟩ : UInt64)).toNat = maxMul10Witness := rfl
        have h_n_hi_norm : n_hi_pos.isNormalized := by
          right
          refine ⟨?_, ?_, ?_, ?_, ?_⟩
          · change largeRange.min ≤ (⟨⟨⟨maxMul10Witness, h_n_hi_m_lt_64⟩⟩⟩ : UInt64)
            rw [UInt64.le_iff_toNat_le, h_n_hi_toNat]
            have : largeRange.min.toNat = 1000000000000000000 := by decide
            omega
          · change (⟨⟨⟨maxMul10Witness, h_n_hi_m_lt_64⟩⟩⟩ : UInt64) ≤ largeRange.max
            rw [UInt64.le_iff_toNat_le, h_n_hi_toNat]
            have : largeRange.max.toNat = 9999999999999999999 := by decide
            omega
          · right
            change ((⟨⟨⟨maxMul10Witness, h_n_hi_m_lt_64⟩⟩⟩ : UInt64).toNat) % 10 = 0
            rw [h_n_hi_toNat]
          · change minExponent ≤ maxExponent; decide
          · change maxExponent ≤ maxExponent; rfl
        have h_n_hi_neg_false : n_hi_pos.negative_ = false := rfl
        have h_n_hi_ge : q ≤ n_hi_pos.toRat := by
          rw [Number.toRat_of_nonneg n_hi_pos h_n_hi_neg_false]
          change q ≤ ((⟨⟨⟨maxMul10Witness, h_n_hi_m_lt_64⟩⟩⟩ : UInt64).toNat : ℚ) * (10 : ℚ) ^ maxExponent
          rw [h_n_hi_toNat]
          have h_cast : ((maxMul10Witness : ℕ) : ℚ) = maxMul10Witness := by norm_cast
          rw [h_cast]
          have h_factor_bound : maxRepNat * ((2^63 + 7 : ℚ) / (2^63 + 2)) ≤ maxMul10Witness := by
            have h_denom : (0 : ℚ) < 2^63 + 2 := by norm_num
            have h_rewrite : maxRepNat * ((2^63 + 7 : ℚ) / (2^63 + 2)) = (maxRepNat * (2^63 + 7 : ℚ)) / (2^63 + 2) := by
              ring
            rw [h_rewrite, div_le_iff₀ h_denom]
            have h_2_63 : (2^63 : ℚ) = 9223372036854775808 := by norm_num
            rw [h_2_63]
            norm_num
          calc q ≤ maxRepNat * 10^maxExponent * ((2^63 + 7 : ℚ) / (2^63 + 2)) := h_q_bound1
            _ = maxRepNat * ((2^63 + 7 : ℚ) / (2^63 + 2)) * 10^maxExponent := by ring
            _ ≤ maxMul10Witness * 10^maxExponent :=
                mul_le_mul_of_nonneg_right h_factor_bound (le_of_lt h_pow_max_pos)
        exact ⟨n_hi_pos, h_n_hi_norm, h_n_hi_ge⟩
    obtain ⟨n_hi_pos, h_n_hi_norm, h_n_hi_ge⟩ := h_upper_exists
    have h_lower_exists := Number.lower_some_of_pos_witnesses q hq_pos
      r_pos h_r_pos_norm h_r_pos_pos h_r_pos_le_q
      n_hi_pos h_n_hi_norm h_n_hi_ge
    obtain ⟨n_pos, h_n_pos_eq⟩ := h_lower_exists
    have hq_not_neg : ¬ (q < 0) := not_lt.mpr (le_of_lt hq_pos)
    have hq_ne : q ≠ 0 := ne_of_gt hq_pos
    have h_lowerPosAux_eq : lowerPosAux q = some n_pos := by
      have h_unfold : Number.lower q = lowerPosAux q := by
        unfold Number.lower; rw [if_neg hq_ne, if_neg hq_not_neg]
      rw [h_unfold] at h_n_pos_eq; exact h_n_pos_eq
    have h_upper_eq : Number.upper truth = some ({n_pos with negative_ := true} : Number) := by
      rw [Number.upper_neg_eq truth h_truth_neg]
      have h_neg_truth_eq : (-truth) = q := by rw [hq_def]
      rw [h_neg_truth_eq, h_lowerPosAux_eq]
      simp only [Option.map_some]
      have h_n_pos_ne_zero : n_pos ≠ Number.zero := by
        intro h_eq
        have h_zero_toRat : n_pos.toRat = 0 := by rw [h_eq, Number.toRat_zero]
        have h_tight := Number.lower_tight q n_pos h_n_pos_eq r_pos h_r_pos_norm h_r_pos_le_q
        linarith
      rw [if_neg h_n_pos_ne_zero]
    refine ⟨({n_pos with negative_ := true} : Number), h_upper_eq, ?_⟩
    have h_max : ∀ m : Number, m.isNormalized → m.toRat ≤ q → m.toRat ≤ r_pos.toRat := by
      intro m h_norm h_m_le
      by_contra h_not
      push_neg at h_not
      by_cases h_m_pos : 0 < m.toRat
      · have h_m_neg_false : m.negative_ = false := by
          by_contra hh
          have h_mn : m.negative_ = true := by
            cases hN : m.negative_
            · exact absurd hN hh
            · rfl
          have : m.toRat ≤ 0 := Number.toRat_nonpos_of_negative m h_mn
          linarith
        set m_neg : Number := ({m with negative_ := true} : Number) with hm_neg_def
        have h_m_neg_toRat : m_neg.toRat = -m.toRat := by
          rw [hm_neg_def]; exact Number.toRat_set_neg_true_of_nn m h_m_neg_false
        have h_m_neg_norm : m_neg.isNormalized := by
          rcases h_norm with hz | ⟨hmin, hmax, hv, hemin, hemax⟩
          · exfalso
            have h_zero : m.toRat = 0 := by rw [hz, Number.toRat_zero]
            linarith
          right; exact ⟨hmin, hmax, hv, hemin, hemax⟩
        have h_m_neg_lt_result : m_neg.toRat < result.toRat := by
          rw [h_m_neg_toRat]
          have : -m.toRat < -r_pos.toRat := by linarith
          rw [h_r_pos_toRat] at this
          linarith
        have h_truth_le_m_neg : truth ≤ m_neg.toRat := by
          rw [h_m_neg_toRat]
          have hqv : q = -truth := hq_def
          linarith
        exact (h_no_inbetween m_neg h_m_neg_norm h_m_neg_lt_result) h_truth_le_m_neg
      · push_neg at h_m_pos
        linarith
    have h_r_pos_eq_n_pos : r_pos.toRat = n_pos.toRat :=
      Number.toRat_eq_lower_of_max q r_pos n_pos h_n_pos_eq h_r_pos_norm h_r_pos_le_q h_max
    have h_n_pos_neg_false : n_pos.negative_ = false := by
      unfold lowerPosAux at h_lowerPosAux_eq
      simp only at h_lowerPosAux_eq
      split at h_lowerPosAux_eq
      · split at h_lowerPosAux_eq
        · exact congrArg (·.negative_) (Option.some.inj h_lowerPosAux_eq).symm
        · split at h_lowerPosAux_eq
          · have : n_pos = Number.zero := (Option.some.inj h_lowerPosAux_eq).symm
            rw [this]; rfl
          · exact absurd h_lowerPosAux_eq (by simp)
      · split at h_lowerPosAux_eq
        · exact congrArg (·.negative_) (Option.some.inj h_lowerPosAux_eq).symm
        · split at h_lowerPosAux_eq
          · have : n_pos = Number.zero := (Option.some.inj h_lowerPosAux_eq).symm
            rw [this]; rfl
          · exact absurd h_lowerPosAux_eq (by simp)
    have h_flip_n_pos : ({n_pos with negative_ := true} : Number).toRat = -n_pos.toRat :=
      Number.toRat_set_neg_true_of_nn n_pos h_n_pos_neg_false
    rw [h_flip_n_pos]
    have h_res_eq : result.toRat = -r_pos.toRat := by rw [h_r_pos_toRat]; ring
    rw [h_res_eq, h_r_pos_eq_n_pos]
  · exact absurd h_truth_zero h_truth_ne
  · have h_sign := toRat_mul_sign x y
    have h_xy_same_sign : x.negative_ = y.negative_ := by
      by_contra h_diff
      have : truth ≤ 0 := h_sign.2 h_diff
      linarith
    have h_result_pos : 0 < result.toRat := lt_of_lt_of_le h_truth_pos h_round_up
    have h_result_neg_false : result.negative_ = false := by
      by_contra hh
      have hh' : result.negative_ = true := by
        cases hN : result.negative_
        · exact absurd hN hh
        · rfl
      have : result.toRat ≤ 0 := Number.toRat_nonpos_of_negative result hh'
      linarith
    rcases h_result_norm with hz | ⟨h_rmin, h_rmax, h_rcusp, h_remin, h_remax⟩
    · exfalso; apply hresult; rw [hz]; rfl
    set smallest_pos_rep : ℚ := (largeRange.min.toNat : ℚ) * (10 : ℚ) ^ minExponent
        with hspr_def
    have h_spr_eq : smallest_pos_rep = (10 : ℚ)^18 * (10 : ℚ) ^ minExponent := by
      rw [hspr_def]
      have h_min_eq : largeRange.min.toNat = 10^18 := by decide
      rw [h_min_eq]; norm_cast
    have h_spr_pos : 0 < smallest_pos_rep := by
      rw [hspr_def]
      have h_min_eq : largeRange.min.toNat = 10^18 := by decide
      have : (0 : ℚ) < (largeRange.min.toNat : ℚ) := by rw [h_min_eq]; norm_num
      have h_pow_pos : (0 : ℚ) < (10 : ℚ) ^ minExponent := zpow_pos (by norm_num) _
      positivity
    by_cases h_exp_gt : minExponent < result.exponent_
    · have h_lower_exists : ∃ n_lo : Number, n_lo.isNormalized ∧
                            0 < n_lo.toRat ∧ n_lo.toRat ≤ truth := by
        let n_lo : Number :=
          { negative_ := result.negative_, mantissa_ := result.mantissa_,
            exponent_ := result.exponent_ - 1 }
        have h_n_lo_norm : n_lo.isNormalized := by
          right
          refine ⟨h_rmin, h_rmax, h_rcusp, ?_, ?_⟩
          · change minExponent ≤ result.exponent_ - 1
            linarith
          · change result.exponent_ - 1 ≤ maxExponent
            linarith
        have h_n_lo_abs : |n_lo.toRat| = (result.mantissa_.toNat : ℚ) * 10 ^ (result.exponent_ - 1) := by
          have := abs_toRat_eq n_lo
          change |n_lo.toRat| = _
          convert this using 2
        have h_result_abs : |result.toRat| = (result.mantissa_.toNat : ℚ) * 10 ^ result.exponent_ :=
          abs_toRat_eq result
        have h_n_lo_neg_false : n_lo.negative_ = false := h_result_neg_false
        have h_n_lo_nn : 0 ≤ n_lo.toRat := Number.toRat_nonneg_of_nonnegative n_lo h_n_lo_neg_false
        have h_n_lo_eq : n_lo.toRat = result.toRat / 10 := by
          have habs : n_lo.toRat = |n_lo.toRat| := (abs_of_nonneg h_n_lo_nn).symm
          rw [habs, h_n_lo_abs]
          have h_result_eq : result.toRat = (result.mantissa_.toNat : ℚ) * 10 ^ result.exponent_ := by
            have := abs_of_pos h_result_pos
            rw [← this, h_result_abs]
          rw [h_result_eq]
          have h10_pos : (0 : ℚ) < 10 := by norm_num
          have h_exp_split : (10 : ℚ) ^ result.exponent_ = (10 : ℚ) ^ (result.exponent_ - 1) * 10 := by
            rw [show result.exponent_ = (result.exponent_ - 1) + 1 from by ring,
                zpow_add_one₀ (by norm_num : (10 : ℚ) ≠ 0)]
            ring_nf
          rw [h_exp_split]
          field_simp
        have h_n_lo_pos : 0 < n_lo.toRat := by
          rw [h_n_lo_eq]
          positivity
        have h_bound := operator_mul_rounding_bound x y result hx hy hx_mant_ne hy_mant_ne hok hresult
        rw [← htruth_def] at h_bound
        have h_truth_abs : |truth| = truth := abs_of_pos h_truth_pos
        have h_diff_le : |result.toRat - truth| ≤ truth * (5 / (2 ^ 63 + 7 : ℚ)) := by
          have := h_bound; rw [h_truth_abs] at this; exact this
        have h_diff_eq : |result.toRat - truth| = result.toRat - truth := by
          rw [abs_of_nonneg (by linarith : 0 ≤ result.toRat - truth)]
        rw [h_diff_eq] at h_diff_le
        have h_eps_lt : (5 / (2 ^ 63 + 7 : ℚ)) < 9 := by norm_num
        have h_eps_mul : truth * (5 / (2 ^ 63 + 7 : ℚ)) < truth * 9 :=
          mul_lt_mul_of_pos_left h_eps_lt h_truth_pos
        have h_n_lo_le : n_lo.toRat ≤ truth := by
          rw [h_n_lo_eq]
          have h_result_le : result.toRat ≤ truth + truth * 9 := by linarith
          have h_result_le' : result.toRat ≤ 10 * truth := by linarith
          linarith
        exact ⟨n_lo, h_n_lo_norm, h_n_lo_pos, h_n_lo_le⟩
      obtain ⟨n_lo, h_lo_norm, h_lo_pos, h_lo_le⟩ := h_lower_exists
      have h_upper_exists := Number.upper_some_of_pos_witnesses truth h_truth_pos
        n_lo h_lo_norm h_lo_pos h_lo_le
        result (by right; exact ⟨h_rmin, h_rmax, h_rcusp, h_remin, h_remax⟩) h_round_up
      obtain ⟨n_up, h_up_eq⟩ := h_upper_exists
      refine ⟨n_up, h_up_eq, ?_⟩
      have h_min : ∀ m : Number, m.isNormalized → truth ≤ m.toRat → result.toRat ≤ m.toRat := by
        intro m h_norm h_m_ge
        by_contra h_not
        push_neg at h_not
        exact (h_no_inbetween m h_norm h_not) h_m_ge
      exact Number.toRat_eq_upper_of_min truth result n_up h_up_eq
        (by right; exact ⟨h_rmin, h_rmax, h_rcusp, h_remin, h_remax⟩) h_round_up h_min
    · push_neg at h_exp_gt
      have h_exp_eq : result.exponent_ = minExponent := le_antisymm h_exp_gt h_remin
      have h_bound := operator_mul_rounding_bound x y result hx hy hx_mant_ne hy_mant_ne hok hresult
      rw [← htruth_def] at h_bound
      have h_truth_abs : |truth| = truth := abs_of_pos h_truth_pos
      have h_diff_le : |result.toRat - truth| ≤ truth * (5 / (2 ^ 63 + 7 : ℚ)) := by
        have := h_bound; rw [h_truth_abs] at this; exact this
      have h_diff_eq : |result.toRat - truth| = result.toRat - truth := by
        rw [abs_of_nonneg (by linarith : 0 ≤ result.toRat - truth)]
      rw [h_diff_eq] at h_diff_le
      have h_pow_min_pos : (0 : ℚ) < (10 : ℚ) ^ minExponent := zpow_pos (by norm_num) _
      have h_result_eq_form :
          result.toRat = (result.mantissa_.toNat : ℚ) * (10 : ℚ) ^ minExponent := by
        rw [Number.toRat_of_nonneg result h_result_neg_false, h_exp_eq]
      have h_result_ge_spr : smallest_pos_rep ≤ result.toRat := by
        rw [h_result_eq_form, hspr_def]
        have h_min_nat : largeRange.min.toNat ≤ result.mantissa_.toNat :=
          UInt64.le_iff_toNat_le.mp h_rmin
        have h_cast : (largeRange.min.toNat : ℚ) ≤ (result.mantissa_.toNat : ℚ) := by
          exact_mod_cast h_min_nat
        exact mul_le_mul_of_nonneg_right h_cast (le_of_lt h_pow_min_pos)
      by_cases h_truth_ge : smallest_pos_rep ≤ truth
      · have h_lower_exists : ∃ n_lo : Number, n_lo.isNormalized ∧
                              0 < n_lo.toRat ∧ n_lo.toRat ≤ truth := by
          let n_lo : Number :=
            { negative_ := false, mantissa_ := largeRange.min, exponent_ := minExponent }
          have h_n_lo_norm : n_lo.isNormalized := by
            right
            refine ⟨?_, ?_, ?_, ?_, ?_⟩
            · change largeRange.min ≤ largeRange.min
              rw [UInt64.le_iff_toNat_le]
            · change largeRange.min ≤ largeRange.max
              rw [UInt64.le_iff_toNat_le]
              have hmin_eq : largeRange.min.toNat = 1000000000000000000 := by decide
              have hmax_eq : largeRange.max.toNat = 9999999999999999999 := by decide
              omega
            · right
              change largeRange.min.toNat % 10 = 0
              decide
            · change minExponent ≤ minExponent; rfl
            · change minExponent ≤ maxExponent; decide
          have h_n_lo_neg_false : n_lo.negative_ = false := rfl
          have h_n_lo_eq : n_lo.toRat = smallest_pos_rep := by
            rw [Number.toRat_of_nonneg n_lo h_n_lo_neg_false]
          have h_n_lo_pos : 0 < n_lo.toRat := by rw [h_n_lo_eq]; exact h_spr_pos
          have h_n_lo_le : n_lo.toRat ≤ truth := by rw [h_n_lo_eq]; exact h_truth_ge
          exact ⟨n_lo, h_n_lo_norm, h_n_lo_pos, h_n_lo_le⟩
        obtain ⟨n_lo, h_lo_norm, h_lo_pos, h_lo_le⟩ := h_lower_exists
        have h_upper_exists := Number.upper_some_of_pos_witnesses truth h_truth_pos
          n_lo h_lo_norm h_lo_pos h_lo_le
          result (by right; exact ⟨h_rmin, h_rmax, h_rcusp, h_remin, h_remax⟩) h_round_up
        obtain ⟨n_up, h_up_eq⟩ := h_upper_exists
        refine ⟨n_up, h_up_eq, ?_⟩
        have h_min : ∀ m : Number, m.isNormalized → truth ≤ m.toRat → result.toRat ≤ m.toRat := by
          intro m h_norm h_m_ge
          by_contra h_not
          push_neg at h_not
          exact (h_no_inbetween m h_norm h_not) h_m_ge
        exact Number.toRat_eq_upper_of_min truth result n_up h_up_eq
          (by right; exact ⟨h_rmin, h_rmax, h_rcusp, h_remin, h_remax⟩) h_round_up h_min
      · -- truth < smallest_pos_rep: result.mantissa_ is forced to 10^18.
        push_neg at h_truth_ge
        have h_eps_pos : (0 : ℚ) < 5 / (2^63 + 7 : ℚ) := by norm_num
        have h_result_le : result.toRat ≤ truth + truth * (5 / (2^63 + 7 : ℚ)) := by linarith
        have h_truth_eps_lt : truth * (5 / (2^63 + 7 : ℚ)) < smallest_pos_rep * (5 / (2^63 + 7 : ℚ)) :=
          mul_lt_mul_of_pos_right h_truth_ge h_eps_pos
        have h_result_lt_bound : result.toRat < smallest_pos_rep + smallest_pos_rep * (5 / (2^63 + 7 : ℚ)) := by
          linarith
        have h_bound_mant : (result.mantissa_.toNat : ℚ) <
            (10 : ℚ)^18 + (10 : ℚ)^18 * (5 / (2^63 + 7 : ℚ)) := by
          have h1 : (result.mantissa_.toNat : ℚ) * (10 : ℚ) ^ minExponent <
              ((10 : ℚ)^18 + (10 : ℚ)^18 * (5 / (2^63 + 7 : ℚ))) * (10 : ℚ) ^ minExponent := by
            calc (result.mantissa_.toNat : ℚ) * (10 : ℚ) ^ minExponent
                = result.toRat := h_result_eq_form.symm
              _ < smallest_pos_rep + smallest_pos_rep * (5 / (2^63 + 7 : ℚ)) := h_result_lt_bound
              _ = ((10 : ℚ)^18 + (10 : ℚ)^18 * (5 / (2^63 + 7 : ℚ))) * (10 : ℚ) ^ minExponent := by
                  rw [h_spr_eq]; ring
          exact lt_of_mul_lt_mul_of_nonneg_right h1 (le_of_lt h_pow_min_pos)
        have h_rhs_lt : (10 : ℚ)^18 + (10 : ℚ)^18 * (5 / (2^63 + 7 : ℚ)) < (10 : ℚ)^18 + 1 := by
          have h_denom_pos : (0 : ℚ) < 2^63 + 7 := by norm_num
          have h_eps_small : (10 : ℚ)^18 * (5 / (2^63 + 7 : ℚ)) < 1 := by
            rw [mul_div_assoc']
            rw [div_lt_iff₀ h_denom_pos]
            norm_num
          linarith
        have h_mant_lt : (result.mantissa_.toNat : ℚ) < (10 : ℚ)^18 + 1 := by linarith
        have h_mant_le_pow18 : result.mantissa_.toNat ≤ 10^18 := by
          have h_lt_nat : (result.mantissa_.toNat : ℚ) < ((10^18 + 1 : ℕ) : ℚ) := by
            push_cast; linarith
          have : result.mantissa_.toNat < 10^18 + 1 := by exact_mod_cast h_lt_nat
          omega
        have h_mant_ge_pow18 : 10^18 ≤ result.mantissa_.toNat := by
          have h_min_nat : largeRange.min.toNat ≤ result.mantissa_.toNat :=
            UInt64.le_iff_toNat_le.mp h_rmin
          have h_min_eq : largeRange.min.toNat = 10^18 := by decide
          omega
        have h_mant_eq_pow18 : result.mantissa_.toNat = 10^18 := by omega
        have h_mant_eq_min : result.mantissa_.toNat = largeRange.min.toNat := by
          have h_min_eq : largeRange.min.toNat = 10^18 := by decide
          omega
        have h_result_eq_spr : result.toRat = smallest_pos_rep := by
          rw [h_result_eq_form, hspr_def]
          congr 1
          exact_mod_cast h_mant_eq_min
        have h_truth_ne : truth ≠ 0 := ne_of_gt h_truth_pos
        have h_truth_not_neg : ¬ (truth < 0) := not_lt.mpr (le_of_lt h_truth_pos)
        have h_upper_unfold : Number.upper truth = upperPosAux truth := by
          unfold Number.upper; rw [if_neg h_truth_ne, if_neg h_truth_not_neg]
        have h_truth_lt_pow : truth < (10 : ℚ) ^ (minExponent + 18) := by
          have h_pow_form : smallest_pos_rep = (10 : ℚ) ^ (minExponent + 18) := by
            rw [h_spr_eq]
            rw [show (minExponent + 18 : ℤ) = 18 + minExponent from by ring,
                zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0)]
            congr 1
          rw [← h_pow_form]; exact h_truth_ge
        have h_e_q_lt_min : Int.log 10 truth - mantissaLog < minExponent := by
          by_contra h_not
          push_neg at h_not
          have h_log_ge : minExponent + 18 ≤ Int.log 10 truth := by linarith
          have h_pow_mono : (10 : ℚ) ^ (minExponent + 18) ≤ (10 : ℚ) ^ (Int.log 10 truth) :=
            zpow_le_zpow_right₀ (by norm_num) h_log_ge
          have h_log_le : (10 : ℚ) ^ (Int.log 10 truth) ≤ truth :=
            Int.zpow_log_le_self (by norm_num : (1 : ℕ) < 10) h_truth_pos
          have h_chain : (10 : ℚ) ^ (minExponent + 18) ≤ truth := le_trans h_pow_mono h_log_le
          linarith
        let n_up_target : Number := ⟨false, largeRange.min, minExponent⟩
        have h_n_up_target_toRat : n_up_target.toRat = smallest_pos_rep := by
          have h_neg : n_up_target.negative_ = false := rfl
          rw [Number.toRat_of_nonneg n_up_target h_neg]
        have h_upperPos_eq : upperPosAux truth = some n_up_target := by
          unfold upperPosAux
          simp only
          set e_q : ℤ := Int.log 10 truth - mantissaLog
          set m_real : ℚ := truth * (10 : ℚ)^(-e_q)
          set m_ceil_nat : ℕ := ⌈m_real⌉₊
          have h_e_q_lt_min' : e_q < minExponent := h_e_q_lt_min
          rcases h_bump : bumpToValidMantissa m_ceil_nat with _ | m_b
          · change (if h_exp : minExponent ≤ e_q + 1 ∧ e_q + 1 ≤ maxExponent then
                    some (⟨false, largeRange.min, e_q + 1⟩ : Number)
                  else if e_q + 1 < minExponent then
                    some (⟨false, largeRange.min, minExponent⟩ : Number)
                  else none) = some n_up_target
            have h_e_q1_le_min : e_q + 1 ≤ minExponent := by linarith
            by_cases h_dif : minExponent ≤ e_q + 1 ∧ e_q + 1 ≤ maxExponent
            · have h_e_q1_eq : e_q + 1 = minExponent := le_antisymm h_e_q1_le_min h_dif.1
              rw [dif_pos h_dif]
              change (some (⟨false, largeRange.min, e_q + 1⟩ : Number) : Option Number) = some n_up_target
              rw [h_e_q1_eq]
            · rw [dif_neg h_dif]
              have h_e_q1_lt : e_q + 1 < minExponent := by
                rcases lt_or_eq_of_le h_e_q1_le_min with h | h
                · exact h
                · exfalso; apply h_dif
                  refine ⟨h.ge, ?_⟩
                  have h_min_eq : minExponent = -32768 := rfl
                  have h_max_eq : maxExponent = 32768 := rfl
                  omega
              rw [if_pos h_e_q1_lt]
          · change (if h_exp : minExponent ≤ e_q ∧ e_q ≤ maxExponent ∧ m_b < 2^64 then
                    some (⟨false, ⟨m_b, h_exp.2.2⟩, e_q⟩ : Number)
                  else if e_q < minExponent then
                    some (⟨false, largeRange.min, minExponent⟩ : Number)
                  else none) = some n_up_target
            have h_not_in : ¬ (minExponent ≤ e_q ∧ e_q ≤ maxExponent ∧ m_b < 2^64) := by
              intro ⟨h1, _, _⟩; linarith
            rw [dif_neg h_not_in]
            rw [if_pos h_e_q_lt_min']
        have h_upper_eq_target : Number.upper truth = some n_up_target := by
          rw [h_upper_unfold]; exact h_upperPos_eq
        refine ⟨n_up_target, h_upper_eq_target, ?_⟩
        rw [h_result_eq_spr, h_n_up_target_toRat]

/-! ## Branch C: cusp case (round-up to mantissa above maxRep) — result = upper(truth) -/

/-- Cusp case. Same conclusion as branchB; the cusp distinction matters only at the
algorithm level, not at the discrete-grid statement level. -/
theorem operator_mul_rounded_branchC (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (hok : Number.operator_mul x y .to_nearest = .ok result)
    (hresult : result.mantissa_ ≠ 0)
    (h_round_up : x.toRat * y.toRat ≤ result.toRat)
    (h_no_inbetween : ∀ m : Number, m.isNormalized →
                       m.toRat < result.toRat → ¬ (x.toRat * y.toRat ≤ m.toRat)) :
    ∃ n_up : Number, Number.upper (x.toRat * y.toRat) = some n_up ∧
                     result.toRat = n_up.toRat :=
  operator_mul_rounded_branchB x y result hx hy hx_mant_ne hy_mant_ne hok hresult
    h_round_up h_no_inbetween

/-! ## Main theorem: `operator_mul_rounded` -/

/-- The result of `operator_mul` under `.to_nearest` is either `Number.lower(truth)`
or `Number.upper(truth)`. -/
theorem operator_mul_rounded (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (hok : Number.operator_mul x y .to_nearest = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    Number.RoundsDiscrete result (x.toRat * y.toRat) .to_nearest := by
  set truth : ℚ := x.toRat * y.toRat with htruth_def
  by_cases h_le : result.toRat ≤ truth
  · have h_no_inbetween : ∀ m : Number, m.isNormalized →
                          result.toRat < m.toRat → ¬ (m.toRat ≤ truth) := by
      apply operator_mul_no_inbetween_below x y result hx hy hx_mant_ne hy_mant_ne hok hresult h_le
    have h_branchA := operator_mul_rounded_branchA x y result hx hy hx_mant_ne hy_mant_ne
      hok hresult h_le h_no_inbetween
    obtain ⟨n_lo, h_lo_eq, h_lo_toRat⟩ := h_branchA
    left
    exact ⟨n_lo, h_lo_eq, h_lo_toRat⟩
  · push_neg at h_le
    have h_ge : truth ≤ result.toRat := le_of_lt h_le
    have h_no_inbetween : ∀ m : Number, m.isNormalized →
                          m.toRat < result.toRat → ¬ (truth ≤ m.toRat) := by
      apply operator_mul_no_inbetween_above x y result hx hy hx_mant_ne hy_mant_ne hok hresult h_ge
    have h_branchB := operator_mul_rounded_branchB x y result hx hy hx_mant_ne hy_mant_ne
      hok hresult h_ge h_no_inbetween
    obtain ⟨n_up, h_up_eq, h_up_toRat⟩ := h_branchB
    right
    exact ⟨n_up, h_up_eq, h_up_toRat⟩

end XRPL.Model.Protocol
