import XRPL.Properties.Protocol.Number.Common.Notation
import XRPL.Properties.Protocol.Number.Normalize.ToNearest.AlgorithmicFacts

set_option linter.style.longLine false
set_option linter.style.emptyLine false

namespace XRPL.Model.Protocol

/-- `doNormalize_scaleDown` preserves the guard's sign bit: every recursive step
pushes a digit (`Guard.push` keeps `sbit_`), and the base case returns `g`. -/
private lemma doNormalize_scaleDown_sbit_preserved
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
return `g` unchanged. -/
private lemma doNormalize_capAtMaxRep_sbit_preserved
    (m : UInt64) (e : Int) (g : Guard) (m' : UInt64) (e' : Int) (g' : Guard)
    (h : doNormalize_capAtMaxRep m e g = .ok (m', e', g')) :
    g'.sbit_ = g.sbit_ := by
  unfold doNormalize_capAtMaxRep at h
  by_cases hmr : m > maxRep
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

/-! # Algorithmic decomposition for `Number.normalize` (`.downward`)

Mirrors `normalize_algorithmic_facts` for `.to_nearest`, reusing the
mode-agnostic value-preservation lemmas `doNormalize_scaleUp_value`,
`doNormalize_scaleDown_correct`, `doNormalize_capAtMaxRep_correct`, and
`g0_represents_zero`. The `.downward` mode can round up, so the floor constraint
`zm.toNat = mantissaFloor → 8/10 ≤ f` is kept exactly as in `.to_nearest`. The
only difference from `.to_nearest` is the rounding-mode token. -/

/-- Combined value-preservation + side conditions for `Number.normalize`
(`.downward`).

When `n.normalize largeRange.min largeRange.max .downward = .ok result` with
a non-zero result mantissa, there is a pre-`doRoundUp` state `(zm, ze, g)`
together with a fraction `f` such that:
* the captured value `((zm.toNat : ℚ) + f) * 10 ^ ze` equals `|n.toRat|`,
* `g` represents `f`, with `mantissaFloor ≤ zm.toNat ≤ maxRep.toNat` and `0 ≤ f < 1`,
* the floor constraint `zm.toNat = mantissaFloor → 8/10 ≤ f`,
* `g.doRoundUp false zm ze ...` succeeds with the `positive` form of `result`. -/
theorem normalize_algorithmic_facts_downward (n result : Number)
    (hn_mant_ne : n.mantissa_ ≠ 0)
    (hok : n.normalize largeRange.min largeRange.max .downward = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    ∃ (zm : UInt64) (ze : Int) (f : ℚ) (g : Guard) (res_pos : RoundResult),
      mantissaFloor ≤ zm.toNat ∧
      zm.toNat ≤ maxRep.toNat ∧
      0 ≤ f ∧ f < 1 ∧
      (zm.toNat = mantissaFloor → (8 : ℚ) / 10 ≤ f) ∧
      |n.toRat| = ((zm.toNat : ℚ) + f) * 10 ^ ze ∧
      g.doRoundUp false zm ze largeRange.min largeRange.max .downward "Number::normalize 2" = .ok res_pos ∧
      |result.toRat| = (res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_ ∧
      res_pos.mantissa_ ≠ 0 ∧
      represents g f ∧
      result.negative_ = n.negative_ ∧
      g.sbit_ = n.negative_ := by
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
        g3.doRoundUp n.negative_ m3 e3 largeRange.min largeRange.max .downward "Number::normalize 2" = .ok res := by
      match hrup : g3.doRoundUp n.negative_ m3 e3 largeRange.min largeRange.max .downward "Number::normalize 2" with
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
    have hf3_nn : 0 ≤ f3 := represents_nonneg hrep3
    have hf3_lt : f3 < 1 := represents_lt_one hrep3
    have hfloor_constraint : m3.toNat = mantissaFloor → (8 : ℚ) / 10 ≤ f3 := by
      intro hm3_floor
      by_cases hmr : maxRep.toNat < m2.toNat
      · have hm3_div : m3.toNat = m2.toNat / 10 := by
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
      · exfalso
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
    have hres_mant_ne : res.mantissa_ ≠ 0 := by
      rw [hresult_eq] at hresult; exact hresult
    set res_pos : RoundResult :=
      { negative_ := false, mantissa_ := res.mantissa_, exponent_ := res.exponent_ } with hres_pos_def
    have hrup_pos : g3.doRoundUp false m3 e3 largeRange.min largeRange.max .downward "Number::normalize 2"
        = .ok res_pos :=
      doRoundUp_false_from_ok g3 n.negative_ m3 e3 .downward "Number::normalize 2" res hrup
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
    refine ⟨m3, e3, f3, g3, res_pos, hm3_ge_floor, hm3_le_maxRep_v, hf3_nn, hf3_lt, hfloor_constraint,
      hval_total, hrup_pos, h_result_abs, hres_mant_ne, hrep3, h_res_neg, h_g_sbit⟩

end XRPL.Model.Protocol
