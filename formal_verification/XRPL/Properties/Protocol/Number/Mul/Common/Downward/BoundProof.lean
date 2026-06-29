import XRPL.Properties.Protocol.Number.Common.Notation
import XRPL.Properties.Approx
import XRPL.Properties.Protocol.Number.Common.Helpers
import XRPL.Properties.Protocol.Number.Mul.Common.Downward.AlgorithmicFacts


namespace XRPL.Model.Protocol

set_option maxHeartbeats 1600000 in
-- nlinarith over large constants (2^63+2) plus the cusp-range legs need extra heartbeats
/-- Relative-error bound for `Number.operator_mul` under `.downward` rounding.
The bound `10/(2^63 + 2)` is twice the `.to_nearest` bound. -/
theorem operator_mul_rounding_bound_downward (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (hok : Number.operator_mul x y .downward = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    result.toRat ≤ x.toRat * y.toRat ∧
    x.toRat * y.toRat - result.toRat < |x.toRat * y.toRat| * (10 / (2 ^ 63 + 2 : ℚ)) := by
  obtain ⟨zm, ze', f, g, res_pos, hzm_ge, hzm_le_maxRep, hf_nn, hf_lt1, h_floor_constraint,
          habs_xy_eq, h_rup_pos, h_result_abs, hres_pos_mant_ne, hf_rep, h_sign, h_g_sbit, _⟩ :=
    operator_mul_algorithmic_facts_downward x y result hx hy hx_mant_ne hy_mant_ne hok hresult
  have h10ze'_pos : (0 : ℚ) < 10 ^ ze' := zpow_pos (by norm_num) _
  have h10ze'_nn : (0 : ℚ) ≤ 10 ^ ze' := le_of_lt h10ze'_pos
  have hzm_q_ge : (mantissaFloor : ℚ) ≤ (zm.toNat : ℚ) := by exact_mod_cast hzm_ge
  have hzm_q_le : (zm.toNat : ℚ) ≤ maxRepNat + 3 := by
    have : (zm.toNat : ℚ) ≤ ((maxRepUp.toNat : ℕ) : ℚ) := by exact_mod_cast hzm_le_maxRep
    have hmrq : ((maxRepUp.toNat : ℕ) : ℚ) = maxRepNat + 3 := by
      rw [show maxRepUp.toNat = maxRepUpNat from rfl]; norm_num
    rw [hmrq] at this; exact this
  -- `|truth| ≥ 0`.
  have h_abs_truth_nn : 0 ≤ ((zm.toNat : ℚ) + f) * 10 ^ ze' := by
    apply mul_nonneg _ h10ze'_nn
    have : (0 : ℚ) ≤ (zm.toNat : ℚ) := by exact_mod_cast Nat.zero_le _
    linarith
  -- Denominator constant.
  have h_denom_val : ((2 ^ 63 + 2 : ℚ)) = maxRepCuspTarget := by norm_num
  have h_denom_pos : (0 : ℚ) < ((2 ^ 63 + 2 : ℚ)) := by rw [h_denom_val]; norm_num
  -- Extract `f`'s structure for direction analysis.
  have hf_rep' := hf_rep
  obtain ⟨x_rep, hx_nn, _hx_lt, hf_eq, hxbit_iff, _hall⟩ := hf_rep
  -- Local helper: convert |result.toRat| identity to signed form.
  -- (Direction depends on result.negative_.)
  have h_result_abs_via_self :
      (result.mantissa_.toNat : ℚ) * 10 ^ result.exponent_
        = (res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_ := by
    have habs := abs_toRat_eq result
    rw [h_result_abs] at habs
    exact habs.symm
  -- Split: in-range (zm ≤ maxRep) vs the cusp range (maxRep < zm ≤ maxRepUp).
  by_cases h_zm_le_rep : zm.toNat ≤ maxRep.toNat
  swap
  · -- ===== CUSP RANGE: maxRep < zm ≤ maxRepUp =====
    have h_zm_gt_rep : maxRep.toNat < zm.toNat := by omega
    obtain ⟨v, hv_val, hv_cases⟩ := doRoundUp_value_cuspRange_cases g zm ze' .downward
      h_zm_gt_rep hzm_le_maxRep "Number::multiplication overflow" res_pos h_rup_pos hres_pos_mant_ne
    have hzm_q_gt : (maxRepNat : ℚ) < (zm.toNat : ℚ) := by
      have : (maxRepNat : ℕ) < zm.toNat := by rw [← maxRep_val]; exact h_zm_gt_rep
      exact_mod_cast this
    have hgP_sbit : (g.pushOverflow zm .downward).sbit_ = g.sbit_ := by
      simp only [Guard.pushOverflow, Guard.push]
      split_ifs <;> rfl
    have h_truth_abs_pos : (0 : ℚ) < ((zm.toNat : ℚ) + f) * 10 ^ ze' := by
      apply mul_pos _ h10ze'_pos
      linarith [hf_nn]
    have h_allow_pos : (0 : ℚ) < |x.toRat * y.toRat| * (10 / (2 ^ 63 + 2 : ℚ)) := by
      rw [habs_xy_eq]
      apply mul_pos h_truth_abs_pos
      rw [h_denom_val]; norm_num
    by_cases h_zn : (x.negative_ != y.negative_) = true
    · -- negative truth: the magnitude rounds up (or stays), the value rounds down.
      have h_g_sbit_true : g.sbit_ = true := by rw [h_g_sbit]; exact h_zn
      have h_result_neg : result.negative_ = true := by rw [h_sign]; exact h_zn
      have h_truth_nonpos : x.toRat * y.toRat ≤ 0 := by
        have h_sign_xy := toRat_mul_sign x y
        apply h_sign_xy.2
        intro h_eq
        have : (x.negative_ != y.negative_) = false := by simp [h_eq]
        rw [this] at h_zn; exact Bool.noConfusion h_zn
      have h_abs_truth_eq_neg : |x.toRat * y.toRat| = -(x.toRat * y.toRat) :=
        abs_of_nonpos h_truth_nonpos
      have h_truth_signed : x.toRat * y.toRat = -(((zm.toNat : ℚ) + f) * 10 ^ ze') := by
        have h1 := habs_xy_eq
        rw [h_abs_truth_eq_neg] at h1; linarith
      have h_result_signed : result.toRat = -(v * 10 ^ ze') := by
        have h2 := Number.toRat_of_neg result h_result_neg
        rw [h2, h_result_abs_via_self, hv_val]
      rcases hv_cases with ⟨hv, hzm_lt_up, hbool⟩ | ⟨hv, hcoup⟩ | ⟨hv, hzm_eq, hfire⟩
      · -- v = maxRepNat: impossible for negatives (the pushed-guard decision fires).
        exfalso
        obtain ⟨hdig_pos, hsb⟩ := pushOverflow_cusp_interior_facts g zm .downward h_zm_gt_rep hzm_lt_up
        have h1 : (g.pushOverflow zm .downward).round .downward = 1 :=
          (round_downward_eq_one_iff _).mpr ⟨by rw [hsb]; exact h_g_sbit_true, Or.inl hdig_pos⟩
        rw [h1, show ((1 : Int) == 1) = true from rfl, Bool.true_or] at hbool
        exact Bool.noConfusion hbool
      · -- v = maxRepNat + 3: dead decision (exact) or the fired interior clamp.
        subst hv
        rcases hcoup with ⟨hzm_eq, hdead⟩ | ⟨hzm_lt_up, _⟩
        · -- dead decision at maxRepUp: f = 0 and the result is exact.
          have hzm_q_eq : (zm.toNat : ℚ) = maxRepNat + 3 := by
            rw [hzm_eq, show maxRepUp.toNat = maxRepUpNat from rfl]; norm_num
          have hf_zero : f = 0 := by
            rcases hdead with hb1 | hb2
            · obtain ⟨h_dig0, h_xbit0⟩ :=
                roundUp_bool_downward_false_content g zm h_g_sbit_true hb1
              exact represents_eq_zero_of_digits_zero_xbit_false h_dig0 h_xbit0 hf_rep'
            · have h_push_sbit : (g.push (maxRepUp % 10)).sbit_ = g.sbit_ := rfl
              obtain ⟨h_dig0', h_xbit0'⟩ :=
                roundUp_bool_downward_false_content (g.push (maxRepUp % 10)) (maxRepUp / 10)
                  (h_push_sbit.trans h_g_sbit_true) hb2
              obtain ⟨h_dig0, h_xbit0⟩ := push_content_empty g (maxRepUp % 10) h_dig0' h_xbit0'
              exact represents_eq_zero_of_digits_zero_xbit_false h_dig0 h_xbit0 hf_rep'
          have h_truth_eq_result : x.toRat * y.toRat = result.toRat := by
            rw [h_result_signed, h_truth_signed, hf_zero, hzm_q_eq]; ring
          refine ⟨le_of_eq h_truth_eq_result.symm, ?_⟩
          rw [← h_truth_eq_result,
              show (x.toRat * y.toRat) - (x.toRat * y.toRat) = 0 from by ring]
          exact h_allow_pos
        · -- fired clamp at the cusp interior: magnitude grows to maxRepNat + 3.
          have hzm_q_lt3 : (zm.toNat : ℚ) ≤ maxRepNat + 2 := by
            have h2 : zm.toNat ≤ 9223372036854775809 := by
              rw [show maxRepUp.toNat = maxRepUpNat from rfl] at hzm_lt_up
              omega
            calc (zm.toNat : ℚ) ≤ ((9223372036854775809 : ℕ) : ℚ) := by exact_mod_cast h2
              _ = maxRepNat + 2 := by norm_num
          refine ⟨?_, ?_⟩
          · rw [h_result_signed, h_truth_signed]
            have h_inner : ((zm.toNat : ℚ) + f) ≤ maxRepNat + 3 := by
              linarith [hf_lt1, hzm_q_lt3]
            nlinarith [h10ze'_nn, h_inner]
          · rw [h_abs_truth_eq_neg, h_result_signed, h_truth_signed]
            rw [show -(((zm.toNat : ℚ) + f) * 10 ^ ze') - -(((maxRepNat : ℚ) + 3) * 10 ^ ze')
                  = (((maxRepNat : ℚ) + 3) - ((zm.toNat : ℚ) + f)) * 10 ^ ze' from by ring,
                show -(-(((zm.toNat : ℚ) + f) * 10 ^ ze')) * (10 / ((2 ^ 63 + 2 : ℚ)))
                  = (((zm.toNat : ℚ) + f) * (10 / (2 ^ 63 + 2 : ℚ))) * 10 ^ ze' from by ring]
            apply mul_lt_mul_of_pos_right _ h10ze'_pos
            rw [h_denom_val, show ((zm.toNat : ℚ) + f) * (10 / (maxRepCuspTarget : ℚ))
                  = 10 * ((zm.toNat : ℚ) + f) / maxRepCuspTarget from by ring,
                lt_div_iff₀ (by norm_num : (0 : ℚ) < maxRepCuspTarget)]
            nlinarith [hzm_q_gt, hf_nn]
      · -- v = maxRepNat + 13: the decision fired, so f > 0.
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
        have hf_pos : 0 < f := represents_pos_of_shouldRoundUp_downward g f hf_rep' h_sru
        refine ⟨?_, ?_⟩
        · rw [h_result_signed, h_truth_signed, hzm_q_eq]
          have h_inner : (((maxRepNat : ℚ) + 3) + f) ≤ maxRepNat + 13 := by
            linarith [hf_lt1]
          nlinarith [h10ze'_nn, h_inner]
        · rw [h_abs_truth_eq_neg, h_result_signed, h_truth_signed, hzm_q_eq]
          rw [show -((((maxRepNat : ℚ) + 3) + f) * 10 ^ ze') - -(((maxRepNat : ℚ) + 13) * 10 ^ ze')
                = (10 - f) * 10 ^ ze' from by ring,
              show -(-((((maxRepNat : ℚ) + 3) + f) * 10 ^ ze')) * (10 / ((2 ^ 63 + 2 : ℚ)))
                = ((((maxRepNat : ℚ) + 3) + f) * (10 / (2 ^ 63 + 2 : ℚ))) * 10 ^ ze' from by ring]
          apply mul_lt_mul_of_pos_right _ h10ze'_pos
          rw [h_denom_val, show (((maxRepNat : ℚ) + 3) + f) * (10 / (maxRepCuspTarget : ℚ))
                = 10 * (((maxRepNat : ℚ) + 3) + f) / maxRepCuspTarget from by ring,
              lt_div_iff₀ (by norm_num : (0 : ℚ) < maxRepCuspTarget)]
          nlinarith [hf_pos]
    · -- positive truth: the truncating clamps stay below the truth.
      have h_zn_false : (x.negative_ != y.negative_) = false := Bool.not_eq_true _ |>.mp h_zn
      have h_g_sbit_false : g.sbit_ = false := by rw [h_g_sbit]; exact h_zn_false
      have h_xy_neg_eq : x.negative_ = y.negative_ := by
        by_contra h_ne
        have : (x.negative_ != y.negative_) = true := by
          simp only [bne_iff_ne, ne_eq]; exact h_ne
        rw [this] at h_zn_false; exact Bool.noConfusion h_zn_false
      have h_truth_nn : 0 ≤ x.toRat * y.toRat := (toRat_mul_sign x y).1 h_xy_neg_eq
      have h_abs_truth_eq : |x.toRat * y.toRat| = x.toRat * y.toRat := abs_of_nonneg h_truth_nn
      have h_truth_signed : x.toRat * y.toRat = ((zm.toNat : ℚ) + f) * 10 ^ ze' := by
        have h1 := habs_xy_eq; rw [h_abs_truth_eq] at h1; exact h1
      have h_result_nn : result.negative_ = false := by rw [h_sign]; exact h_zn_false
      have h_result_signed : result.toRat = v * 10 ^ ze' := by
        have h2 := Number.toRat_of_nonneg result h_result_nn
        rw [h2, h_result_abs_via_self, hv_val]
      have h_bool_false : ∀ g' : Guard, g'.sbit_ = false →
          (((g'.round .downward == 1) || ((g'.round .downward == 0) && (zm % 2 == 1))) = false) := by
        intro g' hsb
        have hr_ne1 : g'.round .downward ≠ 1 := by
          intro h1
          have hsru := (round_downward_eq_one_iff g').mp h1
          have h := hsru.1
          rw [hsb] at h
          exact Bool.noConfusion h
        have hr_ne0 : g'.round .downward ≠ 0 := by
          unfold Guard.round
          split_ifs <;> decide
        rw [show (g'.round .downward == 1) = false from beq_eq_false_iff_ne.mpr hr_ne1,
            show (g'.round .downward == 0) = false from beq_eq_false_iff_ne.mpr hr_ne0,
            Bool.false_and]
        rfl
      rcases hv_cases with ⟨hv, _, _⟩ | ⟨hv, hcoup⟩ | ⟨_, _, hfire⟩
      · -- v = maxRepNat: the truncating clamp, below the truth.
        subst hv
        refine ⟨?_, ?_⟩
        · rw [h_result_signed, h_truth_signed]
          exact mul_le_mul_of_nonneg_right (by linarith [hf_nn]) h10ze'_nn
        · rw [h_abs_truth_eq, h_result_signed, h_truth_signed]
          rw [show ((zm.toNat : ℚ) + f) * 10 ^ ze' - (maxRepNat : ℚ) * 10 ^ ze'
                = (((zm.toNat : ℚ) + f) - maxRepNat) * 10 ^ ze' from by ring]
          have h_inner : ((zm.toNat : ℚ) + f) - maxRepNat
              < ((zm.toNat : ℚ) + f) * (10 / ((2 ^ 63 + 2 : ℚ))) :=
            cusp_err_lt_releps hzm_q_le hf_lt1
          calc (((zm.toNat : ℚ) + f) - maxRepNat) * 10 ^ ze'
              < ((zm.toNat : ℚ) + f) * (10 / ((2 ^ 63 + 2 : ℚ))) * 10 ^ ze' :=
                mul_lt_mul_of_pos_right h_inner h10ze'_pos
            _ = ((zm.toNat : ℚ) + f) * 10 ^ ze' * (10 / ((2 ^ 63 + 2 : ℚ))) := by ring
      · -- v = maxRepNat + 3: the decision is dead for positives, so zm = maxRepUp.
        subst hv
        have hzm_eq : zm.toNat = maxRepUp.toNat := by
          rcases hcoup with ⟨h, _⟩ | ⟨_, h⟩
          · exact h
          · rw [h_bool_false (g.pushOverflow zm .downward) (by rw [hgP_sbit]; exact h_g_sbit_false)] at h
            exact absurd h Bool.noConfusion
        have hzm_q_eq : (zm.toNat : ℚ) = maxRepNat + 3 := by
          rw [hzm_eq, show maxRepUp.toNat = maxRepUpNat from rfl]; norm_num
        refine ⟨?_, ?_⟩
        · rw [h_result_signed, h_truth_signed, hzm_q_eq]
          exact mul_le_mul_of_nonneg_right (by linarith [hf_nn]) h10ze'_nn
        · rw [h_abs_truth_eq, h_result_signed, h_truth_signed, hzm_q_eq]
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
      · -- v = maxRepNat + 13: requires the decision, dead when sbit is clear.
        rw [h_bool_false g h_g_sbit_false] at hfire
        exact absurd hfire Bool.noConfusion
  -- ===== IN RANGE: zm ≤ maxRep =====
  -- Case-split: shouldRoundUp_downward or not.
  by_cases h_sru : g.shouldRoundUp_downward
  · -- ===== ROUND UP =====
    have hf_pos : 0 < f := represents_pos_of_shouldRoundUp_downward g f hf_rep' h_sru
    -- sbit = true, so zn = true, so truth ≤ 0.
    have h_zn_true : (x.negative_ != y.negative_) = true := by
      rw [← h_g_sbit]; exact h_sru.1
    have h_result_neg : result.negative_ = true := by rw [h_sign, h_zn_true]
    -- Truth is non-positive.
    have h_truth_nonpos : x.toRat * y.toRat ≤ 0 := by
      have h_sign_xy := toRat_mul_sign x y
      apply h_sign_xy.2
      intro h_eq
      have : (x.negative_ != y.negative_) = false := by simp [h_eq]
      rw [this] at h_zn_true; exact Bool.noConfusion h_zn_true
    -- |truth| = -truth.
    have h_abs_truth_eq_neg : |x.toRat * y.toRat| = -(x.toRat * y.toRat) :=
      abs_of_nonpos h_truth_nonpos
    -- result.toRat = -(res_pos.mantissa * 10^res_pos.exp).
    have h_result_neg_val : result.toRat
        = -((res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_) := by
      have := Number.toRat_of_neg result h_result_neg
      rw [this, h_result_abs_via_self]
    -- Sub-case: cusp or non-cusp.
    by_cases h_cusp : zm = maxRep
    · -- ===== CUSP =====
      have h_cusp_val := doRoundUp_value_downward_roundUp_cusp g false zm ze' h_cusp h_sru
        "Number::multiplication overflow" res_pos h_rup_pos hres_pos_mant_ne
      simp only at h_cusp_val
      -- |result| = maxRepCuspTarget · 10^ze'.
      have hzm_eq_maxRep_q : (zm.toNat : ℚ) = maxRepNat := by
        rw [show zm.toNat = maxRep.toNat from by rw [h_cusp], maxRep_val]; norm_num
      -- truth = -(zm + f)·10^ze' = -(maxRepNat + f)·10^ze'.
      have h_truth_signed : x.toRat * y.toRat
          = -(((zm.toNat : ℚ) + f) * 10 ^ ze') := by
        have h1 : |x.toRat * y.toRat| = ((zm.toNat : ℚ) + f) * 10 ^ ze' := habs_xy_eq
        rw [h_abs_truth_eq_neg] at h1; linarith
      -- result = -(maxRepCuspTarget · 10^ze').
      have h_result_signed : result.toRat
          = -((maxRepCuspTarget : ℚ) * 10 ^ ze') := by
        rw [h_result_neg_val, h_cusp_val]
      -- Direction: result ≤ truth, i.e. -(maxRepCuspTarget)·10^ze' ≤ -(zm+f)·10^ze'
      -- iff (zm+f)·10^ze' ≤ maxRepCuspTarget·10^ze'.
      have h_direction : result.toRat ≤ x.toRat * y.toRat := by
        rw [h_result_signed, h_truth_signed]
        have h_inner : ((zm.toNat : ℚ) + f) ≤ maxRepCuspTarget := by
          rw [hzm_eq_maxRep_q]; linarith [hf_lt1]
        nlinarith [h10ze'_nn, h_inner]
      -- Magnitude: truth - result < |truth| · 10/(2^63+2).
      have h_magnitude : x.toRat * y.toRat - result.toRat
          < |x.toRat * y.toRat| * (10 / ((2 ^ 63 + 2 : ℚ))) := by
        rw [h_abs_truth_eq_neg, h_result_signed, h_truth_signed]
        rw [hzm_eq_maxRep_q]
        have h_lhs_eq : -(((maxRepNat : ℚ) + f) * 10 ^ ze') -
            -((maxRepCuspTarget : ℚ) * 10 ^ ze')
            = (3 - f) * 10 ^ ze' := by ring
        have h_rhs_arrange : -(-(((maxRepNat : ℚ) + f) * 10 ^ ze')) *
            (10 / ((2 ^ 63 + 2 : ℚ)))
            = (maxRepNat + f) * 10 ^ ze' * (10 / (2 ^ 63 + 2 : ℚ)) := by ring
        rw [h_lhs_eq, h_rhs_arrange]
        -- (3-f) · (2^63+2) < 10 · (maxRepNat + f).
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
      have h_nc_val := doRoundUp_value_downward_roundUp_noCusp g false zm ze' h_sru h_no_cusp
        "Number::multiplication overflow" res_pos h_rup_pos hres_pos_mant_ne
      simp only at h_nc_val
      -- |result| = (zm + 1)·10^ze'.
      have h_truth_signed : x.toRat * y.toRat
          = -(((zm.toNat : ℚ) + f) * 10 ^ ze') := by
        have h1 : |x.toRat * y.toRat| = ((zm.toNat : ℚ) + f) * 10 ^ ze' := habs_xy_eq
        rw [h_abs_truth_eq_neg] at h1; linarith
      have h_result_signed : result.toRat
          = -(((zm.toNat : ℚ) + 1) * 10 ^ ze') := by
        rw [h_result_neg_val, h_nc_val]
      -- Direction: result ≤ truth.
      have h_direction : result.toRat ≤ x.toRat * y.toRat := by
        rw [h_result_signed, h_truth_signed]
        have h_inner : ((zm.toNat : ℚ) + f) ≤ ((zm.toNat : ℚ) + 1) := by linarith
        nlinarith [h10ze'_nn, h_inner]
      -- Magnitude: (1 - f) < (zm + f) · 10/(2^63 + 2).
      have h_magnitude : x.toRat * y.toRat - result.toRat
          < |x.toRat * y.toRat| * (10 / ((2 ^ 63 + 2 : ℚ))) := by
        rw [h_abs_truth_eq_neg, h_result_signed, h_truth_signed]
        have h_lhs_eq : -(((zm.toNat : ℚ) + f) * 10 ^ ze') -
            -(((zm.toNat : ℚ) + 1) * 10 ^ ze') = (1 - f) * 10 ^ ze' := by ring
        have h_rhs_arrange : -(-(((zm.toNat : ℚ) + f) * 10 ^ ze')) *
            (10 / ((2 ^ 63 + 2 : ℚ)))
            = ((zm.toNat : ℚ) + f) * 10 ^ ze' * (10 / ((2 ^ 63 + 2 : ℚ))) := by ring
        rw [h_lhs_eq, h_rhs_arrange]
        -- Sub-case: zm = floor or zm > floor.
        have h_inner : (1 - f) < (((zm.toNat : ℚ) + f)) * (10 / ((2 ^ 63 + 2 : ℚ))) :=
          one_sub_f_lt_releps hzm_ge h_floor_constraint hf_pos hf_lt1
        exact releps_lift h_inner h10ze'_pos
      exact ⟨h_direction, h_magnitude⟩
  · -- ===== NO ROUND UP (truncate) =====
    have h_tr_val := doRoundUp_value_downward_truncate g false zm ze' h_sru h_zm_le_rep "Number::multiplication overflow" res_pos h_rup_pos hres_pos_mant_ne
    simp only at h_tr_val
    -- |result| = zm · 10^ze'.
    -- shouldRoundUp_downward fails: sbit=false OR digits=0 ∧ xbit=false.
    -- Direction depends on truth sign.
    by_cases h_zn : (x.negative_ != y.negative_) = true
    · -- zn = true. Truth ≤ 0. shouldRoundUp_to_nearest fails because guard = 0.
      have h_g_sbit_true : g.sbit_ = true := by rw [h_g_sbit]; exact h_zn
      -- ¬ shouldRoundUp_downward ∧ sbit_=true ⇒ ¬(digits > 0 ∨ xbit_=true).
      have h_no_or : ¬ (g.digits_ > 0 ∨ g.xbit_ = true) := by
        intro h
        exact h_sru ⟨h_g_sbit_true, h⟩
      have h_digits_not_pos : ¬ g.digits_ > 0 := fun h => h_no_or (Or.inl h)
      have h_xbit_not_true : ¬ g.xbit_ = true := fun h => h_no_or (Or.inr h)
      have h_xbit_eq_false : g.xbit_ = false := Bool.not_eq_true _ |>.mp h_xbit_not_true
      -- Then f = 0.
      -- f = decimalValue/10^16 + x. digits = 0 → decimalValue = 0. xbit=false → x=0.
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
      -- Now both signed values are zero.
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
        -- h1 : -(x.toRat * y.toRat) = (zm + f) · 10^ze'
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
      -- With f = 0: truth = -(zm · 10^ze') = result.
      have h_truth_eq_result : x.toRat * y.toRat = result.toRat := by
        rw [h_result_signed, h_truth_signed, hf_zero]; ring
      refine ⟨?_, ?_⟩
      · exact le_of_eq h_truth_eq_result.symm
      · rw [h_truth_eq_result]
        have h_zero : result.toRat - result.toRat = 0 := by ring
        rw [h_zero]
        have h_result_ne : result.toRat ≠ 0 := by
          intro h; rw [Number.toRat_eq_zero_iff] at h; exact hresult h
        have h_abs_pos : (0 : ℚ) < |result.toRat| := abs_pos.mpr h_result_ne
        have h_eps_pos : (0 : ℚ) < 10 / ((2 ^ 63 + 2 : ℚ)) := by
          apply div_pos (by norm_num) h_denom_pos
        exact mul_pos h_abs_pos h_eps_pos
    · -- zn = false. Truth ≥ 0.
      have h_zn_false : (x.negative_ != y.negative_) = false := Bool.not_eq_true _ |>.mp h_zn
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
      have h_result_nn : result.negative_ = false := by rw [h_sign]; exact h_zn_false
      have h_result_pos_val : result.toRat
          = (res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_ := by
        have := Number.toRat_of_nonneg result h_result_nn
        rw [this, h_result_abs_via_self]
      have h_result_signed : result.toRat = (zm.toNat : ℚ) * 10 ^ ze' := by
        rw [h_result_pos_val, h_tr_val]
      -- Direction: result = zm·10^ze' ≤ (zm+f)·10^ze' = truth.
      have h_direction : result.toRat ≤ x.toRat * y.toRat := by
        rw [h_result_signed, h_truth_signed]
        nlinarith [h10ze'_nn, hf_nn]
      -- Magnitude: truth - result = f·10^ze' < (zm+f)·10^ze' · 10/(2^63+2).
      have h_magnitude : x.toRat * y.toRat - result.toRat
          < |x.toRat * y.toRat| * (10 / ((2 ^ 63 + 2 : ℚ))) := by
        rw [h_abs_truth_eq, h_result_signed, h_truth_signed]
        have h_lhs_eq : ((zm.toNat : ℚ) + f) * 10 ^ ze' - (zm.toNat : ℚ) * 10 ^ ze'
            = f * 10 ^ ze' := by ring
        rw [h_lhs_eq]
        -- Show: f < ((zm:ℚ) + f) * 10/(2^63+2).
        have h_inner : f < (((zm.toNat : ℚ) + f)) * (10 / ((2 ^ 63 + 2 : ℚ))) := f_lt_releps hzm_q_ge hf_lt1
        exact releps_lift h_inner h10ze'_pos
      exact ⟨h_direction, h_magnitude⟩

end XRPL.Model.Protocol
