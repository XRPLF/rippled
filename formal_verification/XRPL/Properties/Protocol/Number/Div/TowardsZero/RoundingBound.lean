import XRPL.Properties.Protocol.Number.Div.TowardsZero.BoundProof
import XRPL.Properties.Protocol.Number.Div.TowardsZero.WitnessTrace

set_option linter.style.longLine false
set_option linter.style.emptyLine false

namespace XRPL.Model.Protocol

theorem operator_div_rounds_towards_zero (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (hok : Number.operator_div x y .towards_zero = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    Rounds result (x.toRat / y.toRat) .towards_zero (10 / ((2 : ℚ) ^ 63 + 2)) := by
  obtain ⟨h_dir, h_mag⟩ :=
    operator_div_rounding_bound_towards_zero x y result hx hy hx_mant_ne hy_mant_ne hok hresult
  exact ⟨h_dir, le_of_lt h_mag⟩

theorem operator_div_rounding_bound_towards_zero_attained :
    ∃ (x y result : Number),
      x.isNormalized ∧ y.isNormalized ∧
      x.mantissa_ ≠ 0 ∧ y.mantissa_ ≠ 0 ∧
      Number.operator_div x y .towards_zero = .ok result ∧
      result.mantissa_ ≠ 0 ∧
      |result.toRat - x.toRat / y.toRat| > |x.toRat / y.toRat| * (9 / (2 ^ 63 + 2 : ℚ)) := by
  refine ⟨⟨false, 1425000000000000000, 0⟩,
          ⟨false, 1544988095791843793, 0⟩,
          ⟨false, 9223372036854775950, -19⟩,
          ?_, ?_, ?_, ?_, ?_, ?_, ?_⟩
  · exact Or.inr ⟨by decide, by decide, by decide, by decide, by decide⟩
  · exact Or.inr ⟨by decide, by decide, by decide, by decide, by decide⟩
  · decide
  · decide
  · exact operator_div_towards_zero_witness
  · decide
  · have hx_rat : (⟨false, 1425000000000000000, 0⟩ : Number).toRat = 1425000000000000000 := by
      unfold Number.toRat; rfl
    have hy_rat : (⟨false, 1544988095791843793, 0⟩ : Number).toRat = 1544988095791843793 := by
      unfold Number.toRat; rfl
    have hr_rat : (⟨false, 9223372036854775950, -19⟩ : Number).toRat
        = 9223372036854775950 / (10 : ℚ) ^ (19 : ℕ) := by
      unfold Number.toRat; simp; ring
    rw [hx_rat, hy_rat, hr_rat]
    norm_num

end XRPL.Model.Protocol
