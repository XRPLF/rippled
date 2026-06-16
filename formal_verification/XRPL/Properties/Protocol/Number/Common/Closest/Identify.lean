import XRPL.Properties.Protocol.Number.Common.Notation
import XRPL.Properties.Protocol.Number.Common.ProofTactics
import Mathlib.Tactic

import XRPL.Properties.Protocol.Number.Common.Helpers
import XRPL.Properties.Protocol.Number.Common.Closest.Defs
import XRPL.Properties.Protocol.Number.Common.Closest.Existence
import XRPL.Properties.Protocol.Number.Common.Closest.Helpers
import XRPL.Properties.Protocol.Number.Common.Closest.Bounds
import XRPL.Properties.Protocol.Number.Common.Closest.Tightness


namespace XRPL.Model.Protocol

/-! # Closest-grid identification from direction + no-inbetween

`closest_lower_of_no_inbetween` / `closest_upper_of_no_inbetween` convert
"the result is on this side of the truth and nothing representable lies
strictly between" into the discrete claims `result = Number.lower truth` /
`Number.upper truth`. They are shared by every operation's `Rounded.lean`
(the operation enters only through `result.isNormalized`, a uniform relative
error bound, and the overflow-corner truth bound). -/

/-- If `result ≤ truth` and no representable lies strictly between them,
`result` equals `Number.lower truth`. Generic over the producing operation:
the operation enters only through normalization of `result`, the uniform
relative bound `h_bound`, and the overflow-corner truth bound `h_truth_top`. -/
theorem closest_lower_of_no_inbetween (result : Number) (truth : ℚ)
    (h_result_norm : result.isNormalized)
    (hresult : result.mantissa_ ≠ 0)
    (h_truth_ne : truth ≠ 0)
    (h_bound : |result.toRat - truth| ≤ |truth| * (11 / (2 ^ 63 - 18 : ℚ)))
    (h_truth_top : result.exponent_ ≥ maxExponent → |truth| < 10 ^ 19 * (10 : ℚ) ^ (maxExponent : ℤ))
    (h_round_down : result.toRat ≤ truth)
    (h_no_inbetween : ∀ m : Number, m.isNormalized →
                       result.toRat < m.toRat → ¬ (m.toRat ≤ truth)) :
    ∃ n_lo : Number, Number.lower truth = some n_lo ∧
                     result.toRat = n_lo.toRat := by
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
        have h_truth_abs : |truth| = -truth := abs_of_neg h_truth_neg
        have h_diff_le : |result.toRat - truth| ≤ -truth * (11 / (2 ^ 63 - 18 : ℚ)) := by
          have := h_bound; rw [h_truth_abs] at this; exact this
        have h_diff_eq : |result.toRat - truth| = truth - result.toRat := by
          rw [abs_of_nonpos (by linarith : result.toRat - truth ≤ 0)]; ring
        rw [h_diff_eq] at h_diff_le
        have h_r_pos_le_q : r_pos.toRat ≤ q + q * (11 / (2 ^ 63 - 18 : ℚ)) := by
          have h3 : r_pos.toRat - q = truth - result.toRat := by
            rw [h_r_pos_toRat, hq_def]; ring
          rw [show q + q * (11 / (2 ^ 63 - 18 : ℚ)) = q + (-truth) * (11 / (2 ^ 63 - 18 : ℚ)) from by rw [hq_def]]
          linarith
        have h_n_lo_le_q : n_lo_pos.toRat ≤ q := by
          rw [h_n_lo_eq]
          have h_eps_small : (11 / (2 ^ 63 - 18 : ℚ)) < 9 := by norm_num
          have h_mul_lt : q * (11 / (2 ^ 63 - 18 : ℚ)) < q * 9 :=
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
      have h_truth_abs : |truth| = -truth := abs_of_neg h_truth_neg
      have h_diff_le : |result.toRat - truth| ≤ -truth * (11 / (2 ^ 63 - 18 : ℚ)) := by
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
      have h_r_pos_le_q_plus : r_pos.toRat ≤ q + q * (11 / (2 ^ 63 - 18 : ℚ)) := by
        have h_step : truth - result.toRat ≤ q * (11 / (2 ^ 63 - 18 : ℚ)) := by
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
              have hmin_eq : largeRange.min.toNat = 1000000000000000000 := largeRange_min_val
              have hmax_eq : largeRange.max.toNat = 9999999999999999999 := largeRange_max_val
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
        -- Coarse ε can't separate 10^18 from 10^18+1; exclude the larger
        -- mantissa via no-inbetween against −smallest_pos_rep.
        have h_mant_le_pow18 : r_pos.mantissa_.toNat ≤ 10^18 := by
          by_contra h_gt
          push_neg at h_gt
          have h_mant_ge_q : ((10 : ℚ)^18 + 1) ≤ (r_pos.mantissa_.toNat : ℚ) := by
            have h1 : (10^18 + 1 : ℕ) ≤ r_pos.mantissa_.toNat := h_gt
            exact_mod_cast h1
          have h_r_pos_gt_spr : smallest_pos_rep < r_pos.toRat := by
            rw [h_r_pos_eq_form, h_spr_eq]
            nlinarith [h_pow_min_pos, h_mant_ge_q]
          have h_m₀_norm : (⟨true, largeRange.min, minExponent⟩ : Number).isNormalized := by
            norm_isNormalized
          have h_m₀_toRat : (⟨true, largeRange.min, minExponent⟩ : Number).toRat
              = -smallest_pos_rep := by
            rw [Number.toRat_of_neg _ rfl, hspr_def]
          exact h_no_inbetween ⟨true, largeRange.min, minExponent⟩ h_m₀_norm
            (by rw [h_m₀_toRat]
                have h_res : result.toRat = -r_pos.toRat := by rw [h_r_pos_toRat]; ring
                rw [h_res]; linarith)
            (by rw [h_m₀_toRat]
                have h_tr : truth = -q := by rw [hq_def]; ring
                rw [h_tr]; linarith [le_of_lt h_q_ge])
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
  · have h_result_pos : 0 < result.toRat := by
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
      have h_abs_truth_pos : 0 < |truth| := abs_pos.mpr h_truth_ne
      have h_eps_small : (11 / (2 ^ 63 - 18 : ℚ)) < 1 := by norm_num
      have h_rhs_lt : |truth| * (11 / (2 ^ 63 - 18 : ℚ)) < |truth| := by
        have : |truth| * (11 / (2 ^ 63 - 18 : ℚ)) < |truth| * 1 :=
          mul_lt_mul_of_pos_left h_eps_small h_abs_truth_pos
        linarith
      have h_truth_abs : |truth| = truth := abs_of_pos h_truth_pos
      have h_diff_eq : |result.toRat - truth| = truth - result.toRat := by
        rw [abs_of_nonpos (by linarith : result.toRat - truth ≤ 0)]
        ring
      rw [h_diff_eq] at h_bound
      rw [h_truth_abs] at h_bound h_rhs_lt
      linarith
    have h_truth_lt_top : truth < 10 ^ 19 * (10 : ℚ) ^ (maxExponent : ℤ) := by
      by_cases h_exp_lt : result.exponent_ < maxExponent
      · -- Below the top decade: result ≤ (10^19 − 1)·10^(maxE−1) and truth ≤ 2·result.
        have h_result_bounds := h_result_norm.mantissaBounds hresult
        have h_mant_le : (result.mantissa_.toNat : ℚ) ≤ 10 ^ 19 - 1 := by
          have h_le : result.mantissa_.toNat ≤ largeRange.max.toNat :=
            UInt64.le_iff_toNat_le.mp h_result_bounds.2
          have h_max_v : largeRange.max.toNat = 9999999999999999999 := largeRange_max_val
          rw [h_max_v] at h_le
          have h_cast : (result.mantissa_.toNat : ℚ) ≤ (9999999999999999999 : ℚ) := by
            exact_mod_cast h_le
          linarith
        have h_result_abs : |result.toRat| = (result.mantissa_.toNat : ℚ) * 10 ^ result.exponent_ :=
          abs_toRat_eq result
        have h_exp_le : result.exponent_ ≤ maxExponent - 1 := by omega
        have h_pow_le : (10 : ℚ) ^ result.exponent_ ≤ (10 : ℚ) ^ (maxExponent - 1 : ℤ) :=
          zpow_le_zpow_right₀ (by norm_num) h_exp_le
        have h_pow_pos_e : (0 : ℚ) < (10 : ℚ) ^ result.exponent_ := zpow_pos (by norm_num) _
        have h_pow_pos_m : (0 : ℚ) < (10 : ℚ) ^ (maxExponent - 1 : ℤ) := zpow_pos (by norm_num) _
        have h_result_le : result.toRat ≤ (10 ^ 19 - 1 : ℚ) * (10 : ℚ) ^ (maxExponent - 1 : ℤ) := by
          have h_eq : result.toRat = |result.toRat| := (abs_of_pos h_result_pos).symm
          rw [h_eq, h_result_abs]
          calc (result.mantissa_.toNat : ℚ) * 10 ^ result.exponent_
              ≤ (10 ^ 19 - 1 : ℚ) * 10 ^ result.exponent_ :=
                mul_le_mul_of_nonneg_right h_mant_le (le_of_lt h_pow_pos_e)
            _ ≤ (10 ^ 19 - 1 : ℚ) * (10 : ℚ) ^ (maxExponent - 1 : ℤ) :=
                mul_le_mul_of_nonneg_left h_pow_le (by norm_num)
        have h_truth_abs : |truth| = truth := abs_of_pos h_truth_pos
        have h_diff_le : |result.toRat - truth| ≤ truth * (11 / (2 ^ 63 - 18 : ℚ)) := by
          have := h_bound; rw [h_truth_abs] at this; exact this
        have h_diff_lower : truth - result.toRat ≤ |result.toRat - truth| := by
          rw [abs_sub_comm]; exact le_abs_self _
        have h_eps_half : (11 / (2 ^ 63 - 18 : ℚ)) ≤ 1 / 2 := by norm_num
        have h_scaled : truth * (11 / (2 ^ 63 - 18 : ℚ)) ≤ truth * (1 / 2) :=
          mul_le_mul_of_nonneg_left h_eps_half (le_of_lt h_truth_pos)
        have h_truth_le_2r : truth ≤ 2 * result.toRat := by linarith
        have h_pow_split : (10 : ℚ) ^ (maxExponent : ℤ) = 10 * (10 : ℚ) ^ (maxExponent - 1 : ℤ) := by
          rw [show (maxExponent : ℤ) = (maxExponent - 1) + 1 from by ring,
              zpow_add_one₀ (by norm_num : (10 : ℚ) ≠ 0)]
          exact mul_comm _ _
        rw [h_pow_split]
        nlinarith [h_pow_pos_m]
      · push_neg at h_exp_lt
        have h := h_truth_top h_exp_lt
        rwa [abs_of_pos h_truth_pos] at h
    obtain ⟨n_lo, h_lo_aux⟩ := lowerPosAux_isSome_of_lt_top truth h_truth_pos h_truth_lt_top
    have h_lo_eq : Number.lower truth = some n_lo := by
      unfold Number.lower
      rw [if_neg (ne_of_gt h_truth_pos), if_neg (not_lt.mpr (le_of_lt h_truth_pos))]
      exact h_lo_aux
    refine ⟨n_lo, h_lo_eq, ?_⟩
    have h_max : ∀ m : Number, m.isNormalized → m.toRat ≤ truth → m.toRat ≤ result.toRat := by
      intro m h_norm h_m_le
      by_contra h_not
      push_neg at h_not
      exact (h_no_inbetween m h_norm h_not) h_m_le
    exact Number.toRat_eq_lower_of_max truth result n_lo h_lo_eq
      h_result_norm h_round_down h_max
/-- If `truth ≤ result` and no representable lies strictly between them,
`result` equals `Number.upper truth`. Generic counterpart of
`closest_lower_of_no_inbetween`. -/
theorem closest_upper_of_no_inbetween (result : Number) (truth : ℚ)
    (h_result_norm : result.isNormalized)
    (hresult : result.mantissa_ ≠ 0)
    (h_truth_ne : truth ≠ 0)
    (h_bound : |result.toRat - truth| ≤ |truth| * (11 / (2 ^ 63 - 18 : ℚ)))
    (h_truth_top : result.exponent_ ≥ maxExponent → |truth| < 10 ^ 19 * (10 : ℚ) ^ (maxExponent : ℤ))
    (h_round_up : truth ≤ result.toRat)
    (h_no_inbetween : ∀ m : Number, m.isNormalized →
                       m.toRat < result.toRat → ¬ (truth ≤ m.toRat)) :
    ∃ n_up : Number, Number.upper truth = some n_up ∧
                     result.toRat = n_up.toRat := by
  rcases lt_trichotomy truth 0 with h_truth_neg | h_truth_zero | h_truth_pos
  · set q : ℚ := -truth with hq_def
    have hq_pos : 0 < q := by rw [hq_def]; linarith
    have h_result_neg_val : result.toRat < 0 := by
      by_contra h_not
      push_neg at h_not
      have h_truth_abs : |truth| = -truth := abs_of_neg h_truth_neg
      have h_eps_small : (11 / (2 ^ 63 - 18 : ℚ)) < 1 := by norm_num
      have h_abs_truth_pos : 0 < |truth| := abs_pos.mpr h_truth_ne
      have h_rhs_lt : |truth| * (11 / (2 ^ 63 - 18 : ℚ)) < |truth| := by
        have : |truth| * (11 / (2 ^ 63 - 18 : ℚ)) < |truth| * 1 :=
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
    have h_q_lt_top : q < 10 ^ 19 * (10 : ℚ) ^ (maxExponent : ℤ) := by
      by_cases h_exp_lt : r_pos.exponent_ < maxExponent
      · -- Below the top decade: r_pos ≤ (10^19 − 1)·10^(maxE−1) and q ≤ 2·r_pos.
        have h_r_pos_mant_eq : r_pos.mantissa_ = result.mantissa_ := by rw [hr_pos_def]
        have h_r_mant_ne : r_pos.mantissa_ ≠ 0 := by rw [h_r_pos_mant_eq]; exact hresult
        have h_r_bounds := h_r_pos_norm.mantissaBounds h_r_mant_ne
        have h_mant_le : (r_pos.mantissa_.toNat : ℚ) ≤ 10 ^ 19 - 1 := by
          have h_le : r_pos.mantissa_.toNat ≤ largeRange.max.toNat :=
            UInt64.le_iff_toNat_le.mp h_r_bounds.2
          have h_max_v : largeRange.max.toNat = 9999999999999999999 := largeRange_max_val
          rw [h_max_v] at h_le
          have h_cast : (r_pos.mantissa_.toNat : ℚ) ≤ (9999999999999999999 : ℚ) := by
            exact_mod_cast h_le
          linarith
        have h_r_abs : |r_pos.toRat| = (r_pos.mantissa_.toNat : ℚ) * 10 ^ r_pos.exponent_ :=
          abs_toRat_eq r_pos
        have h_exp_le : r_pos.exponent_ ≤ maxExponent - 1 := by omega
        have h_pow_le : (10 : ℚ) ^ r_pos.exponent_ ≤ (10 : ℚ) ^ (maxExponent - 1 : ℤ) :=
          zpow_le_zpow_right₀ (by norm_num) h_exp_le
        have h_pow_pos_e : (0 : ℚ) < (10 : ℚ) ^ r_pos.exponent_ := zpow_pos (by norm_num) _
        have h_pow_pos_m : (0 : ℚ) < (10 : ℚ) ^ (maxExponent - 1 : ℤ) := zpow_pos (by norm_num) _
        have h_r_le : r_pos.toRat ≤ (10 ^ 19 - 1 : ℚ) * (10 : ℚ) ^ (maxExponent - 1 : ℤ) := by
          have h_eq : r_pos.toRat = |r_pos.toRat| := (abs_of_pos h_r_pos_pos).symm
          rw [h_eq, h_r_abs]
          calc (r_pos.mantissa_.toNat : ℚ) * 10 ^ r_pos.exponent_
              ≤ (10 ^ 19 - 1 : ℚ) * 10 ^ r_pos.exponent_ :=
                mul_le_mul_of_nonneg_right h_mant_le (le_of_lt h_pow_pos_e)
            _ ≤ (10 ^ 19 - 1 : ℚ) * (10 : ℚ) ^ (maxExponent - 1 : ℤ) :=
                mul_le_mul_of_nonneg_left h_pow_le (by norm_num)
        have h_truth_abs : |truth| = -truth := abs_of_neg h_truth_neg
        have h_diff_le : |result.toRat - truth| ≤ -truth * (11 / (2 ^ 63 - 18 : ℚ)) := by
          have := h_bound; rw [h_truth_abs] at this; exact this
        have h_diff_lower : result.toRat - truth ≤ |result.toRat - truth| := le_abs_self _
        have h_q_minus_r : q - r_pos.toRat = result.toRat - truth := by
          rw [hq_def, h_r_pos_toRat]; ring
        have h_eps_half : (11 / (2 ^ 63 - 18 : ℚ)) ≤ 1 / 2 := by norm_num
        have h_scaled : q * (11 / (2 ^ 63 - 18 : ℚ)) ≤ q * (1 / 2) :=
          mul_le_mul_of_nonneg_left h_eps_half (le_of_lt hq_pos)
        have h_q_le_2r : q ≤ 2 * r_pos.toRat := by
          have h_q_eps : q * (11 / (2 ^ 63 - 18 : ℚ)) = -truth * (11 / (2 ^ 63 - 18 : ℚ)) := by
            rw [hq_def]
          rw [← h_q_eps] at h_diff_le
          linarith
        have h_pow_split : (10 : ℚ) ^ (maxExponent : ℤ) = 10 * (10 : ℚ) ^ (maxExponent - 1 : ℤ) := by
          rw [show (maxExponent : ℤ) = (maxExponent - 1) + 1 from by ring,
              zpow_add_one₀ (by norm_num : (10 : ℚ) ≠ 0)]
          exact mul_comm _ _
        rw [h_pow_split]
        nlinarith [h_pow_pos_m]
      · push_neg at h_exp_lt
        have h_r_pos_exp : r_pos.exponent_ = result.exponent_ := by rw [hr_pos_def]
        have h_e_ge : result.exponent_ ≥ maxExponent := by rw [← h_r_pos_exp]; exact h_exp_lt
        have h := h_truth_top h_e_ge
        rw [abs_of_neg h_truth_neg] at h
        rw [hq_def]
        exact h
    obtain ⟨n_pos, h_lowerPosAux_eq⟩ := lowerPosAux_isSome_of_lt_top q hq_pos h_q_lt_top
    have hq_not_neg : ¬ (q < 0) := not_lt.mpr (le_of_lt hq_pos)
    have hq_ne : q ≠ 0 := ne_of_gt hq_pos
    have h_n_pos_eq : Number.lower q = some n_pos := by
      unfold Number.lower
      rw [if_neg hq_ne, if_neg hq_not_neg]
      exact h_lowerPosAux_eq
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
  · have h_result_pos : 0 < result.toRat := lt_of_lt_of_le h_truth_pos h_round_up
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
        have h_truth_abs : |truth| = truth := abs_of_pos h_truth_pos
        have h_diff_le : |result.toRat - truth| ≤ truth * (11 / (2 ^ 63 - 18 : ℚ)) := by
          have := h_bound; rw [h_truth_abs] at this; exact this
        have h_diff_eq : |result.toRat - truth| = result.toRat - truth := by
          rw [abs_of_nonneg (by linarith : 0 ≤ result.toRat - truth)]
        rw [h_diff_eq] at h_diff_le
        have h_eps_lt : (11 / (2 ^ 63 - 18 : ℚ)) < 9 := by norm_num
        have h_eps_mul : truth * (11 / (2 ^ 63 - 18 : ℚ)) < truth * 9 :=
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
      have h_truth_abs : |truth| = truth := abs_of_pos h_truth_pos
      have h_diff_le : |result.toRat - truth| ≤ truth * (11 / (2 ^ 63 - 18 : ℚ)) := by
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
              have hmin_eq : largeRange.min.toNat = 1000000000000000000 := largeRange_min_val
              have hmax_eq : largeRange.max.toNat = 9999999999999999999 := largeRange_max_val
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
        -- Coarse ε can't separate 10^18 from 10^18+1; exclude the larger
        -- mantissa via no-inbetween against +smallest_pos_rep.
        have h_mant_le_pow18 : result.mantissa_.toNat ≤ 10^18 := by
          by_contra h_gt
          push_neg at h_gt
          have h_mant_ge_q : ((10 : ℚ)^18 + 1) ≤ (result.mantissa_.toNat : ℚ) := by
            have h1 : (10^18 + 1 : ℕ) ≤ result.mantissa_.toNat := h_gt
            exact_mod_cast h1
          have h_result_gt_spr : smallest_pos_rep < result.toRat := by
            rw [h_result_eq_form, h_spr_eq]
            nlinarith [h_pow_min_pos, h_mant_ge_q]
          have h_m₀_norm : (⟨false, largeRange.min, minExponent⟩ : Number).isNormalized := by
            norm_isNormalized
          have h_m₀_toRat : (⟨false, largeRange.min, minExponent⟩ : Number).toRat
              = smallest_pos_rep := by
            rw [Number.toRat_of_nonneg _ rfl, hspr_def]
          exact h_no_inbetween ⟨false, largeRange.min, minExponent⟩ h_m₀_norm
            (by rw [h_m₀_toRat]; linarith)
            (by rw [h_m₀_toRat]; linarith [le_of_lt h_truth_ge])
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

/-- Bridge for operations whose result mantissa is capped at the top exponent:
the overflow-corner truth bound `h_truth_top` follows from the mantissa cap
`≤ 9223372036854775820` plus the uniform relative bound. -/
theorem truth_top_of_result_cap (result : Number) (truth : ℚ)
    (h_result_norm : result.isNormalized)
    (hresult : result.mantissa_ ≠ 0)
    (h_bound : |result.toRat - truth| ≤ |truth| * (11 / (2 ^ 63 - 18 : ℚ)))
    (h_cap : result.mantissa_.toNat ≤ 9223372036854775820)
    (h_exp_ge : result.exponent_ ≥ maxExponent) :
    |truth| < 10 ^ 19 * (10 : ℚ) ^ (maxExponent : ℤ) := by
  rcases h_result_norm with hz | ⟨_, _, _, _, h_remax⟩
  · exfalso; apply hresult; rw [hz]; rfl
  have h_exp_eq : result.exponent_ = maxExponent := le_antisymm h_remax h_exp_ge
  have h_result_abs : |result.toRat| = (result.mantissa_.toNat : ℚ) * 10 ^ result.exponent_ :=
    abs_toRat_eq result
  have h_pow_max_pos : (0 : ℚ) < (10 : ℚ) ^ (maxExponent : ℤ) := zpow_pos (by norm_num) _
  have h_mant_le : (result.mantissa_.toNat : ℚ) ≤ (2 ^ 63 + 12 : ℚ) := by
    calc (result.mantissa_.toNat : ℚ)
        ≤ (9223372036854775820 : ℚ) := by exact_mod_cast h_cap
      _ = (2 ^ 63 + 12 : ℚ) := by norm_num
  have h_result_le : |result.toRat| ≤ (2 ^ 63 + 12 : ℚ) * (10 : ℚ) ^ (maxExponent : ℤ) := by
    rw [h_result_abs, h_exp_eq]
    exact mul_le_mul_of_nonneg_right h_mant_le (le_of_lt h_pow_max_pos)
  have h_abs_diff : |truth| - |result.toRat| ≤ |result.toRat - truth| := by
    rw [abs_sub_comm]
    exact abs_sub_abs_le_abs_sub truth result.toRat
  have h_truth_scaled : |truth| * ((2 ^ 63 - 29 : ℚ) / (2 ^ 63 - 18)) ≤ |result.toRat| := by
    have h_step : |truth| - |truth| * (11 / (2 ^ 63 - 18 : ℚ)) ≤ |result.toRat| := by linarith
    have h_eq : |truth| - |truth| * (11 / (2 ^ 63 - 18 : ℚ))
        = |truth| * ((2 ^ 63 - 29 : ℚ) / (2 ^ 63 - 18)) := by
      field_simp
      ring
    rw [h_eq] at h_step
    exact h_step
  have h_c_pos : (0 : ℚ) < (2 ^ 63 - 29 : ℚ) / (2 ^ 63 - 18) := by
    apply div_pos <;> norm_num
  have h_num : (2 ^ 63 + 12 : ℚ) < 10 ^ 19 * ((2 ^ 63 - 29 : ℚ) / (2 ^ 63 - 18)) := by
    rw [mul_div_assoc']
    rw [lt_div_iff₀ (by norm_num : (0 : ℚ) < 2 ^ 63 - 18)]
    norm_num
  have h_lt : |truth| * ((2 ^ 63 - 29 : ℚ) / (2 ^ 63 - 18))
      < (10 ^ 19 * (10 : ℚ) ^ (maxExponent : ℤ)) * ((2 ^ 63 - 29 : ℚ) / (2 ^ 63 - 18)) := by
    have h_contra := mul_lt_mul_of_pos_right h_num h_pow_max_pos
    calc |truth| * ((2 ^ 63 - 29 : ℚ) / (2 ^ 63 - 18))
        ≤ (2 ^ 63 + 12 : ℚ) * (10 : ℚ) ^ (maxExponent : ℤ) := le_trans h_truth_scaled h_result_le
      _ < (10 ^ 19 * (10 : ℚ) ^ (maxExponent : ℤ)) * ((2 ^ 63 - 29 : ℚ) / (2 ^ 63 - 18)) := by
          linarith [h_contra]
  exact lt_of_mul_lt_mul_right h_lt (le_of_lt h_c_pos)

end XRPL.Model.Protocol
