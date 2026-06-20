import XRPL.Properties.Protocol.Number.Common.Notation
import XRPL.Properties.Protocol.Number.Div.Common.Decompose
import XRPL.Properties.Protocol.Number.Div.Common.TowardsZero.BoundProof
import XRPL.Properties.Protocol.Number.Common.Closest.Identify
import XRPL.Properties.Protocol.Number.Common.Closest.NoInbetween


namespace XRPL.Model.Protocol

/-- No normalized Number sits strictly between `result.toRat` and the quotient
when `result.toRat ≤ x/y`. -/
theorem operator_div_no_inbetween_below_towards_zero (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (hok : Number.operator_div x y .towards_zero = .ok result)
    (hresult : result.mantissa_ ≠ 0)
    (h_le : result.toRat ≤ x.toRat / y.toRat) :
    ∀ m : Number, m.isNormalized →
      result.toRat < m.toRat → ¬ (m.toRat ≤ x.toRat / y.toRat) := by
  obtain ⟨M, ze0, δ, zn, sticky, hδ_low, _hδ_le, hsticky_zero, hM_pos, hM_lt, hδM,
      htruth, hok128, hsign, hδ_lt, hsticky_pos⟩ :=
    operator_div_algorithmic_facts_represents x y result .towards_zero hx hy
      hx_mant_ne hy_mant_ne hok
  obtain ⟨zm, ze', f, g, res_pos, ftilde, hzm_ge, hzm_le, hf_nn, hf_lt, h_value,
      h_rup_pos, h_result_abs, hres_pos_mant_ne, h_neg, _h_sbit, hzm_succ,
      _hrep_t, _h_pos_transfer, _h_zero_transfer⟩ :=
    doNormalize128_algorithmic_facts zn M ze0 δ sticky .towards_zero hδ_low hδ_lt
      hsticky_zero hsticky_pos hM_pos hM_lt hδM result hok128 hresult
  have h_truth_abs : |x.toRat / y.toRat| = ((zm.toNat : ℚ) + f) * 10 ^ ze' :=
    htruth.trans h_value
  exact no_inbetween_below_towards_zero_frame result (x.toRat / y.toRat) zm ze' f g res_pos
    "Number::normalize 2" hzm_ge hzm_le hf_nn hf_lt h_truth_abs h_rup_pos
    h_result_abs hres_pos_mant_ne hzm_succ
    (fun h_pos => by
      have hzn_false : zn = false := zn_eq_false_of_pos hsign.1 h_pos
      exact Number.toRat_nonneg_of_nonnegative result (h_neg.trans hzn_false))
    h_le

/-- No normalized Number sits strictly between the quotient and `result.toRat`
when `x/y ≤ result.toRat`. -/
theorem operator_div_no_inbetween_above_towards_zero (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (hok : Number.operator_div x y .towards_zero = .ok result)
    (hresult : result.mantissa_ ≠ 0)
    (h_ge : x.toRat / y.toRat ≤ result.toRat) :
    ∀ m : Number, m.isNormalized →
      m.toRat < result.toRat → ¬ (x.toRat / y.toRat ≤ m.toRat) := by
  obtain ⟨M, ze0, δ, zn, sticky, hδ_low, _hδ_le, hsticky_zero, hM_pos, hM_lt, hδM,
      htruth, hok128, hsign, hδ_lt, hsticky_pos⟩ :=
    operator_div_algorithmic_facts_represents x y result .towards_zero hx hy
      hx_mant_ne hy_mant_ne hok
  obtain ⟨zm, ze', f, g, res_pos, ftilde, hzm_ge, hzm_le, hf_nn, hf_lt, h_value,
      h_rup_pos, h_result_abs, hres_pos_mant_ne, h_neg, _h_sbit, hzm_succ,
      _hrep_t, _h_pos_transfer, _h_zero_transfer⟩ :=
    doNormalize128_algorithmic_facts zn M ze0 δ sticky .towards_zero hδ_low hδ_lt
      hsticky_zero hsticky_pos hM_pos hM_lt hδM result hok128 hresult
  have h_truth_abs : |x.toRat / y.toRat| = ((zm.toNat : ℚ) + f) * 10 ^ ze' :=
    htruth.trans h_value
  exact no_inbetween_above_towards_zero_frame result (x.toRat / y.toRat) zm ze' f g res_pos
    "Number::normalize 2" hzm_ge hzm_le hf_nn hf_lt h_truth_abs h_rup_pos
    h_result_abs hres_pos_mant_ne hzm_succ
    (fun h_negT => by
      have hzn_true : zn = true := zn_eq_true_of_neg hsign.2 h_negT
      exact Number.toRat_nonpos_of_negative result (h_neg.trans hzn_true))
    h_ge

/-- Underflow dispatch: a zero result mantissa always lands on the correct
grid point for `.towards_zero`. -/
lemma div_rounded_towards_zero_of_result_zero (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (hok : Number.operator_div x y .towards_zero = .ok result)
    (hres : result.mantissa_ = 0) :
    Number.RoundsToRepresentable result (x.toRat / y.toRat) .towards_zero := by
  have h_res0 : result.toRat = 0 := Number.toRat_eq_zero_of_mantissa_zero result hres
  have h_truth_ne : x.toRat / y.toRat ≠ 0 := operator_div_truth_ne x y hx_mant_ne hy_mant_ne
  have h_small := operator_div_underflow_truth_small x y result .towards_zero hx hy
    hx_mant_ne hy_mant_ne hok hres
  change ∃ n' : Number, (if x.toRat / y.toRat ≥ 0 then Number.lower (x.toRat / y.toRat)
    else Number.upper (x.toRat / y.toRat)) = some n' ∧ result.toRat = n'.toRat
  by_cases h_truth_nn : x.toRat / y.toRat ≥ 0
  · rw [if_pos h_truth_nn]
    have h_truth_pos : 0 < x.toRat / y.toRat :=
      lt_of_le_of_ne h_truth_nn (Ne.symm h_truth_ne)
    refine ⟨Number.zero, ?_, ?_⟩
    · exact Number.lower_eq_zero_of_pos_small _ h_truth_pos
        (by rwa [abs_of_pos h_truth_pos] at h_small)
    · rw [h_res0, Number.toRat_zero]
  · rw [if_neg h_truth_nn]
    push_neg at h_truth_nn
    refine ⟨Number.zero, ?_, ?_⟩
    · exact Number.upper_eq_zero_of_neg_small _ h_truth_nn
        (by rwa [abs_of_neg h_truth_nn] at h_small)
    · rw [h_res0, Number.toRat_zero]


/-- `operator_div` under `.towards_zero` is **correctly rounded**: the result
is `Number.lower` of the quotient for nonnegative quotients and `Number.upper`
for negative ones. -/
theorem operator_div_rounded_towards_zero_proof (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hok : Number.operator_div x y .towards_zero = .ok result) :
    Number.RoundsToRepresentable result (x.toRat / y.toRat) .towards_zero := by
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
    change ∃ n' : Number, (if (0 : ℚ) ≥ 0 then Number.lower 0 else Number.upper 0)
      = some n' ∧ result.toRat = n'.toRat
    rw [if_pos (by norm_num : (0 : ℚ) ≥ 0)]
    refine ⟨Number.zero, ?_, ?_⟩
    · unfold Number.lower
      rw [if_pos rfl]
    · rw [h_result, hx_zero]
  -- Underflow: flush-to-zero is correct rounding toward zero.
  by_cases hres : result.mantissa_ = 0
  · exact div_rounded_towards_zero_of_result_zero x y result hx hy hxz hyz hok hres
  -- Generic path.
  have hresult : result.mantissa_ ≠ 0 := hres
  have h_dir := operator_div_rounding_bound_towards_zero_dir x y result hx hy hxz hyz hok hresult
  have h_mag := operator_div_rounding_bound_towards_zero x y result hx hy hxz hyz hok hresult
  have h_result_norm : result.isNormalized :=
    operator_div_result_isNormalized x y result .towards_zero hx hy hxz hyz hok hresult
  have h_truth_ne : x.toRat / y.toRat ≠ 0 := operator_div_truth_ne x y hxz hyz
  have h_bound := le_of_lt h_mag
  have h_truth_top : result.exponent_ ≥ maxExponent →
      |x.toRat / y.toRat| < 10 ^ 19 * (10 : ℚ) ^ (maxExponent : ℤ) := fun h =>
    truth_top_of_result_cap result (x.toRat / y.toRat) h_result_norm hresult h_bound
      (operator_div_no_overflow_mantissa x y result .towards_zero hx hy hxz hyz hok hresult h) h
  change ∃ n' : Number, (if x.toRat / y.toRat ≥ 0 then Number.lower (x.toRat / y.toRat)
    else Number.upper (x.toRat / y.toRat)) = some n' ∧ result.toRat = n'.toRat
  by_cases h_truth_nn : x.toRat / y.toRat ≥ 0
  · rw [if_pos h_truth_nn]
    -- Direction: result ≤ |result| ≤ |truth| = truth.
    have h_round_down : result.toRat ≤ x.toRat / y.toRat := by
      calc result.toRat ≤ |result.toRat| := le_abs_self _
        _ ≤ |x.toRat / y.toRat| := h_dir
        _ = x.toRat / y.toRat := abs_of_nonneg h_truth_nn
    exact closest_lower_of_no_inbetween result (x.toRat / y.toRat) h_result_norm hresult
      h_truth_ne h_bound h_truth_top h_round_down
      (operator_div_no_inbetween_below_towards_zero x y result hx hy hxz hyz
        hok hresult h_round_down)
  · rw [if_neg h_truth_nn]
    push_neg at h_truth_nn
    have h_truth_np : x.toRat / y.toRat ≤ 0 := le_of_lt h_truth_nn
    -- Direction: truth = −|truth| ≤ −|result| ≤ result.
    have h_round_up : x.toRat / y.toRat ≤ result.toRat := by
      have h1 : x.toRat / y.toRat = -|x.toRat / y.toRat| := by
        rw [abs_of_nonpos h_truth_np]; ring
      have h2 : -|result.toRat| ≤ result.toRat := neg_abs_le _
      linarith [neg_le_neg h_dir]
    exact closest_upper_of_no_inbetween result (x.toRat / y.toRat) h_result_norm hresult
      h_truth_ne h_bound h_truth_top h_round_up
      (operator_div_no_inbetween_above_towards_zero x y result hx hy hxz hyz
        hok hresult h_round_up)

end XRPL.Model.Protocol
