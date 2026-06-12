import XRPL.Properties.Protocol.Number.Add.ToNearest.BoundProof
import XRPL.Properties.Protocol.Number.Add.ToNearest.WitnessTrace

set_option linter.style.longLine false
set_option linter.style.emptyLine false

namespace XRPL.Model.Protocol

/-- `Rounds`-shaped restatement of `operator_add_rounding_bound_same_sign`. -/
theorem operator_add_rounds_same_sign (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (h_same_sign : x.negative_ = y.negative_)
    (h_not_zero : ¬ x.operator_eq y.operator_neg)
    (hok : Number.operator_add x y .to_nearest = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    Rounds result (x.toRat + y.toRat) .to_nearest (5 / (2 ^ 63 + 7 : ℚ)) :=
  operator_add_rounding_bound_same_sign x y result hx hy hx_mant_ne hy_mant_ne
    h_same_sign h_not_zero hok hresult

/-- `Rounds`-shaped restatement of the unified tight bound
`operator_add_rounding_bound_tight`. Covers both same-sign and diff-sign
branches with the uniform `ε = 6/(2^63 - 3)`, with no input-magnitude hypothesis. -/
theorem operator_add_rounds (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (h_not_zero : ¬ x.operator_eq y.operator_neg)
    (hok : Number.operator_add x y .to_nearest = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    Rounds result (x.toRat + y.toRat) .to_nearest (6 / (2 ^ 63 - 3 : ℚ)) :=
  operator_add_rounding_bound_tight x y result hx hy hx_mant_ne hy_mant_ne
    h_not_zero hok hresult

/-- The bound `5/(2^63 + 7)` is attained exactly, so it is the tight supremum.
Witness: `x = 5e18`, `y = 4223372036854775815`. Sum = `9223372036854775815 = 2^63 + 7`.
After scaleDown: mantissa at `floor + 1` (odd), guard digit = 5 → tie-to-even rounds up.
Error = 5, relative = `5/(2^63 + 7)`. -/
theorem operator_add_rounding_bound_same_sign_attained :
    ∃ (x y result : Number),
      x.isNormalized ∧ y.isNormalized ∧
      x.mantissa_ ≠ 0 ∧ y.mantissa_ ≠ 0 ∧
      Number.operator_add x y .to_nearest = .ok result ∧
      result.mantissa_ ≠ 0 ∧
      |result.toRat - (x.toRat + y.toRat)|
        = |x.toRat + y.toRat| * (5 / (2 ^ 63 + 7 : ℚ)) := by
  refine ⟨⟨false, 5000000000000000000, 0⟩,
          ⟨false, 4223372036854775815, 0⟩,
          ⟨false, 9223372036854775820, 0⟩,
          ?_, ?_, ?_, ?_, ?_, ?_, ?_⟩
  · right
    refine ⟨?_, ?_, ?_, ?_, ?_⟩ <;> decide
  · right
    refine ⟨?_, ?_, ?_, ?_, ?_⟩ <;> decide
  · decide
  · decide
  · exact operator_add_witness
  · decide
  · have hx_rat : (⟨false, 5000000000000000000, 0⟩ : Number).toRat = 5000000000000000000 := by
      unfold Number.toRat; rfl
    have hy_rat : (⟨false, 4223372036854775815, 0⟩ : Number).toRat = 4223372036854775815 := by
      unfold Number.toRat; rfl
    have hr_rat : (⟨false, 9223372036854775820, 0⟩ : Number).toRat = 9223372036854775820 := by
      unfold Number.toRat; rfl
    rw [hx_rat, hy_rat, hr_rat]
    change |(9223372036854775820 : ℚ) - (5000000000000000000 + 4223372036854775815)|
       = |(5000000000000000000 : ℚ) + 4223372036854775815| * (5 / (2 ^ 63 + 7 : ℚ))
    rw [show ((2 : ℚ) ^ 63 + 7) = 9223372036854775815 by norm_num]
    rw [show (5000000000000000000 : ℚ) + 4223372036854775815 = 9223372036854775815 by norm_num]
    rw [show (9223372036854775820 : ℚ) - 9223372036854775815 = 5 by norm_num]
    rw [abs_of_pos (by norm_num : (0 : ℚ) < 5)]
    rw [abs_of_pos (by norm_num : (0 : ℚ) < 9223372036854775815)]
    field_simp

end XRPL.Model.Protocol
