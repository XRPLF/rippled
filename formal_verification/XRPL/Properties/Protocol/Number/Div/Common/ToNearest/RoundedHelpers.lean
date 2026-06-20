import XRPL.Properties.Protocol.Number.Common.Notation
import XRPL.Properties.Protocol.Number.Div.Common.Decompose
import XRPL.Properties.Protocol.Number.Div.Common.ToNearest.BoundProof
import XRPL.Properties.Protocol.Number.Common.Closest.Identify
import XRPL.Properties.Protocol.Number.Common.Closest.NoInbetween


namespace XRPL.Model.Protocol

/-- No normalized Number sits strictly between `result.toRat` and the quotient
when `result.toRat ≤ x/y`. -/
theorem operator_div_no_inbetween_below_to_nearest (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (hok : Number.operator_div x y .to_nearest = .ok result)
    (hresult : result.mantissa_ ≠ 0)
    (h_le : result.toRat ≤ x.toRat / y.toRat) :
    ∀ m : Number, m.isNormalized →
      result.toRat < m.toRat → ¬ (m.toRat ≤ x.toRat / y.toRat) := by
  obtain ⟨M, ze0, δ, zn, sticky, hδ_low, _hδ_le, hsticky_zero, hM_pos, hM_lt, hδM,
      htruth, hok128, hsign, hδ_lt, hsticky_pos⟩ :=
    operator_div_algorithmic_facts_represents x y result .to_nearest hx hy
      hx_mant_ne hy_mant_ne hok
  obtain ⟨zm, ze', f, g, res_pos, ftilde, hzm_ge, hzm_le, hf_nn, hf_lt, h_value,
      h_rup_pos, h_result_abs, hres_pos_mant_ne, h_neg, _h_sbit, hzm_succ,
      hrep_t, h_pos_transfer, _h_zero_transfer⟩ :=
    doNormalize128_algorithmic_facts zn M ze0 δ sticky .to_nearest hδ_low hδ_lt
      hsticky_zero hsticky_pos hM_pos hM_lt hδM result hok128 hresult
  have h_truth_abs : |x.toRat / y.toRat| = ((zm.toNat : ℚ) + f) * 10 ^ ze' :=
    htruth.trans h_value
  exact no_inbetween_below_to_nearest_frame result (x.toRat / y.toRat) zm ze' f g res_pos
    "Number::normalize 2" hzm_ge hzm_le hf_nn hf_lt
    (fun hfl => absurd hfl (by omega)) h_truth_abs h_rup_pos
    h_result_abs hres_pos_mant_ne
    (fun h_ru => h_pos_transfer (by
      rcases h_ru with h1 | ⟨h0, _⟩
      · linarith [represents_round_eq_one hrep_t h1]
      · linarith [represents_round_eq_zero hrep_t h0]))
    (fun hfl _ => absurd hfl (by omega))
    (fun h_pos => by
      have hzn_false : zn = false := zn_eq_false_of_pos hsign.1 h_pos
      exact Number.toRat_nonneg_of_nonnegative result (h_neg.trans hzn_false))
    h_le

/-- No normalized Number sits strictly between the quotient and `result.toRat`
when `x/y ≤ result.toRat`. -/
theorem operator_div_no_inbetween_above (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (hok : Number.operator_div x y .to_nearest = .ok result)
    (hresult : result.mantissa_ ≠ 0)
    (h_ge : x.toRat / y.toRat ≤ result.toRat) :
    ∀ m : Number, m.isNormalized →
      m.toRat < result.toRat → ¬ (x.toRat / y.toRat ≤ m.toRat) := by
  obtain ⟨M, ze0, δ, zn, sticky, hδ_low, _hδ_le, hsticky_zero, hM_pos, hM_lt, hδM,
      htruth, hok128, hsign, hδ_lt, hsticky_pos⟩ :=
    operator_div_algorithmic_facts_represents x y result .to_nearest hx hy
      hx_mant_ne hy_mant_ne hok
  obtain ⟨zm, ze', f, g, res_pos, ftilde, hzm_ge, hzm_le, hf_nn, hf_lt, h_value,
      h_rup_pos, h_result_abs, hres_pos_mant_ne, h_neg, _h_sbit, hzm_succ,
      hrep_t, h_pos_transfer, _h_zero_transfer⟩ :=
    doNormalize128_algorithmic_facts zn M ze0 δ sticky .to_nearest hδ_low hδ_lt
      hsticky_zero hsticky_pos hM_pos hM_lt hδM result hok128 hresult
  have h_truth_abs : |x.toRat / y.toRat| = ((zm.toNat : ℚ) + f) * 10 ^ ze' :=
    htruth.trans h_value
  exact no_inbetween_above_to_nearest_frame result (x.toRat / y.toRat) zm ze' f g res_pos
    "Number::normalize 2" hzm_ge hzm_le hf_nn hf_lt
    (fun hfl => absurd hfl (by omega)) h_truth_abs h_rup_pos
    h_result_abs hres_pos_mant_ne
    (fun h_ru => h_pos_transfer (by
      rcases h_ru with h1 | ⟨h0, _⟩
      · linarith [represents_round_eq_one hrep_t h1]
      · linarith [represents_round_eq_zero hrep_t h0]))
    (fun hfl _ => absurd hfl (by omega))
    (fun h_negT => by
      have hzn_true : zn = true := zn_eq_true_of_neg hsign.2 h_negT
      exact Number.toRat_nonpos_of_negative result (h_neg.trans hzn_true))
    (fun h_pos => by
      have hzn_false : zn = false := zn_eq_false_of_pos hsign.1 h_pos
      exact Number.toRat_nonneg_of_nonnegative result (h_neg.trans hzn_false))
    h_ge


/-- `operator_div` under `.to_nearest` is **faithfully rounded**: the result
is `Number.lower` or `Number.upper` of the exact quotient. -/
theorem operator_div_rounded_to_nearest_proof (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hok : Number.operator_div x y .to_nearest = .ok result) :
    Number.RoundsToRepresentable result (x.toRat / y.toRat) .to_nearest := by
  -- The divisor is nonzero (division by the canonical zero errors out).
  have hyz : y.mantissa_ ≠ 0 := operator_div_divisor_ne_zero x y result _ hy hok
  -- Zero numerator: the model short-circuits to the canonical zero.
  by_cases hxz : x.mantissa_ = 0
  · have hx_zero : x = Number.zero := Number.eq_zero_of_mantissa_zero x hx hxz
    have hy_guard : ¬ y.operator_eq Number.zero = true := by
      intro h
      have hmeq := Number.mantissa_eq_zero_of_operator_eq_zero h
      exact hyz hmeq
    unfold Number.operator_div at hok
    rw [if_neg hy_guard,
        if_pos (show x.operator_eq Number.zero = true from by rw [hx_zero]; decide)] at hok
    have h_result : result = x :=
      (Except.ok.inj (show (Except.ok x : Except String Number) = .ok result from hok)).symm
    have h_truth0 : x.toRat / y.toRat = 0 := by
      rw [hx_zero, Number.toRat_zero, zero_div]
    rw [h_truth0]
    left
    refine ⟨Number.zero, ?_, ?_⟩
    · unfold Number.lower
      rw [if_pos rfl]
    · rw [h_result, hx_zero]
  -- Underflow: the flushed zero is a grid neighbor of the tiny quotient.
  by_cases hres : result.mantissa_ = 0
  · have h_res0 : result.toRat = 0 := Number.toRat_eq_zero_of_mantissa_zero result hres
    have h_truth_ne : x.toRat / y.toRat ≠ 0 := operator_div_truth_ne x y hxz hyz
    have h_small := operator_div_underflow_truth_small x y result .to_nearest hx hy
      hxz hyz hok hres
    rcases lt_or_gt_of_ne h_truth_ne with h_neg | h_pos
    · right
      refine ⟨Number.zero, ?_, ?_⟩
      · exact Number.upper_eq_zero_of_neg_small _ h_neg
          (by rwa [abs_of_neg h_neg] at h_small)
      · rw [h_res0, Number.toRat_zero]
    · left
      refine ⟨Number.zero, ?_, ?_⟩
      · exact Number.lower_eq_zero_of_pos_small _ h_pos
          (by rwa [abs_of_pos h_pos] at h_small)
      · rw [h_res0, Number.toRat_zero]
  -- Generic path.
  have hresult : result.mantissa_ ≠ 0 := hres
  have h5 := operator_div_rounding_bound_to_nearest x y result hx hy hxz hyz hok hresult
  have h_result_norm : result.isNormalized :=
    operator_div_result_isNormalized x y result .to_nearest hx hy hxz hyz hok hresult
  have h_truth_ne : x.toRat / y.toRat ≠ 0 := operator_div_truth_ne x y hxz hyz
  have h_bound : |result.toRat - x.toRat / y.toRat|
      ≤ |x.toRat / y.toRat| * (11 / (2 ^ 63 - 18 : ℚ)) :=
    le_trans h5 (mul_le_mul_of_nonneg_left (by norm_num) (abs_nonneg _))
  have h_truth_top : result.exponent_ ≥ maxExponent →
      |x.toRat / y.toRat| < 10 ^ 19 * (10 : ℚ) ^ (maxExponent : ℤ) := fun h =>
    truth_top_of_result_cap result (x.toRat / y.toRat) h_result_norm hresult h_bound
      (operator_div_no_overflow_mantissa x y result .to_nearest hx hy hxz hyz hok hresult h) h
  by_cases h_le : result.toRat ≤ x.toRat / y.toRat
  · left
    exact closest_lower_of_no_inbetween result (x.toRat / y.toRat) h_result_norm hresult
      h_truth_ne h_bound h_truth_top h_le
      (operator_div_no_inbetween_below_to_nearest x y result hx hy hxz hyz hok hresult h_le)
  · push_neg at h_le
    have h_ge : x.toRat / y.toRat ≤ result.toRat := le_of_lt h_le
    right
    exact closest_upper_of_no_inbetween result (x.toRat / y.toRat) h_result_norm hresult
      h_truth_ne h_bound h_truth_top h_ge
      (operator_div_no_inbetween_above x y result hx hy hxz hyz hok hresult h_ge)

end XRPL.Model.Protocol
