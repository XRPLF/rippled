import XRPL.Properties.Protocol.Number.Common.Notation
import XRPL.Properties.Protocol.Number.Normalize.Common.Downward.AlgorithmicFacts
import XRPL.Properties.Protocol.Number.Normalize.Common.Downward.BoundProof
import XRPL.Properties.Protocol.Number.Normalize.Common.ResultFacts
import XRPL.Properties.Protocol.Number.Common.Closest.Identify
import XRPL.Properties.Protocol.Number.Common.Closest.NoInbetween


namespace XRPL.Model.Protocol

/-! # `Number.normalize` is correctly rounded under `.downward`

The result is `Number.lower` of the input value — the largest representable
below it. `hresult` is genuinely required: an underflow flush returns zero
regardless of direction, and for a tiny negative input `0 > n.toRat` violates
`.downward` itself — the discrete claim is false at the flush. -/

/-- No normalized Number sits strictly between `result.toRat` and `n.toRat`. -/
theorem normalize_no_inbetween_below_downward (n result : Number)
    (hn_mant_ne : n.mantissa_ ≠ 0)
    (hok : n.normalize largeRange.min largeRange.max .downward = .ok result)
    (hresult : result.mantissa_ ≠ 0)
    (h_le : result.toRat ≤ n.toRat) :
    ∀ m : Number, m.isNormalized →
      result.toRat < m.toRat → ¬ (m.toRat ≤ n.toRat) := by
  obtain ⟨zm, ze', f, g, res_pos, hzm_ge, hzm_le, hf_nn, hf_lt, _hfloor_constr, _hcusp,
          h_truth_abs, h_rup_pos, h_result_abs, hres_pos_mant_ne, hf_rep, h_sign,
          h_g_sbit, hzm_succ⟩ :=
    normalize_algorithmic_facts_downward n result hn_mant_ne hok hresult
  exact no_inbetween_below_downward_frame result n.toRat zm ze' f g res_pos
    "Number::normalize 2" hzm_ge hzm_le hf_nn hf_lt h_truth_abs h_rup_pos
    h_result_abs hres_pos_mant_ne
    (fun h => represents_pos_of_shouldRoundUp_downward g f hf_rep h)
    (fun hd hxb => represents_eq_zero_of_digits_zero_xbit_false hd hxb hf_rep)
    hzm_succ
    (fun h_pos => by
      have hn_nneg : n.negative_ = false := by
        rcases hn : n.negative_ with _ | _
        · rfl
        · exact absurd (Number.toRat_nonpos_of_negative n hn) (not_le.mpr h_pos)
      exact Number.toRat_nonneg_of_nonnegative result (h_sign.trans hn_nneg))
    (fun h_neg => by
      have hn_neg : n.negative_ = true := by
        rcases hn : n.negative_ with _ | _
        · exact absurd (Number.toRat_nonneg_of_nonnegative n hn) (not_le.mpr h_neg)
        · rfl
      exact h_g_sbit.trans hn_neg)
    (fun h_pos => by
      have hn_nneg : n.negative_ = false := by
        rcases hn : n.negative_ with _ | _
        · rfl
        · exact absurd (Number.toRat_nonpos_of_negative n hn) (not_le.mpr h_pos)
      exact h_g_sbit.trans hn_nneg)
    h_le


/-- `Number.normalize` under `.downward` is **correctly rounded**: the result
is `Number.lower` of the input value. A zero input is excluded automatically
by `hresult` (it short-circuits to the zero result). `hresult` itself is
essential: the underflow flush returns zero regardless of direction, which for
a tiny negative input violates `.downward`. -/
theorem normalize_rounded_downward_proof (n result : Number)
    (hok : n.normalize largeRange.min largeRange.max .downward = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    Number.RoundsToRepresentable result n.toRat .downward := by
  -- Zero input contradicts `hresult` (the model returns the canonical zero).
  by_cases hnz : n.mantissa_ = 0
  · exfalso
    unfold Number.normalize doNormalize at hok
    rw [show (n.mantissa_ == 0) = true from beq_iff_eq.mpr hnz, if_pos rfl] at hok
    have h_result : result = Number.zero := (Except.ok.inj hok).symm
    apply hresult
    rw [h_result]
    rfl
  -- Generic path.
  obtain ⟨h_dir, h_mag⟩ :=
    normalize_rounding_bound_downward n result hnz hok hresult
  have h_bound : |result.toRat - n.toRat| ≤ |n.toRat| * (11 / (2 ^ 63 - 18 : ℚ)) := by
    rw [abs_of_nonpos (by linarith : result.toRat - n.toRat ≤ 0)]
    have h_eps : |n.toRat| * (10 / (2 ^ 63 + 2 : ℚ))
        ≤ |n.toRat| * (11 / (2 ^ 63 - 18 : ℚ)) :=
      mul_le_mul_of_nonneg_left (by norm_num) (abs_nonneg _)
    linarith [le_of_lt h_mag]
  have h_result_norm : result.isNormalized :=
    normalize_result_isNormalized n result .downward hnz hok hresult
  exact closest_lower_of_no_inbetween result n.toRat
    h_result_norm
    hresult
    (Number.toRat_ne_zero_of_mantissa_ne_zero n hnz)
    h_bound
    (fun h => truth_top_of_result_cap result n.toRat h_result_norm hresult h_bound
      (le_trans
        (UInt64.le_iff_toNat_le.mp
          (normalize_no_overflow_mantissa n result .downward hnz hok hresult h))
        (by rw [show maxRepUp.toNat = maxRepUpNat from rfl]; norm_num)) h)
    h_dir
    (normalize_no_inbetween_below_downward n result hnz hok hresult h_dir)

end XRPL.Model.Protocol
