import XRPL.Properties.Approx
import XRPL.Properties.Protocol.Number.Div.Common.Downward.BoundProof
import XRPL.Properties.Protocol.Number.Div.Common.Downward.WitnessTrace
import XRPL.Properties.Protocol.Number.Div.Common.Upward.BoundProof
import XRPL.Properties.Protocol.Number.Div.Common.Upward.WitnessTrace
import XRPL.Properties.Protocol.Number.Div.Common.TowardsZero.BoundProof
import XRPL.Properties.Protocol.Number.Div.Common.TowardsZero.WitnessTrace
import XRPL.Properties.Protocol.Number.Div.Common.ToNearest.BoundProof
import XRPL.Properties.Protocol.Number.Div.Common.ToNearest.WitnessTrace
import XRPL.Properties.Protocol.Number.Div.Common.Decompose


namespace XRPL.Model.Protocol

theorem operator_div_rounds_downward (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hok : Number.operator_div x y .downward = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    RoundsWithin result (x.toRat / y.toRat) .downward (11 / ((2 : ℚ) ^ 63 - 18)) := by
  obtain ⟨hx_mant_ne, hy_mant_ne⟩ := operator_div_operands_ne_zero hx hy hok hresult
  have h_dir := operator_div_rounding_bound_downward_dir x y result hx hy
    hx_mant_ne hy_mant_ne hok hresult
  have h_mag := operator_div_rounding_bound_downward x y result hx hy
    hx_mant_ne hy_mant_ne hok hresult
  refine ⟨h_dir, ?_⟩
  change x.toRat / y.toRat - result.toRat ≤ |x.toRat / y.toRat| * (11 / ((2 : ℚ) ^ 63 - 18))
  have h1 : x.toRat / y.toRat - result.toRat ≤ |result.toRat - x.toRat / y.toRat| := by
    rw [abs_sub_comm]
    exact le_abs_self _
  linarith

theorem operator_div_rounding_bound_downward_attained :
    ∃ (x y result : Number),
      x.isNormalized ∧ y.isNormalized ∧
      Number.operator_div x y .downward = .ok result ∧
      result.mantissa_ ≠ 0 ∧
      RoundsWithinWitness result (x.toRat / y.toRat) (9 / (2 ^ 63 - 18 : ℚ)) :=
  operator_div_downward_attained

theorem operator_div_rounds_upward (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hok : Number.operator_div x y .upward = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    RoundsWithin result (x.toRat / y.toRat) .upward (11 / ((2 : ℚ) ^ 63 - 18)) := by
  obtain ⟨hx_mant_ne, hy_mant_ne⟩ := operator_div_operands_ne_zero hx hy hok hresult
  have h_dir := operator_div_rounding_bound_upward_dir x y result hx hy
    hx_mant_ne hy_mant_ne hok hresult
  have h_mag := operator_div_rounding_bound_upward x y result hx hy
    hx_mant_ne hy_mant_ne hok hresult
  refine ⟨h_dir, ?_⟩
  change result.toRat - x.toRat / y.toRat ≤ |x.toRat / y.toRat| * (11 / ((2 : ℚ) ^ 63 - 18))
  have h1 : result.toRat - x.toRat / y.toRat ≤ |result.toRat - x.toRat / y.toRat| :=
    le_abs_self _
  linarith

theorem operator_div_rounding_bound_upward_attained :
    ∃ (x y result : Number),
      x.isNormalized ∧ y.isNormalized ∧
      Number.operator_div x y .upward = .ok result ∧
      result.mantissa_ ≠ 0 ∧
      RoundsWithinWitness result (x.toRat / y.toRat) (9 / (2 ^ 63 - 18 : ℚ)) :=
  operator_div_upward_attained

theorem operator_div_rounds_towards_zero (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hok : Number.operator_div x y .towards_zero = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    RoundsWithin result (x.toRat / y.toRat) .towards_zero (11 / ((2 : ℚ) ^ 63 - 18)) := by
  obtain ⟨hx_mant_ne, hy_mant_ne⟩ := operator_div_operands_ne_zero hx hy hok hresult
  have h_dir := operator_div_rounding_bound_towards_zero_dir x y result hx hy
    hx_mant_ne hy_mant_ne hok hresult
  have h_mag := operator_div_rounding_bound_towards_zero x y result hx hy
    hx_mant_ne hy_mant_ne hok hresult
  refine ⟨h_dir, ?_⟩
  change |x.toRat / y.toRat| - |result.toRat| ≤ |x.toRat / y.toRat| * (11 / ((2 : ℚ) ^ 63 - 18))
  have h1 : |x.toRat / y.toRat| - |result.toRat| ≤ |result.toRat - x.toRat / y.toRat| := by
    rw [abs_sub_comm]
    exact abs_sub_abs_le_abs_sub _ _
  linarith

theorem operator_div_rounding_bound_towards_zero_attained :
    ∃ (x y result : Number),
      x.isNormalized ∧ y.isNormalized ∧
      Number.operator_div x y .towards_zero = .ok result ∧
      result.mantissa_ ≠ 0 ∧
      RoundsWithinWitness result (x.toRat / y.toRat) (9 / (2 ^ 63 - 18 : ℚ)) :=
  operator_div_towards_zero_attained

theorem operator_div_rounds_to_nearest (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hok : Number.operator_div x y .to_nearest = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    RoundsWithin result (x.toRat / y.toRat) .to_nearest (6 / ((2 : ℚ) ^ 63 - 3)) := by
  obtain ⟨hx_mant_ne, hy_mant_ne⟩ := operator_div_operands_ne_zero hx hy hok hresult
  exact operator_div_rounding_bound_to_nearest x y result hx hy hx_mant_ne hy_mant_ne hok hresult

theorem operator_div_rounding_bound_to_nearest_attained :
    ∃ (x y result : Number),
      x.isNormalized ∧ y.isNormalized ∧
      Number.operator_div x y .to_nearest = .ok result ∧
      result.mantissa_ ≠ 0 ∧
      RoundsWithinWitness result (x.toRat / y.toRat) (4 / (2 ^ 63 - 3 : ℚ)) :=
  operator_div_to_nearest_attained

end XRPL.Model.Protocol
