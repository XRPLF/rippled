import XRPL.Properties.Protocol.Number.Common.Notation
-- Discrete-grid rounding identification for `operator_add` under `.upward`.
import XRPL.Properties.Protocol.Number.Add.Common.Rounded
import XRPL.Properties.Protocol.Number.Add.Common.Upward.BoundProof
import XRPL.Properties.Protocol.Number.Add.RoundsWithin
import XRPL.Properties.Protocol.Number.Normalize.Common.ResultFacts
import XRPL.Properties.Protocol.Number.Common.Closest.Identify
import XRPL.Properties.Protocol.Number.Common.Closest.NoInbetween
import XRPL.Properties.Protocol.Number.Common.Closest.GridPoint


namespace XRPL.Model.Protocol

/-! # `operator_add` is correctly rounded under `.upward`

The result is `Number.upper` of the exact sum. `hresult` is genuinely
required: an underflow flush returns zero regardless of direction, and for a
tiny positive sum `0 < truth` violates `.upward` itself. -/

/-- No normalized Number sits strictly between the sum and `result.toRat`. -/
theorem operator_add_no_inbetween_above_upward (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (h_not_zero : ¬ x.operator_eq y.operator_neg)
    (hok : Number.operator_add x y .upward = .ok result)
    (hresult : result.mantissa_ ≠ 0)
    (h_ge : x.toRat + y.toRat ≤ result.toRat) :
    ∀ m : Number, m.isNormalized →
      m.toRat < result.toRat → ¬ (x.toRat + y.toRat ≤ m.toRat) := by
  by_cases h_sign_eq : x.negative_ = y.negative_
  · -- Same-sign: the facts bundle is frame-ready (true fraction represented).
    obtain ⟨zm, ze', f, g, res_pos, hzm_ge, hzm_le, hf_nn, hf_lt, _hfloor_constr, habs_xy_eq,
        h_rup_pos, h_result_abs, hres_pos_mant_ne, hf_rep, h_sign, h_sbit, hzm_succ, _, _, _⟩ :=
      operator_add_algorithmic_facts_same_sign_upward x y result hx hy hx_mant_ne hy_mant_ne
        h_sign_eq h_not_zero hok
    exact no_inbetween_above_upward_frame result (x.toRat + y.toRat) zm ze' f g res_pos
      "Number::addition overflow" hzm_ge hzm_le hf_nn hf_lt habs_xy_eq h_rup_pos
      h_result_abs hres_pos_mant_ne
      (fun h => represents_pos_of_shouldRoundUp_upward g f hf_rep h)
      (fun hd hxb => represents_eq_zero_of_digits_zero_xbit_false hd hxb hf_rep)
      hzm_succ
      (fun h_neg => Number.toRat_nonpos_of_negative result
        (h_sign.trans (add_same_sign_xn_true_of_truth_neg x y h_sign_eq h_neg)))
      (fun h_neg => h_sbit.trans (add_same_sign_xn_true_of_truth_neg x y h_sign_eq h_neg))
      (fun h_pos => h_sbit.trans (add_same_sign_xn_false_of_truth_pos x y h_sign_eq h_pos))
      h_ge
  · -- Diff-sign: through the `doNormalize128` keystone (shadow transfers).
    obtain ⟨M, ze0, δ, zn, sticky, hδ_low, _hδ_le, hsticky_zero, hM_pos, hM_lt, hM_big,
        htruth, hok128, hsign, hδ_lt, hsticky_pos⟩ :=
      operator_add_algorithmic_facts_diff_sign_represents x y result .upward hx hy
        hx_mant_ne hy_mant_ne h_sign_eq h_not_zero hok
    obtain ⟨zm, ze', f, g, res_pos, ftilde, hzm_ge, hzm_le, hf_nn, hf_lt, h_value,
        h_rup_pos, h_result_abs, hres_pos_mant_ne, h_neg, h_sbit, hzm_succ,
        hrep_t, h_pos_transfer, h_zero_transfer⟩ :=
      doNormalize128_algorithmic_facts zn M ze0 δ sticky .upward hδ_low hδ_lt
        hsticky_zero hsticky_pos hM_pos (lt_trans hM_lt (by norm_num))
        (fun hst => by
          have h2 : ((10 : ℚ) ^ 20) ≤ (M.toNat : ℚ) := by exact_mod_cast hM_big hst
          linarith [le_of_lt hδ_lt])
        result hok128 hresult
    have h_truth_abs : |x.toRat + y.toRat| = ((zm.toNat : ℚ) + f) * 10 ^ ze' :=
      htruth.trans h_value
    exact no_inbetween_above_upward_frame result (x.toRat + y.toRat) zm ze' f g res_pos
      "Number::normalize 2" hzm_ge hzm_le hf_nn hf_lt h_truth_abs h_rup_pos
      h_result_abs hres_pos_mant_ne
      (fun h => h_pos_transfer (represents_pos_of_shouldRoundUp_upward g ftilde hrep_t h))
      (fun hd hxb => h_zero_transfer (represents_eq_zero_of_digits_zero_xbit_false hd hxb hrep_t))
      hzm_succ
      (fun h_negT => by
        have hzn_true : zn = true := zn_eq_true_of_neg hsign.2 h_negT
        exact Number.toRat_nonpos_of_negative result (h_neg.trans hzn_true))
      (fun h_negT => by
        have hzn_true : zn = true := zn_eq_true_of_neg hsign.2 h_negT
        exact h_sbit.trans hzn_true)
      (fun h_pos => by
        have hzn_false : zn = false := zn_eq_false_of_pos hsign.1 h_pos
        exact h_sbit.trans hzn_false)
      h_ge

/-- `operator_add` under `.upward` is **correctly rounded**: the result is
`Number.upper` of the exact sum. `hresult` is essential (the underflow flush
ignores the direction). -/
theorem operator_add_rounded_upward_proof (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hok : Number.operator_add x y .upward = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    Number.RoundsToRepresentable result (x.toRat + y.toRat) .upward := by
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
    have hx_mant : x.mantissa_ ≠ 0 := fun h => hresult (by rw [h_result]; exact h)
    have hx_ne : x.toRat ≠ 0 := Number.toRat_ne_zero_of_mantissa_ne_zero x hx_mant
    rw [hy0, add_zero]
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
    have hy_mant : y.mantissa_ ≠ 0 := fun h => hresult (by rw [h_result]; exact h)
    have hy_ne : y.toRat ≠ 0 := Number.toRat_ne_zero_of_mantissa_ne_zero y hy_mant
    rw [hx0, zero_add]
    obtain ⟨n, h_up, h_val⟩ := Number.upper_value_self y hy hy_ne
    exact ⟨n, h_up, by rw [h_result]; exact h_val⟩
  -- Exact cancellation would return the canonical zero — excluded by `hresult`.
  by_cases heq_guard : x.operator_eq y.operator_neg = true
  · exfalso
    have h_result : result = Number.zero := by
      unfold Number.operator_add at hok
      rw [if_neg hy_guard, if_neg hx_guard, if_pos heq_guard] at hok
      exact (Except.ok.inj
        (show (Except.ok Number.zero : Except String Number) = .ok result from hok)).symm
    apply hresult
    rw [h_result]
    rfl
  -- Generic path.
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
    operator_add_truth_ne x y hx hy hx_mant_ne hy_mant_ne heq_guard .upward result hok
  have h_dir : x.toRat + y.toRat ≤ result.toRat :=
    (operator_add_rounds_upward x y result hx hy hx_mant_ne hy_mant_ne heq_guard
      hok hresult).1
  have h_result_norm : result.isNormalized := by
    by_cases h_sign_eq : x.negative_ = y.negative_
    · obtain ⟨_, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, h_norm, _, _⟩ :=
        operator_add_algorithmic_facts_same_sign_upward x y result hx hy
          hx_mant_ne hy_mant_ne h_sign_eq heq_guard hok
      exact h_norm
    · exact operator_add_result_isNormalized x y result .upward hx hy
        hx_mant_ne hy_mant_ne h_sign_eq heq_guard hok hresult
  have h_bound : |result.toRat - (x.toRat + y.toRat)|
      ≤ |x.toRat + y.toRat| * (11 / (2 ^ 63 - 18 : ℚ)) :=
    le_of_lt (operator_add_rounding_bound_upward_tight x y result hx hy
      hx_mant_ne hy_mant_ne heq_guard hok hresult)
  have h_truth_top : result.exponent_ ≥ maxExponent →
      |x.toRat + y.toRat| < 10 ^ 19 * (10 : ℚ) ^ (maxExponent : ℤ) := by
    intro _
    by_cases h_sign_eq : x.negative_ = y.negative_
    · obtain ⟨_, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, h_top, _⟩ :=
        operator_add_algorithmic_facts_same_sign_upward x y result hx hy
          hx_mant_ne hy_mant_ne h_sign_eq heq_guard hok
      exact h_top
    · exact add_diff_sign_truth_top x y hx hy h_sign_eq
  exact closest_upper_of_no_inbetween result (x.toRat + y.toRat) h_result_norm hresult
    h_truth_ne h_bound h_truth_top h_dir
    (operator_add_no_inbetween_above_upward x y result hx hy hx_mant_ne hy_mant_ne
      heq_guard hok hresult h_dir)

end XRPL.Model.Protocol
