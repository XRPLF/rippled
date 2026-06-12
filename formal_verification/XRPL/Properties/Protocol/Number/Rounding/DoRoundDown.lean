import XRPL.Properties.Protocol.Number.Common.Notation
import Mathlib.Tactic

import XRPL.Properties.Protocol.Number.Rounding.DoRoundUp

set_option linter.style.longLine false
set_option linter.style.emptyLine false
set_option linter.unusedTactic false
set_option linter.unusedSimpArgs false
set_option linter.unreachableTactic false

namespace XRPL.Model.Protocol

/-! # Error bound for `Guard.doRoundDown`

`Guard.doRoundDown` is the mirror of `Guard.doRoundUp`: when the round condition fires,
it DECREMENTS the mantissa (instead of incrementing). If `m - 1 < minMantissa`, it
rescales: `((m - 1) * 10, e - 1)`. Otherwise, it keeps the result as `(m - 1, e)`.

There is no cusp branch in `doRoundDown` (no overflow risk on decrement), and the
return type is `RoundResult` directly (not `Except String RoundResult`).
-/

/-! ## Helper: arithmetic facts -/

/-- `m - 1` does not underflow UInt64 when `m.toNat ≥ 1`. -/
lemma m_sub_one_no_underflow {m : UInt64} (h : 1 ≤ m.toNat) :
    (m - 1).toNat = m.toNat - 1 := by
  rw [UInt64.toNat_sub]
  rw [show (1 : UInt64).toNat = 1 from rfl]
  have hm_bound : m.toNat < 2^64 := UInt64.toNat_lt_size m
  omega

/-- `(m - 1) * 10` does not overflow UInt64 when `m ≤ largeRange.min`. -/
lemma m_sub_one_mul_ten_no_overflow {m : UInt64}
    (hmnz : 1 ≤ m.toNat) (hm_le : m.toNat ≤ largeRange.min.toNat) :
    ((m - 1) * 10).toNat = (m.toNat - 1) * 10 := by
  have h10 : (10 : UInt64).toNat = 10 := rfl
  have hsub : (m - 1).toNat = m.toNat - 1 := m_sub_one_no_underflow hmnz
  rw [UInt64.toNat_mul, hsub, h10]
  apply Nat.mod_eq_of_lt
  rw [largeRange_min_val] at hm_le
  calc (m.toNat - 1) * 10 ≤ 1000000000000000000 * 10 := by
        have : m.toNat - 1 ≤ 1000000000000000000 := by omega
        exact Nat.mul_le_mul_right _ this
    _ < 2 ^ 64 := by norm_num

/-! ## Structural form of `bringIntoRange` outputs -/

/-- Value of `bringIntoRange` when mantissa is in range and exponent doesn't underflow. -/
lemma bringIntoRange_value_inRange
    (zn : Bool) (m : UInt64) (e : Int) (minMant : UInt64)
    (h_in_range : ¬ m < minMant) (h_no_under : ¬ e < minExponent) :
    Guard.bringIntoRange zn m e minMant =
      { negative_ := zn, mantissa_ := m, exponent_ := e } := by
  unfold Guard.bringIntoRange
  rw [if_neg h_in_range]
  simp only
  rw [if_neg h_no_under]

/-- Value of `bringIntoRange` when mantissa requires rescale and exponent doesn't underflow. -/
lemma bringIntoRange_value_rescale
    (zn : Bool) (m : UInt64) (e : Int) (minMant : UInt64)
    (h_resc : m < minMant) (h_no_under : ¬ e - 1 < minExponent) :
    Guard.bringIntoRange zn m e minMant =
      { negative_ := zn, mantissa_ := m * 10, exponent_ := e - 1 } := by
  unfold Guard.bringIntoRange
  rw [if_pos h_resc]
  simp only
  rw [if_neg h_no_under]

/-! ## Value characterization for `doRoundDown`

The function returns one of three forms:
- **No round-down**: bringIntoRange applied to `(m, e)`. Value = `m * 10^e`.
- **Round-down, no rescale** (when `m - 1 ≥ minMantissa`): bringIntoRange applied to
  `(m - 1, e)`. Value = `(m - 1) * 10^e`.
- **Round-down, rescale** (when `m = minMantissa`, so `m - 1 < minMantissa`):
  bringIntoRange applied to `((m - 1) * 10, e - 1)`. Value = `(m - 1) * 10^e` (same!).
-/

/-- No round-down case: the rational value of the output equals `m * 10^e`.
Works for any `m` provided the resulting exponent doesn't underflow. -/
lemma doRoundDown_value_noRoundDown
    (g : Guard) (zn : Bool) (m : UInt64) (e : Int) (mode : rounding_mode)
    (h_no_rd : ((g.round mode == 1) || ((g.round mode == 0) && (m % 2 == 1))) = false)
    (h_e_gt : minExponent < e) :
    let res := g.doRoundDown zn m e largeRange.min mode
    (res.mantissa_.toNat : ℚ) * 10 ^ res.exponent_ = (m.toNat : ℚ) * 10 ^ e := by
  unfold Guard.doRoundDown
  simp only [h_no_rd, Bool.false_eq_true, if_false]
  unfold Guard.bringIntoRange
  by_cases hresc : m < largeRange.min
  · rw [if_pos hresc]; simp only []
    have h_no_under : ¬ e - 1 < minExponent := by omega
    rw [if_neg h_no_under]
    have hm_mul_10 : (m * 10).toNat = m.toNat * 10 :=
      m_mul_ten_no_overflow (UInt64.lt_iff_toNat_lt.mp hresc)
    change ((m * 10).toNat : ℚ) * 10 ^ (e - 1) = (m.toNat : ℚ) * 10 ^ e
    rw [hm_mul_10]
    push_cast
    rw [show (e - 1 : ℤ) = e + (-1) from by ring,
        zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0), zpow_neg_one]
    field_simp
  · rw [if_neg hresc]; simp only []
    have h_no_under : ¬ e < minExponent := by omega
    rw [if_neg h_no_under]

/-- Round-down, no rescale case (when `m - 1 ≥ largeRange.min`):
output value equals `(m - 1) * 10^e`. -/
lemma doRoundDown_value_roundDown_noRescale
    (g : Guard) (zn : Bool) (m : UInt64) (e : Int) (mode : rounding_mode)
    (h_rd : ((g.round mode == 1) || ((g.round mode == 0) && (m % 2 == 1))) = true)
    (h_m_gt_min : largeRange.min.toNat < m.toNat)
    (h_e_ge : minExponent ≤ e) :
    let res := g.doRoundDown zn m e largeRange.min mode
    (res.mantissa_.toNat : ℚ) * 10 ^ res.exponent_ = ((m.toNat : ℚ) - 1) * 10 ^ e := by
  have h_m_pos : 1 ≤ m.toNat := by
    rw [largeRange_min_val] at h_m_gt_min; omega
  have hsub : (m - 1).toNat = m.toNat - 1 := m_sub_one_no_underflow h_m_pos
  have h_m_sub_one_ge : largeRange.min.toNat ≤ (m - 1).toNat := by
    rw [hsub]; omega
  have h_m_sub_one_not_lt : ¬ (m - 1) < largeRange.min := by
    intro h; have := UInt64.lt_iff_toNat_lt.mp h; omega
  unfold Guard.doRoundDown
  simp only [h_rd, if_true]
  rw [if_neg h_m_sub_one_not_lt]
  simp only
  have h_no_under : ¬ e < minExponent := by omega
  rw [bringIntoRange_value_inRange zn (m - 1) e largeRange.min h_m_sub_one_not_lt h_no_under]
  simp only
  rw [hsub]
  have h_cast : ((m.toNat - 1 : ℕ) : ℚ) = (m.toNat : ℚ) - 1 := by
    rw [Nat.cast_sub h_m_pos]; simp
  rw [h_cast]

/-- Round-down, rescale case (when `m ≤ largeRange.min`, so `m - 1 < minMantissa`):
output value equals `(m - 1) * 10^e`, achieved via `((m-1)*10, e-1)`. Requires
`(m-1)*10 ≥ largeRange.min` so that `bringIntoRange` doesn't further rescale.
This holds in particular when `m.toNat ≥ 100000000000000001` (= 10^17 + 1). -/
lemma doRoundDown_value_roundDown_rescale
    (g : Guard) (zn : Bool) (m : UInt64) (e : Int) (mode : rounding_mode)
    (h_rd : ((g.round mode == 1) || ((g.round mode == 0) && (m % 2 == 1))) = true)
    (h_m_ge : 100000000000000001 ≤ m.toNat)
    (h_m_le_min : m.toNat ≤ largeRange.min.toNat)
    (h_e_gt : minExponent < e) :
    let res := g.doRoundDown zn m e largeRange.min mode
    (res.mantissa_.toNat : ℚ) * 10 ^ res.exponent_ = ((m.toNat : ℚ) - 1) * 10 ^ e := by
  have h_m_pos : 1 ≤ m.toNat := by omega
  have hsub : (m - 1).toNat = m.toNat - 1 := m_sub_one_no_underflow h_m_pos
  have h_m_sub_one_lt_min : (m - 1) < largeRange.min := by
    rw [UInt64.lt_iff_toNat_lt, hsub]
    rw [largeRange_min_val] at h_m_le_min ⊢; omega
  have h_m_sub_one_mul10_toNat : ((m - 1) * 10).toNat = (m.toNat - 1) * 10 :=
    m_sub_one_mul_ten_no_overflow h_m_pos h_m_le_min
  -- Key: (m.toNat - 1) * 10 ≥ largeRange.min = 10^18
  have h_mul_ge : largeRange.min.toNat ≤ ((m - 1) * 10).toNat := by
    rw [h_m_sub_one_mul10_toNat, largeRange_min_val]
    -- need (m.toNat - 1) * 10 ≥ 10^18, given m.toNat ≥ 10^17 + 1, so m.toNat - 1 ≥ 10^17
    have : m.toNat - 1 ≥ 100000000000000000 := by omega
    calc 1000000000000000000 = 100000000000000000 * 10 := by norm_num
      _ ≤ (m.toNat - 1) * 10 := Nat.mul_le_mul_right _ this
  have h_mul_no_resc : ¬ (m - 1) * 10 < largeRange.min := by
    intro h; have := UInt64.lt_iff_toNat_lt.mp h; omega
  unfold Guard.doRoundDown
  simp only [h_rd, if_true]
  rw [if_pos h_m_sub_one_lt_min]
  simp only
  have h_no_under : ¬ e - 1 < minExponent := by omega
  rw [bringIntoRange_value_inRange zn ((m - 1) * 10) (e - 1) largeRange.min h_mul_no_resc h_no_under]
  simp only
  rw [h_m_sub_one_mul10_toNat]
  have h_cast : (((m.toNat - 1) * 10 : ℕ) : ℚ) = ((m.toNat : ℚ) - 1) * 10 := by
    rw [Nat.cast_mul, Nat.cast_sub h_m_pos]
    push_cast
    ring
  rw [h_cast]
  rw [show (e - 1 : ℤ) = e + (-1) from by ring,
      zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0), zpow_neg_one]
  field_simp

/-- Unified round-down value lemma: when round-down fires and `m.toNat ≥ 10^17 + 1`,
the output value is `(m - 1) * 10^e`, regardless of whether the rescale branch triggers. -/
lemma doRoundDown_value_roundDown
    (g : Guard) (zn : Bool) (m : UInt64) (e : Int) (mode : rounding_mode)
    (h_rd : ((g.round mode == 1) || ((g.round mode == 0) && (m % 2 == 1))) = true)
    (h_m_ge : 100000000000000001 ≤ m.toNat)
    (h_e_gt : minExponent < e) :
    let res := g.doRoundDown zn m e largeRange.min mode
    (res.mantissa_.toNat : ℚ) * 10 ^ res.exponent_ = ((m.toNat : ℚ) - 1) * 10 ^ e := by
  by_cases h_le : m.toNat ≤ largeRange.min.toNat
  · exact doRoundDown_value_roundDown_rescale g zn m e mode h_rd h_m_ge h_le h_e_gt
  · push_neg at h_le
    exact doRoundDown_value_roundDown_noRescale g zn m e mode h_rd h_le (le_of_lt h_e_gt)

/-! ## `.to_nearest` mode: main error bound -/

/-- Supremum-tight relative error bound `5 / (2^63 - 3)` for `.to_nearest`,
analog of `doRoundUp_rounds_to_nearest_supTight`. The denominator differs from
doRoundUp's `2^63 + 7` because doRoundDown has NO cusp branch; the worst case
is the non-floor round-down at `zm = mantissaFloorSucc, f → 1/2+`, giving
error ratio approaching `5/(2^63 - 3)`.

The proof uses the floor-residue constraint `f ≤ 2/10` (the mirror of `f ≥ 8/10`
for doRoundUp) when `zm.toNat = mantissaFloor`. This corresponds to the
sharpness case where the represented "negative correction" is at most `0.2`, so
the underlying ideal value is at most `0.2 * 10^e` below the truncated mantissa. -/
lemma doRoundDown_rounds_to_nearest_supTight (g : Guard) (zm : UInt64) (ze : Int) (f : ℚ)
    (hf_rep : represents g f)
    (h_zm_ge : (mantissaFloor : ℕ) ≤ zm.toNat)
    (_h_zm_le_max : zm.toNat ≤ maxRep.toNat)
    (h_ze_gt : minExponent < ze)
    (h_floor_constraint : zm.toNat = mantissaFloor → f ≤ (2 : ℚ) / 10) :
    let res := g.doRoundDown false zm ze largeRange.min .to_nearest
    |(res.mantissa_.toNat : ℚ) * 10 ^ res.exponent_ -
       ((zm.toNat : ℚ) - f) * 10 ^ ze|
      ≤ ((zm.toNat : ℚ) - f) * 10 ^ ze * (5 / (2 ^ 63 - 3 : ℕ)) := by
  simp only
  have hzm_q_ge : (mantissaFloor : ℚ) ≤ (zm.toNat : ℚ) := by exact_mod_cast h_zm_ge
  have h10ze'_pos : (0 : ℚ) < 10 ^ ze := zpow_pos (by norm_num) _
  have h10ze'_nn : (0 : ℚ) ≤ 10 ^ ze := le_of_lt h10ze'_pos
  have hf_nn := represents_nonneg hf_rep
  have hf_lt1 := represents_lt_one hf_rep
  obtain ⟨hround_pos, hround_neg, hround_zero⟩ := round_correct hf_rep
  have h_denom_val : (((2 ^ 63 - 3 : ℕ) : ℚ)) = 9223372036854775805 := by
    push_cast; norm_num
  have h_denom_pos : (0 : ℚ) < ((2 ^ 63 - 3 : ℕ) : ℚ) := by
    rw [h_denom_val]; norm_num
  have h_zm_ge_lr : (100000000000000001 : ℕ) ≤ zm.toNat := by omega
  have h_round_values : g.round .to_nearest = 1 ∨ g.round .to_nearest = 0
      ∨ g.round .to_nearest = -1 := by
    unfold Guard.round
    by_cases h1 : g.digits_ > 0x5000_0000_0000_0000
    · left; rw [if_pos h1]
    · rw [if_neg h1]
      by_cases h2 : g.digits_ < 0x5000_0000_0000_0000
      · right; right; rw [if_pos h2]
      · rw [if_neg h2]
        by_cases h3 : g.xbit_ = true
        · left; rw [if_pos h3]
        · right; left; rw [if_neg h3]
  -- The error in each case: zm.toNat - f vs the rounded value
  -- Case roundDown fires: value = (zm - 1). Error = (zm - 1) - (zm - f) = (f - 1).
  --   |f - 1| = 1 - f (since f < 1).
  -- Case no round-down: value = zm. Error = zm - (zm - f) = f. |f| = f.
  by_cases h_floor : zm.toNat = mantissaFloor
  · -- floor case: f ≤ 2/10
    have hf_le : f ≤ (2 : ℚ) / 10 := h_floor_constraint h_floor
    have hzm_q_eq_floor : (zm.toNat : ℚ) = mantissaFloor := by
      rw [h_floor]; norm_num
    -- f ≤ 2/10 < 1/2 → round = -1 (no round-down)
    have h_down : g.round .to_nearest = -1 := by
      rcases h_round_values with h1 | h2 | h3
      · -- f > 1/2 contradicts f ≤ 2/10
        have : f > 1/2 := hround_pos.mp h1; linarith
      · -- f = 1/2 contradicts f ≤ 2/10
        have : f = 1/2 := hround_zero.mp h2; linarith
      · exact h3
    have h_no_rd : ((g.round .to_nearest == 1) || ((g.round .to_nearest == 0) && (zm % 2 == 1))) = false := by
      rw [h_down]; rfl
    have h_val := doRoundDown_value_noRoundDown g false zm ze .to_nearest h_no_rd h_ze_gt
    simp only at h_val
    rw [h_val, hzm_q_eq_floor]
    have h_diff : (mantissaFloor : ℚ) * 10 ^ ze - (mantissaFloor - f) * 10 ^ ze
        = f * 10 ^ ze := by ring
    rw [h_diff, abs_mul, abs_of_nonneg h10ze'_nn]
    have h_abs : |f| = f := abs_of_nonneg hf_nn
    rw [h_abs]
    have h_final : f ≤ (mantissaFloor - f) * (5 / ((2 ^ 63 - 3 : ℕ) : ℚ)) := by
      rw [h_denom_val]
      rw [show ((mantissaFloor : ℚ) - f) * (5 / 9223372036854775805)
            = 5 * ((mantissaFloor : ℚ) - f) / 9223372036854775805 by ring]
      rw [le_div_iff₀ (by norm_num : (0 : ℚ) < 9223372036854775805)]
      nlinarith [hf_nn, hf_le]
    calc f * 10 ^ ze
        ≤ (mantissaFloor - f) * (5 / ((2 ^ 63 - 3 : ℕ) : ℚ)) * 10 ^ ze :=
          mul_le_mul_of_nonneg_right h_final h10ze'_nn
      _ = (mantissaFloor - f) * 10 ^ ze * (5 / ((2 ^ 63 - 3 : ℕ) : ℚ)) := by ring
  · -- non-floor: zm.toNat ≥ mantissaFloorSucc
    have h_zm_gt : mantissaFloorSucc ≤ zm.toNat := by omega
    have hzm_q_gt : (mantissaFloorSucc : ℚ) ≤ (zm.toNat : ℚ) := by exact_mod_cast h_zm_gt
    rcases h_round_values with h_up | h_tie | h_down
    · -- Round = 1: roundDown fires. Output = (zm - 1) * 10^ze. Error = (f - 1) * 10^ze, |.| = 1 - f.
      have hf_gt : f > 1/2 := hround_pos.mp h_up
      have h_rd : ((g.round .to_nearest == 1) || ((g.round .to_nearest == 0) && (zm % 2 == 1))) = true := by
        rw [h_up]; rfl
      have h_val := doRoundDown_value_roundDown g false zm ze .to_nearest h_rd h_zm_ge_lr h_ze_gt
      simp only at h_val
      rw [h_val]
      have h_diff : ((zm.toNat : ℚ) - 1) * 10 ^ ze - ((zm.toNat : ℚ) - f) * 10 ^ ze
          = (f - 1) * 10 ^ ze := by ring
      rw [h_diff, abs_mul, abs_of_nonneg h10ze'_nn]
      have h_abs : |(f - 1 : ℚ)| = 1 - f := by
        rw [abs_of_nonpos (by linarith : f - 1 ≤ 0)]; ring
      rw [h_abs]
      have h_final : (1 - f) ≤ ((zm.toNat : ℚ) - f) * (5 / ((2 ^ 63 - 3 : ℕ) : ℚ)) := by
        rw [h_denom_val]
        rw [show ((zm.toNat : ℚ) - f) * (5 / 9223372036854775805)
              = 5 * ((zm.toNat : ℚ) - f) / 9223372036854775805 by ring]
        rw [le_div_iff₀ (by norm_num : (0 : ℚ) < 9223372036854775805)]
        nlinarith [hf_gt, hf_lt1, hzm_q_gt]
      calc (1 - f) * 10 ^ ze
          ≤ ((zm.toNat : ℚ) - f) * (5 / ((2 ^ 63 - 3 : ℕ) : ℚ)) * 10 ^ ze :=
            mul_le_mul_of_nonneg_right h_final h10ze'_nn
        _ = ((zm.toNat : ℚ) - f) * 10 ^ ze * (5 / ((2 ^ 63 - 3 : ℕ) : ℚ)) := by ring
    · -- Round = 0 (tie): f = 1/2.
      have hf_eq : f = 1/2 := hround_zero.mp h_tie
      by_cases h_odd : zm % 2 = 1
      · -- Odd: round-down fires. Value = zm - 1. Error = (f - 1) = -1/2, |.| = 1/2.
        have h_rd : ((g.round .to_nearest == 1) || ((g.round .to_nearest == 0) && (zm % 2 == 1))) = true := by
          rw [h_tie]; simp [h_odd]
        have h_val := doRoundDown_value_roundDown g false zm ze .to_nearest h_rd h_zm_ge_lr h_ze_gt
        simp only at h_val
        rw [h_val, hf_eq]
        have h_diff : ((zm.toNat : ℚ) - 1) * 10 ^ ze - ((zm.toNat : ℚ) - 1/2) * 10 ^ ze
            = -(1/2) * 10 ^ ze := by ring
        rw [h_diff, abs_mul, abs_of_nonneg h10ze'_nn]
        have h_abs : |(-(1/2) : ℚ)| = 1/2 := by norm_num
        rw [h_abs]
        have h_final : (1/2 : ℚ) ≤ ((zm.toNat : ℚ) - 1/2) * (5 / ((2 ^ 63 - 3 : ℕ) : ℚ)) := by
          rw [h_denom_val]
          rw [show ((zm.toNat : ℚ) - 1/2) * (5 / 9223372036854775805)
                = 5 * ((zm.toNat : ℚ) - 1/2) / 9223372036854775805 by ring]
          rw [le_div_iff₀ (by norm_num : (0 : ℚ) < 9223372036854775805)]
          linarith [hzm_q_gt]
        calc (1/2 : ℚ) * 10 ^ ze
            ≤ ((zm.toNat : ℚ) - 1/2) * (5 / ((2 ^ 63 - 3 : ℕ) : ℚ)) * 10 ^ ze :=
              mul_le_mul_of_nonneg_right h_final h10ze'_nn
          _ = ((zm.toNat : ℚ) - 1/2) * 10 ^ ze * (5 / ((2 ^ 63 - 3 : ℕ) : ℚ)) := by ring
      · -- Even: no round-down. Value = zm. Error = f = 1/2.
        have h_zm_even : zm.toNat % 2 = 0 := by
          have hne : (zm % 2).toNat ≠ (1 : UInt64).toNat := by
            intro heq; exact h_odd (UInt64.toNat_inj.mp heq)
          have hmod_nat : (zm % 2).toNat = zm.toNat % (2 : UInt64).toNat := by
            rw [UInt64.toNat_mod]
          have h2_nat : (2 : UInt64).toNat = 2 := rfl
          have h1_nat : (1 : UInt64).toNat = 1 := rfl
          rw [hmod_nat, h2_nat, h1_nat] at hne
          have hmod_lt : zm.toNat % 2 < 2 := Nat.mod_lt _ (by norm_num)
          omega
        have h_zm_ge_even : 922337203685477582 ≤ zm.toNat := by
          have h_odd_lb : mantissaFloorSucc % 2 = 1 := by norm_num
          omega
        have hzm_q_ge_even : (922337203685477582 : ℚ) ≤ (zm.toNat : ℚ) := by
          exact_mod_cast h_zm_ge_even
        have h_no_rd : ((g.round .to_nearest == 1) || ((g.round .to_nearest == 0) && (zm % 2 == 1))) = false := by
          rw [h_tie]
          have : (zm % 2 == 1) = false := by
            apply Bool.eq_false_iff.mpr
            intro h; exact h_odd (beq_iff_eq.mp h)
          simp [this]
        have h_val := doRoundDown_value_noRoundDown g false zm ze .to_nearest h_no_rd h_ze_gt
        simp only at h_val
        rw [h_val, hf_eq]
        have h_diff : (zm.toNat : ℚ) * 10 ^ ze - ((zm.toNat : ℚ) - 1/2) * 10 ^ ze
            = (1/2) * 10 ^ ze := by ring
        rw [h_diff, abs_mul, abs_of_nonneg h10ze'_nn]
        have h_abs : |((1/2) : ℚ)| = 1/2 := by norm_num
        rw [h_abs]
        have h_final : (1/2 : ℚ) ≤ ((zm.toNat : ℚ) - 1/2) * (5 / ((2 ^ 63 - 3 : ℕ) : ℚ)) := by
          rw [h_denom_val]
          rw [show ((zm.toNat : ℚ) - 1/2) * (5 / 9223372036854775805)
                = 5 * ((zm.toNat : ℚ) - 1/2) / 9223372036854775805 by ring]
          rw [le_div_iff₀ (by norm_num : (0 : ℚ) < 9223372036854775805)]
          linarith [hzm_q_ge_even]
        calc (1/2 : ℚ) * 10 ^ ze
            ≤ ((zm.toNat : ℚ) - 1/2) * (5 / ((2 ^ 63 - 3 : ℕ) : ℚ)) * 10 ^ ze :=
              mul_le_mul_of_nonneg_right h_final h10ze'_nn
          _ = ((zm.toNat : ℚ) - 1/2) * 10 ^ ze * (5 / ((2 ^ 63 - 3 : ℕ) : ℚ)) := by ring
    · -- Round = -1: no round-down. Value = zm. Error = f.
      have hf_lt : f < 1/2 := hround_neg.mp h_down
      have h_no_rd : ((g.round .to_nearest == 1) || ((g.round .to_nearest == 0) && (zm % 2 == 1))) = false := by
        rw [h_down]; rfl
      have h_val := doRoundDown_value_noRoundDown g false zm ze .to_nearest h_no_rd h_ze_gt
      simp only at h_val
      rw [h_val]
      have h_diff : (zm.toNat : ℚ) * 10 ^ ze - ((zm.toNat : ℚ) - f) * 10 ^ ze
          = f * 10 ^ ze := by ring
      rw [h_diff, abs_mul, abs_of_nonneg h10ze'_nn]
      have h_abs : |f| = f := abs_of_nonneg hf_nn
      rw [h_abs]
      have h_final : f ≤ ((zm.toNat : ℚ) - f) * (5 / ((2 ^ 63 - 3 : ℕ) : ℚ)) := by
        rw [h_denom_val]
        rw [show ((zm.toNat : ℚ) - f) * (5 / 9223372036854775805)
              = 5 * ((zm.toNat : ℚ) - f) / 9223372036854775805 by ring]
        rw [le_div_iff₀ (by norm_num : (0 : ℚ) < 9223372036854775805)]
        nlinarith [hf_nn, hf_lt, hzm_q_gt]
      calc f * 10 ^ ze
          ≤ ((zm.toNat : ℚ) - f) * (5 / ((2 ^ 63 - 3 : ℕ) : ℚ)) * 10 ^ ze :=
            mul_le_mul_of_nonneg_right h_final h10ze'_nn
        _ = ((zm.toNat : ℚ) - f) * 10 ^ ze * (5 / ((2 ^ 63 - 3 : ℕ) : ℚ)) := by ring

/-! ## Directed-mode value characterizations -/

/-- For `.downward`, the boolean condition in `doRoundDown` matches `shouldRoundUp_downward`. -/
private lemma roundDown_bool_downward_false (g : Guard) (m : UInt64)
    (h_no : ¬ g.shouldRoundUp_downward) :
    ((g.round .downward == 1) || ((g.round .downward == 0) && (m % 2 == 1))) = false := by
  have h_neg1 : g.round .downward = -1 := (round_downward_eq_neg_one_iff g).mpr h_no
  rw [h_neg1]; rfl

private lemma roundDown_bool_downward_true (g : Guard) (m : UInt64)
    (h_yes : g.shouldRoundUp_downward) :
    ((g.round .downward == 1) || ((g.round .downward == 0) && (m % 2 == 1))) = true := by
  have h1 : g.round .downward = 1 := (round_downward_eq_one_iff g).mpr h_yes
  rw [h1]; rfl

/-- `.downward`: value lemmas. -/
theorem doRoundDown_value_downward_truncate
    (g : Guard) (zn : Bool) (zm : UInt64) (ze : Int)
    (h_no_rd : ¬ g.shouldRoundUp_downward)
    (h_ze_gt : minExponent < ze) :
    let res := g.doRoundDown zn zm ze largeRange.min .downward
    (res.mantissa_.toNat : ℚ) * 10 ^ res.exponent_ = (zm.toNat : ℚ) * 10 ^ ze :=
  doRoundDown_value_noRoundDown g zn zm ze .downward (roundDown_bool_downward_false g zm h_no_rd) h_ze_gt

theorem doRoundDown_value_downward_roundDown
    (g : Guard) (zn : Bool) (zm : UInt64) (ze : Int)
    (h_rd : g.shouldRoundUp_downward)
    (h_zm_ge : (100000000000000001 : ℕ) ≤ zm.toNat)
    (h_ze_gt : minExponent < ze) :
    let res := g.doRoundDown zn zm ze largeRange.min .downward
    (res.mantissa_.toNat : ℚ) * 10 ^ res.exponent_ = ((zm.toNat : ℚ) - 1) * 10 ^ ze :=
  doRoundDown_value_roundDown g zn zm ze .downward (roundDown_bool_downward_true g zm h_rd) h_zm_ge h_ze_gt

/-- `.upward`: value lemmas. -/
private lemma roundDown_bool_upward_false (g : Guard) (m : UInt64)
    (h_no : ¬ g.shouldRoundUp_upward) :
    ((g.round .upward == 1) || ((g.round .upward == 0) && (m % 2 == 1))) = false := by
  have h_neg1 : g.round .upward = -1 := (round_upward_eq_neg_one_iff g).mpr h_no
  rw [h_neg1]; rfl

private lemma roundDown_bool_upward_true (g : Guard) (m : UInt64)
    (h_yes : g.shouldRoundUp_upward) :
    ((g.round .upward == 1) || ((g.round .upward == 0) && (m % 2 == 1))) = true := by
  have h1 : g.round .upward = 1 := (round_upward_eq_one_iff g).mpr h_yes
  rw [h1]; rfl

theorem doRoundDown_value_upward_truncate
    (g : Guard) (zn : Bool) (zm : UInt64) (ze : Int)
    (h_no_rd : ¬ g.shouldRoundUp_upward)
    (h_ze_gt : minExponent < ze) :
    let res := g.doRoundDown zn zm ze largeRange.min .upward
    (res.mantissa_.toNat : ℚ) * 10 ^ res.exponent_ = (zm.toNat : ℚ) * 10 ^ ze :=
  doRoundDown_value_noRoundDown g zn zm ze .upward (roundDown_bool_upward_false g zm h_no_rd) h_ze_gt

theorem doRoundDown_value_upward_roundDown
    (g : Guard) (zn : Bool) (zm : UInt64) (ze : Int)
    (h_rd : g.shouldRoundUp_upward)
    (h_zm_ge : (100000000000000001 : ℕ) ≤ zm.toNat)
    (h_ze_gt : minExponent < ze) :
    let res := g.doRoundDown zn zm ze largeRange.min .upward
    (res.mantissa_.toNat : ℚ) * 10 ^ res.exponent_ = ((zm.toNat : ℚ) - 1) * 10 ^ ze :=
  doRoundDown_value_roundDown g zn zm ze .upward (roundDown_bool_upward_true g zm h_rd) h_zm_ge h_ze_gt

/-- `.towards_zero`: never fires round-down. -/
theorem doRoundDown_value_towards_zero_truncate
    (g : Guard) (zn : Bool) (zm : UInt64) (ze : Int)
    (h_ze_gt : minExponent < ze) :
    let res := g.doRoundDown zn zm ze largeRange.min .towards_zero
    (res.mantissa_.toNat : ℚ) * 10 ^ res.exponent_ = (zm.toNat : ℚ) * 10 ^ ze := by
  apply doRoundDown_value_noRoundDown g zn zm ze .towards_zero _ h_ze_gt
  rw [round_towards_zero_eq_neg_one]; rfl

/-! ## Directed-mode error bounds (supTight)

For directed modes, the error is at most `10 * 10^e` in absolute terms (no halving from
nearest-even). The relative bound is `10/(2^63 + 2)` which is the standard XRPL constant. -/

/-- `.downward` mode `_supTight`: error ≤ value * `10/(2^63 - 8)`.

In `.downward` mode, the only way round-down fires is when `sbit_ = true` and the
guard is non-empty, i.e., the represented value has a "negative residue" component.
This makes the bound symmetric with directed-mode doRoundUp. -/
lemma doRoundDown_rounds_downward_supTight (g : Guard) (zm : UInt64) (ze : Int) (f : ℚ)
    (hf_rep : represents g f)
    (h_zm_ge : (mantissaFloor : ℕ) ≤ zm.toNat)
    (_h_zm_le_max : zm.toNat ≤ maxRep.toNat)
    (h_ze_gt : minExponent < ze) :
    let res := g.doRoundDown false zm ze largeRange.min .downward
    |(res.mantissa_.toNat : ℚ) * 10 ^ res.exponent_ -
       ((zm.toNat : ℚ) - f) * 10 ^ ze|
      ≤ ((zm.toNat : ℚ) - f) * 10 ^ ze * (10 / (2 ^ 63 - 18 : ℕ)) := by
  simp only
  have hzm_q_ge : (mantissaFloor : ℚ) ≤ (zm.toNat : ℚ) := by exact_mod_cast h_zm_ge
  have h10ze'_pos : (0 : ℚ) < 10 ^ ze := zpow_pos (by norm_num) _
  have h10ze'_nn : (0 : ℚ) ≤ 10 ^ ze := le_of_lt h10ze'_pos
  have hf_nn := represents_nonneg hf_rep
  have hf_lt1 := represents_lt_one hf_rep
  have h_denom_val : (((2 ^ 63 - 18 : ℕ) : ℚ)) = 9223372036854775790 := by
    push_cast; norm_num
  have h_denom_pos : (0 : ℚ) < ((2 ^ 63 - 18 : ℕ) : ℚ) := by
    rw [h_denom_val]; norm_num
  have h_zm_ge_lr : (100000000000000001 : ℕ) ≤ zm.toNat := by omega
  by_cases h_rd : g.shouldRoundUp_downward
  · -- Round-down fires. Value = (zm - 1) * 10^ze. Error = (f - 1)·10^ze, |.| = 1 - f.
    have h_val := doRoundDown_value_downward_roundDown g false zm ze h_rd h_zm_ge_lr h_ze_gt
    simp only at h_val
    rw [h_val]
    have h_diff : ((zm.toNat : ℚ) - 1) * 10 ^ ze - ((zm.toNat : ℚ) - f) * 10 ^ ze
        = (f - 1) * 10 ^ ze := by ring
    rw [h_diff, abs_mul, abs_of_nonneg h10ze'_nn]
    have h_abs : |(f - 1 : ℚ)| = 1 - f := by
      rw [abs_of_nonpos (by linarith : f - 1 ≤ 0)]; ring
    rw [h_abs]
    have h_final : (1 - f) ≤ ((zm.toNat : ℚ) - f) * (10 / ((2 ^ 63 - 18 : ℕ) : ℚ)) := by
      rw [h_denom_val]
      rw [show ((zm.toNat : ℚ) - f) * (10 / 9223372036854775790)
            = 10 * ((zm.toNat : ℚ) - f) / 9223372036854775790 by ring]
      rw [le_div_iff₀ (by norm_num : (0 : ℚ) < 9223372036854775790)]
      nlinarith [hf_nn, hf_lt1, hzm_q_ge]
    calc (1 - f) * 10 ^ ze
        ≤ ((zm.toNat : ℚ) - f) * (10 / ((2 ^ 63 - 18 : ℕ) : ℚ)) * 10 ^ ze :=
          mul_le_mul_of_nonneg_right h_final h10ze'_nn
      _ = ((zm.toNat : ℚ) - f) * 10 ^ ze * (10 / ((2 ^ 63 - 18 : ℕ) : ℚ)) := by ring
  · -- No round-down. Value = zm * 10^ze. Error = f * 10^ze.
    have h_val := doRoundDown_value_downward_truncate g false zm ze h_rd h_ze_gt
    simp only at h_val
    rw [h_val]
    have h_diff : (zm.toNat : ℚ) * 10 ^ ze - ((zm.toNat : ℚ) - f) * 10 ^ ze = f * 10 ^ ze := by ring
    rw [h_diff, abs_mul, abs_of_nonneg h10ze'_nn]
    have h_abs : |f| = f := abs_of_nonneg hf_nn
    rw [h_abs]
    have h_final : f ≤ ((zm.toNat : ℚ) - f) * (10 / ((2 ^ 63 - 18 : ℕ) : ℚ)) := by
      rw [h_denom_val]
      rw [show ((zm.toNat : ℚ) - f) * (10 / 9223372036854775790)
            = 10 * ((zm.toNat : ℚ) - f) / 9223372036854775790 by ring]
      rw [le_div_iff₀ (by norm_num : (0 : ℚ) < 9223372036854775790)]
      nlinarith [hf_nn, hf_lt1, hzm_q_ge]
    calc f * 10 ^ ze
        ≤ ((zm.toNat : ℚ) - f) * (10 / ((2 ^ 63 - 18 : ℕ) : ℚ)) * 10 ^ ze :=
          mul_le_mul_of_nonneg_right h_final h10ze'_nn
      _ = ((zm.toNat : ℚ) - f) * 10 ^ ze * (10 / ((2 ^ 63 - 18 : ℕ) : ℚ)) := by ring

/-- `.upward` mode `_supTight`. -/
lemma doRoundDown_rounds_upward_supTight (g : Guard) (zm : UInt64) (ze : Int) (f : ℚ)
    (hf_rep : represents g f)
    (h_zm_ge : (mantissaFloor : ℕ) ≤ zm.toNat)
    (_h_zm_le_max : zm.toNat ≤ maxRep.toNat)
    (h_ze_gt : minExponent < ze) :
    let res := g.doRoundDown false zm ze largeRange.min .upward
    |(res.mantissa_.toNat : ℚ) * 10 ^ res.exponent_ -
       ((zm.toNat : ℚ) - f) * 10 ^ ze|
      ≤ ((zm.toNat : ℚ) - f) * 10 ^ ze * (10 / (2 ^ 63 - 18 : ℕ)) := by
  simp only
  have hzm_q_ge : (mantissaFloor : ℚ) ≤ (zm.toNat : ℚ) := by exact_mod_cast h_zm_ge
  have h10ze'_pos : (0 : ℚ) < 10 ^ ze := zpow_pos (by norm_num) _
  have h10ze'_nn : (0 : ℚ) ≤ 10 ^ ze := le_of_lt h10ze'_pos
  have hf_nn := represents_nonneg hf_rep
  have hf_lt1 := represents_lt_one hf_rep
  have h_denom_val : (((2 ^ 63 - 18 : ℕ) : ℚ)) = 9223372036854775790 := by
    push_cast; norm_num
  have h_denom_pos : (0 : ℚ) < ((2 ^ 63 - 18 : ℕ) : ℚ) := by
    rw [h_denom_val]; norm_num
  have h_zm_ge_lr : (100000000000000001 : ℕ) ≤ zm.toNat := by omega
  by_cases h_rd : g.shouldRoundUp_upward
  · have h_val := doRoundDown_value_upward_roundDown g false zm ze h_rd h_zm_ge_lr h_ze_gt
    simp only at h_val
    rw [h_val]
    have h_diff : ((zm.toNat : ℚ) - 1) * 10 ^ ze - ((zm.toNat : ℚ) - f) * 10 ^ ze
        = (f - 1) * 10 ^ ze := by ring
    rw [h_diff, abs_mul, abs_of_nonneg h10ze'_nn]
    have h_abs : |(f - 1 : ℚ)| = 1 - f := by
      rw [abs_of_nonpos (by linarith : f - 1 ≤ 0)]; ring
    rw [h_abs]
    have h_final : (1 - f) ≤ ((zm.toNat : ℚ) - f) * (10 / ((2 ^ 63 - 18 : ℕ) : ℚ)) := by
      rw [h_denom_val]
      rw [show ((zm.toNat : ℚ) - f) * (10 / 9223372036854775790)
            = 10 * ((zm.toNat : ℚ) - f) / 9223372036854775790 by ring]
      rw [le_div_iff₀ (by norm_num : (0 : ℚ) < 9223372036854775790)]
      nlinarith [hf_nn, hf_lt1, hzm_q_ge]
    calc (1 - f) * 10 ^ ze
        ≤ ((zm.toNat : ℚ) - f) * (10 / ((2 ^ 63 - 18 : ℕ) : ℚ)) * 10 ^ ze :=
          mul_le_mul_of_nonneg_right h_final h10ze'_nn
      _ = ((zm.toNat : ℚ) - f) * 10 ^ ze * (10 / ((2 ^ 63 - 18 : ℕ) : ℚ)) := by ring
  · have h_val := doRoundDown_value_upward_truncate g false zm ze h_rd h_ze_gt
    simp only at h_val
    rw [h_val]
    have h_diff : (zm.toNat : ℚ) * 10 ^ ze - ((zm.toNat : ℚ) - f) * 10 ^ ze = f * 10 ^ ze := by ring
    rw [h_diff, abs_mul, abs_of_nonneg h10ze'_nn]
    have h_abs : |f| = f := abs_of_nonneg hf_nn
    rw [h_abs]
    have h_final : f ≤ ((zm.toNat : ℚ) - f) * (10 / ((2 ^ 63 - 18 : ℕ) : ℚ)) := by
      rw [h_denom_val]
      rw [show ((zm.toNat : ℚ) - f) * (10 / 9223372036854775790)
            = 10 * ((zm.toNat : ℚ) - f) / 9223372036854775790 by ring]
      rw [le_div_iff₀ (by norm_num : (0 : ℚ) < 9223372036854775790)]
      nlinarith [hf_nn, hf_lt1, hzm_q_ge]
    calc f * 10 ^ ze
        ≤ ((zm.toNat : ℚ) - f) * (10 / ((2 ^ 63 - 18 : ℕ) : ℚ)) * 10 ^ ze :=
          mul_le_mul_of_nonneg_right h_final h10ze'_nn
      _ = ((zm.toNat : ℚ) - f) * 10 ^ ze * (10 / ((2 ^ 63 - 18 : ℕ) : ℚ)) := by ring

/-- `.towards_zero` mode `_supTight`. Always truncates. -/
lemma doRoundDown_rounds_towards_zero_supTight (g : Guard) (zm : UInt64) (ze : Int) (f : ℚ)
    (hf_rep : represents g f)
    (h_zm_ge : (mantissaFloor : ℕ) ≤ zm.toNat)
    (_h_zm_le_max : zm.toNat ≤ maxRep.toNat)
    (h_ze_gt : minExponent < ze) :
    let res := g.doRoundDown false zm ze largeRange.min .towards_zero
    |(res.mantissa_.toNat : ℚ) * 10 ^ res.exponent_ -
       ((zm.toNat : ℚ) - f) * 10 ^ ze|
      ≤ ((zm.toNat : ℚ) - f) * 10 ^ ze * (10 / (2 ^ 63 - 18 : ℕ)) := by
  simp only
  have hzm_q_ge : (mantissaFloor : ℚ) ≤ (zm.toNat : ℚ) := by exact_mod_cast h_zm_ge
  have h10ze'_pos : (0 : ℚ) < 10 ^ ze := zpow_pos (by norm_num) _
  have h10ze'_nn : (0 : ℚ) ≤ 10 ^ ze := le_of_lt h10ze'_pos
  have hf_nn := represents_nonneg hf_rep
  have hf_lt1 := represents_lt_one hf_rep
  have h_denom_val : (((2 ^ 63 - 18 : ℕ) : ℚ)) = 9223372036854775790 := by
    push_cast; norm_num
  have h_denom_pos : (0 : ℚ) < ((2 ^ 63 - 18 : ℕ) : ℚ) := by
    rw [h_denom_val]; norm_num
  have h_val := doRoundDown_value_towards_zero_truncate g false zm ze h_ze_gt
  simp only at h_val
  rw [h_val]
  have h_diff : (zm.toNat : ℚ) * 10 ^ ze - ((zm.toNat : ℚ) - f) * 10 ^ ze = f * 10 ^ ze := by ring
  rw [h_diff, abs_mul, abs_of_nonneg h10ze'_nn]
  have h_abs : |f| = f := abs_of_nonneg hf_nn
  rw [h_abs]
  have h_final : f ≤ ((zm.toNat : ℚ) - f) * (10 / ((2 ^ 63 - 18 : ℕ) : ℚ)) := by
    rw [h_denom_val]
    rw [show ((zm.toNat : ℚ) - f) * (10 / 9223372036854775790)
          = 10 * ((zm.toNat : ℚ) - f) / 9223372036854775790 by ring]
    rw [le_div_iff₀ (by norm_num : (0 : ℚ) < 9223372036854775790)]
    nlinarith [hf_nn, hf_lt1, hzm_q_ge]
  calc f * 10 ^ ze
      ≤ ((zm.toNat : ℚ) - f) * (10 / ((2 ^ 63 - 18 : ℕ) : ℚ)) * 10 ^ ze :=
        mul_le_mul_of_nonneg_right h_final h10ze'_nn
    _ = ((zm.toNat : ℚ) - f) * 10 ^ ze * (10 / ((2 ^ 63 - 18 : ℕ) : ℚ)) := by ring

/-! ## Output invariants for `doRoundDown`

Like `doRoundUp_output_invariants`, these show the result mantissa is in
`[largeRange.min, largeRange.max]` and the exponent is bounded below by minExponent.
Unlike doRoundUp, there's no cusp branch so the mantissa never exceeds maxRep. -/

/-- Output invariants for `.to_nearest` mode. Given a "normalized" input (mantissa in
the expected range), the output is also normalized. -/
lemma doRoundDown_output_invariants
    (g : Guard) (zn : Bool) (m : UInt64) (e : Int)
    (h_lb : largeRange.min.toNat ≤ m.toNat)
    (h_ub : m.toNat ≤ maxRep.toNat)
    (h_e_gt : minExponent < e) :
    let res := g.doRoundDown zn m e largeRange.min .to_nearest
    largeRange.min.toNat ≤ res.mantissa_.toNat ∧
    res.mantissa_.toNat ≤ largeRange.max.toNat ∧
    minExponent ≤ res.exponent_ ∧
    (res.mantissa_.toNat > maxRep.toNat → res.mantissa_.toNat % 10 = 0) := by
  simp only
  have hmaxRep_v : maxRep.toNat = maxRepNat := maxRep_val
  have hminMant_v : largeRange.min.toNat = 1000000000000000000 := largeRange_min_val
  have hmaxMant_v : largeRange.max.toNat = 9999999999999999999 := largeRange_max_val
  have h_m_pos : 1 ≤ m.toNat := by rw [hminMant_v] at h_lb; omega
  have hsub : (m - 1).toNat = m.toNat - 1 := m_sub_one_no_underflow h_m_pos
  by_cases h_rd : ((g.round .to_nearest == 1) || ((g.round .to_nearest == 0) && (m % 2 == 1))) = true
  · -- round-down fires
    by_cases h_eq_min : m.toNat = largeRange.min.toNat
    · -- m = largeRange.min: m - 1 < largeRange.min, rescale path
      have h_m_sub_one_lt : (m - 1) < largeRange.min := by
        rw [UInt64.lt_iff_toNat_lt, hsub]; rw [hminMant_v] at h_eq_min ⊢; omega
      have h_m_le_min : m.toNat ≤ largeRange.min.toNat := by omega
      have h_mul10_toNat : ((m - 1) * 10).toNat = (m.toNat - 1) * 10 :=
        m_sub_one_mul_ten_no_overflow h_m_pos h_m_le_min
      have h_mul10_ge : largeRange.min.toNat ≤ ((m - 1) * 10).toNat := by
        rw [h_mul10_toNat, hminMant_v]; rw [hminMant_v] at h_eq_min; omega
      have h_mul10_not_lt : ¬ ((m - 1) * 10) < largeRange.min := by
        intro h; have := UInt64.lt_iff_toNat_lt.mp h; omega
      have h_no_under : ¬ e - 1 < minExponent := by omega
      have hres : g.doRoundDown zn m e largeRange.min .to_nearest =
          { negative_ := zn, mantissa_ := (m - 1) * 10, exponent_ := e - 1 } := by
        unfold Guard.doRoundDown
        simp only [h_rd, if_true, if_pos h_m_sub_one_lt]
        exact bringIntoRange_value_inRange zn ((m - 1) * 10) (e - 1) largeRange.min h_mul10_not_lt h_no_under
      rw [hres]; simp only
      refine ⟨h_mul10_ge, ?_, ?_, ?_⟩
      · rw [h_mul10_toNat, hmaxMant_v]
        rw [hminMant_v] at h_eq_min
        have : m.toNat - 1 = 999999999999999999 := by omega
        rw [this]; norm_num
      · omega
      · intro _; rw [h_mul10_toNat]
        rw [hminMant_v] at h_eq_min
        have : m.toNat - 1 = 999999999999999999 := by omega
        rw [this]
    · -- m > largeRange.min: m - 1 ≥ largeRange.min, no rescale
      have h_m_gt_min : largeRange.min.toNat < m.toNat := lt_of_le_of_ne h_lb (Ne.symm h_eq_min)
      have h_m_sub_one_ge : largeRange.min.toNat ≤ (m - 1).toNat := by
        rw [hsub]; omega
      have h_m_sub_one_not_lt : ¬ (m - 1) < largeRange.min := by
        intro h; have := UInt64.lt_iff_toNat_lt.mp h; omega
      have h_no_under : ¬ e < minExponent := by omega
      have hres : g.doRoundDown zn m e largeRange.min .to_nearest =
          { negative_ := zn, mantissa_ := m - 1, exponent_ := e } := by
        unfold Guard.doRoundDown
        simp only [h_rd, if_true, if_neg h_m_sub_one_not_lt]
        exact bringIntoRange_value_inRange zn (m - 1) e largeRange.min h_m_sub_one_not_lt h_no_under
      rw [hres]; simp only
      refine ⟨h_m_sub_one_ge, ?_, ?_, ?_⟩
      · rw [hsub, hmaxMant_v]; rw [hmaxRep_v] at h_ub; omega
      · omega
      · intro h_gt
        rw [hsub] at h_gt ⊢
        rw [hmaxRep_v] at h_gt h_ub
        omega
  · have h_rd_false : ((g.round .to_nearest == 1) || ((g.round .to_nearest == 0) && (m % 2 == 1))) = false := by
      rw [Bool.eq_false_iff]; exact h_rd
    have h_m_not_lt : ¬ m < largeRange.min := by
      intro h; have := UInt64.lt_iff_toNat_lt.mp h; omega
    have h_no_under : ¬ e < minExponent := by omega
    have hres : g.doRoundDown zn m e largeRange.min .to_nearest =
        { negative_ := zn, mantissa_ := m, exponent_ := e } := by
      unfold Guard.doRoundDown
      simp only [h_rd_false, Bool.false_eq_true, if_false]
      exact bringIntoRange_value_inRange zn m e largeRange.min h_m_not_lt h_no_under
    rw [hres]; simp only
    refine ⟨h_lb, ?_, ?_, ?_⟩
    · rw [hmaxRep_v] at h_ub; rw [hmaxMant_v]; omega
    · omega
    · intro h_gt; rw [hmaxRep_v] at h_gt h_ub; omega

/-- Output invariants for `.downward` mode. -/
lemma doRoundDown_output_invariants_downward
    (g : Guard) (zn : Bool) (m : UInt64) (e : Int)
    (h_lb : largeRange.min.toNat ≤ m.toNat)
    (h_ub : m.toNat ≤ maxRep.toNat)
    (h_e_gt : minExponent < e) :
    let res := g.doRoundDown zn m e largeRange.min .downward
    largeRange.min.toNat ≤ res.mantissa_.toNat ∧
    res.mantissa_.toNat ≤ largeRange.max.toNat ∧
    minExponent ≤ res.exponent_ ∧
    (res.mantissa_.toNat > maxRep.toNat → res.mantissa_.toNat % 10 = 0) := by
  simp only
  have hmaxRep_v : maxRep.toNat = maxRepNat := maxRep_val
  have hminMant_v : largeRange.min.toNat = 1000000000000000000 := largeRange_min_val
  have hmaxMant_v : largeRange.max.toNat = 9999999999999999999 := largeRange_max_val
  have h_m_pos : 1 ≤ m.toNat := by rw [hminMant_v] at h_lb; omega
  have hsub : (m - 1).toNat = m.toNat - 1 := m_sub_one_no_underflow h_m_pos
  by_cases h_rd : ((g.round .downward == 1) || ((g.round .downward == 0) && (m % 2 == 1))) = true
  · by_cases h_eq_min : m.toNat = largeRange.min.toNat
    · have h_m_sub_one_lt : (m - 1) < largeRange.min := by
        rw [UInt64.lt_iff_toNat_lt, hsub]; rw [hminMant_v] at h_eq_min ⊢; omega
      have h_m_le_min : m.toNat ≤ largeRange.min.toNat := by omega
      have h_mul10_toNat : ((m - 1) * 10).toNat = (m.toNat - 1) * 10 :=
        m_sub_one_mul_ten_no_overflow h_m_pos h_m_le_min
      have h_mul10_ge : largeRange.min.toNat ≤ ((m - 1) * 10).toNat := by
        rw [h_mul10_toNat, hminMant_v]; rw [hminMant_v] at h_eq_min; omega
      have h_mul10_not_lt : ¬ ((m - 1) * 10) < largeRange.min := by
        intro h; have := UInt64.lt_iff_toNat_lt.mp h; omega
      have h_no_under : ¬ e - 1 < minExponent := by omega
      have hres : g.doRoundDown zn m e largeRange.min .downward =
          { negative_ := zn, mantissa_ := (m - 1) * 10, exponent_ := e - 1 } := by
        unfold Guard.doRoundDown
        simp only [h_rd, if_true, if_pos h_m_sub_one_lt]
        exact bringIntoRange_value_inRange zn ((m - 1) * 10) (e - 1) largeRange.min h_mul10_not_lt h_no_under
      rw [hres]; simp only
      refine ⟨h_mul10_ge, ?_, ?_, ?_⟩
      · rw [h_mul10_toNat, hmaxMant_v]
        rw [hminMant_v] at h_eq_min
        have : m.toNat - 1 = 999999999999999999 := by omega
        rw [this]; norm_num
      · omega
      · intro _; rw [h_mul10_toNat]
        rw [hminMant_v] at h_eq_min
        have : m.toNat - 1 = 999999999999999999 := by omega
        rw [this]
    · have h_m_gt_min : largeRange.min.toNat < m.toNat := lt_of_le_of_ne h_lb (Ne.symm h_eq_min)
      have h_m_sub_one_ge : largeRange.min.toNat ≤ (m - 1).toNat := by
        rw [hsub]; omega
      have h_m_sub_one_not_lt : ¬ (m - 1) < largeRange.min := by
        intro h; have := UInt64.lt_iff_toNat_lt.mp h; omega
      have h_no_under : ¬ e < minExponent := by omega
      have hres : g.doRoundDown zn m e largeRange.min .downward =
          { negative_ := zn, mantissa_ := m - 1, exponent_ := e } := by
        unfold Guard.doRoundDown
        simp only [h_rd, if_true, if_neg h_m_sub_one_not_lt]
        exact bringIntoRange_value_inRange zn (m - 1) e largeRange.min h_m_sub_one_not_lt h_no_under
      rw [hres]; simp only
      refine ⟨h_m_sub_one_ge, ?_, ?_, ?_⟩
      · rw [hsub, hmaxMant_v]; rw [hmaxRep_v] at h_ub; omega
      · omega
      · intro h_gt
        rw [hsub] at h_gt ⊢
        rw [hmaxRep_v] at h_gt h_ub; omega
  · have h_rd_false : ((g.round .downward == 1) || ((g.round .downward == 0) && (m % 2 == 1))) = false := by
      rw [Bool.eq_false_iff]; exact h_rd
    have h_m_not_lt : ¬ m < largeRange.min := by
      intro h; have := UInt64.lt_iff_toNat_lt.mp h; omega
    have h_no_under : ¬ e < minExponent := by omega
    have hres : g.doRoundDown zn m e largeRange.min .downward =
        { negative_ := zn, mantissa_ := m, exponent_ := e } := by
      unfold Guard.doRoundDown
      simp only [h_rd_false, Bool.false_eq_true, if_false]
      exact bringIntoRange_value_inRange zn m e largeRange.min h_m_not_lt h_no_under
    rw [hres]; simp only
    refine ⟨h_lb, ?_, ?_, ?_⟩
    · rw [hmaxRep_v] at h_ub; rw [hmaxMant_v]; omega
    · omega
    · intro h_gt; rw [hmaxRep_v] at h_gt h_ub; omega

/-- Output invariants for `.upward` mode. -/
lemma doRoundDown_output_invariants_upward
    (g : Guard) (zn : Bool) (m : UInt64) (e : Int)
    (h_lb : largeRange.min.toNat ≤ m.toNat)
    (h_ub : m.toNat ≤ maxRep.toNat)
    (h_e_gt : minExponent < e) :
    let res := g.doRoundDown zn m e largeRange.min .upward
    largeRange.min.toNat ≤ res.mantissa_.toNat ∧
    res.mantissa_.toNat ≤ largeRange.max.toNat ∧
    minExponent ≤ res.exponent_ ∧
    (res.mantissa_.toNat > maxRep.toNat → res.mantissa_.toNat % 10 = 0) := by
  simp only
  have hmaxRep_v : maxRep.toNat = maxRepNat := maxRep_val
  have hminMant_v : largeRange.min.toNat = 1000000000000000000 := largeRange_min_val
  have hmaxMant_v : largeRange.max.toNat = 9999999999999999999 := largeRange_max_val
  have h_m_pos : 1 ≤ m.toNat := by rw [hminMant_v] at h_lb; omega
  have hsub : (m - 1).toNat = m.toNat - 1 := m_sub_one_no_underflow h_m_pos
  by_cases h_rd : ((g.round .upward == 1) || ((g.round .upward == 0) && (m % 2 == 1))) = true
  · by_cases h_eq_min : m.toNat = largeRange.min.toNat
    · have h_m_sub_one_lt : (m - 1) < largeRange.min := by
        rw [UInt64.lt_iff_toNat_lt, hsub]; rw [hminMant_v] at h_eq_min ⊢; omega
      have h_m_le_min : m.toNat ≤ largeRange.min.toNat := by omega
      have h_mul10_toNat : ((m - 1) * 10).toNat = (m.toNat - 1) * 10 :=
        m_sub_one_mul_ten_no_overflow h_m_pos h_m_le_min
      have h_mul10_ge : largeRange.min.toNat ≤ ((m - 1) * 10).toNat := by
        rw [h_mul10_toNat, hminMant_v]; rw [hminMant_v] at h_eq_min; omega
      have h_mul10_not_lt : ¬ ((m - 1) * 10) < largeRange.min := by
        intro h; have := UInt64.lt_iff_toNat_lt.mp h; omega
      have h_no_under : ¬ e - 1 < minExponent := by omega
      have hres : g.doRoundDown zn m e largeRange.min .upward =
          { negative_ := zn, mantissa_ := (m - 1) * 10, exponent_ := e - 1 } := by
        unfold Guard.doRoundDown
        simp only [h_rd, if_true, if_pos h_m_sub_one_lt]
        exact bringIntoRange_value_inRange zn ((m - 1) * 10) (e - 1) largeRange.min h_mul10_not_lt h_no_under
      rw [hres]; simp only
      refine ⟨h_mul10_ge, ?_, ?_, ?_⟩
      · rw [h_mul10_toNat, hmaxMant_v]
        rw [hminMant_v] at h_eq_min
        have : m.toNat - 1 = 999999999999999999 := by omega
        rw [this]; norm_num
      · omega
      · intro _; rw [h_mul10_toNat]
        rw [hminMant_v] at h_eq_min
        have : m.toNat - 1 = 999999999999999999 := by omega
        rw [this]
    · have h_m_gt_min : largeRange.min.toNat < m.toNat := lt_of_le_of_ne h_lb (Ne.symm h_eq_min)
      have h_m_sub_one_ge : largeRange.min.toNat ≤ (m - 1).toNat := by
        rw [hsub]; omega
      have h_m_sub_one_not_lt : ¬ (m - 1) < largeRange.min := by
        intro h; have := UInt64.lt_iff_toNat_lt.mp h; omega
      have h_no_under : ¬ e < minExponent := by omega
      have hres : g.doRoundDown zn m e largeRange.min .upward =
          { negative_ := zn, mantissa_ := m - 1, exponent_ := e } := by
        unfold Guard.doRoundDown
        simp only [h_rd, if_true, if_neg h_m_sub_one_not_lt]
        exact bringIntoRange_value_inRange zn (m - 1) e largeRange.min h_m_sub_one_not_lt h_no_under
      rw [hres]; simp only
      refine ⟨h_m_sub_one_ge, ?_, ?_, ?_⟩
      · rw [hsub, hmaxMant_v]; rw [hmaxRep_v] at h_ub; omega
      · omega
      · intro h_gt
        rw [hsub] at h_gt ⊢
        rw [hmaxRep_v] at h_gt h_ub; omega
  · have h_rd_false : ((g.round .upward == 1) || ((g.round .upward == 0) && (m % 2 == 1))) = false := by
      rw [Bool.eq_false_iff]; exact h_rd
    have h_m_not_lt : ¬ m < largeRange.min := by
      intro h; have := UInt64.lt_iff_toNat_lt.mp h; omega
    have h_no_under : ¬ e < minExponent := by omega
    have hres : g.doRoundDown zn m e largeRange.min .upward =
        { negative_ := zn, mantissa_ := m, exponent_ := e } := by
      unfold Guard.doRoundDown
      simp only [h_rd_false, Bool.false_eq_true, if_false]
      exact bringIntoRange_value_inRange zn m e largeRange.min h_m_not_lt h_no_under
    rw [hres]; simp only
    refine ⟨h_lb, ?_, ?_, ?_⟩
    · rw [hmaxRep_v] at h_ub; rw [hmaxMant_v]; omega
    · omega
    · intro h_gt; rw [hmaxRep_v] at h_gt h_ub; omega

/-- Output invariants for `.towards_zero` mode. -/
lemma doRoundDown_output_invariants_towards_zero
    (g : Guard) (zn : Bool) (m : UInt64) (e : Int)
    (h_lb : largeRange.min.toNat ≤ m.toNat)
    (h_ub : m.toNat ≤ maxRep.toNat)
    (h_e_gt : minExponent < e) :
    let res := g.doRoundDown zn m e largeRange.min .towards_zero
    largeRange.min.toNat ≤ res.mantissa_.toNat ∧
    res.mantissa_.toNat ≤ largeRange.max.toNat ∧
    minExponent ≤ res.exponent_ ∧
    (res.mantissa_.toNat > maxRep.toNat → res.mantissa_.toNat % 10 = 0) := by
  simp only
  have hmaxRep_v : maxRep.toNat = maxRepNat := maxRep_val
  have hminMant_v : largeRange.min.toNat = 1000000000000000000 := largeRange_min_val
  have hmaxMant_v : largeRange.max.toNat = 9999999999999999999 := largeRange_max_val
  have h_ru_false :
      (g.round .towards_zero == 1 || (g.round .towards_zero == 0 && m % 2 == 1)) = false := by
    rw [round_towards_zero_eq_neg_one]; rfl
  have h_m_not_lt : ¬ m < largeRange.min := by
    intro h; have := UInt64.lt_iff_toNat_lt.mp h; omega
  have h_no_under : ¬ e < minExponent := by omega
  have hres : g.doRoundDown zn m e largeRange.min .towards_zero =
      { negative_ := zn, mantissa_ := m, exponent_ := e } := by
    unfold Guard.doRoundDown
    simp only [h_ru_false, Bool.false_eq_true, if_false]
    exact bringIntoRange_value_inRange zn m e largeRange.min h_m_not_lt h_no_under
  rw [hres]; simp only
  refine ⟨h_lb, ?_, ?_, ?_⟩
  · rw [hmaxRep_v] at h_ub; rw [hmaxMant_v]; omega
  · omega
  · intro h_gt; rw [hmaxRep_v] at h_gt h_ub; omega

/-! ## Directed-mode error bounds for diff-sign branch

These mirror the supTight lemmas above, but use the value equation
`(zm + f) * 10^ze = truth` (instead of `(zm - f) * 10^ze` from `represents`).
This is the form produced by the diff-sign branch of `operator_add`:
after the `recover` loop, `f ∈ [0, 1)` is a value-equation residue, and
the `represents g f` predicate does NOT hold.

The rounding decision is entirely a function of `g` (independent of `f`),
so we require `¬ g.shouldRoundUp_<mode>` to ensure no round-down fires.
This is satisfied in the diff-sign use case because the recover loop
reconstructs the guard such that no further round-down is needed.
-/

/-- `.downward` mode `_supTight_of_bounds`: variant of
`doRoundDown_rounds_downward_supTight` that takes `(hf_nn, hf_lt)` instead of
`represents g f`, and frames truth as `(zm + f) * 10^ze`.

Requires `¬ g.shouldRoundUp_downward` so that `doRoundDown` truncates (the
diff-sign branch ensures this). -/
lemma doRoundDown_rounds_downward_supTight_of_bounds
    (g : Guard) (zm : UInt64) (ze : Int) (f : ℚ)
    (hf_nn : 0 ≤ f) (hf_lt : f < 1)
    (h_no_rd : ¬ g.shouldRoundUp_downward)
    (h_zm_ge : (mantissaFloor : ℕ) ≤ zm.toNat)
    (_h_zm_le_max : zm.toNat ≤ maxRep.toNat)
    (h_ze_gt : minExponent < ze) :
    let res := g.doRoundDown false zm ze largeRange.min .downward
    |(res.mantissa_.toNat : ℚ) * 10 ^ res.exponent_ -
       ((zm.toNat : ℚ) + f) * 10 ^ ze|
      ≤ ((zm.toNat : ℚ) + f) * 10 ^ ze * (10 / (2 ^ 63 - 18 : ℕ)) := by
  simp only
  have hzm_q_ge : (mantissaFloor : ℚ) ≤ (zm.toNat : ℚ) := by exact_mod_cast h_zm_ge
  have h10ze'_pos : (0 : ℚ) < 10 ^ ze := zpow_pos (by norm_num) _
  have h10ze'_nn : (0 : ℚ) ≤ 10 ^ ze := le_of_lt h10ze'_pos
  have h_denom_val : (((2 ^ 63 - 18 : ℕ) : ℚ)) = 9223372036854775790 := by
    push_cast; norm_num
  have h_denom_pos : (0 : ℚ) < ((2 ^ 63 - 18 : ℕ) : ℚ) := by
    rw [h_denom_val]; norm_num
  -- No round-down. Value = zm * 10^ze. Error = -f * 10^ze.
  have h_val := doRoundDown_value_downward_truncate g false zm ze h_no_rd h_ze_gt
  simp only at h_val
  rw [h_val]
  have h_diff : (zm.toNat : ℚ) * 10 ^ ze - ((zm.toNat : ℚ) + f) * 10 ^ ze = (-f) * 10 ^ ze := by
    ring
  rw [h_diff, abs_mul, abs_of_nonneg h10ze'_nn]
  have h_abs : |(-f : ℚ)| = f := by
    rw [abs_neg, abs_of_nonneg hf_nn]
  rw [h_abs]
  have h_final : f ≤ ((zm.toNat : ℚ) + f) * (10 / ((2 ^ 63 - 18 : ℕ) : ℚ)) := by
    rw [h_denom_val]
    rw [show ((zm.toNat : ℚ) + f) * (10 / 9223372036854775790)
          = 10 * ((zm.toNat : ℚ) + f) / 9223372036854775790 by ring]
    rw [le_div_iff₀ (by norm_num : (0 : ℚ) < 9223372036854775790)]
    nlinarith [hf_nn, hf_lt, hzm_q_ge]
  calc f * 10 ^ ze
      ≤ ((zm.toNat : ℚ) + f) * (10 / ((2 ^ 63 - 18 : ℕ) : ℚ)) * 10 ^ ze :=
        mul_le_mul_of_nonneg_right h_final h10ze'_nn
    _ = ((zm.toNat : ℚ) + f) * 10 ^ ze * (10 / ((2 ^ 63 - 18 : ℕ) : ℚ)) := by ring

/-- `.upward` mode `_supTight_of_bounds`: variant taking `(hf_nn, hf_lt)` and
framing truth as `(zm + f) * 10^ze`. Requires `¬ g.shouldRoundUp_upward`. -/
lemma doRoundDown_rounds_upward_supTight_of_bounds
    (g : Guard) (zm : UInt64) (ze : Int) (f : ℚ)
    (hf_nn : 0 ≤ f) (hf_lt : f < 1)
    (h_no_rd : ¬ g.shouldRoundUp_upward)
    (h_zm_ge : (mantissaFloor : ℕ) ≤ zm.toNat)
    (_h_zm_le_max : zm.toNat ≤ maxRep.toNat)
    (h_ze_gt : minExponent < ze) :
    let res := g.doRoundDown false zm ze largeRange.min .upward
    |(res.mantissa_.toNat : ℚ) * 10 ^ res.exponent_ -
       ((zm.toNat : ℚ) + f) * 10 ^ ze|
      ≤ ((zm.toNat : ℚ) + f) * 10 ^ ze * (10 / (2 ^ 63 - 18 : ℕ)) := by
  simp only
  have hzm_q_ge : (mantissaFloor : ℚ) ≤ (zm.toNat : ℚ) := by exact_mod_cast h_zm_ge
  have h10ze'_pos : (0 : ℚ) < 10 ^ ze := zpow_pos (by norm_num) _
  have h10ze'_nn : (0 : ℚ) ≤ 10 ^ ze := le_of_lt h10ze'_pos
  have h_denom_val : (((2 ^ 63 - 18 : ℕ) : ℚ)) = 9223372036854775790 := by
    push_cast; norm_num
  have h_denom_pos : (0 : ℚ) < ((2 ^ 63 - 18 : ℕ) : ℚ) := by
    rw [h_denom_val]; norm_num
  have h_val := doRoundDown_value_upward_truncate g false zm ze h_no_rd h_ze_gt
  simp only at h_val
  rw [h_val]
  have h_diff : (zm.toNat : ℚ) * 10 ^ ze - ((zm.toNat : ℚ) + f) * 10 ^ ze = (-f) * 10 ^ ze := by
    ring
  rw [h_diff, abs_mul, abs_of_nonneg h10ze'_nn]
  have h_abs : |(-f : ℚ)| = f := by
    rw [abs_neg, abs_of_nonneg hf_nn]
  rw [h_abs]
  have h_final : f ≤ ((zm.toNat : ℚ) + f) * (10 / ((2 ^ 63 - 18 : ℕ) : ℚ)) := by
    rw [h_denom_val]
    rw [show ((zm.toNat : ℚ) + f) * (10 / 9223372036854775790)
          = 10 * ((zm.toNat : ℚ) + f) / 9223372036854775790 by ring]
    rw [le_div_iff₀ (by norm_num : (0 : ℚ) < 9223372036854775790)]
    nlinarith [hf_nn, hf_lt, hzm_q_ge]
  calc f * 10 ^ ze
      ≤ ((zm.toNat : ℚ) + f) * (10 / ((2 ^ 63 - 18 : ℕ) : ℚ)) * 10 ^ ze :=
        mul_le_mul_of_nonneg_right h_final h10ze'_nn
    _ = ((zm.toNat : ℚ) + f) * 10 ^ ze * (10 / ((2 ^ 63 - 18 : ℕ) : ℚ)) := by ring

/-- `.towards_zero` mode `_supTight_of_bounds`: variant taking `(hf_nn, hf_lt)` and
framing truth as `(zm + f) * 10^ze`. Always truncates (no rd-consistency needed). -/
lemma doRoundDown_rounds_towards_zero_supTight_of_bounds
    (g : Guard) (zm : UInt64) (ze : Int) (f : ℚ)
    (hf_nn : 0 ≤ f) (hf_lt : f < 1)
    (h_zm_ge : (mantissaFloor : ℕ) ≤ zm.toNat)
    (_h_zm_le_max : zm.toNat ≤ maxRep.toNat)
    (h_ze_gt : minExponent < ze) :
    let res := g.doRoundDown false zm ze largeRange.min .towards_zero
    |(res.mantissa_.toNat : ℚ) * 10 ^ res.exponent_ -
       ((zm.toNat : ℚ) + f) * 10 ^ ze|
      ≤ ((zm.toNat : ℚ) + f) * 10 ^ ze * (10 / (2 ^ 63 - 18 : ℕ)) := by
  simp only
  have hzm_q_ge : (mantissaFloor : ℚ) ≤ (zm.toNat : ℚ) := by exact_mod_cast h_zm_ge
  have h10ze'_pos : (0 : ℚ) < 10 ^ ze := zpow_pos (by norm_num) _
  have h10ze'_nn : (0 : ℚ) ≤ 10 ^ ze := le_of_lt h10ze'_pos
  have h_denom_val : (((2 ^ 63 - 18 : ℕ) : ℚ)) = 9223372036854775790 := by
    push_cast; norm_num
  have h_denom_pos : (0 : ℚ) < ((2 ^ 63 - 18 : ℕ) : ℚ) := by
    rw [h_denom_val]; norm_num
  have h_val := doRoundDown_value_towards_zero_truncate g false zm ze h_ze_gt
  simp only at h_val
  rw [h_val]
  have h_diff : (zm.toNat : ℚ) * 10 ^ ze - ((zm.toNat : ℚ) + f) * 10 ^ ze = (-f) * 10 ^ ze := by
    ring
  rw [h_diff, abs_mul, abs_of_nonneg h10ze'_nn]
  have h_abs : |(-f : ℚ)| = f := by
    rw [abs_neg, abs_of_nonneg hf_nn]
  rw [h_abs]
  have h_final : f ≤ ((zm.toNat : ℚ) + f) * (10 / ((2 ^ 63 - 18 : ℕ) : ℚ)) := by
    rw [h_denom_val]
    rw [show ((zm.toNat : ℚ) + f) * (10 / 9223372036854775790)
          = 10 * ((zm.toNat : ℚ) + f) / 9223372036854775790 by ring]
    rw [le_div_iff₀ (by norm_num : (0 : ℚ) < 9223372036854775790)]
    nlinarith [hf_nn, hf_lt, hzm_q_ge]
  calc f * 10 ^ ze
      ≤ ((zm.toNat : ℚ) + f) * (10 / ((2 ^ 63 - 18 : ℕ) : ℚ)) * 10 ^ ze :=
        mul_le_mul_of_nonneg_right h_final h10ze'_nn
    _ = ((zm.toNat : ℚ) + f) * 10 ^ ze * (10 / ((2 ^ 63 - 18 : ℕ) : ℚ)) := by ring

/-! ## `_supTight_of_bounds` for `.to_nearest` mode

This is the `.to_nearest` analog of the directed-mode `_of_bounds` lemmas. It
takes `(hf_nn, hf_lt)` and explicit round-vs-f consistency hypotheses, instead
of `represents g f`. The output bound is the same supremum-tight `5/(2^63-3)`.

The framing matches the diff-sign chain: truth as `(zm - f) * 10^ze`, where
`f ∈ [0, 1)` is the unit-interval residue. Designed to be applicable from the
algorithmic chain when the residue can be brought into `[0, 1)` form.

The round-consistency hypotheses replace `round_correct (represents g f)`:
they assert that `g.round .to_nearest` matches `f` vs `1/2`. -/
lemma doRoundDown_rounds_to_nearest_supTight_of_bounds (g : Guard) (zm : UInt64) (ze : Int) (f : ℚ)
    (hf_nn : 0 ≤ f) (hf_lt : f < 1)
    (h_round_up_iff : g.round .to_nearest = 1 ↔ f > 1 / 2)
    (h_round_zero_iff : g.round .to_nearest = 0 ↔ f = 1 / 2)
    (h_round_down_iff : g.round .to_nearest = -1 ↔ f < 1 / 2)
    (h_zm_ge : (mantissaFloor : ℕ) ≤ zm.toNat)
    (_h_zm_le_max : zm.toNat ≤ maxRep.toNat)
    (h_ze_gt : minExponent < ze)
    (h_floor_constraint : zm.toNat = mantissaFloor → f ≤ (2 : ℚ) / 10) :
    let res := g.doRoundDown false zm ze largeRange.min .to_nearest
    |(res.mantissa_.toNat : ℚ) * 10 ^ res.exponent_ -
       ((zm.toNat : ℚ) - f) * 10 ^ ze|
      ≤ ((zm.toNat : ℚ) - f) * 10 ^ ze * (5 / (2 ^ 63 - 3 : ℕ)) := by
  simp only
  have hzm_q_ge : (mantissaFloor : ℚ) ≤ (zm.toNat : ℚ) := by exact_mod_cast h_zm_ge
  have h10ze'_pos : (0 : ℚ) < 10 ^ ze := zpow_pos (by norm_num) _
  have h10ze'_nn : (0 : ℚ) ≤ 10 ^ ze := le_of_lt h10ze'_pos
  have h_denom_val : (((2 ^ 63 - 3 : ℕ) : ℚ)) = 9223372036854775805 := by
    push_cast; norm_num
  have h_denom_pos : (0 : ℚ) < ((2 ^ 63 - 3 : ℕ) : ℚ) := by
    rw [h_denom_val]; norm_num
  have h_zm_ge_lr : (100000000000000001 : ℕ) ≤ zm.toNat := by omega
  have h_round_values : g.round .to_nearest = 1 ∨ g.round .to_nearest = 0
      ∨ g.round .to_nearest = -1 := by
    unfold Guard.round
    by_cases h1 : g.digits_ > 0x5000_0000_0000_0000
    · left; rw [if_pos h1]
    · rw [if_neg h1]
      by_cases h2 : g.digits_ < 0x5000_0000_0000_0000
      · right; right; rw [if_pos h2]
      · rw [if_neg h2]
        by_cases h3 : g.xbit_ = true
        · left; rw [if_pos h3]
        · right; left; rw [if_neg h3]
  by_cases h_floor : zm.toNat = mantissaFloor
  · -- floor case: f ≤ 2/10
    have hf_le : f ≤ (2 : ℚ) / 10 := h_floor_constraint h_floor
    have hzm_q_eq_floor : (zm.toNat : ℚ) = mantissaFloor := by
      rw [h_floor]; norm_num
    -- f ≤ 2/10 < 1/2 → round = -1 (no round-down)
    have h_down : g.round .to_nearest = -1 := by
      rcases h_round_values with h1 | h2 | h3
      · have : f > 1/2 := h_round_up_iff.mp h1; linarith
      · have : f = 1/2 := h_round_zero_iff.mp h2; linarith
      · exact h3
    have h_no_rd : ((g.round .to_nearest == 1) || ((g.round .to_nearest == 0) && (zm % 2 == 1))) = false := by
      rw [h_down]; rfl
    have h_val := doRoundDown_value_noRoundDown g false zm ze .to_nearest h_no_rd h_ze_gt
    simp only at h_val
    rw [h_val, hzm_q_eq_floor]
    have h_diff : (mantissaFloor : ℚ) * 10 ^ ze - (mantissaFloor - f) * 10 ^ ze
        = f * 10 ^ ze := by ring
    rw [h_diff, abs_mul, abs_of_nonneg h10ze'_nn]
    have h_abs : |f| = f := abs_of_nonneg hf_nn
    rw [h_abs]
    have h_final : f ≤ (mantissaFloor - f) * (5 / ((2 ^ 63 - 3 : ℕ) : ℚ)) := by
      rw [h_denom_val]
      rw [show ((mantissaFloor : ℚ) - f) * (5 / 9223372036854775805)
            = 5 * ((mantissaFloor : ℚ) - f) / 9223372036854775805 by ring]
      rw [le_div_iff₀ (by norm_num : (0 : ℚ) < 9223372036854775805)]
      nlinarith [hf_nn, hf_le]
    calc f * 10 ^ ze
        ≤ (mantissaFloor - f) * (5 / ((2 ^ 63 - 3 : ℕ) : ℚ)) * 10 ^ ze :=
          mul_le_mul_of_nonneg_right h_final h10ze'_nn
      _ = (mantissaFloor - f) * 10 ^ ze * (5 / ((2 ^ 63 - 3 : ℕ) : ℚ)) := by ring
  · -- non-floor: zm.toNat ≥ mantissaFloorSucc
    have h_zm_gt : mantissaFloorSucc ≤ zm.toNat := by omega
    have hzm_q_gt : (mantissaFloorSucc : ℚ) ≤ (zm.toNat : ℚ) := by exact_mod_cast h_zm_gt
    rcases h_round_values with h_up | h_tie | h_down
    · -- Round = 1: roundDown fires. Output = (zm - 1) * 10^ze.
      have hf_gt : f > 1/2 := h_round_up_iff.mp h_up
      have h_rd : ((g.round .to_nearest == 1) || ((g.round .to_nearest == 0) && (zm % 2 == 1))) = true := by
        rw [h_up]; rfl
      have h_val := doRoundDown_value_roundDown g false zm ze .to_nearest h_rd h_zm_ge_lr h_ze_gt
      simp only at h_val
      rw [h_val]
      have h_diff : ((zm.toNat : ℚ) - 1) * 10 ^ ze - ((zm.toNat : ℚ) - f) * 10 ^ ze
          = (f - 1) * 10 ^ ze := by ring
      rw [h_diff, abs_mul, abs_of_nonneg h10ze'_nn]
      have h_abs : |(f - 1 : ℚ)| = 1 - f := by
        rw [abs_of_nonpos (by linarith : f - 1 ≤ 0)]; ring
      rw [h_abs]
      have h_final : (1 - f) ≤ ((zm.toNat : ℚ) - f) * (5 / ((2 ^ 63 - 3 : ℕ) : ℚ)) := by
        rw [h_denom_val]
        rw [show ((zm.toNat : ℚ) - f) * (5 / 9223372036854775805)
              = 5 * ((zm.toNat : ℚ) - f) / 9223372036854775805 by ring]
        rw [le_div_iff₀ (by norm_num : (0 : ℚ) < 9223372036854775805)]
        nlinarith [hf_gt, hf_lt, hzm_q_gt]
      calc (1 - f) * 10 ^ ze
          ≤ ((zm.toNat : ℚ) - f) * (5 / ((2 ^ 63 - 3 : ℕ) : ℚ)) * 10 ^ ze :=
            mul_le_mul_of_nonneg_right h_final h10ze'_nn
        _ = ((zm.toNat : ℚ) - f) * 10 ^ ze * (5 / ((2 ^ 63 - 3 : ℕ) : ℚ)) := by ring
    · -- Round = 0 (tie): f = 1/2.
      have hf_eq : f = 1/2 := h_round_zero_iff.mp h_tie
      by_cases h_odd : zm % 2 = 1
      · have h_rd : ((g.round .to_nearest == 1) || ((g.round .to_nearest == 0) && (zm % 2 == 1))) = true := by
          rw [h_tie]; simp [h_odd]
        have h_val := doRoundDown_value_roundDown g false zm ze .to_nearest h_rd h_zm_ge_lr h_ze_gt
        simp only at h_val
        rw [h_val, hf_eq]
        have h_diff : ((zm.toNat : ℚ) - 1) * 10 ^ ze - ((zm.toNat : ℚ) - 1/2) * 10 ^ ze
            = -(1/2) * 10 ^ ze := by ring
        rw [h_diff, abs_mul, abs_of_nonneg h10ze'_nn]
        have h_abs : |(-(1/2) : ℚ)| = 1/2 := by norm_num
        rw [h_abs]
        have h_final : (1/2 : ℚ) ≤ ((zm.toNat : ℚ) - 1/2) * (5 / ((2 ^ 63 - 3 : ℕ) : ℚ)) := by
          rw [h_denom_val]
          rw [show ((zm.toNat : ℚ) - 1/2) * (5 / 9223372036854775805)
                = 5 * ((zm.toNat : ℚ) - 1/2) / 9223372036854775805 by ring]
          rw [le_div_iff₀ (by norm_num : (0 : ℚ) < 9223372036854775805)]
          linarith [hzm_q_gt]
        calc (1/2 : ℚ) * 10 ^ ze
            ≤ ((zm.toNat : ℚ) - 1/2) * (5 / ((2 ^ 63 - 3 : ℕ) : ℚ)) * 10 ^ ze :=
              mul_le_mul_of_nonneg_right h_final h10ze'_nn
          _ = ((zm.toNat : ℚ) - 1/2) * 10 ^ ze * (5 / ((2 ^ 63 - 3 : ℕ) : ℚ)) := by ring
      · have h_zm_even : zm.toNat % 2 = 0 := by
          have hne : (zm % 2).toNat ≠ (1 : UInt64).toNat := by
            intro heq; exact h_odd (UInt64.toNat_inj.mp heq)
          have hmod_nat : (zm % 2).toNat = zm.toNat % (2 : UInt64).toNat := by
            rw [UInt64.toNat_mod]
          have h2_nat : (2 : UInt64).toNat = 2 := rfl
          have h1_nat : (1 : UInt64).toNat = 1 := rfl
          rw [hmod_nat, h2_nat, h1_nat] at hne
          have hmod_lt : zm.toNat % 2 < 2 := Nat.mod_lt _ (by norm_num)
          omega
        have h_zm_ge_even : 922337203685477582 ≤ zm.toNat := by
          have h_odd_lb : mantissaFloorSucc % 2 = 1 := by norm_num
          omega
        have hzm_q_ge_even : (922337203685477582 : ℚ) ≤ (zm.toNat : ℚ) := by
          exact_mod_cast h_zm_ge_even
        have h_no_rd : ((g.round .to_nearest == 1) || ((g.round .to_nearest == 0) && (zm % 2 == 1))) = false := by
          rw [h_tie]
          have : (zm % 2 == 1) = false := by
            apply Bool.eq_false_iff.mpr
            intro h; exact h_odd (beq_iff_eq.mp h)
          simp [this]
        have h_val := doRoundDown_value_noRoundDown g false zm ze .to_nearest h_no_rd h_ze_gt
        simp only at h_val
        rw [h_val, hf_eq]
        have h_diff : (zm.toNat : ℚ) * 10 ^ ze - ((zm.toNat : ℚ) - 1/2) * 10 ^ ze
            = (1/2) * 10 ^ ze := by ring
        rw [h_diff, abs_mul, abs_of_nonneg h10ze'_nn]
        have h_abs : |((1/2) : ℚ)| = 1/2 := by norm_num
        rw [h_abs]
        have h_final : (1/2 : ℚ) ≤ ((zm.toNat : ℚ) - 1/2) * (5 / ((2 ^ 63 - 3 : ℕ) : ℚ)) := by
          rw [h_denom_val]
          rw [show ((zm.toNat : ℚ) - 1/2) * (5 / 9223372036854775805)
                = 5 * ((zm.toNat : ℚ) - 1/2) / 9223372036854775805 by ring]
          rw [le_div_iff₀ (by norm_num : (0 : ℚ) < 9223372036854775805)]
          linarith [hzm_q_ge_even]
        calc (1/2 : ℚ) * 10 ^ ze
            ≤ ((zm.toNat : ℚ) - 1/2) * (5 / ((2 ^ 63 - 3 : ℕ) : ℚ)) * 10 ^ ze :=
              mul_le_mul_of_nonneg_right h_final h10ze'_nn
          _ = ((zm.toNat : ℚ) - 1/2) * 10 ^ ze * (5 / ((2 ^ 63 - 3 : ℕ) : ℚ)) := by ring
    · -- Round = -1: no round-down.
      have hf_lt_half : f < 1/2 := h_round_down_iff.mp h_down
      have h_no_rd : ((g.round .to_nearest == 1) || ((g.round .to_nearest == 0) && (zm % 2 == 1))) = false := by
        rw [h_down]; rfl
      have h_val := doRoundDown_value_noRoundDown g false zm ze .to_nearest h_no_rd h_ze_gt
      simp only at h_val
      rw [h_val]
      have h_diff : (zm.toNat : ℚ) * 10 ^ ze - ((zm.toNat : ℚ) - f) * 10 ^ ze
          = f * 10 ^ ze := by ring
      rw [h_diff, abs_mul, abs_of_nonneg h10ze'_nn]
      have h_abs : |f| = f := abs_of_nonneg hf_nn
      rw [h_abs]
      have h_final : f ≤ ((zm.toNat : ℚ) - f) * (5 / ((2 ^ 63 - 3 : ℕ) : ℚ)) := by
        rw [h_denom_val]
        rw [show ((zm.toNat : ℚ) - f) * (5 / 9223372036854775805)
              = 5 * ((zm.toNat : ℚ) - f) / 9223372036854775805 by ring]
        rw [le_div_iff₀ (by norm_num : (0 : ℚ) < 9223372036854775805)]
        nlinarith [hf_nn, hf_lt_half, hzm_q_gt]
      calc f * 10 ^ ze
          ≤ ((zm.toNat : ℚ) - f) * (5 / ((2 ^ 63 - 3 : ℕ) : ℚ)) * 10 ^ ze :=
            mul_le_mul_of_nonneg_right h_final h10ze'_nn
        _ = ((zm.toNat : ℚ) - f) * 10 ^ ze * (5 / ((2 ^ 63 - 3 : ℕ) : ℚ)) := by ring

end XRPL.Model.Protocol
