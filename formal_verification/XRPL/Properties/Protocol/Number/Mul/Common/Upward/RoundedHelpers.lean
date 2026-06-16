import XRPL.Properties.Protocol.Number.Common.Notation
import XRPL.Properties.Protocol.Number.Mul.Common.Upward.AlgorithmicFacts
import XRPL.Properties.Protocol.Number.Mul.Common.Upward.BoundProof
import XRPL.Properties.Protocol.Number.Mul.Common.Underflow
import XRPL.Properties.Protocol.Number.Common.Closest.Identify
import XRPL.Properties.Protocol.Number.Common.Closest.NoInbetween


namespace XRPL.Model.Protocol

/-- No normalized Number sits strictly between `truth` and `result.toRat`. -/
theorem operator_mul_no_inbetween_above_upward (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (hok : Number.operator_mul x y .upward = .ok result)
    (hresult : result.mantissa_ ≠ 0)
    (h_ge : x.toRat * y.toRat ≤ result.toRat) :
    ∀ m : Number, m.isNormalized →
      m.toRat < result.toRat → ¬ (x.toRat * y.toRat ≤ m.toRat) := by
  obtain ⟨zm, ze', f, g, res_pos, hzm_ge, hzm_le, hf_nn, hf_lt, _hfloor_constr, h_truth_abs,
          h_rup_pos, h_result_abs, hres_pos_mant_ne, hf_rep, h_sign, h_g_sbit, hzm_succ⟩ :=
    operator_mul_algorithmic_facts_upward x y result hx hy hx_mant_ne hy_mant_ne hok hresult
  exact no_inbetween_above_upward_frame result (x.toRat * y.toRat) zm ze' f g res_pos
    "Number::multiplication overflow" hzm_ge hzm_le hf_nn hf_lt h_truth_abs h_rup_pos
    h_result_abs hres_pos_mant_ne
    (fun h => represents_pos_of_shouldRoundUp_upward g f hf_rep h)
    (fun hd hxb => represents_eq_zero_of_digits_zero_xbit_false hd hxb hf_rep)
    hzm_succ
    (fun h_neg => mul_result_nonpos_of_truth_neg x y result h_sign h_neg)
    (fun h_neg => by rw [h_g_sbit]; exact mul_zn_true_of_truth_neg x y h_neg)
    (fun h_pos => by rw [h_g_sbit]; exact mul_zn_false_of_truth_pos x y h_pos)
    h_ge


theorem operator_mul_rounded_upward_proof (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hok : Number.operator_mul x y .upward = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    Number.RoundsToRepresentable result (x.toRat * y.toRat) .upward := by
  -- Zero operands contradict `hresult` (the model returns the zero operand).
  by_cases hxz : x.mantissa_ = 0
  · exfalso
    have hx_zero : x = Number.zero := Number.eq_zero_of_mantissa_zero x hx hxz
    unfold Number.operator_mul at hok
    rw [if_pos (show x.operator_eq Number.zero = true from by rw [hx_zero]; decide)] at hok
    have h_result : result = x :=
      (Except.ok.inj (show (Except.ok x : Except String Number) = .ok result from hok)).symm
    apply hresult
    rw [h_result, hx_zero]
    rfl
  by_cases hyz : y.mantissa_ = 0
  · exfalso
    have hx_guard : ¬ x.operator_eq Number.zero = true := by
      intro h
      have hmeq := Number.mantissa_eq_zero_of_operator_eq_zero h
      exact hxz hmeq
    have hy_zero : y = Number.zero := Number.eq_zero_of_mantissa_zero y hy hyz
    unfold Number.operator_mul at hok
    rw [if_neg hx_guard,
        if_pos (show y.operator_eq Number.zero = true from by rw [hy_zero]; decide)] at hok
    have h_result : result = y :=
      (Except.ok.inj (show (Except.ok y : Except String Number) = .ok result from hok)).symm
    apply hresult
    rw [h_result, hy_zero]
    rfl
  -- Generic path.
  obtain ⟨h_dir, h_mag⟩ :=
    operator_mul_rounding_bound_upward x y result hx hy hxz hyz hok hresult
  have h_bound : |result.toRat - x.toRat * y.toRat|
      ≤ |x.toRat * y.toRat| * (11 / (2 ^ 63 - 18 : ℚ)) := by
    rw [abs_of_nonneg (by linarith : (0 : ℚ) ≤ result.toRat - x.toRat * y.toRat)]
    have h_eps : |x.toRat * y.toRat| * (10 / (2 ^ 63 + 2 : ℚ))
        ≤ |x.toRat * y.toRat| * (11 / (2 ^ 63 - 18 : ℚ)) :=
      mul_le_mul_of_nonneg_left (by norm_num) (abs_nonneg _)
    linarith [le_of_lt h_mag]
  have h_result_norm : result.isNormalized :=
    operator_mul_result_isNormalized x y result .upward hx hy hxz hyz hok hresult
  exact closest_upper_of_no_inbetween result (x.toRat * y.toRat)
    h_result_norm
    hresult
    (toRat_mul_ne_zero_of_normalized x y hx hy hxz hyz)
    h_bound
    (fun h => truth_top_of_result_cap result (x.toRat * y.toRat) h_result_norm hresult h_bound
      (le_trans
        (UInt64.le_iff_toNat_le.mp
          (operator_mul_no_overflow_mantissa x y result .upward hx hy hxz hyz hok hresult h))
        (by rw [show maxRepUp.toNat = maxRepUpNat from rfl]; norm_num)) h)
    h_dir
    (operator_mul_no_inbetween_above_upward x y result hx hy hxz hyz hok hresult h_dir)

end XRPL.Model.Protocol
