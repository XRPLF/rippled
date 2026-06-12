import XRPL.Properties.Protocol.Number.Normalize.ToNearest.AlgorithmicFacts
import XRPL.Properties.Protocol.Number.Common.Helpers
import XRPL.Common.Approx

set_option linter.style.longLine false
set_option linter.style.emptyLine false

namespace XRPL.Model.Protocol

/-! ## to_nearest -/

/-- Relative-error bound for `Number.normalize` under `.to_nearest` rounding.

`normalize` accepts an arbitrary `(mantissa, exponent)` pair (the input need not
be normalized). Its final stage is `doRoundUp`, so the rounding error matches the
same-sign `doRoundUp` to-nearest supremum `5/(2^63 + 7)`. -/
theorem normalize_rounding_bound_to_nearest (n result : Number)
    (hn_mant_ne : n.mantissa_ ≠ 0)
    (hok : n.normalize largeRange.min largeRange.max .to_nearest = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    |result.toRat - n.toRat| ≤ |n.toRat| * (5 / (2 ^ 63 + 7 : ℚ)) := by
  obtain ⟨zm, ze, f, g, res_pos, hzm_ge, hzm_le_max, h_floor_constraint,
          habs_n_eq, h_rup_pos, h_result_abs, hres_pos_mant_ne, hf_rep, h_sign⟩ :=
    normalize_algorithmic_facts n result hn_mant_ne hok hresult
  -- align signs: result.negative_ = n.negative_, so |result - n| = ||result| - |n||
  have h_abs_diff_eq : |result.toRat - n.toRat|
      = |(|result.toRat| - |n.toRat|)| := by
    apply abs_diff_eq_abs_sub_abs_of_sign_aligned result n.toRat
    · intro h_neg
      rw [h_sign] at h_neg
      exact Number.toRat_nonpos_of_negative n h_neg
    · intro h_pos
      rw [h_sign] at h_pos
      exact Number.toRat_nonneg_of_nonnegative n h_pos
  rw [h_abs_diff_eq, h_result_abs, habs_n_eq]
  have h_bound_eq : ((2 ^ 63 + 7 : ℕ) : ℚ) = (2 ^ 63 + 7 : ℚ) := by norm_num
  rw [← h_bound_eq]
  exact doRoundUp_rounds_to_nearest_supTight g zm ze f hf_rep hzm_ge hzm_le_max
    h_floor_constraint "Number::normalize 2" res_pos h_rup_pos hres_pos_mant_ne

end XRPL.Model.Protocol
