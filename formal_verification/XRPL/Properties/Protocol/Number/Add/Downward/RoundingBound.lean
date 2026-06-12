import XRPL.Properties.Protocol.Number.Common.Notation
import XRPL.Common.Approx
import XRPL.Properties.Protocol.Number.Add.Downward.BoundProof
import XRPL.Properties.Protocol.Number.Add.Downward.WitnessTrace

set_option linter.style.longLine false
set_option linter.style.emptyLine false

namespace XRPL.Model.Protocol

/-- `Rounds`-shaped restatement of `operator_add_rounding_bound_same_sign_downward`,
restricted to **non-negative** operands.

For same-sign Adds with the cusp scaling, the C++ algorithm uses `Guard.new`
as the initial guard when `xe = ye`, which does not carry the sign bit. This
means that for negative same-sign sums in the cusp case, the algorithm rounds
the magnitude DOWN (toward zero, i.e. UP in the signed sense) rather than
toward `-∞`. The directional `Rounds` predicate therefore only holds in the
non-negative branch; the magnitude bound
(`operator_add_rounding_bound_same_sign_downward`) holds universally. -/
theorem operator_add_rounds_same_sign_downward (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (h_same_sign : x.negative_ = y.negative_)
    (h_not_zero : ¬ x.operator_eq y.operator_neg)
    (h_pos : x.negative_ = false)
    (hok : Number.operator_add x y .downward = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    Rounds result (x.toRat + y.toRat) .downward (10 / (2 ^ 63 + 2 : ℚ)) := by
  obtain ⟨zm, ze', f, g, res_pos, hzm_ge, hzm_le_maxRep, hf_nn, hf_lt1, _h_floor_constraint,
          habs_xy_eq, h_rup_pos, h_result_abs, hres_pos_mant_ne, hf_rep, h_sign, h_sbit_pos⟩ :=
    operator_add_algorithmic_facts_same_sign_downward x y result hx hy hx_mant_ne hy_mant_ne
      h_same_sign h_not_zero hok hresult
  have hy_pos : y.negative_ = false := h_same_sign ▸ h_pos
  have h_result_pos : result.negative_ = false := h_sign.trans h_pos
  have hx_nn : 0 ≤ x.toRat := Number.toRat_nonneg_of_nonnegative x h_pos
  have hy_nn : 0 ≤ y.toRat := Number.toRat_nonneg_of_nonnegative y hy_pos
  have h_truth_nn : 0 ≤ x.toRat + y.toRat := by linarith
  have h_abs_truth : |x.toRat + y.toRat| = x.toRat + y.toRat := abs_of_nonneg h_truth_nn
  have h_result_eq_abs : result.toRat = |result.toRat| := by
    have h_result_nn : 0 ≤ result.toRat := Number.toRat_nonneg_of_nonnegative result h_result_pos
    rw [abs_of_nonneg h_result_nn]
  -- Truth via positive form.
  have h_xy_signed : x.toRat + y.toRat = ((zm.toNat : ℚ) + f) * 10 ^ ze' := by
    have := habs_xy_eq; rw [h_abs_truth] at this; exact this
  have h_result_signed : result.toRat = (res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_ := by
    rw [h_result_eq_abs]; exact h_result_abs
  -- For positive operands, g.sbit_ = false, so shouldRoundUp_downward is impossible.
  have h_g_sbit_false : g.sbit_ = false := h_sbit_pos h_pos
  have h_no_sru : ¬ g.shouldRoundUp_downward := by
    intro h
    have : g.sbit_ = true := h.1
    rw [h_g_sbit_false] at this; exact Bool.noConfusion this
  -- doRoundUp truncates.
  have h_tr_val := doRoundUp_value_downward_truncate g false zm ze' h_no_sru
    "Number::addition overflow" res_pos h_rup_pos hres_pos_mant_ne
  simp only at h_tr_val
  have h10ze'_pos : (0 : ℚ) < 10 ^ ze' := zpow_pos (by norm_num) _
  have h10ze'_nn : (0 : ℚ) ≤ 10 ^ ze' := le_of_lt h10ze'_pos
  have hzm_q_ge : (mantissaFloor : ℚ) ≤ (zm.toNat : ℚ) := by exact_mod_cast hzm_ge
  have h_denom_val : ((2 ^ 63 + 2 : ℚ)) = maxRepCuspTarget := by norm_num
  -- Show Rounds.
  have h_approx : (Approx.toRat result : ℚ) = result.toRat := rfl
  refine ⟨?_, ?_⟩
  · -- Direction
    rw [h_approx, h_result_signed, h_xy_signed, h_tr_val]
    nlinarith [h10ze'_nn, hf_nn]
  · -- Magnitude
    rw [h_approx, h_abs_truth, h_result_signed, h_xy_signed, h_tr_val]
    have h_diff : ((zm.toNat : ℚ) + f) * 10 ^ ze' - (zm.toNat : ℚ) * 10 ^ ze'
        = f * 10 ^ ze' := by ring
    rw [h_diff]
    have h_inner : f ≤ (((zm.toNat : ℚ) + f)) * (10 / ((2 ^ 63 + 2 : ℚ))) := by
      rw [h_denom_val]
      rw [show (((zm.toNat : ℚ) + f)) * (10 / (maxRepCuspTarget : ℚ))
            = 10 * ((zm.toNat : ℚ) + f) / maxRepCuspTarget by ring]
      rw [le_div_iff₀ (by norm_num : (0 : ℚ) < maxRepCuspTarget)]
      have h10zm : 9223372036854775800 ≤ 10 * (zm.toNat : ℚ) := by linarith
      have hf_bound : f * 9223372036854775800 ≤ 9223372036854775800 := by
        nlinarith [hf_lt1]
      linarith
    calc f * 10 ^ ze'
        ≤ (((zm.toNat : ℚ) + f)) * (10 / ((2 ^ 63 + 2 : ℚ))) * 10 ^ ze' :=
          mul_le_mul_of_nonneg_right h_inner h10ze'_nn
      _ = (((zm.toNat : ℚ) + f)) * 10 ^ ze' * (10 / ((2 ^ 63 + 2 : ℚ))) := by ring

/-- `Rounds`-shaped restatement of the unified tight bound
`operator_add_rounding_bound_downward_tight`. Covers both same-sign and diff-sign
branches with the uniform magnitude scale `11/(2^63 - 18)`.

The directional component (`result ≤ truth`) holds under the **non-negative truth**
hypothesis `h_truth_nn : 0 ≤ x.toRat + y.toRat` (replacing the former circular
`h_dir`). For negative truth the directed-rounding quirk (the `xe = ye` guard uses
`Guard.new`, dropping the sign bit) means the magnitude can round the "wrong" way,
so the directional half is genuinely restricted to non-negative truth. The
direction is dispatched to `operator_add_rounds_same_sign_downward` (same sign) and
`operator_add_rounding_bound_diff_sign_downward_dir` (different signs). -/
theorem operator_add_rounds_downward (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (h_not_zero : ¬ x.operator_eq y.operator_neg)
    (hok : Number.operator_add x y .downward = .ok result)
    (hresult : result.mantissa_ ≠ 0)
    (h_truth_nn : 0 ≤ x.toRat + y.toRat) :
    Rounds result (x.toRat + y.toRat) .downward (11 / (2 ^ 63 - 18 : ℚ)) := by
  -- Direction: dispatch on the sign relationship.
  have h_dir : result.toRat ≤ x.toRat + y.toRat := by
    by_cases h_sign : x.negative_ = y.negative_
    · -- Same sign: non-negative truth forces both operands non-negative.
      have hx_pos : x.negative_ = false := by
        by_contra hxn
        have hxn' : x.negative_ = true := by
          cases hxc : x.negative_ with
          | false => exact absurd hxc hxn
          | true => rfl
        have hyn' : y.negative_ = true := h_sign ▸ hxn'
        have hx_np : x.toRat ≤ 0 := Number.toRat_nonpos_of_negative x hxn'
        have hy_np : y.toRat ≤ 0 := Number.toRat_nonpos_of_negative y hyn'
        have hx_abs_pos : 0 < |x.toRat| := by
          rw [abs_toRat_eq x]
          apply mul_pos
          · exact_mod_cast Nat.pos_of_ne_zero (fun h => hx_mant_ne (UInt64.toNat_inj.mp (by rw [h]; rfl)))
          · exact zpow_pos (by norm_num) _
        have hx_ne0 : x.toRat ≠ 0 := fun h => by rw [h, abs_zero] at hx_abs_pos; exact lt_irrefl _ hx_abs_pos
        have hx_neg : x.toRat < 0 := lt_of_le_of_ne hx_np hx_ne0
        linarith
      exact (operator_add_rounds_same_sign_downward x y result hx hy hx_mant_ne hy_mant_ne
        h_sign h_not_zero hx_pos hok hresult).1
    · exact operator_add_rounding_bound_diff_sign_downward_dir x y result hx hy hx_mant_ne hy_mant_ne
        h_sign h_not_zero hok hresult h_truth_nn
  refine ⟨h_dir, ?_⟩
  have h_bound := operator_add_rounding_bound_downward_tight x y result hx hy
    hx_mant_ne hy_mant_ne h_not_zero hok hresult
  have h_abs_eq : |result.toRat - (x.toRat + y.toRat)|
      = (x.toRat + y.toRat) - result.toRat := by
    rw [show result.toRat - (x.toRat + y.toRat)
        = -((x.toRat + y.toRat) - result.toRat) from by ring]
    rw [abs_neg, abs_of_nonneg (by linarith)]
  rw [h_abs_eq] at h_bound
  rw [show (Approx.toRat result : ℚ) = result.toRat from rfl]
  linarith

/-- The bound `10/(2^63+2)` is tight for `.downward`: there exist normalized inputs
where the relative error exceeds `9/(2^63+2)`, proving 10 is the optimal integer
numerator. -/
theorem operator_add_rounding_bound_same_sign_downward_attained :
    ∃ (x y result : Number),
      x.isNormalized ∧ y.isNormalized ∧
      x.mantissa_ ≠ 0 ∧ y.mantissa_ ≠ 0 ∧
      Number.operator_add x y .downward = .ok result ∧
      result.mantissa_ ≠ 0 ∧
      |result.toRat - (x.toRat + y.toRat)|
        > |x.toRat + y.toRat| * (9 / (2 ^ 63 + 2 : ℚ)) := by
  refine ⟨⟨false, 5000000000000000001, 0⟩,
          ⟨false, 4223372036854775808, 0⟩,
          ⟨false, 9223372036854775800, 0⟩,
          ?_, ?_, ?_, ?_, ?_, ?_, ?_⟩
  · right
    refine ⟨?_, ?_, ?_, ?_, ?_⟩ <;> decide
  · right
    refine ⟨?_, ?_, ?_, ?_, ?_⟩ <;> decide
  · decide
  · decide
  · exact operator_add_downward_witness
  · decide
  · have hx_rat : (⟨false, 5000000000000000001, 0⟩ : Number).toRat = 5000000000000000001 := by
      unfold Number.toRat; rfl
    have hy_rat : (⟨false, 4223372036854775808, 0⟩ : Number).toRat = 4223372036854775808 := by
      unfold Number.toRat; rfl
    have hr_rat : (⟨false, 9223372036854775800, 0⟩ : Number).toRat = 9223372036854775800 := by
      unfold Number.toRat; rfl
    rw [hx_rat, hy_rat, hr_rat]
    change |(9223372036854775800 : ℚ) - (5000000000000000001 + 4223372036854775808)|
       > |(5000000000000000001 : ℚ) + 4223372036854775808| * (9 / (2 ^ 63 + 2 : ℚ))
    rw [show ((2 : ℚ) ^ 63 + 2) = maxRepCuspTarget by norm_num]
    rw [show (5000000000000000001 : ℚ) + 4223372036854775808 = 9223372036854775809 by norm_num]
    rw [show (9223372036854775800 : ℚ) - 9223372036854775809 = -9 by norm_num]
    rw [abs_neg]
    rw [abs_of_pos (by norm_num : (0 : ℚ) < 9)]
    rw [abs_of_pos (by norm_num : (0 : ℚ) < 9223372036854775809)]
    rw [gt_iff_lt, show (9223372036854775809 : ℚ) * (9 / maxRepCuspTarget)
                       = 9 * 9223372036854775809 / maxRepCuspTarget by ring]
    rw [div_lt_iff₀ (by norm_num : (0 : ℚ) < maxRepCuspTarget)]
    norm_num

end XRPL.Model.Protocol
