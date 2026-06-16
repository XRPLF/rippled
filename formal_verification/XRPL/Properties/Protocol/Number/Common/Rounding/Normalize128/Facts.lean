import Mathlib.Tactic

import XRPL.Properties.Protocol.Number.Common.Notation
import XRPL.Properties.Protocol.Number.Common.Rounding.BitVec
import XRPL.Properties.Protocol.Number.Common.Rounding.Guard
import XRPL.Properties.Protocol.Number.Common.Rounding.DoRoundUp
import XRPL.Properties.Protocol.Number.Common.Rounding.Normalize

namespace XRPL.Model.Protocol

/-! # Value preservation and rounding for `doNormalize128`

`doNormalize128` is the tail of the diff-sign addition path:

  `scaleUp (exact) → doNormalize_scaleDown128 (pushes digits) → capAtMaxRep
     (one conditional push) → doRoundUp (rounds)`.

The true value entering the pipeline is `(M + δ) * 10^e` where `δ ∈ [0,1)` is
the sticky tail (`δ = 0` iff the sticky flag is off). The guard cannot
`represents` the tail tightly (its hidden-fraction bound is `1/10^16`), so we
thread TWO fractions through the digit-pushing stages:

* `φ` — the *true* discarded fraction (arbitrary in `[0,1)`), giving the exact
  value equation; and
* `f̃` — a *representable* shadow (`represents g f̃`), within slip `s` of `φ`.

Each push maps both through `(· + d)/10`, so the slip never grows. At the final
`doRoundUp`, the supTight bound applies to `f̃` and a triangle step absorbs the
slip (the mantissa there is `≥ 10^18`, the slip `< 1`). -/

/-! ## scaleUp: exact value, bounds -/

/-- `doNormalize128.scaleUp` preserves the value exactly and keeps the mantissa
positive and below `10^23` (it only scales while `m < 10^18`, so it never exceeds
`10^19`). The final conjunct records that it is either the identity or its
output is below `10^19` (one scaling step happened last at `m < 10^18`). -/
lemma doNormalize128_scaleUp_facts (minMantissa : UInt64) (m : UInt128) (e : Int)
    (hmin : minMantissa.toNat = 1000000000000000000)
    (hm_pos : 1 ≤ m.toNat) (hm_lt : m.toNat < 10 ^ 23) :
    let r := doNormalize128.scaleUp minMantissa m e
    ((r.1.toNat : ℚ) * 10 ^ r.2 = (m.toNat : ℚ) * 10 ^ e) ∧
    1 ≤ r.1.toNat ∧ r.1.toNat < 10 ^ 23 ∧ r.2 ≤ e ∧
    (r.1 = m ∧ r.2 = e ∨ r.1.toNat < 10 ^ 19) := by
  induction m, e using doNormalize128.scaleUp.induct (minMantissa := minMantissa) with
  | case1 m e hcond ih =>
    obtain ⟨h1, h2⟩ := hcond
    rw [show doNormalize128.scaleUp minMantissa m e
        = doNormalize128.scaleUp minMantissa (m * 10) (e - 1) from by
      rw [doNormalize128.scaleUp.eq_def]
      simp only [if_pos (And.intro h1 h2)]]
    have h_lt_min : m.toNat < 1000000000000000000 := by
      have := BitVec.lt_def.mp h1
      rwa [toNat_toUInt128, hmin] at this
    have h_mul : (m * 10 : UInt128).toNat = m.toNat * 10 := by
      have h10 : ((10 : UInt128)).toNat = 10 := by decide
      rw [BitVec.toNat_mul, h10]
      apply Nat.mod_eq_of_lt
      have : m.toNat * 10 < 10 ^ 19 := by omega
      have h2128 : (10 : ℕ) ^ 19 < 2 ^ 128 := by norm_num
      omega
    have hm_pos' : 1 ≤ (m * 10 : UInt128).toNat := by rw [h_mul]; omega
    have hm_lt' : (m * 10 : UInt128).toNat < 10 ^ 23 := by rw [h_mul]; omega
    obtain ⟨hval, hpos, hlt, hexp, hsize⟩ := ih hm_pos' hm_lt'
    refine ⟨?_, hpos, hlt, by omega, ?_⟩
    · rw [hval, h_mul]
      push_cast
      rw [show (10 : ℚ) ^ e = (10 : ℚ) ^ (e - 1) * 10 from by
        rw [show e = (e - 1) + 1 from by ring, zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0)]
        simp]
      ring
    · right
      rcases hsize with ⟨hm_eq, _⟩ | hlt19
      · rw [hm_eq, h_mul]; omega
      · exact hlt19
  | case2 m e hcond =>
    rw [show doNormalize128.scaleUp minMantissa m e = (m, e) from by
      rw [doNormalize128.scaleUp.eq_def]
      simp only [if_neg hcond]]
    exact ⟨rfl, hm_pos, hm_lt, le_refl _, Or.inl ⟨rfl, rfl⟩⟩

/-! ## scaleDown128: digit-push accumulation with a representable shadow -/

set_option maxHeartbeats 1600000 in
-- Functional induction over UInt128 digit pushes with per-step slip algebra is heartbeat-heavy.
/-- Value/`represents` accumulation through `doNormalize_scaleDown128`. Threads
the true fraction `φ` and a representable shadow `f̃` through each digit push.
The slip between them is tracked *at scale*: `|φ₂ − f̃₂|·10^e₂ ≤ |φ − f̃|·10^e`
(each push divides the slip by exactly the factor the exponent gains). -/
theorem doNormalize_scaleDown128_repr
    (maxMantissa : UInt64) (m : UInt128) (e : Int) (g : Guard)
    (_hmaxM : maxMantissa.toNat = 9999999999999999999)
    (φ ftilde : ℚ)
    (hφ_low : 0 ≤ φ) (hφ_le : φ ≤ 1)
    (hrep : represents g ftilde)
    (hm_lt : m.toNat < 10 ^ 23)
    (out : UInt128 × Int × Guard)
    (hok : doNormalize_scaleDown128 maxMantissa m e g = .ok out) :
    ∃ φ₂ ftilde₂ : ℚ,
      0 ≤ φ₂ ∧ φ₂ ≤ 1 ∧
      represents out.2.2 ftilde₂ ∧
      |φ₂ - ftilde₂| * (10 : ℚ) ^ out.2.1 ≤ |φ - ftilde| * (10 : ℚ) ^ e ∧
      ((m.toNat : ℚ) + φ) * 10 ^ e = ((out.1.toNat : ℚ) + φ₂) * 10 ^ out.2.1 ∧
      out.1.toNat ≤ maxMantissa.toNat ∧
      out.1.toNat < 10 ^ 23 ∧
      e ≤ out.2.1 ∧
      out.2.2.sbit_ = g.sbit_ ∧
      (g.xbit_ = true → out.2.2.xbit_ = true) ∧
      (0 < φ → 0 < φ₂) ∧
      (φ < 1 → φ₂ < 1) := by
  induction m, e, g using doNormalize_scaleDown128.induct (maxMantissa := maxMantissa)
    generalizing φ ftilde with
  | case1 m e g hgt hge =>
    -- error branch: m > maxMantissa and e ≥ maxExponent — contradicts hok.
    rw [show doNormalize_scaleDown128 maxMantissa m e g = .error "Number::normalize 1" from by
      rw [doNormalize_scaleDown128.eq_def]
      rw [dif_pos hgt, if_pos hge]] at hok
    exact absurd hok (by intro h; cases h)
  | case2 m e g hgt hnge ih =>
    -- step: m > maxMantissa, ¬ e ≥ maxExponent; push m % 10, recurse on m / 10.
    rw [show doNormalize_scaleDown128 maxMantissa m e g
        = doNormalize_scaleDown128 maxMantissa (m / 10) (e + 1) (g.push (toUInt64 (m % 10)))
        from by
      rw [doNormalize_scaleDown128.eq_def]
      rw [dif_pos hgt, if_neg hnge]] at hok
    have h10 : ((10 : UInt128)).toNat = 10 := by decide
    have h_div : (m / 10 : UInt128).toNat = m.toNat / 10 := by
      rw [BitVec.toNat_udiv, h10]
    have h_mod : (m % 10 : UInt128).toNat = m.toNat % 10 := by
      rw [BitVec.toNat_umod, h10]
    have h_mod_lt : (m % 10 : UInt128).toNat < 10 := by
      rw [h_mod]; exact Nat.mod_lt _ (by norm_num)
    have h_d_toNat : (toUInt64 (m % 10)).toNat = m.toNat % 10 := by
      rw [toNat_toUInt64 (by rw [h_mod]; have : m.toNat % 10 < 10 := Nat.mod_lt _ (by norm_num); omega), h_mod]
    have h_d_lt : (toUInt64 (m % 10)).toNat < 10 := by
      rw [h_d_toNat]; exact Nat.mod_lt _ (by norm_num)
    -- new fractions
    have hrep' : represents (g.push (toUInt64 (m % 10)))
        ((ftilde + ((toUInt64 (m % 10)).toNat : ℚ)) / 10) :=
      represents_push hrep h_d_lt
    have hφ'_nn : 0 ≤ (φ + ((toUInt64 (m % 10)).toNat : ℚ)) / 10 := by
      have hd_nn : (0 : ℚ) ≤ ((toUInt64 (m % 10)).toNat : ℚ) := Nat.cast_nonneg _
      apply div_nonneg (by linarith) (by norm_num)
    have hφ'_lt : (φ + ((toUInt64 (m % 10)).toNat : ℚ)) / 10 ≤ 1 := by
      rw [div_le_one (by norm_num : (0 : ℚ) < 10)]
      have : ((toUInt64 (m % 10)).toNat : ℚ) ≤ 9 := by
        exact_mod_cast Nat.lt_succ_iff.mp h_d_lt
      linarith
    have hslip_eq : |(φ + ((toUInt64 (m % 10)).toNat : ℚ)) / 10
        - (ftilde + ((toUInt64 (m % 10)).toNat : ℚ)) / 10| = |φ - ftilde| / 10 := by
      rw [show (φ + ((toUInt64 (m % 10)).toNat : ℚ)) / 10
            - (ftilde + ((toUInt64 (m % 10)).toNat : ℚ)) / 10
          = (φ - ftilde) / 10 from by ring]
      rw [abs_div, abs_of_nonneg (by norm_num : (0 : ℚ) ≤ 10)]
    have hm_lt' : (m / 10 : UInt128).toNat < 10 ^ 23 := by
      rw [h_div]
      have := Nat.div_le_self m.toNat 10
      omega
    obtain ⟨φ₂, ftilde₂, h2nn, h2lt, h2rep, h2slip, h2val, h2le, h2lt22, h2exp, h2sbit, h2xbit,
            h2pos, h2lt1⟩ :=
      ih ((φ + ((toUInt64 (m % 10)).toNat : ℚ)) / 10)
        ((ftilde + ((toUInt64 (m % 10)).toNat : ℚ)) / 10)
        hφ'_nn hφ'_lt hrep' hm_lt' hok
    have hslip_trans : |φ₂ - ftilde₂| * (10 : ℚ) ^ out.2.1 ≤ |φ - ftilde| * (10 : ℚ) ^ e := by
      calc |φ₂ - ftilde₂| * (10 : ℚ) ^ out.2.1
          ≤ |(φ + ((toUInt64 (m % 10)).toNat : ℚ)) / 10
              - (ftilde + ((toUInt64 (m % 10)).toNat : ℚ)) / 10| * (10 : ℚ) ^ (e + 1) := h2slip
        _ = (|φ - ftilde| / 10) * (10 : ℚ) ^ (e + 1) := by rw [hslip_eq]
        _ = |φ - ftilde| * (10 : ℚ) ^ e := by
            rw [show (10 : ℚ) ^ (e + 1) = 10 ^ e * 10 from by
              rw [zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0)]; simp]
            ring
    have h_push_sbit : (g.push (toUInt64 (m % 10))).sbit_ = g.sbit_ := rfl
    have h_push_xbit : g.xbit_ = true → (g.push (toUInt64 (m % 10))).xbit_ = true := by
      intro hx
      rw [xbit_push, hx, Bool.true_or]
    refine ⟨φ₂, ftilde₂, h2nn, h2lt, h2rep, hslip_trans, ?_, h2le, h2lt22, by omega,
      h2sbit.trans h_push_sbit, fun hx => h2xbit (h_push_xbit hx), ?_, ?_⟩
    · rw [← h2val]
      -- (m + φ)·10^e = (m/10 + (d + φ)/10)·10^(e+1)
      have h_decomp : (m.toNat : ℚ) = ((m / 10 : UInt128).toNat : ℚ) * 10
          + ((toUInt64 (m % 10)).toNat : ℚ) := by
        rw [h_div, h_d_toNat]
        have h_nat : m.toNat = m.toNat / 10 * 10 + m.toNat % 10 := by omega
        have h_q : (m.toNat : ℚ) = ((m.toNat / 10 * 10 + m.toNat % 10 : ℕ) : ℚ) := by
          exact_mod_cast congrArg (Nat.cast : ℕ → ℚ) h_nat
        rw [h_q]; push_cast; ring
      rw [h_decomp]
      rw [show (10 : ℚ) ^ (e + 1) = 10 ^ e * 10 from by
        rw [zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0)]; simp]
      field_simp
      ring
    · intro hφ
      apply h2pos
      have hd_nn : (0 : ℚ) ≤ ((toUInt64 (m % 10)).toNat : ℚ) := Nat.cast_nonneg _
      exact div_pos (by linarith) (by norm_num)
    · intro hφ
      apply h2lt1
      rw [div_lt_one (by norm_num : (0 : ℚ) < 10)]
      have : ((toUInt64 (m % 10)).toNat : ℚ) ≤ 9 := by
        exact_mod_cast Nat.lt_succ_iff.mp h_d_lt
      linarith
  | case3 m e g hgt =>
    -- exit: m ≤ maxMantissa.
    rw [show doNormalize_scaleDown128 maxMantissa m e g = .ok (m, e, g) from by
      rw [doNormalize_scaleDown128.eq_def]
      rw [dif_neg hgt]] at hok
    obtain rfl := (Except.ok.inj hok).symm
    have h_le : m.toNat ≤ maxMantissa.toNat := by
      by_contra h
      push_neg at h
      apply hgt
      change toUInt128 maxMantissa < m
      rw [BitVec.lt_def, toNat_toUInt128]
      exact h
    exact ⟨φ, ftilde, hφ_low, hφ_le, hrep, le_refl _, rfl, h_le, hm_lt, le_refl _,
      rfl, fun hx => hx, fun h => h, fun h => h⟩

/-! ## capAtMaxRep: one conditional push -/

/-- Value/`represents` accumulation through `doNormalize_capAtMaxRep` (at most one
push). Inputs satisfy `10^18 ≤ m ≤ maxMantissa`; the output mantissa lies in
`[mantissaFloorSucc, maxRepUp]` — in particular strictly above `mantissaFloor`,
which makes the supTight floor-constraint vacuous downstream. -/
theorem doNormalize_capAtMaxRep_repr
    (m : UInt64) (e : Int) (g : Guard)
    (hm_ge : 1000000000000000000 ≤ m.toNat)
    (hm_le : m.toNat ≤ 9999999999999999999)
    (φ ftilde : ℚ)
    (hφ_low : 0 ≤ φ) (hφ_le : φ ≤ 1)
    (hrep : represents g ftilde)
    (out : UInt64 × Int × Guard)
    (hok : doNormalize_capAtMaxRep m e g = .ok out) :
    ∃ φ₂ ftilde₂ : ℚ,
      0 ≤ φ₂ ∧ φ₂ ≤ 1 ∧
      represents out.2.2 ftilde₂ ∧
      |φ₂ - ftilde₂| * (10 : ℚ) ^ out.2.1 ≤ |φ - ftilde| * (10 : ℚ) ^ e ∧
      ((m.toNat : ℚ) + φ) * 10 ^ e = ((out.1.toNat : ℚ) + φ₂) * 10 ^ out.2.1 ∧
      (mantissaFloor : ℕ) < out.1.toNat ∧
      out.1.toNat ≤ maxRepUp.toNat ∧
      e ≤ out.2.1 ∧
      out.2.2.sbit_ = g.sbit_ ∧
      (g.xbit_ = true → out.2.2.xbit_ = true) ∧
      (0 < φ → 0 < φ₂) ∧
      (φ < 1 → φ₂ < 1) := by
  unfold doNormalize_capAtMaxRep at hok
  by_cases h_gt : m > maxRepUp
  · rw [if_pos h_gt] at hok
    by_cases h_ge : e ≥ maxExponent
    · rw [if_pos h_ge] at hok
      exact absurd hok (by intro h; cases h)
    · rw [if_neg h_ge] at hok
      unfold divu10 at hok
      simp only [] at hok
      obtain rfl := (Except.ok.inj hok).symm
      have h_gt_nat : maxRepUp.toNat < m.toNat := UInt64.lt_iff_toNat_lt.mp h_gt
      rw [show maxRepUp.toNat = maxRepUpNat from rfl] at h_gt_nat
      have h10 : (10 : UInt64).toNat = 10 := uint64_ten_toNat
      have h_div : (m / 10).toNat = m.toNat / 10 := by rw [UInt64.toNat_div, h10]
      have h_mod : (m % 10).toNat = m.toNat % 10 := by rw [UInt64.toNat_mod, h10]
      have h_d_lt : (m % 10).toNat < 10 := by rw [h_mod]; exact Nat.mod_lt _ (by norm_num)
      have hrep' : represents (g.push (m % 10)) ((ftilde + ((m % 10).toNat : ℚ)) / 10) :=
        represents_push hrep h_d_lt
      have hφ'_nn : 0 ≤ (φ + ((m % 10).toNat : ℚ)) / 10 := by
        have hd_nn : (0 : ℚ) ≤ ((m % 10).toNat : ℚ) := Nat.cast_nonneg _
        apply div_nonneg (by linarith) (by norm_num)
      have hφ'_lt : (φ + ((m % 10).toNat : ℚ)) / 10 ≤ 1 := by
        rw [div_le_one (by norm_num : (0 : ℚ) < 10)]
        have : ((m % 10).toNat : ℚ) ≤ 9 := by exact_mod_cast Nat.lt_succ_iff.mp h_d_lt
        linarith
      have h_push_xbit : g.xbit_ = true → (g.push (m % 10)).xbit_ = true := by
        intro hx
        rw [xbit_push, hx, Bool.true_or]
      refine ⟨(φ + ((m % 10).toNat : ℚ)) / 10, (ftilde + ((m % 10).toNat : ℚ)) / 10,
        hφ'_nn, hφ'_lt, hrep', ?_, ?_, ?_, ?_, by change e ≤ e + 1; omega, rfl, h_push_xbit,
        ?_, ?_⟩
      · -- slip at scale: (|φ - f̃|/10) · 10^(e+1) = |φ - f̃| · 10^e
        rw [show (φ + ((m % 10).toNat : ℚ)) / 10 - (ftilde + ((m % 10).toNat : ℚ)) / 10
              = (φ - ftilde) / 10 from by ring,
            abs_div, abs_of_nonneg (by norm_num : (0 : ℚ) ≤ 10)]
        rw [show (10 : ℚ) ^ (e + 1) = 10 ^ e * 10 from by
          rw [zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0)]; simp]
        rw [show |φ - ftilde| / 10 * (10 ^ e * 10) = |φ - ftilde| * 10 ^ e from by ring]
      · -- value: (m + φ)·10^e = (m/10 + (d + φ)/10)·10^(e+1)
        have h_decomp : (m.toNat : ℚ) = ((m / 10).toNat : ℚ) * 10 + ((m % 10).toNat : ℚ) := by
          rw [h_div, h_mod]
          have h_nat : m.toNat = m.toNat / 10 * 10 + m.toNat % 10 := by omega
          have h_q : (m.toNat : ℚ) = ((m.toNat / 10 * 10 + m.toNat % 10 : ℕ) : ℚ) := by
            exact_mod_cast congrArg (Nat.cast : ℕ → ℚ) h_nat
          rw [h_q]; push_cast; ring
        rw [h_decomp]
        rw [show (10 : ℚ) ^ (e + 1) = 10 ^ e * 10 from by
          rw [zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0)]; simp]
        field_simp
        ring
      · -- floor < m/10: m ≥ maxRepUp + 1 → m/10 ≥ 922337203685477581 = floorSucc
        rw [h_div]
        have : m.toNat / 10 ≥ 9223372036854775811 / 10 := Nat.div_le_div_right (by omega)
        have h_calc : (9223372036854775811 : ℕ) / 10 = 922337203685477581 := by norm_num
        omega
      · -- m/10 ≤ maxRepUp
        rw [h_div, show maxRepUp.toNat = maxRepUpNat from rfl]
        have : m.toNat / 10 ≤ 9999999999999999999 / 10 := Nat.div_le_div_right hm_le
        have h_calc : (9999999999999999999 : ℕ) / 10 = 999999999999999999 := by norm_num
        omega
      · intro hφ
        have hd_nn : (0 : ℚ) ≤ ((m % 10).toNat : ℚ) := Nat.cast_nonneg _
        exact div_pos (by linarith) (by norm_num)
      · intro hφ
        rw [div_lt_one (by norm_num : (0 : ℚ) < 10)]
        have : ((m % 10).toNat : ℚ) ≤ 9 := by exact_mod_cast Nat.lt_succ_iff.mp h_d_lt
        linarith
  · rw [if_neg h_gt] at hok
    obtain rfl := (Except.ok.inj hok).symm
    have h_le_nat : m.toNat ≤ maxRepUp.toNat := by
      by_contra h
      push_neg at h
      exact h_gt (UInt64.lt_iff_toNat_lt.mpr h)
    refine ⟨φ, ftilde, hφ_low, hφ_le, hrep, le_refl _, rfl, ?_, h_le_nat, le_refl _,
      rfl, fun hx => hx, fun h => h, fun h => h⟩
    change (mantissaFloor : ℕ) < m.toNat
    omega

/-! ## Initial guards -/

/-- The sign-seeded initial guard represents `0`. -/
lemma represents_initial128 (zn : Bool) :
    represents (if zn then Guard.new.set_negative else Guard.new) 0 := by
  obtain ⟨x0, hx0_nn, hx0_lt, hf0, hxb0, hall0⟩ := represents_new
  have hdig : (if zn then Guard.new.set_negative else Guard.new).digits_ = Guard.new.digits_ := by
    by_cases h : zn
    · rw [if_pos h]; rfl
    · rw [if_neg h]
  have hxbit : (if zn then Guard.new.set_negative else Guard.new).xbit_ = Guard.new.xbit_ := by
    by_cases h : zn
    · rw [if_pos h]; rfl
    · rw [if_neg h]
  refine ⟨x0, hx0_nn, hx0_lt, ?_, ?_, ?_⟩
  · rw [hdig]; exact hf0
  · rw [hxbit]; exact hxb0
  · rw [hdig]; exact hall0

/-- The sticky-seeded initial guard (digits `0`, `xbit = true`) represents the
fixed positive shadow `1/10^17`. -/
lemma represents_sticky_initial128 (zn : Bool) :
    represents ((if zn then Guard.new.set_negative else Guard.new).set_sticky)
      ((1 : ℚ) / 10 ^ 17) := by
  obtain ⟨x0, hx0_nn, hx0_lt, hf0, hxb0, hall0⟩ := represents_new
  -- From `represents Guard.new 0`: the decimal value of the empty digits is 0.
  have hdv0 : (decimalValue Guard.new.digits_ : ℚ) = 0 := by
    have hdv_nn : (0 : ℚ) ≤ (decimalValue Guard.new.digits_ : ℚ) / 10 ^ 16 := by positivity
    have h16 : (0 : ℚ) < 10 ^ 16 := by positivity
    nlinarith [hf0, hx0_nn]
  have hdig : ((if zn then Guard.new.set_negative else Guard.new).set_sticky).digits_
      = Guard.new.digits_ := by
    by_cases h : zn
    · rw [if_pos h]; rfl
    · rw [if_neg h]; rfl
  have hxbit : ((if zn then Guard.new.set_negative else Guard.new).set_sticky).xbit_ = true := by
    by_cases h : zn
    · rw [if_pos h]; rfl
    · rw [if_neg h]; rfl
  refine ⟨(1 : ℚ) / 10 ^ 17, by positivity, by norm_num, ?_, ?_, ?_⟩
  · rw [hdig, hdv0]
    norm_num
  · rw [hxbit]
    constructor
    · intro _; positivity
    · intro _; rfl
  · rw [hdig]; exact hall0

end XRPL.Model.Protocol
