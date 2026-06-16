import XRPL.Properties.Protocol.Number.Common.Notation
import XRPL.Properties.Protocol.Number.Common.Helpers
import XRPL.Properties.Protocol.Number.Common.Rounding.ScaleDown
import XRPL.Properties.Protocol.Number.Common.Rounding.Normalize


namespace XRPL.Model.Protocol

/-! # Value-preservation lemmas for `Number.normalize` (`.to_nearest`)

The normalize pipeline is: exact `scaleUp`, then `scaleDown`+`capAtMaxRep`
(each dropped decimal digit pushed into the guard), then `doRoundUp`. This file
proves that the pre-`doRoundUp` state exactly captures `|n.toRat|` in the
`represents` form `((m.toNat : ℚ) + f) * 10 ^ e`. -/

/-- `doNormalize_scaleDown` preserves the guard's sign bit: every recursive step
pushes a digit (`Guard.push` keeps `sbit_`), and the base case returns `g`.
Shared by the directed-mode `Normalize` algorithmic-facts files. -/
lemma doNormalize_scaleDown_sbit_preserved
    (maxM m : UInt64) (e : Int) (g : Guard) (m' : UInt64) (e' : Int) (g' : Guard)
    (h : doNormalize_scaleDown maxM m e g = .ok (m', e', g')) :
    g'.sbit_ = g.sbit_ := by
  induction m, e, g using doNormalize_scaleDown.induct (maxMantissa := maxM) generalizing m' e' g' with
  | case1 m e g hgt hmax =>
    rw [doNormalize_scaleDown, dif_pos hgt, if_pos hmax] at h
    exact absurd h (by intro hh; cases hh)
  | case2 m e g hgt hmax IH =>
    rw [doNormalize_scaleDown, dif_pos hgt, if_neg hmax] at h
    rw [IH m' e' g' h]; rfl
  | case3 m e g hgt =>
    rw [doNormalize_scaleDown, dif_neg hgt] at h
    have hg' : g = g' := (Prod.mk.inj (Prod.mk.inj (Except.ok.inj h)).2).2
    rw [← hg']

/-- `doNormalize_capAtMaxRep` preserves the guard's sign bit: the only mutating
branch pushes a digit (`Guard.push` keeps `sbit_`), and both other branches
return `g` unchanged. Shared by the directed-mode `Normalize` files. -/
lemma doNormalize_capAtMaxRep_sbit_preserved
    (m : UInt64) (e : Int) (g : Guard) (m' : UInt64) (e' : Int) (g' : Guard)
    (h : doNormalize_capAtMaxRep m e g = .ok (m', e', g')) :
    g'.sbit_ = g.sbit_ := by
  unfold doNormalize_capAtMaxRep at h
  by_cases hmr : m > maxRepUp
  · rw [if_pos hmr] at h
    by_cases hexp : e ≥ maxExponent
    · rw [if_pos hexp] at h; exact absurd h (by intro hh; cases hh)
    · rw [if_neg hexp] at h
      simp only [divu10] at h
      have hg' : (g.push (m % 10)) = g' := (Prod.mk.inj (Prod.mk.inj (Except.ok.inj h)).2).2
      rw [← hg']; rfl
  · rw [if_neg hmr] at h
    have hg' : g = g' := (Prod.mk.inj (Prod.mk.inj (Except.ok.inj h)).2).2
    rw [← hg']

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
      have h10 : (10 : UInt64).toNat = 10 := uint64_ten_toNat
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
    have h10 : (10 : UInt64).toNat = 10 := uint64_ten_toNat
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

It is either the identity (`m ≤ maxRepUp`) or one further scale-down step
(`m > maxRepUp`), pushing the dropped digit `m % 10` into the guard. -/
theorem doNormalize_capAtMaxRep_correct
    (m : UInt64) (e : Int) (g0 : Guard) (f0 : ℚ)
    (hrep0 : represents g0 f0)
    (m' : UInt64) (e' : Int) (g' : Guard)
    (hok : doNormalize_capAtMaxRep m e g0 = .ok (m', e', g')) :
    ∃ (f' : ℚ),
      m'.toNat ≤ maxRepUp.toNat ∧
      ((m.toNat : ℚ) + f0) * 10 ^ e = ((m'.toNat : ℚ) + f') * 10 ^ e' ∧
      represents g' f' ∧
      (maxRepUp.toNat < m.toNat → 0 ≤ f0 → ((m.toNat % 10 : ℕ) : ℚ) / 10 ≤ f') := by
  unfold doNormalize_capAtMaxRep at hok
  by_cases hmr : m > maxRepUp
  · rw [if_pos hmr] at hok
    by_cases hexp : e ≥ maxExponent
    · rw [if_pos hexp] at hok
      exact absurd hok (by intro h; cases h)
    · rw [if_neg hexp] at hok
      simp only [divu10] at hok
      obtain ⟨rfl, rfl, rfl⟩ : m / 10 = m' ∧ e + 1 = e' ∧ g0.push (m % 10) = g' := by
        have := Except.ok.inj hok; simp only [Prod.mk.injEq] at this; tauto
      have h10 : (10 : UInt64).toNat = 10 := uint64_ten_toNat
      have hMmod_nat : (m % 10 : UInt64).toNat = m.toNat % 10 := by rw [UInt64.toNat_mod, h10]
      have hM10_nat : (m / 10 : UInt64).toNat = m.toNat / 10 := by rw [UInt64.toNat_div, h10]
      have hd_lt : (m % 10 : UInt64).toNat < 10 := by rw [hMmod_nat]; exact Nat.mod_lt _ (by decide)
      have hpush : represents (g0.push (m % 10)) ((f0 + ((m % 10 : UInt64).toNat : ℚ)) / 10) :=
        represents_push hrep0 hd_lt
      refine ⟨(f0 + ((m % 10 : UInt64).toNat : ℚ)) / 10, ?_, ?_, hpush, ?_⟩
      · -- (m/10).toNat ≤ maxRepUp.toNat
        rw [hM10_nat]
        have : m.toNat < 2 ^ 64 := UInt64.toNat_lt_size m
        rw [show maxRepUp.toNat = maxRepUpNat from rfl]
        omega
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
    have hmle : m.toNat ≤ maxRepUp.toNat := by
      have hle : ¬ maxRepUp < m := hmr
      rw [UInt64.lt_iff_toNat_lt] at hle
      omega
    refine ⟨f0, hmle, by simp, hrep0, ?_⟩
    intro hmr_gt _
    omega

/-- If `doNormalize_scaleDown` actually fires (`m > maxMantissa`), its output
mantissa is at most `(2^64 − 1)/10` — in particular strictly below `maxRep`. -/
lemma doNormalize_scaleDown_fired_output_le
    (maxM m : UInt64) (e : Int) (g : Guard) (m' : UInt64) (e' : Int) (g' : Guard)
    (hgt : m > maxM)
    (h : doNormalize_scaleDown maxM m e g = .ok (m', e', g')) :
    m'.toNat ≤ 1844674407370955161 := by
  induction m, e, g using doNormalize_scaleDown.induct (maxMantissa := maxM) generalizing m' e' g' with
  | case1 m e g hgt' hmax =>
    rw [doNormalize_scaleDown, dif_pos hgt', if_pos hmax] at h
    exact absurd h (by intro hh; cases hh)
  | case2 m e g hgt' hmax IH =>
    rw [doNormalize_scaleDown, dif_pos hgt', if_neg hmax] at h
    by_cases hgt2 : m / 10 > maxM
    · exact IH m' e' g' hgt2 h
    · rw [doNormalize_scaleDown, dif_neg hgt2] at h
      have hm'_eq : m / 10 = m' := (Prod.mk.inj (Except.ok.inj h)).1
      have h64 : m.toNat < 2 ^ 64 := UInt64.toNat_lt_size m
      rw [← hm'_eq, UInt64.toNat_div, show (10 : UInt64).toNat = 10 from rfl]
      omega
  | case3 m e g hgt' =>
    exact absurd hgt hgt'

/-- Combined value-preservation + side conditions for `Number.normalize` (`.to_nearest`).

When `n.normalize largeRange.min largeRange.max .to_nearest = .ok result` with a
non-zero result mantissa, there is a pre-`doRoundUp` state `(zm, ze, g)` together
with a fraction `f` such that:
* the captured value `((zm.toNat : ℚ) + f) * 10 ^ ze` equals `|n.toRat|`,
* `g` represents `f`, with `mantissaFloor ≤ zm.toNat ≤ maxRep.toNat`,
* the floor constraint is vacuous (`zm.toNat = mantissaFloor` never holds since
  `zm.toNat ≥ minMantissa > mantissaFloor`),
* `g.doRoundUp false zm ze ...` succeeds with the `positive` form of `result`. -/
theorem normalize_algorithmic_facts_anyMode (n result : Number) (mode : rounding_mode)
    (hn_mant_ne : n.mantissa_ ≠ 0)
    (hok : n.normalize largeRange.min largeRange.max mode = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    ∃ (zm : UInt64) (ze : Int) (f : ℚ) (g : Guard) (res_pos : RoundResult),
      mantissaFloor ≤ zm.toNat ∧
      zm.toNat ≤ maxRepUp.toNat ∧
      0 ≤ f ∧ f < 1 ∧
      (zm.toNat = mantissaFloor → (8 : ℚ) / 10 ≤ f) ∧
      (maxRep.toNat < zm.toNat → f = 0 ∧ g.empty = true) ∧
      |n.toRat| = ((zm.toNat : ℚ) + f) * 10 ^ ze ∧
      g.doRoundUp false zm ze largeRange.min largeRange.max mode "Number::normalize 2" = .ok res_pos ∧
      |result.toRat| = (res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_ ∧
      res_pos.mantissa_ ≠ 0 ∧
      represents g f ∧
      result.negative_ = n.negative_ ∧
      g.sbit_ = n.negative_ ∧
      mantissaFloorSucc ≤ zm.toNat := by
  have habs_n : |n.toRat| = (n.mantissa_.toNat : ℚ) * 10 ^ n.exponent_ := abs_toRat_eq n
  unfold Number.normalize doNormalize at hok
  rw [beq_eq_false_iff_ne.mpr hn_mant_ne] at hok
  simp only [Bool.false_eq_true, if_false] at hok
  set su := doNormalize_scaleUp largeRange.min n.mantissa_ n.exponent_ with hsu_def
  set m1 : UInt64 := su.1 with hm1_def
  set e1 : Int := su.2 with he1_def
  set g0 : Guard := if n.negative_ then Guard.new.set_negative else Guard.new with hg0_def
  have hg0_rep : represents g0 0 := g0_represents_zero n.negative_
  obtain ⟨m2, e2, g2, hsd⟩ : ∃ m2 e2 g2,
      doNormalize_scaleDown largeRange.max m1 e1 g0 = .ok (m2, e2, g2) := by
    match hsd : doNormalize_scaleDown largeRange.max m1 e1 g0 with
    | .error e => rw [hsd] at hok; exact absurd hok (by intro h; cases h)
    | .ok (m2, e2, g2) => exact ⟨m2, e2, g2, rfl⟩
  rw [hsd] at hok
  simp only at hok
  by_cases hzero : e2 < minExponent ∨ m2 < largeRange.min
  · rw [show (decide (e2 < minExponent) || decide (m2 < largeRange.min)) = true from by
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
    obtain ⟨m3, e3, g3, hcap⟩ : ∃ m3 e3 g3,
        doNormalize_capAtMaxRep m2 e2 g2 = .ok (m3, e3, g3) := by
      match hcap : doNormalize_capAtMaxRep m2 e2 g2 with
      | .error e => rw [hcap] at hok; exact absurd hok (by intro h; cases h)
      | .ok (m3, e3, g3) => exact ⟨m3, e3, g3, rfl⟩
    rw [hcap] at hok
    simp only at hok
    obtain ⟨res, hrup⟩ : ∃ res,
        g3.doRoundUp n.negative_ m3 e3 largeRange.min largeRange.max mode "Number::normalize 2" = .ok res := by
      match hrup : g3.doRoundUp n.negative_ m3 e3 largeRange.min largeRange.max mode "Number::normalize 2" with
      | .error e => rw [hrup] at hok; exact absurd hok (by intro h; cases h)
      | .ok res => exact ⟨res, rfl⟩
    rw [hrup] at hok
    simp only at hok
    have hresult_eq : result = res.toNumber := (Except.ok.inj hok).symm
    have hsu_val := doNormalize_scaleUp_value largeRange.min n.mantissa_ n.exponent_
      (by rw [largeRange_min_val]; norm_num)
    have hsu_val' : (m1.toNat : ℚ) * 10 ^ e1 = (n.mantissa_.toNat : ℚ) * 10 ^ n.exponent_ := by
      have : (doNormalize_scaleUp largeRange.min n.mantissa_ n.exponent_).1 = m1 := rfl
      have h2 : (doNormalize_scaleUp largeRange.min n.mantissa_ n.exponent_).2 = e1 := rfl
      simpa [this, h2] using hsu_val
    obtain ⟨k2, f2, he2_eq, hm2_le_maxMant, hval2, hrep2⟩ :=
      doNormalize_scaleDown_correct largeRange.max m1 e1 g0 0 hg0_rep m2 e2 g2 hsd
    obtain ⟨f3, hm3_le_maxRep, hval3, hrep3, hf3_resid⟩ :=
      doNormalize_capAtMaxRep_correct m2 e2 g2 f2 hrep2 m3 e3 g3 hcap
    have hf2_nn : 0 ≤ f2 := represents_nonneg hrep2
    have hval_total : |n.toRat| = ((m3.toNat : ℚ) + f3) * 10 ^ e3 := by
      rw [habs_n, ← hsu_val']
      calc (m1.toNat : ℚ) * 10 ^ e1
          = ((m1.toNat : ℚ) + 0) * 10 ^ e1 := by ring
        _ = ((m2.toNat : ℚ) + f2) * 10 ^ e2 := hval2
        _ = ((m3.toNat : ℚ) + f3) * 10 ^ e3 := hval3
    have hm2_ge_min : largeRange.min.toNat ≤ m2.toNat := by
      rw [UInt64.lt_iff_toNat_lt] at hm2_ge; omega
    have hminMant_v : largeRange.min.toNat = 1000000000000000000 := largeRange_min_val
    have hm3_ge_floor : mantissaFloor ≤ m3.toNat := by
      unfold doNormalize_capAtMaxRep at hcap
      by_cases hmr : m2 > maxRepUp
      · rw [if_pos hmr] at hcap
        by_cases hexp3 : e2 ≥ maxExponent
        · rw [if_pos hexp3] at hcap; exact absurd hcap (by intro h; cases h)
        · rw [if_neg hexp3] at hcap
          simp only [divu10] at hcap
          have hm3_eq : m3 = m2 / 10 := ((Prod.mk.inj (Except.ok.inj hcap)).1).symm
          rw [hm3_eq, UInt64.toNat_div, show (10 : UInt64).toNat = 10 from rfl]
          have hmr_nat : maxRepUp.toNat < m2.toNat := UInt64.lt_iff_toNat_lt.mp hmr
          rw [show maxRepUp.toNat = maxRepUpNat from rfl] at hmr_nat
          omega
      · rw [if_neg hmr] at hcap
        have hm3_eq : m3 = m2 := ((Prod.mk.inj (Except.ok.inj hcap)).1).symm
        rw [hm3_eq]; rw [hminMant_v] at hm2_ge_min; omega
    have hm3_le_maxRepUp_v : m3.toNat ≤ maxRepUp.toNat := hm3_le_maxRep
    have hf3_nn : 0 ≤ f3 := represents_nonneg hrep3
    have hf3_lt : f3 < 1 := represents_lt_one hrep3
    -- Floor constraint is vacuous under the maxRepUp cap: in the divide case
    -- m3 = m2/10 ≥ (maxRepUp+1)/10 > mantissaFloor, and in the identity case
    -- m3 = m2 ≥ minMantissa = 10^18 > mantissaFloor.
    have hfloor_constraint : m3.toNat = mantissaFloor → (8 : ℚ) / 10 ≤ f3 := by
      intro hm3_floor
      exfalso
      by_cases hmr : maxRepUp.toNat < m2.toNat
      · have hm3_div : m3.toNat = m2.toNat / 10 := by
          unfold doNormalize_capAtMaxRep at hcap
          have hmr_u : m2 > maxRepUp := UInt64.lt_iff_toNat_lt.mpr hmr
          rw [if_pos hmr_u] at hcap
          by_cases hexp3 : e2 ≥ maxExponent
          · rw [if_pos hexp3] at hcap; exact absurd hcap (by intro h; cases h)
          · rw [if_neg hexp3] at hcap
            simp only [divu10] at hcap
            have : m3 = m2 / 10 := ((Prod.mk.inj (Except.ok.inj hcap)).1).symm
            rw [this, UInt64.toNat_div, show (10 : UInt64).toNat = 10 from rfl]
        rw [show maxRepUp.toNat = maxRepUpNat from rfl] at hmr
        omega
      · push_neg at hmr
        have hm3_eq : m3.toNat = m2.toNat := by
          unfold doNormalize_capAtMaxRep at hcap
          have hmr_u : ¬ m2 > maxRepUp := by
            intro hgt
            have := UInt64.lt_iff_toNat_lt.mp hgt; omega
          rw [if_neg hmr_u] at hcap
          have : m3 = m2 := ((Prod.mk.inj (Except.ok.inj hcap)).1).symm
          rw [this]
        rw [hminMant_v] at hm2_ge_min
        omega
    -- In the cusp range no digits were ever pushed: both scaling stages were the
    -- identity, so the guard is still the (empty-content) initial guard.
    have hcusp_state : maxRep.toNat < m3.toNat → f3 = 0 ∧ g3.empty = true := by
      intro hgt
      rw [maxRep_val] at hgt
      have hcap_id : m3 = m2 ∧ g3 = g2 := by
        unfold doNormalize_capAtMaxRep at hcap
        by_cases hmr : m2 > maxRepUp
        · exfalso
          rw [if_pos hmr] at hcap
          by_cases hexp3 : e2 ≥ maxExponent
          · rw [if_pos hexp3] at hcap; exact absurd hcap (by intro h; cases h)
          · rw [if_neg hexp3] at hcap
            simp only [divu10] at hcap
            have hm3_eq : m3 = m2 / 10 := ((Prod.mk.inj (Except.ok.inj hcap)).1).symm
            have hdiv : m3.toNat = m2.toNat / 10 := by
              rw [hm3_eq, UInt64.toNat_div, show (10 : UInt64).toNat = 10 from rfl]
            have h64 : m2.toNat < 2 ^ 64 := UInt64.toNat_lt_size m2
            omega
        · rw [if_neg hmr] at hcap
          have h1 : m3 = m2 := ((Prod.mk.inj (Except.ok.inj hcap)).1).symm
          have h2 : g3 = g2 := (Prod.mk.inj (Prod.mk.inj (Except.ok.inj hcap)).2).2.symm
          exact ⟨h1, h2⟩
      obtain ⟨hm32, hg32⟩ := hcap_id
      have hsd_id : g2 = g0 := by
        by_cases hm1_gt : m1 > largeRange.max
        · exfalso
          have hb := doNormalize_scaleDown_fired_output_le largeRange.max m1 e1 g0 m2 e2 g2 hm1_gt hsd
          have hm23 : m2.toNat = m3.toNat := by rw [hm32]
          omega
        · rw [doNormalize_scaleDown, dif_neg hm1_gt] at hsd
          exact ((Prod.mk.inj (Prod.mk.inj (Except.ok.inj hsd)).2).2).symm
      have hg3_g0 : g3 = g0 := by rw [hg32, hsd_id]
      have hempty : g3.empty = true := by
        rw [hg3_g0, hg0_def]
        by_cases hn : n.negative_
        · rw [if_pos hn]; decide
        · rw [if_neg hn]; decide
      refine ⟨?_, hempty⟩
      have hdig : g3.digits_ = 0 := by
        rw [hg3_g0, hg0_def]
        by_cases hn : n.negative_
        · rw [if_pos hn]; rfl
        · rw [if_neg hn]; rfl
      have hxb : g3.xbit_ = false := by
        rw [hg3_g0, hg0_def]
        by_cases hn : n.negative_
        · rw [if_pos hn]; rfl
        · rw [if_neg hn]; rfl
      exact represents_eq_zero_of_digits_zero_xbit_false hdig hxb hrep3
    have hres_mant_ne : res.mantissa_ ≠ 0 := by
      rw [hresult_eq] at hresult; exact hresult
    set res_pos : RoundResult :=
      { negative_ := false, mantissa_ := res.mantissa_, exponent_ := res.exponent_ } with hres_pos_def
    have hrup_pos : g3.doRoundUp false m3 e3 largeRange.min largeRange.max mode "Number::normalize 2"
        = .ok res_pos :=
      doRoundUp_false_from_ok g3 n.negative_ m3 e3 mode "Number::normalize 2" res hrup
    have h_result_abs : |result.toRat| = (res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_ := by
      rw [hresult_eq, abs_toRat_eq res.toNumber]; rfl
    have h_res_neg : result.negative_ = n.negative_ := by
      rw [hresult_eq]
      exact doRoundUp_negative_of_mant_ne g3 n.negative_ m3 e3 _ _ _ "Number::normalize 2" res hrup hres_mant_ne
    have h_g_sbit : g3.sbit_ = n.negative_ := by
      have h_g0_sbit : g0.sbit_ = n.negative_ := by
        rw [hg0_def]; by_cases hn : n.negative_
        · rw [if_pos hn, hn]; rfl
        · rw [if_neg hn]; rw [Bool.not_eq_true] at hn; rw [hn]; rfl
      have h_g2_sbit : g2.sbit_ = g0.sbit_ :=
        doNormalize_scaleDown_sbit_preserved largeRange.max m1 e1 g0 m2 e2 g2 hsd
      have h_g3_sbit : g3.sbit_ = g2.sbit_ :=
        doNormalize_capAtMaxRep_sbit_preserved m2 e2 g2 m3 e3 g3 hcap
      rw [h_g3_sbit, h_g2_sbit, h_g0_sbit]
    -- Strong lower bound: the capAtMaxRep stage exits at ≥ floorSucc (divide
    -- case: m2 > maxRepUp ⟹ m2/10 ≥ (maxRepUp+1)/10; identity: m3 = m2 ≥ 10^18).
    have hm3_ge_floorSucc : mantissaFloorSucc ≤ m3.toNat := by
      unfold doNormalize_capAtMaxRep at hcap
      by_cases hmr : m2 > maxRepUp
      · rw [if_pos hmr] at hcap
        by_cases hexp3 : e2 ≥ maxExponent
        · rw [if_pos hexp3] at hcap; exact absurd hcap (by intro h; cases h)
        · rw [if_neg hexp3] at hcap
          simp only [divu10] at hcap
          have hm3_eq : m3 = m2 / 10 := ((Prod.mk.inj (Except.ok.inj hcap)).1).symm
          rw [hm3_eq, UInt64.toNat_div, show (10 : UInt64).toNat = 10 from rfl]
          have hmr_nat : maxRepUp.toNat < m2.toNat := UInt64.lt_iff_toNat_lt.mp hmr
          rw [show maxRepUp.toNat = maxRepUpNat from rfl] at hmr_nat
          omega
      · rw [if_neg hmr] at hcap
        have hm3_eq : m3 = m2 := ((Prod.mk.inj (Except.ok.inj hcap)).1).symm
        rw [hm3_eq]; rw [hminMant_v] at hm2_ge_min; omega
    refine ⟨m3, e3, f3, g3, res_pos, hm3_ge_floor, hm3_le_maxRepUp_v, hf3_nn, hf3_lt, hfloor_constraint,
      hcusp_state, hval_total, hrup_pos, h_result_abs, hres_mant_ne, hrep3, h_res_neg, h_g_sbit,
      hm3_ge_floorSucc⟩

theorem normalize_algorithmic_facts_to_nearest (n result : Number)
    (hn_mant_ne : n.mantissa_ ≠ 0)
    (hok : n.normalize largeRange.min largeRange.max .to_nearest = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    ∃ (zm : UInt64) (ze : Int) (f : ℚ) (g : Guard) (res_pos : RoundResult),
      mantissaFloor ≤ zm.toNat ∧
      zm.toNat ≤ maxRepUp.toNat ∧
      (zm.toNat = mantissaFloor → (8 : ℚ) / 10 ≤ f) ∧
      |n.toRat| = ((zm.toNat : ℚ) + f) * 10 ^ ze ∧
      g.doRoundUp false zm ze largeRange.min largeRange.max .to_nearest "Number::normalize 2" = .ok res_pos ∧
      |result.toRat| = (res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_ ∧
      res_pos.mantissa_ ≠ 0 ∧
      represents g f ∧
      result.negative_ = n.negative_ := by
  obtain ⟨zm, ze, f, g, res_pos, hc1, hc2, _hc3, _hc4, hc5, _hc6, hc7, hc8, hc9, hc10, hc11, hc12, _hc13, _hc14⟩ :=
    normalize_algorithmic_facts_anyMode n result .to_nearest hn_mant_ne hok hresult
  exact ⟨zm, ze, f, g, res_pos, hc1, hc2, hc5, hc7, hc8, hc9, hc10, hc11, hc12⟩

end XRPL.Model.Protocol
