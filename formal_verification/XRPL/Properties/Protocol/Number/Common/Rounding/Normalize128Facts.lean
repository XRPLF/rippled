import XRPL.Properties.Protocol.Number.Common.Notation
import XRPL.Properties.Protocol.Number.Common.ProofTactics
import Mathlib.Tactic

import XRPL.Properties.Protocol.Number.Common.Rounding.Normalize128
import XRPL.Properties.Protocol.Number.Normalize.Common.ResultFacts


namespace XRPL.Model.Protocol

/-! # Frame facts for `doNormalize128`

`doNormalize128` consumes a 128-bit mantissa `M` with a sticky tail `δ` and
ends in the same `(zm, ze', g) → doRoundUp → res.toNumber` frame as the other
pipelines. This file packages a successful run into the no-inbetween frame
inputs (with `f` the **true** residual fraction — the guard's represented
shadow `f̃` differs from it by the slip, so the decision-side facts are
exported through the shadow-transfer implications), plus the result-shape
facts (`isNormalized`, weak top-exponent mantissa cap) and the underflow
characterization. Division and diff-sign addition consume these. -/

/-- If the 128-bit scale-up exits below the minimum mantissa, it exhausted the
exponent range. -/
lemma doNormalize128_scaleUp_exit_exp (minMant : UInt64) (m : UInt128) (e : Int) :
    (doNormalize128.scaleUp minMant m e).1 < toUInt128 minMant →
    (doNormalize128.scaleUp minMant m e).2 ≤ minExponent := by
  induction m, e using doNormalize128.scaleUp.induct (minMantissa := minMant) with
  | case1 m e hcond IH =>
    rw [show doNormalize128.scaleUp minMant m e
        = doNormalize128.scaleUp minMant (m * 10) (e - 1) from by
      rw [doNormalize128.scaleUp.eq_def]
      simp only [if_pos hcond]]
    exact IH
  | case2 m e hcond =>
    rw [show doNormalize128.scaleUp minMant m e = (m, e) from by
      rw [doNormalize128.scaleUp.eq_def]
      simp only [if_neg hcond]]
    intro hlt
    by_contra hgt
    push_neg at hgt
    exact hcond ⟨hlt, hgt⟩

/-- A fired 128-bit scale-down exits at or above `(maxMantissa+1)/10`: with
`maxMantissa = largeRange.max = 10^19 − 1` the output is `≥ 10^18`. -/
lemma doNormalize_scaleDown128_fired_output_ge
    (m : UInt128) (e : Int) (g : Guard) (out : UInt128 × Int × Guard)
    (hgt : m > toUInt128 largeRange.max)
    (h : doNormalize_scaleDown128 largeRange.max m e g = .ok out) :
    1000000000000000000 ≤ out.1.toNat := by
  induction m, e, g using doNormalize_scaleDown128.induct (maxMantissa := largeRange.max)
    generalizing out with
  | case1 m e g hgt' hmax =>
    rw [show doNormalize_scaleDown128 largeRange.max m e g = .error "Number::normalize 1" from by
      rw [doNormalize_scaleDown128.eq_def]
      rw [dif_pos hgt', if_pos hmax]] at h
    exact absurd h (by intro hh; cases hh)
  | case2 m e g hgt' hnge ih =>
    rw [show doNormalize_scaleDown128 largeRange.max m e g
        = doNormalize_scaleDown128 largeRange.max (m / 10) (e + 1) (g.push (toUInt64 (m % 10)))
        from by
      rw [doNormalize_scaleDown128.eq_def]
      rw [dif_pos hgt', if_neg hnge]] at h
    by_cases hgt2 : m / 10 > toUInt128 largeRange.max
    · exact ih out hgt2 h
    · rw [show doNormalize_scaleDown128 largeRange.max (m / 10) (e + 1)
            (g.push (toUInt64 (m % 10)))
          = .ok (m / 10, e + 1, g.push (toUInt64 (m % 10))) from by
        rw [doNormalize_scaleDown128.eq_def]
        rw [dif_neg hgt2]] at h
      obtain rfl := (Except.ok.inj h).symm
      have h10 : ((10 : UInt128)).toNat = 10 := by decide
      have h_div : (m / 10 : UInt128).toNat = m.toNat / 10 := by
        rw [BitVec.toNat_udiv, h10]
      have hgt_nat : (toUInt128 largeRange.max).toNat < m.toNat := BitVec.lt_def.mp hgt'
      rw [toNat_toUInt128, largeRange_max_val] at hgt_nat
      change 1000000000000000000 ≤ (m / 10 : UInt128).toNat
      rw [h_div]
      omega
  | case3 m e g hgt' =>
    exact absurd hgt hgt'

/-- A successful 128-bit scale-down never exits above `maxMantissa`. -/
lemma doNormalize_scaleDown128_output_le
    (m : UInt128) (e : Int) (g : Guard) (out : UInt128 × Int × Guard)
    (h : doNormalize_scaleDown128 largeRange.max m e g = .ok out) :
    out.1.toNat ≤ 9999999999999999999 := by
  induction m, e, g using doNormalize_scaleDown128.induct (maxMantissa := largeRange.max)
    generalizing out with
  | case1 m e g hgt' hmax =>
    rw [show doNormalize_scaleDown128 largeRange.max m e g
        = .error "Number::normalize 1" from by
      rw [doNormalize_scaleDown128.eq_def]
      rw [dif_pos hgt', if_pos hmax]] at h
    exact absurd h (by intro hh; cases hh)
  | case2 m e g hgt' hnge ih =>
    rw [show doNormalize_scaleDown128 largeRange.max m e g
        = doNormalize_scaleDown128 largeRange.max (m / 10) (e + 1)
            (g.push (toUInt64 (m % 10))) from by
      rw [doNormalize_scaleDown128.eq_def]
      rw [dif_pos hgt', if_neg hnge]] at h
    exact ih out h
  | case3 m e g hgt' =>
    rw [show doNormalize_scaleDown128 largeRange.max m e g = .ok (m, e, g) from by
      rw [doNormalize_scaleDown128.eq_def]
      rw [dif_neg hgt']] at h
    obtain rfl := (Except.ok.inj h).symm
    have hle : m.toNat ≤ (toUInt128 largeRange.max).toNat := by
      by_contra hc
      push_neg at hc
      exact hgt' (BitVec.lt_def.mpr hc)
    rw [toNat_toUInt128, largeRange_max_val] at hle
    exact hle

set_option maxHeartbeats 1600000 in
-- Five-stage UInt128 pipeline walk with per-stage equational extraction.
/-- Value-free pipeline walk for `doNormalize128`: exposes the final
`doRoundUp` stage with the input-side bounds the result-shape lemmas need. -/
theorem doNormalize128_doRoundUp_stage
    (zn : Bool) (M : UInt128) (e : Int) (sticky : Bool) (mode : rounding_mode)
    (hM_pos : 1 ≤ M.toNat)
    (result : Number)
    (hok : doNormalize128 zn M e largeRange.min largeRange.max mode sticky = .ok result)
    (hres : result.mantissa_ ≠ 0) :
    ∃ (zm : UInt64) (ze' : Int) (g : Guard) (res : RoundResult),
      g.doRoundUp zn zm ze' largeRange.min largeRange.max mode
        "Number::normalize 2" = .ok res ∧
      result = res.toNumber ∧
      mantissaFloor ≤ zm.toNat ∧
      zm.toNat ≤ maxRepUp.toNat ∧
      (zm.toNat < 1000000000000000000 → ze' ≤ maxExponent) := by
  have hminM_v : largeRange.min.toNat = 1000000000000000000 := largeRange_min_val
  have hmaxM_v : largeRange.max.toNat = 9999999999999999999 := largeRange_max_val
  have hM_ne : ¬ (M == 0) = true := by
    intro h
    have : M = 0 := by exact_mod_cast beq_iff_eq.mp h
    rw [this] at hM_pos
    simp at hM_pos
  unfold doNormalize128 at hok
  rw [if_neg hM_ne] at hok
  simp only [] at hok
  rcases hsu : doNormalize128.scaleUp largeRange.min M e with ⟨M₁, e₁⟩
  rw [hsu] at hok
  simp only [] at hok
  set g₀ : Guard := (if sticky = true
      then (if zn then Guard.new.set_negative else Guard.new).set_sticky
      else (if zn then Guard.new.set_negative else Guard.new)) with hg₀_def
  cases hsd : doNormalize_scaleDown128 largeRange.max M₁ e₁ g₀ with
  | error err =>
    except_clash hsd hok
  | ok sd =>
    rw [hsd] at hok
    simp only [] at hok
    by_cases hund : (sd.2.1 < minExponent || sd.1 < toUInt128 largeRange.min) = true
    · rw [if_pos hund] at hok
      exfalso
      apply hres
      have h := Except.ok.inj hok
      rw [← h]
      rfl
    · rw [if_neg hund] at hok
      rw [Bool.not_eq_true, Bool.or_eq_false_iff] at hund
      obtain ⟨hund1, hund2⟩ := hund
      have hM₂_ge : 1000000000000000000 ≤ sd.1.toNat := by
        by_contra h
        push_neg at h
        apply absurd (decide_eq_true (show sd.1 < toUInt128 largeRange.min from by
          rw [BitVec.lt_def, toNat_toUInt128, hminM_v]; exact h))
        rw [hund2]; simp
      have hM₂_le : sd.1.toNat ≤ 9999999999999999999 :=
        doNormalize_scaleDown128_output_le M₁ e₁ g₀ sd hsd
      have hM₂_fit : sd.1.toNat < 2 ^ 64 := by omega
      have hM₂u_toNat : (toUInt64 sd.1).toNat = sd.1.toNat := toNat_toUInt64 hM₂_fit
      cases hcap : doNormalize_capAtMaxRep (toUInt64 sd.1) sd.2.1 sd.2.2 with
      | error err =>
        except_clash hcap hok
      | ok cp =>
        rw [hcap] at hok
        simp only [] at hok
        cases hru : cp.2.2.doRoundUp zn cp.1 cp.2.1 largeRange.min largeRange.max
            mode "Number::normalize 2" with
        | error err =>
          except_clash hru hok
        | ok res =>
          rw [hru] at hok
          have h_result : result = res.toNumber := (Except.ok.inj hok).symm
          refine ⟨cp.1, cp.2.1, cp.2.2, res, hru, h_result, ?_, ?_, ?_⟩
          · -- mantissaFloor ≤ cp.1.
            unfold doNormalize_capAtMaxRep at hcap
            by_cases hmr : toUInt64 sd.1 > maxRepUp
            · rw [if_pos hmr] at hcap
              by_cases hexp3 : sd.2.1 ≥ maxExponent
              · rw [if_pos hexp3] at hcap; exact absurd hcap (by intro h; cases h)
              · rw [if_neg hexp3] at hcap
                simp only [divu10] at hcap
                have hm3_eq : cp.1 = toUInt64 sd.1 / 10 :=
                  ((Prod.mk.inj (Except.ok.inj hcap)).1).symm
                rw [hm3_eq, UInt64.toNat_div, show (10 : UInt64).toNat = 10 from rfl,
                    hM₂u_toNat]
                have hmr_nat : maxRepUp.toNat < (toUInt64 sd.1).toNat :=
                  UInt64.lt_iff_toNat_lt.mp hmr
                rw [show maxRepUp.toNat = maxRepUpNat from rfl, hM₂u_toNat] at hmr_nat
                omega
            · rw [if_neg hmr] at hcap
              have hm3_eq : cp.1 = toUInt64 sd.1 :=
                ((Prod.mk.inj (Except.ok.inj hcap)).1).symm
              rw [hm3_eq, hM₂u_toNat]
              omega
          · -- cp.1 ≤ maxRepUp.
            unfold doNormalize_capAtMaxRep at hcap
            by_cases hmr : toUInt64 sd.1 > maxRepUp
            · rw [if_pos hmr] at hcap
              by_cases hexp3 : sd.2.1 ≥ maxExponent
              · rw [if_pos hexp3] at hcap; exact absurd hcap (by intro h; cases h)
              · rw [if_neg hexp3] at hcap
                simp only [divu10] at hcap
                have hm3_eq : cp.1 = toUInt64 sd.1 / 10 :=
                  ((Prod.mk.inj (Except.ok.inj hcap)).1).symm
                rw [hm3_eq, UInt64.toNat_div, show (10 : UInt64).toNat = 10 from rfl,
                    hM₂u_toNat, show maxRepUp.toNat = maxRepUpNat from rfl]
                omega
            · rw [if_neg hmr] at hcap
              have hm3_eq : cp.1 = toUInt64 sd.1 :=
                ((Prod.mk.inj (Except.ok.inj hcap)).1).symm
              have hmr_nat : ¬ maxRepUp.toNat < (toUInt64 sd.1).toNat := fun h =>
                hmr (UInt64.lt_iff_toNat_lt.mpr h)
              rw [hm3_eq]
              omega
          · -- cp.1 < 10^18 → cp.2.1 ≤ maxExponent.
            intro hm3_lt
            unfold doNormalize_capAtMaxRep at hcap
            by_cases hmr : toUInt64 sd.1 > maxRepUp
            · rw [if_pos hmr] at hcap
              by_cases hexp3 : sd.2.1 ≥ maxExponent
              · rw [if_pos hexp3] at hcap; exact absurd hcap (by intro h; cases h)
              · rw [if_neg hexp3] at hcap
                simp only [divu10] at hcap
                have he3_eq : cp.2.1 = sd.2.1 + 1 :=
                  ((Prod.mk.inj (Prod.mk.inj (Except.ok.inj hcap)).2).1).symm
                omega
            · rw [if_neg hmr] at hcap
              have hm3_eq : cp.1 = toUInt64 sd.1 :=
                ((Prod.mk.inj (Except.ok.inj hcap)).1).symm
              exfalso
              rw [hm3_eq, hM₂u_toNat] at hm3_lt
              omega

/-- The result of a successful `doNormalize128` with nonzero mantissa is
normalized. -/
theorem doNormalize128_result_isNormalized
    (zn : Bool) (M : UInt128) (e : Int) (sticky : Bool) (mode : rounding_mode)
    (hM_pos : 1 ≤ M.toNat)
    (result : Number)
    (hok : doNormalize128 zn M e largeRange.min largeRange.max mode sticky = .ok result)
    (hres : result.mantissa_ ≠ 0) :
    result.isNormalized := by
  obtain ⟨zm, ze', g, res, hru, h_result, hfloor, hle, _⟩ :=
    doNormalize128_doRoundUp_stage zn M e sticky mode hM_pos result hok hres
  have hres_ne : res.mantissa_ ≠ 0 := by
    rw [h_result] at hres
    exact hres
  have h_inv : largeRange.min.toNat ≤ res.mantissa_.toNat ∧
      res.mantissa_.toNat ≤ largeRange.max.toNat ∧
      minExponent ≤ res.exponent_ ∧
      (res.mantissa_.toNat > maxRep.toNat → res.mantissa_.toNat % 10 = 0) := by
    cases mode with
    | to_nearest =>
      exact doRoundUp_output_invariants_to_nearest_upTo_maxRepUp g zn zm ze'
        hfloor hle "Number::normalize 2" res hru hres_ne
    | towards_zero =>
      exact doRoundUp_output_invariants_towards_zero_upTo_maxRepUp g zn zm ze'
        hfloor hle "Number::normalize 2" res hru hres_ne
    | downward =>
      exact doRoundUp_output_invariants_downward_upTo_maxRepUp g zn zm ze'
        hfloor hle "Number::normalize 2" res hru hres_ne
    | upward =>
      exact doRoundUp_output_invariants_upward_upTo_maxRepUp g zn zm ze'
        hfloor hle "Number::normalize 2" res hru hres_ne
  obtain ⟨h1, h2, h3, h4⟩ := h_inv
  have h_exp_le : res.exponent_ ≤ maxExponent :=
    doRoundUp_exponent_le_max g zn zm ze' mode "Number::normalize 2" res hru
  right
  rw [h_result]
  refine ⟨UInt64.le_iff_toNat_le.mpr h1, UInt64.le_iff_toNat_le.mpr h2, ?_, h3, h_exp_le⟩
  by_cases hcase : res.mantissa_.toNat ≤ maxRep.toNat
  · left
    exact UInt64.le_iff_toNat_le.mpr hcase
  · right
    exact h4 (by omega)

/-- Weak top-exponent mantissa cap for `doNormalize128`. -/
theorem doNormalize128_no_overflow_mantissa
    (zn : Bool) (M : UInt128) (e : Int) (sticky : Bool) (mode : rounding_mode)
    (hM_pos : 1 ≤ M.toNat)
    (result : Number)
    (hok : doNormalize128 zn M e largeRange.min largeRange.max mode sticky = .ok result)
    (hres : result.mantissa_ ≠ 0)
    (h_exp_ge : result.exponent_ ≥ maxExponent) :
    result.mantissa_.toNat ≤ 9223372036854775820 := by
  obtain ⟨zm, ze', g, res, hru, h_result, _, hle, hcap_exp⟩ :=
    doNormalize128_doRoundUp_stage zn M e sticky mode hM_pos result hok hres
  rw [h_result] at h_exp_ge ⊢
  exact doRoundUp_mantissa_le_cuspTop_at_maxExp g zn zm ze' mode
    "Number::normalize 2" hle hcap_exp res hru h_exp_ge

set_option maxHeartbeats 12800000 in
-- Full value-laden walk: scaleUp facts + two slip-threaded repr stages over
-- giant UInt128 terms, with the shadow-transfer derivations on top.
/-- Frame decomposition for `doNormalize128` against the true value
`(M + δ)·10^e`. The exported fraction `f` is the **true** residual; the
guard's represented shadow `f̃` (which drives the round decision) is exported
alongside with the two transfer implications the no-inbetween frames need:
a positive shadow certifies a positive residual, and a zero shadow certifies
an exact residual. (With the sticky flag off the two coincide; with it on the
seeded tail keeps both strictly positive.) -/
theorem doNormalize128_algorithmic_facts
    (zn : Bool) (M : UInt128) (e : Int) (δ : ℚ) (sticky : Bool) (mode : rounding_mode)
    (hδ_low : 0 ≤ δ) (hδ_lt : δ < 1)
    (hsticky_zero : sticky = false → δ = 0)
    (hsticky_pos : sticky = true → 0 < δ)
    (hM_pos : 1 ≤ M.toNat) (hM_lt : M.toNat < 10 ^ 23)
    (hδM : sticky = true → δ * 10 ^ 20 ≤ (M.toNat : ℚ))
    (result : Number)
    (hok : doNormalize128 zn M e largeRange.min largeRange.max mode sticky = .ok result)
    (hres : result.mantissa_ ≠ 0) :
    ∃ (zm : UInt64) (ze' : Int) (f : ℚ) (g : Guard) (res_pos : RoundResult) (ftilde : ℚ),
      mantissaFloor ≤ zm.toNat ∧
      zm.toNat ≤ maxRepUp.toNat ∧
      0 ≤ f ∧ f < 1 ∧
      ((M.toNat : ℚ) + δ) * 10 ^ e = ((zm.toNat : ℚ) + f) * 10 ^ ze' ∧
      g.doRoundUp false zm ze' largeRange.min largeRange.max mode
        "Number::normalize 2" = .ok res_pos ∧
      |result.toRat| = (res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_ ∧
      res_pos.mantissa_ ≠ 0 ∧
      result.negative_ = zn ∧
      g.sbit_ = zn ∧
      mantissaFloorSucc ≤ zm.toNat ∧
      represents g ftilde ∧
      (0 < ftilde → 0 < f) ∧
      (ftilde = 0 → f = 0) := by
  have hminM_v : largeRange.min.toNat = 1000000000000000000 := largeRange_min_val
  have hmaxM_v : largeRange.max.toNat = 9999999999999999999 := largeRange_max_val
  have hM_ne : ¬ (M == 0) = true := by
    intro h
    have : M = 0 := by exact_mod_cast beq_iff_eq.mp h
    rw [this] at hM_pos
    simp at hM_pos
  unfold doNormalize128 at hok
  rw [if_neg hM_ne] at hok
  simp only [] at hok
  rcases hsu : doNormalize128.scaleUp largeRange.min M e with ⟨M₁, e₁⟩
  rw [hsu] at hok
  simp only [] at hok
  obtain ⟨hval_su, hM₁_pos, hM₁_lt, he₁_le, hM₁_size⟩ :
      ((M₁.toNat : ℚ) * 10 ^ e₁ = (M.toNat : ℚ) * 10 ^ e)
      ∧ 1 ≤ M₁.toNat ∧ M₁.toNat < 10 ^ 23 ∧ e₁ ≤ e
      ∧ (M₁ = M ∧ e₁ = e ∨ M₁.toNat < 10 ^ 19) := by
    have hfacts := doNormalize128_scaleUp_facts largeRange.min M e hminM_v hM_pos hM_lt
    rw [hsu] at hfacts
    exact hfacts
  have h10e_pos : (0 : ℚ) < (10 : ℚ) ^ e := zpow_pos (by norm_num) _
  have h10e₁_pos : (0 : ℚ) < (10 : ℚ) ^ e₁ := zpow_pos (by norm_num) _
  set δ₁ : ℚ := δ * 10 ^ (e - e₁) with hδ₁_def
  have hδ₁_nn : 0 ≤ δ₁ :=
    mul_nonneg hδ_low (le_of_lt (zpow_pos (by norm_num) _))
  have hδ₁_zero : sticky = false → δ₁ = 0 := by
    intro hst
    rw [hδ₁_def, hsticky_zero hst, zero_mul]
  have hδ₁_pos : sticky = true → 0 < δ₁ := fun hst =>
    mul_pos (hsticky_pos hst) (zpow_pos (by norm_num) _)
  have hval₁ : ((M₁.toNat : ℚ) + δ₁) * 10 ^ e₁ = ((M.toNat : ℚ) + δ) * 10 ^ e := by
    rw [hδ₁_def, add_mul, add_mul, hval_su]
    congr 1
    rw [show δ * 10 ^ (e - e₁) * 10 ^ e₁ = δ * (10 ^ (e - e₁) * 10 ^ e₁) from by ring,
        ← zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0), show e - e₁ + e₁ = e from by omega]
  have hδ₁M : δ₁ * 10 ^ 20 ≤ (M₁.toNat : ℚ) := by
    by_cases hst : sticky = true
    · have h := hδM hst
      have lhs_eq : δ₁ * 10 ^ 20 * 10 ^ e₁ = δ * 10 ^ 20 * 10 ^ e := by
        rw [hδ₁_def,
            show δ * 10 ^ (e - e₁) * 10 ^ 20 * 10 ^ e₁
              = δ * 10 ^ 20 * (10 ^ (e - e₁) * 10 ^ e₁) from by ring,
            ← zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0), show e - e₁ + e₁ = e from by omega]
      have hfin : δ₁ * 10 ^ 20 * 10 ^ e₁ ≤ (M₁.toNat : ℚ) * 10 ^ e₁ := by
        rw [lhs_eq, hval_su]
        exact mul_le_mul_of_nonneg_right h (le_of_lt h10e_pos)
      exact le_of_mul_le_mul_right hfin h10e₁_pos
    · rw [Bool.not_eq_true] at hst
      rw [hδ₁_zero hst, zero_mul]
      exact Nat.cast_nonneg _
  have hδ₁_lt : δ₁ < 1 := by
    rcases hM₁_size with ⟨_, heeq⟩ | hlt19
    · rw [hδ₁_def, heeq, sub_self, zpow_zero, mul_one]
      exact hδ_lt
    · have hM₁q : (M₁.toNat : ℚ) < 10 ^ 19 := by exact_mod_cast hlt19
      nlinarith [hδ₁M, hM₁q, hδ₁_nn]
  set g₀ : Guard := (if sticky = true
      then (if zn then Guard.new.set_negative else Guard.new).set_sticky
      else (if zn then Guard.new.set_negative else Guard.new)) with hg₀_def
  set ftilde₀ : ℚ := (if sticky = true then (1 : ℚ) / 10 ^ 17 else 0) with hft₀_def
  have hrep₀ : represents g₀ ftilde₀ := by
    rw [hg₀_def, hft₀_def]
    by_cases hst : sticky = true
    · rw [if_pos hst, if_pos hst]
      exact represents_sticky_initial128 zn
    · rw [if_neg hst, if_neg hst]
      exact represents_initial128 zn
  have h_g0_sbit : g₀.sbit_ = zn := by
    rw [hg₀_def]
    by_cases hst : sticky = true
    · rw [if_pos hst]
      by_cases hz : zn
      · rw [if_pos hz, hz]; rfl
      · rw [if_neg hz]
        rw [Bool.not_eq_true] at hz
        rw [hz]; rfl
    · rw [if_neg hst]
      by_cases hz : zn
      · rw [if_pos hz, hz]; rfl
      · rw [if_neg hz]
        rw [Bool.not_eq_true] at hz
        rw [hz]; rfl
  cases hsd : doNormalize_scaleDown128 largeRange.max M₁ e₁ g₀ with
  | error err =>
    except_clash hsd hok
  | ok sd =>
    rw [hsd] at hok
    simp only [] at hok
    obtain ⟨φ₂, ftilde₂, h2nn, h2lt, h2rep, h2slip, h2val, h2le, h2lt22, h2exp, h2sbit, h2xbit,
            h2pos, h2lt1⟩ :=
      doNormalize_scaleDown128_repr largeRange.max M₁ e₁ g₀ hmaxM_v δ₁ ftilde₀
        hδ₁_nn (le_of_lt hδ₁_lt) hrep₀ hM₁_lt sd hsd
    by_cases hund : (sd.2.1 < minExponent || sd.1 < toUInt128 largeRange.min) = true
    · rw [if_pos hund] at hok
      exfalso
      apply hres
      have h := Except.ok.inj hok
      rw [← h]
      rfl
    · rw [if_neg hund] at hok
      rw [Bool.not_eq_true, Bool.or_eq_false_iff] at hund
      obtain ⟨hund1, hund2⟩ := hund
      have hM₂_ge : 1000000000000000000 ≤ sd.1.toNat := by
        by_contra h
        push_neg at h
        apply absurd (decide_eq_true (show sd.1 < toUInt128 largeRange.min from by
          rw [BitVec.lt_def, toNat_toUInt128, hminM_v]; exact h))
        rw [hund2]; simp
      have hM₂_fit : sd.1.toNat < 2 ^ 64 := by
        rw [hmaxM_v] at h2le
        omega
      have hM₂u_toNat : (toUInt64 sd.1).toNat = sd.1.toNat := toNat_toUInt64 hM₂_fit
      cases hcap : doNormalize_capAtMaxRep (toUInt64 sd.1) sd.2.1 sd.2.2 with
      | error err =>
        except_clash hcap hok
      | ok cp =>
        rw [hcap] at hok
        simp only [] at hok
        obtain ⟨φ₃, ftilde₃, h3nn, h3lt, h3rep, h3slip, h3val, h3floor, h3le, h3exp, h3sbit, h3xbit,
                h3pos, h3lt1⟩ :=
          doNormalize_capAtMaxRep_repr (toUInt64 sd.1) sd.2.1 sd.2.2
            (by rw [hM₂u_toNat]; exact hM₂_ge)
            (by rw [hM₂u_toNat]; rw [hmaxM_v] at h2le; exact h2le)
            φ₂ ftilde₂ h2nn h2lt h2rep cp hcap
        cases hru : cp.2.2.doRoundUp zn cp.1 cp.2.1 largeRange.min largeRange.max
            mode "Number::normalize 2" with
        | error err =>
          except_clash hru hok
        | ok res =>
          rw [hru] at hok
          have h_result : result = res.toNumber := (Except.ok.inj hok).symm
          have hres_mant : res.mantissa_ ≠ 0 := by
            rw [h_result] at hres
            exact hres
          set res_pos : RoundResult :=
            { negative_ := false, mantissa_ := res.mantissa_, exponent_ := res.exponent_ }
            with hres_pos_def
          have h_rup_pos : cp.2.2.doRoundUp false cp.1 cp.2.1 largeRange.min largeRange.max
              mode "Number::normalize 2" = .ok res_pos :=
            doRoundUp_false_from_ok cp.2.2 zn cp.1 cp.2.1 mode "Number::normalize 2" res hru
          have hres_pos_mant : res_pos.mantissa_ ≠ 0 := hres_mant
          have h_value : ((cp.1.toNat : ℚ) + φ₃) * 10 ^ cp.2.1
              = ((M.toNat : ℚ) + δ) * 10 ^ e := by
            rw [← h3val, hM₂u_toNat, ← h2val, hval₁]
          have h_abs : |result.toRat|
              = (res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_ := by
            rw [h_result, abs_toRat_eq res.toNumber]
            rfl
          have h_neg : result.negative_ = zn := by
            rw [h_result]
            exact doRoundUp_negative_of_mant_ne cp.2.2 zn cp.1 cp.2.1 _ _ _
              "Number::normalize 2" res hru hres_mant
          have h_sbit : cp.2.2.sbit_ = zn := h3sbit.trans (h2sbit.trans h_g0_sbit)
          -- Shadow ↔ residual transfers.
          have h_eq_nonsticky : sticky = false → φ₃ = ftilde₃ := by
            intro hst'
            have hft₀_eq : ftilde₀ = 0 := by
              rw [hft₀_def, if_neg (by rw [hst']; exact Bool.false_ne_true)]
            have hδ₁_eq : δ₁ = 0 := hδ₁_zero hst'
            have hslip_top : |δ₁ - ftilde₀| * (10 : ℚ) ^ e₁ = 0 := by
              rw [hδ₁_eq, hft₀_eq, sub_zero, abs_zero, zero_mul]
            have hslip0 : |φ₃ - ftilde₃| * (10 : ℚ) ^ cp.2.1 ≤ 0 := by
              calc |φ₃ - ftilde₃| * (10 : ℚ) ^ cp.2.1
                  ≤ |φ₂ - ftilde₂| * (10 : ℚ) ^ sd.2.1 := h3slip
                _ ≤ |δ₁ - ftilde₀| * (10 : ℚ) ^ e₁ := h2slip
                _ = 0 := hslip_top
            have h10pos : (0 : ℚ) < (10 : ℚ) ^ cp.2.1 := zpow_pos (by norm_num) _
            have habs0 : |φ₃ - ftilde₃| = 0 := by
              by_contra hne
              have hpos : 0 < |φ₃ - ftilde₃| := lt_of_le_of_ne (abs_nonneg _) (Ne.symm hne)
              nlinarith
            have := abs_eq_zero.mp habs0
            linarith
          have h_pos_transfer : 0 < ftilde₃ → 0 < φ₃ := by
            intro hft_pos
            by_cases hst : sticky = true
            · exact h3pos (h2pos (hδ₁_pos hst))
            · rw [h_eq_nonsticky (Bool.not_eq_true _ |>.mp hst)]
              exact hft_pos
          have h_zero_transfer : ftilde₃ = 0 → φ₃ = 0 := by
            intro hft0
            by_cases hst : sticky = true
            · exfalso
              have hxbit₀ : g₀.xbit_ = true := by
                rw [hg₀_def, if_pos hst]
                by_cases hz : zn
                · rw [if_pos hz]; rfl
                · rw [if_neg hz]; rfl
              have hxbit₃ : cp.2.2.xbit_ = true := h3xbit (h2xbit hxbit₀)
              obtain ⟨x3, hx3_nn, _, hf3_eq, hx3_iff, _⟩ := h3rep
              have hx3_pos : 0 < x3 := hx3_iff.mp hxbit₃
              have hdv_nn : (0 : ℚ) ≤ (decimalValue cp.2.2.digits_ : ℚ) / 10 ^ 16 := by
                positivity
              rw [hf3_eq] at hft0
              linarith
            · rw [h_eq_nonsticky (Bool.not_eq_true _ |>.mp hst)]
              exact hft0
          exact ⟨cp.1, cp.2.1, φ₃, cp.2.2, res_pos, ftilde₃,
            le_of_lt h3floor, h3le, h3nn, h3lt1 (h2lt1 hδ₁_lt), h_value.symm,
            h_rup_pos, h_abs, hres_pos_mant, h_neg, h_sbit, by omega,
            h3rep, h_pos_transfer, h_zero_transfer⟩

set_option maxHeartbeats 12800000 in
-- Same value-laden walk as the facts theorem, plus the flush/zero-branch
-- spr-algebra closers.
/-- A zero result mantissa from `doNormalize128` forces the input value
strictly below the smallest positive representable. -/
theorem doNormalize128_underflow_value_small
    (zn : Bool) (M : UInt128) (e : Int) (δ : ℚ) (sticky : Bool) (mode : rounding_mode)
    (hδ_low : 0 ≤ δ) (hδ_lt : δ < 1)
    (hsticky_zero : sticky = false → δ = 0)
    (hM_pos : 1 ≤ M.toNat) (hM_lt : M.toNat < 10 ^ 23)
    (hδM : sticky = true → δ * 10 ^ 20 ≤ (M.toNat : ℚ))
    (result : Number)
    (hok : doNormalize128 zn M e largeRange.min largeRange.max mode sticky = .ok result)
    (hres0 : result.mantissa_ = 0) :
    ((M.toNat : ℚ) + δ) * 10 ^ e < (10 : ℚ) ^ (18 : ℕ) * (10 : ℚ) ^ (minExponent : ℤ) := by
  have hminM_v : largeRange.min.toNat = 1000000000000000000 := largeRange_min_val
  have hmaxM_v : largeRange.max.toNat = 9999999999999999999 := largeRange_max_val
  have hM_ne : ¬ (M == 0) = true := by
    intro h
    have : M = 0 := by exact_mod_cast beq_iff_eq.mp h
    rw [this] at hM_pos
    simp at hM_pos
  unfold doNormalize128 at hok
  rw [if_neg hM_ne] at hok
  simp only [] at hok
  rcases hsu : doNormalize128.scaleUp largeRange.min M e with ⟨M₁, e₁⟩
  rw [hsu] at hok
  simp only [] at hok
  obtain ⟨hval_su, hM₁_pos, hM₁_lt, he₁_le, hM₁_size⟩ :
      ((M₁.toNat : ℚ) * 10 ^ e₁ = (M.toNat : ℚ) * 10 ^ e)
      ∧ 1 ≤ M₁.toNat ∧ M₁.toNat < 10 ^ 23 ∧ e₁ ≤ e
      ∧ (M₁ = M ∧ e₁ = e ∨ M₁.toNat < 10 ^ 19) := by
    have hfacts := doNormalize128_scaleUp_facts largeRange.min M e hminM_v hM_pos hM_lt
    rw [hsu] at hfacts
    exact hfacts
  have h10e_pos : (0 : ℚ) < (10 : ℚ) ^ e := zpow_pos (by norm_num) _
  have h10e₁_pos : (0 : ℚ) < (10 : ℚ) ^ e₁ := zpow_pos (by norm_num) _
  set δ₁ : ℚ := δ * 10 ^ (e - e₁) with hδ₁_def
  have hδ₁_nn : 0 ≤ δ₁ :=
    mul_nonneg hδ_low (le_of_lt (zpow_pos (by norm_num) _))
  have hδ₁_zero : sticky = false → δ₁ = 0 := by
    intro hst
    rw [hδ₁_def, hsticky_zero hst, zero_mul]
  have hval₁ : ((M₁.toNat : ℚ) + δ₁) * 10 ^ e₁ = ((M.toNat : ℚ) + δ) * 10 ^ e := by
    rw [hδ₁_def, add_mul, add_mul, hval_su]
    congr 1
    rw [show δ * 10 ^ (e - e₁) * 10 ^ e₁ = δ * (10 ^ (e - e₁) * 10 ^ e₁) from by ring,
        ← zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0), show e - e₁ + e₁ = e from by omega]
  have hδ₁M : δ₁ * 10 ^ 20 ≤ (M₁.toNat : ℚ) := by
    by_cases hst : sticky = true
    · have h := hδM hst
      have lhs_eq : δ₁ * 10 ^ 20 * 10 ^ e₁ = δ * 10 ^ 20 * 10 ^ e := by
        rw [hδ₁_def,
            show δ * 10 ^ (e - e₁) * 10 ^ 20 * 10 ^ e₁
              = δ * 10 ^ 20 * (10 ^ (e - e₁) * 10 ^ e₁) from by ring,
            ← zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0), show e - e₁ + e₁ = e from by omega]
      have hfin : δ₁ * 10 ^ 20 * 10 ^ e₁ ≤ (M₁.toNat : ℚ) * 10 ^ e₁ := by
        rw [lhs_eq, hval_su]
        exact mul_le_mul_of_nonneg_right h (le_of_lt h10e_pos)
      exact le_of_mul_le_mul_right hfin h10e₁_pos
    · rw [Bool.not_eq_true] at hst
      rw [hδ₁_zero hst, zero_mul]
      exact Nat.cast_nonneg _
  have hδ₁_lt : δ₁ < 1 := by
    rcases hM₁_size with ⟨_, heeq⟩ | hlt19
    · rw [hδ₁_def, heeq, sub_self, zpow_zero, mul_one]
      exact hδ_lt
    · have hM₁q : (M₁.toNat : ℚ) < 10 ^ 19 := by exact_mod_cast hlt19
      nlinarith [hδ₁M, hM₁q, hδ₁_nn]
  set g₀ : Guard := (if sticky = true
      then (if zn then Guard.new.set_negative else Guard.new).set_sticky
      else (if zn then Guard.new.set_negative else Guard.new)) with hg₀_def
  set ftilde₀ : ℚ := (if sticky = true then (1 : ℚ) / 10 ^ 17 else 0) with hft₀_def
  have hrep₀ : represents g₀ ftilde₀ := by
    rw [hg₀_def, hft₀_def]
    by_cases hst : sticky = true
    · rw [if_pos hst, if_pos hst]
      exact represents_sticky_initial128 zn
    · rw [if_neg hst, if_neg hst]
      exact represents_initial128 zn
  cases hsd : doNormalize_scaleDown128 largeRange.max M₁ e₁ g₀ with
  | error err =>
    except_clash hsd hok
  | ok sd =>
    rw [hsd] at hok
    simp only [] at hok
    obtain ⟨φ₂, ftilde₂, h2nn, h2lt, h2rep, h2slip, h2val, h2le, h2lt22, h2exp, h2sbit, h2xbit,
            h2pos, h2lt1⟩ :=
      doNormalize_scaleDown128_repr largeRange.max M₁ e₁ g₀ hmaxM_v δ₁ ftilde₀
        hδ₁_nn (le_of_lt hδ₁_lt) hrep₀ hM₁_lt sd hsd
    have hφ₂_lt : φ₂ < 1 := h2lt1 hδ₁_lt
    have h_sd_le : sd.1.toNat ≤ 9999999999999999999 :=
      doNormalize_scaleDown128_output_le M₁ e₁ g₀ sd hsd
    have h_val2 : ((M.toNat : ℚ) + δ) * 10 ^ e = ((sd.1.toNat : ℚ) + φ₂) * 10 ^ sd.2.1 := by
      rw [← h2val, hval₁]
    have h18_q : (10 : ℚ) ^ (18 : ℕ) = 1000000000000000000 := by norm_num
    have h19_q : (10 : ℚ) ^ (19 : ℕ) = 10000000000000000000 := by norm_num
    by_cases hund : (sd.2.1 < minExponent || sd.1 < toUInt128 largeRange.min) = true
    · -- Explicit zero branch: bound the captured value directly.
      rw [h_val2]
      have h10e2_pos : (0 : ℚ) < (10 : ℚ) ^ sd.2.1 := zpow_pos (by norm_num) _
      simp only [Bool.or_eq_true, decide_eq_true_eq] at hund
      rcases hund with he2 | hm2
      · have h1 : ((sd.1.toNat : ℚ) + φ₂) * 10 ^ sd.2.1
            < ((sd.1.toNat : ℚ) + 1) * 10 ^ sd.2.1 :=
          mul_lt_mul_of_pos_right (by linarith) h10e2_pos
        have h2 : ((sd.1.toNat : ℚ) + 1) * 10 ^ sd.2.1 ≤ (10 : ℚ) ^ (19 : ℕ) * 10 ^ sd.2.1 := by
          apply mul_le_mul_of_nonneg_right _ (le_of_lt h10e2_pos)
          rw [h19_q]
          have : (sd.1.toNat : ℚ) ≤ 9999999999999999999 := by exact_mod_cast h_sd_le
          linarith
        have h3 : (10 : ℚ) ^ (19 : ℕ) * (10 : ℚ) ^ sd.2.1
            ≤ (10 : ℚ) ^ (19 : ℕ) * (10 : ℚ) ^ ((minExponent : ℤ) - 1) := by
          apply mul_le_mul_of_nonneg_left _ (by norm_num)
          apply zpow_le_zpow_right₀ (by norm_num)
          omega
        have h4 : (10 : ℚ) ^ (19 : ℕ) * (10 : ℚ) ^ ((minExponent : ℤ) - 1)
            = (10 : ℚ) ^ (18 : ℕ) * (10 : ℚ) ^ (minExponent : ℤ) := by
          have h19 : (10 : ℚ) ^ (19 : ℕ) = (10 : ℚ) ^ (18 : ℕ) * 10 := by norm_num
          have hsplit : (10 : ℚ) ^ ((minExponent : ℤ) - 1) * 10
              = (10 : ℚ) ^ (minExponent : ℤ) := by
            rw [← zpow_add_one₀ (by norm_num : (10 : ℚ) ≠ 0)]
            norm_num
          rw [h19]
          calc (10 : ℚ) ^ (18 : ℕ) * 10 * (10 : ℚ) ^ ((minExponent : ℤ) - 1)
              = (10 : ℚ) ^ (18 : ℕ) * ((10 : ℚ) ^ ((minExponent : ℤ) - 1) * 10) := by ring
            _ = (10 : ℚ) ^ (18 : ℕ) * (10 : ℚ) ^ (minExponent : ℤ) := by rw [hsplit]
        linarith
      · -- sd.1 < 10^18: scaleDown was the identity, so the exponent is ≤ minE.
        have hm2_nat : sd.1.toNat < 1000000000000000000 := by
          have h := BitVec.lt_def.mp hm2
          rwa [toNat_toUInt128, hminM_v] at h
        have h_id : sd.1 = M₁ ∧ sd.2.1 = e₁ := by
          by_cases hM₁_gt : M₁ > toUInt128 largeRange.max
          · exfalso
            have hge := doNormalize_scaleDown128_fired_output_ge M₁ e₁ g₀ sd hM₁_gt hsd
            omega
          · rw [show doNormalize_scaleDown128 largeRange.max M₁ e₁ g₀ = .ok (M₁, e₁, g₀) from by
              rw [doNormalize_scaleDown128.eq_def]
              rw [dif_neg hM₁_gt]] at hsd
            have hpair := Except.ok.inj hsd
            exact ⟨((Prod.mk.inj hpair).1).symm,
                   ((Prod.mk.inj (Prod.mk.inj hpair).2).1).symm⟩
        obtain ⟨h_sd1, h_sd2⟩ := h_id
        have hM₁_lt_min : M₁ < toUInt128 largeRange.min := by
          rw [← h_sd1]
          exact hm2
        have he₁_le_min : e₁ ≤ minExponent := by
          have hex := doNormalize128_scaleUp_exit_exp largeRange.min M e
          rw [hsu] at hex
          exact hex hM₁_lt_min
        rw [h_sd1, h_sd2]
        have hM₁_lt18 : M₁.toNat < 1000000000000000000 := by
          rw [← h_sd1]
          exact hm2_nat
        have h1 : ((M₁.toNat : ℚ) + φ₂) * 10 ^ e₁ < ((M₁.toNat : ℚ) + 1) * 10 ^ e₁ :=
          mul_lt_mul_of_pos_right (by linarith) h10e₁_pos
        have h2 : ((M₁.toNat : ℚ) + 1) * 10 ^ e₁ ≤ (10 : ℚ) ^ (18 : ℕ) * 10 ^ e₁ := by
          apply mul_le_mul_of_nonneg_right _ (le_of_lt h10e₁_pos)
          rw [h18_q]
          have : (M₁.toNat : ℚ) + 1 ≤ 1000000000000000000 := by
            have h : M₁.toNat + 1 ≤ 1000000000000000000 := by omega
            exact_mod_cast h
          linarith
        have h3 : (10 : ℚ) ^ (18 : ℕ) * (10 : ℚ) ^ e₁
            ≤ (10 : ℚ) ^ (18 : ℕ) * (10 : ℚ) ^ (minExponent : ℤ) := by
          apply mul_le_mul_of_nonneg_left _ (by norm_num)
          exact zpow_le_zpow_right₀ (by norm_num) he₁_le_min
        linarith
    · -- Round-stage flush: `doRoundUp_flush_value_small` pins the frame.
      rw [if_neg hund] at hok
      rw [Bool.not_eq_true, Bool.or_eq_false_iff] at hund
      obtain ⟨hund1, hund2⟩ := hund
      have hM₂_ge : 1000000000000000000 ≤ sd.1.toNat := by
        by_contra h
        push_neg at h
        apply absurd (decide_eq_true (show sd.1 < toUInt128 largeRange.min from by
          rw [BitVec.lt_def, toNat_toUInt128, hminM_v]; exact h))
        rw [hund2]; simp
      have hM₂_fit : sd.1.toNat < 2 ^ 64 := by
        rw [hmaxM_v] at h2le
        omega
      have hM₂u_toNat : (toUInt64 sd.1).toNat = sd.1.toNat := toNat_toUInt64 hM₂_fit
      cases hcap : doNormalize_capAtMaxRep (toUInt64 sd.1) sd.2.1 sd.2.2 with
      | error err =>
        except_clash hcap hok
      | ok cp =>
        rw [hcap] at hok
        simp only [] at hok
        obtain ⟨φ₃, ftilde₃, h3nn, h3lt, h3rep, h3slip, h3val, h3floor, h3le, h3exp, h3sbit, h3xbit,
                h3pos, h3lt1⟩ :=
          doNormalize_capAtMaxRep_repr (toUInt64 sd.1) sd.2.1 sd.2.2
            (by rw [hM₂u_toNat]; exact hM₂_ge)
            (by rw [hM₂u_toNat]; rw [hmaxM_v] at h2le; exact h2le)
            φ₂ ftilde₂ h2nn h2lt h2rep cp hcap
        cases hru : cp.2.2.doRoundUp zn cp.1 cp.2.1 largeRange.min largeRange.max
            mode "Number::normalize 2" with
        | error err =>
          except_clash hru hok
        | ok res =>
          rw [hru] at hok
          have h_result : result = res.toNumber := (Except.ok.inj hok).symm
          have hres_mant0 : res.mantissa_ = 0 := by
            have h : result.mantissa_ = res.mantissa_ := by rw [h_result]; rfl
            rw [← h]
            exact hres0
          have hφ₃_lt : φ₃ < 1 := h3lt1 (h2lt1 hδ₁_lt)
          have h_val3 : ((M.toNat : ℚ) + δ) * 10 ^ e
              = ((cp.1.toNat : ℚ) + φ₃) * 10 ^ cp.2.1 := by
            rw [← h3val, hM₂u_toNat, ← h2val, hval₁]
          have hflush := doRoundUp_flush_value_small cp.2.2 zn cp.1 cp.2.1 mode
            (le_of_lt h3floor) h3le "Number::normalize 2" res hru hres_mant0
          rw [h_val3]
          calc ((cp.1.toNat : ℚ) + φ₃) * 10 ^ cp.2.1
              < ((cp.1.toNat : ℚ) + 1) * 10 ^ cp.2.1 :=
                mul_lt_mul_of_pos_right (by linarith) (zpow_pos (by norm_num) _)
            _ ≤ (10 : ℚ) ^ (18 : ℕ) * (10 : ℚ) ^ (minExponent : ℤ) := hflush

end XRPL.Model.Protocol
