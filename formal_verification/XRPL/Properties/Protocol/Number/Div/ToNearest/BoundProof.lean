import XRPL.Properties.Protocol.Number.Common.Notation
import Mathlib.Tactic

import XRPL.Common.Approx
import XRPL.Properties.Protocol.Number.Common.Helpers
import XRPL.Properties.Protocol.Number.Div.ToNearest.AlgorithmicFacts

set_option linter.style.longLine false
set_option linter.style.emptyLine false

namespace XRPL.Model.Protocol

/-! # Division rounding bound (`.to_nearest`)

The relative error of `operator_div` under `.to_nearest` is at most `6/(2^63+18)`.

Unlike multiplication (where the tight bound is `5/(2^63+7)`), division has a slightly
larger error because `divQuotient128` truncates the quotient to an integer, losing a
remainder fraction `δ` that the guard cannot see. The rounding decision is based on the
guard fraction `f` alone, not `f + δ`, so when `f = 1/2` (tie) and `δ > 0`, the result
can be slightly further from truth than multiplication would produce.

The bound `6/(2^63+18)` is the exact supremum, approached (but never attained) at
`zm = floor+2`, `f = 1/2`, `δ → 1/10`. The witness `operator_div_rounding_bound_attained`
proves that the multiplication bound `5/(2^63+7)` is violated.

A tighter bound `50000000000000001/92233720368547758250000000000000001` ≈ `5/(2^63+7) + 10⁻³⁵`
is achievable by proving `δ < 1/10^17` (from `k ≥ 17` in the correction path), but
the improvement over `6/(2^63+18)` is negligible (~10⁻³⁵ relative) and the bound
loses its clean closed form.

## Proof structure

**Case 1 (R ≥ T):** `R - T ≤ R - A ≤ A·5/(2^63+7) ≤ T·6/(2^63+18)`.

**Case 2 (R < T, no round up):**
- δ = 0: identical to multiplication, bounded by `5/(2^63+7) ≤ 6/(2^63+18)`.
- δ > 0, f < 1/2: `f + δ < 1/2` (integer granularity of f), giving
  `(f+δ)(2^63+12) < (1/2)(2^63+12) ≤ 6·(floor+1) ≤ 6·zm`.
- δ > 0, f = 1/2: tie-to-even forces zm even; zm ≥ floor+1 (odd) gives zm ≥ floor+2.
  Then `(f+δ)(2^63+12) < (3/5)(2^63+12) = 6·(floor+2) ≤ 6·zm`. -/

-- Case 2 algebraic inequality for δ > 0: (f+δ)(2^63+12) ≤ 6·zm
-- implies (f+δ)(2^63+18) ≤ 6·(zm+f+δ) since 2^63+18 = (2^63+12) + 6.
private lemma div_case2_ineq (zm_q f delta : ℚ)
    (h_tight : (f + delta) * ((2 : ℚ) ^ 63 + 12) ≤ 6 * zm_q) :
    (f + delta) * ((2 : ℚ) ^ 63 + 18) ≤ 6 * (zm_q + f + delta) := by
  nlinarith

-- Large existential destructuring from algorithmic_facts + rational algebra with 10^ze'
-- and 2^63+7 denominators, plus case-split proof on R vs T
set_option maxHeartbeats 1600000 in -- existential destructuring + 3-way case split on R vs T vs δ
theorem operator_div_rounding_bound (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (hok : Number.operator_div x y .to_nearest = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    |result.toRat - x.toRat / y.toRat| ≤ |x.toRat / y.toRat| * (6 / ((2 : ℚ) ^ 63 + 18)) := by
  obtain ⟨zm, ze', f, δ, g, res_pos, hzm_ge, hzm_le, hf_nn, hf_lt, hδ_nn, hfδ_lt,
          habs_xy_eq, h_rup_pos, h_result_abs, hres_pos_mant_ne, hf_rep, h_sign, h_floor_constraint,
          h_fδ_half, hδ_lt_tenth⟩ :=
    operator_div_algorithmic_facts x y result hx hy hx_mant_ne hy_mant_ne hok hresult
  have hy_toRat_ne : y.toRat ≠ 0 := by
    intro h
    have := hy.mantissaBounds hy_mant_ne
    rw [Number.toRat_eq_zero_iff] at h
    exact hy_mant_ne h
  have h_abs_diff_eq : |result.toRat - x.toRat / y.toRat|
      = |(|result.toRat| - |x.toRat / y.toRat|)| := by
    apply abs_diff_eq_abs_sub_abs_of_sign_aligned result (x.toRat / y.toRat)
    · intro h_neg
      have h_div_sign := toRat_div_sign x y hy_toRat_ne
      apply h_div_sign.2
      intro h_eq
      have h_zn_false : (x.negative_ != y.negative_) = false := by simp [h_eq]
      rw [h_zn_false] at h_sign
      exact Bool.noConfusion (h_neg.symm.trans h_sign)
    · intro h_pos
      have h_div_sign := toRat_div_sign x y hy_toRat_ne
      apply h_div_sign.1
      by_contra h_ne
      have h_zn_true : (x.negative_ != y.negative_) = true := by
        simp only [bne_iff_ne, ne_eq]; exact h_ne
      rw [h_zn_true] at h_sign
      exact Bool.noConfusion (h_pos.symm.trans h_sign)
  rw [h_abs_diff_eq, h_result_abs, habs_xy_eq]
  -- Let R = result_abs, A = (zm+f)·10^ze', T = (zm+f+δ)·10^ze'
  -- Goal: |R - T| ≤ T · 5/(2^63+7)
  set R : ℚ := (res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_ with hR_def
  set A : ℚ := ((zm.toNat : ℚ) + f) * 10 ^ ze' with hA_def
  set T : ℚ := ((zm.toNat : ℚ) + f + δ) * 10 ^ ze' with hT_def
  -- Useful facts
  have h10ze_pos : (0 : ℚ) < 10 ^ ze' := zpow_pos (by norm_num) _
  have hzm_q_ge : (mantissaFloor : ℚ) ≤ (zm.toNat : ℚ) := by exact_mod_cast hzm_ge
  have hzmf_pos : (0 : ℚ) < (zm.toNat : ℚ) + f := by linarith [Nat.cast_nonneg (α := ℚ) zm.toNat]
  have hA_pos : (0 : ℚ) < A := by rw [hA_def]; positivity
  have hD_nn : (0 : ℚ) ≤ δ * 10 ^ ze' := mul_nonneg hδ_nn (le_of_lt h10ze_pos)
  have hA_le_T : A ≤ T := by rw [hA_def, hT_def]; nlinarith
  have hT_pos : (0 : ℚ) < T := lt_of_lt_of_le hA_pos hA_le_T
  -- supTight bound: |R - A| ≤ A · 5/(2^63+7)
  have h_supTight := doRoundUp_rounds_to_nearest_supTight g zm ze' f hf_rep hzm_ge hzm_le
    h_floor_constraint "Number::operator_div overflow" res_pos h_rup_pos hres_pos_mant_ne
  have h_bound_eq : ((2 ^ 63 + 7 : ℕ) : ℚ) = (2 ^ 63 + 7 : ℚ) := by push_cast; norm_num
  -- Get the round_correct information
  obtain ⟨hround_pos, hround_neg, hround_zero⟩ := round_correct hf_rep
  -- Case split: R ≥ T or R < T
  by_cases hRT : R ≥ T
  · -- Case 1: R ≥ T. Upper direction only.
    -- R - T ≤ R - A ≤ A · 5/(2^63+7) ≤ T · 5/(2^63+7)
    rw [abs_le]; constructor
    · -- Lower: -(T · 5/(2^63+7)) ≤ R - T. True since R ≥ T and T · 5/(2^63+7) ≥ 0.
      linarith
    · -- Upper: R - T ≤ T · 5/(2^63+7)
      have hR_sub_A := (abs_le.mp h_supTight).2
      rw [h_bound_eq] at hR_sub_A
      -- hR_sub_A : R - A ≤ A · (5/(2^63+7))
      have h1 : R - T ≤ R - A := by linarith [hA_le_T]
      have h2 : A * (5 / ((2 : ℚ) ^ 63 + 7)) ≤ T * (6 / ((2 : ℚ) ^ 63 + 18)) := by
        apply le_trans (mul_le_mul_of_nonneg_right hA_le_T (by positivity))
        apply mul_le_mul_of_nonneg_left _ (le_of_lt hT_pos)
        norm_num
      linarith
  · -- Case 2: R < T. Lower direction only.
    push_neg at hRT
    -- Key claim: doRoundUp did NOT round up.
    -- If round-up fired (non-cusp): R = (zm+1)·10^ze > T since f+δ < 1. Contradiction.
    -- If round-up fired (cusp, zm = maxRep): R = maxRepCuspTarget·10^ze > (maxRep+f+δ)·10^ze = T.
    -- So no round-up. R = zm·10^ze.
    have h_no_roundUp : ¬ (g.round .to_nearest = 1 ∨ (g.round .to_nearest = 0 ∧ zm % 2 = 1)) := by
      intro h_ru
      -- If round-up fires, show R ≥ T, contradicting hRT.
      by_cases h_cusp : zm.toNat = maxRep.toNat
      · -- Cusp case: R = maxRepCuspTarget · 10^ze
        have h_zm_eq : zm = maxRep := UInt64.toNat_inj.mp h_cusp
        have h_val := doRoundUp_value_cusp g zm ze' h_zm_eq h_ru "Number::operator_div overflow" res_pos h_rup_pos hres_pos_mant_ne
        simp only at h_val
        have hzm_eq : (zm.toNat : ℚ) = maxRepNat := by rw [h_cusp, maxRep_val]; norm_num
        -- R = maxRepCuspTarget · 10^ze', T = (maxRepNat + f + δ) · 10^ze'
        -- Since f+δ < 1, T < 9223372036854775808 · 10^ze' < R. Contradicts R < T.
        have hR_eq : R = (maxRepCuspTarget : ℚ) * 10 ^ ze' := by rw [hR_def]; exact h_val
        have hT_eq : T = ((maxRepNat : ℚ) + f + δ) * 10 ^ ze' := by rw [hT_def, hzm_eq]
        have hR_ge_T : R ≥ T := by
          rw [hR_eq, hT_eq]
          apply mul_le_mul_of_nonneg_right _ (le_of_lt h10ze_pos)
          nlinarith
        linarith
      · -- Non-cusp: R = (zm+1) · 10^ze
        have h_no_ovf : zm.toNat + 1 ≤ maxRep.toNat := by omega
        have h_val := doRoundUp_value_roundUp_noOverflow g zm ze' h_ru h_no_ovf "Number::operator_div overflow" res_pos h_rup_pos hres_pos_mant_ne
        simp only at h_val
        rw [hR_def] at hRT; rw [hT_def] at hRT
        rw [h_val] at hRT
        have : (((zm.toNat : ℚ) + 1) - ((zm.toNat : ℚ) + f + δ)) * 10 ^ ze' > 0 := by
          apply mul_pos _ h10ze_pos; nlinarith
        linarith
    -- From no-round-up: R = zm · 10^ze
    have h_val := doRoundUp_value_noRoundUp g zm ze' h_no_roundUp "Number::operator_div overflow" res_pos h_rup_pos hres_pos_mant_ne
    simp only at h_val
    -- Key: zm ≥ floor + 1 = mantissaFloorSucc.
    have hzm_gt_floor : (mantissaFloorSucc : ℕ) ≤ zm.toNat := by
      by_contra h_eq
      push_neg at h_eq
      have hzm_eq_floor : zm.toNat = mantissaFloor := by omega
      have hf_ge_8_10 : (8 : ℚ) / 10 ≤ f := h_floor_constraint hzm_eq_floor
      push_neg at h_no_roundUp
      obtain ⟨h_not_1, _⟩ := h_no_roundUp
      have : f > 1 / 2 := by linarith
      exact h_not_1 (hround_pos.mpr this)
    have hzm_q_gt : (mantissaFloorSucc : ℚ) ≤ (zm.toNat : ℚ) := by exact_mod_cast hzm_gt_floor
    have hfd_nn : (0 : ℚ) ≤ f + δ := by linarith
    have h_denom_pos : (0 : ℚ) < (2 : ℚ) ^ 63 + 18 := by positivity
    -- Sub-split on δ = 0 vs δ > 0
    by_cases hδ_zero : δ = 0
    · -- δ = 0: T = A. Use supTight directly.
      have hTA : T = A := by rw [hT_def, hA_def, hδ_zero]; ring
      rw [abs_le]; constructor
      · -- Lower: -(T · 6/(2^63+7)) ≤ R - T. Since T = A, use supTight lower bound.
        have hR_sub_A := (abs_le.mp h_supTight).1
        rw [h_bound_eq] at hR_sub_A
        have h_5_le_6 : A * (5 / ((2 : ℚ) ^ 63 + 7)) ≤ A * (6 / ((2 : ℚ) ^ 63 + 18)) := by
          apply mul_le_mul_of_nonneg_left _ (le_of_lt hA_pos); norm_num
        rw [hTA]; linarith
      · -- Upper: R - T ≤ T · 6/(2^63+18). Since R < T, trivial.
        have : R - T < 0 := by linarith
        have : (0 : ℚ) ≤ T * (6 / ((2 : ℚ) ^ 63 + 18)) := by positivity
        linarith
    · -- δ > 0: Show (f+δ)(2^63+2) ≤ 5·zm, then apply div_case2_ineq.
      have hδ_pos : δ > 0 := lt_of_le_of_ne hδ_nn (Ne.symm hδ_zero)
      -- From no-round-up: f ≤ 1/2 (otherwise g.round = 1 would have fired).
      have hf_le_half : f ≤ 1 / 2 := by
        by_contra h; push_neg at h
        exact h_no_roundUp (Or.inl (hround_pos.mpr h))
      -- Key algebraic claim: (f+δ)(2^63+12) ≤ 6·zm.
      -- Case split on f < 1/2 vs f = 1/2.
      -- Sub-case f < 1/2: h_fδ_half gives f+δ ≤ 1/2. (1/2)(2^63+12) ≤ 6*mantissaFloorSucc. ✓
      -- Sub-case f = 1/2: no-round-up + g.round=0 → zm even → zm ≥ 922337203685477582.
      --   f+δ < 3/5 strictly (δ < 1/10). (3/5)(2^63+12) = 6*922337203685477582 ≤ 6*zm. ✓
      have h_tight : (f + δ) * ((2 : ℚ) ^ 63 + 12) ≤ 6 * (zm.toNat : ℚ) := by
        by_cases hf_strict : f < 1 / 2
        · -- f < 1/2: from h_fδ_half, f+δ < 1/2
          have hfδ_lt_half : f + δ < 1 / 2 := h_fδ_half hf_strict
          have h_bound : (1 : ℚ) / 2 * ((2 : ℚ) ^ 63 + 12) ≤ 6 * mantissaFloorSucc := by norm_num
          nlinarith [mul_lt_mul_of_pos_right hfδ_lt_half (show (0:ℚ) < (2:ℚ)^63+12 from by norm_num)]
        · -- f = 1/2
          push_neg at hf_strict
          have hf_eq : f = 1 / 2 := le_antisymm hf_le_half hf_strict
          -- g.round = 0 since f = 1/2
          have h_round_0 : g.round .to_nearest = 0 := hround_zero.mpr hf_eq
          -- no-round-up → ¬(zm % 2 = 1) in UInt64
          have h_zm_odd_false : ¬ (zm % 2 = 1) := by
            intro h_odd
            exact h_no_roundUp (Or.inr ⟨h_round_0, h_odd⟩)
          -- zm.toNat is even
          have h_zm_nat_even : zm.toNat % 2 ≠ 1 := by
            intro h
            apply h_zm_odd_false
            apply UInt64.toNat_inj.mp
            have hmod_nat : (zm % 2).toNat = zm.toNat % (2 : UInt64).toNat := UInt64.toNat_mod zm 2
            have h2_nat : (2 : UInt64).toNat = 2 := rfl
            have h1_nat : (1 : UInt64).toNat = 1 := rfl
            rw [hmod_nat, h2_nat, h1_nat, h]
          -- zm.toNat ≥ mantissaFloorSucc (odd) and even → zm.toNat ≥ 922337203685477582
          have hzm_ge_floor2 : (922337203685477582 : ℕ) ≤ zm.toNat := by omega
          have hzm_q_ge2 : (922337203685477582 : ℚ) ≤ (zm.toNat : ℚ) := by exact_mod_cast hzm_ge_floor2
          -- f+δ < 3/5 strictly
          have h_sum : f + δ < 3 / 5 := by linarith
          have h_bound : (3 : ℚ) / 5 * ((2 : ℚ) ^ 63 + 12) = 6 * 922337203685477582 := by norm_num
          nlinarith [mul_lt_mul_of_pos_right h_sum (show (0:ℚ) < (2:ℚ)^63+12 from by norm_num)]
      have h_ineq := div_case2_ineq (zm.toNat : ℚ) f δ h_tight
      rw [abs_le]; constructor
      · -- Lower: -(T · 6/(2^63+18)) ≤ R - T.
        rw [hR_def, h_val, hT_def]
        have h_key : (f + δ) ≤ ((zm.toNat : ℚ) + f + δ) * (6 / ((2 : ℚ) ^ 63 + 18)) := by
          rw [show ((zm.toNat : ℚ) + f + δ) * (6 / ((2 : ℚ) ^ 63 + 18))
                = 6 * ((zm.toNat : ℚ) + f + δ) / ((2 : ℚ) ^ 63 + 18) from by ring]
          rw [le_div_iff₀ h_denom_pos]
          linarith [h_ineq]
        nlinarith
      · -- Upper: R - T ≤ T · 6/(2^63+18). Since R < T, trivial.
        have : R - T < 0 := by linarith
        have : (0 : ℚ) ≤ T * (6 / ((2 : ℚ) ^ 63 + 18)) := by positivity
        linarith

end XRPL.Model.Protocol
