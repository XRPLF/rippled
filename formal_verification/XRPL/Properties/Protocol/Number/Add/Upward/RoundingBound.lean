import XRPL.Properties.Protocol.Number.Common.Notation
import XRPL.Common.Approx
import XRPL.Properties.Protocol.Number.Add.Upward.BoundProof
import XRPL.Properties.Protocol.Number.Add.Upward.WitnessTrace

set_option linter.style.longLine false
set_option linter.style.emptyLine false

namespace XRPL.Model.Protocol

/-- `Rounds`-shaped restatement of `operator_add_rounding_bound_same_sign_upward`,
restricted to **non-negative** operands.

For the same-sign Add Upward bound with two negative operands at equal exponents,
a guard-driven drop can momentarily round the magnitude up — making `result` more
negative than `truth` and falsifying the directional `truth ≤ result` part of
`Rounds .upward`. The absolute-error bound
(`operator_add_rounding_bound_same_sign_upward`) holds universally; this
`Rounds`-shaped variant requires the positive branch. -/
theorem operator_add_rounds_same_sign_upward (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (h_same_sign : x.negative_ = y.negative_)
    (h_not_zero : ¬ x.operator_eq y.operator_neg)
    (_h_pos : x.negative_ = false)
    (hok : Number.operator_add x y .upward = .ok result)
    (hresult : result.mantissa_ ≠ 0)
    (h_dir : x.toRat + y.toRat ≤ result.toRat) :
    Rounds result (x.toRat + y.toRat) .upward (10 / (2 ^ 63 + 2 : ℚ)) := by
  refine ⟨h_dir, ?_⟩
  have h_abs_bound := operator_add_rounding_bound_same_sign_upward x y result hx hy
    hx_mant_ne hy_mant_ne h_same_sign h_not_zero hok hresult
  have h_abs_eq : |result.toRat - (x.toRat + y.toRat)| = result.toRat - (x.toRat + y.toRat) :=
    abs_of_nonneg (by linarith)
  rw [h_abs_eq] at h_abs_bound
  rw [show (Approx.toRat result : ℚ) = result.toRat from rfl]
  exact le_of_lt h_abs_bound

/-- `Rounds`-shaped restatement of the unified tight bound
`operator_add_rounding_bound_upward_tight`. Covers both same-sign and diff-sign
branches with the uniform magnitude scale `11/(2^63 - 18)`.

The directional component (`truth ≤ result`) holds under non-negative truth and
is **fully proved for the diff-sign branch** by
`operator_add_rounding_bound_diff_sign_upward_dir` (which shows that the diff-sign
guard's sticky-bit fact rules out a mantissa round-down, so the result lands at or
above the truth).  The **same-sign branch direction remains open** here, so the
unified wrapper still takes `h_dir : truth ≤ result` as a side hypothesis.

Blocker for same-sign: under non-negative (hence positive) operands the only way
`.upward` can land *below* the truth is the `doRoundUp_value_upward_truncate`
sub-case, where the result mantissa is `zm` and the truth is `(zm + f)·10^ze'`.
This is harmless precisely when `f = 0`, which follows from `g.sbit_ = false`
(the positive-operand guard never sets the sign bit) via the represents algebra.
Discharging it requires threading `g.sbit_ = false` out of
`operator_add_algorithmic_facts_same_sign_upward` (and the intermediate
`operator_add_post_alignment_upward`), mirroring the `g.sbit_`/`g.digits_` conjunct
already added to the diff-sign facts — left as future work. -/
theorem operator_add_rounds_upward (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (h_not_zero : ¬ x.operator_eq y.operator_neg)
    (hok : Number.operator_add x y .upward = .ok result)
    (hresult : result.mantissa_ ≠ 0)
    (h_dir : x.toRat + y.toRat ≤ result.toRat) :
    Rounds result (x.toRat + y.toRat) .upward (11 / (2 ^ 63 - 18 : ℚ)) := by
  refine ⟨h_dir, ?_⟩
  have h_bound := operator_add_rounding_bound_upward_tight x y result hx hy
    hx_mant_ne hy_mant_ne h_not_zero hok hresult
  have h_abs_eq : |result.toRat - (x.toRat + y.toRat)| = result.toRat - (x.toRat + y.toRat) :=
    abs_of_nonneg (by linarith)
  rw [h_abs_eq] at h_bound
  rw [show (Approx.toRat result : ℚ) = result.toRat from rfl]
  exact le_of_lt h_bound

/-! ### Tight sharpness witness for `operator_add_rounding_bound_same_sign_upward`

The bound `10/(2^63 + 2)` is the tight supremum: not attained, but witnessed by
inputs whose relative error strictly exceeds `9/(2^63 + 2)`.

Note: the same-sign Add Upward bound is stated as an **absolute** error bound
`|result - truth| ≤ |truth| · 10/(2^63+2)` rather than the directional `Rounds`
predicate, because for the same-sign branch with two negative operands at equal
exponents, a guard-driven drop can momentarily round the magnitude up — making
`result` more negative than `truth` and falsifying the directional
`truth ≤ result` part of `Rounds .upward`. The absolute bound still holds. -/

/-- The bound `10/(2^63 + 2)` is the tight supremum (not attained but witnessed by
inputs whose relative error strictly exceeds `9/(2^63 + 2)`).
Witness: `x = 10^18` at exponent 0, `y = 10^18` at exponent 20.
After alignment, `xm_a = 0` with the guard carrying a `1` at slot 14;
the sum is `10^18`, no further drop fires, and `.upward` rounds up to
`10^18 + 1` because the guard is nonzero.
Error = `10^20 - 10^18`, relative error = `99 * 10^18 / (10^38 + 10^18) > 9/(2^63 + 2)`. -/
theorem operator_add_rounding_bound_same_sign_upward_attained :
    ∃ (x y result : Number),
      x.isNormalized ∧ y.isNormalized ∧
      x.mantissa_ ≠ 0 ∧ y.mantissa_ ≠ 0 ∧
      Number.operator_add x y .upward = .ok result ∧
      result.mantissa_ ≠ 0 ∧
      |result.toRat - (x.toRat + y.toRat)|
        > |x.toRat + y.toRat| * (9 / (2 ^ 63 + 2 : ℚ)) := by
  refine ⟨⟨false, 1000000000000000000, 0⟩,
          ⟨false, 1000000000000000000, 20⟩,
          ⟨false, 1000000000000000001, 20⟩,
          ?_, ?_, ?_, ?_, ?_, ?_, ?_⟩
  · right
    refine ⟨?_, ?_, ?_, ?_, ?_⟩ <;> decide
  · right
    refine ⟨?_, ?_, ?_, ?_, ?_⟩ <;> decide
  · decide
  · decide
  · exact operator_add_upward_witness
  · decide
  · have hx_rat : (⟨false, 1000000000000000000, 0⟩ : Number).toRat = 1000000000000000000 := by
      unfold Number.toRat; rfl
    have hy_rat : (⟨false, 1000000000000000000, 20⟩ : Number).toRat
                = 1000000000000000000 * (10 : ℚ) ^ (20 : ℕ) := by
      unfold Number.toRat; simp; ring
    have hr_rat : (⟨false, 1000000000000000001, 20⟩ : Number).toRat
                = 1000000000000000001 * (10 : ℚ) ^ (20 : ℕ) := by
      unfold Number.toRat; simp; ring
    rw [hx_rat, hy_rat, hr_rat]
    rw [show ((2 : ℚ) ^ 63 + 2) = maxRepCuspTarget by norm_num]
    rw [show (1000000000000000000 : ℚ) + 1000000000000000000 * (10 : ℚ) ^ (20 : ℕ)
          = 100000000000000000001000000000000000000 by norm_num]
    rw [show (1000000000000000001 : ℚ) * (10 : ℚ) ^ (20 : ℕ)
            - 100000000000000000001000000000000000000
          = 99000000000000000000 by norm_num]
    rw [abs_of_pos (by norm_num : (0 : ℚ) < 99000000000000000000)]
    rw [abs_of_pos (by norm_num : (0 : ℚ) < 100000000000000000001000000000000000000)]
    rw [show (100000000000000000001000000000000000000 : ℚ) * (9 / (maxRepCuspTarget : ℚ))
          = 9 * 100000000000000000001000000000000000000 / maxRepCuspTarget by ring]
    rw [gt_iff_lt, div_lt_iff₀ (by norm_num : (0 : ℚ) < maxRepCuspTarget)]
    norm_num

end XRPL.Model.Protocol
