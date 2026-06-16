import XRPL.Properties.Protocol.Number.Common.Notation
import XRPL.Properties.Protocol.Number.Mul.Common.TowardsZero.AlgorithmicFacts
import XRPL.Properties.Protocol.Number.Mul.Common.TowardsZero.BoundProof
import XRPL.Properties.Protocol.Number.Mul.Common.Underflow
import XRPL.Properties.Protocol.Number.Common.Closest.Identify
import XRPL.Properties.Protocol.Number.Common.Closest.NoInbetween


namespace XRPL.Model.Protocol

/-- No normalized Number sits strictly between `result.toRat` and `truth` when
`result.toRat ≤ truth`. -/
theorem operator_mul_no_inbetween_below_towards_zero (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (hok : Number.operator_mul x y .towards_zero = .ok result)
    (hresult : result.mantissa_ ≠ 0)
    (h_le : result.toRat ≤ x.toRat * y.toRat) :
    ∀ m : Number, m.isNormalized →
      result.toRat < m.toRat → ¬ (m.toRat ≤ x.toRat * y.toRat) := by
  obtain ⟨zm, ze', f, g, res_pos, hzm_ge, hzm_le, hf_nn, hf_lt, h_truth_abs,
          h_rup_pos, h_result_abs, hres_pos_mant_ne, _hf_rep, h_sign, hzm_succ⟩ :=
    operator_mul_algorithmic_facts_towards_zero x y result hx hy hx_mant_ne hy_mant_ne hok hresult
  exact no_inbetween_below_towards_zero_frame result (x.toRat * y.toRat) zm ze' f g res_pos
    "Number::multiplication overflow" hzm_ge hzm_le hf_nn hf_lt h_truth_abs h_rup_pos
    h_result_abs hres_pos_mant_ne hzm_succ
    (fun h_pos => mul_result_nonneg_of_truth_pos x y result h_sign h_pos)
    h_le

/-- No normalized Number sits strictly between `truth` and `result.toRat` when
`truth ≤ result.toRat`. -/
theorem operator_mul_no_inbetween_above_towards_zero (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (hok : Number.operator_mul x y .towards_zero = .ok result)
    (hresult : result.mantissa_ ≠ 0)
    (h_ge : x.toRat * y.toRat ≤ result.toRat) :
    ∀ m : Number, m.isNormalized →
      m.toRat < result.toRat → ¬ (x.toRat * y.toRat ≤ m.toRat) := by
  obtain ⟨zm, ze', f, g, res_pos, hzm_ge, hzm_le, hf_nn, hf_lt, h_truth_abs,
          h_rup_pos, h_result_abs, hres_pos_mant_ne, _hf_rep, h_sign, hzm_succ⟩ :=
    operator_mul_algorithmic_facts_towards_zero x y result hx hy hx_mant_ne hy_mant_ne hok hresult
  exact no_inbetween_above_towards_zero_frame result (x.toRat * y.toRat) zm ze' f g res_pos
    "Number::multiplication overflow" hzm_ge hzm_le hf_nn hf_lt h_truth_abs h_rup_pos
    h_result_abs hres_pos_mant_ne hzm_succ
    (fun h_neg => mul_result_nonpos_of_truth_neg x y result h_sign h_neg)
    h_ge

/-- Zero-operand and underflow dispatch shared by the discrete theorems: a zero
result mantissa always lands on the correct grid point for `.towards_zero`
(flush-to-zero *is* rounding toward zero). -/
lemma rounded_towards_zero_of_result_zero (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (hok : Number.operator_mul x y .towards_zero = .ok result)
    (hres : result.mantissa_ = 0) :
    Number.RoundsToRepresentable result (x.toRat * y.toRat) .towards_zero := by
  have h_res0 : result.toRat = 0 := by
    have h := abs_toRat_eq result
    rw [show (result.mantissa_.toNat : ℚ) = 0 from by
          rw [show result.mantissa_.toNat = 0 from by rw [hres]; rfl]; norm_num,
        zero_mul] at h
    exact abs_eq_zero.mp h
  have h_truth_ne : x.toRat * y.toRat ≠ 0 :=
    toRat_mul_ne_zero_of_normalized x y hx hy hx_mant_ne hy_mant_ne
  have h_small := operator_mul_underflow_truth_small x y result .towards_zero hx hy
    hx_mant_ne hy_mant_ne hok hres
  change ∃ n, (if x.toRat * y.toRat ≥ 0 then Number.lower (x.toRat * y.toRat)
              else Number.upper (x.toRat * y.toRat)) = some n ∧ result.toRat = n.toRat
  by_cases h_truth_nn : x.toRat * y.toRat ≥ 0
  · rw [if_pos h_truth_nn]
    have h_truth_pos : 0 < x.toRat * y.toRat :=
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


theorem operator_mul_rounded_towards_zero_proof (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hok : Number.operator_mul x y .towards_zero = .ok result) :
    Number.RoundsToRepresentable result (x.toRat * y.toRat) .towards_zero := by
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
    change ∃ n, (if (0 : ℚ) ≥ 0 then Number.lower 0 else Number.upper 0) = some n
      ∧ result.toRat = n.toRat
    rw [if_pos (by norm_num : (0 : ℚ) ≥ 0)]
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
    change ∃ n, (if (0 : ℚ) ≥ 0 then Number.lower 0 else Number.upper 0) = some n
      ∧ result.toRat = n.toRat
    rw [if_pos (by norm_num : (0 : ℚ) ≥ 0)]
    refine ⟨Number.zero, ?_, ?_⟩
    · unfold Number.lower
      rw [if_pos rfl]
    · rw [h_result, hy_zero]
  -- Underflow: flush-to-zero is correct rounding toward zero.
  by_cases hres : result.mantissa_ = 0
  · exact rounded_towards_zero_of_result_zero x y result hx hy hxz hyz hok hres
  -- Generic path.
  have hx_mant_ne : x.mantissa_ ≠ 0 := hxz
  have hy_mant_ne : y.mantissa_ ≠ 0 := hyz
  have hresult : result.mantissa_ ≠ 0 := hres
  obtain ⟨h_dir, h_mag⟩ :=
    operator_mul_rounding_bound_towards_zero x y result hx hy hx_mant_ne hy_mant_ne hok hresult
  have h_result_norm : result.isNormalized :=
    operator_mul_result_isNormalized x y result .towards_zero hx hy hx_mant_ne hy_mant_ne
      hok hresult
  have h_truth_ne : x.toRat * y.toRat ≠ 0 :=
    toRat_mul_ne_zero_of_normalized x y hx hy hx_mant_ne hy_mant_ne
  obtain ⟨_, _, _, _, _, _, _, _, _, _, _, _, _, _, h_sign, _⟩ :=
    operator_mul_algorithmic_facts_towards_zero x y result hx hy hx_mant_ne hy_mant_ne
      hok hresult
  have h_sign_xy := toRat_mul_sign x y
  have h_abs_diff_eq : |result.toRat - x.toRat * y.toRat|
      = |(|result.toRat| - |x.toRat * y.toRat|)| := by
    apply abs_diff_eq_abs_sub_abs_of_sign_aligned result (x.toRat * y.toRat)
    · intro h_neg
      apply h_sign_xy.2
      intro h_eq
      have h_zn_false : (x.negative_ != y.negative_) = false := by simp [h_eq]
      rw [h_zn_false] at h_sign
      exact Bool.noConfusion (h_neg.symm.trans h_sign)
    · intro h_pos
      apply h_sign_xy.1
      by_contra h_ne
      have h_zn_true : (x.negative_ != y.negative_) = true := by
        simp only [bne_iff_ne, ne_eq]; exact h_ne
      rw [h_zn_true] at h_sign
      exact Bool.noConfusion (h_pos.symm.trans h_sign)
  have h_bound : |result.toRat - x.toRat * y.toRat|
      ≤ |x.toRat * y.toRat| * (11 / (2 ^ 63 - 18 : ℚ)) := by
    rw [h_abs_diff_eq,
        abs_of_nonpos (by linarith [h_dir] : |result.toRat| - |x.toRat * y.toRat| ≤ 0)]
    have h_eps : |x.toRat * y.toRat| * (10 / (2 ^ 63 + 2 : ℚ))
        ≤ |x.toRat * y.toRat| * (11 / (2 ^ 63 - 18 : ℚ)) :=
      mul_le_mul_of_nonneg_left (by norm_num) (abs_nonneg _)
    linarith [le_of_lt h_mag]
  have h_truth_top : result.exponent_ ≥ maxExponent →
      |x.toRat * y.toRat| < 10 ^ 19 * (10 : ℚ) ^ (maxExponent : ℤ) := fun h =>
    truth_top_of_result_cap result (x.toRat * y.toRat) h_result_norm hresult h_bound
      (le_trans
        (UInt64.le_iff_toNat_le.mp
          (operator_mul_no_overflow_mantissa x y result .towards_zero hx hy hx_mant_ne hy_mant_ne
            hok hresult h))
        (by rw [show maxRepUp.toNat = maxRepUpNat from rfl]; norm_num)) h
  change ∃ n, (if x.toRat * y.toRat ≥ 0 then Number.lower (x.toRat * y.toRat)
              else Number.upper (x.toRat * y.toRat)) = some n ∧ result.toRat = n.toRat
  by_cases h_truth_nn : x.toRat * y.toRat ≥ 0
  · rw [if_pos h_truth_nn]
    -- Direction: result ≤ |result| ≤ |truth| = truth.
    have h_round_down : result.toRat ≤ x.toRat * y.toRat := by
      calc result.toRat ≤ |result.toRat| := le_abs_self _
        _ ≤ |x.toRat * y.toRat| := h_dir
        _ = x.toRat * y.toRat := abs_of_nonneg h_truth_nn
    exact closest_lower_of_no_inbetween result (x.toRat * y.toRat) h_result_norm hresult
      h_truth_ne h_bound h_truth_top h_round_down
      (operator_mul_no_inbetween_below_towards_zero x y result hx hy hx_mant_ne hy_mant_ne
        hok hresult h_round_down)
  · rw [if_neg h_truth_nn]
    push_neg at h_truth_nn
    have h_truth_np : x.toRat * y.toRat ≤ 0 := le_of_lt h_truth_nn
    -- Direction: truth = −|truth| ≤ −|result| ≤ result.
    have h_round_up : x.toRat * y.toRat ≤ result.toRat := by
      have h1 : x.toRat * y.toRat = -|x.toRat * y.toRat| := by
        rw [abs_of_nonpos h_truth_np]; ring
      have h2 : -|result.toRat| ≤ result.toRat := neg_abs_le _
      linarith [neg_le_neg h_dir]
    exact closest_upper_of_no_inbetween result (x.toRat * y.toRat) h_result_norm hresult
      h_truth_ne h_bound h_truth_top h_round_up
      (operator_mul_no_inbetween_above_towards_zero x y result hx hy hx_mant_ne hy_mant_ne
        hok hresult h_round_up)

/-! ## Main theorem: `operator_mul_rounded_to_nearest` -/

end XRPL.Model.Protocol
