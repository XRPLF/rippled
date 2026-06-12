import XRPL.Properties.Protocol.Number.Common.Notation
import XRPL.Properties.Protocol.Number.Mul.Upward.BoundProof
import XRPL.Properties.Protocol.Number.Mul.Upward.WitnessTrace

set_option linter.style.longLine false
set_option linter.style.emptyLine false

namespace XRPL.Model.Protocol


theorem operator_mul_rounds_upward (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (hok : Number.operator_mul x y .upward = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    Rounds result (x.toRat * y.toRat) .upward (10 / (2 ^ 63 + 2 : ℚ)) := by
  obtain ⟨h_dir, h_mag⟩ :=
    operator_mul_rounding_bound_upward x y result hx hy hx_mant_ne hy_mant_ne hok hresult
  exact ⟨h_dir, le_of_lt h_mag⟩

theorem operator_mul_rounding_bound_upward_attained :
    ∃ (x y result : Number),
      x.isNormalized ∧ y.isNormalized ∧
      x.mantissa_ ≠ 0 ∧ y.mantissa_ ≠ 0 ∧
      Number.operator_mul x y .upward = .ok result ∧
      result.mantissa_ ≠ 0 ∧
      |result.toRat - x.toRat * y.toRat| > |x.toRat * y.toRat| * (9 / (2 ^ 63 + 2 : ℚ)) := by
  refine ⟨⟨false, maxRepNat, 0⟩,
          ⟨false, 1000000000000000009, 0⟩,
          ⟨false, mulUpwardWitness, 18⟩,
          ?_, ?_, ?_, ?_, ?_, ?_, ?_⟩
  · right
    refine ⟨?_, ?_, ?_, ?_, ?_⟩ <;> decide
  · right
    refine ⟨?_, ?_, ?_, ?_, ?_⟩ <;> decide
  · decide
  · decide
  · exact operator_mul_upward_witness
  · decide
  · have hx_rat : (⟨false, maxRepNat, 0⟩ : Number).toRat = maxRepNat := by
      unfold Number.toRat; rfl
    have hy_rat : (⟨false, 1000000000000000009, 0⟩ : Number).toRat = 1000000000000000009 := by
      unfold Number.toRat; rfl
    have hr_rat : (⟨false, mulUpwardWitness, 18⟩ : Number).toRat
                = mulUpwardWitness * (10 : ℚ) ^ (18 : ℕ) := by
      unfold Number.toRat; simp; ring
    rw [hx_rat, hy_rat, hr_rat]
    -- truth = maxRepNat * 1000000000000000009
    -- result = mulUpwardWitness * 10^18
    -- error = result - truth = mulUpwardWitness * 10^18 - maxRepNat * 1000000000000000009
    --       = 9989651668307017737
    rw [show (maxRepNat : ℚ) * 1000000000000000009
          = 9223372036854775890010348331692982263 / 1 by norm_num]
    rw [show mulUpwardWitness * (10 : ℚ) ^ (18 : ℕ) - 9223372036854775890010348331692982263 / 1
          = 9989651668307017737 * 10 ^ (18 : ℕ) / 10 ^ (18 : ℕ) by norm_num]
    rw [show ((2 : ℚ) ^ 63 + 2) = maxRepCuspTarget by norm_num]
    rw [abs_of_pos (by positivity : (0 : ℚ) < 9989651668307017737 * 10 ^ (18 : ℕ) / 10 ^ (18 : ℕ))]
    rw [abs_of_pos (by norm_num : (0 : ℚ) < 9223372036854775890010348331692982263 / 1)]
    rw [show (9223372036854775890010348331692982263 : ℚ) / 1 *
          (9 / (maxRepCuspTarget : ℚ))
          = 9 * 9223372036854775890010348331692982263 / maxRepCuspTarget by ring]
    rw [show (9989651668307017737 : ℚ) * 10 ^ (18 : ℕ) / 10 ^ (18 : ℕ) = 9989651668307017737 by
          field_simp]
    norm_num

end XRPL.Model.Protocol
