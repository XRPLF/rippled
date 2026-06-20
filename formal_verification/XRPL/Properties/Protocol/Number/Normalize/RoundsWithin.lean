import XRPL.Properties.Protocol.Number.Common.Notation
import XRPL.Properties.Approx
import XRPL.Properties.Protocol.Number.Normalize.Common.ResultFacts
import XRPL.Properties.Protocol.Number.Normalize.Common.Downward.BoundProof
import XRPL.Properties.Protocol.Number.Normalize.Common.Downward.WitnessTrace
import XRPL.Properties.Protocol.Number.Normalize.Common.Upward.BoundProof
import XRPL.Properties.Protocol.Number.Normalize.Common.Upward.WitnessTrace
import XRPL.Properties.Protocol.Number.Normalize.Common.TowardsZero.BoundProof
import XRPL.Properties.Protocol.Number.Normalize.Common.TowardsZero.WitnessTrace
import XRPL.Properties.Protocol.Number.Normalize.Common.ToNearest.BoundProof
import XRPL.Properties.Protocol.Number.Normalize.Common.ToNearest.WitnessTrace


namespace XRPL.Model.Protocol

theorem normalize_rounds_downward (n result : Number)
    (hok : n.normalize largeRange.min largeRange.max .downward = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    RoundsWithin result n.toRat .downward (10 / (2 ^ 63 + 2 : ℚ)) := by
  have hn_mant_ne := normalize_operand_ne_zero n result .downward hok hresult
  obtain ⟨h_dir, h_mag⟩ :=
    normalize_rounding_bound_downward n result hn_mant_ne hok hresult
  exact ⟨h_dir, le_of_lt h_mag⟩

theorem normalize_rounding_bound_downward_attained :
    ∃ (n result : Number),
      Number.normalize n largeRange.min largeRange.max .downward = .ok result ∧
      result.mantissa_ ≠ 0 ∧
      RoundsWithinWitness result n.toRat (8 / (2 ^ 63 + 2 : ℚ)) :=
  normalize_downward_attained

theorem normalize_rounds_upward (n result : Number)
    (hok : n.normalize largeRange.min largeRange.max .upward = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    RoundsWithin result n.toRat .upward (10 / (2 ^ 63 + 2 : ℚ)) := by
  have hn_mant_ne := normalize_operand_ne_zero n result .upward hok hresult
  obtain ⟨h_dir, h_mag⟩ :=
    normalize_rounding_bound_upward n result hn_mant_ne hok hresult
  exact ⟨h_dir, le_of_lt h_mag⟩

theorem normalize_rounding_bound_upward_attained :
    ∃ (n result : Number),
      Number.normalize n largeRange.min largeRange.max .upward = .ok result ∧
      result.mantissa_ ≠ 0 ∧
      RoundsWithinWitness result n.toRat (8 / (2 ^ 63 + 2 : ℚ)) :=
  normalize_upward_attained

theorem normalize_rounds_towards_zero (n result : Number)
    (hok : n.normalize largeRange.min largeRange.max .towards_zero = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    RoundsWithin result n.toRat .towards_zero (10 / (2 ^ 63 + 2 : ℚ)) := by
  have hn_mant_ne := normalize_operand_ne_zero n result .towards_zero hok hresult
  obtain ⟨h_dir, h_mag⟩ :=
    normalize_rounding_bound_towards_zero n result hn_mant_ne hok hresult
  exact ⟨h_dir, le_of_lt h_mag⟩

theorem normalize_rounding_bound_towards_zero_attained :
    ∃ (n result : Number),
      Number.normalize n largeRange.min largeRange.max .towards_zero = .ok result ∧
      result.mantissa_ ≠ 0 ∧
      RoundsWithinWitness result n.toRat (8 / (2 ^ 63 + 2 : ℚ)) :=
  normalize_towards_zero_attained

theorem normalize_rounds_to_nearest (n result : Number)
    (hok : n.normalize largeRange.min largeRange.max .to_nearest = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    RoundsWithin result n.toRat .to_nearest (5 / (2 ^ 63 + 7 : ℚ)) := by
  have hn_mant_ne := normalize_operand_ne_zero n result .to_nearest hok hresult
  exact normalize_rounding_bound_to_nearest n result hn_mant_ne hok hresult

theorem normalize_rounding_bound_to_nearest_attained :
    ∃ (n result : Number),
      Number.normalize n largeRange.min largeRange.max .to_nearest = .ok result ∧
      result.mantissa_ ≠ 0 ∧
      |result.toRat - n.toRat| = |n.toRat| * (5 / (2 ^ 63 + 7 : ℚ)) :=
  normalize_to_nearest_attained

end XRPL.Model.Protocol
