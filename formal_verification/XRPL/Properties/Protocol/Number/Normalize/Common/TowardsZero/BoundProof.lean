import XRPL.Properties.Protocol.Number.Common.Notation
import XRPL.Properties.Protocol.Number.Normalize.Common.TowardsZero.AlgorithmicFacts
import XRPL.Properties.Protocol.Number.Common.Helpers
import XRPL.Properties.Approx


namespace XRPL.Model.Protocol

/-! ## towards_zero -/

/-- Relative-error bound for `Number.normalize` under `.towards_zero` rounding.

`normalize` accepts an arbitrary `(mantissa, exponent)` pair (the input need not
be normalized). Its final stage is `doRoundUp`, which under `.towards_zero`
always truncates toward zero. The error matches the same-sign `.towards_zero`
truncation supremum `10/(2^63 + 2)`. -/
theorem normalize_rounding_bound_towards_zero (n result : Number)
    (hn_mant_ne : n.mantissa_ ≠ 0)
    (hok : n.normalize largeRange.min largeRange.max .towards_zero = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    |result.toRat| ≤ |n.toRat| ∧
    |n.toRat| - |result.toRat| < |n.toRat| * (10 / (2 ^ 63 + 2 : ℚ)) := by
  obtain ⟨zm, ze, f, g, res_pos, hzm_ge, hzm_le_max, hf_nn, hf_lt1, _hcusp_state,
          habs_n_eq, h_rup_pos, h_result_abs, hres_pos_mant_ne, _hf_rep, _h_sign, _⟩ :=
    normalize_algorithmic_facts_towards_zero n result hn_mant_ne hok hresult
  have h10ze_pos : (0 : ℚ) < 10 ^ ze := zpow_pos (by norm_num) _
  have h10ze_nn : (0 : ℚ) ≤ 10 ^ ze := le_of_lt h10ze_pos
  have hzm_q_ge : (mantissaFloor : ℚ) ≤ (zm.toNat : ℚ) := by exact_mod_cast hzm_ge
  by_cases h_zm_le_rep : zm.toNat ≤ maxRep.toNat
  · -- ===== IN RANGE: zm ≤ maxRep — towards_zero truncates: |result| = zm·10^ze =====
    have h_tr_val := doRoundUp_value_towards_zero_truncate g false zm ze h_zm_le_rep
      "Number::normalize 2" res_pos h_rup_pos hres_pos_mant_ne
    have h_result_abs_eq : |result.toRat| = (zm.toNat : ℚ) * 10 ^ ze := by
      rw [h_result_abs]; exact h_tr_val
    -- |result| ≤ |n|
    have h_direction : |result.toRat| ≤ |n.toRat| := by
      rw [habs_n_eq, h_result_abs_eq]
      nlinarith [h10ze_nn, hf_nn]
    -- magnitude shortfall < |n| · ε
    have h_magnitude : |n.toRat| - |result.toRat| < |n.toRat| * (10 / (2 ^ 63 + 2 : ℚ)) := by
      rw [habs_n_eq, h_result_abs_eq]
      have h_lhs_eq : ((zm.toNat : ℚ) + f) * 10 ^ ze - (zm.toNat : ℚ) * 10 ^ ze
          = f * 10 ^ ze := by ring
      rw [h_lhs_eq]
      have h_inner : f < (((zm.toNat : ℚ) + f)) * (10 / ((2 ^ 63 + 2 : ℚ))) := f_lt_releps hzm_q_ge hf_lt1
      exact releps_lift h_inner h10ze_pos
    exact ⟨h_direction, h_magnitude⟩
  · -- ===== CUSP RANGE: maxRep < zm ≤ maxRepUp =====
    push_neg at h_zm_le_rep
    obtain ⟨v, hv_val, hv_cases⟩ := doRoundUp_value_cuspRange_cases g zm ze .towards_zero
      h_zm_le_rep hzm_le_max "Number::normalize 2" res_pos h_rup_pos hres_pos_mant_ne
    have h_result_abs_eq : |result.toRat| = v * 10 ^ ze := by
      rw [h_result_abs]; exact hv_val
    obtain ⟨hzm_q_gt, hzm_q_le3⟩ := cusp_zm_qbounds h_zm_le_rep hzm_le_max
    have h_denom_val : ((2 ^ 63 + 2 : ℚ)) = maxRepCuspTarget := by norm_num
    rcases hv_cases with ⟨hv, _, _⟩ | ⟨hv, hcoup⟩ | ⟨_, _, hfire⟩
    · -- v = maxRepNat: the truncating clamp, below the truth.
      subst hv
      refine ⟨?_, ?_⟩
      · rw [habs_n_eq, h_result_abs_eq]
        exact mul_le_mul_of_nonneg_right (by linarith [hf_nn]) h10ze_nn
      · rw [habs_n_eq, h_result_abs_eq]
        rw [show ((zm.toNat : ℚ) + f) * 10 ^ ze - (maxRepNat : ℚ) * 10 ^ ze
              = (((zm.toNat : ℚ) + f) - maxRepNat) * 10 ^ ze from by ring]
        have h_inner : ((zm.toNat : ℚ) + f) - maxRepNat
            < ((zm.toNat : ℚ) + f) * (10 / ((2 ^ 63 + 2 : ℚ))) :=
          cusp_err_lt_releps hzm_q_le3 hf_lt1
        calc (((zm.toNat : ℚ) + f) - maxRepNat) * 10 ^ ze
            < ((zm.toNat : ℚ) + f) * (10 / ((2 ^ 63 + 2 : ℚ))) * 10 ^ ze :=
              mul_lt_mul_of_pos_right h_inner h10ze_pos
          _ = ((zm.toNat : ℚ) + f) * 10 ^ ze * (10 / ((2 ^ 63 + 2 : ℚ))) := by ring
    · -- v = maxRepNat + 3: the round decision never fires in towards_zero, so zm = maxRepUp.
      subst hv
      have hzm_eq : zm.toNat = maxRepUp.toNat := by
        rcases hcoup with ⟨h, _⟩ | ⟨_, h⟩
        · exact h
        · rw [roundUp_bool_towards_zero_false] at h
          exact absurd h Bool.noConfusion
      have hzm_q_eq : (zm.toNat : ℚ) = maxRepNat + 3 := by
        rw [hzm_eq, show maxRepUp.toNat = maxRepUpNat from rfl]; norm_num
      refine ⟨?_, ?_⟩
      · rw [habs_n_eq, h_result_abs_eq, hzm_q_eq]
        exact mul_le_mul_of_nonneg_right (by linarith [hf_nn]) h10ze_nn
      · rw [habs_n_eq, h_result_abs_eq, hzm_q_eq]
        rw [show (((maxRepNat : ℚ) + 3) + f) * 10 ^ ze - ((maxRepNat : ℚ) + 3) * 10 ^ ze
              = f * 10 ^ ze from by ring]
        have h_inner : f < (((maxRepNat : ℚ) + 3) + f) * (10 / ((2 ^ 63 + 2 : ℚ))) := by
          rw [h_denom_val, show (((maxRepNat : ℚ) + 3) + f) * (10 / (maxRepCuspTarget : ℚ))
                = 10 * (((maxRepNat : ℚ) + 3) + f) / maxRepCuspTarget from by ring,
              lt_div_iff₀ (by norm_num : (0 : ℚ) < maxRepCuspTarget)]
          nlinarith [hf_nn, hf_lt1]
        calc f * 10 ^ ze
            < (((maxRepNat : ℚ) + 3) + f) * (10 / ((2 ^ 63 + 2 : ℚ))) * 10 ^ ze :=
              mul_lt_mul_of_pos_right h_inner h10ze_pos
          _ = (((maxRepNat : ℚ) + 3) + f) * 10 ^ ze * (10 / ((2 ^ 63 + 2 : ℚ))) := by ring
    · -- v = maxRepNat + 13: the round decision never fires in towards_zero.
      rw [roundUp_bool_towards_zero_false] at hfire
      exact absurd hfire Bool.noConfusion

end XRPL.Model.Protocol
