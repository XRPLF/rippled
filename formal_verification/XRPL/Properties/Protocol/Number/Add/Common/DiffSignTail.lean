import XRPL.Properties.Protocol.Number.Common.Notation
import XRPL.Properties.Protocol.Number.Add.Common.RecoverBasic
import XRPL.Properties.Protocol.Number.Common.Helpers
import XRPL.Properties.Protocol.Number.Rounding.DoRoundDown
import XRPL.Properties.Protocol.Number.Rounding.DoRoundUp
import XRPL.Properties.Protocol.Number.Rounding.Normalize

set_option linter.style.longLine false
set_option linter.style.emptyLine false

namespace XRPL.Model.Protocol

/-! ## Mode-independent helper lemmas for the diff-sign rounding-bound tail.

These lemmas describe the structure of `doRoundDown` / `doNormalize` on the
diff-sign addition tail. None of them depend on the rounding mode in a way that
matters (they are about sign-bit independence, exponent floors, mantissa ranges,
or the capAtMaxRep reduction), so they are shared across all four rounding modes. -/

/-- `doRoundDown`'s mantissa is independent of the sign bit `negative`. The sign
only affects the `negative_` field set by `bringIntoRange`; the mantissa/exponent
computation ignores it. -/
lemma doRoundDown_mantissa_sign_indep
    (g : Guard) (b : Bool) (m : UInt64) (e : Int) (minMant : UInt64) (mode : rounding_mode) :
    (g.doRoundDown b m e minMant mode).mantissa_
      = (g.doRoundDown false m e minMant mode).mantissa_ := by
  unfold Guard.doRoundDown Guard.bringIntoRange
  simp only []
  split_ifs <;> rfl

/-- `doRoundDown`'s exponent is independent of the sign bit `negative`. -/
lemma doRoundDown_exponent_sign_indep
    (g : Guard) (b : Bool) (m : UInt64) (e : Int) (minMant : UInt64) (mode : rounding_mode) :
    (g.doRoundDown b m e minMant mode).exponent_
      = (g.doRoundDown false m e minMant mode).exponent_ := by
  unfold Guard.doRoundDown Guard.bringIntoRange
  simp only []
  split_ifs <;> rfl

/-- `doNormalize` preserves the `negative_` flag when the output mantissa is
nonzero. The scaleUp/scaleDown/capAtMaxRep stages thread `negative` through the
guard's sign bit, and the final `doRoundUp` copies it to `negative_`. -/
lemma doNormalize_preserves_negative
    {negative : Bool} {m : UInt64} {e : Int} {minMant maxMant : UInt64}
    {mode : rounding_mode} {result : Number}
    (hok : doNormalize negative m e minMant maxMant mode = .ok result)
    (hres : result.mantissa_ ≠ 0) :
    result.negative_ = negative := by
  unfold doNormalize at hok
  by_cases hm0 : m == 0
  · rw [if_pos hm0] at hok
    rw [Except.ok.injEq] at hok
    rw [← hok] at hres
    exact absurd rfl hres
  · rw [if_neg hm0] at hok
    simp only at hok
    -- scaleUp result
    set se := doNormalize_scaleUp minMant m e with hse
    set g0 : Guard := if negative then Guard.new.set_negative else Guard.new with hg0
    -- scaleDown
    cases hsd : doNormalize_scaleDown maxMant se.1 se.2 g0 with
    | error err => rw [hsd] at hok; exact absurd hok (by intro h; cases h)
    | ok sd =>
      rw [hsd] at hok
      simp only at hok
      by_cases hcheck : (sd.2.1 < minExponent || sd.1 < minMant)
      · rw [if_pos hcheck] at hok
        rw [Except.ok.injEq] at hok
        rw [← hok] at hres
        exact absurd rfl hres
      · rw [if_neg hcheck] at hok
        cases hcap : doNormalize_capAtMaxRep sd.1 sd.2.1 sd.2.2 with
        | error err => rw [hcap] at hok; exact absurd hok (by intro h; cases h)
        | ok cp =>
          rw [hcap] at hok
          simp only at hok
          cases hru : cp.2.2.doRoundUp negative cp.1 cp.2.1 minMant maxMant mode "Number::normalize 2" with
          | error err => rw [hru] at hok; exact absurd hok (by intro h; cases h)
          | ok res =>
            rw [hru] at hok
            rw [Except.ok.injEq] at hok
            have hres_mant : res.mantissa_ ≠ 0 := by
              rw [← hok] at hres; exact hres
            have := doRoundUp_negative_of_mant_ne cp.2.2 negative cp.1 cp.2.1 minMant maxMant mode
              "Number::normalize 2" res hru hres_mant
            rw [← hok]; exact this

/-- If `doRoundDown` produces a nonzero mantissa, the input exponent is at least
`minExponent`. The bringIntoRange underflow branch sets both `mantissa_ := 0` and
`exponent_ := -2147483648`; a nonzero output rules it out, so the output (hence
input) exponent satisfies `minExponent ≤ ze` (output exponent is `ze` or `ze-1`,
but the underflow guard fires precisely when the *post-step* exponent `< minExponent`). -/
lemma doRoundDown_input_exp_ge_minExp_of_mant_ne
    (g : Guard) (zn : Bool) (zm : UInt64) (ze : Int) (minMant : UInt64) (mode : rounding_mode)
    (hne : (g.doRoundDown zn zm ze minMant mode).mantissa_ ≠ 0) :
    minExponent ≤ ze := by
  by_contra h
  push_neg at h
  apply hne
  unfold Guard.doRoundDown Guard.bringIntoRange
  simp only []
  -- In every branch the post-step exponent is ≤ ze < minExponent, so underflow fires.
  split_ifs with hrd hresc hunder hunder2 hunder3 hunder4 <;>
    first
    | rfl
    | (exfalso; omega)

/-- Output invariants for `doRoundDown` under the weaker lower bound
`floor ≤ zm` (floor = mantissaFloor < minMantissa). When `zm < minMantissa`
the bringIntoRange rescale fires (`zm * 10`), and `floor * 10 ≥ minMantissa`, so the
output mantissa is still in `[minMantissa, maxMantissa]`. Mode-independent: every
branch holds regardless of the round-down decision. -/
lemma doRoundDown_output_in_range_of_floor
    (g : Guard) (zn : Bool) (m : UInt64) (e : Int) (mode : rounding_mode)
    (h_lb : (mantissaFloor : ℕ) ≤ m.toNat)
    (h_ub : m.toNat ≤ maxRep.toNat)
    (h_e_gt : minExponent < e) :
    let res := g.doRoundDown zn m e largeRange.min mode
    largeRange.min.toNat ≤ res.mantissa_.toNat ∧
    res.mantissa_.toNat ≤ largeRange.max.toNat ∧
    minExponent ≤ res.exponent_ ∧
    (res.mantissa_.toNat > maxRep.toNat → res.mantissa_.toNat % 10 = 0) := by
  simp only
  have hmaxRep_v : maxRep.toNat = maxRepNat := maxRep_toNat_val
  have hminMant_v : largeRange.min.toNat = 1000000000000000000 := by decide
  have hmaxMant_v : largeRange.max.toNat = 9999999999999999999 := by decide
  have h_m_pos : 1 ≤ m.toNat := by omega
  have hsub : (m - 1).toNat = m.toNat - 1 := m_sub_one_no_underflow h_m_pos
  have h_no_under : ¬ e < minExponent := by omega
  have h_no_under_resc : ¬ e - 1 < minExponent := by omega
  by_cases h_rd : ((g.round mode == 1) || ((g.round mode == 0) && (m % 2 == 1))) = true
  · -- round-down fires: candidate mantissa is m - 1.
    by_cases h_m1_lt : (m - 1) < largeRange.min
    · -- rescale: ((m-1)*10, e-1).
      have h_m_le_min : m.toNat ≤ largeRange.min.toNat := by
        rw [UInt64.lt_iff_toNat_lt, hsub, hminMant_v] at h_m1_lt
        rw [hminMant_v]; omega
      have h_mul10_toNat : ((m - 1) * 10).toNat = (m.toNat - 1) * 10 :=
        m_sub_one_mul_ten_no_overflow h_m_pos h_m_le_min
      have h_mul10_not_lt : ¬ ((m - 1) * 10) < largeRange.min := by
        rw [UInt64.lt_iff_toNat_lt, h_mul10_toNat, hminMant_v]; omega
      have hres : g.doRoundDown zn m e largeRange.min mode =
          { negative_ := zn, mantissa_ := (m - 1) * 10, exponent_ := e - 1 } := by
        unfold Guard.doRoundDown
        simp only [h_rd, if_true, if_pos h_m1_lt]
        exact bringIntoRange_value_inRange zn ((m - 1) * 10) (e - 1) largeRange.min h_mul10_not_lt h_no_under_resc
      rw [hres]; simp only
      refine ⟨?_, ?_, by omega, ?_⟩
      · rw [h_mul10_toNat, hminMant_v]; omega
      · rw [h_mul10_toNat, hmaxMant_v]; omega
      · intro _; rw [h_mul10_toNat]; omega
    · -- no rescale: (m-1, e).
      have hres : g.doRoundDown zn m e largeRange.min mode =
          { negative_ := zn, mantissa_ := m - 1, exponent_ := e } := by
        unfold Guard.doRoundDown
        simp only [h_rd, if_true, if_neg h_m1_lt]
        exact bringIntoRange_value_inRange zn (m - 1) e largeRange.min h_m1_lt h_no_under
      rw [hres]; simp only
      have h_m1_ge : largeRange.min.toNat ≤ (m - 1).toNat := by
        rw [UInt64.lt_iff_toNat_lt] at h_m1_lt; omega
      refine ⟨h_m1_ge, ?_, by omega, ?_⟩
      · rw [hsub, hmaxMant_v]; omega
      · intro h_gt; rw [hsub] at h_gt ⊢; rw [hmaxRep_v] at h_gt; omega
  · -- no round-down: candidate mantissa is m.
    have h_rd_false : ((g.round mode == 1) || ((g.round mode == 0) && (m % 2 == 1))) = false := by
      rw [Bool.eq_false_iff]; exact h_rd
    by_cases h_m_lt : m < largeRange.min
    · -- rescale: (m*10, e-1).
      have h_m_lt_num : m.toNat < 1000000000000000000 := by
        rw [UInt64.lt_iff_toNat_lt, hminMant_v] at h_m_lt; exact h_m_lt
      have h_m10_toNat : (m * 10).toNat = m.toNat * 10 :=
        mul_ten_no_overflow_of_lt_lr_min h_m_lt
      have hres : g.doRoundDown zn m e largeRange.min mode =
          { negative_ := zn, mantissa_ := m * 10, exponent_ := e - 1 } := by
        unfold Guard.doRoundDown
        simp only [h_rd_false, Bool.false_eq_true, if_false]
        exact bringIntoRange_value_rescale zn m e largeRange.min h_m_lt h_no_under_resc
      rw [hres]; simp only
      refine ⟨?_, ?_, by omega, ?_⟩
      · rw [h_m10_toNat, hminMant_v]; omega
      · rw [h_m10_toNat, hmaxMant_v]; omega
      · intro _; rw [h_m10_toNat]; omega
    · -- no rescale: (m, e).
      have h_m_ge : largeRange.min.toNat ≤ m.toNat := by
        rw [UInt64.lt_iff_toNat_lt] at h_m_lt; omega
      have hres : g.doRoundDown zn m e largeRange.min mode =
          { negative_ := zn, mantissa_ := m, exponent_ := e } := by
        unfold Guard.doRoundDown
        simp only [h_rd_false, Bool.false_eq_true, if_false]
        exact bringIntoRange_value_inRange zn m e largeRange.min h_m_lt h_no_under
      rw [hres]; simp only
      refine ⟨h_m_ge, ?_, by omega, ?_⟩
      · rw [hmaxMant_v]; rw [hmaxRep_v] at h_ub; omega
      · intro h_gt; rw [hmaxRep_v] at h_gt h_ub; omega

/-- Reduction of `doNormalize` on the capAtMaxRep-firing path: when
`maxRep < m ≤ maxMantissa`, `minExponent ≤ e < maxExponent`, the scaleUp and
scaleDown stages are identities, and `doNormalize` reduces to a single
`doRoundUp` on `(m/10, e+1)` with guard `Guard.new(.set_negative).push (m%10)`.
Mode-independent: the capAtMaxRep path does not consult the round mode. -/
lemma doNormalize_capAtMaxRep_reduce
    (negative : Bool) (m : UInt64) (e : Int) (mode : rounding_mode)
    (h_gt : maxRep < m)
    (h_le_max : m.toNat ≤ largeRange.max.toNat)
    (hexp : minExponent ≤ e)
    (hexp_lt : e < maxExponent) :
    doNormalize negative m e largeRange.min largeRange.max mode
      = Except.map RoundResult.toNumber
          (((if negative then Guard.new.set_negative else Guard.new).push (m % 10)).doRoundUp
            negative (m / 10) (e + 1) largeRange.min largeRange.max mode "Number::normalize 2") := by
  unfold doNormalize
  have hm_ne_zero : m ≠ 0 := by
    intro h; rw [h] at h_gt; exact absurd h_gt (by decide)
  rw [beq_eq_false_iff_ne.mpr hm_ne_zero]
  simp only [Bool.false_eq_true, if_false]
  have h_min_le : largeRange.min ≤ m := by
    rw [UInt64.le_iff_toNat_le, largeRange_min_toNat]
    have := UInt64.lt_iff_toNat_lt.mp h_gt; rw [maxRep_toNat_val] at this; omega
  have h_max_le : m ≤ largeRange.max := UInt64.le_iff_toNat_le.mpr h_le_max
  rw [doNormalize_scaleUp_id largeRange.min m e h_min_le]
  rw [doNormalize_scaleDown_id largeRange.max m e _ h_max_le]
  simp only []
  have h_no_under_mant : ¬ m < largeRange.min := by
    rw [UInt64.lt_iff_toNat_lt]
    exact Nat.not_lt.mpr (UInt64.le_iff_toNat_le.mp h_min_le)
  have h_no_under_exp : ¬ e < minExponent := not_lt.mpr hexp
  have h_check_false : (e < minExponent || m < largeRange.min) = false := by
    simp [h_no_under_exp, h_no_under_mant]
  rw [h_check_false]
  simp only [Bool.false_eq_true, if_false]
  have h_mr_u : m > maxRep := h_gt
  have h_nge_exp : ¬ e ≥ maxExponent := not_le.mpr hexp_lt
  have h_cap_eq : doNormalize_capAtMaxRep m e (if negative then Guard.new.set_negative else Guard.new)
      = .ok (m / 10, e + 1, (if negative then Guard.new.set_negative else Guard.new).push (m % 10)) := by
    unfold doNormalize_capAtMaxRep divu10
    rw [if_pos h_mr_u, if_neg h_nge_exp]
  rw [h_cap_eq]
  simp only []
  cases hru : ((if negative then Guard.new.set_negative else Guard.new).push (m % 10)).doRoundUp
      negative (m / 10) (e + 1) largeRange.min largeRange.max mode "Number::normalize 2" with
  | error err => rfl
  | ok res => rfl

end XRPL.Model.Protocol
