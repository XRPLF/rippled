import XRPL.Properties.Protocol.Number.Common.Notation
import XRPL.Properties.Approx
import XRPL.Properties.Protocol.Number.Add.Common.TowardsZero.AlgorithmicFacts
import XRPL.Properties.Protocol.Number.Add.Common.TowardsZero.DiffSignTight
import XRPL.Properties.Protocol.Number.Common.Helpers


namespace XRPL.Model.Protocol

theorem operator_add_rounding_bound_same_sign_towards_zero (x y : Number) (result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (h_same_sign : x.negative_ = y.negative_)
    (h_not_zero : ¬ x.operator_eq y.operator_neg)
    (hok : Number.operator_add x y .towards_zero = .ok result) :
    |result.toRat| ≤ |x.toRat + y.toRat| ∧
    |x.toRat + y.toRat| - |result.toRat| < |x.toRat + y.toRat| * (10 / (2 ^ 63 + 2 : ℚ)) := by
  obtain ⟨zm, ze', f, g, res_pos, hzm_ge, hzm_le_maxRep, hf_nn, hf_lt1,
          habs_xy_eq, h_rup_pos, h_result_abs, hres_pos_mant_ne, _hf_rep, _h_sign, _, _, _, _⟩ :=
    operator_add_algorithmic_facts_same_sign_towards_zero x y result hx hy hx_mant_ne hy_mant_ne
      h_same_sign h_not_zero hok
  have h10ze'_pos : (0 : ℚ) < 10 ^ ze' := zpow_pos (by norm_num) _
  have h10ze'_nn : (0 : ℚ) ≤ 10 ^ ze' := le_of_lt h10ze'_pos
  have hzm_q_ge : (mantissaFloor : ℚ) ≤ (zm.toNat : ℚ) := by exact_mod_cast hzm_ge
  have h_denom_val : ((2 ^ 63 + 2 : ℚ)) = maxRepCuspTarget := by norm_num
  have h_denom_pos : (0 : ℚ) < ((2 ^ 63 + 2 : ℚ)) := by rw [h_denom_val]; norm_num
  by_cases h_zm_le_rep : zm.toNat ≤ maxRep.toNat
  · -- In-range: plain truncate.
    have h_tr_val := doRoundUp_value_towards_zero_truncate g false zm ze' h_zm_le_rep
      "Number::addition overflow" res_pos h_rup_pos hres_pos_mant_ne
    simp only at h_tr_val
    have h_result_abs_eq : |result.toRat| = (zm.toNat : ℚ) * 10 ^ ze' := by
      rw [h_result_abs]; exact h_tr_val
    have h_direction : |result.toRat| ≤ |x.toRat + y.toRat| := by
      rw [habs_xy_eq, h_result_abs_eq]
      nlinarith [h10ze'_nn, hf_nn]
    have h_magnitude : |x.toRat + y.toRat| - |result.toRat|
        < |x.toRat + y.toRat| * (10 / ((2 ^ 63 + 2 : ℚ))) := by
      rw [habs_xy_eq, h_result_abs_eq]
      have h_lhs_eq : ((zm.toNat : ℚ) + f) * 10 ^ ze' - (zm.toNat : ℚ) * 10 ^ ze'
          = f * 10 ^ ze' := by ring
      rw [h_lhs_eq]
      have h_inner : f < (((zm.toNat : ℚ) + f)) * (10 / ((2 ^ 63 + 2 : ℚ))) := f_lt_releps hzm_q_ge hf_lt1
      exact releps_lift h_inner h10ze'_pos
    exact ⟨h_direction, h_magnitude⟩
  · -- Cusp range: maxRep < zm ≤ maxRepUp.
    push_neg at h_zm_le_rep
    obtain ⟨v, hv_val, hv_cases⟩ := doRoundUp_value_cuspRange_cases g zm ze' .towards_zero
      h_zm_le_rep hzm_le_maxRep "Number::addition overflow" res_pos h_rup_pos hres_pos_mant_ne
    have h_result_abs_eq : |result.toRat| = v * 10 ^ ze' := by
      rw [h_result_abs]; exact hv_val
    obtain ⟨hzm_q_gt, hzm_q_le3⟩ := cusp_zm_qbounds h_zm_le_rep hzm_le_maxRep
    rcases hv_cases with ⟨hv, _, _⟩ | ⟨hv, hcoup⟩ | ⟨_, _, hfire⟩
    · -- v = maxRepNat: truncate clamp.
      subst hv
      constructor
      · rw [habs_xy_eq, h_result_abs_eq]
        have h_le : (maxRepNat : ℚ) ≤ (zm.toNat : ℚ) + f := by linarith [hf_nn]
        exact mul_le_mul_of_nonneg_right h_le h10ze'_nn
      · rw [habs_xy_eq, h_result_abs_eq]
        rw [show ((zm.toNat : ℚ) + f) * 10 ^ ze' - (maxRepNat : ℚ) * 10 ^ ze'
              = (((zm.toNat : ℚ) + f) - maxRepNat) * 10 ^ ze' from by ring]
        have h_inner : ((zm.toNat : ℚ) + f) - maxRepNat
            < ((zm.toNat : ℚ) + f) * (10 / ((2 ^ 63 + 2 : ℚ))) :=
          cusp_err_lt_releps hzm_q_le3 hf_lt1
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
          exact absurd h Bool.false_ne_true
      have hzm_q_eq : (zm.toNat : ℚ) = maxRepNat + 3 := by
        rw [hzm_eq, show maxRepUp.toNat = maxRepUpNat from rfl]; norm_num
      constructor
      · rw [habs_xy_eq, h_result_abs_eq, hzm_q_eq]
        have h_le : (maxRepNat : ℚ) + 3 ≤ ((maxRepNat : ℚ) + 3) + f := by linarith [hf_nn]
        exact mul_le_mul_of_nonneg_right h_le h10ze'_nn
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
      exact absurd hfire Bool.false_ne_true

theorem operator_add_rounding_bound_same_sign_towards_zero_rel (x y : Number) (result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (h_same_sign : x.negative_ = y.negative_)
    (h_not_zero : ¬ x.operator_eq y.operator_neg)
    (hok : Number.operator_add x y .towards_zero = .ok result) :
    |result.toRat - (x.toRat + y.toRat)| < |x.toRat + y.toRat| * (10 / (2 ^ 63 + 2 : ℚ)) := by
  obtain ⟨_zm, _ze', _f, _g, _res_pos, _hzm_ge, _hzm_le_maxRep, _hf_nn, _hf_lt1,
          _habs_xy_eq, _h_rup_pos, _h_result_abs, _hres_pos_mant_ne, _hf_rep, h_sign, _, _, _, _⟩ :=
    operator_add_algorithmic_facts_same_sign_towards_zero x y result hx hy hx_mant_ne hy_mant_ne
      h_same_sign h_not_zero hok
  obtain ⟨h_dir, h_mag⟩ := operator_add_rounding_bound_same_sign_towards_zero x y result hx hy
    hx_mant_ne hy_mant_ne h_same_sign h_not_zero hok
  -- result and x+y have the same sign, so |result - (x+y)| = ||result| - |x+y||.
  have h_abs_diff_eq : |result.toRat - (x.toRat + y.toRat)|
      = |(|result.toRat| - |x.toRat + y.toRat|)| := by
    apply abs_diff_eq_abs_sub_abs_of_sign_aligned result (x.toRat + y.toRat)
    · intro h_neg
      have hx_neg : x.negative_ = true := h_sign ▸ h_neg
      have hy_neg : y.negative_ = true := h_same_sign ▸ hx_neg
      have hx_np : x.toRat ≤ 0 := Number.toRat_nonpos_of_negative x hx_neg
      have hy_np : y.toRat ≤ 0 := Number.toRat_nonpos_of_negative y hy_neg
      linarith
    · intro h_pos
      have hx_nn : x.negative_ = false := h_sign ▸ h_pos
      have hy_nn : y.negative_ = false := h_same_sign ▸ hx_nn
      have hx_nn' : 0 ≤ x.toRat := Number.toRat_nonneg_of_nonnegative x hx_nn
      have hy_nn' : 0 ≤ y.toRat := Number.toRat_nonneg_of_nonnegative y hy_nn
      linarith
  rw [h_abs_diff_eq]
  -- |truth| ≥ |result|, so ||result| - |truth|| = |truth| - |result|
  have h_diff_nn : 0 ≤ |x.toRat + y.toRat| - |result.toRat| := by linarith
  rw [show |result.toRat| - |x.toRat + y.toRat| = -(|x.toRat + y.toRat| - |result.toRat|) from by ring,
      abs_neg, abs_of_nonneg h_diff_nn]
  exact h_mag

/-- Combined **tight** relative-error bound for `Number.operator_add` under
`.towards_zero` rounding, covering both same-sign and diff-sign branches with the
uniform ε-scale `11/(2^63 - 18)`. Mirrors `operator_add_rounding_bound_downward_tight`
for `.downward`. -/
theorem operator_add_rounding_bound_towards_zero_tight (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (h_not_zero : ¬ x.operator_eq y.operator_neg)
    (hok : Number.operator_add x y .towards_zero = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    |result.toRat - (x.toRat + y.toRat)| < |x.toRat + y.toRat| * (11 / (2 ^ 63 - 18 : ℚ)) := by
  by_cases h_sign : x.negative_ = y.negative_
  · have hss := operator_add_rounding_bound_same_sign_towards_zero_rel x y result hx hy
      hx_mant_ne hy_mant_ne
      h_sign h_not_zero hok
    have htruth_nn : 0 ≤ |x.toRat + y.toRat| := abs_nonneg _
    have hconst : (10 / (2 ^ 63 + 2 : ℚ)) ≤ (11 / (2 ^ 63 - 18 : ℚ)) := by norm_num
    calc |result.toRat - (x.toRat + y.toRat)|
        < |x.toRat + y.toRat| * (10 / (2 ^ 63 + 2 : ℚ)) := hss
      _ ≤ |x.toRat + y.toRat| * (11 / (2 ^ 63 - 18 : ℚ)) :=
          mul_le_mul_of_nonneg_left hconst htruth_nn
  · exact operator_add_rounding_bound_diff_sign_towards_zero_tight x y result hx hy hx_mant_ne hy_mant_ne
      h_sign h_not_zero hok hresult

end XRPL.Model.Protocol
