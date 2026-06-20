import XRPL.Properties.Protocol.Number.Common.Notation
import Mathlib.Tactic

import XRPL.Properties.Protocol.Number.Common.Rounding.ScaleDown

namespace XRPL.Model.Protocol


/-! # `divQuotient128` correctness (staged form, PR 7389 head)

The staged division quotient `divQuotient128 xm ym xe ye` expands the numerator
by `10^17` (Stage 1), refines a nonzero remainder by a further `10^5`
(Stage 2 — total factor `10^22`), and reports whether any residual survives
(Stage 3, the `dropped` sticky flag).

The key property: `xm · 10^N = zm128 · ym + r` with `0 ≤ r < ym`, `N ∈ {17, 22}`,
`ze = xe - ye - N`, and `dropped = true ↔ r ≠ 0`. This means
`|x/y| = (zm128 + r/ym) · 10^ze` with the sticky flag exactly tracking `r ≠ 0`.
When the Stage-2 correction is zero (`N = 17` with `r ≠ 0` possible), the
residual is tiny: `r · 10^5 < ym`, i.e. `δ = r/ym < 10⁻⁵` — the
`doNormalize128` keystones consume this through their `δ·10^20 ≤ M` ratio
hypothesis.

## Kernel-checkability

The kernel's defeq blows its recursion guard whenever it must relate the full
`divQuotient128` application to a reduced form in one step (`simp`-unfolding,
`extract_lets`, whole-body `change`, and `.1/.2` projection forms all die).
Small steps are fine, so the proof navigates equationally: `unfold` (one
equation), then one single-let zeta `change` per `let` (head-zeta + syntactic
comparison each), then `rw [if_pos/if_neg]` (equations) and per-leaf `rfl`s
whose defeq is one `Prod` iota. -/

abbrev divQuotientTail (zmq : UInt128) (zeq : Int) (dropped : Bool) (N r : ℕ)
    (xm ym : UInt64) (xe ye : Int) : Prop :=
  (N = 17 ∨ N = 22) ∧
  xm.toNat * 10 ^ N = zmq.toNat * ym.toNat + r ∧
  r < ym.toNat ∧
  zeq = xe - ye - N ∧
  (dropped = true ↔ r ≠ 0) ∧
  (N = 17 → r * 10 ^ 5 < ym.toNat)

set_option maxHeartbeats 3200000 in
-- The correction path does UInt128 multiplications by 10^17/10^5; bounding them
-- below 2^128 requires the Euclidean-division chain across two stages. The
-- incremental zeta-expansion blocks add elaboration work on top.
/-- `divQuotient128` satisfies the Euclidean division property:
there exist `N ∈ {17, 22}` and a remainder `r < ym` such that
`xm * 10^N = zm128 * ym + r`, `ze = xe - ye - N`, the `dropped` flag is set
iff `r ≠ 0`, and in the `N = 17` case the residual is tiny (`r·10^5 < ym`).

The output is exposed through an explicit tuple equation; the witnesses are
the spelled-out branch values, and each branch equation closes by a small
`rfl` (one `Prod` iota), keeping every kernel defeq shallow. -/
theorem divQuotient128_correct (xm ym : UInt64) (xe ye : Int)
    (_hxm_pos : 0 < xm.toNat)
    (hym_pos : 0 < ym.toNat)
    (hxm_le : xm.toNat ≤ largeRange.max.toNat)
    (_hym_le : ym.toNat ≤ largeRange.max.toNat)
    (hym_min : largeRange.min.toNat ≤ ym.toNat) :
    ∃ (zmq : UInt128) (zeq : Int) (dropped : Bool) (N r : ℕ),
      divQuotient128 xm ym xe ye = (zmq, zeq, dropped) ∧
      divQuotientTail zmq zeq dropped N r xm ym xe ye := by
  -- Key constant values
  have hmax_val : largeRange.max.toNat = 10 ^ 19 - 1 := by decide
  have hmin_val : largeRange.min.toNat = 10 ^ 18 := by decide
  -- UInt128 <-> Nat bridge
  have hym_nat : (toUInt128 ym).toNat = ym.toNat := toNat_toUInt128 ym
  -- numerator doesn't overflow UInt128
  have hxm_bound : xm.toNat ≤ 10 ^ 19 - 1 := by omega
  have hnum_overflow : xm.toNat * 10 ^ 17 < 2 ^ 128 := by
    calc xm.toNat * 10 ^ 17 ≤ (10 ^ 19 - 1) * 10 ^ 17 := Nat.mul_le_mul_right _ hxm_bound
      _ < 2 ^ 128 := by norm_num
  have hnum_nat : (toUInt128 xm * (100000000000000000 : UInt128)).toNat
      = xm.toNat * 10 ^ 17 := by
    have hf : ((100000000000000000 : UInt128)).toNat = 10 ^ 17 := by decide
    rw [BitVec.toNat_mul_of_lt (by rw [toNat_toUInt128, hf]; exact hnum_overflow),
        toNat_toUInt128, hf]
  -- Nat-level Euclidean division
  have hzm_nat : (toUInt128 xm * (100000000000000000 : UInt128) / toUInt128 ym).toNat
      = (xm.toNat * 10 ^ 17) / ym.toNat := by
    rw [BitVec.toNat_udiv, hnum_nat, hym_nat]
  have hrem_nat : (toUInt128 xm * (100000000000000000 : UInt128) % toUInt128 ym).toNat
      = (xm.toNat * 10 ^ 17) % ym.toNat := by
    rw [BitVec.toNat_umod, hnum_nat, hym_nat]
  have heuc : ym.toNat * ((xm.toNat * 10 ^ 17) / ym.toNat)
      + (xm.toNat * 10 ^ 17) % ym.toNat = xm.toNat * 10 ^ 17 := Nat.div_add_mod _ _
  have hrem_lt : (xm.toNat * 10 ^ 17) % ym.toNat < ym.toNat := Nat.mod_lt _ hym_pos
  -- Bounds
  have hym_lower : 10 ^ 18 ≤ ym.toNat := by omega
  have hzm_bound : (xm.toNat * 10 ^ 17) / ym.toNat ≤ 999999999999999999 := by
    calc (xm.toNat * 10 ^ 17) / ym.toNat
        ≤ (xm.toNat * 10 ^ 17) / (10 ^ 18) := Nat.div_le_div_left hym_lower (by norm_num)
      _ ≤ ((10 ^ 19 - 1) * 10 ^ 17) / (10 ^ 18) :=
          Nat.div_le_div_right (Nat.mul_le_mul_right _ hxm_bound)
      _ = 999999999999999999 := by norm_num
  -- Unfold and zeta-expand the staged body, one let per step.
  unfold divQuotient128
  change ∃ (zmq : UInt128) (zeq : Int) (dropped : Bool) (N r : ℕ),
    (let ym128 := toUInt128 ym;
     let f : UInt128 := 100000000000000000;
     let fexp : Int := 17;
     let numerator := toUInt128 xm * f;
     let zm128 := numerator / ym128;
     let ze : Int := xe - ye - fexp;
     let remainder := numerator % ym128;
     if (remainder != 0) = true then
       let correctionFactor : UInt128 := 100000
       let partialNumerator := remainder * correctionFactor
       let correction := partialNumerator / ym128
       let (zm128, ze) :=
         if (correction != 0) = true then (zm128 * correctionFactor + correction, ze - 5)
         else (zm128, ze)
       let dropped := partialNumerator % ym128 != 0
       (zm128, ze, dropped)
     else (zm128, ze, false)) = (zmq, zeq, dropped) ∧
    divQuotientTail zmq zeq dropped N r xm ym xe ye
  change ∃ (zmq : UInt128) (zeq : Int) (dropped : Bool) (N r : ℕ),
    (let f : UInt128 := 100000000000000000;
     let fexp : Int := 17;
     let numerator := toUInt128 xm * f;
     let zm128 := numerator / toUInt128 ym;
     let ze : Int := xe - ye - fexp;
     let remainder := numerator % toUInt128 ym;
     if (remainder != 0) = true then
       let correctionFactor : UInt128 := 100000
       let partialNumerator := remainder * correctionFactor
       let correction := partialNumerator / toUInt128 ym
       let (zm128, ze) :=
         if (correction != 0) = true then (zm128 * correctionFactor + correction, ze - 5)
         else (zm128, ze)
       let dropped := partialNumerator % toUInt128 ym != 0
       (zm128, ze, dropped)
     else (zm128, ze, false)) = (zmq, zeq, dropped) ∧
    divQuotientTail zmq zeq dropped N r xm ym xe ye
  change ∃ (zmq : UInt128) (zeq : Int) (dropped : Bool) (N r : ℕ),
    (let fexp : Int := 17;
     let numerator := toUInt128 xm * (100000000000000000 : UInt128);
     let zm128 := numerator / toUInt128 ym;
     let ze : Int := xe - ye - fexp;
     let remainder := numerator % toUInt128 ym;
     if (remainder != 0) = true then
       let correctionFactor : UInt128 := 100000
       let partialNumerator := remainder * correctionFactor
       let correction := partialNumerator / toUInt128 ym
       let (zm128, ze) :=
         if (correction != 0) = true then (zm128 * correctionFactor + correction, ze - 5)
         else (zm128, ze)
       let dropped := partialNumerator % toUInt128 ym != 0
       (zm128, ze, dropped)
     else (zm128, ze, false)) = (zmq, zeq, dropped) ∧
    divQuotientTail zmq zeq dropped N r xm ym xe ye
  change ∃ (zmq : UInt128) (zeq : Int) (dropped : Bool) (N r : ℕ),
    (let numerator := toUInt128 xm * (100000000000000000 : UInt128);
     let zm128 := numerator / toUInt128 ym;
     let ze : Int := xe - ye - 17;
     let remainder := numerator % toUInt128 ym;
     if (remainder != 0) = true then
       let correctionFactor : UInt128 := 100000
       let partialNumerator := remainder * correctionFactor
       let correction := partialNumerator / toUInt128 ym
       let (zm128, ze) :=
         if (correction != 0) = true then (zm128 * correctionFactor + correction, ze - 5)
         else (zm128, ze)
       let dropped := partialNumerator % toUInt128 ym != 0
       (zm128, ze, dropped)
     else (zm128, ze, false)) = (zmq, zeq, dropped) ∧
    divQuotientTail zmq zeq dropped N r xm ym xe ye
  change ∃ (zmq : UInt128) (zeq : Int) (dropped : Bool) (N r : ℕ),
    (let zm128 := toUInt128 xm * (100000000000000000 : UInt128) / toUInt128 ym;
     let ze : Int := xe - ye - 17;
     let remainder := toUInt128 xm * (100000000000000000 : UInt128) % toUInt128 ym;
     if (remainder != 0) = true then
       let correctionFactor : UInt128 := 100000
       let partialNumerator := remainder * correctionFactor
       let correction := partialNumerator / toUInt128 ym
       let (zm128, ze) :=
         if (correction != 0) = true then (zm128 * correctionFactor + correction, ze - 5)
         else (zm128, ze)
       let dropped := partialNumerator % toUInt128 ym != 0
       (zm128, ze, dropped)
     else (zm128, ze, false)) = (zmq, zeq, dropped) ∧
    divQuotientTail zmq zeq dropped N r xm ym xe ye
  change ∃ (zmq : UInt128) (zeq : Int) (dropped : Bool) (N r : ℕ),
    (let ze : Int := xe - ye - 17;
     let remainder := toUInt128 xm * (100000000000000000 : UInt128) % toUInt128 ym;
     if (remainder != 0) = true then
       let correctionFactor : UInt128 := 100000
       let partialNumerator := remainder * correctionFactor
       let correction := partialNumerator / toUInt128 ym
       let (zm128, ze) :=
         if (correction != 0) = true then
           (toUInt128 xm * (100000000000000000 : UInt128) / toUInt128 ym * correctionFactor
              + correction, ze - 5)
         else (toUInt128 xm * (100000000000000000 : UInt128) / toUInt128 ym, ze)
       let dropped := partialNumerator % toUInt128 ym != 0
       (zm128, ze, dropped)
     else (toUInt128 xm * (100000000000000000 : UInt128) / toUInt128 ym, ze, false))
    = (zmq, zeq, dropped) ∧
    divQuotientTail zmq zeq dropped N r xm ym xe ye
  change ∃ (zmq : UInt128) (zeq : Int) (dropped : Bool) (N r : ℕ),
    (let remainder := toUInt128 xm * (100000000000000000 : UInt128) % toUInt128 ym;
     if (remainder != 0) = true then
       let correctionFactor : UInt128 := 100000
       let partialNumerator := remainder * correctionFactor
       let correction := partialNumerator / toUInt128 ym
       let (zm128, ze) :=
         if (correction != 0) = true then
           (toUInt128 xm * (100000000000000000 : UInt128) / toUInt128 ym * correctionFactor
              + correction, xe - ye - 17 - 5)
         else (toUInt128 xm * (100000000000000000 : UInt128) / toUInt128 ym, xe - ye - 17)
       let dropped := partialNumerator % toUInt128 ym != 0
       (zm128, ze, dropped)
     else (toUInt128 xm * (100000000000000000 : UInt128) / toUInt128 ym, xe - ye - 17, false))
    = (zmq, zeq, dropped) ∧
    divQuotientTail zmq zeq dropped N r xm ym xe ye
  change ∃ (zmq : UInt128) (zeq : Int) (dropped : Bool) (N r : ℕ),
    (if (toUInt128 xm * (100000000000000000 : UInt128) % toUInt128 ym != 0) = true then
       let correctionFactor : UInt128 := 100000
       let partialNumerator :=
         toUInt128 xm * (100000000000000000 : UInt128) % toUInt128 ym * correctionFactor
       let correction := partialNumerator / toUInt128 ym
       let (zm128, ze) :=
         if (correction != 0) = true then
           (toUInt128 xm * (100000000000000000 : UInt128) / toUInt128 ym * correctionFactor
              + correction, xe - ye - 17 - 5)
         else (toUInt128 xm * (100000000000000000 : UInt128) / toUInt128 ym, xe - ye - 17)
       let dropped := partialNumerator % toUInt128 ym != 0
       (zm128, ze, dropped)
     else (toUInt128 xm * (100000000000000000 : UInt128) / toUInt128 ym, xe - ye - 17, false))
    = (zmq, zeq, dropped) ∧
    divQuotientTail zmq zeq dropped N r xm ym xe ye
  -- Case split on the Stage-1 remainder.
  by_cases hrem : (toUInt128 xm * (100000000000000000 : UInt128) % toUInt128 ym != 0) = true
  · ---- Case: remainder != 0, Stage-2 refinement ----
    rw [if_pos hrem]
    have hrem_ne_nat : (xm.toNat * 10 ^ 17) % ym.toNat ≠ 0 := by
      intro h
      exact (bne_iff_ne.mp hrem) (by rw [BitVec.toNat_eq, hrem_nat, h]; rfl)
    have hcf_val : ((100000 : UInt128)).toNat = 10 ^ 5 := by decide
    -- Overflow bound for the partial numerator
    have hrem_bound : (xm.toNat * 10 ^ 17) % ym.toNat ≤ 10 ^ 19 - 2 := by omega
    have hpn_overflow : (xm.toNat * 10 ^ 17) % ym.toNat * 10 ^ 5 < 2 ^ 128 := by
      calc (xm.toNat * 10 ^ 17) % ym.toNat * 10 ^ 5 ≤ (10 ^ 19 - 2) * 10 ^ 5 :=
            Nat.mul_le_mul_right _ hrem_bound
        _ < 2 ^ 128 := by norm_num
    have hpn_nat : (toUInt128 xm * (100000000000000000 : UInt128) % toUInt128 ym
        * (100000 : UInt128)).toNat = (xm.toNat * 10 ^ 17) % ym.toNat * 10 ^ 5 := by
      rw [BitVec.toNat_mul_of_lt (by rw [hrem_nat, hcf_val]; exact hpn_overflow),
          hrem_nat, hcf_val]
    have hcorr_nat : (toUInt128 xm * (100000000000000000 : UInt128) % toUInt128 ym
        * (100000 : UInt128) / toUInt128 ym).toNat
        = (xm.toNat * 10 ^ 17) % ym.toNat * 10 ^ 5 / ym.toNat := by
      rw [BitVec.toNat_udiv, hpn_nat, hym_nat]
    have hmod_nat : (toUInt128 xm * (100000000000000000 : UInt128) % toUInt128 ym
        * (100000 : UInt128) % toUInt128 ym).toNat
        = (xm.toNat * 10 ^ 17) % ym.toNat * 10 ^ 5 % ym.toNat := by
      rw [BitVec.toNat_umod, hpn_nat, hym_nat]
    -- Zeta-expand the Stage-2 lets, one per step.
    change ∃ (zmq : UInt128) (zeq : Int) (dropped : Bool) (N r : ℕ),
      (let partialNumerator :=
         toUInt128 xm * (100000000000000000 : UInt128) % toUInt128 ym * (100000 : UInt128)
       let correction := partialNumerator / toUInt128 ym
       let (zm128, ze) :=
         if (correction != 0) = true then
           (toUInt128 xm * (100000000000000000 : UInt128) / toUInt128 ym * (100000 : UInt128)
              + correction, xe - ye - 17 - 5)
         else (toUInt128 xm * (100000000000000000 : UInt128) / toUInt128 ym, xe - ye - 17)
       let dropped := partialNumerator % toUInt128 ym != 0
       (zm128, ze, dropped)) = (zmq, zeq, dropped) ∧
      divQuotientTail zmq zeq dropped N r xm ym xe ye
    change ∃ (zmq : UInt128) (zeq : Int) (dropped : Bool) (N r : ℕ),
      (let correction :=
         toUInt128 xm * (100000000000000000 : UInt128) % toUInt128 ym * (100000 : UInt128)
           / toUInt128 ym
       let (zm128, ze) :=
         if (correction != 0) = true then
           (toUInt128 xm * (100000000000000000 : UInt128) / toUInt128 ym * (100000 : UInt128)
              + correction, xe - ye - 17 - 5)
         else (toUInt128 xm * (100000000000000000 : UInt128) / toUInt128 ym, xe - ye - 17)
       let dropped :=
         toUInt128 xm * (100000000000000000 : UInt128) % toUInt128 ym * (100000 : UInt128)
           % toUInt128 ym != 0
       (zm128, ze, dropped)) = (zmq, zeq, dropped) ∧
      divQuotientTail zmq zeq dropped N r xm ym xe ye
    change ∃ (zmq : UInt128) (zeq : Int) (dropped : Bool) (N r : ℕ),
      (((match
          if (toUInt128 xm * (100000000000000000 : UInt128) % toUInt128 ym * (100000 : UInt128)
                / toUInt128 ym != 0) = true then
            (toUInt128 xm * (100000000000000000 : UInt128) / toUInt128 ym * (100000 : UInt128)
               + toUInt128 xm * (100000000000000000 : UInt128) % toUInt128 ym
                   * (100000 : UInt128) / toUInt128 ym, xe - ye - 17 - 5)
          else (toUInt128 xm * (100000000000000000 : UInt128) / toUInt128 ym, xe - ye - 17) with
        | (zm128, ze) =>
            let dropped :=
              toUInt128 xm * (100000000000000000 : UInt128) % toUInt128 ym * (100000 : UInt128)
                % toUInt128 ym != 0
            (zm128, ze, dropped)) : UInt128 × Int × Bool)) = (zmq, zeq, dropped) ∧
      divQuotientTail zmq zeq dropped N r xm ym xe ye
    by_cases hcorr : (toUInt128 xm * (100000000000000000 : UInt128) % toUInt128 ym
        * (100000 : UInt128) / toUInt128 ym != 0) = true
    · ---- Sub-case: correction != 0, N = 22 ----
      rw [if_pos hcorr]
      -- The corrected quotient and its components stay below 2^128.
      have hcorr_bound : (xm.toNat * 10 ^ 17) % ym.toNat * 10 ^ 5 / ym.toNat < 10 ^ 5 := by
        apply Nat.div_lt_of_lt_mul
        calc (xm.toNat * 10 ^ 17) % ym.toNat * 10 ^ 5 < ym.toNat * 10 ^ 5 :=
              Nat.mul_lt_mul_of_pos_right hrem_lt (by norm_num : 0 < 10 ^ 5)
          _ = ym.toNat * (1 * 10 ^ 5) := by ring_nf
      have hzm_mul_overflow : (xm.toNat * 10 ^ 17) / ym.toNat * 10 ^ 5 < 2 ^ 128 := by
        calc (xm.toNat * 10 ^ 17) / ym.toNat * 10 ^ 5 ≤ 999999999999999999 * 10 ^ 5 :=
              Nat.mul_le_mul_right _ hzm_bound
          _ < 2 ^ 128 := by norm_num
      have hzm_mul_nat : (toUInt128 xm * (100000000000000000 : UInt128) / toUInt128 ym
          * (100000 : UInt128)).toNat = (xm.toNat * 10 ^ 17) / ym.toNat * 10 ^ 5 := by
        rw [BitVec.toNat_mul_of_lt (by rw [hzm_nat, hcf_val]; exact hzm_mul_overflow),
            hzm_nat, hcf_val]
      have hsum_overflow : (xm.toNat * 10 ^ 17) / ym.toNat * 10 ^ 5 +
          (xm.toNat * 10 ^ 17) % ym.toNat * 10 ^ 5 / ym.toNat < 2 ^ 128 := by
        calc (xm.toNat * 10 ^ 17) / ym.toNat * 10 ^ 5
              + (xm.toNat * 10 ^ 17) % ym.toNat * 10 ^ 5 / ym.toNat
            < (xm.toNat * 10 ^ 17) / ym.toNat * 10 ^ 5 + 10 ^ 5 := by omega
          _ ≤ 999999999999999999 * 10 ^ 5 + 10 ^ 5 := by
              apply Nat.add_le_add_right; exact Nat.mul_le_mul_right _ hzm_bound
          _ < 2 ^ 128 := by norm_num
      have hsum_nat : (toUInt128 xm * (100000000000000000 : UInt128) / toUInt128 ym
          * (100000 : UInt128)
          + toUInt128 xm * (100000000000000000 : UInt128) % toUInt128 ym * (100000 : UInt128)
              / toUInt128 ym).toNat =
          (xm.toNat * 10 ^ 17) / ym.toNat * 10 ^ 5
            + (xm.toNat * 10 ^ 17) % ym.toNat * 10 ^ 5 / ym.toNat := by
        rw [BitVec.toNat_add, hzm_mul_nat, hcorr_nat]
        exact Nat.mod_eq_of_lt hsum_overflow
      -- Second Euclidean division: remainder * 10^5 = q2 * ym + r2
      have heuc2 : ym.toNat * ((xm.toNat * 10 ^ 17) % ym.toNat * 10 ^ 5 / ym.toNat) +
          (xm.toNat * 10 ^ 17) % ym.toNat * 10 ^ 5 % ym.toNat
          = (xm.toNat * 10 ^ 17) % ym.toNat * 10 ^ 5 :=
        Nat.div_add_mod _ _
      refine ⟨toUInt128 xm * (100000000000000000 : UInt128) / toUInt128 ym * (100000 : UInt128)
                + toUInt128 xm * (100000000000000000 : UInt128) % toUInt128 ym
                    * (100000 : UInt128) / toUInt128 ym,
              xe - ye - 17 - 5,
              (toUInt128 xm * (100000000000000000 : UInt128) % toUInt128 ym * (100000 : UInt128)
                % toUInt128 ym != 0),
              22, (xm.toNat * 10 ^ 17) % ym.toNat * 10 ^ 5 % ym.toNat,
              rfl, Or.inr rfl, ?_, Nat.mod_lt _ hym_pos, ?_, ?_,
              by intro h; exact absurd h (by norm_num)⟩
      · -- Main equation: xm * 10^22 = zm_final * ym + r_final
        rw [hsum_nat, show (10 : ℕ) ^ 22 = 10 ^ 17 * 10 ^ 5 from by norm_num]
        nlinarith [heuc, heuc2]
      · -- Exponent: ze = xe - ye - 22
        push_cast; ring
      · -- Sticky flag: dropped ↔ r₂ ≠ 0
        constructor
        · intro h hr0
          exact (bne_iff_ne.mp h) (by rw [BitVec.toNat_eq, hmod_nat, hr0]; rfl)
        · intro hr
          rw [bne_iff_ne]
          intro h0
          apply hr
          rw [← hmod_nat, h0]
          rfl
    · ---- Sub-case: correction = 0, N = 17 with a tiny nonzero residual ----
      rw [if_neg hcorr]
      have hcorr_zero_nat : (xm.toNat * 10 ^ 17) % ym.toNat * 10 ^ 5 / ym.toNat = 0 := by
        have h0 : toUInt128 xm * (100000000000000000 : UInt128) % toUInt128 ym
            * (100000 : UInt128) / toUInt128 ym = 0 := by
          by_contra hne
          exact hcorr (bne_iff_ne.mpr hne)
        rw [← hcorr_nat, h0]
        rfl
      have hpn_lt : (xm.toNat * 10 ^ 17) % ym.toNat * 10 ^ 5 < ym.toNat := by
        by_contra hge
        push_neg at hge
        have := Nat.div_pos hge hym_pos
        omega
      refine ⟨toUInt128 xm * (100000000000000000 : UInt128) / toUInt128 ym, xe - ye - 17,
              (toUInt128 xm * (100000000000000000 : UInt128) % toUInt128 ym * (100000 : UInt128)
                % toUInt128 ym != 0),
              17, (xm.toNat * 10 ^ 17) % ym.toNat,
              rfl, Or.inl rfl, ?_, hrem_lt, ?_, ?_, fun _ => hpn_lt⟩
      · -- xm * 10^17 = zm_init * ym + remainder
        rw [hzm_nat]
        linarith [heuc]
      · -- Exponent: ze = xe - ye - 17
        push_cast; ring
      · -- Sticky flag: dropped = true and remainder ≠ 0
        have hdropped : (toUInt128 xm * (100000000000000000 : UInt128) % toUInt128 ym
            * (100000 : UInt128) % toUInt128 ym != 0) = true := by
          rw [bne_iff_ne]
          intro h0
          have hh : (toUInt128 xm * (100000000000000000 : UInt128) % toUInt128 ym
              * (100000 : UInt128) % toUInt128 ym).toNat = 0 := by rw [h0]; rfl
          rw [hmod_nat, Nat.mod_eq_of_lt hpn_lt] at hh
          omega
        exact ⟨fun _ => hrem_ne_nat, fun _ => hdropped⟩
  · ---- Case: remainder = 0, no refinement, N = 17 ----
    rw [if_neg hrem]
    have hrem_nat_zero : (xm.toNat * 10 ^ 17) % ym.toNat = 0 := by
      have h0 : toUInt128 xm * (100000000000000000 : UInt128) % toUInt128 ym = 0 := by
        by_contra hne
        exact hrem (bne_iff_ne.mpr hne)
      rw [← hrem_nat, h0]
      rfl
    refine ⟨toUInt128 xm * (100000000000000000 : UInt128) / toUInt128 ym, xe - ye - 17,
            false, 17, 0,
            rfl, Or.inl rfl, ?_, hym_pos, ?_, ?_, fun _ => by omega⟩
    · -- xm * 10^17 = zm_init * ym
      rw [hzm_nat, Nat.add_zero,
          Nat.mul_comm (xm.toNat * 10 ^ 17 / ym.toNat) ym.toNat]
      omega
    · -- Exponent: ze = xe - ye - 17
      push_cast; ring
    · -- Sticky flag: both sides false
      exact ⟨fun h => absurd h (by decide), fun h => absurd rfl h⟩

end XRPL.Model.Protocol
