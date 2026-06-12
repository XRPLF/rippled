import XRPL.Properties.Protocol.Number.Common.Notation
import XRPL.Properties.Protocol.Number.Common.Helpers
import XRPL.Properties.Protocol.Number.Rounding.ScaleDown
import XRPL.Properties.Protocol.Number.Rounding.Normalize

set_option linter.style.longLine false
set_option linter.style.emptyLine false

namespace XRPL.Model.Protocol

/-! # Value-preservation lemmas for `Number.normalize` (`.to_nearest`)

The normalize pipeline is: exact `scaleUp`, then `scaleDown`+`capAtMaxRep`
(each dropped decimal digit pushed into the guard), then `doRoundUp`. This file
proves that the pre-`doRoundUp` state exactly captures `|n.toRat|` in the
`represents` form `((m.toNat : ℚ) + f) * 10 ^ e`. -/

/-- `doNormalize_scaleUp` preserves the exact value `m.toNat * 10^e`.

The loop multiplies `m` by `10` (and decrements the exponent) while
`m < minMantissa`; with `minMantissa.toNat ≤ 10^18` no `UInt64` overflow occurs
since `m.toNat < 10^18 ⇒ m.toNat * 10 < 10^19 < 2^64`. -/
theorem doNormalize_scaleUp_value (minMant m : UInt64) (e : Int)
    (hminMant : minMant.toNat ≤ 10 ^ 18) :
    let (m', e') := doNormalize_scaleUp minMant m e
    (m'.toNat : ℚ) * 10 ^ e' = (m.toNat : ℚ) * 10 ^ e := by
  induction m, e using doNormalize_scaleUp.induct (minMantissa := minMant) with
  | case1 m e hcond IH =>
    obtain ⟨hlt, hexp⟩ := hcond
    have hunfold : doNormalize_scaleUp minMant m e
        = doNormalize_scaleUp minMant (m * 10) (e - 1) := by
      conv_lhs => rw [doNormalize_scaleUp]
      rw [if_pos ⟨hlt, hexp⟩]
    simp only [hunfold] at *
    -- m < minMant ≤ 10^18, so m.toNat < 10^18, hence m.toNat * 10 < 10^19 < 2^64
    have hm_lt : m.toNat < 10 ^ 18 := by
      have := UInt64.lt_iff_toNat_lt.mp hlt
      omega
    have hm10_nat : (m * 10).toNat = m.toNat * 10 := by
      rw [UInt64.toNat_mul]
      have h10 : (10 : UInt64).toNat = 10 := rfl
      rw [h10]
      apply Nat.mod_eq_of_lt
      omega
    rw [IH]
    -- goal: (m*10).toNat * 10^(e-1) = m.toNat * 10^e
    rw [hm10_nat]
    push_cast
    have he : (10 : ℚ) ^ e = 10 ^ (e - 1) * 10 := by
      rw [show e = (e - 1) + 1 by ring, zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0)]
      norm_num
    rw [he]
    ring
  | case2 m e hcond =>
    have hunfold : doNormalize_scaleUp minMant m e = (m, e) := by
      rw [doNormalize_scaleUp]; rw [if_neg hcond]
    rw [hunfold]

/-- `doNormalize_scaleDown` preserves value as a `represents` fraction.

Mirrors `scaleDown128_correct`: dividing `m` by 10 pushes the dropped digit
`m % 10` into the guard, and the loop stops at `m ≤ maxMantissa`. When it
returns `.ok (m', e', g')`, the value `m.toNat * 10^e` equals
`(m'.toNat + f') * 10^e'` with `represents g' f'`. -/
theorem doNormalize_scaleDown_correct
    (maxMant m : UInt64) (e : Int) (g0 : Guard) (f0 : ℚ)
    (hrep0 : represents g0 f0)
    (m' : UInt64) (e' : Int) (g' : Guard)
    (hok : doNormalize_scaleDown maxMant m e g0 = .ok (m', e', g')) :
    ∃ (k : ℕ) (f' : ℚ),
      e' = e + k ∧
      m'.toNat ≤ maxMant.toNat ∧
      ((m.toNat : ℚ) + f0) * 10 ^ e = ((m'.toNat : ℚ) + f') * 10 ^ e' ∧
      represents g' f' := by
  induction m, e, g0 using doNormalize_scaleDown.induct (maxMantissa := maxMant) generalizing f0 with
  | case1 m e g0 hcond hexp =>
    -- error branch: contradicts hok
    exfalso
    rw [doNormalize_scaleDown] at hok
    rw [dif_pos hcond, if_pos hexp] at hok
    exact absurd hok (by intro h; cases h)
  | case2 m e g0 hcond hexp IH =>
    -- recursive branch
    have h10 : (10 : UInt64).toNat = 10 := rfl
    have hMmod_nat : (m % 10 : UInt64).toNat = m.toNat % 10 := by
      rw [UInt64.toNat_mod, h10]
    have hM10_nat : (m / 10 : UInt64).toNat = m.toNat / 10 := by
      rw [UInt64.toNat_div, h10]
    have hd_lt : (m % 10 : UInt64).toNat < 10 := by rw [hMmod_nat]; exact Nat.mod_lt _ (by decide)
    have hpush : represents (g0.push (m % 10)) ((f0 + ((m % 10 : UInt64).toNat : ℚ)) / 10) :=
      represents_push hrep0 hd_lt
    have hunfold : doNormalize_scaleDown maxMant m e g0
        = doNormalize_scaleDown maxMant (m / 10) (e + 1) (g0.push (m % 10)) := by
      rw [doNormalize_scaleDown]
      rw [dif_pos hcond, if_neg hexp]
    rw [hunfold] at hok
    obtain ⟨k, f'', hek, hmle, hval, hrep_g⟩ := IH _ hpush hok
    refine ⟨k + 1, f'', ?_, hmle, ?_, hrep_g⟩
    · push_cast; omega
    · rw [← hval]
      have hEuc : (m.toNat : ℚ) = 10 * ((m / 10).toNat : ℚ) + ((m % 10).toNat : ℚ) := by
        rw [hM10_nat, hMmod_nat]
        have h : m.toNat = 10 * (m.toNat / 10) + m.toNat % 10 := (Nat.div_add_mod m.toNat 10).symm
        exact_mod_cast h
      have he1 : (10 : ℚ) ^ (e + 1) = 10 ^ e * 10 := by
        rw [zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0)]; norm_num
      rw [hEuc, he1]
      field_simp
      ring
  | case3 m e g0 hcond =>
    -- base branch: m ≤ maxMant, returns (m, e, g0)
    rw [doNormalize_scaleDown] at hok
    rw [dif_neg hcond] at hok
    obtain ⟨rfl, rfl, rfl⟩ : m = m' ∧ e = e' ∧ g0 = g' := by
      have := Except.ok.inj hok; simp only [Prod.mk.injEq] at this; tauto
    have hmle : m.toNat ≤ maxMant.toNat := by
      have hle : ¬ maxMant < m := hcond
      rw [UInt64.lt_iff_toNat_lt] at hle
      omega
    exact ⟨0, f0, by simp, hmle, by simp, hrep0⟩

/-- `doNormalize_capAtMaxRep` preserves value as a `represents` fraction.

It is either the identity (`m ≤ maxRep`) or one further scale-down step
(`m > maxRep`), pushing the dropped digit `m % 10` into the guard. -/
theorem doNormalize_capAtMaxRep_correct
    (m : UInt64) (e : Int) (g0 : Guard) (f0 : ℚ)
    (hrep0 : represents g0 f0)
    (m' : UInt64) (e' : Int) (g' : Guard)
    (hok : doNormalize_capAtMaxRep m e g0 = .ok (m', e', g')) :
    ∃ (f' : ℚ),
      m'.toNat ≤ maxRep.toNat ∧
      ((m.toNat : ℚ) + f0) * 10 ^ e = ((m'.toNat : ℚ) + f') * 10 ^ e' ∧
      represents g' f' ∧
      (maxRep.toNat < m.toNat → 0 ≤ f0 → ((m.toNat % 10 : ℕ) : ℚ) / 10 ≤ f') := by
  unfold doNormalize_capAtMaxRep at hok
  by_cases hmr : m > maxRep
  · rw [if_pos hmr] at hok
    by_cases hexp : e ≥ maxExponent
    · rw [if_pos hexp] at hok
      exact absurd hok (by intro h; cases h)
    · rw [if_neg hexp] at hok
      simp only [divu10] at hok
      obtain ⟨rfl, rfl, rfl⟩ : m / 10 = m' ∧ e + 1 = e' ∧ g0.push (m % 10) = g' := by
        have := Except.ok.inj hok; simp only [Prod.mk.injEq] at this; tauto
      have h10 : (10 : UInt64).toNat = 10 := rfl
      have hMmod_nat : (m % 10 : UInt64).toNat = m.toNat % 10 := by rw [UInt64.toNat_mod, h10]
      have hM10_nat : (m / 10 : UInt64).toNat = m.toNat / 10 := by rw [UInt64.toNat_div, h10]
      have hd_lt : (m % 10 : UInt64).toNat < 10 := by rw [hMmod_nat]; exact Nat.mod_lt _ (by decide)
      have hpush : represents (g0.push (m % 10)) ((f0 + ((m % 10 : UInt64).toNat : ℚ)) / 10) :=
        represents_push hrep0 hd_lt
      refine ⟨(f0 + ((m % 10 : UInt64).toNat : ℚ)) / 10, ?_, ?_, hpush, ?_⟩
      · -- (m/10).toNat ≤ maxRep.toNat
        rw [hM10_nat]
        have : m.toNat < 2 ^ 64 := UInt64.toNat_lt_size m
        have hmaxRep_v : maxRep.toNat = maxRepNat := maxRep_val
        rw [hmaxRep_v]; omega
      · have hEuc : (m.toNat : ℚ) = 10 * ((m / 10).toNat : ℚ) + ((m % 10).toNat : ℚ) := by
          rw [hM10_nat, hMmod_nat]
          have h : m.toNat = 10 * (m.toNat / 10) + m.toNat % 10 := (Nat.div_add_mod m.toNat 10).symm
          exact_mod_cast h
        have he1 : (10 : ℚ) ^ (e + 1) = 10 ^ e * 10 := by
          rw [zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0)]; norm_num
        rw [hEuc, he1]
        field_simp
        ring
      · intro _ hf0_nn
        rw [hMmod_nat]
        gcongr
        linarith
  · rw [if_neg hmr] at hok
    obtain ⟨rfl, rfl, rfl⟩ : m = m' ∧ e = e' ∧ g0 = g' := by
      have := Except.ok.inj hok; simp only [Prod.mk.injEq] at this; tauto
    have hmle : m.toNat ≤ maxRep.toNat := by
      have hle : ¬ maxRep < m := hmr
      rw [UInt64.lt_iff_toNat_lt] at hle
      omega
    refine ⟨f0, hmle, by simp, hrep0, ?_⟩
    intro hmr_gt _
    omega

/-- `Guard.new` and `Guard.new.set_negative` both represent the fraction `0`. -/
lemma g0_represents_zero (negative : Bool) :
    represents (if negative then Guard.new.set_negative else Guard.new) 0 := by
  cases negative
  · simp only [Bool.false_eq_true, if_false]; exact represents_new
  · simp only [if_true]
    obtain ⟨x_rep, hx_nn, hx_lt, hf_eq, hxbit, hall⟩ := represents_new
    refine ⟨x_rep, hx_nn, hx_lt, ?_, ?_, ?_⟩
    · have : Guard.new.set_negative.digits_ = Guard.new.digits_ := rfl
      rw [this]; exact hf_eq
    · have hxbit_eq : Guard.new.set_negative.xbit_ = Guard.new.xbit_ := rfl
      rw [hxbit_eq]; exact hxbit
    · have : Guard.new.set_negative.digits_ = Guard.new.digits_ := rfl
      rw [this]; exact hall

/-- Combined value-preservation + side conditions for `Number.normalize` (`.to_nearest`).

When `n.normalize largeRange.min largeRange.max .to_nearest = .ok result` with a
non-zero result mantissa, there is a pre-`doRoundUp` state `(zm, ze, g)` together
with a fraction `f` such that:
* the captured value `((zm.toNat : ℚ) + f) * 10 ^ ze` equals `|n.toRat|`,
* `g` represents `f`, with `mantissaFloor ≤ zm.toNat ≤ maxRep.toNat`,
* the floor constraint is vacuous (`zm.toNat = mantissaFloor` never holds since
  `zm.toNat ≥ minMantissa > mantissaFloor`),
* `g.doRoundUp false zm ze ...` succeeds with the `positive` form of `result`. -/
theorem normalize_algorithmic_facts (n result : Number)
    (hn_mant_ne : n.mantissa_ ≠ 0)
    (hok : n.normalize largeRange.min largeRange.max .to_nearest = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    ∃ (zm : UInt64) (ze : Int) (f : ℚ) (g : Guard) (res_pos : RoundResult),
      mantissaFloor ≤ zm.toNat ∧
      zm.toNat ≤ maxRep.toNat ∧
      (zm.toNat = mantissaFloor → (8 : ℚ) / 10 ≤ f) ∧
      |n.toRat| = ((zm.toNat : ℚ) + f) * 10 ^ ze ∧
      g.doRoundUp false zm ze largeRange.min largeRange.max .to_nearest "Number::normalize 2" = .ok res_pos ∧
      |result.toRat| = (res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_ ∧
      res_pos.mantissa_ ≠ 0 ∧
      represents g f ∧
      result.negative_ = n.negative_ := by
  -- Input value
  have habs_n : |n.toRat| = (n.mantissa_.toNat : ℚ) * 10 ^ n.exponent_ := abs_toRat_eq n
  -- Unfold normalize / doNormalize
  unfold Number.normalize doNormalize at hok
  rw [beq_eq_false_iff_ne.mpr hn_mant_ne] at hok
  simp only [Bool.false_eq_true, if_false] at hok
  -- scaleUp
  set su := doNormalize_scaleUp largeRange.min n.mantissa_ n.exponent_ with hsu_def
  set m1 : UInt64 := su.1 with hm1_def
  set e1 : Int := su.2 with he1_def
  have hsu_split : su = (m1, e1) := rfl
  -- guard
  set g0 : Guard := if n.negative_ then Guard.new.set_negative else Guard.new with hg0_def
  have hg0_rep : represents g0 0 := g0_represents_zero n.negative_
  -- scaleDown result must be .ok (else hok is .error)
  obtain ⟨m2, e2, g2, hsd⟩ : ∃ m2 e2 g2,
      doNormalize_scaleDown largeRange.max m1 e1 g0 = .ok (m2, e2, g2) := by
    match hsd : doNormalize_scaleDown largeRange.max m1 e1 g0 with
    | .error e => rw [hsd] at hok; exact absurd hok (by intro h; cases h)
    | .ok (m2, e2, g2) => exact ⟨m2, e2, g2, rfl⟩
  rw [hsd] at hok
  simp only at hok
  -- zero check
  by_cases hzero : e2 < minExponent ∨ m2 < largeRange.min
  · -- returns zero, contradicts hresult
    rw [show (decide (e2 < minExponent) || decide (m2 < largeRange.min)) = true from by
      rcases hzero with h | h
      · simp [h]
      · simp [h]] at hok
    simp only [if_true] at hok
    have : Number.zero = result := Except.ok.inj hok
    rw [← this] at hresult; exact absurd rfl hresult
  · push_neg at hzero
    obtain ⟨he2_ge, hm2_ge⟩ := hzero
    rw [show (decide (e2 < minExponent) || decide (m2 < largeRange.min)) = false from by
      rw [Bool.or_eq_false_iff]
      exact ⟨decide_eq_false (not_lt.mpr he2_ge), decide_eq_false hm2_ge⟩] at hok
    simp only [Bool.false_eq_true, if_false] at hok
    -- cap
    obtain ⟨m3, e3, g3, hcap⟩ : ∃ m3 e3 g3,
        doNormalize_capAtMaxRep m2 e2 g2 = .ok (m3, e3, g3) := by
      match hcap : doNormalize_capAtMaxRep m2 e2 g2 with
      | .error e => rw [hcap] at hok; exact absurd hok (by intro h; cases h)
      | .ok (m3, e3, g3) => exact ⟨m3, e3, g3, rfl⟩
    rw [hcap] at hok
    simp only at hok
    -- doRoundUp
    obtain ⟨res, hrup⟩ : ∃ res,
        g3.doRoundUp n.negative_ m3 e3 largeRange.min largeRange.max .to_nearest "Number::normalize 2" = .ok res := by
      match hrup : g3.doRoundUp n.negative_ m3 e3 largeRange.min largeRange.max .to_nearest "Number::normalize 2" with
      | .error e => rw [hrup] at hok; exact absurd hok (by intro h; cases h)
      | .ok res => exact ⟨res, rfl⟩
    rw [hrup] at hok
    simp only at hok
    have hresult_eq : result = res.toNumber := (Except.ok.inj hok).symm
    -- value preservation
    have hsu_val := doNormalize_scaleUp_value largeRange.min n.mantissa_ n.exponent_
      (by rw [largeRange_min_val]; norm_num)
    -- hsu_val (after reducing the let-match) : m1.toNat * 10^e1 = n.mantissa_.toNat * 10^n.exponent_
    have hsu_val' : (m1.toNat : ℚ) * 10 ^ e1 = (n.mantissa_.toNat : ℚ) * 10 ^ n.exponent_ := by
      have : (doNormalize_scaleUp largeRange.min n.mantissa_ n.exponent_).1 = m1 := rfl
      have h2 : (doNormalize_scaleUp largeRange.min n.mantissa_ n.exponent_).2 = e1 := rfl
      simpa [this, h2] using hsu_val
    obtain ⟨k2, f2, he2_eq, hm2_le_maxMant, hval2, hrep2⟩ :=
      doNormalize_scaleDown_correct largeRange.max m1 e1 g0 0 hg0_rep m2 e2 g2 hsd
    -- hval2 : (m1.toNat + 0) * 10^e1 = (m2.toNat + f2) * 10^e2
    obtain ⟨f3, hm3_le_maxRep, hval3, hrep3, hf3_resid⟩ :=
      doNormalize_capAtMaxRep_correct m2 e2 g2 f2 hrep2 m3 e3 g3 hcap
    have hf2_nn : 0 ≤ f2 := represents_nonneg hrep2
    -- hval3 : (m2.toNat + f2) * 10^e2 = (m3.toNat + f3) * 10^e3
    -- combined value
    have hval_total : |n.toRat| = ((m3.toNat : ℚ) + f3) * 10 ^ e3 := by
      rw [habs_n, ← hsu_val']
      calc (m1.toNat : ℚ) * 10 ^ e1
          = ((m1.toNat : ℚ) + 0) * 10 ^ e1 := by ring
        _ = ((m2.toNat : ℚ) + f2) * 10 ^ e2 := hval2
        _ = ((m3.toNat : ℚ) + f3) * 10 ^ e3 := hval3
    -- side conditions: m2 ≥ minMant, and cap preserves the lower bound modulo /10
    have hm2_ge_min : largeRange.min.toNat ≤ m2.toNat := by
      rw [UInt64.lt_iff_toNat_lt] at hm2_ge; omega
    have hminMant_v : largeRange.min.toNat = 1000000000000000000 := largeRange_min_val
    -- m3 ≥ minMant: cap either kept m2 (m2 ≤ maxRep) or divided once (m2 > maxRep ⇒ m3 = m2/10 ≥ 10^17)
    -- For the bound we only need mantissaFloor ≤ m3.toNat. Since m3 is the doRoundUp input
    -- and result.mantissa_ ≠ 0, use doRoundUp_output_invariants prerequisites:
    -- Actually mantissaFloor ≤ m3 follows once we know minMant ≤ m3. But after capping m3 may
    -- drop below minMant. Establish mantissaFloor ≤ m3.toNat directly.
    have hm3_ge_floor : mantissaFloor ≤ m3.toNat := by
      -- Either cap was identity (m3 = m2 ≥ minMant > floor) or cap divided once.
      unfold doNormalize_capAtMaxRep at hcap
      by_cases hmr : m2 > maxRep
      · rw [if_pos hmr] at hcap
        by_cases hexp3 : e2 ≥ maxExponent
        · rw [if_pos hexp3] at hcap; exact absurd hcap (by intro h; cases h)
        · rw [if_neg hexp3] at hcap
          simp only [divu10] at hcap
          have hm3_eq : m3 = m2 / 10 := ((Prod.mk.inj (Except.ok.inj hcap)).1).symm
          rw [hm3_eq, UInt64.toNat_div, show (10 : UInt64).toNat = 10 from rfl]
          have hmr_nat : maxRep.toNat < m2.toNat := UInt64.lt_iff_toNat_lt.mp hmr
          rw [maxRep_val] at hmr_nat
          omega
      · rw [if_neg hmr] at hcap
        have hm3_eq : m3 = m2 := ((Prod.mk.inj (Except.ok.inj hcap)).1).symm
        rw [hm3_eq]; rw [hminMant_v] at hm2_ge_min; omega
    have hm3_le_maxRep_v : m3.toNat ≤ maxRep.toNat := hm3_le_maxRep
    -- floor constraint vacuous: m3 ≥ mantissaFloor and m3 = mantissaFloor impossible? Not needed:
    -- mantissaFloor ≤ m3 suffices, the floor constraint `m3 = mantissaFloor → ...` is also dischargeable
    -- but here m3 ≥ minMant (10^18) > mantissaFloor in the identity case; in the divide case m3 could
    -- equal mantissaFloor. We prove the implication vacuously only when it does not equal; otherwise
    -- we must show 8/10 ≤ f3. Handle via: if m3 = mantissaFloor then derive contradiction or the residue.
    have hfloor_constraint : m3.toNat = mantissaFloor → (8 : ℚ) / 10 ≤ f3 := by
      intro hm3_floor
      by_cases hmr : maxRep.toNat < m2.toNat
      · -- cap divided: m3 = m2 / 10 = mantissaFloor, m2 > maxRep ⟹ m2 % 10 ≥ 8
        have hm3_div : m3.toNat = m2.toNat / 10 := by
          unfold doNormalize_capAtMaxRep at hcap
          have hmr_u : m2 > maxRep := UInt64.lt_iff_toNat_lt.mpr hmr
          rw [if_pos hmr_u] at hcap
          by_cases hexp3 : e2 ≥ maxExponent
          · rw [if_pos hexp3] at hcap; exact absurd hcap (by intro h; cases h)
          · rw [if_neg hexp3] at hcap
            simp only [divu10] at hcap
            have : m3 = m2 / 10 := ((Prod.mk.inj (Except.ok.inj hcap)).1).symm
            rw [this, UInt64.toNat_div, show (10 : UInt64).toNat = 10 from rfl]
        have hmod8 : m2.toNat % 10 ≥ 8 := by
          have hmr' : maxRepNat < m2.toNat := by rw [← maxRep_val]; exact hmr
          have hdiv : m2.toNat / 10 = mantissaFloor := by rw [← hm3_div]; exact hm3_floor
          omega
        have hresid := hf3_resid hmr hf2_nn
        have h8q : (8 : ℚ) / 10 ≤ ((m2.toNat % 10 : ℕ) : ℚ) / 10 := by
          gcongr
          exact_mod_cast hmod8
        exact le_trans h8q hresid
      · -- cap was identity: m3 = m2 ≥ minMant = 10^18 > mantissaFloor, contradicts hm3_floor
        exfalso
        push_neg at hmr
        have hm3_eq : m3.toNat = m2.toNat := by
          unfold doNormalize_capAtMaxRep at hcap
          have hmr_u : ¬ m2 > maxRep := by
            intro hgt
            have := UInt64.lt_iff_toNat_lt.mp hgt; omega
          rw [if_neg hmr_u] at hcap
          have : m3 = m2 := ((Prod.mk.inj (Except.ok.inj hcap)).1).symm
          rw [this]
        rw [hminMant_v] at hm2_ge_min
        omega
    -- Build the positive doRoundUp result
    have hres_mant_ne : res.mantissa_ ≠ 0 := by
      rw [hresult_eq] at hresult; exact hresult
    set res_pos : RoundResult :=
      { negative_ := false, mantissa_ := res.mantissa_, exponent_ := res.exponent_ } with hres_pos_def
    have hrup_pos : g3.doRoundUp false m3 e3 largeRange.min largeRange.max .to_nearest "Number::normalize 2"
        = .ok res_pos :=
      doRoundUp_false_from_ok g3 n.negative_ m3 e3 .to_nearest "Number::normalize 2" res hrup
    have h_result_abs : |result.toRat| = (res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_ := by
      rw [hresult_eq, abs_toRat_eq res.toNumber]; rfl
    have h_res_neg : result.negative_ = n.negative_ := by
      rw [hresult_eq]
      exact doRoundUp_negative_of_mant_ne g3 n.negative_ m3 e3 _ _ _ "Number::normalize 2" res hrup hres_mant_ne
    refine ⟨m3, e3, f3, g3, res_pos, hm3_ge_floor, hm3_le_maxRep_v, hfloor_constraint,
      hval_total, hrup_pos, h_result_abs, hres_mant_ne, hrep3, h_res_neg⟩

end XRPL.Model.Protocol
