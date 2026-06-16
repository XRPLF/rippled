import XRPL.Properties.Protocol.Number.Common.Notation
import XRPL.Properties.Protocol.Number.Normalize.Common.ToNearest.AlgorithmicFacts
import XRPL.Properties.Protocol.Number.Normalize.Common.ToNearest.BoundProof
import XRPL.Properties.Protocol.Number.Normalize.Common.Underflow
import XRPL.Properties.Protocol.Number.Normalize.Common.ResultFacts
import XRPL.Properties.Protocol.Number.Common.Closest.Identify
import XRPL.Properties.Protocol.Number.Common.Closest.NoInbetween


namespace XRPL.Model.Protocol

/-! # `Number.normalize` is faithfully rounded under `.to_nearest`

The result is `Number.lower` or `Number.upper` of the input value. (True
nearest-ness is quantized at the `pushOverflow` cusp, exactly as for the
arithmetic operators — faithful rounding is the C++-faithful statement.)
No nonzero hypotheses: a zero input mantissa short-circuits to the zero grid
point, and an underflow flush lands on zero, which is a grid neighbor of any
input below the smallest positive representable. -/

/-- No normalized Number sits strictly between `result.toRat` and `n.toRat`
when `result.toRat ≤ n.toRat`. -/
theorem normalize_no_inbetween_below_to_nearest (n result : Number)
    (hn_mant_ne : n.mantissa_ ≠ 0)
    (hok : n.normalize largeRange.min largeRange.max .to_nearest = .ok result)
    (hresult : result.mantissa_ ≠ 0)
    (h_le : result.toRat ≤ n.toRat) :
    ∀ m : Number, m.isNormalized →
      result.toRat < m.toRat → ¬ (m.toRat ≤ n.toRat) := by
  obtain ⟨zm, ze', f, g, res_pos, hzm_ge, hzm_le, hfloor_constr, h_truth_abs,
          h_rup_pos, h_result_abs, hres_pos_mant_ne, hf_rep, h_sign⟩ :=
    normalize_algorithmic_facts_to_nearest n result hn_mant_ne hok hresult
  exact no_inbetween_below_to_nearest_frame result n.toRat zm ze' f g res_pos
    "Number::normalize 2" hzm_ge hzm_le (represents_nonneg hf_rep)
    (represents_lt_one hf_rep) hfloor_constr h_truth_abs h_rup_pos
    h_result_abs hres_pos_mant_ne
    (fun h_ru => by
      rcases h_ru with h1 | ⟨h0, _⟩
      · linarith [represents_round_eq_one hf_rep h1]
      · linarith [represents_round_eq_zero hf_rep h0])
    (fun hfl hns => hns (Or.inl (represents_f_gt_half hf_rep (by linarith [hfloor_constr hfl]))))
    (fun h_pos => by
      have hn_nneg : n.negative_ = false := by
        rcases hn : n.negative_ with _ | _
        · rfl
        · exact absurd (Number.toRat_nonpos_of_negative n hn) (not_le.mpr h_pos)
      exact Number.toRat_nonneg_of_nonnegative result (h_sign.trans hn_nneg))
    h_le

/-- No normalized Number sits strictly between `n.toRat` and `result.toRat`
when `n.toRat ≤ result.toRat`. -/
theorem normalize_no_inbetween_above (n result : Number)
    (hn_mant_ne : n.mantissa_ ≠ 0)
    (hok : n.normalize largeRange.min largeRange.max .to_nearest = .ok result)
    (hresult : result.mantissa_ ≠ 0)
    (h_ge : n.toRat ≤ result.toRat) :
    ∀ m : Number, m.isNormalized →
      m.toRat < result.toRat → ¬ (n.toRat ≤ m.toRat) := by
  obtain ⟨zm, ze', f, g, res_pos, hzm_ge, hzm_le, hfloor_constr, h_truth_abs,
          h_rup_pos, h_result_abs, hres_pos_mant_ne, hf_rep, h_sign⟩ :=
    normalize_algorithmic_facts_to_nearest n result hn_mant_ne hok hresult
  exact no_inbetween_above_to_nearest_frame result n.toRat zm ze' f g res_pos
    "Number::normalize 2" hzm_ge hzm_le (represents_nonneg hf_rep)
    (represents_lt_one hf_rep) hfloor_constr h_truth_abs h_rup_pos
    h_result_abs hres_pos_mant_ne
    (fun h_ru => by
      rcases h_ru with h1 | ⟨h0, _⟩
      · linarith [represents_round_eq_one hf_rep h1]
      · linarith [represents_round_eq_zero hf_rep h0])
    (fun hfl hns => hns (Or.inl (represents_f_gt_half hf_rep (by linarith [hfloor_constr hfl]))))
    (fun h_neg => by
      have hn_neg : n.negative_ = true := by
        rcases hn : n.negative_ with _ | _
        · exact absurd (Number.toRat_nonneg_of_nonnegative n hn) (not_le.mpr h_neg)
        · rfl
      exact Number.toRat_nonpos_of_negative result (h_sign.trans hn_neg))
    (fun h_pos => by
      have hn_nneg : n.negative_ = false := by
        rcases hn : n.negative_ with _ | _
        · rfl
        · exact absurd (Number.toRat_nonpos_of_negative n hn) (not_le.mpr h_pos)
      exact Number.toRat_nonneg_of_nonnegative result (h_sign.trans hn_nneg))
    h_ge


/-- `Number.normalize` under `.to_nearest` is **faithfully rounded**: the
result is `Number.lower` or `Number.upper` of the input value. Only `hok` is
assumed. -/
theorem normalize_rounded_to_nearest_proof (n result : Number)
    (hok : n.normalize largeRange.min largeRange.max .to_nearest = .ok result) :
    Number.RoundsToRepresentable result n.toRat .to_nearest := by
  -- Zero input: the model short-circuits to the canonical zero.
  by_cases hnz : n.mantissa_ = 0
  · have h_truth0 : n.toRat = 0 := Number.toRat_eq_zero_of_mantissa_zero n hnz
    unfold Number.normalize doNormalize at hok
    rw [show (n.mantissa_ == 0) = true from beq_iff_eq.mpr hnz, if_pos rfl] at hok
    have h_result : result = Number.zero := (Except.ok.inj hok).symm
    rw [h_truth0]
    left
    refine ⟨Number.zero, ?_, ?_⟩
    · unfold Number.lower
      rw [if_pos rfl]
    · rw [h_result]
  -- Underflow: the flushed zero is a grid neighbor of the tiny input.
  by_cases hres : result.mantissa_ = 0
  · have h_res0 : result.toRat = 0 := Number.toRat_eq_zero_of_mantissa_zero result hres
    have h_truth_ne : n.toRat ≠ 0 := Number.toRat_ne_zero_of_mantissa_ne_zero n hnz
    have h_small := normalize_underflow_truth_small n result .to_nearest hnz hok hres
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
  have h5 := normalize_rounding_bound_to_nearest n result hnz hok hresult
  have h_result_norm : result.isNormalized :=
    normalize_result_isNormalized n result .to_nearest hnz hok hresult
  have h_truth_ne : n.toRat ≠ 0 := Number.toRat_ne_zero_of_mantissa_ne_zero n hnz
  have h_bound : |result.toRat - n.toRat| ≤ |n.toRat| * (11 / (2 ^ 63 - 18 : ℚ)) :=
    le_trans h5 (mul_le_mul_of_nonneg_left (by norm_num) (abs_nonneg _))
  have h_truth_top : result.exponent_ ≥ maxExponent →
      |n.toRat| < 10 ^ 19 * (10 : ℚ) ^ (maxExponent : ℤ) := fun h =>
    truth_top_of_result_cap result n.toRat h_result_norm hresult h_bound
      (le_trans
        (UInt64.le_iff_toNat_le.mp
          (normalize_no_overflow_mantissa n result .to_nearest hnz hok hresult h))
        (by rw [show maxRepUp.toNat = maxRepUpNat from rfl]; norm_num)) h
  by_cases h_le : result.toRat ≤ n.toRat
  · left
    exact closest_lower_of_no_inbetween result n.toRat h_result_norm hresult
      h_truth_ne h_bound h_truth_top h_le
      (normalize_no_inbetween_below_to_nearest n result hnz hok hresult h_le)
  · push_neg at h_le
    have h_ge : n.toRat ≤ result.toRat := le_of_lt h_le
    right
    exact closest_upper_of_no_inbetween result n.toRat h_result_norm hresult
      h_truth_ne h_bound h_truth_top h_ge
      (normalize_no_inbetween_above n result hnz hok hresult h_ge)

end XRPL.Model.Protocol
