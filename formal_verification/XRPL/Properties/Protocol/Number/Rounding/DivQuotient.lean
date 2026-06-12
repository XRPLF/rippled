import XRPL.Properties.Protocol.Number.Common.Notation
import Mathlib.Tactic

import XRPL.Properties.Protocol.Number.Rounding.ScaleDown

namespace XRPL.Model.Protocol

set_option linter.style.longLine false
set_option linter.style.emptyLine false
set_option linter.style.nativeDecide false

/-! # `divQuotient128` correctness

The division quotient `divQuotient128 xm ym xe ye` computes
`zm128 = floor(xm · 10^36 / ym)` (when the remainder correction fires) or
`zm128 = floor(xm · 10^19 / ym)` (when the remainder is zero), with
`ze = xe - ye - 36` or `ze = xe - ye - 19` respectively.

The key property: `xm · 10^N = zm128 · ym + r` where `0 ≤ r < ym` and `N ∈ {19, 36}`.
This means `truth = (zm128 + r/ym) · 10^ze`, giving a clean rational decomposition. -/

/-- The correction factor used in `divQuotient128` for large mantissas. -/
private def corrFactor : UInt128 := 100000000000000000  -- 10^17

private lemma corrFactor_val : corrFactor.toNat = 10 ^ 17 := by decide

/-- The initial shift factor for large mantissas in `divQuotient128`. -/
private def initFactor : UInt128 := tenPow19  -- 10^19

private lemma initFactor_val : initFactor.toNat = 10 ^ 19 := by decide

set_option maxHeartbeats 3200000 in
-- The correction path does three UInt128 multiplications by 10^17; bounding them
-- below 2^128 requires the Euclidean-division chain across two stages.
/-- `divQuotient128` for large mantissas satisfies the Euclidean division property:
there exists `N ∈ {19, 36}` and remainder `r < ym` such that
`xm * 10^N = zm128 * ym + r` and `ze = xe - ye - N`.

The statement uses `let result := ...` with `.1`/`.2` projections rather than
`let (zm128, ze) := ...` destructuring to avoid kernel deep recursion on the large
UInt128 literals inside `divQuotient128`.

The hypothesis `hym_min` (lower bound `largeRange.min ≤ ym`) is required because
the correction path multiplies intermediate UInt128 values by `10^17`. Without this bound,
the products can overflow `2^128`, breaking the Nat-level Euclidean division identity. -/
theorem divQuotient128_correct (xm ym : UInt64) (xe ye : Int)
    (_hxm_pos : 0 < xm.toNat)
    (hym_pos : 0 < ym.toNat)
    (hxm_le : xm.toNat ≤ largeRange.max.toNat)
    (_hym_le : ym.toNat ≤ largeRange.max.toNat)
    (hym_min : largeRange.min.toNat ≤ ym.toNat) :
    let result := divQuotient128 xm ym xe ye
    ∃ (N : ℕ) (r : ℕ),
      (N = 19 ∨ N = 36) ∧
      xm.toNat * 10 ^ N = result.1.toNat * ym.toNat + r ∧
      r < ym.toNat ∧
      result.2 = xe - ye - N ∧
      (N = 19 → r = 0) := by
  simp only [divQuotient128]
  set numerator := toUInt128 xm * (tenPow19 : UInt128) with hnum_def
  set ym128 := toUInt128 ym with hym128_def
  set zm_init := numerator / ym128 with hzm_def
  set remainder := numerator % ym128 with hrem_def
  -- Key constant values
  have hmax_val : largeRange.max.toNat = 10 ^ 19 - 1 := by decide
  have hmin_val : largeRange.min.toNat = 10 ^ 18 := by decide
  -- UInt128 <-> Nat bridge
  have hym_nat : ym128.toNat = ym.toNat := toNat_toUInt128 ym
  -- numerator doesn't overflow UInt128
  have hxm_bound : xm.toNat ≤ 10 ^ 19 - 1 := by omega
  have hnum_overflow : xm.toNat * 10 ^ 19 < 2 ^ 128 := by
    calc xm.toNat * 10 ^ 19 ≤ (10 ^ 19 - 1) * 10 ^ 19 := Nat.mul_le_mul_right _ hxm_bound
      _ < 2 ^ 128 := by norm_num
  have hnum_nat : numerator.toNat = xm.toNat * 10 ^ 19 := by
    rw [hnum_def]
    have hf : (tenPow19 : UInt128).toNat = 10 ^ 19 := by decide
    rw [BitVec.toNat_mul_of_lt (by rw [toNat_toUInt128, hf]; exact hnum_overflow),
        toNat_toUInt128, hf]
  -- Nat-level Euclidean division
  have hzm_nat : zm_init.toNat = (xm.toNat * 10 ^ 19) / ym.toNat := by
    rw [hzm_def, BitVec.toNat_udiv, hnum_nat, hym_nat]
  have hrem_nat : remainder.toNat = (xm.toNat * 10 ^ 19) % ym.toNat := by
    rw [hrem_def, BitVec.toNat_umod, hnum_nat, hym_nat]
  have heuc : ym.toNat * zm_init.toNat + remainder.toNat = xm.toNat * 10 ^ 19 := by
    rw [hzm_nat, hrem_nat]; exact Nat.div_add_mod _ _
  have hrem_lt : remainder.toNat < ym.toNat := by
    rw [hrem_nat]; exact Nat.mod_lt _ hym_pos
  -- Bounds
  have hym_lower : 10 ^ 18 ≤ ym.toNat := by omega
  have hzm_bound : zm_init.toNat ≤ 99999999999999999990 := by
    rw [hzm_nat]
    calc (xm.toNat * 10 ^ 19) / ym.toNat
        ≤ (xm.toNat * 10 ^ 19) / (10 ^ 18) := Nat.div_le_div_left hym_lower (by norm_num)
      _ ≤ ((10 ^ 19 - 1) * 10 ^ 19) / (10 ^ 18) :=
          Nat.div_le_div_right (Nat.mul_le_mul_right _ hxm_bound)
      _ = 99999999999999999990 := by norm_num
  -- Case split on remainder
  by_cases hrem : (remainder != (0 : UInt128)) = true
  · ---- Case: remainder != 0, correction path, N = 36 ----
    rw [if_pos hrem]
    have hcorr_val : (100000000000000000 : UInt128).toNat = 10 ^ 17 := by decide
    -- Overflow bounds for the correction multiplications
    have hzm_mul_overflow : zm_init.toNat * 10 ^ 17 < 2 ^ 128 := by
      calc zm_init.toNat * 10 ^ 17 ≤ 99999999999999999990 * 10 ^ 17 :=
            Nat.mul_le_mul_right _ hzm_bound
        _ < 2 ^ 128 := by norm_num
    have hrem_bound : remainder.toNat ≤ 10 ^ 19 - 2 := by omega
    have hrem_mul_overflow : remainder.toNat * 10 ^ 17 < 2 ^ 128 := by
      calc remainder.toNat * 10 ^ 17 ≤ (10 ^ 19 - 2) * 10 ^ 17 :=
            Nat.mul_le_mul_right _ hrem_bound
        _ < 2 ^ 128 := by norm_num
    -- Convert UInt128 products to Nat
    have hzm_mul_nat : (zm_init * (100000000000000000 : UInt128)).toNat =
        zm_init.toNat * 10 ^ 17 := by
      rw [BitVec.toNat_mul_of_lt (by rw [hcorr_val]; exact hzm_mul_overflow), hcorr_val]
    have hrem_mul_nat : (remainder * (100000000000000000 : UInt128)).toNat =
        remainder.toNat * 10 ^ 17 := by
      rw [BitVec.toNat_mul_of_lt (by rw [hcorr_val]; exact hrem_mul_overflow), hcorr_val]
    have hrem_div_nat : (remainder * (100000000000000000 : UInt128) / ym128).toNat =
        remainder.toNat * 10 ^ 17 / ym.toNat := by
      rw [BitVec.toNat_udiv, hrem_mul_nat, hym_nat]
    -- The corrected quotient doesn't overflow
    have hrem_div_bound : remainder.toNat * 10 ^ 17 / ym.toNat < 10 ^ 17 := by
      apply Nat.div_lt_of_lt_mul
      calc remainder.toNat * 10 ^ 17 < ym.toNat * 10 ^ 17 :=
            Nat.mul_lt_mul_of_pos_right hrem_lt (by norm_num : 0 < 10 ^ 17)
        _ = ym.toNat * (1 * 10 ^ 17) := by ring_nf
    have hsum_overflow : zm_init.toNat * 10 ^ 17 +
        remainder.toNat * 10 ^ 17 / ym.toNat < 2 ^ 128 := by
      calc zm_init.toNat * 10 ^ 17 + remainder.toNat * 10 ^ 17 / ym.toNat
          < zm_init.toNat * 10 ^ 17 + 10 ^ 17 := by omega
        _ ≤ 99999999999999999990 * 10 ^ 17 + 10 ^ 17 := by
            apply Nat.add_le_add_right; exact Nat.mul_le_mul_right _ hzm_bound
        _ < 2 ^ 128 := by norm_num
    -- Convert the UInt128 sum to Nat
    have hsum_nat : (zm_init * (100000000000000000 : UInt128) +
        remainder * (100000000000000000 : UInt128) / ym128).toNat =
        zm_init.toNat * 10 ^ 17 + remainder.toNat * 10 ^ 17 / ym.toNat := by
      rw [BitVec.toNat_add, hzm_mul_nat, hrem_div_nat]
      exact Nat.mod_eq_of_lt hsum_overflow
    -- Second Euclidean division: remainder * 10^17 = q2 * ym + r2
    have heuc2 : ym.toNat * (remainder.toNat * 10 ^ 17 / ym.toNat) +
        remainder.toNat * 10 ^ 17 % ym.toNat = remainder.toNat * 10 ^ 17 :=
      Nat.div_add_mod _ _
    -- Provide witnesses: N=36, r = remainder * 10^17 % ym
    refine ⟨36, remainder.toNat * 10 ^ 17 % ym.toNat, Or.inr rfl, ?_,
            Nat.mod_lt _ hym_pos, ?_, by intro h; exact absurd h (by norm_num)⟩
    · -- Main equation: xm * 10^36 = zm_final * ym + r_final
      change xm.toNat * 10 ^ 36 =
        (zm_init * (100000000000000000 : UInt128) +
          remainder * (100000000000000000 : UInt128) / ym128,
         xe - ye - 19 - 17).1.toNat * ym.toNat + remainder.toNat * 10 ^ 17 % ym.toNat
      rw [show (Prod.fst (zm_init * 100000000000000000 +
          remainder * 100000000000000000 / ym128, xe - ye - 19 - 17)) =
          zm_init * 100000000000000000 + remainder * 100000000000000000 / ym128 from rfl]
      rw [hsum_nat, show (10 : ℕ) ^ 36 = 10 ^ 19 * 10 ^ 17 from by norm_num]
      nlinarith [heuc, heuc2]
    · -- Exponent: ze = xe - ye - 36
      change (xe - ye - 19 - 17 : Int) = xe - ye - (36 : ℕ)
      push_cast; ring
  · ---- Case: remainder = 0, no correction, N = 19 ----
    rw [if_neg hrem]
    have hrem_zero : remainder = 0 := by
      simp only [bne_iff_ne, ne_eq, not_not] at hrem; exact hrem
    have hrem_nat_zero : remainder.toNat = 0 := by rw [hrem_zero]; rfl
    refine ⟨19, 0, Or.inl rfl, ?_, hym_pos, ?_, fun _ => rfl⟩
    · -- xm * 10^19 = zm_init * ym
      change xm.toNat * 10 ^ 19 = (zm_init, xe - ye - (19 : Int)).1.toNat * ym.toNat + 0
      rw [show (Prod.fst (zm_init, xe - ye - (19 : Int))) = zm_init from rfl, Nat.add_zero]
      linarith [heuc]
    · -- Exponent: ze = xe - ye - 19
      change (xe - ye - (19 : Int)) = xe - ye - (19 : ℕ)
      push_cast; ring

end XRPL.Model.Protocol
