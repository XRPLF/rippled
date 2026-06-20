import XRPL.Properties.Protocol.Number.Common.Notation
import XRPL.Properties.Protocol.Number.Normalize.Common.TowardsZero.AlgorithmicFacts
import XRPL.Properties.Protocol.Number.Normalize.Common.TowardsZero.BoundProof
import XRPL.Properties.Protocol.Number.Normalize.Common.Underflow
import XRPL.Properties.Protocol.Number.Normalize.Common.ResultFacts
import XRPL.Properties.Protocol.Number.Common.Closest.Identify
import XRPL.Properties.Protocol.Number.Common.Closest.NoInbetween


namespace XRPL.Model.Protocol

/-! # `Number.normalize` is correctly rounded under `.towards_zero`

`.towards_zero` always truncates, so the result is `Number.lower` of the input
value for nonnegative inputs and `Number.upper` for negative ones. The
no-inbetween dischargers are the generic frames instantiated with the
normalize facts bundle. No nonzero hypotheses: a zero input mantissa
short-circuits to the zero grid point, and underflow flushes to zero — which
is itself correct rounding toward zero. -/

/-- No normalized Number sits strictly between `result.toRat` and `n.toRat`
when `result.toRat ≤ n.toRat`. -/
theorem normalize_no_inbetween_below_towards_zero (n result : Number)
    (hn_mant_ne : n.mantissa_ ≠ 0)
    (hok : n.normalize largeRange.min largeRange.max .towards_zero = .ok result)
    (hresult : result.mantissa_ ≠ 0)
    (h_le : result.toRat ≤ n.toRat) :
    ∀ m : Number, m.isNormalized →
      result.toRat < m.toRat → ¬ (m.toRat ≤ n.toRat) := by
  obtain ⟨zm, ze', f, g, res_pos, hzm_ge, hzm_le, hf_nn, hf_lt, _hcusp, h_truth_abs,
          h_rup_pos, h_result_abs, hres_pos_mant_ne, _hf_rep, h_sign, hzm_succ⟩ :=
    normalize_algorithmic_facts_towards_zero n result hn_mant_ne hok hresult
  exact no_inbetween_below_towards_zero_frame result n.toRat zm ze' f g res_pos
    "Number::normalize 2" hzm_ge hzm_le hf_nn hf_lt h_truth_abs h_rup_pos
    h_result_abs hres_pos_mant_ne hzm_succ
    (fun h_pos => by
      have hn_nneg : n.negative_ = false := by
        rcases hn : n.negative_ with _ | _
        · rfl
        · exact absurd (Number.toRat_nonpos_of_negative n hn) (not_le.mpr h_pos)
      exact Number.toRat_nonneg_of_nonnegative result (h_sign.trans hn_nneg))
    h_le

/-- No normalized Number sits strictly between `n.toRat` and `result.toRat`
when `n.toRat ≤ result.toRat`. -/
theorem normalize_no_inbetween_above_towards_zero (n result : Number)
    (hn_mant_ne : n.mantissa_ ≠ 0)
    (hok : n.normalize largeRange.min largeRange.max .towards_zero = .ok result)
    (hresult : result.mantissa_ ≠ 0)
    (h_ge : n.toRat ≤ result.toRat) :
    ∀ m : Number, m.isNormalized →
      m.toRat < result.toRat → ¬ (n.toRat ≤ m.toRat) := by
  obtain ⟨zm, ze', f, g, res_pos, hzm_ge, hzm_le, hf_nn, hf_lt, _hcusp, h_truth_abs,
          h_rup_pos, h_result_abs, hres_pos_mant_ne, _hf_rep, h_sign, hzm_succ⟩ :=
    normalize_algorithmic_facts_towards_zero n result hn_mant_ne hok hresult
  exact no_inbetween_above_towards_zero_frame result n.toRat zm ze' f g res_pos
    "Number::normalize 2" hzm_ge hzm_le hf_nn hf_lt h_truth_abs h_rup_pos
    h_result_abs hres_pos_mant_ne hzm_succ
    (fun h_neg => by
      have hn_neg : n.negative_ = true := by
        rcases hn : n.negative_ with _ | _
        · exact absurd (Number.toRat_nonneg_of_nonnegative n hn) (not_le.mpr h_neg)
        · rfl
      exact Number.toRat_nonpos_of_negative result (h_sign.trans hn_neg))
    h_ge

/-- Underflow dispatch: a zero result mantissa always lands on the correct
grid point for `.towards_zero` (flush-to-zero *is* rounding toward zero). -/
lemma normalize_rounded_towards_zero_of_result_zero (n result : Number)
    (hn_mant_ne : n.mantissa_ ≠ 0)
    (hok : n.normalize largeRange.min largeRange.max .towards_zero = .ok result)
    (hres : result.mantissa_ = 0) :
    Number.RoundsToRepresentable result n.toRat .towards_zero := by
  have h_res0 : result.toRat = 0 := Number.toRat_eq_zero_of_mantissa_zero result hres
  have h_truth_ne : n.toRat ≠ 0 := Number.toRat_ne_zero_of_mantissa_ne_zero n hn_mant_ne
  have h_small := normalize_underflow_truth_small n result .towards_zero hn_mant_ne hok hres
  change ∃ n' : Number, (if n.toRat ≥ 0 then Number.lower n.toRat else Number.upper n.toRat)
    = some n' ∧ result.toRat = n'.toRat
  by_cases h_truth_nn : n.toRat ≥ 0
  · rw [if_pos h_truth_nn]
    have h_truth_pos : 0 < n.toRat := lt_of_le_of_ne h_truth_nn (Ne.symm h_truth_ne)
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


/-- `Number.normalize` under `.towards_zero` is **correctly rounded**: the
result is `Number.lower` of the input value for nonnegative inputs and
`Number.upper` for negative ones. Only `hok` is assumed. -/
theorem normalize_rounded_towards_zero_proof (n result : Number)
    (hok : n.normalize largeRange.min largeRange.max .towards_zero = .ok result) :
    Number.RoundsToRepresentable result n.toRat .towards_zero := by
  -- Zero input: the model short-circuits to the canonical zero.
  by_cases hnz : n.mantissa_ = 0
  · have h_truth0 : n.toRat = 0 := Number.toRat_eq_zero_of_mantissa_zero n hnz
    unfold Number.normalize doNormalize at hok
    rw [show (n.mantissa_ == 0) = true from beq_iff_eq.mpr hnz, if_pos rfl] at hok
    have h_result : result = Number.zero := (Except.ok.inj hok).symm
    rw [h_truth0]
    change ∃ n' : Number, (if (0 : ℚ) ≥ 0 then Number.lower 0 else Number.upper 0)
      = some n' ∧ result.toRat = n'.toRat
    rw [if_pos (by norm_num : (0 : ℚ) ≥ 0)]
    refine ⟨Number.zero, ?_, ?_⟩
    · unfold Number.lower
      rw [if_pos rfl]
    · rw [h_result]
  -- Underflow: flush-to-zero is correct rounding toward zero.
  by_cases hres : result.mantissa_ = 0
  · exact normalize_rounded_towards_zero_of_result_zero n result hnz hok hres
  -- Generic path.
  have hresult : result.mantissa_ ≠ 0 := hres
  obtain ⟨h_dir, h_mag⟩ :=
    normalize_rounding_bound_towards_zero n result hnz hok hresult
  have h_result_norm : result.isNormalized :=
    normalize_result_isNormalized n result .towards_zero hnz hok hresult
  have h_truth_ne : n.toRat ≠ 0 := Number.toRat_ne_zero_of_mantissa_ne_zero n hnz
  obtain ⟨_, _, _, _, _, _, _, _, _, _, _, _, _, _, _, h_sign, _⟩ :=
    normalize_algorithmic_facts_towards_zero n result hnz hok hresult
  have h_abs_diff_eq : |result.toRat - n.toRat| = |(|result.toRat| - |n.toRat|)| := by
    apply abs_diff_eq_abs_sub_abs_of_sign_aligned result n.toRat
    · intro h_neg
      exact Number.toRat_nonpos_of_negative n (h_sign.symm.trans h_neg)
    · intro h_nneg
      exact Number.toRat_nonneg_of_nonnegative n (h_sign.symm.trans h_nneg)
  have h_bound : |result.toRat - n.toRat| ≤ |n.toRat| * (11 / (2 ^ 63 - 18 : ℚ)) := by
    rw [h_abs_diff_eq,
        abs_of_nonpos (by linarith [h_dir] : |result.toRat| - |n.toRat| ≤ 0)]
    have h_eps : |n.toRat| * (10 / (2 ^ 63 + 2 : ℚ))
        ≤ |n.toRat| * (11 / (2 ^ 63 - 18 : ℚ)) :=
      mul_le_mul_of_nonneg_left (by norm_num) (abs_nonneg _)
    linarith [le_of_lt h_mag]
  have h_truth_top : result.exponent_ ≥ maxExponent →
      |n.toRat| < 10 ^ 19 * (10 : ℚ) ^ (maxExponent : ℤ) := fun h =>
    truth_top_of_result_cap result n.toRat h_result_norm hresult h_bound
      (le_trans
        (UInt64.le_iff_toNat_le.mp
          (normalize_no_overflow_mantissa n result .towards_zero hnz hok hresult h))
        (by rw [show maxRepUp.toNat = maxRepUpNat from rfl]; norm_num)) h
  change ∃ n' : Number, (if n.toRat ≥ 0 then Number.lower n.toRat else Number.upper n.toRat)
    = some n' ∧ result.toRat = n'.toRat
  by_cases h_truth_nn : n.toRat ≥ 0
  · rw [if_pos h_truth_nn]
    -- Direction: result ≤ |result| ≤ |truth| = truth.
    have h_round_down : result.toRat ≤ n.toRat := by
      calc result.toRat ≤ |result.toRat| := le_abs_self _
        _ ≤ |n.toRat| := h_dir
        _ = n.toRat := abs_of_nonneg h_truth_nn
    exact closest_lower_of_no_inbetween result n.toRat h_result_norm hresult
      h_truth_ne h_bound h_truth_top h_round_down
      (normalize_no_inbetween_below_towards_zero n result hnz hok hresult h_round_down)
  · rw [if_neg h_truth_nn]
    push_neg at h_truth_nn
    have h_truth_np : n.toRat ≤ 0 := le_of_lt h_truth_nn
    -- Direction: truth = −|truth| ≤ −|result| ≤ result.
    have h_round_up : n.toRat ≤ result.toRat := by
      have h1 : n.toRat = -|n.toRat| := by
        rw [abs_of_nonpos h_truth_np]; ring
      have h2 : -|result.toRat| ≤ result.toRat := neg_abs_le _
      linarith [neg_le_neg h_dir]
    exact closest_upper_of_no_inbetween result n.toRat h_result_norm hresult
      h_truth_ne h_bound h_truth_top h_round_up
      (normalize_no_inbetween_above_towards_zero n result hnz hok hresult h_round_up)

end XRPL.Model.Protocol
