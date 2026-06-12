import XRPL.Properties.Protocol.Number.Div.Upward.BoundProof
import XRPL.Properties.Protocol.Number.Div.Upward.WitnessTrace

set_option linter.style.longLine false
set_option linter.style.emptyLine false

namespace XRPL.Model.Protocol

/-- `Rounds`-shaped restatement of `operator_div_rounding_bound_upward`. -/
theorem operator_div_rounds_upward (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (hok : Number.operator_div x y .upward = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    |result.toRat - x.toRat / y.toRat| ≤ |x.toRat / y.toRat| * (10 / ((2 : ℚ) ^ 63 + 2)) :=
  le_of_lt (operator_div_rounding_bound_upward x y result hx hy hx_mant_ne hy_mant_ne hok hresult)

theorem operator_div_rounding_bound_upward_attained :
    ∃ (x y result : Number),
      x.isNormalized ∧ y.isNormalized ∧
      x.mantissa_ ≠ 0 ∧ y.mantissa_ ≠ 0 ∧
      Number.operator_div x y .upward = .ok result ∧
      result.mantissa_ ≠ 0 ∧
      |result.toRat - x.toRat / y.toRat| > |x.toRat / y.toRat| * (9 / (2 ^ 63 + 2 : ℚ)) := by
  refine ⟨⟨false, 1340000000000000000, 0⟩,
          ⟨false, 1452830911130575894, 0⟩,
          ⟨false, 9223372036854776120, -19⟩,
          ?_, ?_, ?_, ?_, ?_, ?_, ?_⟩
  · exact Or.inr ⟨by decide, by decide, by decide, by decide, by decide⟩
  · exact Or.inr ⟨by decide, by decide, by decide, by decide, by decide⟩
  · decide
  · decide
  · exact operator_div_upward_witness
  · decide
  · have hx_rat : (⟨false, 1340000000000000000, 0⟩ : Number).toRat = 1340000000000000000 := by
      unfold Number.toRat; rfl
    have hy_rat : (⟨false, 1452830911130575894, 0⟩ : Number).toRat = 1452830911130575894 := by
      unfold Number.toRat; rfl
    have hr_rat : (⟨false, 9223372036854776120, -19⟩ : Number).toRat
        = 9223372036854776120 / (10 : ℚ) ^ (19 : ℕ) := by
      unfold Number.toRat; simp; ring
    rw [hx_rat, hy_rat, hr_rat]
    norm_num

end XRPL.Model.Protocol
