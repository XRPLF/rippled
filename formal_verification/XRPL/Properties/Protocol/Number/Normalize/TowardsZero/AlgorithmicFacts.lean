import XRPL.Properties.Protocol.Number.Common.Notation
import XRPL.Properties.Protocol.Number.Normalize.ToNearest.AlgorithmicFacts

set_option linter.style.longLine false
set_option linter.style.emptyLine false

namespace XRPL.Model.Protocol

/-! # Algorithmic decomposition for `Number.normalize` (`.towards_zero`)

Mirrors `normalize_algorithmic_facts` for `.to_nearest`, reusing the
mode-agnostic value-preservation lemmas `doNormalize_scaleUp_value`,
`doNormalize_scaleDown_correct`, `doNormalize_capAtMaxRep_correct`, and
`g0_represents_zero`. The `.towards_zero` mode truncates, so instead of the
to-nearest floor constraint `8/10 ≤ f` we carry the plain residue bounds
`0 ≤ f ∧ f < 1`. -/

/-- Combined value-preservation + side conditions for `Number.normalize`
(`.towards_zero`).

When `n.normalize largeRange.min largeRange.max .towards_zero = .ok result` with
a non-zero result mantissa, there is a pre-`doRoundUp` state `(zm, ze, g)`
together with a fraction `f` such that:
* the captured value `((zm.toNat : ℚ) + f) * 10 ^ ze` equals `|n.toRat|`,
* `g` represents `f`, with `mantissaFloor ≤ zm.toNat ≤ maxRep.toNat` and `0 ≤ f < 1`,
* `g.doRoundUp false zm ze ...` succeeds with the `positive` form of `result`. -/
theorem normalize_algorithmic_facts_towards_zero (n result : Number)
    (hn_mant_ne : n.mantissa_ ≠ 0)
    (hok : n.normalize largeRange.min largeRange.max .towards_zero = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    ∃ (zm : UInt64) (ze : Int) (f : ℚ) (g : Guard) (res_pos : RoundResult),
      mantissaFloor ≤ zm.toNat ∧
      zm.toNat ≤ maxRep.toNat ∧
      0 ≤ f ∧ f < 1 ∧
      |n.toRat| = ((zm.toNat : ℚ) + f) * 10 ^ ze ∧
      g.doRoundUp false zm ze largeRange.min largeRange.max .towards_zero "Number::normalize 2" = .ok res_pos ∧
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
        g3.doRoundUp n.negative_ m3 e3 largeRange.min largeRange.max .towards_zero "Number::normalize 2" = .ok res := by
      match hrup : g3.doRoundUp n.negative_ m3 e3 largeRange.min largeRange.max .towards_zero "Number::normalize 2" with
      | .error e => rw [hrup] at hok; exact absurd hok (by intro h; cases h)
      | .ok res => exact ⟨res, rfl⟩
    rw [hrup] at hok
    simp only at hok
    have hresult_eq : result = res.toNumber := (Except.ok.inj hok).symm
    -- value preservation
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
    -- Build the positive doRoundUp result
    have hres_mant_ne : res.mantissa_ ≠ 0 := by
      rw [hresult_eq] at hresult; exact hresult
    set res_pos : RoundResult :=
      { negative_ := false, mantissa_ := res.mantissa_, exponent_ := res.exponent_ } with hres_pos_def
    have hrup_pos : g3.doRoundUp false m3 e3 largeRange.min largeRange.max .towards_zero "Number::normalize 2"
        = .ok res_pos :=
      doRoundUp_false_from_ok g3 n.negative_ m3 e3 .towards_zero "Number::normalize 2" res hrup
    have h_result_abs : |result.toRat| = (res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_ := by
      rw [hresult_eq, abs_toRat_eq res.toNumber]; rfl
    have h_res_neg : result.negative_ = n.negative_ := by
      rw [hresult_eq]
      exact doRoundUp_negative_of_mant_ne g3 n.negative_ m3 e3 _ _ _ "Number::normalize 2" res hrup hres_mant_ne
    refine ⟨m3, e3, f3, g3, res_pos, hm3_ge_floor, hm3_le_maxRep_v, hf3_nn, hf3_lt,
      hval_total, hrup_pos, h_result_abs, hres_mant_ne, hrep3, h_res_neg⟩

end XRPL.Model.Protocol
