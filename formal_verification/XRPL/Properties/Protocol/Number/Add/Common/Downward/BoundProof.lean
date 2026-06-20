import XRPL.Properties.Protocol.Number.Common.Notation
import XRPL.Properties.Protocol.Number.Add.Common.Downward.AlgorithmicFacts
import XRPL.Properties.Protocol.Number.Add.Common.Downward.DiffSignTight
import XRPL.Properties.Protocol.Number.Common.Helpers


namespace XRPL.Model.Protocol

set_option maxHeartbeats 1600000 in
-- three-way case split on shouldRoundUp_downward × cusp; each branch runs nlinarith over
-- large constants (2^63 + 2 ≈ 9.2e18)

theorem operator_add_rounding_bound_same_sign_downward (x y : Number) (result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (h_same_sign : x.negative_ = y.negative_)
    (h_not_zero : ¬ x.operator_eq y.operator_neg)
    (hok : Number.operator_add x y .downward = .ok result) :
    |result.toRat - (x.toRat + y.toRat)| < |x.toRat + y.toRat| * (10 / (2 ^ 63 + 2 : ℚ)) := by
  obtain ⟨zm, ze', f, g, res_pos, hzm_ge, hzm_le_maxRep, hf_nn, hf_lt1, h_floor_constraint,
          habs_xy_eq, h_rup_pos, h_result_abs, hres_pos_mant_ne, hf_rep, h_sign, _h_sbit_pos, _, _, _, _⟩ :=
    operator_add_algorithmic_facts_same_sign_downward x y result hx hy hx_mant_ne hy_mant_ne
      h_same_sign h_not_zero hok
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
  rw [h_abs_diff_eq, h_result_abs, habs_xy_eq]
  -- Now: |(res_pos.mantissa : ℚ) * 10^res_pos.exponent_ - (zm + f) * 10^ze'|
  --        ≤ (zm + f) * 10^ze' * (10/(2^63+2))
  have h10ze'_pos : (0 : ℚ) < 10 ^ ze' := zpow_pos (by norm_num) _
  have h10ze'_nn : (0 : ℚ) ≤ 10 ^ ze' := le_of_lt h10ze'_pos
  have hzm_q_ge : (mantissaFloor : ℚ) ≤ (zm.toNat : ℚ) := by exact_mod_cast hzm_ge
  have h_denom_val : ((2 ^ 63 + 2 : ℚ)) = maxRepCuspTarget := by norm_num
  have h_denom_pos : (0 : ℚ) < ((2 ^ 63 + 2 : ℚ)) := by rw [h_denom_val]; norm_num
  by_cases h_zm_le_rep : zm.toNat ≤ maxRep.toNat
  · -- ===== IN RANGE: zm ≤ maxRep =====
    by_cases h_sru : g.shouldRoundUp_downward
    · -- ROUND UP
      have hf_pos : 0 < f := represents_pos_of_shouldRoundUp_downward g f hf_rep h_sru
      by_cases h_cusp : zm = maxRep
      · -- CUSP (zm = maxRep)
        have h_cusp_val := doRoundUp_value_downward_roundUp_cusp g false zm ze' h_cusp h_sru
          "Number::addition overflow" res_pos h_rup_pos hres_pos_mant_ne
        simp only at h_cusp_val
        rw [h_cusp_val]
        have hzm_eq_maxRep_q : (zm.toNat : ℚ) = maxRepNat := by
          rw [show zm.toNat = maxRep.toNat from by rw [h_cusp], maxRep_val]; norm_num
        rw [hzm_eq_maxRep_q]
        have h_diff : (maxRepCuspTarget : ℚ) * 10 ^ ze' - (maxRepNat + f) * 10 ^ ze'
            = (3 - f) * 10 ^ ze' := by ring
        rw [h_diff, abs_mul, abs_of_nonneg h10ze'_nn]
        have h_abs : |(3 - f : ℚ)| = 3 - f := by
          rw [abs_of_nonneg (by linarith : (0 : ℚ) ≤ 3 - f)]
        rw [h_abs]
        have h_final : (3 - f) < (maxRepNat + f) * (10 / ((2 ^ 63 + 2 : ℚ))) := by
          rw [h_denom_val]
          rw [show (maxRepNat + f) * (10 / (maxRepCuspTarget : ℚ))
                = 10 * (maxRepNat + f) / maxRepCuspTarget by ring]
          rw [lt_div_iff₀ (by norm_num : (0 : ℚ) < maxRepCuspTarget)]
          nlinarith [hf_nn, hf_lt1]
        calc (3 - f) * 10 ^ ze'
            < (maxRepNat + f) * (10 / ((2 ^ 63 + 2 : ℚ))) * 10 ^ ze' :=
              mul_lt_mul_of_pos_right h_final h10ze'_pos
          _ = (maxRepNat + f) * 10 ^ ze' * (10 / ((2 ^ 63 + 2 : ℚ))) := by ring
      · -- NON-CUSP round up
        have hzm_lt_maxRep : zm.toNat < maxRep.toNat := by
          have : zm.toNat ≠ maxRep.toNat := fun heq => h_cusp (UInt64.toNat_inj.mp heq)
          omega
        have h_no_cusp : zm.toNat + 1 ≤ maxRep.toNat := by omega
        have h_nc_val := doRoundUp_value_downward_roundUp_noCusp g false zm ze' h_sru h_no_cusp
          "Number::addition overflow" res_pos h_rup_pos hres_pos_mant_ne
        simp only at h_nc_val
        rw [h_nc_val]
        have h_diff : ((zm.toNat : ℚ) + 1) * 10 ^ ze' - ((zm.toNat : ℚ) + f) * 10 ^ ze'
            = (1 - f) * 10 ^ ze' := by ring
        rw [h_diff, abs_mul, abs_of_nonneg h10ze'_nn]
        have h_abs : |(1 - f : ℚ)| = 1 - f := by
          rw [abs_of_nonneg (by linarith : (0 : ℚ) ≤ 1 - f)]
        rw [h_abs]
        have h_inner : (1 - f) < (((zm.toNat : ℚ) + f)) * (10 / ((2 ^ 63 + 2 : ℚ))) :=
          one_sub_f_lt_releps hzm_ge h_floor_constraint hf_pos hf_lt1
        exact releps_lift h_inner h10ze'_pos
    · -- NO ROUND UP (truncate)
      have h_tr_val := doRoundUp_value_downward_truncate g false zm ze' h_sru h_zm_le_rep
        "Number::addition overflow" res_pos h_rup_pos hres_pos_mant_ne
      simp only at h_tr_val
      rw [h_tr_val]
      have h_diff : (zm.toNat : ℚ) * 10 ^ ze' - ((zm.toNat : ℚ) + f) * 10 ^ ze'
          = -f * 10 ^ ze' := by ring
      rw [h_diff, abs_mul, abs_of_nonneg h10ze'_nn]
      have h_abs : |(-f : ℚ)| = f := by rw [abs_neg, abs_of_nonneg hf_nn]
      rw [h_abs]
      have h_inner : f < (((zm.toNat : ℚ) + f)) * (10 / ((2 ^ 63 + 2 : ℚ))) := f_lt_releps hzm_q_ge hf_lt1
      exact releps_lift h_inner h10ze'_pos
  · -- ===== CUSP RANGE: maxRep < zm ≤ maxRepUp =====
    push_neg at h_zm_le_rep
    obtain ⟨v, hv_val, hv_cases⟩ := doRoundUp_value_cuspRange_cases g zm ze' .downward
      h_zm_le_rep hzm_le_maxRep "Number::addition overflow" res_pos h_rup_pos hres_pos_mant_ne
    rw [hv_val]
    obtain ⟨hzm_q_gt, hzm_q_le3⟩ := cusp_zm_qbounds h_zm_le_rep hzm_le_maxRep
    rcases hv_cases with ⟨hv, _, _⟩ | ⟨hv, _⟩ | ⟨hv, hzm_eq, hfire⟩
    · -- v = maxRepNat: truncate clamp; err = zm + f − maxRepNat ∈ (0, 4).
      subst hv
      rw [show (maxRepNat : ℚ) * 10 ^ ze' - ((zm.toNat : ℚ) + f) * 10 ^ ze'
            = -((((zm.toNat : ℚ) + f) - maxRepNat) * 10 ^ ze') from by ring,
          abs_neg, abs_mul,
          abs_of_nonneg (by linarith [hf_nn] : (0 : ℚ) ≤ ((zm.toNat : ℚ) + f) - maxRepNat),
          abs_of_nonneg h10ze'_nn]
      have h_inner : ((zm.toNat : ℚ) + f) - maxRepNat
          < ((zm.toNat : ℚ) + f) * (10 / ((2 ^ 63 + 2 : ℚ))) :=
        cusp_err_lt_releps hzm_q_le3 hf_lt1
      calc (((zm.toNat : ℚ) + f) - maxRepNat) * 10 ^ ze'
          < ((zm.toNat : ℚ) + f) * (10 / ((2 ^ 63 + 2 : ℚ))) * 10 ^ ze' :=
            mul_lt_mul_of_pos_right h_inner h10ze'_pos
        _ = ((zm.toNat : ℚ) + f) * 10 ^ ze' * (10 / ((2 ^ 63 + 2 : ℚ))) := by ring
    · -- v = maxRepNat + 3: |err| ≤ 3.
      subst hv
      rw [show ((maxRepNat : ℚ) + 3) * 10 ^ ze' - ((zm.toNat : ℚ) + f) * 10 ^ ze'
            = (((maxRepNat : ℚ) + 3) - ((zm.toNat : ℚ) + f)) * 10 ^ ze' from by ring,
          abs_mul, abs_of_nonneg h10ze'_nn]
      have h_abs3 : |((maxRepNat : ℚ) + 3) - ((zm.toNat : ℚ) + f)| ≤ 3 := by
        rw [abs_le]
        constructor
        · linarith [hzm_q_le3, hf_lt1]
        · linarith [hzm_q_gt, hf_nn]
      have h_inner : (3 : ℚ) < ((zm.toNat : ℚ) + f) * (10 / ((2 ^ 63 + 2 : ℚ))) := by
        rw [h_denom_val, show ((zm.toNat : ℚ) + f) * (10 / (maxRepCuspTarget : ℚ))
              = 10 * ((zm.toNat : ℚ) + f) / maxRepCuspTarget from by ring,
            lt_div_iff₀ (by norm_num : (0 : ℚ) < maxRepCuspTarget)]
        nlinarith [hzm_q_gt, hf_nn]
      calc |((maxRepNat : ℚ) + 3) - ((zm.toNat : ℚ) + f)| * 10 ^ ze'
          ≤ 3 * 10 ^ ze' := mul_le_mul_of_nonneg_right h_abs3 h10ze'_nn
        _ < ((zm.toNat : ℚ) + f) * (10 / ((2 ^ 63 + 2 : ℚ))) * 10 ^ ze' :=
            mul_lt_mul_of_pos_right h_inner h10ze'_pos
        _ = ((zm.toNat : ℚ) + f) * 10 ^ ze' * (10 / ((2 ^ 63 + 2 : ℚ))) := by ring
    · -- v = maxRepNat + 13: zm = maxRepUp and the fire forces f > 0 (downward).
      subst hv
      have hzm_q_eq : (zm.toNat : ℚ) = maxRepNat + 3 := by
        rw [hzm_eq, show maxRepUp.toNat = maxRepUpNat from rfl]; norm_num
      have h_fire1 : g.round .downward = 1 := by
        rw [Bool.or_eq_true] at hfire
        rcases hfire with h | h
        · exact beq_iff_eq.mp h
        · rw [Bool.and_eq_true] at h
          have h0 : g.round .downward = 0 := beq_iff_eq.mp h.1
          exfalso
          unfold Guard.round at h0
          by_cases he : g.empty
          · rw [if_pos he] at h0; norm_num at h0
          · rw [if_neg he] at h0
            by_cases hs : g.sbit_ = true
            · rw [if_pos hs] at h0
              by_cases hd : (g.digits_ > 0 || g.xbit_) = true
              · rw [if_pos hd] at h0; norm_num at h0
              · rw [if_neg hd] at h0; norm_num at h0
            · rw [if_neg hs] at h0; norm_num at h0
      have h_sru : g.shouldRoundUp_downward := (round_downward_eq_one_iff g).mp h_fire1
      have hf_pos : 0 < f := represents_pos_of_shouldRoundUp_downward g f hf_rep h_sru
      rw [hzm_q_eq, show ((maxRepNat : ℚ) + 13) * 10 ^ ze' - (((maxRepNat : ℚ) + 3) + f) * 10 ^ ze'
            = (10 - f) * 10 ^ ze' from by ring,
          abs_mul, abs_of_nonneg (by linarith [hf_lt1] : (0 : ℚ) ≤ 10 - f),
          abs_of_nonneg h10ze'_nn]
      have h_inner : 10 - f < (((maxRepNat : ℚ) + 3) + f) * (10 / ((2 ^ 63 + 2 : ℚ))) := by
        rw [h_denom_val, show (((maxRepNat : ℚ) + 3) + f) * (10 / (maxRepCuspTarget : ℚ))
              = 10 * (((maxRepNat : ℚ) + 3) + f) / maxRepCuspTarget from by ring,
            lt_div_iff₀ (by norm_num : (0 : ℚ) < maxRepCuspTarget)]
        nlinarith [hf_pos]
      calc (10 - f) * 10 ^ ze'
          < (((maxRepNat : ℚ) + 3) + f) * (10 / ((2 ^ 63 + 2 : ℚ))) * 10 ^ ze' :=
            mul_lt_mul_of_pos_right h_inner h10ze'_pos
        _ = (((maxRepNat : ℚ) + 3) + f) * 10 ^ ze' * (10 / ((2 ^ 63 + 2 : ℚ))) := by ring

theorem operator_add_rounding_bound_downward_tight (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (h_not_zero : ¬ x.operator_eq y.operator_neg)
    (hok : Number.operator_add x y .downward = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    |result.toRat - (x.toRat + y.toRat)| < |x.toRat + y.toRat| * (11 / (2 ^ 63 - 18 : ℚ)) := by
  by_cases h_sign : x.negative_ = y.negative_
  · have hss := operator_add_rounding_bound_same_sign_downward x y result hx hy hx_mant_ne hy_mant_ne
      h_sign h_not_zero hok
    have htruth_nn : 0 ≤ |x.toRat + y.toRat| := abs_nonneg _
    have hconst : (10 / (2 ^ 63 + 2 : ℚ)) ≤ (11 / (2 ^ 63 - 18 : ℚ)) := by norm_num
    calc |result.toRat - (x.toRat + y.toRat)|
        < |x.toRat + y.toRat| * (10 / (2 ^ 63 + 2 : ℚ)) := hss
      _ ≤ |x.toRat + y.toRat| * (11 / (2 ^ 63 - 18 : ℚ)) :=
          mul_le_mul_of_nonneg_left hconst htruth_nn
  · exact operator_add_rounding_bound_diff_sign_downward_tight x y result hx hy hx_mant_ne hy_mant_ne
      h_sign h_not_zero hok hresult

end XRPL.Model.Protocol
