import XRPL.Properties.Protocol.Number.Common.Notation
import XRPL.Properties.Protocol.Number.Normalize.Upward.BoundProof
import XRPL.Properties.Protocol.Number.Normalize.Upward.WitnessTrace

set_option linter.style.longLine false
set_option linter.style.emptyLine false

namespace XRPL.Model.Protocol

/-! ## upward -/

theorem normalize_rounds_upward (n result : Number)
    (hn_mant_ne : n.mantissa_ ≠ 0)
    (hok : n.normalize largeRange.min largeRange.max .upward = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    Rounds result n.toRat .upward (10 / (2 ^ 63 + 2 : ℚ)) := by
  obtain ⟨h_dir, h_mag⟩ :=
    normalize_rounding_bound_upward n result hn_mant_ne hok hresult
  exact ⟨h_dir, le_of_lt h_mag⟩

/-- The `10/(2^63+2)` upward normalize bound is a **tight supremum but not
    attained**: normalizing the negative value `-(2^63+1)` truncates its
    magnitude toward zero by exactly 9 ULP-at-scale, giving relative error
    `9/(2^63+1)`, which strictly exceeds `9/(2^63+2)`. So no numerator below 10
    suffices, yet 10 is never reached (since the residue `f < 1`). -/
theorem normalize_rounding_bound_upward_attained :
    ∃ (n result : Number),
      n.mantissa_ ≠ 0 ∧
      Number.normalize n largeRange.min largeRange.max .upward = .ok result ∧
      result.mantissa_ ≠ 0 ∧
      |result.toRat - n.toRat| > |n.toRat| * (9 / (2 ^ 63 + 2 : ℚ)) := by
  refine ⟨⟨true, twoPow63Add1, 0⟩,
          ⟨true, twoPow63Sub8, 0⟩,
          ?_, ?_, ?_, ?_⟩
  · decide
  · exact normalize_upward_witness
  · decide
  · have hn_rat : (⟨true, twoPow63Add1, 0⟩ : Number).toRat = -twoPow63Add1 := by
      unfold Number.toRat; rfl
    have hr_rat : (⟨true, twoPow63Sub8, 0⟩ : Number).toRat = -twoPow63Sub8 := by
      unfold Number.toRat; rfl
    rw [hn_rat, hr_rat]
    change |(-twoPow63Sub8 : ℚ) - (-twoPow63Add1)|
       > |(-twoPow63Add1 : ℚ)| * (9 / (2 ^ 63 + 2 : ℚ))
    rw [show (-twoPow63Sub8 : ℚ) - (-twoPow63Add1) = 9 by norm_num]
    rw [abs_of_pos (by norm_num : (0 : ℚ) < 9)]
    rw [show (-twoPow63Add1 : ℚ) = -(twoPow63Add1) by ring, abs_neg,
        abs_of_pos (by norm_num : (0 : ℚ) < twoPow63Add1)]
    rw [show ((2 : ℚ) ^ 63 + 2) = maxRepCuspTarget by norm_num]
    rw [show (twoPow63Add1 : ℚ) * (9 / maxRepCuspTarget)
          = 9 * twoPow63Add1 / maxRepCuspTarget by ring]
    rw [gt_iff_lt, div_lt_iff₀ (by norm_num : (0 : ℚ) < maxRepCuspTarget)]
    norm_num

end XRPL.Model.Protocol
