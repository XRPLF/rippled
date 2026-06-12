import XRPL.Properties.Protocol.Number.Common.Notation
import Mathlib.Tactic

import XRPL.Common.Approx
import XRPL.Properties.Protocol.Number.Common.Helpers
import XRPL.Properties.Protocol.Number.Div.ToNearest.AlgorithmicFacts

set_option linter.style.longLine false
set_option linter.style.emptyLine false

namespace XRPL.Model.Protocol

/-! # Division rounding bounds for directed modes

All three directed modes use **magnitude-only** bounds (`|result - truth| ≤ |truth| · ε`).
The directional property (`result ≤ truth` for downward, `truth ≤ result` for upward)
can fail for division due to the invisible division remainder `δ`: when the guard
is zero but `δ > 0`, the algorithm truncates without knowing the exact truth.

The proof structure mirrors `.to_nearest`: reduce via `abs_diff_eq_abs_sub_abs_of_sign_aligned`
to `|(R - T)| ≤ T · ε` where `R = |result|` and `T = |truth|`, then case-split on `R ≥ T`
vs `R < T`. The bound `10/(2^63+2)` works because the maximum error is at most 1 ULP
(for round-up) or `f+δ < 1` (for truncation), and 1 ULP relative to `zm ≥ floor` gives
`1/floor ≈ 10/(2^63+2)`.

The bound `10/(2^63+2)` works because `f + δ < 1` (strict inequality from
`operator_div_algorithmic_facts_*`) gives `(f+δ)(2^63-8) < 2^63-8 = 10·floor ≤ 10·zm`,
and hence `(f+δ)(2^63+2) < 10·(zm+f+δ)`. For round-up at `zm = floor`, the floor
constraint gives `f ≥ 0.8` so `1-f-δ < 0.2 < 10/floor`. For the cusp case
`zm = maxRep`, error `= (3-f-δ)/maxRep < 3/maxRep ≪ 10/(2^63+2)`. -/

-- Shared helper: reduce |R-T| ≤ T·ε for division directed modes using R vs T case split.
-- R = doRoundUp output magnitude, T = (zm+f+δ)·10^ze' = |truth|.
-- Truncation gives R = zm·10^ze', round-up gives (zm+1)·10^ze' or cusp maxRepCuspTarget·10^ze'.
-- In all cases |R-T| ≤ max(f+δ, 1-f-δ, 3-f-δ)·10^ze' ≤ T · 10/(2^63+2).
set_option linter.unusedVariables false in
lemma div_directed_magnitude_bound
    (zm : UInt64) (ze' : Int) (f δ : ℚ) (R T : ℚ)
    (hzm_ge : mantissaFloor ≤ zm.toNat)
    (hf_nn : 0 ≤ f) (hf_lt1 : f < 1) (hδ_nn : 0 ≤ δ) (hfδ_lt : f + δ < 1)
    (hT_def : T = ((zm.toNat : ℚ) + f + δ) * 10 ^ ze')
    (hR_cases : R = (zm.toNat : ℚ) * 10 ^ ze' ∨
                (R = ((zm.toNat : ℚ) + 1) * 10 ^ ze' ∧ (zm.toNat = mantissaFloor → (8 : ℚ) / 10 ≤ f)) ∨
                (zm = maxRep ∧ R = (maxRepCuspTarget : ℚ) * 10 ^ ze')) :
    |R - T| ≤ T * (10 / ((2 : ℚ) ^ 63 + 2)) := by
  have h10ze'_pos : (0 : ℚ) < 10 ^ ze' := zpow_pos (by norm_num) _
  have h10ze'_nn : (0 : ℚ) ≤ 10 ^ ze' := le_of_lt h10ze'_pos
  have hzm_q_ge : (mantissaFloor : ℚ) ≤ (zm.toNat : ℚ) := by exact_mod_cast hzm_ge
  have hfd_nn : (0 : ℚ) ≤ f + δ := by linarith
  have hT_pos : (0 : ℚ) < T := by rw [hT_def]; positivity
  have h_denom_val : ((2 ^ 63 + 2 : ℚ)) = maxRepCuspTarget := by norm_num
  -- f+δ < 1 (strict) gives (f+δ)(2^63-8) < 2^63-8 = 10·floor ≤ 10·zm.
  -- Hence (f+δ)(2^63+2) = (f+δ)(2^63-8) + (f+δ)·10 < 10·zm + 10·(f+δ) = 10·(zm+f+δ).
  have h_fδ_bound : (f + δ) < ((zm.toNat : ℚ) + f + δ) * (10 / ((2 : ℚ) ^ 63 + 2)) := by
    rw [h_denom_val]
    rw [show ((zm.toNat : ℚ) + f + δ) * (10 / (maxRepCuspTarget : ℚ))
          = 10 * ((zm.toNat : ℚ) + f + δ) / maxRepCuspTarget by ring]
    rw [lt_div_iff₀ (by norm_num : (0 : ℚ) < maxRepCuspTarget)]
    -- Need: (f+δ)·(2^63+2) < 10·(zm+f+δ), i.e., (f+δ)(2^63-8) < 10·zm.
    -- Since f+δ < 1: (f+δ)(2^63-8) < 2^63-8 = 10·mantissaFloor ≤ 10·zm.
    have h10floor : (10 : ℚ) * mantissaFloor = (2 : ℚ) ^ 63 - 8 := by norm_num
    nlinarith [hzm_q_ge, hf_nn, hδ_nn, hfδ_lt]
  rcases hR_cases with hR_trunc | ⟨hR_roundup, hR_floor_pos⟩ | ⟨hR_cusp_zm, hR_cusp⟩
  · -- Truncation: R = zm·10^ze'. |R - T| = (f+δ)·10^ze'. Strict ineq suffices.
    rw [hR_trunc, hT_def]
    have h_eq : (zm.toNat : ℚ) * 10 ^ ze' - ((zm.toNat : ℚ) + f + δ) * 10 ^ ze'
        = -(f + δ) * 10 ^ ze' := by ring
    rw [h_eq, show -(f + δ) * 10 ^ ze' = -((f + δ) * 10 ^ ze') from by ring,
        abs_neg, abs_of_nonneg (by nlinarith [h10ze'_nn, hfd_nn])]
    have h_strict : (f + δ) * 10 ^ ze'
        < ((zm.toNat : ℚ) + f + δ) * 10 ^ ze' * (10 / ((2 : ℚ) ^ 63 + 2)) := by
      calc (f + δ) * 10 ^ ze'
          < ((zm.toNat : ℚ) + f + δ) * (10 / ((2 : ℚ) ^ 63 + 2)) * 10 ^ ze' :=
            mul_lt_mul_of_pos_right h_fδ_bound h10ze'_pos
        _ = ((zm.toNat : ℚ) + f + δ) * 10 ^ ze' * (10 / ((2 : ℚ) ^ 63 + 2)) := by ring
    linarith
  · -- Round-up non-cusp: R = (zm+1)·10^ze'. |R - T| = (1-f-δ)·10^ze'.
    rw [hR_roundup, hT_def]
    have h_eq : ((zm.toNat : ℚ) + 1) * 10 ^ ze' - ((zm.toNat : ℚ) + f + δ) * 10 ^ ze'
        = (1 - f - δ) * 10 ^ ze' := by ring
    rw [h_eq, abs_of_nonneg (by nlinarith [h10ze'_nn, hfδ_lt])]
    -- Need: (1-f-δ)*(2^63+2) ≤ 10*(zm+f+δ). Split on zm = floor vs zm > floor.
    by_cases hzm_eq_floor : zm.toNat = mantissaFloor
    · -- zm = floor. Floor constraint: f ≥ 8/10, giving (1-f-δ) ≤ 1/5.
      have hf_ge : (8 : ℚ) / 10 ≤ f := hR_floor_pos hzm_eq_floor
      have hzm_eq : (zm.toNat : ℚ) = mantissaFloor := by exact_mod_cast hzm_eq_floor
      have h_1mfδ_bound : (1 - f - δ) ≤ ((zm.toNat : ℚ) + f + δ) * (10 / ((2 : ℚ) ^ 63 + 2)) := by
        rw [hzm_eq, h_denom_val]
        rw [show ((mantissaFloor : ℚ) + f + δ) * (10 / (maxRepCuspTarget : ℚ))
              = 10 * ((mantissaFloor : ℚ) + f + δ) / maxRepCuspTarget from by ring]
        rw [le_div_iff₀ (by norm_num : (0 : ℚ) < maxRepCuspTarget)]
        nlinarith [hf_ge, hδ_nn, hfδ_lt]
      calc (1 - f - δ) * 10 ^ ze'
          ≤ (((zm.toNat : ℚ) + f + δ) * (10 / ((2 : ℚ) ^ 63 + 2))) * 10 ^ ze' :=
            mul_le_mul_of_nonneg_right h_1mfδ_bound h10ze'_nn
        _ = ((zm.toNat : ℚ) + f + δ) * 10 ^ ze' * (10 / ((2 : ℚ) ^ 63 + 2)) := by ring
    · -- zm > floor, so zm ≥ floor+1. 10*zm ≥ 2^63+2 ≥ (1-f-δ)*(2^63+2).
      have hzm_ge_succ : (mantissaFloorSucc : ℚ) ≤ (zm.toNat : ℚ) := by
        have : mantissaFloorSucc ≤ zm.toNat := by omega
        exact_mod_cast this
      have h_1mfδ_bound : (1 - f - δ) ≤ ((zm.toNat : ℚ) + f + δ) * (10 / ((2 : ℚ) ^ 63 + 2)) := by
        rw [h_denom_val]
        rw [show ((zm.toNat : ℚ) + f + δ) * (10 / (maxRepCuspTarget : ℚ))
              = 10 * ((zm.toNat : ℚ) + f + δ) / maxRepCuspTarget from by ring]
        rw [le_div_iff₀ (by norm_num : (0 : ℚ) < maxRepCuspTarget)]
        have h_key : (10 : ℚ) * mantissaFloorSucc = maxRepCuspTarget := by norm_num
        nlinarith [hf_nn, hδ_nn, hf_lt1, hfδ_lt]
      calc (1 - f - δ) * 10 ^ ze'
          ≤ (((zm.toNat : ℚ) + f + δ) * (10 / ((2 : ℚ) ^ 63 + 2))) * 10 ^ ze' :=
            mul_le_mul_of_nonneg_right h_1mfδ_bound h10ze'_nn
        _ = ((zm.toNat : ℚ) + f + δ) * 10 ^ ze' * (10 / ((2 : ℚ) ^ 63 + 2)) := by ring
  · -- Cusp: zm = maxRep, R = maxRepCuspTarget·10^ze'. |R - T| = (3-f-δ)·10^ze'.
    rw [hR_cusp, hT_def]
    have hzm_eq : (zm.toNat : ℚ) = maxRepNat := by
      rw [show zm.toNat = maxRep.toNat from by rw [hR_cusp_zm], maxRep_val]; norm_num
    rw [hzm_eq]
    have h_eq : (maxRepCuspTarget : ℚ) * 10 ^ ze' -
        ((maxRepNat : ℚ) + f + δ) * 10 ^ ze'
        = (3 - f - δ) * 10 ^ ze' := by ring
    rw [h_eq, abs_of_nonneg (by nlinarith [h10ze'_nn, hfδ_lt])]
    have h_3mfδ_bound : (3 - f - δ) ≤ (maxRepNat + f + δ) * (10 / ((2 : ℚ) ^ 63 + 2)) := by
      rw [h_denom_val]
      rw [show (maxRepNat + f + δ) * (10 / (maxRepCuspTarget : ℚ))
            = 10 * (maxRepNat + f + δ) / maxRepCuspTarget by ring]
      rw [le_div_iff₀ (by norm_num : (0 : ℚ) < maxRepCuspTarget)]
      nlinarith [hf_nn, hδ_nn, hf_lt1, hfδ_lt]
    calc (3 - f - δ) * 10 ^ ze'
        ≤ (maxRepNat + f + δ) * (10 / ((2 : ℚ) ^ 63 + 2)) * 10 ^ ze' :=
          mul_le_mul_of_nonneg_right h_3mfδ_bound h10ze'_nn
      _ = ((maxRepNat : ℚ) + f + δ) * 10 ^ ze' * (10 / ((2 : ℚ) ^ 63 + 2)) := by ring

-- Helper: classify doRoundUp output into truncation, round-up, or cusp.
-- The round-up case additionally carries `zm.toNat = floor → 0 < f + δ`
-- (needed by `div_directed_magnitude_bound` to close the floor case).
-- For directed modes, shouldRoundUp requires the guard to be nonzero,
-- which means f > 0, giving the needed constraint.
lemma doRoundUp_value_trichotomy_downward
    (g : Guard) (zm : UInt64) (ze' : Int) (f δ : ℚ)
    (_hf_nn : 0 ≤ f) (_hδ_nn : 0 ≤ δ)
    (h_floor : zm.toNat = mantissaFloor → (8 : ℚ) / 10 ≤ f)
    (loc : String) (res_pos : RoundResult)
    (hok_pos : g.doRoundUp false zm ze' largeRange.min largeRange.max .downward loc = .ok res_pos)
    (hres_pos_mant_ne : res_pos.mantissa_ ≠ 0)
    (hzm_le : zm.toNat ≤ maxRep.toNat) :
    (res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_ = (zm.toNat : ℚ) * 10 ^ ze' ∨
    ((res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_ = ((zm.toNat : ℚ) + 1) * 10 ^ ze' ∧
     (zm.toNat = mantissaFloor → (8 : ℚ) / 10 ≤ f)) ∨
    (zm = maxRep ∧ (res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_ = (maxRepCuspTarget : ℚ) * 10 ^ ze') := by
  by_cases h_sru : g.shouldRoundUp_downward
  · by_cases h_cusp : zm = maxRep
    · right; right
      exact ⟨h_cusp, doRoundUp_value_downward_roundUp_cusp g false zm ze' h_cusp h_sru loc res_pos hok_pos hres_pos_mant_ne⟩
    · right; left
      have h_no_cusp : zm.toNat + 1 ≤ maxRep.toNat := by
        have : zm.toNat ≠ maxRep.toNat := fun heq => h_cusp (UInt64.toNat_inj.mp heq)
        omega
      exact ⟨doRoundUp_value_downward_roundUp_noCusp g false zm ze' h_sru h_no_cusp loc res_pos hok_pos hres_pos_mant_ne, h_floor⟩
  · left
    exact doRoundUp_value_downward_truncate g false zm ze' h_sru loc res_pos hok_pos hres_pos_mant_ne

lemma doRoundUp_value_trichotomy_upward
    (g : Guard) (zm : UInt64) (ze' : Int) (f δ : ℚ)
    (_hf_nn : 0 ≤ f) (_hδ_nn : 0 ≤ δ)
    (h_floor : zm.toNat = mantissaFloor → (8 : ℚ) / 10 ≤ f)
    (loc : String) (res_pos : RoundResult)
    (hok_pos : g.doRoundUp false zm ze' largeRange.min largeRange.max .upward loc = .ok res_pos)
    (hres_pos_mant_ne : res_pos.mantissa_ ≠ 0)
    (hzm_le : zm.toNat ≤ maxRep.toNat) :
    (res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_ = (zm.toNat : ℚ) * 10 ^ ze' ∨
    ((res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_ = ((zm.toNat : ℚ) + 1) * 10 ^ ze' ∧
     (zm.toNat = mantissaFloor → (8 : ℚ) / 10 ≤ f)) ∨
    (zm = maxRep ∧ (res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_ = (maxRepCuspTarget : ℚ) * 10 ^ ze') := by
  by_cases h_sru : g.shouldRoundUp_upward
  · by_cases h_cusp : zm = maxRep
    · right; right
      exact ⟨h_cusp, doRoundUp_value_upward_roundUp_cusp g false zm ze' h_cusp h_sru loc res_pos hok_pos hres_pos_mant_ne⟩
    · right; left
      have h_no_cusp : zm.toNat + 1 ≤ maxRep.toNat := by
        have : zm.toNat ≠ maxRep.toNat := fun heq => h_cusp (UInt64.toNat_inj.mp heq)
        omega
      exact ⟨doRoundUp_value_upward_roundUp_noCusp g false zm ze' h_sru h_no_cusp loc res_pos hok_pos hres_pos_mant_ne, h_floor⟩
  · left
    exact doRoundUp_value_upward_truncate g false zm ze' h_sru loc res_pos hok_pos hres_pos_mant_ne

-- Strict magnitude bound for `.upward` division. The round-up non-cusp case
-- additionally carries `0 < f` (round-up fires only when the guard is nonzero),
-- which is exactly what turns the `(1-f-δ)` bound strict at `zm ≥ mantissaFloorSucc`.
set_option linter.unusedVariables false in
lemma div_directed_magnitude_bound_strict
    (zm : UInt64) (ze' : Int) (f δ : ℚ) (R T : ℚ)
    (hzm_ge : mantissaFloor ≤ zm.toNat)
    (hf_nn : 0 ≤ f) (hf_lt1 : f < 1) (hδ_nn : 0 ≤ δ) (hfδ_lt : f + δ < 1)
    (hT_def : T = ((zm.toNat : ℚ) + f + δ) * 10 ^ ze')
    (hR_cases : R = (zm.toNat : ℚ) * 10 ^ ze' ∨
                (R = ((zm.toNat : ℚ) + 1) * 10 ^ ze' ∧
                 (zm.toNat = mantissaFloor → (8 : ℚ) / 10 ≤ f) ∧ 0 < f) ∨
                (zm = maxRep ∧ R = (maxRepCuspTarget : ℚ) * 10 ^ ze')) :
    |R - T| < T * (10 / ((2 : ℚ) ^ 63 + 2)) := by
  have h10ze'_pos : (0 : ℚ) < 10 ^ ze' := zpow_pos (by norm_num) _
  have h10ze'_nn : (0 : ℚ) ≤ 10 ^ ze' := le_of_lt h10ze'_pos
  have hzm_q_ge : (mantissaFloor : ℚ) ≤ (zm.toNat : ℚ) := by exact_mod_cast hzm_ge
  have hfd_nn : (0 : ℚ) ≤ f + δ := by linarith
  have hT_pos : (0 : ℚ) < T := by rw [hT_def]; positivity
  have h_denom_val : ((2 ^ 63 + 2 : ℚ)) = maxRepCuspTarget := by norm_num
  have h_fδ_bound : (f + δ) < ((zm.toNat : ℚ) + f + δ) * (10 / ((2 : ℚ) ^ 63 + 2)) := by
    rw [h_denom_val]
    rw [show ((zm.toNat : ℚ) + f + δ) * (10 / (maxRepCuspTarget : ℚ))
          = 10 * ((zm.toNat : ℚ) + f + δ) / maxRepCuspTarget by ring]
    rw [lt_div_iff₀ (by norm_num : (0 : ℚ) < maxRepCuspTarget)]
    have h10floor : (10 : ℚ) * mantissaFloor = (2 : ℚ) ^ 63 - 8 := by norm_num
    nlinarith [hzm_q_ge, hf_nn, hδ_nn, hfδ_lt]
  rcases hR_cases with hR_trunc | ⟨hR_roundup, hR_floor_pos, hf_pos⟩ | ⟨hR_cusp_zm, hR_cusp⟩
  · -- Truncation: |R - T| = (f+δ)·10^ze' < T·ε (strict).
    rw [hR_trunc, hT_def]
    have h_eq : (zm.toNat : ℚ) * 10 ^ ze' - ((zm.toNat : ℚ) + f + δ) * 10 ^ ze'
        = -(f + δ) * 10 ^ ze' := by ring
    rw [h_eq, show -(f + δ) * 10 ^ ze' = -((f + δ) * 10 ^ ze') from by ring,
        abs_neg, abs_of_nonneg (by nlinarith [h10ze'_nn, hfd_nn])]
    calc (f + δ) * 10 ^ ze'
        < ((zm.toNat : ℚ) + f + δ) * (10 / ((2 : ℚ) ^ 63 + 2)) * 10 ^ ze' :=
          mul_lt_mul_of_pos_right h_fδ_bound h10ze'_pos
      _ = ((zm.toNat : ℚ) + f + δ) * 10 ^ ze' * (10 / ((2 : ℚ) ^ 63 + 2)) := by ring
  · -- Round-up non-cusp: |R - T| = (1-f-δ)·10^ze'.
    rw [hR_roundup, hT_def]
    have h_eq : ((zm.toNat : ℚ) + 1) * 10 ^ ze' - ((zm.toNat : ℚ) + f + δ) * 10 ^ ze'
        = (1 - f - δ) * 10 ^ ze' := by ring
    rw [h_eq, abs_of_nonneg (by nlinarith [h10ze'_nn, hfδ_lt])]
    by_cases hzm_eq_floor : zm.toNat = mantissaFloor
    · have hf_ge : (8 : ℚ) / 10 ≤ f := hR_floor_pos hzm_eq_floor
      have hzm_eq : (zm.toNat : ℚ) = mantissaFloor := by exact_mod_cast hzm_eq_floor
      have h_1mfδ_bound : (1 - f - δ) < ((zm.toNat : ℚ) + f + δ) * (10 / ((2 : ℚ) ^ 63 + 2)) := by
        rw [hzm_eq, h_denom_val]
        rw [show ((mantissaFloor : ℚ) + f + δ) * (10 / (maxRepCuspTarget : ℚ))
              = 10 * ((mantissaFloor : ℚ) + f + δ) / maxRepCuspTarget from by ring]
        rw [lt_div_iff₀ (by norm_num : (0 : ℚ) < maxRepCuspTarget)]
        nlinarith [hf_ge, hδ_nn, hfδ_lt]
      calc (1 - f - δ) * 10 ^ ze'
          < (((zm.toNat : ℚ) + f + δ) * (10 / ((2 : ℚ) ^ 63 + 2))) * 10 ^ ze' :=
            mul_lt_mul_of_pos_right h_1mfδ_bound h10ze'_pos
        _ = ((zm.toNat : ℚ) + f + δ) * 10 ^ ze' * (10 / ((2 : ℚ) ^ 63 + 2)) := by ring
    · have hzm_ge_succ : (mantissaFloorSucc : ℚ) ≤ (zm.toNat : ℚ) := by
        have : mantissaFloorSucc ≤ zm.toNat := by omega
        exact_mod_cast this
      have h_1mfδ_bound : (1 - f - δ) < ((zm.toNat : ℚ) + f + δ) * (10 / ((2 : ℚ) ^ 63 + 2)) := by
        rw [h_denom_val]
        rw [show ((zm.toNat : ℚ) + f + δ) * (10 / (maxRepCuspTarget : ℚ))
              = 10 * ((zm.toNat : ℚ) + f + δ) / maxRepCuspTarget from by ring]
        rw [lt_div_iff₀ (by norm_num : (0 : ℚ) < maxRepCuspTarget)]
        have h_key : (10 : ℚ) * mantissaFloorSucc = maxRepCuspTarget := by norm_num
        nlinarith [hf_nn, hδ_nn, hf_lt1, hfδ_lt, hf_pos, hzm_ge_succ]
      calc (1 - f - δ) * 10 ^ ze'
          < (((zm.toNat : ℚ) + f + δ) * (10 / ((2 : ℚ) ^ 63 + 2))) * 10 ^ ze' :=
            mul_lt_mul_of_pos_right h_1mfδ_bound h10ze'_pos
        _ = ((zm.toNat : ℚ) + f + δ) * 10 ^ ze' * (10 / ((2 : ℚ) ^ 63 + 2)) := by ring
  · -- Cusp: |R - T| = (3-f-δ)·10^ze' < T·ε (strict; 10·maxRepNat ≫ 3).
    rw [hR_cusp, hT_def]
    have hzm_eq : (zm.toNat : ℚ) = maxRepNat := by
      rw [show zm.toNat = maxRep.toNat from by rw [hR_cusp_zm], maxRep_val]; norm_num
    rw [hzm_eq]
    have h_eq : (maxRepCuspTarget : ℚ) * 10 ^ ze' -
        ((maxRepNat : ℚ) + f + δ) * 10 ^ ze'
        = (3 - f - δ) * 10 ^ ze' := by ring
    rw [h_eq, abs_of_nonneg (by nlinarith [h10ze'_nn, hfδ_lt])]
    have h_3mfδ_bound : (3 - f - δ) < (maxRepNat + f + δ) * (10 / ((2 : ℚ) ^ 63 + 2)) := by
      rw [h_denom_val]
      rw [show (maxRepNat + f + δ) * (10 / (maxRepCuspTarget : ℚ))
            = 10 * (maxRepNat + f + δ) / maxRepCuspTarget by ring]
      rw [lt_div_iff₀ (by norm_num : (0 : ℚ) < maxRepCuspTarget)]
      nlinarith [hf_nn, hδ_nn, hf_lt1, hfδ_lt]
    calc (3 - f - δ) * 10 ^ ze'
        < (maxRepNat + f + δ) * (10 / ((2 : ℚ) ^ 63 + 2)) * 10 ^ ze' :=
          mul_lt_mul_of_pos_right h_3mfδ_bound h10ze'_pos
      _ = ((maxRepNat : ℚ) + f + δ) * 10 ^ ze' * (10 / ((2 : ℚ) ^ 63 + 2)) := by ring

-- Strict trichotomy: the round-up non-cusp case carries `0 < f`, derived from
-- `shouldRoundUp_upward` (the guard is nonzero) via `represents g f`.
lemma doRoundUp_value_trichotomy_upward_strict
    (g : Guard) (zm : UInt64) (ze' : Int) (f δ : ℚ)
    (hf_rep : represents g f)
    (_hf_nn : 0 ≤ f) (_hδ_nn : 0 ≤ δ)
    (h_floor : zm.toNat = mantissaFloor → (8 : ℚ) / 10 ≤ f)
    (loc : String) (res_pos : RoundResult)
    (hok_pos : g.doRoundUp false zm ze' largeRange.min largeRange.max .upward loc = .ok res_pos)
    (hres_pos_mant_ne : res_pos.mantissa_ ≠ 0)
    (hzm_le : zm.toNat ≤ maxRep.toNat) :
    (res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_ = (zm.toNat : ℚ) * 10 ^ ze' ∨
    ((res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_ = ((zm.toNat : ℚ) + 1) * 10 ^ ze' ∧
     (zm.toNat = mantissaFloor → (8 : ℚ) / 10 ≤ f) ∧ 0 < f) ∨
    (zm = maxRep ∧ (res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_ = (maxRepCuspTarget : ℚ) * 10 ^ ze') := by
  by_cases h_sru : g.shouldRoundUp_upward
  · by_cases h_cusp : zm = maxRep
    · right; right
      exact ⟨h_cusp, doRoundUp_value_upward_roundUp_cusp g false zm ze' h_cusp h_sru loc res_pos hok_pos hres_pos_mant_ne⟩
    · right; left
      have h_no_cusp : zm.toNat + 1 ≤ maxRep.toNat := by
        have : zm.toNat ≠ maxRep.toNat := fun heq => h_cusp (UInt64.toNat_inj.mp heq)
        omega
      have hf_pos : 0 < f := represents_pos_of_shouldRoundUp_upward g f hf_rep h_sru
      exact ⟨doRoundUp_value_upward_roundUp_noCusp g false zm ze' h_sru h_no_cusp loc res_pos hok_pos hres_pos_mant_ne, h_floor, hf_pos⟩
  · left
    exact doRoundUp_value_upward_truncate g false zm ze' h_sru loc res_pos hok_pos hres_pos_mant_ne

-- Strict trichotomy: the round-up non-cusp case carries `0 < f`, derived from
-- `shouldRoundUp_downward` (the guard is nonzero) via `represents g f`.
lemma doRoundUp_value_trichotomy_downward_strict
    (g : Guard) (zm : UInt64) (ze' : Int) (f δ : ℚ)
    (hf_rep : represents g f)
    (_hf_nn : 0 ≤ f) (_hδ_nn : 0 ≤ δ)
    (h_floor : zm.toNat = mantissaFloor → (8 : ℚ) / 10 ≤ f)
    (loc : String) (res_pos : RoundResult)
    (hok_pos : g.doRoundUp false zm ze' largeRange.min largeRange.max .downward loc = .ok res_pos)
    (hres_pos_mant_ne : res_pos.mantissa_ ≠ 0)
    (hzm_le : zm.toNat ≤ maxRep.toNat) :
    (res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_ = (zm.toNat : ℚ) * 10 ^ ze' ∨
    ((res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_ = ((zm.toNat : ℚ) + 1) * 10 ^ ze' ∧
     (zm.toNat = mantissaFloor → (8 : ℚ) / 10 ≤ f) ∧ 0 < f) ∨
    (zm = maxRep ∧ (res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_ = (maxRepCuspTarget : ℚ) * 10 ^ ze') := by
  by_cases h_sru : g.shouldRoundUp_downward
  · by_cases h_cusp : zm = maxRep
    · right; right
      exact ⟨h_cusp, doRoundUp_value_downward_roundUp_cusp g false zm ze' h_cusp h_sru loc res_pos hok_pos hres_pos_mant_ne⟩
    · right; left
      have h_no_cusp : zm.toNat + 1 ≤ maxRep.toNat := by
        have : zm.toNat ≠ maxRep.toNat := fun heq => h_cusp (UInt64.toNat_inj.mp heq)
        omega
      have hf_pos : 0 < f := represents_pos_of_shouldRoundUp_downward g f hf_rep h_sru
      exact ⟨doRoundUp_value_downward_roundUp_noCusp g false zm ze' h_sru h_no_cusp loc res_pos hok_pos hres_pos_mant_ne, h_floor, hf_pos⟩
  · left
    exact doRoundUp_value_downward_truncate g false zm ze' h_sru loc res_pos hok_pos hres_pos_mant_ne

/-! ## Shared scaleDown128 witness for downward/towards_zero -/

local syntax "sd_step" : tactic
local macro_rules | `(tactic| sd_step) => `(tactic|
  (conv_lhs => rw [scaleDown128]; rw [dif_pos (by decide)]; rfl))

-- scaleDown128 witness for xm=1425·10^15, ym=1544988095791843793 (downward/towards_zero)
lemma scaleDown128_div_witness_dw :
    scaleDown128 (922337203685477595999649331516121518 : UInt128) (-36 : Int) Guard.new
        = (922337203685477595, -18,
           { digits_ := 11067113618055500309, xbit_ := true, sbit_ := false }) := by
  sd_step; sd_step; sd_step; sd_step; sd_step
  sd_step; sd_step; sd_step; sd_step; sd_step
  sd_step; sd_step; sd_step; sd_step; sd_step
  sd_step; sd_step; sd_step
  conv_lhs => rw [scaleDown128]; rw [dif_neg (by decide)]
  rfl

end XRPL.Model.Protocol
