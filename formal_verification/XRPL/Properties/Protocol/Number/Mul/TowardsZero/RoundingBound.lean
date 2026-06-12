import XRPL.Properties.Protocol.Number.Common.Notation
import XRPL.Properties.Protocol.Number.Mul.TowardsZero.BoundProof
import XRPL.Properties.Protocol.Number.Mul.TowardsZero.WitnessTrace

set_option linter.style.longLine false
set_option linter.style.emptyLine false

namespace XRPL.Model.Protocol


theorem operator_mul_rounds_towards_zero (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (hok : Number.operator_mul x y .towards_zero = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    Rounds result (x.toRat * y.toRat) .towards_zero (10 / (2 ^ 63 + 2 : ℚ)) := by
  obtain ⟨h_dir, h_mag⟩ :=
    operator_mul_rounding_bound_towards_zero x y result hx hy hx_mant_ne hy_mant_ne hok hresult
  exact ⟨h_dir, le_of_lt h_mag⟩

theorem operator_mul_rounding_bound_towards_zero_attained :
    ∃ (x y result : Number),
      x.isNormalized ∧ y.isNormalized ∧
      x.mantissa_ ≠ 0 ∧ y.mantissa_ ≠ 0 ∧
      Number.operator_mul x y .towards_zero = .ok result ∧
      result.mantissa_ ≠ 0 ∧
      |result.toRat - x.toRat * y.toRat| > |x.toRat * y.toRat| * (9 / (2 ^ 63 + 2 : ℚ)) := by
  refine ⟨⟨false, 9223372036854775800, 0⟩,
          ⟨false, 1000000000000000001, 0⟩,
          ⟨false, 9223372036854775800, 18⟩,
          ?_, ?_, ?_, ?_, ?_, ?_, ?_⟩
  · right
    refine ⟨?_, ?_, ?_, ?_, ?_⟩ <;> decide
  · right
    refine ⟨?_, ?_, ?_, ?_, ?_⟩ <;> decide
  · decide
  · decide
  · exact operator_mul_towards_zero_witness
  · decide
  · have hx_rat : (⟨false, 9223372036854775800, 0⟩ : Number).toRat = 9223372036854775800 := by
      unfold Number.toRat; rfl
    have hy_rat : (⟨false, 1000000000000000001, 0⟩ : Number).toRat = 1000000000000000001 := by
      unfold Number.toRat; rfl
    have hr_rat : (⟨false, 9223372036854775800, 18⟩ : Number).toRat
                = 9223372036854775800 * (10 : ℚ) ^ (18 : ℕ) := by
      unfold Number.toRat; simp; ring
    rw [hx_rat, hy_rat, hr_rat]
    rw [show (9223372036854775800 : ℚ) * 1000000000000000001
          = 9223372036854775800 * 10 ^ (18 : ℕ) + 9223372036854775800 by norm_num]
    rw [show 9223372036854775800 * (10 : ℚ) ^ (18 : ℕ) -
          (9223372036854775800 * 10 ^ (18 : ℕ) + 9223372036854775800)
          = -(9223372036854775800 : ℚ) by ring]
    rw [show ((2 : ℚ) ^ 63 + 2) = maxRepCuspTarget by norm_num]
    rw [abs_neg]
    rw [abs_of_pos (by positivity : (0 : ℚ) < (9223372036854775800 : ℚ))]
    rw [abs_of_pos (by positivity : (0 : ℚ) < 9223372036854775800 * 10 ^ (18 : ℕ) + 9223372036854775800)]
    rw [show (9223372036854775800 * 10 ^ (18 : ℕ) + 9223372036854775800) *
          (9 / (maxRepCuspTarget : ℚ))
          = 9 * (9223372036854775800 * 10 ^ (18 : ℕ) + 9223372036854775800) / maxRepCuspTarget by ring]
    rw [gt_iff_lt, div_lt_iff₀ (by norm_num : (0 : ℚ) < maxRepCuspTarget)]
    norm_num

end XRPL.Model.Protocol
