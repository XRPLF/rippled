import XRPL.Properties.Protocol.Number.Common.Notation
import XRPL.Properties.Protocol.Number.Add.TowardsZero.BoundProof
import XRPL.Properties.Protocol.Number.Add.TowardsZero.WitnessTrace

set_option linter.style.longLine false
set_option linter.style.emptyLine false

namespace XRPL.Model.Protocol

/-- `Rounds`-shaped restatement of `operator_add_rounding_bound_same_sign_towards_zero`. -/
theorem operator_add_rounds_same_sign_towards_zero (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (h_same_sign : x.negative_ = y.negative_)
    (h_not_zero : ¬ x.operator_eq y.operator_neg)
    (hok : Number.operator_add x y .towards_zero = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    Rounds result (x.toRat + y.toRat) .towards_zero (10 / (2 ^ 63 + 2 : ℚ)) := by
  obtain ⟨h_dir, h_mag⟩ := operator_add_rounding_bound_same_sign_towards_zero x y result hx hy
    hx_mant_ne hy_mant_ne h_same_sign h_not_zero hok hresult
  exact ⟨h_dir, le_of_lt h_mag⟩

/-- `Rounds`-shaped restatement of the unified tight bound
`operator_add_rounding_bound_towards_zero_tight`. Covers both same-sign and
diff-sign branches with the uniform magnitude scale `11/(2^63 - 18)`.

The directional component (`|result| ≤ |truth|`) is supplied as the side
hypothesis `h_dir`, and **cannot be removed**: for the diff-sign branch the
truth magnitude is `(zm - f)·10^ze'` (the integer mantissa `zm` minus a positive
fraction), and `.towards_zero` truncates to the *floor* mantissa `zm`, giving
`|result| = zm·10^ze' > (zm - f)·10^ze' = |truth|` whenever `f > 0`.  So
`.towards_zero` rounds **away** from zero here, not toward it.  A concrete
witness (both operands normalized): `x = ⟨false, maxRepNat, 1⟩`,
`y = ⟨true, 1000000000000000003, 0⟩` gives `result = ⟨false, 9123372036854775807, 1⟩`
with `|result| = 91233720368547758070 > 91233720368547758067 = |truth|`.  Hence
the magnitude direction is genuinely non-universal and `h_dir` is load-bearing,
unlike the `.downward`/`.upward` directions (provable under non-negative truth). -/
theorem operator_add_rounds_towards_zero (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (h_not_zero : ¬ x.operator_eq y.operator_neg)
    (hok : Number.operator_add x y .towards_zero = .ok result)
    (hresult : result.mantissa_ ≠ 0)
    (h_dir : |result.toRat| ≤ |x.toRat + y.toRat|) :
    Rounds result (x.toRat + y.toRat) .towards_zero (11 / (2 ^ 63 - 18 : ℚ)) := by
  refine ⟨h_dir, ?_⟩
  have h_bound := operator_add_rounding_bound_towards_zero_tight x y result hx hy
    hx_mant_ne hy_mant_ne h_not_zero hok hresult
  have h_le : |x.toRat + y.toRat| - |Approx.toRat result|
      ≤ |result.toRat - (x.toRat + y.toRat)| := by
    have h1 := abs_sub_abs_le_abs_sub (x.toRat + y.toRat) result.toRat
    rw [show (Approx.toRat result : ℚ) = result.toRat from rfl]
    calc |x.toRat + y.toRat| - |result.toRat|
        ≤ |(x.toRat + y.toRat) - result.toRat| := h1
      _ = |result.toRat - (x.toRat + y.toRat)| := abs_sub_comm _ _
  linarith

/-- The bound `10/(2^63 + 2)` is the **tight supremum** but is **not attained**.
Witness inputs `x = (false, 10^18, 0)`, `y = (false, 8223372036854775809, 0)`
produce a relative error strictly greater than `9/(2^63 + 2)`. -/
theorem operator_add_rounding_bound_same_sign_towards_zero_attained :
    ∃ (x y result : Number),
      x.isNormalized ∧ y.isNormalized ∧
      x.mantissa_ ≠ 0 ∧ y.mantissa_ ≠ 0 ∧
      Number.operator_add x y .towards_zero = .ok result ∧
      result.mantissa_ ≠ 0 ∧
      |result.toRat - (x.toRat + y.toRat)|
        > |x.toRat + y.toRat| * (9 / (2 ^ 63 + 2 : ℚ)) := by
  refine ⟨⟨false, 1000000000000000000, 0⟩,
          ⟨false, 8223372036854775809, 0⟩,
          ⟨false, 9223372036854775800, 0⟩,
          ?_, ?_, ?_, ?_, ?_, ?_, ?_⟩
  · right
    refine ⟨?_, ?_, ?_, ?_, ?_⟩ <;> decide
  · right
    refine ⟨?_, ?_, ?_, ?_, ?_⟩ <;> decide
  · decide
  · decide
  · exact operator_add_towards_zero_witness
  · decide
  · have hx_rat : (⟨false, 1000000000000000000, 0⟩ : Number).toRat = 1000000000000000000 := by
      unfold Number.toRat; rfl
    have hy_rat : (⟨false, 8223372036854775809, 0⟩ : Number).toRat = 8223372036854775809 := by
      unfold Number.toRat; rfl
    have hr_rat : (⟨false, 9223372036854775800, 0⟩ : Number).toRat = 9223372036854775800 := by
      unfold Number.toRat; rfl
    rw [hx_rat, hy_rat, hr_rat]
    rw [show (1000000000000000000 : ℚ) + 8223372036854775809 = 9223372036854775809 by norm_num]
    rw [show (9223372036854775800 : ℚ) - 9223372036854775809 = -9 by norm_num]
    rw [show ((2 : ℚ) ^ 63 + 2) = maxRepCuspTarget by norm_num]
    rw [abs_neg]
    rw [abs_of_pos (by norm_num : (0 : ℚ) < 9)]
    rw [abs_of_pos (by norm_num : (0 : ℚ) < 9223372036854775809)]
    rw [show (9223372036854775809 : ℚ) * (9 / maxRepCuspTarget)
          = 9 * 9223372036854775809 / maxRepCuspTarget by ring]
    rw [gt_iff_lt, div_lt_iff₀ (by norm_num : (0 : ℚ) < maxRepCuspTarget)]
    norm_num

end XRPL.Model.Protocol
