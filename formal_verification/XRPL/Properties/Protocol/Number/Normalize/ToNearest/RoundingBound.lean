import XRPL.Properties.Protocol.Number.Common.Notation
import XRPL.Properties.Protocol.Number.Normalize.ToNearest.BoundProof
import XRPL.Properties.Protocol.Number.Normalize.ToNearest.WitnessTrace

set_option linter.style.longLine false
set_option linter.style.emptyLine false

namespace XRPL.Model.Protocol

/-! ## to_nearest -/

theorem normalize_rounds_to_nearest (n result : Number)
    (hn_mant_ne : n.mantissa_ ≠ 0)
    (hok : n.normalize largeRange.min largeRange.max .to_nearest = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    Rounds result n.toRat .to_nearest (5 / (2 ^ 63 + 7 : ℚ)) := by
  exact normalize_rounding_bound_to_nearest n result hn_mant_ne hok hresult

/-- The `5/(2^63+7)` to-nearest normalize bound is attained: normalizing the
    value `2^63+7` rounds up by exactly 5 ULP-at-scale (tie on an odd mantissa),
    giving relative error exactly `5/(2^63+7)`. So 5 is the optimal numerator. -/
theorem normalize_rounding_bound_to_nearest_attained :
    ∃ (n result : Number),
      n.mantissa_ ≠ 0 ∧
      Number.normalize n largeRange.min largeRange.max .to_nearest = .ok result ∧
      result.mantissa_ ≠ 0 ∧
      |result.toRat - n.toRat| = |n.toRat| * (5 / (2 ^ 63 + 7 : ℚ)) := by
  refine ⟨⟨false, twoPow63Add7, 0⟩,
          ⟨false, twoPow63Add12, 0⟩,
          ?_, ?_, ?_, ?_⟩
  · decide
  · exact normalize_to_nearest_witness
  · decide
  · have hn_rat : (⟨false, twoPow63Add7, 0⟩ : Number).toRat = twoPow63Add7 := by
      unfold Number.toRat; rfl
    have hr_rat : (⟨false, twoPow63Add12, 0⟩ : Number).toRat = twoPow63Add12 := by
      unfold Number.toRat; rfl
    rw [hn_rat, hr_rat]
    change |(twoPow63Add12 : ℚ) - twoPow63Add7|
       = |(twoPow63Add7 : ℚ)| * (5 / (2 ^ 63 + 7 : ℚ))
    rw [show ((2 : ℚ) ^ 63 + 7) = twoPow63Add7 by norm_num]
    rw [show (twoPow63Add12 : ℚ) - twoPow63Add7 = 5 by norm_num]
    rw [abs_of_pos (by norm_num : (0 : ℚ) < 5)]
    rw [abs_of_pos (by norm_num : (0 : ℚ) < twoPow63Add7)]
    field_simp

end XRPL.Model.Protocol
