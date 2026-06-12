import XRPL.Properties.Protocol.Number.Mul.ToNearest.BoundProof
import XRPL.Properties.Protocol.Number.Mul.ToNearest.WitnessTrace

set_option linter.style.longLine false
set_option linter.style.emptyLine false

namespace XRPL.Model.Protocol

/-- `Rounds`-shaped restatement of `operator_mul_rounding_bound`. -/
theorem operator_mul_rounds (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (hok : Number.operator_mul x y .to_nearest = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    Rounds result (x.toRat * y.toRat) .to_nearest (5 / (2 ^ 63 + 7 : ℚ)) :=
  operator_mul_rounding_bound x y result hx hy hx_mant_ne hy_mant_ne hok hresult

/-- The bound `5/(2^63 + 7)` is attained exactly, so it is the tight supremum. -/
theorem operator_mul_rounding_bound_attained :
    ∃ (x y result : Number),
      x.isNormalized ∧ y.isNormalized ∧
      x.mantissa_ ≠ 0 ∧ y.mantissa_ ≠ 0 ∧
      Number.operator_mul x y .to_nearest = .ok result ∧
      result.mantissa_ ≠ 0 ∧
      |result.toRat - x.toRat * y.toRat|
        = |x.toRat * y.toRat| * (5 / (2 ^ 63 + 7 : ℚ)) := by
  refine ⟨⟨false, 5000000000000000000, 0⟩,
          ⟨false, 1844674407370955163, 0⟩,
          ⟨false, 9223372036854775820, 18⟩,
          ?_, ?_, ?_, ?_, ?_, ?_, ?_⟩
  · right
    refine ⟨?_, ?_, ?_, ?_, ?_⟩ <;> decide
  · right
    refine ⟨?_, ?_, ?_, ?_, ?_⟩ <;> decide
  · decide
  · decide
  · exact operator_mul_witness
  · decide
  · have hx_rat : (⟨false, 5000000000000000000, 0⟩ : Number).toRat = 5000000000000000000 := by
      unfold Number.toRat; rfl
    have hy_rat : (⟨false, 1844674407370955163, 0⟩ : Number).toRat = 1844674407370955163 := by
      unfold Number.toRat; rfl
    have hr_rat : (⟨false, 9223372036854775820, 18⟩ : Number).toRat
                = 9223372036854775820 * (10 : ℚ) ^ (18 : ℕ) := by
      unfold Number.toRat; simp; ring
    rw [hx_rat, hy_rat, hr_rat]
    change |9223372036854775820 * (10 : ℚ) ^ (18 : ℕ) - 5000000000000000000 * 1844674407370955163|
       = |(5000000000000000000 : ℚ) * 1844674407370955163| * (5 / (2 ^ 63 + 7 : ℚ))
    rw [show (5000000000000000000 : ℚ) * 1844674407370955163
          = 9223372036854775815 * 10 ^ (18 : ℕ) by norm_num]
    rw [show (9223372036854775820 : ℚ) * 10 ^ (18 : ℕ) - 9223372036854775815 * 10 ^ (18 : ℕ)
          = 5 * 10 ^ (18 : ℕ) by ring]
    rw [show ((2 : ℚ) ^ 63 + 7) = 9223372036854775815 by norm_num]
    rw [abs_of_pos (by positivity : (0 : ℚ) < 5 * 10 ^ (18 : ℕ))]
    rw [abs_of_pos (by positivity : (0 : ℚ) < 9223372036854775815 * 10 ^ (18 : ℕ))]
    field_simp

end XRPL.Model.Protocol
