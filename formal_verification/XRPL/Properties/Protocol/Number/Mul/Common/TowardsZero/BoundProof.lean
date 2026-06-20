import XRPL.Properties.Protocol.Number.Common.Notation
import XRPL.Properties.Approx
import XRPL.Properties.Protocol.Number.Common.Helpers
import XRPL.Properties.Protocol.Number.Mul.Common.TowardsZero.AlgorithmicFacts


namespace XRPL.Model.Protocol

set_option maxHeartbeats 800000 in
-- nlinarith over large constants (2^63+2) plus the cusp-range legs need extra heartbeats
/-- Relative-error bound for `Number.operator_mul` under `.towards_zero` rounding.
The bound `10/(2^63 + 2)` matches `.downward` (twice the `.to_nearest` bound). -/
theorem operator_mul_rounding_bound_towards_zero (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (hok : Number.operator_mul x y .towards_zero = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    |result.toRat| ≤ |x.toRat * y.toRat| ∧
    |x.toRat * y.toRat| - |result.toRat| < |x.toRat * y.toRat| * (10 / (2 ^ 63 + 2 : ℚ)) := by
  obtain ⟨zm, ze', f, g, res_pos, hzm_ge, hzm_le_maxRep, hf_nn, hf_lt1,
          habs_xy_eq, h_rup_pos, h_result_abs, hres_pos_mant_ne, _hf_rep, _h_sign⟩ :=
    operator_mul_algorithmic_facts_towards_zero x y result hx hy hx_mant_ne hy_mant_ne hok hresult
  have h10ze'_pos : (0 : ℚ) < 10 ^ ze' := zpow_pos (by norm_num) _
  have h10ze'_nn : (0 : ℚ) ≤ 10 ^ ze' := le_of_lt h10ze'_pos
  have hzm_q_ge : (mantissaFloor : ℚ) ≤ (zm.toNat : ℚ) := by exact_mod_cast hzm_ge
  have hzm_q_le : (zm.toNat : ℚ) ≤ maxRepNat + 3 := by
    have : (zm.toNat : ℚ) ≤ ((maxRepUp.toNat : ℕ) : ℚ) := by exact_mod_cast hzm_le_maxRep
    have hmrq : ((maxRepUp.toNat : ℕ) : ℚ) = maxRepNat + 3 := by
      rw [show maxRepUp.toNat = maxRepUpNat from rfl]; norm_num
    rw [hmrq] at this; exact this
  have h_abs_truth_nn : 0 ≤ ((zm.toNat : ℚ) + f) * 10 ^ ze' := by
    apply mul_nonneg _ h10ze'_nn
    have : (0 : ℚ) ≤ (zm.toNat : ℚ) := by exact_mod_cast Nat.zero_le _
    linarith
  have h_denom_val : ((2 ^ 63 + 2 : ℚ)) = maxRepCuspTarget := by norm_num
  have h_denom_pos : (0 : ℚ) < ((2 ^ 63 + 2 : ℚ)) := by rw [h_denom_val]; norm_num
  by_cases h_zm_le_rep : zm.toNat ≤ maxRep.toNat
  swap
  · -- ===== CUSP RANGE: maxRep < zm ≤ maxRepUp =====
    have h_zm_gt_rep : maxRep.toNat < zm.toNat := by omega
    obtain ⟨v, hv_val, hv_cases⟩ := doRoundUp_value_cuspRange_cases g zm ze' .towards_zero
      h_zm_gt_rep hzm_le_maxRep "Number::multiplication overflow" res_pos h_rup_pos hres_pos_mant_ne
    have h_result_abs_eq : |result.toRat| = v * 10 ^ ze' := by
      rw [h_result_abs]; exact hv_val
    have hzm_q_gt : (maxRepNat : ℚ) < (zm.toNat : ℚ) := by
      have : (maxRepNat : ℕ) < zm.toNat := by rw [← maxRep_val]; exact h_zm_gt_rep
      exact_mod_cast this
    rcases hv_cases with ⟨hv, _, _⟩ | ⟨hv, hcoup⟩ | ⟨_, _, hfire⟩
    · -- v = maxRepNat: the truncating clamp, below the truth.
      subst hv
      refine ⟨?_, ?_⟩
      · rw [habs_xy_eq, h_result_abs_eq]
        exact mul_le_mul_of_nonneg_right (by linarith [hf_nn]) h10ze'_nn
      · rw [habs_xy_eq, h_result_abs_eq]
        rw [show ((zm.toNat : ℚ) + f) * 10 ^ ze' - (maxRepNat : ℚ) * 10 ^ ze'
              = (((zm.toNat : ℚ) + f) - maxRepNat) * 10 ^ ze' from by ring]
        have h_inner : ((zm.toNat : ℚ) + f) - maxRepNat
            < ((zm.toNat : ℚ) + f) * (10 / ((2 ^ 63 + 2 : ℚ))) :=
          cusp_err_lt_releps hzm_q_le hf_lt1
        calc (((zm.toNat : ℚ) + f) - maxRepNat) * 10 ^ ze'
            < ((zm.toNat : ℚ) + f) * (10 / ((2 ^ 63 + 2 : ℚ))) * 10 ^ ze' :=
              mul_lt_mul_of_pos_right h_inner h10ze'_pos
          _ = ((zm.toNat : ℚ) + f) * 10 ^ ze' * (10 / ((2 ^ 63 + 2 : ℚ))) := by ring
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
      · rw [habs_xy_eq, h_result_abs_eq, hzm_q_eq]
        exact mul_le_mul_of_nonneg_right (by linarith [hf_nn]) h10ze'_nn
      · rw [habs_xy_eq, h_result_abs_eq, hzm_q_eq]
        rw [show (((maxRepNat : ℚ) + 3) + f) * 10 ^ ze' - ((maxRepNat : ℚ) + 3) * 10 ^ ze'
              = f * 10 ^ ze' from by ring]
        have h_inner : f < (((maxRepNat : ℚ) + 3) + f) * (10 / ((2 ^ 63 + 2 : ℚ))) := by
          rw [h_denom_val, show (((maxRepNat : ℚ) + 3) + f) * (10 / (maxRepCuspTarget : ℚ))
                = 10 * (((maxRepNat : ℚ) + 3) + f) / maxRepCuspTarget from by ring,
              lt_div_iff₀ (by norm_num : (0 : ℚ) < maxRepCuspTarget)]
          nlinarith [hf_nn, hf_lt1]
        calc f * 10 ^ ze'
            < (((maxRepNat : ℚ) + 3) + f) * (10 / ((2 ^ 63 + 2 : ℚ))) * 10 ^ ze' :=
              mul_lt_mul_of_pos_right h_inner h10ze'_pos
          _ = (((maxRepNat : ℚ) + 3) + f) * 10 ^ ze' * (10 / ((2 ^ 63 + 2 : ℚ))) := by ring
    · -- v = maxRepNat + 13: the round decision never fires in towards_zero.
      rw [roundUp_bool_towards_zero_false] at hfire
      exact absurd hfire Bool.noConfusion
  -- ===== IN RANGE: zm ≤ maxRep =====
  have h_tr_val := doRoundUp_value_towards_zero_truncate g false zm ze' h_zm_le_rep "Number::multiplication overflow" res_pos h_rup_pos hres_pos_mant_ne
  simp only at h_tr_val
  have h_result_abs_eq : |result.toRat| = (zm.toNat : ℚ) * 10 ^ ze' := by
    rw [h_result_abs]; exact h_tr_val
  have h_direction : |result.toRat| ≤ |x.toRat * y.toRat| := by
    rw [habs_xy_eq, h_result_abs_eq]
    nlinarith [h10ze'_nn, hf_nn]
  have h_magnitude : |x.toRat * y.toRat| - |result.toRat|
      < |x.toRat * y.toRat| * (10 / ((2 ^ 63 + 2 : ℚ))) := by
    rw [habs_xy_eq, h_result_abs_eq]
    have h_lhs_eq : ((zm.toNat : ℚ) + f) * 10 ^ ze' - (zm.toNat : ℚ) * 10 ^ ze'
        = f * 10 ^ ze' := by ring
    rw [h_lhs_eq]
    have h_inner : f < (((zm.toNat : ℚ) + f)) * (10 / ((2 ^ 63 + 2 : ℚ))) := f_lt_releps hzm_q_ge hf_lt1
    exact releps_lift h_inner h10ze'_pos
  exact ⟨h_direction, h_magnitude⟩

end XRPL.Model.Protocol
