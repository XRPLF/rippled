import XRPL.Properties.Protocol.Number.Common.Notation
import XRPL.Properties.Protocol.Number.Div.Common.Decompose
import XRPL.Properties.Protocol.Number.Div.Common.Downward.BoundProof
import XRPL.Properties.Protocol.Number.Common.Closest.Identify
import XRPL.Properties.Protocol.Number.Common.Closest.NoInbetween


namespace XRPL.Model.Protocol

/-- No normalized Number sits strictly between `result.toRat` and the
quotient. -/
theorem operator_div_no_inbetween_below_downward (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (hok : Number.operator_div x y .downward = .ok result)
    (hresult : result.mantissa_ ≠ 0)
    (h_le : result.toRat ≤ x.toRat / y.toRat) :
    ∀ m : Number, m.isNormalized →
      result.toRat < m.toRat → ¬ (m.toRat ≤ x.toRat / y.toRat) := by
  obtain ⟨M, ze0, δ, zn, sticky, hδ_low, _hδ_le, hsticky_zero, hM_pos, hM_lt, hδM,
      htruth, hok128, hsign, hδ_lt, hsticky_pos⟩ :=
    operator_div_algorithmic_facts_represents x y result .downward hx hy
      hx_mant_ne hy_mant_ne hok
  obtain ⟨zm, ze', f, g, res_pos, ftilde, hzm_ge, hzm_le, hf_nn, hf_lt, h_value,
      h_rup_pos, h_result_abs, hres_pos_mant_ne, h_neg, h_sbit, hzm_succ,
      hrep_t, h_pos_transfer, h_zero_transfer⟩ :=
    doNormalize128_algorithmic_facts zn M ze0 δ sticky .downward hδ_low hδ_lt
      hsticky_zero hsticky_pos hM_pos hM_lt hδM result hok128 hresult
  have h_truth_abs : |x.toRat / y.toRat| = ((zm.toNat : ℚ) + f) * 10 ^ ze' :=
    htruth.trans h_value
  exact no_inbetween_below_downward_frame result (x.toRat / y.toRat) zm ze' f g res_pos
    "Number::normalize 2" hzm_ge hzm_le hf_nn hf_lt h_truth_abs h_rup_pos
    h_result_abs hres_pos_mant_ne
    (fun h => h_pos_transfer (represents_pos_of_shouldRoundUp_downward g ftilde hrep_t h))
    (fun hd hxb => h_zero_transfer (represents_eq_zero_of_digits_zero_xbit_false hd hxb hrep_t))
    hzm_succ
    (fun h_pos => by
      have hzn_false : zn = false := zn_eq_false_of_pos hsign.1 h_pos
      exact Number.toRat_nonneg_of_nonnegative result (h_neg.trans hzn_false))
    (fun h_negT => by
      have hzn_true : zn = true := zn_eq_true_of_neg hsign.2 h_negT
      exact h_sbit.trans hzn_true)
    (fun h_pos => by
      have hzn_false : zn = false := zn_eq_false_of_pos hsign.1 h_pos
      exact h_sbit.trans hzn_false)
    h_le


/-- `operator_div` under `.downward` is **correctly rounded**: the result is
`Number.lower` of the exact quotient. A zero numerator is excluded
automatically by `hresult`; `hresult` itself is essential (the underflow flush
ignores the direction). -/
theorem operator_div_rounded_downward_proof (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hok : Number.operator_div x y .downward = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    Number.RoundsToRepresentable result (x.toRat / y.toRat) .downward := by
  -- The divisor is nonzero (division by the canonical zero errors out).
  have hyz : y.mantissa_ ≠ 0 := operator_div_divisor_ne_zero x y result _ hy hok
  -- Zero numerator contradicts `hresult` (the model returns the operand).
  by_cases hxz : x.mantissa_ = 0
  · exfalso
    have hx_zero : x = Number.zero := Number.eq_zero_of_mantissa_zero x hx hxz
    have hy_guard : ¬ y.operator_eq Number.zero = true := by
      intro h
      have hmeq := Number.mantissa_eq_zero_of_operator_eq_zero h
      exact hyz hmeq
    unfold Number.operator_div at hok
    rw [if_neg hy_guard,
        if_pos (show x.operator_eq Number.zero = true from by rw [hx_zero]; decide)] at hok
    have h_result : result = x :=
      (Except.ok.inj (show (Except.ok x : Except String Number) = .ok result from hok)).symm
    apply hresult
    rw [h_result, hx_zero]
    rfl
  -- Generic path.
  have h_dir := operator_div_rounding_bound_downward_dir x y result hx hy hxz hyz hok hresult
  have h_mag := operator_div_rounding_bound_downward x y result hx hy hxz hyz hok hresult
  have h_result_norm : result.isNormalized :=
    operator_div_result_isNormalized x y result .downward hx hy hxz hyz hok hresult
  exact closest_lower_of_no_inbetween result (x.toRat / y.toRat)
    h_result_norm
    hresult
    (operator_div_truth_ne x y hxz hyz)
    (le_of_lt h_mag)
    (fun h => truth_top_of_result_cap result (x.toRat / y.toRat) h_result_norm hresult
      (le_of_lt h_mag)
      (operator_div_no_overflow_mantissa x y result .downward hx hy hxz hyz hok hresult h) h)
    h_dir
    (operator_div_no_inbetween_below_downward x y result hx hy hxz hyz hok hresult h_dir)

end XRPL.Model.Protocol
