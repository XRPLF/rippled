import XRPL.Properties.Protocol.Number.Common.Notation
import XRPL.Common.Approx
import XRPL.Properties.Protocol.Number.Common.Helpers
import XRPL.Properties.Protocol.Number.Mul.Upward.AlgorithmicFacts

set_option linter.style.longLine false
set_option linter.style.emptyLine false

namespace XRPL.Model.Protocol

set_option maxHeartbeats 800000 in
-- nlinarith over large constants (2^63+2) needs extra heartbeats
/-- Relative-error bound for `Number.operator_mul` under `.upward` rounding.
The bound `10/(2^63 + 2)` is twice the `.to_nearest` bound (sign-flipped `.downward`). -/
theorem operator_mul_rounding_bound_upward (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (hok : Number.operator_mul x y .upward = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    x.toRat * y.toRat ≤ result.toRat ∧
    result.toRat - x.toRat * y.toRat < |x.toRat * y.toRat| * (10 / (2 ^ 63 + 2 : ℚ)) := by
  obtain ⟨zm, ze', f, g, res_pos, hzm_ge, hzm_le_maxRep, hf_nn, hf_lt1, h_floor_constraint,
          habs_xy_eq, h_rup_pos, h_result_abs, hres_pos_mant_ne, hf_rep, h_sign, h_g_sbit⟩ :=
    operator_mul_algorithmic_facts_upward x y result hx hy hx_mant_ne hy_mant_ne hok hresult
  have h10ze'_pos : (0 : ℚ) < 10 ^ ze' := zpow_pos (by norm_num) _
  have h10ze'_nn : (0 : ℚ) ≤ 10 ^ ze' := le_of_lt h10ze'_pos
  have hzm_q_ge : (mantissaFloor : ℚ) ≤ (zm.toNat : ℚ) := by exact_mod_cast hzm_ge
  have hzm_q_le : (zm.toNat : ℚ) ≤ maxRepNat := by
    have : (zm.toNat : ℚ) ≤ ((maxRep.toNat : ℕ) : ℚ) := by exact_mod_cast hzm_le_maxRep
    have hmrq : ((maxRep.toNat : ℕ) : ℚ) = maxRepNat := by
      rw [maxRep_val]; norm_num
    rw [hmrq] at this; exact this
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
  -- Case-split: shouldRoundUp_upward or not.
  by_cases h_sru : g.shouldRoundUp_upward
  · -- ===== ROUND UP =====
    have hf_pos : 0 < f := represents_pos_of_shouldRoundUp_upward g f hf_rep' h_sru
    -- sbit = false, so zn = false, so truth ≥ 0.
    have h_zn_false : (x.negative_ != y.negative_) = false := by
      rw [← h_g_sbit]; exact h_sru.1
    have h_result_pos : result.negative_ = false := by rw [h_sign, h_zn_false]
    -- Truth is non-negative.
    have h_xy_neg_eq : x.negative_ = y.negative_ := by
      by_contra h_ne
      have : (x.negative_ != y.negative_) = true := by
        simp only [bne_iff_ne, ne_eq]; exact h_ne
      rw [this] at h_zn_false; exact Bool.noConfusion h_zn_false
    have h_truth_nn : 0 ≤ x.toRat * y.toRat := (toRat_mul_sign x y).1 h_xy_neg_eq
    have h_abs_truth_eq : |x.toRat * y.toRat| = x.toRat * y.toRat :=
      abs_of_nonneg h_truth_nn
    -- result.toRat = + res_pos.mantissa * 10^res_pos.exp.
    have h_result_pos_val : result.toRat
        = (res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_ := by
      have := Number.toRat_of_nonneg result h_result_pos
      rw [this, h_result_abs_via_self]
    -- Sub-case: cusp or non-cusp.
    by_cases h_cusp : zm = maxRep
    · -- ===== CUSP =====
      have h_cusp_val := doRoundUp_value_upward_roundUp_cusp g false zm ze' h_cusp h_sru
        "Number::multiplication overflow" res_pos h_rup_pos hres_pos_mant_ne
      simp only at h_cusp_val
      have hzm_eq_maxRep_q : (zm.toNat : ℚ) = maxRepNat := by
        rw [show zm.toNat = maxRep.toNat from by rw [h_cusp], maxRep_val]; norm_num
      -- truth = (zm + f)·10^ze' = (maxRepNat + f)·10^ze'.
      have h_truth_signed : x.toRat * y.toRat
          = ((zm.toNat : ℚ) + f) * 10 ^ ze' := by
        have h1 : |x.toRat * y.toRat| = ((zm.toNat : ℚ) + f) * 10 ^ ze' := habs_xy_eq
        rw [h_abs_truth_eq] at h1; exact h1
      -- result = maxRepCuspTarget · 10^ze'.
      have h_result_signed : result.toRat
          = (maxRepCuspTarget : ℚ) * 10 ^ ze' := by
        rw [h_result_pos_val, h_cusp_val]
      -- Direction: truth ≤ result.
      have h_direction : x.toRat * y.toRat ≤ result.toRat := by
        rw [h_result_signed, h_truth_signed]
        have h_inner : ((zm.toNat : ℚ) + f) ≤ maxRepCuspTarget := by
          rw [hzm_eq_maxRep_q]; linarith [hf_lt1]
        nlinarith [h10ze'_nn, h_inner]
      -- Magnitude: result - truth < |truth| · 10/(2^63+2).
      have h_magnitude : result.toRat - x.toRat * y.toRat
          < |x.toRat * y.toRat| * (10 / ((2 ^ 63 + 2 : ℚ))) := by
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
        "Number::multiplication overflow" res_pos h_rup_pos hres_pos_mant_ne
      simp only at h_nc_val
      have h_truth_signed : x.toRat * y.toRat
          = ((zm.toNat : ℚ) + f) * 10 ^ ze' := by
        have h1 : |x.toRat * y.toRat| = ((zm.toNat : ℚ) + f) * 10 ^ ze' := habs_xy_eq
        rw [h_abs_truth_eq] at h1; exact h1
      have h_result_signed : result.toRat
          = ((zm.toNat : ℚ) + 1) * 10 ^ ze' := by
        rw [h_result_pos_val, h_nc_val]
      -- Direction: truth ≤ result.
      have h_direction : x.toRat * y.toRat ≤ result.toRat := by
        rw [h_result_signed, h_truth_signed]
        have h_inner : ((zm.toNat : ℚ) + f) ≤ ((zm.toNat : ℚ) + 1) := by linarith
        nlinarith [h10ze'_nn, h_inner]
      -- Magnitude.
      have h_magnitude : result.toRat - x.toRat * y.toRat
          < |x.toRat * y.toRat| * (10 / ((2 ^ 63 + 2 : ℚ))) := by
        rw [h_abs_truth_eq, h_result_signed, h_truth_signed]
        have h_lhs_eq : ((zm.toNat : ℚ) + 1) * 10 ^ ze' -
            ((zm.toNat : ℚ) + f) * 10 ^ ze' = (1 - f) * 10 ^ ze' := by ring
        have h_rhs_arrange : (((zm.toNat : ℚ) + f) * 10 ^ ze') *
            (10 / ((2 ^ 63 + 2 : ℚ)))
            = ((zm.toNat : ℚ) + f) * 10 ^ ze' * (10 / ((2 ^ 63 + 2 : ℚ))) := by ring
        rw [h_lhs_eq, h_rhs_arrange]
        -- Sub-case: zm = floor or zm > floor.
        by_cases h_floor : zm.toNat = mantissaFloor
        · have hf_ge : (8 : ℚ) / 10 ≤ f := h_floor_constraint h_floor
          have hzm_eq_floor_q : (zm.toNat : ℚ) = mantissaFloor := by
            rw [h_floor]; norm_num
          have h_inner : (1 - f) < (((zm.toNat : ℚ) + f)) * (10 / ((2 ^ 63 + 2 : ℚ))) := by
            rw [hzm_eq_floor_q, h_denom_val]
            rw [show ((mantissaFloor : ℚ) + f) * (10 / (maxRepCuspTarget : ℚ))
                  = 10 * (mantissaFloor + f) / maxRepCuspTarget by ring]
            rw [lt_div_iff₀ (by norm_num : (0 : ℚ) < maxRepCuspTarget)]
            nlinarith [hf_ge, hf_lt1]
          calc (1 - f) * 10 ^ ze'
              < (((zm.toNat : ℚ) + f)) * (10 / ((2 ^ 63 + 2 : ℚ))) * 10 ^ ze' :=
                mul_lt_mul_of_pos_right h_inner h10ze'_pos
            _ = (((zm.toNat : ℚ) + f)) * 10 ^ ze' * (10 / ((2 ^ 63 + 2 : ℚ))) := by ring
        · have h_zm_gt : mantissaFloorSucc ≤ zm.toNat := by omega
          have hzm_q_gt : (mantissaFloorSucc : ℚ) ≤ (zm.toNat : ℚ) := by exact_mod_cast h_zm_gt
          have h_inner : (1 - f) < (((zm.toNat : ℚ) + f)) * (10 / ((2 ^ 63 + 2 : ℚ))) := by
            rw [h_denom_val]
            rw [show (((zm.toNat : ℚ) + f)) * (10 / (maxRepCuspTarget : ℚ))
                  = 10 * ((zm.toNat : ℚ) + f) / maxRepCuspTarget by ring]
            rw [lt_div_iff₀ (by norm_num : (0 : ℚ) < maxRepCuspTarget)]
            nlinarith [hzm_q_gt, hf_nn, hf_lt1, hf_pos]
          calc (1 - f) * 10 ^ ze'
              < (((zm.toNat : ℚ) + f)) * (10 / ((2 ^ 63 + 2 : ℚ))) * 10 ^ ze' :=
                mul_lt_mul_of_pos_right h_inner h10ze'_pos
            _ = (((zm.toNat : ℚ) + f)) * 10 ^ ze' * (10 / ((2 ^ 63 + 2 : ℚ))) := by ring
      exact ⟨h_direction, h_magnitude⟩
  · -- ===== NO ROUND UP (truncate) =====
    have h_tr_val := doRoundUp_value_upward_truncate g false zm ze' h_sru "Number::multiplication overflow" res_pos h_rup_pos hres_pos_mant_ne
    simp only at h_tr_val
    -- shouldRoundUp_upward fails: sbit=true OR (digits=0 ∧ xbit=false).
    -- Direction depends on truth sign.
    by_cases h_zn : (x.negative_ != y.negative_) = true
    · -- zn = true. Truth ≤ 0. shouldRoundUp_upward fails because sbit=true.
      have h_g_sbit_true : g.sbit_ = true := by rw [h_g_sbit]; exact h_zn
      have h_truth_nonpos : x.toRat * y.toRat ≤ 0 := by
        have h_sign_xy := toRat_mul_sign x y
        apply h_sign_xy.2
        intro h_eq
        have : (x.negative_ != y.negative_) = false := by simp [h_eq]
        rw [this] at h_zn; exact Bool.noConfusion h_zn
      have h_abs_truth_eq_neg : |x.toRat * y.toRat| = -(x.toRat * y.toRat) :=
        abs_of_nonpos h_truth_nonpos
      have h_truth_signed : x.toRat * y.toRat
          = -(((zm.toNat : ℚ) + f) * 10 ^ ze') := by
        have h1 : |x.toRat * y.toRat| = ((zm.toNat : ℚ) + f) * 10 ^ ze' := habs_xy_eq
        rw [h_abs_truth_eq_neg] at h1
        have h2 : x.toRat * y.toRat = -(((zm.toNat : ℚ) + f) * 10 ^ ze') := by
          rw [← h1]; ring
        exact h2
      have h_result_neg : result.negative_ = true := by rw [h_sign]; exact h_zn
      have h_result_neg_val : result.toRat
          = -((res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_) := by
        have := Number.toRat_of_neg result h_result_neg
        rw [this, h_result_abs_via_self]
      have h_result_signed : result.toRat = -((zm.toNat : ℚ) * 10 ^ ze') := by
        rw [h_result_neg_val, h_tr_val]
      -- Direction: truth = -(zm+f)·10^ze' ≤ -zm·10^ze' = result.
      have h_direction : x.toRat * y.toRat ≤ result.toRat := by
        rw [h_result_signed, h_truth_signed]
        nlinarith [h10ze'_nn, hf_nn]
      -- Magnitude: result - truth = f·10^ze' < (zm+f)·10^ze' · 10/(2^63+2).
      have h_magnitude : result.toRat - x.toRat * y.toRat
          < |x.toRat * y.toRat| * (10 / ((2 ^ 63 + 2 : ℚ))) := by
        rw [h_abs_truth_eq_neg, h_result_signed, h_truth_signed]
        have h_lhs_eq : -((zm.toNat : ℚ) * 10 ^ ze') -
            -(((zm.toNat : ℚ) + f) * 10 ^ ze') = f * 10 ^ ze' := by ring
        have h_rhs_arrange : -(-(((zm.toNat : ℚ) + f) * 10 ^ ze')) *
            (10 / ((2 ^ 63 + 2 : ℚ)))
            = ((zm.toNat : ℚ) + f) * 10 ^ ze' * (10 / ((2 ^ 63 + 2 : ℚ))) := by ring
        rw [h_lhs_eq, h_rhs_arrange]
        -- Show: f < ((zm:ℚ) + f) · 10/(2^63+2).
        have h_inner : f < (((zm.toNat : ℚ) + f)) * (10 / ((2 ^ 63 + 2 : ℚ))) := by
          rw [h_denom_val]
          rw [show (((zm.toNat : ℚ) + f)) * (10 / (maxRepCuspTarget : ℚ))
                = 10 * ((zm.toNat : ℚ) + f) / maxRepCuspTarget by ring]
          rw [lt_div_iff₀ (by norm_num : (0 : ℚ) < maxRepCuspTarget)]
          have h_key : f * (mantissaFloor : ℚ) < (zm.toNat : ℚ) := by
            nlinarith [hf_lt1, hzm_q_ge]
          nlinarith [h_key]
        calc f * 10 ^ ze'
            < (((zm.toNat : ℚ) + f)) * (10 / ((2 ^ 63 + 2 : ℚ))) * 10 ^ ze' :=
              mul_lt_mul_of_pos_right h_inner h10ze'_pos
          _ = (((zm.toNat : ℚ) + f)) * 10 ^ ze' * (10 / ((2 ^ 63 + 2 : ℚ))) := by ring
      exact ⟨h_direction, h_magnitude⟩
    · -- zn = false. Truth ≥ 0. sbit=false, so shouldRoundUp_upward fails ⇒ guard = 0 ⇒ f = 0.
      have h_zn_false : (x.negative_ != y.negative_) = false := Bool.not_eq_true _ |>.mp h_zn
      have h_g_sbit_false : g.sbit_ = false := by rw [h_g_sbit]; exact h_zn_false
      -- ¬ shouldRoundUp_upward ∧ sbit_=false ⇒ ¬(digits > 0 ∨ xbit_=true).
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
      have h_xy_neg_eq : x.negative_ = y.negative_ := by
        by_contra h_ne
        have : (x.negative_ != y.negative_) = true := by
          simp only [bne_iff_ne, ne_eq]; exact h_ne
        rw [this] at h_zn_false; exact Bool.noConfusion h_zn_false
      have h_truth_nn : 0 ≤ x.toRat * y.toRat := (toRat_mul_sign x y).1 h_xy_neg_eq
      have h_abs_truth_eq : |x.toRat * y.toRat| = x.toRat * y.toRat :=
        abs_of_nonneg h_truth_nn
      have h_truth_signed : x.toRat * y.toRat = ((zm.toNat : ℚ) + f) * 10 ^ ze' := by
        have h1 : |x.toRat * y.toRat| = ((zm.toNat : ℚ) + f) * 10 ^ ze' := habs_xy_eq
        rw [h_abs_truth_eq] at h1; exact h1
      have h_result_pos : result.negative_ = false := by rw [h_sign]; exact h_zn_false
      have h_result_pos_val : result.toRat
          = (res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_ := by
        have := Number.toRat_of_nonneg result h_result_pos
        rw [this, h_result_abs_via_self]
      have h_result_signed : result.toRat = (zm.toNat : ℚ) * 10 ^ ze' := by
        rw [h_result_pos_val, h_tr_val]
      -- With f = 0: truth = zm·10^ze' = result.
      have h_truth_eq_result : x.toRat * y.toRat = result.toRat := by
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

end XRPL.Model.Protocol
