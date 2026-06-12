import XRPL.Properties.Protocol.Number.Div.ToNearest.BoundProof
import XRPL.Properties.Protocol.Number.Div.ToNearest.WitnessTrace

set_option linter.style.longLine false
set_option linter.style.emptyLine false

namespace XRPL.Model.Protocol

/-- `Rounds`-shaped restatement of `operator_div_rounding_bound`. -/
theorem operator_div_rounds (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (hok : Number.operator_div x y .to_nearest = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    Rounds result (x.toRat / y.toRat) .to_nearest (6 / ((2 : ℚ) ^ 63 + 18)) :=
  operator_div_rounding_bound x y result hx hy hx_mant_ne hy_mant_ne hok hresult

theorem operator_div_rounding_bound_attained :
    ∃ (x y result : Number),
      x.isNormalized ∧ y.isNormalized ∧
      x.mantissa_ ≠ 0 ∧ y.mantissa_ ≠ 0 ∧
      Number.operator_div x y .to_nearest = .ok result ∧
      result.mantissa_ ≠ 0 ∧
      |result.toRat - x.toRat / y.toRat| > |x.toRat / y.toRat| * (5 / (2 ^ 63 + 7 : ℚ)) := by
  refine ⟨⟨false, 2880280442173067684, 0⟩,
          ⟨false, 3122806312771549303, 0⟩,
          ⟨false, 9223372036854775820, -19⟩,
          ?_, ?_, ?_, ?_, ?_, ?_, ?_⟩
  · exact Or.inr ⟨by decide, by decide, by decide, by decide, by decide⟩
  · exact Or.inr ⟨by decide, by decide, by decide, by decide, by decide⟩
  · decide
  · decide
  · exact operator_div_witness
  · decide
  · have hx_rat : (⟨false, 2880280442173067684, 0⟩ : Number).toRat = 2880280442173067684 := by
      unfold Number.toRat; rfl
    have hy_rat : (⟨false, 3122806312771549303, 0⟩ : Number).toRat = 3122806312771549303 := by
      unfold Number.toRat; rfl
    have hr_rat : (⟨false, 9223372036854775820, -19⟩ : Number).toRat
        = 9223372036854775820 / (10 : ℚ) ^ (19 : ℕ) := by
      unfold Number.toRat; simp; ring
    rw [hx_rat, hy_rat, hr_rat]
    norm_num

/-- The bound `5/(2^63+18)` does NOT hold for division: the same witness also exceeds it.
Combined with `operator_div_rounding_bound_attained`, this shows `6/(2^63+18)` is the tightest
bound of the form `N/(2^63+18)` with integer N. -/
theorem operator_div_rounding_bound_attained_2 :
    ∃ (x y result : Number),
      x.isNormalized ∧ y.isNormalized ∧
      x.mantissa_ ≠ 0 ∧ y.mantissa_ ≠ 0 ∧
      Number.operator_div x y .to_nearest = .ok result ∧
      result.mantissa_ ≠ 0 ∧
      |result.toRat - x.toRat / y.toRat| > |x.toRat / y.toRat| * (5 / (2 ^ 63 + 18 : ℚ)) := by
  refine ⟨⟨false, 2880280442173067684, 0⟩,
          ⟨false, 3122806312771549303, 0⟩,
          ⟨false, 9223372036854775820, -19⟩,
          ?_, ?_, ?_, ?_, ?_, ?_, ?_⟩
  · exact Or.inr ⟨by decide, by decide, by decide, by decide, by decide⟩
  · exact Or.inr ⟨by decide, by decide, by decide, by decide, by decide⟩
  · decide
  · decide
  · exact operator_div_witness
  · decide
  · have hx_rat : (⟨false, 2880280442173067684, 0⟩ : Number).toRat = 2880280442173067684 := by
      unfold Number.toRat; rfl
    have hy_rat : (⟨false, 3122806312771549303, 0⟩ : Number).toRat = 3122806312771549303 := by
      unfold Number.toRat; rfl
    have hr_rat : (⟨false, 9223372036854775820, -19⟩ : Number).toRat
        = 9223372036854775820 / (10 : ℚ) ^ (19 : ℕ) := by
      unfold Number.toRat; simp; ring
    rw [hx_rat, hy_rat, hr_rat]
    norm_num

end XRPL.Model.Protocol
