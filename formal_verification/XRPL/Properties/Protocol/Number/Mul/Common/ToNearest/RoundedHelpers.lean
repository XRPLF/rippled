import XRPL.Properties.Protocol.Number.Common.Notation
import XRPL.Properties.Protocol.Number.Mul.Common.ToNearest.AlgorithmicFacts
import XRPL.Properties.Protocol.Number.Mul.Common.ToNearest.BoundProof
import XRPL.Properties.Protocol.Number.Mul.Common.Underflow
import XRPL.Properties.Protocol.Number.Common.Closest.Identify
import XRPL.Properties.Protocol.Number.Common.Closest.NoInbetween


namespace XRPL.Model.Protocol

/-- No normalized Number sits strictly between `result.toRat` and `truth` when
`result.toRat ≤ truth`. -/
theorem operator_mul_no_inbetween_below_to_nearest (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (hok : Number.operator_mul x y .to_nearest = .ok result)
    (hresult : result.mantissa_ ≠ 0)
    (h_le : result.toRat ≤ x.toRat * y.toRat) :
    ∀ m : Number, m.isNormalized →
      result.toRat < m.toRat → ¬ (m.toRat ≤ x.toRat * y.toRat) := by
  obtain ⟨zm, ze', f, g, res_pos, hzm_ge, hzm_le, hf_nn, hf_lt, hfloor_constr, h_truth_abs,
          h_rup_pos, h_result_abs, hres_pos_mant_ne, hf_rep, h_sign⟩ :=
    operator_mul_algorithmic_facts_to_nearest x y result hx hy hx_mant_ne hy_mant_ne hok hresult
  exact no_inbetween_below_to_nearest_frame result (x.toRat * y.toRat) zm ze' f g res_pos
    "Number::multiplication overflow" hzm_ge hzm_le hf_nn hf_lt hfloor_constr h_truth_abs
    h_rup_pos h_result_abs hres_pos_mant_ne
    (fun h_ru => by
      rcases h_ru with h1 | ⟨h0, _⟩
      · linarith [represents_round_eq_one hf_rep h1]
      · linarith [represents_round_eq_zero hf_rep h0])
    (fun hfl hns => hns (Or.inl (represents_f_gt_half hf_rep (by linarith [hfloor_constr hfl]))))
    (fun h_pos => mul_result_nonneg_of_truth_pos x y result h_sign h_pos)
    h_le

/-- No normalized Number sits strictly between `truth` and `result.toRat` when
`truth ≤ result.toRat`. -/
theorem operator_mul_no_inbetween_above (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (hok : Number.operator_mul x y .to_nearest = .ok result)
    (hresult : result.mantissa_ ≠ 0)
    (h_ge : x.toRat * y.toRat ≤ result.toRat) :
    ∀ m : Number, m.isNormalized →
      m.toRat < result.toRat → ¬ (x.toRat * y.toRat ≤ m.toRat) := by
  obtain ⟨zm, ze', f, g, res_pos, hzm_ge, hzm_le, hf_nn, hf_lt, hfloor_constr, h_truth_abs,
          h_rup_pos, h_result_abs, hres_pos_mant_ne, hf_rep, h_sign⟩ :=
    operator_mul_algorithmic_facts_to_nearest x y result hx hy hx_mant_ne hy_mant_ne hok hresult
  exact no_inbetween_above_to_nearest_frame result (x.toRat * y.toRat) zm ze' f g res_pos
    "Number::multiplication overflow" hzm_ge hzm_le hf_nn hf_lt hfloor_constr h_truth_abs
    h_rup_pos h_result_abs hres_pos_mant_ne
    (fun h_ru => by
      rcases h_ru with h1 | ⟨h0, _⟩
      · linarith [represents_round_eq_one hf_rep h1]
      · linarith [represents_round_eq_zero hf_rep h0])
    (fun hfl hns => hns (Or.inl (represents_f_gt_half hf_rep (by linarith [hfloor_constr hfl]))))
    (fun h_neg => mul_result_nonpos_of_truth_neg x y result h_sign h_neg)
    (fun h_pos => mul_result_nonneg_of_truth_pos x y result h_sign h_pos)
    h_ge

/-! ## Branch A: round-down (no round-up fires) — result = lower(truth) -/

/-- When round-up does not fire (`result.toRat ≤ truth`) and no representable lies
strictly between `result` and `truth`, the result equals `Number.lower(truth)`. -/
theorem operator_mul_rounded_branchA (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (hok : Number.operator_mul x y .to_nearest = .ok result)
    (hresult : result.mantissa_ ≠ 0)
    (h_round_down : result.toRat ≤ x.toRat * y.toRat)
    (h_no_inbetween : ∀ m : Number, m.isNormalized →
                       result.toRat < m.toRat → ¬ (m.toRat ≤ x.toRat * y.toRat)) :
    ∃ n_lo : Number, Number.lower (x.toRat * y.toRat) = some n_lo ∧
                     result.toRat = n_lo.toRat := by
  have h5 := operator_mul_rounding_bound_to_nearest x y result hx hy hx_mant_ne hy_mant_ne hok hresult
  have h_result_norm : result.isNormalized :=
    operator_mul_result_isNormalized x y result .to_nearest hx hy hx_mant_ne hy_mant_ne hok hresult
  have h_bound : |result.toRat - x.toRat * y.toRat|
      ≤ |x.toRat * y.toRat| * (11 / (2 ^ 63 - 18 : ℚ)) :=
    le_trans h5 (mul_le_mul_of_nonneg_left (by norm_num) (abs_nonneg _))
  exact closest_lower_of_no_inbetween result (x.toRat * y.toRat)
    h_result_norm
    hresult
    (toRat_mul_ne_zero_of_normalized x y hx hy hx_mant_ne hy_mant_ne)
    h_bound
    (fun h => truth_top_of_result_cap result (x.toRat * y.toRat) h_result_norm hresult h_bound
      (le_trans
        (UInt64.le_iff_toNat_le.mp
          (operator_mul_no_overflow_mantissa x y result .to_nearest hx hy hx_mant_ne hy_mant_ne hok hresult h))
        (by rw [show maxRepUp.toNat = maxRepUpNat from rfl]; norm_num)) h)
    h_round_down h_no_inbetween

/-! ## Branch B: round-up (no cusp overflow) — result = upper(truth) -/

/-- When round-up fires (`truth ≤ result.toRat`) and no representable lies strictly
between `truth` and `result`, the result equals `Number.upper(truth)`. -/
theorem operator_mul_rounded_branchB (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (hok : Number.operator_mul x y .to_nearest = .ok result)
    (hresult : result.mantissa_ ≠ 0)
    (h_round_up : x.toRat * y.toRat ≤ result.toRat)
    (h_no_inbetween : ∀ m : Number, m.isNormalized →
                       m.toRat < result.toRat → ¬ (x.toRat * y.toRat ≤ m.toRat)) :
    ∃ n_up : Number, Number.upper (x.toRat * y.toRat) = some n_up ∧
                     result.toRat = n_up.toRat := by
  have h5 := operator_mul_rounding_bound_to_nearest x y result hx hy hx_mant_ne hy_mant_ne hok hresult
  have h_result_norm : result.isNormalized :=
    operator_mul_result_isNormalized x y result .to_nearest hx hy hx_mant_ne hy_mant_ne hok hresult
  have h_bound : |result.toRat - x.toRat * y.toRat|
      ≤ |x.toRat * y.toRat| * (11 / (2 ^ 63 - 18 : ℚ)) :=
    le_trans h5 (mul_le_mul_of_nonneg_left (by norm_num) (abs_nonneg _))
  exact closest_upper_of_no_inbetween result (x.toRat * y.toRat)
    h_result_norm
    hresult
    (toRat_mul_ne_zero_of_normalized x y hx hy hx_mant_ne hy_mant_ne)
    h_bound
    (fun h => truth_top_of_result_cap result (x.toRat * y.toRat) h_result_norm hresult h_bound
      (le_trans
        (UInt64.le_iff_toNat_le.mp
          (operator_mul_no_overflow_mantissa x y result .to_nearest hx hy hx_mant_ne hy_mant_ne hok hresult h))
        (by rw [show maxRepUp.toNat = maxRepUpNat from rfl]; norm_num)) h)
    h_round_up h_no_inbetween


theorem operator_mul_rounded_to_nearest_proof (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hok : Number.operator_mul x y .to_nearest = .ok result) :
    Number.RoundsToRepresentable result (x.toRat * y.toRat) .to_nearest := by
  -- Zero operands: the model short-circuits to the canonical zero.
  by_cases hxz : x.mantissa_ = 0
  · have hx_zero : x = Number.zero := Number.eq_zero_of_mantissa_zero x hx hxz
    unfold Number.operator_mul at hok
    rw [if_pos (show x.operator_eq Number.zero = true from by rw [hx_zero]; decide)] at hok
    have h_result : result = x :=
      (Except.ok.inj (show (Except.ok x : Except String Number) = .ok result from hok)).symm
    have h_truth0 : x.toRat * y.toRat = 0 := by
      rw [hx_zero, Number.toRat_zero, zero_mul]
    rw [h_truth0]
    left
    refine ⟨Number.zero, ?_, ?_⟩
    · unfold Number.lower
      rw [if_pos rfl]
    · rw [h_result, hx_zero]
  by_cases hyz : y.mantissa_ = 0
  · have hx_guard : ¬ x.operator_eq Number.zero = true := by
      intro h
      have hmeq := Number.mantissa_eq_zero_of_operator_eq_zero h
      exact hxz hmeq
    have hy_zero : y = Number.zero := Number.eq_zero_of_mantissa_zero y hy hyz
    unfold Number.operator_mul at hok
    rw [if_neg hx_guard,
        if_pos (show y.operator_eq Number.zero = true from by rw [hy_zero]; decide)] at hok
    have h_result : result = y :=
      (Except.ok.inj (show (Except.ok y : Except String Number) = .ok result from hok)).symm
    have h_truth0 : x.toRat * y.toRat = 0 := by
      rw [hy_zero, Number.toRat_zero, mul_zero]
    rw [h_truth0]
    left
    refine ⟨Number.zero, ?_, ?_⟩
    · unfold Number.lower
      rw [if_pos rfl]
    · rw [h_result, hy_zero]
  -- Underflow: the flushed zero is a grid neighbor of the tiny product.
  by_cases hres : result.mantissa_ = 0
  · have h_res0 : result.toRat = 0 := by
      have h := abs_toRat_eq result
      rw [show (result.mantissa_.toNat : ℚ) = 0 from by
            rw [show result.mantissa_.toNat = 0 from by rw [hres]; rfl]; norm_num,
          zero_mul] at h
      exact abs_eq_zero.mp h
    have h_truth_ne : x.toRat * y.toRat ≠ 0 :=
      toRat_mul_ne_zero_of_normalized x y hx hy hxz hyz
    have h_small := operator_mul_underflow_truth_small x y result .to_nearest hx hy
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
  have hx_mant_ne : x.mantissa_ ≠ 0 := hxz
  have hy_mant_ne : y.mantissa_ ≠ 0 := hyz
  have hresult : result.mantissa_ ≠ 0 := hres
  set truth : ℚ := x.toRat * y.toRat with htruth_def
  by_cases h_le : result.toRat ≤ truth
  · have h_no_inbetween : ∀ m : Number, m.isNormalized →
                          result.toRat < m.toRat → ¬ (m.toRat ≤ truth) := by
      apply operator_mul_no_inbetween_below_to_nearest x y result hx hy hx_mant_ne hy_mant_ne hok hresult h_le
    have h_branchA := operator_mul_rounded_branchA x y result hx hy hx_mant_ne hy_mant_ne
      hok hresult h_le h_no_inbetween
    obtain ⟨n_lo, h_lo_eq, h_lo_toRat⟩ := h_branchA
    left
    exact ⟨n_lo, h_lo_eq, h_lo_toRat⟩
  · push_neg at h_le
    have h_ge : truth ≤ result.toRat := le_of_lt h_le
    have h_no_inbetween : ∀ m : Number, m.isNormalized →
                          m.toRat < result.toRat → ¬ (truth ≤ m.toRat) := by
      apply operator_mul_no_inbetween_above x y result hx hy hx_mant_ne hy_mant_ne hok hresult h_ge
    have h_branchB := operator_mul_rounded_branchB x y result hx hy hx_mant_ne hy_mant_ne
      hok hresult h_ge h_no_inbetween
    obtain ⟨n_up, h_up_eq, h_up_toRat⟩ := h_branchB
    right
    exact ⟨n_up, h_up_eq, h_up_toRat⟩


end XRPL.Model.Protocol
