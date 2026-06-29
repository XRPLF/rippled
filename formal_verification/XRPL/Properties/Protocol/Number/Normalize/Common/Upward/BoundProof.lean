import XRPL.Properties.Protocol.Number.Common.Notation
import XRPL.Properties.Protocol.Number.Normalize.Common.Upward.AlgorithmicFacts
import XRPL.Properties.Protocol.Number.Common.Helpers
import XRPL.Properties.Approx


namespace XRPL.Model.Protocol

/-! ## upward -/

set_option maxHeartbeats 800000 in
-- nlinarith over large constants (2^63+2) needs extra heartbeats
/-- Relative-error bound for `Number.normalize` under `.upward` rounding.

`normalize` accepts an arbitrary `(mantissa, exponent)` pair. Its final stage is
`doRoundUp`, so the error matches the same-sign `.upward` supremum
`10/(2^63 + 2)`, the tight supremum (not attained — see
`normalize_rounding_bound_upward_attained`). -/
theorem normalize_rounding_bound_upward (n result : Number)
    (hn_mant_ne : n.mantissa_ ≠ 0)
    (hok : n.normalize largeRange.min largeRange.max .upward = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    n.toRat ≤ result.toRat ∧
    result.toRat - n.toRat < |n.toRat| * (10 / (2 ^ 63 + 2 : ℚ)) := by
  obtain ⟨zm, ze', f, g, res_pos, hzm_ge, hzm_le_maxRep, hf_nn, hf_lt1, h_floor_constraint,
          hcusp_state, habs_n_eq, h_rup_pos, h_result_abs, hres_pos_mant_ne, hf_rep, h_sign, h_g_sbit, _⟩ :=
    normalize_algorithmic_facts_upward n result hn_mant_ne hok hresult
  have h10ze'_pos : (0 : ℚ) < 10 ^ ze' := zpow_pos (by norm_num) _
  have h10ze'_nn : (0 : ℚ) ≤ 10 ^ ze' := le_of_lt h10ze'_pos
  have hzm_q_ge : (mantissaFloor : ℚ) ≤ (zm.toNat : ℚ) := by exact_mod_cast hzm_ge
  have h_abs_truth_nn : 0 ≤ ((zm.toNat : ℚ) + f) * 10 ^ ze' := by
    apply mul_nonneg _ h10ze'_nn
    have : (0 : ℚ) ≤ (zm.toNat : ℚ) := by exact_mod_cast Nat.zero_le _
    linarith
  have h_denom_val : ((2 ^ 63 + 2 : ℚ)) = maxRepCuspTarget := by norm_num
  have h_denom_pos : (0 : ℚ) < ((2 ^ 63 + 2 : ℚ)) := by rw [h_denom_val]; norm_num
  -- Extract `f`'s structure for direction analysis.
  have hf_rep' := hf_rep
  obtain ⟨x_rep, hx_nn, _hx_lt, hf_eq, hxbit_iff, _hall⟩ := hf_rep
  -- Local helper: convert |result.toRat| identity to signed form.
  have h_result_abs_via_self :
      (result.mantissa_.toNat : ℚ) * 10 ^ result.exponent_
        = (res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_ := by
    have habs := abs_toRat_eq result
    rw [h_result_abs] at habs
    exact habs.symm
  by_cases h_zm_le_rep : zm.toNat ≤ maxRep.toNat
  · -- ===== IN RANGE: zm ≤ maxRep =====
    by_cases h_sru : g.shouldRoundUp_upward
    · -- ===== ROUND UP =====
      have hf_pos : 0 < f := represents_pos_of_shouldRoundUp_upward g f hf_rep' h_sru
      -- sbit = false, so n is non-negative.
      have h_g_sbit_false : g.sbit_ = false := h_sru.1
      have h_n_nonneg : n.negative_ = false := by rw [← h_g_sbit]; exact h_g_sbit_false
      have h_result_pos : result.negative_ = false := by rw [h_sign]; exact h_n_nonneg
      have h_truth_nn : 0 ≤ n.toRat := Number.toRat_nonneg_of_nonnegative n h_n_nonneg
      have h_abs_truth_eq : |n.toRat| = n.toRat := abs_of_nonneg h_truth_nn
      have h_result_pos_val : result.toRat
          = (res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_ := by
        have := Number.toRat_of_nonneg result h_result_pos
        rw [this, h_result_abs_via_self]
      by_cases h_cusp : zm = maxRep
      · -- ===== CUSP =====
        have h_cusp_val := doRoundUp_value_upward_roundUp_cusp g false zm ze' h_cusp h_sru
          "Number::normalize 2" res_pos h_rup_pos hres_pos_mant_ne
        simp only at h_cusp_val
        have hzm_eq_maxRep_q : (zm.toNat : ℚ) = maxRepNat := by
          rw [show zm.toNat = maxRep.toNat from by rw [h_cusp], maxRep_val]; norm_num
        have h_truth_signed : n.toRat = ((zm.toNat : ℚ) + f) * 10 ^ ze' := by
          have h1 : |n.toRat| = ((zm.toNat : ℚ) + f) * 10 ^ ze' := habs_n_eq
          rw [h_abs_truth_eq] at h1; exact h1
        have h_result_signed : result.toRat = (maxRepCuspTarget : ℚ) * 10 ^ ze' := by
          rw [h_result_pos_val, h_cusp_val]
        have h_direction : n.toRat ≤ result.toRat := by
          rw [h_result_signed, h_truth_signed]
          have h_inner : ((zm.toNat : ℚ) + f) ≤ maxRepCuspTarget := by
            rw [hzm_eq_maxRep_q]; linarith [hf_lt1]
          nlinarith [h10ze'_nn, h_inner]
        have h_magnitude : result.toRat - n.toRat
            < |n.toRat| * (10 / ((2 ^ 63 + 2 : ℚ))) := by
          rw [h_abs_truth_eq, h_result_signed, h_truth_signed]
          rw [hzm_eq_maxRep_q]
          have h_lhs_eq : (maxRepCuspTarget : ℚ) * 10 ^ ze' -
              ((maxRepNat : ℚ) + f) * 10 ^ ze'
              = (3 - f) * 10 ^ ze' := by ring
          have h_rhs_arrange : (((maxRepNat : ℚ) + f) * 10 ^ ze') *
              (10 / ((2 ^ 63 + 2 : ℚ)))
              = (maxRepNat + f) * 10 ^ ze' * (10 / (2 ^ 63 + 2 : ℚ)) := by ring
          rw [h_lhs_eq, h_rhs_arrange]
          have h_inner : (3 - f) < (maxRepNat + f) * (10 / ((2 ^ 63 + 2 : ℚ))) := by
            rw [h_denom_val]
            rw [show (maxRepNat + f) * (10 / (maxRepCuspTarget : ℚ))
                  = 10 * (maxRepNat + f) / maxRepCuspTarget by ring]
            rw [lt_div_iff₀ (by norm_num : (0 : ℚ) < maxRepCuspTarget)]
            nlinarith [hf_nn, hf_lt1]
          calc (3 - f) * 10 ^ ze'
              < (maxRepNat + f) * (10 / ((2 ^ 63 + 2 : ℚ))) * 10 ^ ze' :=
                mul_lt_mul_of_pos_right h_inner h10ze'_pos
            _ = (maxRepNat + f) * 10 ^ ze' * (10 / ((2 ^ 63 + 2 : ℚ))) := by ring
        exact ⟨h_direction, h_magnitude⟩
      · -- ===== NON-CUSP, RoundUp =====
        have hzm_lt_maxRep : zm.toNat < maxRep.toNat := by
          have : zm.toNat ≠ maxRep.toNat := fun heq => h_cusp (UInt64.toNat_inj.mp heq)
          omega
        have h_no_cusp : zm.toNat + 1 ≤ maxRep.toNat := by omega
        have h_nc_val := doRoundUp_value_upward_roundUp_noCusp g false zm ze' h_sru h_no_cusp
          "Number::normalize 2" res_pos h_rup_pos hres_pos_mant_ne
        simp only at h_nc_val
        have h_truth_signed : n.toRat = ((zm.toNat : ℚ) + f) * 10 ^ ze' := by
          have h1 : |n.toRat| = ((zm.toNat : ℚ) + f) * 10 ^ ze' := habs_n_eq
          rw [h_abs_truth_eq] at h1; exact h1
        have h_result_signed : result.toRat = ((zm.toNat : ℚ) + 1) * 10 ^ ze' := by
          rw [h_result_pos_val, h_nc_val]
        have h_direction : n.toRat ≤ result.toRat := by
          rw [h_result_signed, h_truth_signed]
          have h_inner : ((zm.toNat : ℚ) + f) ≤ ((zm.toNat : ℚ) + 1) := by linarith
          nlinarith [h10ze'_nn, h_inner]
        have h_magnitude : result.toRat - n.toRat
            < |n.toRat| * (10 / ((2 ^ 63 + 2 : ℚ))) := by
          rw [h_abs_truth_eq, h_result_signed, h_truth_signed]
          have h_lhs_eq : ((zm.toNat : ℚ) + 1) * 10 ^ ze' -
              ((zm.toNat : ℚ) + f) * 10 ^ ze' = (1 - f) * 10 ^ ze' := by ring
          have h_rhs_arrange : (((zm.toNat : ℚ) + f) * 10 ^ ze') *
              (10 / ((2 ^ 63 + 2 : ℚ)))
              = ((zm.toNat : ℚ) + f) * 10 ^ ze' * (10 / ((2 ^ 63 + 2 : ℚ))) := by ring
          rw [h_lhs_eq, h_rhs_arrange]
          have h_inner : (1 - f) < (((zm.toNat : ℚ) + f)) * (10 / ((2 ^ 63 + 2 : ℚ))) :=
            one_sub_f_lt_releps hzm_ge h_floor_constraint hf_pos hf_lt1
          exact releps_lift h_inner h10ze'_pos
        exact ⟨h_direction, h_magnitude⟩
    · -- ===== NO ROUND UP (truncate) =====
      have h_tr_val := doRoundUp_value_upward_truncate g false zm ze' h_sru h_zm_le_rep
        "Number::normalize 2" res_pos h_rup_pos hres_pos_mant_ne
      simp only at h_tr_val
      by_cases h_zn : n.negative_ = true
      · -- n < 0. Truncating magnitude toward zero ⇒ |result| ≤ |n|, both ≤ 0 ⇒ n ≤ result.
        have h_truth_nonpos : n.toRat ≤ 0 := Number.toRat_nonpos_of_negative n h_zn
        have h_abs_truth_eq_neg : |n.toRat| = -(n.toRat) := abs_of_nonpos h_truth_nonpos
        have h_truth_signed : n.toRat = -(((zm.toNat : ℚ) + f) * 10 ^ ze') := by
          have h1 : |n.toRat| = ((zm.toNat : ℚ) + f) * 10 ^ ze' := habs_n_eq
          rw [h_abs_truth_eq_neg] at h1
          have h2 : n.toRat = -(((zm.toNat : ℚ) + f) * 10 ^ ze') := by rw [← h1]; ring
          exact h2
        have h_result_neg : result.negative_ = true := by rw [h_sign]; exact h_zn
        have h_result_neg_val : result.toRat
            = -((res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_) := by
          have := Number.toRat_of_neg result h_result_neg
          rw [this, h_result_abs_via_self]
        have h_result_signed : result.toRat = -((zm.toNat : ℚ) * 10 ^ ze') := by
          rw [h_result_neg_val, h_tr_val]
        have h_direction : n.toRat ≤ result.toRat := by
          rw [h_result_signed, h_truth_signed]
          nlinarith [h10ze'_nn, hf_nn]
        have h_magnitude : result.toRat - n.toRat
            < |n.toRat| * (10 / ((2 ^ 63 + 2 : ℚ))) := by
          rw [h_abs_truth_eq_neg, h_result_signed, h_truth_signed]
          have h_lhs_eq : -((zm.toNat : ℚ) * 10 ^ ze') -
              -(((zm.toNat : ℚ) + f) * 10 ^ ze') = f * 10 ^ ze' := by ring
          have h_rhs_arrange : -(-(((zm.toNat : ℚ) + f) * 10 ^ ze')) *
              (10 / ((2 ^ 63 + 2 : ℚ)))
              = ((zm.toNat : ℚ) + f) * 10 ^ ze' * (10 / ((2 ^ 63 + 2 : ℚ))) := by ring
          rw [h_lhs_eq, h_rhs_arrange]
          have h_inner : f < (((zm.toNat : ℚ) + f)) * (10 / ((2 ^ 63 + 2 : ℚ))) := f_lt_releps hzm_q_ge hf_lt1
          exact releps_lift h_inner h10ze'_pos
        exact ⟨h_direction, h_magnitude⟩
      · -- n ≥ 0. sbit=false; ¬shouldRoundUp_upward ⇒ guard = 0 ⇒ f = 0 ⇒ result = n.
        have h_n_nonneg : n.negative_ = false := Bool.not_eq_true _ |>.mp h_zn
        have h_g_sbit_false : g.sbit_ = false := by rw [h_g_sbit]; exact h_n_nonneg
        have h_no_or : ¬ (g.digits_ > 0 ∨ g.xbit_ = true) := by
          intro h
          exact h_sru ⟨h_g_sbit_false, h⟩
        have h_digits_not_pos : ¬ g.digits_ > 0 := fun h => h_no_or (Or.inl h)
        have h_xbit_not_true : ¬ g.xbit_ = true := fun h => h_no_or (Or.inr h)
        have h_xbit_eq_false : g.xbit_ = false := Bool.not_eq_true _ |>.mp h_xbit_not_true
        have h_digits_zero_uint : g.digits_ = 0 := by
          have h_dn : g.digits_.toNat = 0 := by
            by_contra h_ne
            apply h_digits_not_pos
            change (0 : UInt64) < g.digits_
            rw [UInt64.lt_iff_toNat_lt]
            have h0 : (0 : UInt64).toNat = 0 := rfl
            rw [h0]; omega
          rw [← UInt64.toNat_inj]; rw [h_dn]; rfl
        have h_decval_zero : (decimalValue g.digits_ : ℚ) = 0 := by
          rw [h_digits_zero_uint, decimalValue_zero]; norm_num
        have h_x_zero : x_rep = 0 := by
          rcases lt_or_eq_of_le hx_nn with hlt | heq
          · exfalso
            have : g.xbit_ = true := hxbit_iff.mpr hlt
            rw [this] at h_xbit_eq_false; exact Bool.noConfusion h_xbit_eq_false
          · exact heq.symm
        have hf_zero : f = 0 := by
          rw [hf_eq, h_decval_zero, h_x_zero]; ring
        have h_truth_nn : 0 ≤ n.toRat := Number.toRat_nonneg_of_nonnegative n h_n_nonneg
        have h_abs_truth_eq : |n.toRat| = n.toRat := abs_of_nonneg h_truth_nn
        have h_truth_signed : n.toRat = ((zm.toNat : ℚ) + f) * 10 ^ ze' := by
          have h1 : |n.toRat| = ((zm.toNat : ℚ) + f) * 10 ^ ze' := habs_n_eq
          rw [h_abs_truth_eq] at h1; exact h1
        have h_result_pos : result.negative_ = false := by rw [h_sign]; exact h_n_nonneg
        have h_result_pos_val : result.toRat
            = (res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_ := by
          have := Number.toRat_of_nonneg result h_result_pos
          rw [this, h_result_abs_via_self]
        have h_result_signed : result.toRat = (zm.toNat : ℚ) * 10 ^ ze' := by
          rw [h_result_pos_val, h_tr_val]
        have h_truth_eq_result : n.toRat = result.toRat := by
          rw [h_result_signed, h_truth_signed, hf_zero]; ring
        refine ⟨?_, ?_⟩
        · exact le_of_eq h_truth_eq_result
        · rw [h_truth_eq_result]
          have h_zero : result.toRat - result.toRat = 0 := by ring
          rw [h_zero]
          have h_result_ne : result.toRat ≠ 0 := by
            intro h; rw [Number.toRat_eq_zero_iff] at h; exact hresult h
          have h_abs_pos : (0 : ℚ) < |result.toRat| := abs_pos.mpr h_result_ne
          have h_eps_pos : (0 : ℚ) < 10 / ((2 ^ 63 + 2 : ℚ)) := by
            apply div_pos (by norm_num) h_denom_pos
          exact mul_pos h_abs_pos h_eps_pos
  · -- ===== CUSP RANGE: maxRep < zm ≤ maxRepUp — no digits were pushed, f = 0 =====
    push_neg at h_zm_le_rep
    obtain ⟨hf0, hempty⟩ := hcusp_state h_zm_le_rep
    obtain ⟨v, hv_val, hv_cases⟩ := doRoundUp_value_cuspRange_cases g zm ze' .upward
      h_zm_le_rep hzm_le_maxRep "Number::normalize 2" res_pos h_rup_pos hres_pos_mant_ne
    obtain ⟨hzm_q_gt, hzm_q_le3⟩ := cusp_zm_qbounds h_zm_le_rep hzm_le_maxRep
    -- The plain-guard round decision is dead (the guard is empty).
    have h_ground : g.round .upward = -2 := by
      unfold Guard.round
      rw [if_pos hempty]
    have h_gbool_false :
        ((g.round .upward == 1) || ((g.round .upward == 0) && (zm % 2 == 1))) = false := by
      rw [h_ground]
      rfl
    have habs_n_eq' : |n.toRat| = (zm.toNat : ℚ) * 10 ^ ze' := by
      rw [habs_n_eq, hf0]; ring
    have h_allow_pos : (0 : ℚ) < |n.toRat| * (10 / (2 ^ 63 + 2 : ℚ)) := by
      rw [habs_n_eq']
      apply mul_pos (mul_pos (by linarith : (0 : ℚ) < (zm.toNat : ℚ)) h10ze'_pos)
      rw [h_denom_val]; norm_num
    by_cases h_zn : n.negative_ = true
    · -- ===== negative n: `.upward` truncates the magnitude =====
      have h_truth_nonpos : n.toRat ≤ 0 := Number.toRat_nonpos_of_negative n h_zn
      have h_abs_neg : |n.toRat| = -(n.toRat) := abs_of_nonpos h_truth_nonpos
      have h_truth_signed : n.toRat = -((zm.toNat : ℚ) * 10 ^ ze') := by
        have h1 := habs_n_eq'
        rw [h_abs_neg] at h1; linarith
      have h_result_neg : result.negative_ = true := by rw [h_sign]; exact h_zn
      have h_result_signed : result.toRat = -(v * 10 ^ ze') := by
        have h2 := Number.toRat_of_neg result h_result_neg
        rw [h2, h_result_abs_via_self, hv_val]
      rcases hv_cases with ⟨hv, _, _⟩ | ⟨hv, hcoup⟩ | ⟨_, _, hfire⟩
      · -- v = maxRepNat: the truncating clamp shrinks the magnitude, so n ≤ result.
        subst hv
        refine ⟨?_, ?_⟩
        · rw [h_result_signed, h_truth_signed]
          nlinarith [h10ze'_nn, hzm_q_gt]
        · rw [h_abs_neg, h_truth_signed, h_result_signed]
          rw [show -((maxRepNat : ℚ) * 10 ^ ze') - -((zm.toNat : ℚ) * 10 ^ ze')
                = ((zm.toNat : ℚ) - maxRepNat) * 10 ^ ze' from by ring,
              show -(-((zm.toNat : ℚ) * 10 ^ ze')) * (10 / (2 ^ 63 + 2 : ℚ))
                = ((zm.toNat : ℚ) * (10 / (2 ^ 63 + 2 : ℚ))) * 10 ^ ze' from by ring]
          apply mul_lt_mul_of_pos_right _ h10ze'_pos
          rw [h_denom_val, show (zm.toNat : ℚ) * (10 / (maxRepCuspTarget : ℚ))
                = 10 * (zm.toNat : ℚ) / maxRepCuspTarget from by ring,
              lt_div_iff₀ (by norm_num : (0 : ℚ) < maxRepCuspTarget)]
          nlinarith [hzm_q_gt, hzm_q_le3]
      · -- v = maxRepNat + 3: the pushed-guard decision is dead for negatives,
        -- so zm = maxRepUp and the result is exact.
        subst hv
        have hzm_eq : zm.toNat = maxRepUp.toNat := by
          rcases hcoup with ⟨h, _⟩ | ⟨_, h⟩
          · exact h
          · exfalso
            have hr_ne1 : (g.pushOverflow zm .upward).round .upward ≠ 1 := by
              intro h1
              have hsru := (round_upward_eq_one_iff _).mp h1
              have hsb : (g.pushOverflow zm .upward).sbit_ = g.sbit_ := by
                simp only [Guard.pushOverflow, Guard.push]
                split_ifs <;> rfl
              have hsb' := hsru.1
              rw [hsb, h_g_sbit, h_zn] at hsb'
              exact Bool.noConfusion hsb'
            have hr_ne0 : (g.pushOverflow zm .upward).round .upward ≠ 0 := by
              unfold Guard.round
              split_ifs <;> decide
            rw [show ((g.pushOverflow zm .upward).round .upward == 1) = false from
                  beq_eq_false_iff_ne.mpr hr_ne1,
                show ((g.pushOverflow zm .upward).round .upward == 0) = false from
                  beq_eq_false_iff_ne.mpr hr_ne0,
                Bool.false_and] at h
            exact absurd h (by decide)
        have hzm_q_eq : (zm.toNat : ℚ) = maxRepNat + 3 := by
          rw [hzm_eq, show maxRepUp.toNat = maxRepUpNat from rfl]; norm_num
        have h_req : result.toRat = n.toRat := by
          rw [h_result_signed, h_truth_signed, hzm_q_eq]
        refine ⟨le_of_eq h_req.symm, ?_⟩
        rw [h_req, show n.toRat - n.toRat = 0 from by ring]
        exact h_allow_pos
      · -- v = maxRepNat + 13: requires the dead plain-guard decision.
        rw [h_gbool_false] at hfire
        exact absurd hfire (by decide)
    · -- ===== non-negative n: the pushed-guard decision fires at the interior =====
      have h_n_nonneg : n.negative_ = false := Bool.not_eq_true _ |>.mp h_zn
      have h_truth_nn : 0 ≤ n.toRat := Number.toRat_nonneg_of_nonnegative n h_n_nonneg
      have h_abs_eq : |n.toRat| = n.toRat := abs_of_nonneg h_truth_nn
      have h_truth_signed : n.toRat = (zm.toNat : ℚ) * 10 ^ ze' := by
        have h1 := habs_n_eq'
        rw [h_abs_eq] at h1; exact h1
      have h_result_nn : result.negative_ = false := by rw [h_sign]; exact h_n_nonneg
      have h_result_signed : result.toRat = v * 10 ^ ze' := by
        have h2 := Number.toRat_of_nonneg result h_result_nn
        rw [h2, h_result_abs_via_self, hv_val]
      rcases hv_cases with ⟨hv, hzm_lt_up, hbool⟩ | ⟨hv, hcoup⟩ | ⟨_, _, hfire⟩
      · -- v = maxRepNat: impossible for non-negative (the pushed-guard decision fires).
        exfalso
        obtain ⟨hdig_pos, hsb⟩ := pushOverflow_cusp_interior_facts g zm .upward h_zm_le_rep hzm_lt_up
        have h1 : (g.pushOverflow zm .upward).round .upward = 1 :=
          (round_upward_eq_one_iff _).mpr
            ⟨by rw [hsb, h_g_sbit]; exact h_n_nonneg, Or.inl hdig_pos⟩
        rw [h1, show ((1 : Int) == 1) = true from rfl, Bool.true_or] at hbool
        exact Bool.noConfusion hbool
      · -- v = maxRepNat + 3: either exact (zm = maxRepUp) or the fired clamp upward.
        subst hv
        rcases hcoup with ⟨hzm_eq, _⟩ | ⟨_, _⟩
        · -- zm = maxRepUp: result = n exactly.
          have hzm_q_eq : (zm.toNat : ℚ) = maxRepNat + 3 := by
            rw [hzm_eq, show maxRepUp.toNat = maxRepUpNat from rfl]; norm_num
          have h_req : result.toRat = n.toRat := by
            rw [h_result_signed, h_truth_signed, hzm_q_eq]
          refine ⟨le_of_eq h_req.symm, ?_⟩
          rw [h_req, show n.toRat - n.toRat = 0 from by ring]
          exact h_allow_pos
        · -- the fired clamp to maxRepUp: result above the truth, error < 3.
          refine ⟨?_, ?_⟩
          · rw [h_result_signed, h_truth_signed]
            nlinarith [h10ze'_nn, hzm_q_le3]
          · rw [h_abs_eq, h_truth_signed, h_result_signed]
            rw [show ((maxRepNat : ℚ) + 3) * 10 ^ ze' - (zm.toNat : ℚ) * 10 ^ ze'
                  = (((maxRepNat : ℚ) + 3) - (zm.toNat : ℚ)) * 10 ^ ze' from by ring,
                show (zm.toNat : ℚ) * 10 ^ ze' * (10 / (2 ^ 63 + 2 : ℚ))
                  = ((zm.toNat : ℚ) * (10 / (2 ^ 63 + 2 : ℚ))) * 10 ^ ze' from by ring]
            apply mul_lt_mul_of_pos_right _ h10ze'_pos
            rw [h_denom_val, show (zm.toNat : ℚ) * (10 / (maxRepCuspTarget : ℚ))
                  = 10 * (zm.toNat : ℚ) / maxRepCuspTarget from by ring,
                lt_div_iff₀ (by norm_num : (0 : ℚ) < maxRepCuspTarget)]
            nlinarith [hzm_q_gt, hzm_q_le3]
      · -- v = maxRepNat + 13: requires the dead plain-guard decision.
        rw [h_gbool_false] at hfire
        exact absurd hfire (by decide)

end XRPL.Model.Protocol
