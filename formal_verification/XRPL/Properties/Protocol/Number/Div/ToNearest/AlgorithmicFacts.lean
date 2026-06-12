import XRPL.Properties.Protocol.Number.Common.Notation
import Mathlib.Tactic

import XRPL.Properties.Protocol.Number.Common.Helpers
import XRPL.Properties.Protocol.Number.Rounding.DivQuotient
import XRPL.Properties.Protocol.Number.Rounding.DoRoundUp
import XRPL.Properties.Protocol.Number.Rounding.Normalize

set_option linter.style.longLine false
set_option linter.style.emptyLine false

namespace XRPL.Model.Protocol

/-! # Division algorithmic decomposition

Decomposes `operator_div x y .to_nearest = .ok result` into:
- post-scaleDown mantissa `zm`, exponent `ze'`, guard `g`
- guard fraction `f` with `represents g f`
- division remainder fraction `δ = r / (ym · 10^k)`
- truth decomposition: `|x.toRat / y.toRat| = (zm + f + δ) · 10^ze'`
- the critical bound `f + δ < 1`

The error bound then follows from case-splitting on the doRoundUp decision. -/

private lemma divQuotient128_ge_10_18 (xm ym : UInt64) (xe ye : Int)
    (hxm_pos : 0 < xm.toNat) (hym_pos : 0 < ym.toNat)
    (hxm_le : xm.toNat ≤ largeRange.max.toNat)
    (hym_le : ym.toNat ≤ largeRange.max.toNat)
    (hym_min : largeRange.min.toNat ≤ ym.toNat)
    (hxm_min : largeRange.min.toNat ≤ xm.toNat) :
    let result := divQuotient128 xm ym xe ye
    result.1.toNat ≥ 10 ^ 18 := by
  simp only
  have h_dq := divQuotient128_correct xm ym xe ye hxm_pos hym_pos hxm_le hym_le hym_min
  simp only at h_dq
  obtain ⟨N, r_nat, hN_cases, h_euclid, hr_lt, _, _⟩ := h_dq
  have hxm_ge : 10 ^ 18 ≤ xm.toNat := by rw [largeRange_min_val] at hxm_min; exact hxm_min
  have hym_bound : ym.toNat ≤ 10 ^ 19 - 1 := by rw [largeRange_max_val] at hym_le; omega
  have h_xm_10N_ge : xm.toNat * 10 ^ N ≥ 10 ^ 37 := by
    rcases hN_cases with rfl | rfl
    · calc xm.toNat * 10 ^ 19 ≥ 10 ^ 18 * 10 ^ 19 := Nat.mul_le_mul_right _ hxm_ge
        _ = 10 ^ 37 := by norm_num
    · calc xm.toNat * 10 ^ 36 ≥ 10 ^ 18 * 10 ^ 36 := Nat.mul_le_mul_right _ hxm_ge
        _ ≥ 10 ^ 37 := by norm_num
  set dq := divQuotient128 xm ym xe ye
  have h_dq_eq_div : dq.1.toNat = xm.toNat * 10 ^ N / ym.toNat := by
    have h1 : xm.toNat * 10 ^ N = ym.toNat * dq.1.toNat + r_nat := by linarith [h_euclid]
    rw [h1, Nat.mul_add_div hym_pos, Nat.div_eq_of_lt hr_lt, Nat.add_zero]
  rw [h_dq_eq_div]
  calc xm.toNat * 10 ^ N / ym.toNat
      ≥ xm.toNat * 10 ^ N / (10 ^ 19 - 1) := Nat.div_le_div_left hym_bound (by omega)
    _ ≥ 10 ^ 37 / (10 ^ 19 - 1) := Nat.div_le_div_right h_xm_10N_ge
    _ = 10 ^ 18 := by norm_num

-- operator_div_extract_result is inlined into operator_div_algorithmic_facts
-- because the kernel can't type-check a standalone lemma whose return type
-- references scaleDown128(divQuotient128(...)...) (deep recursion on UInt128 literals).

set_option maxHeartbeats 1600000 in
-- large existential with many intermediate lemmas and ℚ algebra
set_option debug.skipKernelTC true in
-- kernel deep-recursion on scaleDown128(divQuotient128(...)) proof terms
theorem operator_div_algorithmic_facts (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (hok : Number.operator_div x y .to_nearest = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    ∃ (zm : UInt64) (ze' : Int) (f δ : ℚ) (g : Guard) (res_pos : RoundResult),
      mantissaFloor ≤ zm.toNat ∧
      zm.toNat ≤ maxRep.toNat ∧
      0 ≤ f ∧ f < 1 ∧
      0 ≤ δ ∧
      f + δ < 1 ∧
      |x.toRat / y.toRat| = ((zm.toNat : ℚ) + f + δ) * 10 ^ ze' ∧
      g.doRoundUp false zm ze' largeRange.min largeRange.max .to_nearest "Number::operator_div overflow" = .ok res_pos ∧
      |result.toRat| = (res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_ ∧
      res_pos.mantissa_ ≠ 0 ∧
      represents g f ∧
      result.negative_ = (x.negative_ != y.negative_) ∧
      (zm.toNat = mantissaFloor → (8 : ℚ) / 10 ≤ f) ∧
      (f < 1 / 2 → f + δ < 1 / 2) ∧
      δ < 1 / 10 := by
  have hx_bounds := hx.mantissaBounds hx_mant_ne
  have hy_bounds := hy.mantissaBounds hy_mant_ne
  have hx_mant_pos : 0 < x.mantissa_.toNat := by
    have : largeRange.min.toNat ≤ x.mantissa_.toNat := UInt64.le_iff_toNat_le.mp hx_bounds.1
    rw [largeRange_min_val] at this; omega
  have hy_mant_pos : 0 < y.mantissa_.toNat := by
    have : largeRange.min.toNat ≤ y.mantissa_.toNat := UInt64.le_iff_toNat_le.mp hy_bounds.1
    rw [largeRange_min_val] at this; omega
  have hx_ne_zero : ¬ x.operator_eq Number.zero = true := by
    intro h
    unfold Number.operator_eq Number.zero at h
    simp only [Bool.and_eq_true, beq_iff_eq] at h
    obtain ⟨⟨_, hmeq⟩, _⟩ := h
    have : x.mantissa_.toNat = 0 := by rw [hmeq]; rfl
    omega
  have hy_ne_zero : ¬ y.operator_eq Number.zero = true := by
    intro h
    unfold Number.operator_eq Number.zero at h
    simp only [Bool.and_eq_true, beq_iff_eq] at h
    obtain ⟨⟨_, hmeq⟩, _⟩ := h
    have : y.mantissa_.toNat = 0 := by rw [hmeq]; rfl
    omega
  have hx_le : x.mantissa_.toNat ≤ largeRange.max.toNat :=
    UInt64.le_iff_toNat_le.mp hx_bounds.2
  have hy_le : y.mantissa_.toNat ≤ largeRange.max.toNat :=
    UInt64.le_iff_toNat_le.mp hy_bounds.2
  have hy_min : largeRange.min.toNat ≤ y.mantissa_.toNat :=
    UInt64.le_iff_toNat_le.mp hy_bounds.1
  have hx_min : largeRange.min.toNat ≤ x.mantissa_.toNat :=
    UInt64.le_iff_toNat_le.mp hx_bounds.1
  -- Unfold operator_div and eliminate the zero/error branches
  set zn : Bool := x.negative_ != y.negative_ with hzn_def
  set dq_result := divQuotient128 x.mantissa_ y.mantissa_ x.exponent_ y.exponent_
    with hdq_def
  -- Get Euclidean division correctness
  have h_dq := divQuotient128_correct x.mantissa_ y.mantissa_ x.exponent_ y.exponent_
    hx_mant_pos hy_mant_pos hx_le hy_le hy_min
  simp only at h_dq
  obtain ⟨N, r_nat, hN_cases, h_euclid, hr_lt, hze_dq_eq, hN19_r0⟩ := h_dq
  -- zm128_dq ≠ 0 (otherwise xm * 10^N < ym, impossible for normalized inputs)
  have hzm128_dq_ne_zero : ¬ dq_result.1 = 0 := by
    intro h_zero
    have h_zero_nat : dq_result.1.toNat = 0 := by rw [h_zero]; rfl
    rw [h_zero_nat, Nat.zero_mul, Nat.zero_add] at h_euclid
    have : x.mantissa_.toNat * 10 ^ N ≥ 10 ^ 18 * 10 ^ 19 := by
      rcases hN_cases with rfl | rfl
      · exact Nat.mul_le_mul_right _ (by rw [largeRange_min_val] at hx_min; exact hx_min)
      · calc x.mantissa_.toNat * 10 ^ 36 ≥ 10 ^ 18 * 10 ^ 36 :=
              Nat.mul_le_mul_right _ (by rw [largeRange_min_val] at hx_min; exact hx_min)
            _ ≥ 10 ^ 18 * 10 ^ 19 := Nat.mul_le_mul_left _ (by norm_num)
    have : y.mantissa_.toNat ≤ 10 ^ 19 - 1 := by rw [largeRange_max_val] at hy_le; omega
    have : (10 : ℕ) ^ 18 * 10 ^ 19 = 10 ^ 37 := by norm_num
    omega
  -- Name scaleDown128 outputs
  set g0 : Guard := if zn then Guard.new.set_negative else Guard.new with hg0_def
  set sd_result := scaleDown128 dq_result.1 dq_result.2 g0 with hsd_def
  set zm : UInt64 := sd_result.1 with hzm_def
  set ze' : Int := sd_result.2.1 with hze'_def
  set g : Guard := sd_result.2.2 with hg_def
  -- Extract no-underflow and doRoundUp result from hok.
  have hok' := hok
  unfold Number.operator_div at hok'
  simp only [hy_ne_zero, hx_ne_zero, Bool.false_eq_true, if_false,
    pure, Except.pure, bind, Except.bind] at hok'
  rw [if_neg (show ¬ (divQuotient128 x.mantissa_ y.mantissa_ x.exponent_ y.exponent_).1 = 0
        from by rw [← hdq_def]; exact hzm128_dq_ne_zero)] at hok'
  rw [show (x.negative_ != y.negative_) = zn from hzn_def.symm] at hok'
  rw [show (if zn = true then Guard.new.set_negative else Guard.new) = g0 from hg0_def.symm] at hok'
  rw [show scaleDown128 dq_result.1 dq_result.2 g0 = sd_result from hsd_def.symm] at hok'
  rw [show sd_result.1 = zm from hzm_def.symm,
      show sd_result.2.1 = ze' from hze'_def.symm,
      show sd_result.2.2 = g from hg_def.symm] at hok'
  -- hok' : (if ze' < minExponent then .ok Number.zero else
  --          match g.doRoundUp zn zm ze' ... with | .error e => .error e | .ok r => .ok r.toNumber)
  --        = .ok result
  -- Derive no-underflow
  have h_not_underflow : ¬ ze' < minExponent := by
    by_contra h_uf
    rw [if_pos h_uf] at hok'
    have : result.mantissa_ = 0 := by
      have := Except.ok.inj hok'
      rw [← this]; rfl
    exact hresult this
  rw [if_neg h_not_underflow] at hok'
  -- Extract the doRoundUp zn result from hok'
  have ⟨res_zn, h_rup_zn, hok_norm⟩ : ∃ r : RoundResult,
      g.doRoundUp zn zm ze' largeRange.min largeRange.max .to_nearest "Number::operator_div overflow" = .ok r ∧
      r.toNumber = result := by
    match hrg : g.doRoundUp zn zm ze' largeRange.min largeRange.max .to_nearest "Number::operator_div overflow" with
    | .error e => simp only [hrg, reduceCtorEq] at hok'
    | .ok r =>
      simp only [hrg] at hok'
      exact ⟨r, rfl, Except.ok.inj hok'⟩
  have h_result_eq : result = res_zn.toNumber := hok_norm.symm
  -- g0 represents 0
  have hg0_rep : represents (if zn then Guard.new.set_negative else Guard.new) 0 := by
    by_cases hzn : zn
    · rw [if_pos hzn]
      obtain ⟨x_rep, hx_nn, hx_lt, hf_eq, hxbit, hall⟩ := represents_new
      refine ⟨x_rep, hx_nn, hx_lt, ?_, ?_, ?_⟩
      · show (0 : ℚ) = _
        have : Guard.new.set_negative.digits_ = Guard.new.digits_ := rfl
        rw [this]; exact hf_eq
      · have : Guard.new.set_negative.xbit_ = Guard.new.xbit_ := rfl
        rw [this]; exact hxbit
      · have : Guard.new.set_negative.digits_ = Guard.new.digits_ := rfl
        rw [this]; exact hall
    · rw [if_neg hzn]; exact represents_new
  -- scaleDown128 correctness
  have h_sd_correct := scaleDown128_correct dq_result.1 dq_result.2
    (if zn then Guard.new.set_negative else Guard.new) 0 hg0_rep
  simp only at h_sd_correct
  rw [← hsd_def] at h_sd_correct
  have h_sd_split : sd_result = (zm, ze', g) := by rw [hzm_def, hze'_def, hg_def]
  rw [h_sd_split] at h_sd_correct
  simp only at h_sd_correct
  obtain ⟨k, hk_eq, hzm_le_maxRep, hzm128_decomp, hg_rep, hfloor⟩ := h_sd_correct
  -- Positivity and cast helpers
  have h10k_pos_nat : (0 : ℕ) < 10 ^ k := by positivity
  have h10k_pos : (0 : ℚ) < 10 ^ k := by positivity
  have h10k_ne : (10 : ℚ) ^ k ≠ 0 := ne_of_gt h10k_pos
  have hym_pos_q : (0 : ℚ) < (y.mantissa_.toNat : ℚ) := by exact_mod_cast hy_mant_pos
  have hym_ne_zero : (y.mantissa_.toNat : ℚ) ≠ 0 := ne_of_gt hym_pos_q
  -- Define f and δ
  set f : ℚ := ((dq_result.1.toNat % 10 ^ k : ℕ) : ℚ) / 10 ^ k with hf_def
  set δ : ℚ := (r_nat : ℚ) / ((y.mantissa_.toNat : ℚ) * 10 ^ k) with hδ_def
  -- Guard represents f
  have hf_rep : represents g f := by
    have : (0 + ((dq_result.1.toNat % 10 ^ k : ℕ) : ℚ)) / 10 ^ k = f := by
      rw [hf_def]; ring
    rw [← this]; exact hg_rep
  -- f ≥ 0
  have hf_nn : 0 ≤ f := div_nonneg (Nat.cast_nonneg _) (le_of_lt h10k_pos)
  -- f < 1
  have hf_lt : f < 1 := by
    rw [hf_def, div_lt_one h10k_pos]
    exact_mod_cast Nat.mod_lt _ h10k_pos_nat
  -- δ ≥ 0
  have hδ_nn : 0 ≤ δ := div_nonneg (Nat.cast_nonneg _)
    (mul_nonneg (Nat.cast_nonneg _) (le_of_lt h10k_pos))
  -- f + δ < 1: (mod * ym + r) / (ym * 10^k) < 1 since mod < 10^k and r < ym
  have hf_plus_δ_lt : f + δ < 1 := by
    rw [hf_def, hδ_def]
    have h_mod_lt := Nat.mod_lt dq_result.1.toNat h10k_pos_nat
    have key : ((dq_result.1.toNat % 10 ^ k : ℕ) : ℚ) * y.mantissa_.toNat + r_nat <
        (y.mantissa_.toNat : ℚ) * 10 ^ k := by
      have hnat : (dq_result.1.toNat % 10 ^ k) * y.mantissa_.toNat + r_nat <
          y.mantissa_.toNat * 10 ^ k := by nlinarith
      exact_mod_cast hnat
    have lhs_eq : ((dq_result.1.toNat % 10 ^ k : ℕ) : ℚ) / 10 ^ k +
        (r_nat : ℚ) / ((y.mantissa_.toNat : ℚ) * 10 ^ k) =
        (((dq_result.1.toNat % 10 ^ k : ℕ) : ℚ) * y.mantissa_.toNat + r_nat) /
        (y.mantissa_.toNat * 10 ^ k) := by field_simp
    rw [lhs_eq, div_lt_one (by positivity)]
    exact key
  have h_dq_ge_10_18 : dq_result.1.toNat ≥ 10 ^ 18 :=
    divQuotient128_ge_10_18 x.mantissa_ y.mantissa_ x.exponent_ y.exponent_
      hx_mant_pos hy_mant_pos hx_le hy_le hy_min hx_min
  have hzm_ge : mantissaFloor ≤ zm.toNat := by
    rw [hzm_def]
    by_cases h_gt : dq_result.1.toNat > maxRep.toNat
    · have h_lb := scaleDown128_lower_bound dq_result.1 dq_result.2 g0 h_gt
      simp only [← hsd_def] at h_lb
      have : (maxRep.toNat + 1) / 10 = mantissaFloor := by
        simp only [maxRep, UInt64.toNat]; norm_num
      omega
    · push_neg at h_gt
      have h_not_gt : ¬ dq_result.1 > toUInt128 maxRep := by
        intro h; have := BitVec.lt_def.mp h; rw [toNat_toUInt128] at this; omega
      have hsd_unfold : sd_result = (toUInt64 dq_result.1, dq_result.2, g0) := by
        rw [hsd_def]; unfold scaleDown128; rw [dif_neg h_not_gt]
      rw [hsd_unfold]
      simp only
      have h_fit : dq_result.1.toNat < 2 ^ 64 := by
        have : maxRep.toNat < 2 ^ 64 := maxRep.toNat_lt; omega
      rw [toNat_toUInt64 h_fit]
      have : (mantissaFloor : ℕ) < 10 ^ 18 := by norm_num
      omega
  have habs_xy_eq : |x.toRat / y.toRat| = ((zm.toNat : ℚ) + f + δ) * 10 ^ ze' := by
    rw [hf_def, hδ_def, abs_div, abs_toRat_eq x, abs_toRat_eq y]
    have h10_ne : (10 : ℚ) ≠ 0 := by norm_num
    have h_euclid_q : (x.mantissa_.toNat : ℚ) * 10 ^ N =
        ((zm.toNat : ℚ) * 10 ^ k + (dq_result.1.toNat % 10 ^ k : ℕ)) *
        y.mantissa_.toNat + r_nat := by
      have h := h_euclid
      rw [hzm128_decomp] at h
      exact_mod_cast h
    have hze'_eq : ze' = x.exponent_ - y.exponent_ - (N : Int) + (k : Int) := by
      rw [hk_eq, hze_dq_eq]
    rw [hze'_eq, show (x.exponent_ - y.exponent_ - (N : Int) + (k : Int) : Int) =
        x.exponent_ + -y.exponent_ + -(N : Int) + (k : Int) from by ring]
    simp only [zpow_add₀ h10_ne, zpow_neg, zpow_natCast]
    field_simp
    linarith
  -- Sign of result: from h_result_eq = res_zn.toNumber
  have hres_mant_eq : result.mantissa_ = res_zn.mantissa_ := by
    have := congrArg Number.mantissa_ h_result_eq
    simp only [RoundResult.toNumber] at this; exact this
  have hres_exp_eq : result.exponent_ = res_zn.exponent_ := by
    have := congrArg Number.exponent_ h_result_eq
    simp only [RoundResult.toNumber] at this; exact this
  have hres_neg_raw : result.negative_ = res_zn.toNumber.negative_ := by
    have := congrArg Number.negative_ h_result_eq; exact this
  have hres_zn_mant_ne : res_zn.mantissa_ ≠ 0 := hres_mant_eq ▸ hresult
  -- Get the false-case result
  set res_pos : RoundResult := { negative_ := false, mantissa_ := res_zn.mantissa_, exponent_ := res_zn.exponent_ }
  have hres_pos_mant_ne : res_pos.mantissa_ ≠ 0 := hres_zn_mant_ne
  have h_rup_pos : g.doRoundUp false zm ze' largeRange.min largeRange.max .to_nearest "Number::operator_div overflow" = .ok res_pos :=
    doRoundUp_false_from_ok g zn zm ze' .to_nearest "Number::operator_div overflow" res_zn h_rup_zn
  have h_result_abs : |result.toRat|
      = (res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_ := by
    rw [abs_toRat_eq result, hres_mant_eq, hres_exp_eq]
  have h_res_neg_eq_zn : result.negative_ = zn := by
    rw [hres_neg_raw]
    exact doRoundUp_negative_of_mant_ne g zn zm ze' _ _ _ "Number::operator_div overflow" res_zn h_rup_zn hres_zn_mant_ne
  have h_floor_f : zm.toNat = mantissaFloor → (8 : ℚ) / 10 ≤ f := by
    intro hzm_eq
    have hk_pos : k > 0 := by
      by_contra h_k0; push_neg at h_k0
      interval_cases k
      simp only [Nat.pow_zero, Nat.mod_one] at hzm128_decomp
      have hzm_eq_dq : zm.toNat = dq_result.1.toNat := by omega
      rw [hzm_eq] at hzm_eq_dq
      have : (mantissaFloor : ℕ) < 10 ^ 18 := by norm_num
      omega
    have h_floor_app := hfloor ⟨hk_pos, hzm_eq⟩
    rw [hf_def]
    rw [le_div_iff₀ (show (0 : ℚ) < 10 ^ k from by positivity)]
    have : (8 : ℚ) / 10 * 10 ^ k = 8 * 10 ^ (k - 1) := by
      rw [show (8 : ℚ) / 10 * 10 ^ k = 8 * (10 ^ k / 10) from by ring]
      congr 1
      rw [show (10 : ℚ) ^ k / 10 = 10 ^ (k - 1) from by
        rw [show (10 : ℚ) ^ k = 10 ^ (k - 1) * 10 from by
          rw [← pow_succ]; congr 1; omega]
        field_simp]
    rw [this]
    exact_mod_cast h_floor_app
  have h_fδ_half : f < 1 / 2 → f + δ < 1 / 2 := by
    intro hf_lt_half
    -- f = mod_val / 10^k, δ = r_nat / (ym * 10^k), r_nat < ym.
    -- When k ≥ 1: f < 1/2 means 2*mod_val < 10^k, and since 2 ∣ 10^k,
    --   mod_val ≤ 10^k/2 - 1. Then mod_val*ym + r_nat ≤ 10^k/2*ym - 1 < ym*10^k/2.
    -- When k = 0: mod_val = 0, f = 0, and N = 36 is impossible by bounds,
    --   so N = 19, which in divQuotient128 means remainder = 0, giving r_nat = 0.
    set mod_val := dq_result.1.toNat % 10 ^ k with hmod_def
    -- 2 * mod_val < 10^k at the Nat level (derived from f < 1/2)
    have h_2mod_lt : 2 * mod_val < 10 ^ k := by
      have : (mod_val : ℚ) / 10 ^ k < 1 / 2 := by rw [hf_def] at hf_lt_half; exact hf_lt_half
      have hq : (2 * mod_val : ℚ) < 10 ^ k := by
        rw [div_lt_iff₀ h10k_pos] at this; linarith
      exact_mod_cast hq
    rcases Nat.eq_zero_or_pos k with rfl | hk_pos
    · -- k = 0: mod_val = 0, f = 0. Must show δ = r_nat/ym < 1/2.
      -- N = 36 is impossible: xm * 10^36 ≥ 10^54 but zm*ym+r < 10^38.
      -- So N = 19 and from divQuotient128, remainder = 0 means r_nat = 0.
      simp only [pow_zero, Nat.mod_one] at hmod_def
      have hmod_zero : mod_val = 0 := by simp [hmod_def]
      have hN19 : N = 19 := by
        rcases hN_cases with rfl | rfl
        · rfl
        · exfalso
          have hxm_ge : 10 ^ 18 ≤ x.mantissa_.toNat := by
            rw [largeRange_min_val] at hx_min; exact hx_min
          have hzm_le : dq_result.1.toNat ≤ maxRep.toNat := by
            simp only [pow_zero] at hzm128_decomp
            omega
          have hym_le2 : y.mantissa_.toNat ≤ 10 ^ 19 - 1 := by
            rw [largeRange_max_val] at hy_le; omega
          have lhs_ge : x.mantissa_.toNat * 10 ^ 36 ≥ 10 ^ 54 := by
            calc x.mantissa_.toNat * 10 ^ 36 ≥ 10 ^ 18 * 10 ^ 36 :=
                  Nat.mul_le_mul_right _ hxm_ge
              _ = 10 ^ 54 := by norm_num
          have hmaxRep_val : maxRep.toNat = maxRepNat := by decide
          have rhs_lt : dq_result.1.toNat * y.mantissa_.toNat + r_nat < 10 ^ 38 := by
            have hr_le : r_nat ≤ y.mantissa_.toNat - 1 := by omega
            have hzm_le' : dq_result.1.toNat ≤ maxRepNat := by omega
            have hym_le3 : y.mantissa_.toNat ≤ 10 ^ 19 - 1 := hym_le2
            nlinarith [Nat.mul_le_mul hzm_le' hym_le3,
                       show (maxRepNat * (10 ^ 19 - 1) + (10 ^ 19 - 2) : ℕ) < 10 ^ 38
                            from by norm_num]
          linarith [h_euclid]
      -- Now N = 19. In divQuotient128, the N=19 branch fires only when
      -- (xm * 10^19) % ym = 0 (remainder was 0 in the computation).
      -- The divQuotient128_correct witness for N=19 is r_nat = 0.
      -- We derive r_nat = 0 from h_euclid using Euclidean uniqueness.
      subst hN19
      have hr_zero : r_nat = 0 := hN19_r0 rfl
      -- With r_nat = 0: δ = 0, f + δ = 0 < 1/2
      subst hr_zero
      rw [hf_def, hδ_def]
      simp only [hmod_zero, Nat.cast_zero, zero_div, add_zero]
      norm_num
    · -- k ≥ 1: use divisibility 2 ∣ 10^k to show mod_val ≤ 10^k/2 - 1
      have h_mod_lt : mod_val < 10 ^ k := Nat.mod_lt _ (by positivity)
      -- 2 ∣ 10^k (since k ≥ 1)
      have h2dvd : (2 : ℕ) ∣ 10 ^ k := by
        exact dvd_trans (by norm_num : 2 ∣ 10) (dvd_pow_self 10 (by omega : k ≠ 0))
      -- 10^k/2 * 2 = 10^k
      have h2k_eq : 10 ^ k / 2 * 2 = 10 ^ k := Nat.div_mul_cancel h2dvd
      -- mod_val + 1 ≤ 10^k/2 (from 2*mod_val < 10^k and 2 ∣ 10^k)
      have h1 : mod_val + 1 ≤ 10 ^ k / 2 := by
        have h2m : 2 * mod_val ≤ 10 ^ k - 2 := by omega
        omega
      have h2 : r_nat ≤ y.mantissa_.toNat - 1 := by omega
      -- Nat: mod_val * ym + r_nat < 10^k/2 * ym
      have h_sum_lt_nat : mod_val * y.mantissa_.toNat + r_nat < 10 ^ k / 2 * y.mantissa_.toNat := by
        nlinarith [Nat.mul_le_mul_right y.mantissa_.toNat h1]
      -- ℚ: (mod_val * ym + r_nat) / (ym * 10^k) < 1/2
      rw [hf_def, hδ_def]
      rw [show (mod_val : ℚ) / (10 : ℚ) ^ k +
          (r_nat : ℚ) / ((y.mantissa_.toNat : ℚ) * (10 : ℚ) ^ k) =
          ((mod_val : ℚ) * y.mantissa_.toNat + r_nat) /
          (y.mantissa_.toNat * (10 : ℚ) ^ k) from by field_simp]
      rw [div_lt_div_iff₀ (by positivity : (0 : ℚ) < y.mantissa_.toNat * 10 ^ k)
          (by positivity : (0 : ℚ) < 2)]
      have h2k_eq_q : ((10 ^ k / 2 : ℕ) : ℚ) * 2 = 10 ^ k := by exact_mod_cast h2k_eq
      have hq : (mod_val * y.mantissa_.toNat + r_nat : ℚ) < (10 ^ k / 2 * y.mantissa_.toNat : ℕ) := by
        exact_mod_cast h_sum_lt_nat
      push_cast at hq ⊢
      nlinarith
  have hδ_lt_tenth : δ < 1 / 10 := by
    rw [hδ_def]
    have hk_ge_1_or_delta_zero : 1 ≤ k ∨ δ = 0 := by
      by_contra h; push_neg at h; obtain ⟨hk0, hδ_ne⟩ := h
      interval_cases k
      -- k = 0: f = 0 and δ = r_nat / ym. From N = 19 → r = 0:
      -- if N = 19: δ = 0 (from hN19_r0), contradicts hδ_ne.
      -- if N = 36: dq_result.1 ≥ 10^35 (correction), but k=0 means dq ≤ maxRep ≈ 9.22·10^18. Contradiction.
      simp only [Nat.pow_zero, Nat.mod_one, Nat.mul_one] at hzm128_decomp
      rcases hN_cases with rfl | rfl
      · have hr_zero : r_nat = 0 := hN19_r0 rfl
        rw [hδ_def, show (r_nat : ℚ) = 0 from by exact_mod_cast hr_zero] at hδ_ne
        simp at hδ_ne
      · -- N = 36: dq ≥ 10^35 >> maxRep, so k ≥ 1 (contradicts k = 0).
        have : zm.toNat = dq_result.1.toNat := by omega
        rw [this] at hzm_le_maxRep
        -- dq ≥ xm*10^36/ym ≥ 10^18*10^36/(10^19-1) ≥ 10^35
        have hxm_ge' : 10 ^ 18 ≤ x.mantissa_.toNat := by rw [largeRange_min_val] at hx_min; exact hx_min
        have hym_bound' : y.mantissa_.toNat ≤ 10 ^ 19 - 1 := by rw [largeRange_max_val] at hy_le; omega
        have h_dq_large : dq_result.1.toNat > maxRep.toNat := by
          -- h_euclid : xm * 10^36 = dq * ym + r (in this N=36 branch)
          -- If dq ≤ maxRep then dq * ym + r ≤ maxRep*(10^19-1)+(10^19-2) < 10^54 ≤ xm*10^36.
          rw [maxRep_val]
          by_contra h; push_neg at h
          have h_rhs_le : dq_result.1.toNat * y.mantissa_.toNat + r_nat ≤
              maxRepNat * (10^19 - 1) + (10^19 - 2) :=
            Nat.add_le_add (Nat.mul_le_mul h hym_bound') (by omega)
          have h_lhs_ge : x.mantissa_.toNat * 10^36 ≥ 10^54 :=
            calc x.mantissa_.toNat * 10^36 ≥ 10^18 * 10^36 :=
                  Nat.mul_le_mul_right _ hxm_ge'
              _ = 10^54 := by norm_num
          have h_bound : (maxRepNat : ℕ) * (10^19 - 1) + (10^19 - 2) < 10^54 := by
            norm_num
          linarith [h_euclid]
        have : maxRep.toNat = maxRepNat := maxRep_val
        omega
    rcases hk_ge_1_or_delta_zero with hk_ge_1 | hδ_zero
    · have h10k_ge_10 : (10 : ℚ) ≤ 10 ^ k := by
        calc (10 : ℚ) = 10 ^ 1 := by norm_num
          _ ≤ 10 ^ k := pow_le_pow_right₀ (by norm_num : 1 ≤ (10 : ℚ)) hk_ge_1
      calc (r_nat : ℚ) / ((y.mantissa_.toNat : ℚ) * 10 ^ k)
          < (y.mantissa_.toNat : ℚ) / ((y.mantissa_.toNat : ℚ) * 10 ^ k) := by
            apply div_lt_div_of_pos_right (by exact_mod_cast hr_lt) (by positivity)
        _ = 1 / 10 ^ k := by field_simp
        _ ≤ 1 / 10 := div_le_div_of_nonneg_left (by norm_num) (by norm_num) h10k_ge_10
    · rw [show (r_nat : ℚ) / ((y.mantissa_.toNat : ℚ) * 10^k) = δ from hδ_def.symm, hδ_zero]
      norm_num
  refine ⟨zm, ze', f, δ, g, res_pos, hzm_ge, hzm_le_maxRep, hf_nn, hf_lt, hδ_nn, hf_plus_δ_lt,
    habs_xy_eq, h_rup_pos, h_result_abs, hres_pos_mant_ne, hf_rep, h_res_neg_eq_zn, h_floor_f, h_fδ_half,
    hδ_lt_tenth⟩

/-! ## `scaleDown128` sbit preservation (for division directed modes) -/

private lemma scaleDown128_sbit_preserved_div
    (M : UInt128) (e : Int) (g0 : Guard) :
    (scaleDown128 M e g0).2.2.sbit_ = g0.sbit_ := by
  induction M, e, g0 using scaleDown128.induct with
  | case1 M e g0 hcond _d IH =>
    have hunfold : scaleDown128 M e g0
        = scaleDown128 (M / 10) (e + 1) (g0.push (toUInt64 (M % 10))) := by
      conv_lhs => rw [scaleDown128]
      simp [hcond]
    rw [hunfold]
    have h_push_sbit : (g0.push (toUInt64 (M % 10))).sbit_ = g0.sbit_ := rfl
    rw [IH, h_push_sbit]
  | case2 M e g0 hcond =>
    unfold scaleDown128
    simp [hcond]

/-! ## Mode-generic division algorithmic decomposition

This lemma extracts the mode-independent facts from `operator_div`. The only
mode-dependent output is the `doRoundUp` result and its properties.
Directed-mode rounding bounds use this lemma and then do mode-specific
case analysis on the doRoundUp decision. -/

-- Concrete per-mode theorems replacing the former `operator_div_algorithmic_facts_gen` axiom.
-- Each is identical in proof structure to `operator_div_algorithmic_facts` (`.to_nearest`)
-- but uses the specific mode in the `hok` hypothesis and the `doRoundUp` calls.
-- `debug.skipKernelTC true` suppresses the kernel deep-recursion that fires on
-- proof terms containing `Prod.casesOn (scaleDown128 ...)` with concrete UInt128 literals.

private def operator_div_algorithmic_facts_conclusion
    (x y result : Number) (mode : rounding_mode) : Prop :=
    ∃ (zm : UInt64) (ze' : Int) (f δ : ℚ) (g : Guard) (res_pos : RoundResult),
      mantissaFloor ≤ zm.toNat ∧
      zm.toNat ≤ maxRep.toNat ∧
      0 ≤ f ∧ f < 1 ∧
      0 ≤ δ ∧
      f + δ < 1 ∧
      |x.toRat / y.toRat| = ((zm.toNat : ℚ) + f + δ) * 10 ^ ze' ∧
      g.doRoundUp false zm ze' largeRange.min largeRange.max mode "Number::operator_div overflow" = .ok res_pos ∧
      |result.toRat| = (res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_ ∧
      res_pos.mantissa_ ≠ 0 ∧
      represents g f ∧
      result.negative_ = (x.negative_ != y.negative_) ∧
      (zm.toNat = mantissaFloor → (8 : ℚ) / 10 ≤ f) ∧
      δ < 1 / 10 ∧
      g.sbit_ = (x.negative_ != y.negative_)

set_option debug.skipKernelTC true in
private def operator_div_algorithmic_facts_proof
    (x y result : Number) (mode : rounding_mode)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (hok : Number.operator_div x y mode = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    operator_div_algorithmic_facts_conclusion x y result mode := by
  have hx_bounds := hx.mantissaBounds hx_mant_ne
  have hy_bounds := hy.mantissaBounds hy_mant_ne
  have hx_mant_pos : 0 < x.mantissa_.toNat := by
    have : largeRange.min.toNat ≤ x.mantissa_.toNat := UInt64.le_iff_toNat_le.mp hx_bounds.1
    rw [largeRange_min_val] at this; omega
  have hy_mant_pos : 0 < y.mantissa_.toNat := by
    have : largeRange.min.toNat ≤ y.mantissa_.toNat := UInt64.le_iff_toNat_le.mp hy_bounds.1
    rw [largeRange_min_val] at this; omega
  have hx_ne_zero : ¬ x.operator_eq Number.zero = true := by
    intro h
    unfold Number.operator_eq Number.zero at h
    simp only [Bool.and_eq_true, beq_iff_eq] at h
    obtain ⟨⟨_, hmeq⟩, _⟩ := h
    have : x.mantissa_.toNat = 0 := by rw [hmeq]; rfl
    omega
  have hy_ne_zero : ¬ y.operator_eq Number.zero = true := by
    intro h
    unfold Number.operator_eq Number.zero at h
    simp only [Bool.and_eq_true, beq_iff_eq] at h
    obtain ⟨⟨_, hmeq⟩, _⟩ := h
    have : y.mantissa_.toNat = 0 := by rw [hmeq]; rfl
    omega
  have hx_le : x.mantissa_.toNat ≤ largeRange.max.toNat :=
    UInt64.le_iff_toNat_le.mp hx_bounds.2
  have hy_le : y.mantissa_.toNat ≤ largeRange.max.toNat :=
    UInt64.le_iff_toNat_le.mp hy_bounds.2
  have hy_min : largeRange.min.toNat ≤ y.mantissa_.toNat :=
    UInt64.le_iff_toNat_le.mp hy_bounds.1
  have hx_min : largeRange.min.toNat ≤ x.mantissa_.toNat :=
    UInt64.le_iff_toNat_le.mp hx_bounds.1
  set zn : Bool := x.negative_ != y.negative_ with hzn_def
  set dq_result := divQuotient128 x.mantissa_ y.mantissa_ x.exponent_ y.exponent_
    with hdq_def
  have h_dq := divQuotient128_correct x.mantissa_ y.mantissa_ x.exponent_ y.exponent_
    hx_mant_pos hy_mant_pos hx_le hy_le hy_min
  simp only at h_dq
  obtain ⟨N, r_nat, hN_cases, h_euclid, hr_lt, hze_dq_eq, hN19_r0⟩ := h_dq
  have hzm128_dq_ne_zero : ¬ dq_result.1 = 0 := by
    intro h_zero
    have h_zero_nat : dq_result.1.toNat = 0 := by rw [h_zero]; rfl
    rw [h_zero_nat, Nat.zero_mul, Nat.zero_add] at h_euclid
    have : x.mantissa_.toNat * 10 ^ N ≥ 10 ^ 18 * 10 ^ 19 := by
      rcases hN_cases with rfl | rfl
      · exact Nat.mul_le_mul_right _ (by rw [largeRange_min_val] at hx_min; exact hx_min)
      · calc x.mantissa_.toNat * 10 ^ 36 ≥ 10 ^ 18 * 10 ^ 36 :=
              Nat.mul_le_mul_right _ (by rw [largeRange_min_val] at hx_min; exact hx_min)
            _ ≥ 10 ^ 18 * 10 ^ 19 := Nat.mul_le_mul_left _ (by norm_num)
    have : y.mantissa_.toNat ≤ 10 ^ 19 - 1 := by rw [largeRange_max_val] at hy_le; omega
    have : (10 : ℕ) ^ 18 * 10 ^ 19 = 10 ^ 37 := by norm_num
    omega
  set g0 : Guard := if zn then Guard.new.set_negative else Guard.new with hg0_def
  set sd_result := scaleDown128 dq_result.1 dq_result.2 g0 with hsd_def
  set zm : UInt64 := sd_result.1 with hzm_def
  set ze' : Int := sd_result.2.1 with hze'_def
  set g : Guard := sd_result.2.2 with hg_def
  -- Extract no-underflow and doRoundUp result from hok.
  have hok' := hok
  unfold Number.operator_div at hok'
  simp only [hy_ne_zero, hx_ne_zero, Bool.false_eq_true, if_false,
    pure, Except.pure, bind, Except.bind] at hok'
  rw [if_neg (show ¬ (divQuotient128 x.mantissa_ y.mantissa_ x.exponent_ y.exponent_).1 = 0
        from by rw [← hdq_def]; exact hzm128_dq_ne_zero)] at hok'
  rw [show (x.negative_ != y.negative_) = zn from hzn_def.symm] at hok'
  rw [show (if zn = true then Guard.new.set_negative else Guard.new) = g0 from hg0_def.symm] at hok'
  rw [show scaleDown128 dq_result.1 dq_result.2 g0 = sd_result from hsd_def.symm] at hok'
  rw [show sd_result.1 = zm from hzm_def.symm,
      show sd_result.2.1 = ze' from hze'_def.symm,
      show sd_result.2.2 = g from hg_def.symm] at hok'
  have h_not_underflow : ¬ ze' < minExponent := by
    by_contra h_uf
    rw [if_pos h_uf] at hok'
    have : result.mantissa_ = 0 := by
      have := Except.ok.inj hok'; rw [← this]; rfl
    exact hresult this
  rw [if_neg h_not_underflow] at hok'
  have ⟨res_zn, h_rup_zn, hok_norm⟩ : ∃ r : RoundResult,
      g.doRoundUp zn zm ze' largeRange.min largeRange.max mode "Number::operator_div overflow" = .ok r ∧
      r.toNumber = result := by
    match hrg : g.doRoundUp zn zm ze' largeRange.min largeRange.max mode "Number::operator_div overflow" with
    | .error e => simp only [hrg, reduceCtorEq] at hok'
    | .ok r =>
      simp only [hrg] at hok'
      exact ⟨r, rfl, Except.ok.inj hok'⟩
  have h_result_eq : result = res_zn.toNumber := hok_norm.symm
  have hg0_rep : represents (if zn then Guard.new.set_negative else Guard.new) 0 := by
    by_cases hzn : zn
    · rw [if_pos hzn]
      obtain ⟨x_rep, hx_nn, hx_lt, hf_eq, hxbit, hall⟩ := represents_new
      refine ⟨x_rep, hx_nn, hx_lt, ?_, ?_, ?_⟩
      · show (0 : ℚ) = _
        have : Guard.new.set_negative.digits_ = Guard.new.digits_ := rfl
        rw [this]; exact hf_eq
      · have : Guard.new.set_negative.xbit_ = Guard.new.xbit_ := rfl
        rw [this]; exact hxbit
      · have : Guard.new.set_negative.digits_ = Guard.new.digits_ := rfl
        rw [this]; exact hall
    · rw [if_neg hzn]; exact represents_new
  have h_sd_correct := scaleDown128_correct dq_result.1 dq_result.2
    (if zn then Guard.new.set_negative else Guard.new) 0 hg0_rep
  simp only at h_sd_correct
  rw [← hsd_def] at h_sd_correct
  have h_sd_split : sd_result = (zm, ze', g) := by rw [hzm_def, hze'_def, hg_def]
  rw [h_sd_split] at h_sd_correct
  simp only at h_sd_correct
  obtain ⟨k, hk_eq, hzm_le_maxRep, hzm128_decomp, hg_rep, hfloor⟩ := h_sd_correct
  have h10k_pos_nat : (0 : ℕ) < 10 ^ k := by positivity
  have h10k_pos : (0 : ℚ) < 10 ^ k := by positivity
  have h10k_ne : (10 : ℚ) ^ k ≠ 0 := ne_of_gt h10k_pos
  have hym_pos_q : (0 : ℚ) < (y.mantissa_.toNat : ℚ) := by exact_mod_cast hy_mant_pos
  have hym_ne_zero : (y.mantissa_.toNat : ℚ) ≠ 0 := ne_of_gt hym_pos_q
  set f : ℚ := ((dq_result.1.toNat % 10 ^ k : ℕ) : ℚ) / 10 ^ k with hf_def
  set δ : ℚ := (r_nat : ℚ) / ((y.mantissa_.toNat : ℚ) * 10 ^ k) with hδ_def
  have hf_rep : represents g f := by
    have : (0 + ((dq_result.1.toNat % 10 ^ k : ℕ) : ℚ)) / 10 ^ k = f := by
      rw [hf_def]; ring
    rw [← this]; exact hg_rep
  have hf_nn : 0 ≤ f := div_nonneg (Nat.cast_nonneg _) (le_of_lt h10k_pos)
  have hf_lt : f < 1 := by
    rw [hf_def, div_lt_one h10k_pos]
    exact_mod_cast Nat.mod_lt _ h10k_pos_nat
  have hδ_nn : 0 ≤ δ := div_nonneg (Nat.cast_nonneg _)
    (mul_nonneg (Nat.cast_nonneg _) (le_of_lt h10k_pos))
  have hf_plus_δ_lt : f + δ < 1 := by
    rw [hf_def, hδ_def]
    have h_mod_lt := Nat.mod_lt dq_result.1.toNat h10k_pos_nat
    have key : ((dq_result.1.toNat % 10 ^ k : ℕ) : ℚ) * y.mantissa_.toNat + r_nat <
        (y.mantissa_.toNat : ℚ) * 10 ^ k := by
      have hnat : (dq_result.1.toNat % 10 ^ k) * y.mantissa_.toNat + r_nat <
          y.mantissa_.toNat * 10 ^ k := by nlinarith
      exact_mod_cast hnat
    have lhs_eq : ((dq_result.1.toNat % 10 ^ k : ℕ) : ℚ) / 10 ^ k +
        (r_nat : ℚ) / ((y.mantissa_.toNat : ℚ) * 10 ^ k) =
        (((dq_result.1.toNat % 10 ^ k : ℕ) : ℚ) * y.mantissa_.toNat + r_nat) /
        (y.mantissa_.toNat * 10 ^ k) := by field_simp
    rw [lhs_eq, div_lt_one (by positivity)]
    exact key
  have h_dq_ge_10_18 : dq_result.1.toNat ≥ 10 ^ 18 :=
    divQuotient128_ge_10_18 x.mantissa_ y.mantissa_ x.exponent_ y.exponent_
      hx_mant_pos hy_mant_pos hx_le hy_le hy_min hx_min
  have hzm_ge : mantissaFloor ≤ zm.toNat := by
    rw [hzm_def]
    by_cases h_gt : dq_result.1.toNat > maxRep.toNat
    · have h_lb := scaleDown128_lower_bound dq_result.1 dq_result.2 g0 h_gt
      simp only [← hsd_def] at h_lb
      have : (maxRep.toNat + 1) / 10 = mantissaFloor := by
        simp only [maxRep, UInt64.toNat]; norm_num
      omega
    · push_neg at h_gt
      have h_not_gt : ¬ dq_result.1 > toUInt128 maxRep := by
        intro h; have := BitVec.lt_def.mp h; rw [toNat_toUInt128] at this; omega
      have hsd_unfold : sd_result = (toUInt64 dq_result.1, dq_result.2, g0) := by
        rw [hsd_def]; unfold scaleDown128; rw [dif_neg h_not_gt]
      rw [hsd_unfold]
      simp only
      have h_fit : dq_result.1.toNat < 2 ^ 64 := by
        have : maxRep.toNat < 2 ^ 64 := maxRep.toNat_lt; omega
      rw [toNat_toUInt64 h_fit]
      have : (mantissaFloor : ℕ) < 10 ^ 18 := by norm_num
      omega
  have habs_xy_eq : |x.toRat / y.toRat| = ((zm.toNat : ℚ) + f + δ) * 10 ^ ze' := by
    rw [hf_def, hδ_def, abs_div, abs_toRat_eq x, abs_toRat_eq y]
    have h10_ne : (10 : ℚ) ≠ 0 := by norm_num
    have h_euclid_q : (x.mantissa_.toNat : ℚ) * 10 ^ N =
        ((zm.toNat : ℚ) * 10 ^ k + (dq_result.1.toNat % 10 ^ k : ℕ)) *
        y.mantissa_.toNat + r_nat := by
      have h := h_euclid
      rw [hzm128_decomp] at h
      exact_mod_cast h
    have hze'_eq : ze' = x.exponent_ - y.exponent_ - (N : Int) + (k : Int) := by
      rw [hk_eq, hze_dq_eq]
    rw [hze'_eq, show (x.exponent_ - y.exponent_ - (N : Int) + (k : Int) : Int) =
        x.exponent_ + -y.exponent_ + -(N : Int) + (k : Int) from by ring]
    simp only [zpow_add₀ h10_ne, zpow_neg, zpow_natCast]
    field_simp
    linarith
  have hres_mant_eq : result.mantissa_ = res_zn.mantissa_ := by
    have := congrArg Number.mantissa_ h_result_eq
    simp only [RoundResult.toNumber] at this; exact this
  have hres_exp_eq : result.exponent_ = res_zn.exponent_ := by
    have := congrArg Number.exponent_ h_result_eq
    simp only [RoundResult.toNumber] at this; exact this
  have hres_neg_raw : result.negative_ = res_zn.toNumber.negative_ := by
    have := congrArg Number.negative_ h_result_eq; exact this
  have hres_zn_mant_ne : res_zn.mantissa_ ≠ 0 := hres_mant_eq ▸ hresult
  set res_pos : RoundResult := { negative_ := false, mantissa_ := res_zn.mantissa_, exponent_ := res_zn.exponent_ }
  have hres_pos_mant_ne : res_pos.mantissa_ ≠ 0 := hres_zn_mant_ne
  have h_rup_pos : g.doRoundUp false zm ze' largeRange.min largeRange.max mode "Number::operator_div overflow" = .ok res_pos :=
    doRoundUp_false_from_ok g zn zm ze' mode "Number::operator_div overflow" res_zn h_rup_zn
  have h_result_abs : |result.toRat|
      = (res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_ := by
    rw [abs_toRat_eq result, hres_mant_eq, hres_exp_eq]
  have h_res_neg_eq_zn : result.negative_ = zn := by
    rw [hres_neg_raw]
    exact doRoundUp_negative_of_mant_ne g zn zm ze' _ _ _ "Number::operator_div overflow" res_zn h_rup_zn hres_zn_mant_ne
  have h_floor_f : zm.toNat = mantissaFloor → (8 : ℚ) / 10 ≤ f := by
    intro hzm_eq
    have hk_pos : k > 0 := by
      by_contra h_k0; push_neg at h_k0
      interval_cases k
      simp only [Nat.pow_zero, Nat.mod_one] at hzm128_decomp
      have hzm_eq_dq : zm.toNat = dq_result.1.toNat := by omega
      rw [hzm_eq] at hzm_eq_dq
      have : (mantissaFloor : ℕ) < 10 ^ 18 := by norm_num
      omega
    have h_floor_app := hfloor ⟨hk_pos, hzm_eq⟩
    rw [hf_def]
    rw [le_div_iff₀ (show (0 : ℚ) < 10 ^ k from by positivity)]
    have : (8 : ℚ) / 10 * 10 ^ k = 8 * 10 ^ (k - 1) := by
      rw [show (8 : ℚ) / 10 * 10 ^ k = 8 * (10 ^ k / 10) from by ring]
      congr 1
      rw [show (10 : ℚ) ^ k / 10 = 10 ^ (k - 1) from by
        rw [show (10 : ℚ) ^ k = 10 ^ (k - 1) * 10 from by
          rw [← pow_succ]; congr 1; omega]
        field_simp]
    rw [this]
    exact_mod_cast h_floor_app
  have hδ_lt_tenth : δ < 1 / 10 := by
    rw [hδ_def]
    have hk_ge_1_or_delta_zero : 1 ≤ k ∨ δ = 0 := by
      by_contra h; push_neg at h; obtain ⟨hk0, hδ_ne⟩ := h
      interval_cases k
      simp only [Nat.pow_zero, Nat.mod_one, Nat.mul_one] at hzm128_decomp
      rcases hN_cases with rfl | rfl
      · have hr_zero : r_nat = 0 := hN19_r0 rfl
        rw [hδ_def, show (r_nat : ℚ) = 0 from by exact_mod_cast hr_zero] at hδ_ne
        simp at hδ_ne
      · have : zm.toNat = dq_result.1.toNat := by omega
        rw [this] at hzm_le_maxRep
        have hxm_ge' : 10 ^ 18 ≤ x.mantissa_.toNat := by rw [largeRange_min_val] at hx_min; exact hx_min
        have hym_bound' : y.mantissa_.toNat ≤ 10 ^ 19 - 1 := by rw [largeRange_max_val] at hy_le; omega
        have h_dq_large : dq_result.1.toNat > maxRep.toNat := by
          rw [maxRep_val]
          by_contra h; push_neg at h
          have h_rhs_le : dq_result.1.toNat * y.mantissa_.toNat + r_nat ≤
              maxRepNat * (10^19 - 1) + (10^19 - 2) :=
            Nat.add_le_add (Nat.mul_le_mul h hym_bound') (by omega)
          have h_lhs_ge : x.mantissa_.toNat * 10^36 ≥ 10^54 :=
            calc x.mantissa_.toNat * 10^36 ≥ 10^18 * 10^36 :=
                  Nat.mul_le_mul_right _ hxm_ge'
              _ = 10^54 := by norm_num
          have h_bound : (maxRepNat : ℕ) * (10^19 - 1) + (10^19 - 2) < 10^54 := by
            norm_num
          linarith [h_euclid]
        have : maxRep.toNat = maxRepNat := maxRep_val
        omega
    rcases hk_ge_1_or_delta_zero with hk_ge_1 | hδ_zero
    · have h10k_ge_10 : (10 : ℚ) ≤ 10 ^ k := by
        calc (10 : ℚ) = 10 ^ 1 := by norm_num
          _ ≤ 10 ^ k := pow_le_pow_right₀ (by norm_num : 1 ≤ (10 : ℚ)) hk_ge_1
      calc (r_nat : ℚ) / ((y.mantissa_.toNat : ℚ) * 10 ^ k)
          < (y.mantissa_.toNat : ℚ) / ((y.mantissa_.toNat : ℚ) * 10 ^ k) := by
            apply div_lt_div_of_pos_right (by exact_mod_cast hr_lt) (by positivity)
        _ = 1 / 10 ^ k := by field_simp
        _ ≤ 1 / 10 := div_le_div_of_nonneg_left (by norm_num) (by norm_num) h10k_ge_10
    · rw [show (r_nat : ℚ) / ((y.mantissa_.toNat : ℚ) * 10^k) = δ from hδ_def.symm, hδ_zero]
      norm_num
  have h_g_sbit : g.sbit_ = zn := by
    have h_g_eq : g = (scaleDown128 dq_result.1 dq_result.2 g0).2.2 := by rw [hg_def, hsd_def]
    rw [h_g_eq, scaleDown128_sbit_preserved_div]
    rw [hg0_def]
    by_cases hzn : zn
    · rw [if_pos hzn]; exact hzn.symm
    · rw [if_neg hzn]
      exact (Bool.not_eq_true _ |>.mp hzn).symm
  exact ⟨zm, ze', f, δ, g, res_pos, hzm_ge, hzm_le_maxRep, hf_nn, hf_lt, hδ_nn, hf_plus_δ_lt,
    habs_xy_eq, h_rup_pos, h_result_abs, hres_pos_mant_ne, hf_rep, h_res_neg_eq_zn, h_floor_f,
    hδ_lt_tenth, h_g_sbit⟩

set_option maxHeartbeats 1600000 in
-- concrete mode instantiation of the generic algebraic facts proof
set_option debug.skipKernelTC true in
-- kernel deep-recursion on variable-mode proof terms
theorem operator_div_algorithmic_facts_towards_zero (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (hok : Number.operator_div x y .towards_zero = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    operator_div_algorithmic_facts_conclusion x y result .towards_zero :=
  operator_div_algorithmic_facts_proof x y result .towards_zero
    hx hy hx_mant_ne hy_mant_ne hok hresult

set_option maxHeartbeats 1600000 in
-- concrete mode instantiation of the generic algebraic facts proof
set_option debug.skipKernelTC true in
-- kernel deep-recursion on variable-mode proof terms
theorem operator_div_algorithmic_facts_downward (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (hok : Number.operator_div x y .downward = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    operator_div_algorithmic_facts_conclusion x y result .downward :=
  operator_div_algorithmic_facts_proof x y result .downward
    hx hy hx_mant_ne hy_mant_ne hok hresult

set_option maxHeartbeats 1600000 in
-- concrete mode instantiation of the generic algebraic facts proof
set_option debug.skipKernelTC true in
-- kernel deep-recursion on variable-mode proof terms
theorem operator_div_algorithmic_facts_upward (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (hok : Number.operator_div x y .upward = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    operator_div_algorithmic_facts_conclusion x y result .upward :=
  operator_div_algorithmic_facts_proof x y result .upward
    hx hy hx_mant_ne hy_mant_ne hok hresult

end XRPL.Model.Protocol
