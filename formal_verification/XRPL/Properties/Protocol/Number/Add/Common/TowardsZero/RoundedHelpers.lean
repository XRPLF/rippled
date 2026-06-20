import XRPL.Properties.Protocol.Number.Common.Notation
-- Discrete-grid rounding identification for `operator_add` under `.towards_zero`.
import XRPL.Properties.Protocol.Number.Add.Common.Rounded
import XRPL.Properties.Protocol.Number.Add.Common.TowardsZero.BoundProof
import XRPL.Properties.Protocol.Number.Add.RoundsWithin
import XRPL.Properties.Protocol.Number.Normalize.Common.ResultFacts
import XRPL.Properties.Protocol.Number.Common.Closest.Identify
import XRPL.Properties.Protocol.Number.Common.Closest.NoInbetween
import XRPL.Properties.Protocol.Number.Common.Closest.GridPoint


namespace XRPL.Model.Protocol

theorem operator_add_no_inbetween_below_towards_zero (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (h_not_zero : ¬ x.operator_eq y.operator_neg)
    (hok : Number.operator_add x y .towards_zero = .ok result)
    (hresult : result.mantissa_ ≠ 0)
    (h_le : result.toRat ≤ x.toRat + y.toRat) :
    ∀ m : Number, m.isNormalized →
      result.toRat < m.toRat → ¬ (m.toRat ≤ x.toRat + y.toRat) := by
  by_cases h_sign_eq : x.negative_ = y.negative_
  · -- Same-sign: the facts bundle is frame-ready.
    obtain ⟨zm, ze', f, g, res_pos, hzm_ge, hzm_le, hf_nn, hf_lt, habs_xy_eq,
        h_rup_pos, h_result_abs, hres_pos_mant_ne, _hf_rep, h_sign, hzm_succ, _, _, _⟩ :=
      operator_add_algorithmic_facts_same_sign_towards_zero x y result hx hy
        hx_mant_ne hy_mant_ne h_sign_eq h_not_zero hok
    exact no_inbetween_below_towards_zero_frame result (x.toRat + y.toRat) zm ze' f g res_pos
      "Number::addition overflow" hzm_ge hzm_le hf_nn hf_lt habs_xy_eq
      h_rup_pos h_result_abs hres_pos_mant_ne hzm_succ
      (fun h_pos => Number.toRat_nonneg_of_nonnegative result
        (h_sign.trans (add_same_sign_xn_false_of_truth_pos x y h_sign_eq h_pos)))
      h_le
  · -- Diff-sign: through the `doNormalize128` keystone.
    obtain ⟨M, ze0, δ, zn, sticky, hδ_low, _hδ_le, hsticky_zero, hM_pos, hM_lt, hM_big,
        htruth, hok128, hsign, hδ_lt, hsticky_pos⟩ :=
      operator_add_algorithmic_facts_diff_sign_represents x y result .towards_zero hx hy
        hx_mant_ne hy_mant_ne h_sign_eq h_not_zero hok
    obtain ⟨zm, ze', f, g, res_pos, ftilde, hzm_ge, hzm_le, hf_nn, hf_lt, h_value,
        h_rup_pos, h_result_abs, hres_pos_mant_ne, h_neg, _h_sbit, hzm_succ,
        _hrep_t, _h_pos_transfer, _h_zero_transfer⟩ :=
      doNormalize128_algorithmic_facts zn M ze0 δ sticky .towards_zero hδ_low hδ_lt
        hsticky_zero hsticky_pos hM_pos (lt_trans hM_lt (by norm_num))
        (fun hst => by
          have h2 : ((10 : ℚ) ^ 20) ≤ (M.toNat : ℚ) := by exact_mod_cast hM_big hst
          linarith [le_of_lt hδ_lt])
        result hok128 hresult
    have h_truth_abs : |x.toRat + y.toRat| = ((zm.toNat : ℚ) + f) * 10 ^ ze' :=
      htruth.trans h_value
    exact no_inbetween_below_towards_zero_frame result (x.toRat + y.toRat) zm ze' f g res_pos
      "Number::normalize 2" hzm_ge hzm_le hf_nn hf_lt h_truth_abs h_rup_pos
      h_result_abs hres_pos_mant_ne hzm_succ
      (fun h_pos => by
        have hzn_false : zn = false := zn_eq_false_of_pos hsign.1 h_pos
        exact Number.toRat_nonneg_of_nonnegative result (h_neg.trans hzn_false))
      h_le

theorem operator_add_no_inbetween_above_towards_zero (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (h_not_zero : ¬ x.operator_eq y.operator_neg)
    (hok : Number.operator_add x y .towards_zero = .ok result)
    (hresult : result.mantissa_ ≠ 0)
    (h_ge : x.toRat + y.toRat ≤ result.toRat) :
    ∀ m : Number, m.isNormalized →
      m.toRat < result.toRat → ¬ (x.toRat + y.toRat ≤ m.toRat) := by
  by_cases h_sign_eq : x.negative_ = y.negative_
  · obtain ⟨zm, ze', f, g, res_pos, hzm_ge, hzm_le, hf_nn, hf_lt, habs_xy_eq,
        h_rup_pos, h_result_abs, hres_pos_mant_ne, _hf_rep, h_sign, hzm_succ, _, _, _⟩ :=
      operator_add_algorithmic_facts_same_sign_towards_zero x y result hx hy
        hx_mant_ne hy_mant_ne h_sign_eq h_not_zero hok
    exact no_inbetween_above_towards_zero_frame result (x.toRat + y.toRat) zm ze' f g res_pos
      "Number::addition overflow" hzm_ge hzm_le hf_nn hf_lt habs_xy_eq
      h_rup_pos h_result_abs hres_pos_mant_ne hzm_succ
      (fun h_neg => Number.toRat_nonpos_of_negative result
        (h_sign.trans (add_same_sign_xn_true_of_truth_neg x y h_sign_eq h_neg)))
      h_ge
  · obtain ⟨M, ze0, δ, zn, sticky, hδ_low, _hδ_le, hsticky_zero, hM_pos, hM_lt, hM_big,
        htruth, hok128, hsign, hδ_lt, hsticky_pos⟩ :=
      operator_add_algorithmic_facts_diff_sign_represents x y result .towards_zero hx hy
        hx_mant_ne hy_mant_ne h_sign_eq h_not_zero hok
    obtain ⟨zm, ze', f, g, res_pos, ftilde, hzm_ge, hzm_le, hf_nn, hf_lt, h_value,
        h_rup_pos, h_result_abs, hres_pos_mant_ne, h_neg, _h_sbit, hzm_succ,
        _hrep_t, _h_pos_transfer, _h_zero_transfer⟩ :=
      doNormalize128_algorithmic_facts zn M ze0 δ sticky .towards_zero hδ_low hδ_lt
        hsticky_zero hsticky_pos hM_pos (lt_trans hM_lt (by norm_num))
        (fun hst => by
          have h2 : ((10 : ℚ) ^ 20) ≤ (M.toNat : ℚ) := by exact_mod_cast hM_big hst
          linarith [le_of_lt hδ_lt])
        result hok128 hresult
    have h_truth_abs : |x.toRat + y.toRat| = ((zm.toNat : ℚ) + f) * 10 ^ ze' :=
      htruth.trans h_value
    exact no_inbetween_above_towards_zero_frame result (x.toRat + y.toRat) zm ze' f g res_pos
      "Number::normalize 2" hzm_ge hzm_le hf_nn hf_lt h_truth_abs h_rup_pos
      h_result_abs hres_pos_mant_ne hzm_succ
      (fun h_negT => by
        have hzn_true : zn = true := zn_eq_true_of_neg hsign.2 h_negT
        exact Number.toRat_nonpos_of_negative result (h_neg.trans hzn_true))
      h_ge

theorem operator_add_rounded_towards_zero_proof (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hok : Number.operator_add x y .towards_zero = .ok result) :
    Number.RoundsToRepresentable result (x.toRat + y.toRat) .towards_zero := by
  -- Zero-`y` guard: the model returns `x` verbatim, a grid point of itself.
  by_cases hy_guard : y.operator_eq Number.zero = true
  · have hy0 : y.toRat = 0 := by
      have h := hy_guard
      have hmeq := Number.mantissa_eq_zero_of_operator_eq_zero h
      exact Number.toRat_eq_zero_of_mantissa_zero y hmeq
    have h_result : result = x := by
      unfold Number.operator_add at hok
      rw [if_pos hy_guard] at hok
      exact (Except.ok.inj (show (Except.ok x : Except String Number) = .ok result from hok)).symm
    rw [hy0, add_zero]
    by_cases hx_mant : x.mantissa_ = 0
    · have hx0 : x.toRat = 0 := Number.toRat_eq_zero_of_mantissa_zero x hx_mant
      rw [hx0]
      change ∃ n' : Number, (if (0 : ℚ) ≥ 0 then Number.lower 0 else Number.upper 0)
        = some n' ∧ result.toRat = n'.toRat
      rw [if_pos (by norm_num : (0 : ℚ) ≥ 0)]
      refine ⟨Number.zero, ?_, ?_⟩
      · unfold Number.lower
        rw [if_pos rfl]
      · rw [h_result, hx0, Number.toRat_zero]
    · have hx_ne : x.toRat ≠ 0 := Number.toRat_ne_zero_of_mantissa_ne_zero x hx_mant
      change ∃ n' : Number, (if x.toRat ≥ 0 then Number.lower x.toRat else Number.upper x.toRat)
        = some n' ∧ result.toRat = n'.toRat
      by_cases h_nn : x.toRat ≥ 0
      · rw [if_pos h_nn]
        obtain ⟨n, h_lo, h_val⟩ := Number.lower_value_self x hx hx_ne
        exact ⟨n, h_lo, by rw [h_result]; exact h_val⟩
      · rw [if_neg h_nn]
        obtain ⟨n, h_up, h_val⟩ := Number.upper_value_self x hx hx_ne
        exact ⟨n, h_up, by rw [h_result]; exact h_val⟩
  -- Zero-`x` guard: the model returns `y` verbatim.
  by_cases hx_guard : x.operator_eq Number.zero = true
  · have hx0 : x.toRat = 0 := by
      have h := hx_guard
      have hmeq := Number.mantissa_eq_zero_of_operator_eq_zero h
      exact Number.toRat_eq_zero_of_mantissa_zero x hmeq
    have h_result : result = y := by
      unfold Number.operator_add at hok
      rw [if_neg hy_guard, if_pos hx_guard] at hok
      exact (Except.ok.inj (show (Except.ok y : Except String Number) = .ok result from hok)).symm
    have hy_mant_ne : y.mantissa_ ≠ 0 := by
      intro h
      apply hy_guard
      have hy_zero : y = Number.zero := Number.eq_zero_of_mantissa_zero y hy h
      rw [hy_zero]
      decide
    have hy_ne : y.toRat ≠ 0 := Number.toRat_ne_zero_of_mantissa_ne_zero y hy_mant_ne
    rw [hx0, zero_add]
    change ∃ n' : Number, (if y.toRat ≥ 0 then Number.lower y.toRat else Number.upper y.toRat)
      = some n' ∧ result.toRat = n'.toRat
    by_cases h_nn : y.toRat ≥ 0
    · rw [if_pos h_nn]
      obtain ⟨n, h_lo, h_val⟩ := Number.lower_value_self y hy hy_ne
      exact ⟨n, h_lo, by rw [h_result]; exact h_val⟩
    · rw [if_neg h_nn]
      obtain ⟨n, h_up, h_val⟩ := Number.upper_value_self y hy hy_ne
      exact ⟨n, h_up, by rw [h_result]; exact h_val⟩
  -- Exact cancellation: the model returns the canonical zero.
  by_cases heq_guard : x.operator_eq y.operator_neg = true
  · have h_truth0 : x.toRat + y.toRat = 0 := add_truth_zero_of_eq_neg x y heq_guard
    have h_result : result = Number.zero := by
      unfold Number.operator_add at hok
      rw [if_neg hy_guard, if_neg hx_guard, if_pos heq_guard] at hok
      exact (Except.ok.inj
        (show (Except.ok Number.zero : Except String Number) = .ok result from hok)).symm
    rw [h_truth0]
    change ∃ n' : Number, (if (0 : ℚ) ≥ 0 then Number.lower 0 else Number.upper 0)
      = some n' ∧ result.toRat = n'.toRat
    rw [if_pos (by norm_num : (0 : ℚ) ≥ 0)]
    refine ⟨Number.zero, ?_, ?_⟩
    · unfold Number.lower
      rw [if_pos rfl]
    · rw [h_result]
  -- Generic path: both operands nonzero, no cancellation.
  have hx_mant_ne : x.mantissa_ ≠ 0 := by
    intro h
    apply hx_guard
    have hx_zero : x = Number.zero := Number.eq_zero_of_mantissa_zero x hx h
    rw [hx_zero]
    decide
  have hy_mant_ne : y.mantissa_ ≠ 0 := by
    intro h
    apply hy_guard
    have hy_zero : y = Number.zero := Number.eq_zero_of_mantissa_zero y hy h
    rw [hy_zero]
    decide
  have h_truth_ne : x.toRat + y.toRat ≠ 0 :=
    operator_add_truth_ne x y hx hy hx_mant_ne hy_mant_ne heq_guard .towards_zero result hok
  -- Underflow: flush-to-zero is correct rounding toward zero.
  by_cases hres : result.mantissa_ = 0
  · have h_res0 : result.toRat = 0 := Number.toRat_eq_zero_of_mantissa_zero result hres
    by_cases h_sign_eq : x.negative_ = y.negative_
    · -- Same-sign addition cannot flush (the sum stays at operand scale).
      exfalso
      obtain ⟨_, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, hresult_ne, _, _⟩ :=
        operator_add_algorithmic_facts_same_sign_towards_zero x y result hx hy
          hx_mant_ne hy_mant_ne h_sign_eq heq_guard hok
      exact hresult_ne hres
    · have h_small := operator_add_underflow_truth_small x y result .towards_zero hx hy
        hx_mant_ne hy_mant_ne h_sign_eq heq_guard hok hres
      change ∃ n' : Number, (if x.toRat + y.toRat ≥ 0 then Number.lower (x.toRat + y.toRat)
        else Number.upper (x.toRat + y.toRat)) = some n' ∧ result.toRat = n'.toRat
      rcases lt_or_gt_of_ne h_truth_ne with h_neg | h_pos
      · rw [if_neg (not_le.mpr h_neg)]
        refine ⟨Number.zero, ?_, ?_⟩
        · exact Number.upper_eq_zero_of_neg_small _ h_neg
            (by rwa [abs_of_neg h_neg] at h_small)
        · rw [h_res0, Number.toRat_zero]
      · rw [if_pos (le_of_lt h_pos : (0 : ℚ) ≤ x.toRat + y.toRat)]
        refine ⟨Number.zero, ?_, ?_⟩
        · exact Number.lower_eq_zero_of_pos_small _ h_pos
            (by rwa [abs_of_pos h_pos] at h_small)
        · rw [h_res0, Number.toRat_zero]
  -- In-range path.
  have hresult : result.mantissa_ ≠ 0 := hres
  have h_dir : |result.toRat| ≤ |x.toRat + y.toRat| :=
    (operator_add_rounds_towards_zero x y result hx hy hx_mant_ne hy_mant_ne heq_guard
      hok hresult).1
  have h_result_norm : result.isNormalized := by
    by_cases h_sign_eq : x.negative_ = y.negative_
    · obtain ⟨_, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, h_norm, _⟩ :=
        operator_add_algorithmic_facts_same_sign_towards_zero x y result hx hy
          hx_mant_ne hy_mant_ne h_sign_eq heq_guard hok
      exact h_norm
    · exact operator_add_result_isNormalized x y result .towards_zero hx hy
        hx_mant_ne hy_mant_ne h_sign_eq heq_guard hok hresult
  have h_bound : |result.toRat - (x.toRat + y.toRat)|
      ≤ |x.toRat + y.toRat| * (11 / (2 ^ 63 - 18 : ℚ)) :=
    le_of_lt (operator_add_rounding_bound_towards_zero_tight x y result hx hy
      hx_mant_ne hy_mant_ne heq_guard hok hresult)
  have h_truth_top : result.exponent_ ≥ maxExponent →
      |x.toRat + y.toRat| < 10 ^ 19 * (10 : ℚ) ^ (maxExponent : ℤ) := by
    intro _
    by_cases h_sign_eq : x.negative_ = y.negative_
    · obtain ⟨_, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, h_top⟩ :=
        operator_add_algorithmic_facts_same_sign_towards_zero x y result hx hy
          hx_mant_ne hy_mant_ne h_sign_eq heq_guard hok
      exact h_top
    · exact add_diff_sign_truth_top x y hx hy h_sign_eq
  change ∃ n' : Number, (if x.toRat + y.toRat ≥ 0 then Number.lower (x.toRat + y.toRat)
    else Number.upper (x.toRat + y.toRat)) = some n' ∧ result.toRat = n'.toRat
  by_cases h_truth_nn : x.toRat + y.toRat ≥ 0
  · rw [if_pos h_truth_nn]
    -- Direction: result ≤ |result| ≤ |truth| = truth.
    have h_round_down : result.toRat ≤ x.toRat + y.toRat := by
      calc result.toRat ≤ |result.toRat| := le_abs_self _
        _ ≤ |x.toRat + y.toRat| := h_dir
        _ = x.toRat + y.toRat := abs_of_nonneg h_truth_nn
    exact closest_lower_of_no_inbetween result (x.toRat + y.toRat) h_result_norm hresult
      h_truth_ne h_bound h_truth_top h_round_down
      (operator_add_no_inbetween_below_towards_zero x y result hx hy hx_mant_ne hy_mant_ne
        heq_guard hok hresult h_round_down)
  · rw [if_neg h_truth_nn]
    push_neg at h_truth_nn
    have h_truth_np : x.toRat + y.toRat ≤ 0 := le_of_lt h_truth_nn
    -- Direction: truth = −|truth| ≤ −|result| ≤ result.
    have h_round_up : x.toRat + y.toRat ≤ result.toRat := by
      have h1 : x.toRat + y.toRat = -|x.toRat + y.toRat| := by
        rw [abs_of_nonpos h_truth_np]; ring
      have h2 : -|result.toRat| ≤ result.toRat := neg_abs_le _
      linarith [neg_le_neg h_dir]
    exact closest_upper_of_no_inbetween result (x.toRat + y.toRat) h_result_norm hresult
      h_truth_ne h_bound h_truth_top h_round_up
      (operator_add_no_inbetween_above_towards_zero x y result hx hy hx_mant_ne hy_mant_ne
        heq_guard hok hresult h_round_up)

end XRPL.Model.Protocol
