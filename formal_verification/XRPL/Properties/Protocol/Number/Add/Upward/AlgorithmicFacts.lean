import XRPL.Properties.Protocol.Number.Common.Notation
import XRPL.Properties.Protocol.Number.Add.Common
import XRPL.Properties.Protocol.Number.Common.Helpers
import XRPL.Properties.Protocol.Number.Rounding.DoRoundUp

set_option linter.style.longLine false
set_option linter.style.emptyLine false

namespace XRPL.Model.Protocol

/-! # Algorithmic decomposition for `operator_add` — same-sign branch (.upward) -/

/-- For same-sign Numbers, `|x.toRat + y.toRat| = |x.toRat| + |y.toRat|`. -/
private lemma abs_add_eq_of_same_sign_upward {x y : Number} (h : x.negative_ = y.negative_) :
    |x.toRat + y.toRat| = |x.toRat| + |y.toRat| := by
  cases hxn : x.negative_
  · have hyn : y.negative_ = false := h ▸ hxn
    have hx_nn : 0 ≤ x.toRat := Number.toRat_nonneg_of_nonnegative x hxn
    have hy_nn : 0 ≤ y.toRat := Number.toRat_nonneg_of_nonnegative y hyn
    rw [abs_of_nonneg (add_nonneg hx_nn hy_nn), abs_of_nonneg hx_nn, abs_of_nonneg hy_nn]
  · have hyn : y.negative_ = true := h ▸ hxn
    have hx_np : x.toRat ≤ 0 := Number.toRat_nonpos_of_negative x hxn
    have hy_np : y.toRat ≤ 0 := Number.toRat_nonpos_of_negative y hyn
    rw [abs_of_nonpos (add_nonpos hx_np hy_np), abs_of_nonpos hx_np, abs_of_nonpos hy_np]
    ring

/-- The initial guard represents zero. -/
private lemma represents_initial_guard_eq_upward (xn : Bool) :
    represents (if xn then Guard.new.set_negative else Guard.new) 0 := by
  by_cases hxn : xn
  · rw [if_pos hxn]
    obtain ⟨x_rep, hx_nn, hx_lt, hf_eq, hxbit, hall⟩ := represents_new
    refine ⟨x_rep, hx_nn, hx_lt, ?_, ?_, ?_⟩
    · show (0 : ℚ) = _
      have : Guard.new.set_negative.digits_ = Guard.new.digits_ := rfl
      rw [this]; exact hf_eq
    · have hxbit_eq : Guard.new.set_negative.xbit_ = Guard.new.xbit_ := rfl
      rw [hxbit_eq]; exact hxbit
    · have : Guard.new.set_negative.digits_ = Guard.new.digits_ := rfl
      rw [this]; exact hall
  · rw [if_neg hxn]; exact represents_new

/-- After running alignment from below, the mantissa value relation. -/
private lemma alignDown_abs_value_upward
    (n : Number) (g₀ : Guard) (target : Int) (h_le : n.exponent_ ≤ target)
    (f₀ : ℚ) :
    let r := Number.operator_add.alignDown n.mantissa_ n.exponent_ g₀ target
    let f' : ℚ := (f₀ + ((n.mantissa_.toNat % 10 ^ (target - n.exponent_).toNat : ℕ) : ℚ))
                    / 10 ^ (target - n.exponent_).toNat
    |n.toRat| + f₀ * 10 ^ n.exponent_
      = ((r.1.toNat : ℚ) + f') * 10 ^ target := by
  simp only
  have h_abs := abs_toRat_eq n
  rw [h_abs]
  have hmax : max n.exponent_ target = target := max_eq_right h_le
  have h_me : (Number.operator_add.alignDown n.mantissa_ n.exponent_ g₀ target).1.toNat
      = n.mantissa_.toNat / 10 ^ (max n.exponent_ target - n.exponent_).toNat :=
    alignDown_mantissa_eq n.mantissa_ n.exponent_ g₀ target
  rw [hmax] at h_me
  rw [h_me]
  set K : ℕ := (target - n.exponent_).toNat with hK_def
  have hK_eq : (target : ℤ) - n.exponent_ = (K : ℤ) := by
    rw [hK_def]; exact (Int.toNat_of_nonneg (by linarith)).symm
  have h10K_pos : (0 : ℚ) < 10 ^ K := by positivity
  have h10K_ne : (10 : ℚ) ^ K ≠ 0 := ne_of_gt h10K_pos
  have h_pow_split : (10 : ℚ) ^ target = 10 ^ n.exponent_ * 10 ^ K := by
    have : (target : ℤ) = n.exponent_ + K := by linarith
    rw [this]
    rw [zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0), zpow_natCast]
  rw [h_pow_split]
  have hsplit_q : (n.mantissa_.toNat : ℚ) = ((n.mantissa_.toNat / 10 ^ K : ℕ) : ℚ) * (10 ^ K : ℚ) + ((n.mantissa_.toNat % 10 ^ K : ℕ) : ℚ) := by
    have hsplit : n.mantissa_.toNat = (n.mantissa_.toNat / 10 ^ K) * 10 ^ K + n.mantissa_.toNat % 10 ^ K := by
      have := Nat.div_add_mod n.mantissa_.toNat (10 ^ K); linarith
    have : ((n.mantissa_.toNat : ℕ) : ℚ) = (((n.mantissa_.toNat / 10 ^ K) * 10 ^ K + n.mantissa_.toNat % 10 ^ K : ℕ) : ℚ) := by
      exact_mod_cast hsplit
    push_cast at this; linarith
  rw [hsplit_q]
  field_simp
  ring

/-- Helper: post-alignment processing for the same-sign branch (.upward).
After the alignment phase, the state is `(xm_a, e_common, ym_a, g_aln)` with `f_aln`
representing the guard. This lemma packages the conditional drop + doRoundUp + normalize. -/
private theorem operator_add_post_alignment_upward
    (xn : Bool) (xm_a ym_a : UInt64) (e_common : Int) (g_aln : Guard)
    (f_aln : ℚ) (hrep_aln : represents g_aln f_aln)
    (hxm_lt : xm_a.toNat < 10 ^ 19) (hym_lt : ym_a.toNat < 10 ^ 19)
    (hsum_ge : 10 ^ 18 ≤ xm_a.toNat + ym_a.toNat)
    (truth : ℚ)
    (htruth_eq : |truth| = ((xm_a.toNat + ym_a.toNat : ℕ) : ℚ) * 10 ^ e_common
                            + f_aln * 10 ^ e_common)
    (result : Number)
    (hresult : result.mantissa_ ≠ 0)
    (hok : (let zm128 : UInt128 := toUInt128 xm_a + toUInt128 ym_a
            let p : UInt64 × Int × Guard :=
              if zm128 > toUInt128 largeRange.max || zm128 > toUInt128 maxRep then
                (toUInt64 (g_aln.doDropDigit128 zm128 e_common).2.1,
                 (g_aln.doDropDigit128 zm128 e_common).2.2,
                 (g_aln.doDropDigit128 zm128 e_common).1)
              else (toUInt64 zm128, e_common, g_aln)
            match p.2.2.doRoundUp xn p.1 p.2.1 largeRange.min largeRange.max
                  .upward "Number::addition overflow" with
            | .error err => Except.error err
            | .ok res => res.toNumber.normalize largeRange.min largeRange.max .upward)
           = .ok result) :
    ∃ (zm : UInt64) (ze' : Int) (f : ℚ) (g : Guard) (res_pos : RoundResult),
      mantissaFloor ≤ zm.toNat ∧
      zm.toNat ≤ maxRep.toNat ∧
      0 ≤ f ∧ f < 1 ∧
      (zm.toNat = mantissaFloor → (8 : ℚ) / 10 ≤ f) ∧
      |truth| = ((zm.toNat : ℚ) + f) * 10 ^ ze' ∧
      g.doRoundUp false zm ze' largeRange.min largeRange.max .upward "Number::addition overflow" = .ok res_pos ∧
      |result.toRat| = (res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_ ∧
      res_pos.mantissa_ ≠ 0 ∧
      represents g f ∧
      result.negative_ = xn := by
  have hf_aln_nn : 0 ≤ f_aln := represents_nonneg hrep_aln
  have hf_aln_lt : f_aln < 1 := represents_lt_one hrep_aln
  have hxm_tu : (toUInt128 xm_a).toNat = xm_a.toNat := toNat_toUInt128 xm_a
  have hym_tu : (toUInt128 ym_a).toNat = ym_a.toNat := toNat_toUInt128 ym_a
  have hsum_lt : xm_a.toNat + ym_a.toNat < 2 * 10 ^ 19 := by omega
  have hsum_lt128 : xm_a.toNat + ym_a.toNat < 2 ^ 128 := by
    have : 2 * 10 ^ 19 < 2 ^ 128 := by norm_num
    omega
  set zm128 : UInt128 := toUInt128 xm_a + toUInt128 ym_a with hzm128_def
  have hzm128_toNat : zm128.toNat = xm_a.toNat + ym_a.toNat := by
    rw [hzm128_def]
    rw [BitVec.toNat_add]
    rw [hxm_tu, hym_tu]
    apply Nat.mod_eq_of_lt; exact hsum_lt128
  have hmaxRep_v : maxRep.toNat = maxRepNat := maxRep_val
  have hlrmax_v : largeRange.max.toNat = 9999999999999999999 := largeRange_max_val
  have hlrmin_v : largeRange.min.toNat = 1000000000000000000 := largeRange_min_val
  have htu_max_v : (toUInt128 largeRange.max).toNat = 9999999999999999999 := by
    rw [toNat_toUInt128]; exact hlrmax_v
  have htu_maxRep_v : (toUInt128 maxRep).toNat = maxRepNat := by
    rw [toNat_toUInt128]; exact hmaxRep_v
  have h_cond_iff_sum_gt_maxRep : (zm128 > toUInt128 largeRange.max
        || zm128 > toUInt128 maxRep) = decide (zm128.toNat > maxRep.toNat) := by
    by_cases hgt : zm128.toNat > maxRep.toNat
    · have h_zm_gt_maxRep : zm128 > toUInt128 maxRep := by
        change toUInt128 maxRep < zm128
        rw [BitVec.lt_def, htu_maxRep_v]
        rw [hmaxRep_v] at hgt; exact hgt
      rw [decide_eq_true hgt]
      have : (decide (toUInt128 maxRep < zm128) : Bool) = true := decide_eq_true h_zm_gt_maxRep
      change (decide (toUInt128 largeRange.max < zm128)
              || decide (toUInt128 maxRep < zm128)) = true
      rw [this]; simp
    · push_neg at hgt
      have h_zm_le : zm128.toNat ≤ maxRep.toNat := hgt
      have h_zm_not_gt_maxRep : ¬ zm128 > toUInt128 maxRep := by
        intro h
        have : (toUInt128 maxRep).toNat < zm128.toNat := BitVec.lt_def.mp h
        rw [htu_maxRep_v] at this; omega
      have h_zm_not_gt_lrmax : ¬ zm128 > toUInt128 largeRange.max := by
        intro h
        have : (toUInt128 largeRange.max).toNat < zm128.toNat := BitVec.lt_def.mp h
        rw [htu_max_v] at this
        rw [hmaxRep_v] at h_zm_le; omega
      rw [decide_eq_false (by omega : ¬ zm128.toNat > maxRep.toNat)]
      change (decide (toUInt128 largeRange.max < zm128)
              || decide (toUInt128 maxRep < zm128)) = false
      rw [decide_eq_false h_zm_not_gt_lrmax, decide_eq_false h_zm_not_gt_maxRep]
      rfl
  by_cases h_drop : zm128.toNat > maxRep.toNat
  · -- Drop case
    have h_cond_true : (zm128 > toUInt128 largeRange.max
        || zm128 > toUInt128 maxRep) = true := by
      rw [h_cond_iff_sum_gt_maxRep]; exact decide_eq_true h_drop
    set d : UInt64 := toUInt64 (zm128 % 10) with hd_def
    have h10_128 : (10 : UInt128).toNat = 10 := by decide
    have h_zm_mod_lt : zm128.toNat % 10 < 10 := Nat.mod_lt _ (by decide)
    have h_zm_div_eq : (zm128 / 10).toNat = zm128.toNat / 10 := by
      rw [BitVec.toNat_udiv, h10_128]
    have h_zm_mod_eq : (zm128 % 10).toNat = zm128.toNat % 10 := by
      rw [BitVec.toNat_umod, h10_128]
    have h_d_fit : (zm128 % 10).toNat < 2 ^ 64 := by
      rw [h_zm_mod_eq]; omega
    have hd_toNat : d.toNat = zm128.toNat % 10 := by
      rw [hd_def, toNat_toUInt64 h_d_fit, h_zm_mod_eq]
    have hd_lt : d.toNat < 10 := by rw [hd_toNat]; exact h_zm_mod_lt
    set zm_new : UInt64 := toUInt64 (zm128 / 10) with hzm_new_def
    have h_div_fit : (zm128 / 10).toNat < 2 ^ 64 := by
      rw [h_zm_div_eq]; rw [hzm128_toNat]
      have : (xm_a.toNat + ym_a.toNat) / 10 < 2 * 10 ^ 18 := by omega
      have : 2 * 10 ^ 18 < 2 ^ 64 := by norm_num
      omega
    have hzm_new_toNat : zm_new.toNat = zm128.toNat / 10 := by
      rw [hzm_new_def, toNat_toUInt64 h_div_fit, h_zm_div_eq]
    have hzm_new_ge_floor : mantissaFloor ≤ zm_new.toNat := by
      rw [hzm_new_toNat]
      have : zm128.toNat / 10 ≥ 9223372036854775808 / 10 := Nat.div_le_div_right (by omega)
      have h_calc : (9223372036854775808 : ℕ) / 10 = mantissaFloor := by norm_num
      omega
    have hzm_new_le_maxRep : zm_new.toNat ≤ maxRep.toNat := by
      rw [hzm_new_toNat, hmaxRep_v, hzm128_toNat]
      have : (xm_a.toNat + ym_a.toNat) / 10 ≤ (2 * 10 ^ 19 - 1) / 10 := Nat.div_le_div_right (by omega)
      have h_calc : (2 * 10 ^ 19 - 1) / 10 = 1999999999999999999 := by norm_num
      have : zm128.toNat / 10 ≤ 1999999999999999999 := by
        rw [hzm128_toNat]; omega
      omega
    set g_new : Guard := g_aln.push d with hg_new_def
    have hrep_new : represents g_new ((f_aln + d.toNat) / 10) :=
      represents_push hrep_aln hd_lt
    set f_new : ℚ := (f_aln + (d.toNat : ℚ)) / 10 with hf_new_def
    have hf_new_nn : 0 ≤ f_new := by
      rw [hf_new_def]
      apply div_nonneg
      · linarith [Nat.cast_nonneg (α := ℚ) d.toNat]
      · norm_num
    have hf_new_lt : f_new < 1 := by
      rw [hf_new_def, div_lt_one (by norm_num : (0 : ℚ) < 10)]
      have h_d_le : (d.toNat : ℚ) ≤ 9 := by exact_mod_cast (Nat.lt_succ_iff.mp hd_lt)
      linarith
    -- Floor constraint: when zm_new = floor exactly, dropped digit is ≥ 8.
    have h_floor_constraint : zm_new.toNat = mantissaFloor → (8 : ℚ) / 10 ≤ f_new := by
      intro h_floor
      have h_zm128_range : 9223372036854775800 ≤ zm128.toNat ∧ zm128.toNat ≤ 9223372036854775809 := by
        have hd : zm_new.toNat = zm128.toNat / 10 := hzm_new_toNat
        constructor
        · have : zm128.toNat / 10 = mantissaFloor := h_floor ▸ hd.symm
          omega
        · have hd' : zm_new.toNat = zm128.toNat / 10 := hzm_new_toNat
          have : zm128.toNat / 10 = mantissaFloor := h_floor ▸ hd'.symm
          omega
      have h_d_ge_8 : 8 ≤ d.toNat := by
        rw [hd_toNat]
        have hdrop : zm128.toNat > maxRep.toNat := h_drop
        rw [hmaxRep_v] at hdrop
        -- zm128.toNat ∈ {9223372036854775808, 9223372036854775809}
        have h1 : zm128.toNat = 9223372036854775808 ∨ zm128.toNat = 9223372036854775809 := by
          have hb := h_zm128_range.2
          interval_cases zm128.toNat
          · left; rfl
          · right; rfl
        rcases h1 with heq | heq
        · rw [heq]
        · rw [heq]; decide
      rw [hf_new_def]
      rw [le_div_iff₀ (by norm_num : (0 : ℚ) < 10)]
      have h_d_ge_q : (8 : ℚ) ≤ (d.toNat : ℚ) := by exact_mod_cast h_d_ge_8
      have : (8 : ℚ) / 10 * 10 = 8 := by norm_num
      rw [this]
      linarith
    have h_value_eq : |truth| = ((zm_new.toNat : ℚ) + f_new) * 10 ^ (e_common + 1) := by
      rw [htruth_eq]
      have hsum_decomp : (xm_a.toNat + ym_a.toNat : ℕ) = zm_new.toNat * 10 + d.toNat := by
        rw [hzm_new_toNat, hd_toNat, hzm128_toNat]
        omega
      have hsum_q : ((xm_a.toNat + ym_a.toNat : ℕ) : ℚ) = (zm_new.toNat : ℚ) * 10 + (d.toNat : ℚ) := by
        have h := hsum_decomp
        exact_mod_cast h
      rw [hsum_q, hf_new_def]
      have h_pow_eq : (10 : ℚ) ^ (e_common + 1) = 10 ^ e_common * 10 := by
        rw [zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0)]; norm_num
      rw [h_pow_eq]
      field_simp
      ring
    have hok' : (match g_new.doRoundUp xn zm_new (e_common + 1) largeRange.min largeRange.max
                       .upward "Number::addition overflow" with
                 | .error err => Except.error err
                 | .ok res => res.toNumber.normalize largeRange.min largeRange.max .upward)
                = .ok result := by
      have h := hok
      simp only [h_cond_true, if_true] at h
      have hddd : g_aln.doDropDigit128 zm128 e_common = (g_new, zm128 / 10, e_common + 1) := by
        unfold Guard.doDropDigit128
        rw [← hd_def, hg_new_def]
      rw [hddd] at h
      simp only at h
      simp only [← hzm_new_def] at h
      exact h
    have h_rup_exists : ∃ res : RoundResult,
        g_new.doRoundUp xn zm_new (e_common + 1) largeRange.min largeRange.max .upward
          "Number::addition overflow" = .ok res := by
      match hg : g_new.doRoundUp xn zm_new (e_common + 1) largeRange.min largeRange.max .upward "Number::addition overflow" with
      | .error e => simp only [hg, reduceCtorEq] at hok'
      | .ok r => exact ⟨r, rfl⟩
    obtain ⟨res, h_rup⟩ := h_rup_exists
    rw [h_rup] at hok'
    have hres_mant_ne : res.mantissa_ ≠ 0 :=
      Number.normalize_mantissa_ne_zero_of_result hok' hresult
    have h_inv := doRoundUp_output_invariants_upward g_new xn zm_new (e_common + 1)
      hzm_new_ge_floor hzm_new_le_maxRep "Number::addition overflow" res h_rup hres_mant_ne
    obtain ⟨h_res_min, h_res_max, h_res_exp, h_res_mod⟩ := h_inv
    set res_pos : RoundResult := { negative_ := false, mantissa_ := res.mantissa_, exponent_ := res.exponent_ }
    have hres_pos_mant_ne : res_pos.mantissa_ ≠ 0 := hres_mant_ne
    have h_rup_pos : g_new.doRoundUp false zm_new (e_common + 1) largeRange.min largeRange.max .upward "Number::addition overflow" = .ok res_pos :=
      doRoundUp_false_from_ok g_new xn zm_new (e_common + 1) .upward "Number::addition overflow" res h_rup
    have h_result_eq_res : result = res.toNumber :=
      Number.normalize_eq_of_invariants_upward h_res_min h_res_max h_res_exp h_res_mod hok'
    have h_result_abs : |result.toRat| = (res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_ := by
      rw [h_result_eq_res, abs_toRat_eq res.toNumber]; rfl
    have h_res_neg_eq_xn : result.negative_ = xn := by
      rw [h_result_eq_res]
      exact doRoundUp_negative_of_mant_ne g_new xn zm_new (e_common + 1) _ _ _ "Number::addition overflow" res h_rup hres_mant_ne
    refine ⟨zm_new, e_common + 1, f_new, g_new, res_pos, hzm_new_ge_floor, hzm_new_le_maxRep,
            hf_new_nn, hf_new_lt, h_floor_constraint, h_value_eq, h_rup_pos, h_result_abs,
            hres_pos_mant_ne, hrep_new, h_res_neg_eq_xn⟩
  · -- No drop case
    push_neg at h_drop
    have h_cond_false : (zm128 > toUInt128 largeRange.max
        || zm128 > toUInt128 maxRep) = false := by
      rw [h_cond_iff_sum_gt_maxRep]; exact decide_eq_false (by omega : ¬ zm128.toNat > maxRep.toNat)
    have h_zm_fit : zm128.toNat < 2 ^ 64 := by
      have : zm128.toNat ≤ maxRep.toNat := h_drop
      rw [hmaxRep_v] at this
      omega
    set zm_new : UInt64 := toUInt64 zm128 with hzm_new_def
    have hzm_new_toNat : zm_new.toNat = zm128.toNat := by
      rw [hzm_new_def, toNat_toUInt64 h_zm_fit]
    have hzm_new_ge_floor : mantissaFloor ≤ zm_new.toNat := by
      rw [hzm_new_toNat, hzm128_toNat]
      have h18 : (10 : ℕ) ^ 18 = 1000000000000000000 := by norm_num
      omega
    have hzm_new_le_maxRep : zm_new.toNat ≤ maxRep.toNat := by
      rw [hzm_new_toNat]; exact h_drop
    have h_floor_constraint : zm_new.toNat = mantissaFloor → (8 : ℚ) / 10 ≤ f_aln := by
      intro h_floor
      -- Premise can't hold: zm_new ≥ 10^18 > mantissaFloor
      exfalso
      have : (10 : ℕ) ^ 18 ≤ zm_new.toNat := by
        rw [hzm_new_toNat, hzm128_toNat]
        have h18 : (10 : ℕ) ^ 18 = 1000000000000000000 := by norm_num
        omega
      have : (10 : ℕ) ^ 18 ≤ mantissaFloor := h_floor ▸ this
      norm_num at this
    have h_value_eq : |truth| = ((zm_new.toNat : ℚ) + f_aln) * 10 ^ e_common := by
      rw [htruth_eq, hzm_new_toNat, hzm128_toNat]
      push_cast; ring
    have hok' : (match g_aln.doRoundUp xn zm_new e_common largeRange.min largeRange.max
                       .upward "Number::addition overflow" with
                 | .error err => Except.error err
                 | .ok res => res.toNumber.normalize largeRange.min largeRange.max .upward)
                = .ok result := by
      have h := hok
      simp only [h_cond_false, Bool.false_eq_true, if_false] at h
      simp only [← hzm_new_def] at h
      exact h
    have h_rup_exists : ∃ res : RoundResult,
        g_aln.doRoundUp xn zm_new e_common largeRange.min largeRange.max .upward
          "Number::addition overflow" = .ok res := by
      match hg : g_aln.doRoundUp xn zm_new e_common largeRange.min largeRange.max .upward "Number::addition overflow" with
      | .error e => simp only [hg, reduceCtorEq] at hok'
      | .ok r => exact ⟨r, rfl⟩
    obtain ⟨res, h_rup⟩ := h_rup_exists
    rw [h_rup] at hok'
    have hres_mant_ne : res.mantissa_ ≠ 0 :=
      Number.normalize_mantissa_ne_zero_of_result hok' hresult
    have h_inv := doRoundUp_output_invariants_upward g_aln xn zm_new e_common
      hzm_new_ge_floor hzm_new_le_maxRep "Number::addition overflow" res h_rup hres_mant_ne
    obtain ⟨h_res_min, h_res_max, h_res_exp, h_res_mod⟩ := h_inv
    set res_pos : RoundResult := { negative_ := false, mantissa_ := res.mantissa_, exponent_ := res.exponent_ }
    have hres_pos_mant_ne : res_pos.mantissa_ ≠ 0 := hres_mant_ne
    have h_rup_pos : g_aln.doRoundUp false zm_new e_common largeRange.min largeRange.max .upward "Number::addition overflow" = .ok res_pos :=
      doRoundUp_false_from_ok g_aln xn zm_new e_common .upward "Number::addition overflow" res h_rup
    have h_result_eq_res : result = res.toNumber :=
      Number.normalize_eq_of_invariants_upward h_res_min h_res_max h_res_exp h_res_mod hok'
    have h_result_abs : |result.toRat| = (res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_ := by
      rw [h_result_eq_res, abs_toRat_eq res.toNumber]; rfl
    have h_res_neg_eq_xn : result.negative_ = xn := by
      rw [h_result_eq_res]
      exact doRoundUp_negative_of_mant_ne g_aln xn zm_new e_common _ _ _ "Number::addition overflow" res h_rup hres_mant_ne
    refine ⟨zm_new, e_common, f_aln, g_aln, res_pos, hzm_new_ge_floor, hzm_new_le_maxRep,
            hf_aln_nn, hf_aln_lt, h_floor_constraint, h_value_eq, h_rup_pos, h_result_abs,
            hres_pos_mant_ne, hrep_aln, h_res_neg_eq_xn⟩

set_option maxHeartbeats 1600000 in
-- large existential with three alignment cases, each unfolding the post-alignment pipeline
/-- Same-sign branch decomposition of `operator_add` in `.upward` mode. -/
theorem operator_add_algorithmic_facts_same_sign_upward (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (h_same_sign : x.negative_ = y.negative_)
    (h_not_zero : ¬ x.operator_eq y.operator_neg)
    (hok : Number.operator_add x y .upward = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    ∃ (zm : UInt64) (ze' : Int) (f : ℚ) (g : Guard) (res_pos : RoundResult),
      mantissaFloor ≤ zm.toNat ∧
      zm.toNat ≤ maxRep.toNat ∧
      0 ≤ f ∧ f < 1 ∧
      (zm.toNat = mantissaFloor → (8 : ℚ) / 10 ≤ f) ∧
      |x.toRat + y.toRat| = ((zm.toNat : ℚ) + f) * 10 ^ ze' ∧
      g.doRoundUp false zm ze' largeRange.min largeRange.max .upward "Number::addition overflow" = .ok res_pos ∧
      |result.toRat| = (res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_ ∧
      res_pos.mantissa_ ≠ 0 ∧
      represents g f ∧
      result.negative_ = x.negative_ := by
  have hx_bounds := hx.mantissaBounds hx_mant_ne
  have hy_bounds := hy.mantissaBounds hy_mant_ne
  have hx_min : (10 : ℕ) ^ 18 ≤ x.mantissa_.toNat := by
    have := UInt64.le_iff_toNat_le.mp hx_bounds.1
    rw [largeRange_min_val] at this; exact this
  have hy_min : (10 : ℕ) ^ 18 ≤ y.mantissa_.toNat := by
    have := UInt64.le_iff_toNat_le.mp hy_bounds.1
    rw [largeRange_min_val] at this; exact this
  have hx_mant_bound : x.mantissa_.toNat < 10 ^ 19 := by
    have := UInt64.le_iff_toNat_le.mp hx_bounds.2
    rw [largeRange_max_val] at this; omega
  have hy_mant_bound : y.mantissa_.toNat < 10 ^ 19 := by
    have := UInt64.le_iff_toNat_le.mp hy_bounds.2
    rw [largeRange_max_val] at this; omega
  have hx_ne_zero : ¬ x.operator_eq Number.zero := by
    intro h
    unfold Number.operator_eq Number.zero at h
    simp only [Bool.and_eq_true, beq_iff_eq] at h
    obtain ⟨⟨_, hmeq⟩, _⟩ := h
    have hh : x.mantissa_.toNat = 0 := by rw [hmeq]; rfl
    have : (10 : ℕ) ^ 18 ≤ 0 := hh ▸ hx_min
    norm_num at this
  have hy_ne_zero : ¬ y.operator_eq Number.zero := by
    intro h
    unfold Number.operator_eq Number.zero at h
    simp only [Bool.and_eq_true, beq_iff_eq] at h
    obtain ⟨⟨_, hmeq⟩, _⟩ := h
    have hh : y.mantissa_.toNat = 0 := by rw [hmeq]; rfl
    have : (10 : ℕ) ^ 18 ≤ 0 := hh ▸ hy_min
    norm_num at this
  unfold Number.operator_add at hok
  simp only [hy_ne_zero, hx_ne_zero, h_not_zero, Bool.false_eq_true, if_false] at hok
  have h_xneqyn : (x.negative_ == y.negative_) = true := by
    rw [beq_iff_eq]; exact h_same_sign
  have h_abs_add := abs_add_eq_of_same_sign_upward h_same_sign
  by_cases h_xe_lt_ye : x.exponent_ < y.exponent_
  · -- Case 1: xe < ye, align x to ye
    rw [if_pos h_xe_lt_ye] at hok
    set g₀ : Guard := if x.negative_ then Guard.new.set_negative else Guard.new with hg₀_def
    have hg₀_rep : represents g₀ 0 := represents_initial_guard_eq_upward x.negative_
    set aln_result : UInt64 × Int × Guard :=
      Number.operator_add.alignDown x.mantissa_ x.exponent_ g₀ y.exponent_ with haln_def
    set xm_a : UInt64 := aln_result.1 with hxm_a_def
    set e_common : Int := aln_result.2.1 with he_common_def
    set g_aln : Guard := aln_result.2.2 with hg_aln_def
    have he_common_eq : e_common = y.exponent_ := by
      rw [he_common_def, haln_def]
      have := alignDown_e_eq x.mantissa_ x.exponent_ g₀ y.exponent_
      rw [this]
      exact max_eq_right (le_of_lt h_xe_lt_ye)
    have hxm_a_le_xm : xm_a.toNat ≤ x.mantissa_.toNat :=
      alignDown_mantissa_le x.mantissa_ x.exponent_ g₀ y.exponent_
    have hxm_a_lt : xm_a.toNat < 10 ^ 19 := by omega
    set K : ℕ := (max x.exponent_ y.exponent_ - x.exponent_).toNat with hK_def
    have hmax_eq : max x.exponent_ y.exponent_ = y.exponent_ :=
      max_eq_right (le_of_lt h_xe_lt_ye)
    set f_aln : ℚ := (0 + ((x.mantissa_.toNat % 10 ^ K : ℕ) : ℚ)) / 10 ^ K with hf_aln_def
    have hf_aln_rep' : represents g_aln f_aln := by
      have h := alignDown_represents x.mantissa_ x.exponent_ g₀ y.exponent_ 0 hg₀_rep
      have hK_eq : (max x.exponent_ y.exponent_ - x.exponent_).toNat = K := by rw [hK_def]
      rw [hK_eq] at h
      exact h
    have htruth_eq : |x.toRat + y.toRat|
        = ((xm_a.toNat + y.mantissa_.toNat : ℕ) : ℚ) * 10 ^ e_common
          + f_aln * 10 ^ e_common := by
      rw [h_abs_add]
      have h_lemma := alignDown_abs_value_upward x g₀ y.exponent_ (le_of_lt h_xe_lt_ye) 0
      simp only at h_lemma
      have hK_eq : (y.exponent_ - x.exponent_).toNat = K := by
        rw [hK_def, hmax_eq]
      rw [hK_eq] at h_lemma
      have h_y_abs : |y.toRat| = (y.mantissa_.toNat : ℚ) * 10 ^ y.exponent_ := abs_toRat_eq y
      rw [h_y_abs]
      rw [he_common_eq]
      have h_pre : |x.toRat| = ((xm_a.toNat : ℚ) + f_aln) * 10 ^ y.exponent_ := by
        have : |x.toRat| + 0 * 10 ^ x.exponent_ = ((xm_a.toNat : ℚ) + f_aln) * 10 ^ y.exponent_ := by
          have := h_lemma
          rw [hf_aln_def]; rw [hxm_a_def, haln_def] at *
          convert this using 2
        linarith
      rw [h_pre]
      push_cast; ring
    have hsum_ge : 10 ^ 18 ≤ xm_a.toNat + y.mantissa_.toNat := by
      have : (10 : ℕ) ^ 18 ≤ y.mantissa_.toNat := hy_min
      omega
    have hok_post : (let zm128 : UInt128 := toUInt128 xm_a + toUInt128 y.mantissa_
                     let p : UInt64 × Int × Guard :=
                       if zm128 > toUInt128 largeRange.max || zm128 > toUInt128 maxRep then
                         (toUInt64 (g_aln.doDropDigit128 zm128 e_common).2.1,
                          (g_aln.doDropDigit128 zm128 e_common).2.2,
                          (g_aln.doDropDigit128 zm128 e_common).1)
                       else (toUInt64 zm128, e_common, g_aln)
                     match p.2.2.doRoundUp x.negative_ p.1 p.2.1 largeRange.min largeRange.max
                           .upward "Number::addition overflow" with
                     | .error err => Except.error err
                     | .ok res => res.toNumber.normalize largeRange.min largeRange.max .upward)
                    = .ok result := by
      have h := hok
      simp only [h_xneqyn, if_true] at h
      exact h
    exact operator_add_post_alignment_upward x.negative_ xm_a y.mantissa_ e_common g_aln
      f_aln hf_aln_rep' hxm_a_lt hy_mant_bound hsum_ge (x.toRat + y.toRat) htruth_eq
      result hresult hok_post
  · push_neg at h_xe_lt_ye
    by_cases h_xe_gt_ye : x.exponent_ > y.exponent_
    · -- Case 2: xe > ye, align y to xe
      rw [if_neg (not_lt.mpr (le_of_lt h_xe_gt_ye)), if_pos h_xe_gt_ye] at hok
      set g₀ : Guard := if y.negative_ then Guard.new.set_negative else Guard.new with hg₀_def
      have hg₀_rep : represents g₀ 0 := represents_initial_guard_eq_upward y.negative_
      set aln_result : UInt64 × Int × Guard :=
        Number.operator_add.alignDown y.mantissa_ y.exponent_ g₀ x.exponent_ with haln_def
      set ym_a : UInt64 := aln_result.1 with hym_a_def
      set e_common : Int := aln_result.2.1 with he_common_def
      set g_aln : Guard := aln_result.2.2 with hg_aln_def
      have he_common_eq : e_common = x.exponent_ := by
        rw [he_common_def, haln_def]
        have := alignDown_e_eq y.mantissa_ y.exponent_ g₀ x.exponent_
        rw [this]
        exact max_eq_right (le_of_lt h_xe_gt_ye)
      have hym_a_le_ym : ym_a.toNat ≤ y.mantissa_.toNat :=
        alignDown_mantissa_le y.mantissa_ y.exponent_ g₀ x.exponent_
      have hym_a_lt : ym_a.toNat < 10 ^ 19 := by omega
      set K : ℕ := (max y.exponent_ x.exponent_ - y.exponent_).toNat with hK_def
      have hmax_eq : max y.exponent_ x.exponent_ = x.exponent_ :=
        max_eq_right (le_of_lt h_xe_gt_ye)
      set f_aln : ℚ := (0 + ((y.mantissa_.toNat % 10 ^ K : ℕ) : ℚ)) / 10 ^ K with hf_aln_def
      have hf_aln_rep' : represents g_aln f_aln := by
        have h := alignDown_represents y.mantissa_ y.exponent_ g₀ x.exponent_ 0 hg₀_rep
        have hK_eq : (max y.exponent_ x.exponent_ - y.exponent_).toNat = K := by rw [hK_def]
        rw [hK_eq] at h
        exact h
      have htruth_eq : |x.toRat + y.toRat|
          = ((x.mantissa_.toNat + ym_a.toNat : ℕ) : ℚ) * 10 ^ e_common
            + f_aln * 10 ^ e_common := by
        rw [h_abs_add]
        have h_lemma := alignDown_abs_value_upward y g₀ x.exponent_ (le_of_lt h_xe_gt_ye) 0
        simp only at h_lemma
        have hK_eq : (x.exponent_ - y.exponent_).toNat = K := by
          rw [hK_def, hmax_eq]
        rw [hK_eq] at h_lemma
        have h_x_abs : |x.toRat| = (x.mantissa_.toNat : ℚ) * 10 ^ x.exponent_ := abs_toRat_eq x
        rw [h_x_abs]
        rw [he_common_eq]
        have h_pre : |y.toRat| = ((ym_a.toNat : ℚ) + f_aln) * 10 ^ x.exponent_ := by
          have : |y.toRat| + 0 * 10 ^ y.exponent_ = ((ym_a.toNat : ℚ) + f_aln) * 10 ^ x.exponent_ := by
            have := h_lemma
            rw [hf_aln_def]; rw [hym_a_def, haln_def] at *
            convert this using 2
          linarith
        rw [h_pre]
        push_cast; ring
      have hsum_ge : 10 ^ 18 ≤ x.mantissa_.toNat + ym_a.toNat := by
        have : (10 : ℕ) ^ 18 ≤ x.mantissa_.toNat := hx_min
        omega
      have hok_post : (let zm128 : UInt128 := toUInt128 x.mantissa_ + toUInt128 ym_a
                       let p : UInt64 × Int × Guard :=
                         if zm128 > toUInt128 largeRange.max || zm128 > toUInt128 maxRep then
                           (toUInt64 (g_aln.doDropDigit128 zm128 e_common).2.1,
                            (g_aln.doDropDigit128 zm128 e_common).2.2,
                            (g_aln.doDropDigit128 zm128 e_common).1)
                         else (toUInt64 zm128, e_common, g_aln)
                       match p.2.2.doRoundUp x.negative_ p.1 p.2.1 largeRange.min largeRange.max
                             .upward "Number::addition overflow" with
                       | .error err => Except.error err
                       | .ok res => res.toNumber.normalize largeRange.min largeRange.max .upward)
                      = .ok result := by
        have h := hok
        simp only [h_xneqyn, if_true] at h
        rw [he_common_eq]
        exact h
      exact operator_add_post_alignment_upward x.negative_ x.mantissa_ ym_a e_common g_aln
        f_aln hf_aln_rep' hx_mant_bound hym_a_lt hsum_ge (x.toRat + y.toRat) htruth_eq
        result hresult hok_post
    · -- Case 3: xe = ye
      push_neg at h_xe_gt_ye
      have h_xe_eq_ye : x.exponent_ = y.exponent_ := le_antisymm h_xe_gt_ye h_xe_lt_ye
      rw [if_neg (not_lt.mpr h_xe_lt_ye), if_neg (not_lt.mpr h_xe_gt_ye)] at hok
      set xm_a : UInt64 := x.mantissa_ with hxm_a_def
      set ym_a : UInt64 := y.mantissa_ with hym_a_def
      set e_common : Int := x.exponent_ with he_common_def
      set g_aln : Guard := Guard.new with hg_aln_def
      set f_aln : ℚ := 0 with hf_aln_def
      have hf_aln_rep' : represents g_aln f_aln := by
        rw [hg_aln_def, hf_aln_def]; exact represents_new
      have htruth_eq : |x.toRat + y.toRat|
          = ((xm_a.toNat + ym_a.toNat : ℕ) : ℚ) * 10 ^ e_common
            + f_aln * 10 ^ e_common := by
        rw [h_abs_add]
        have h_x_abs : |x.toRat| = (x.mantissa_.toNat : ℚ) * 10 ^ x.exponent_ := abs_toRat_eq x
        have h_y_abs : |y.toRat| = (y.mantissa_.toNat : ℚ) * 10 ^ y.exponent_ := abs_toRat_eq y
        rw [h_x_abs, h_y_abs, hxm_a_def, hym_a_def, he_common_def, hf_aln_def, ← h_xe_eq_ye]
        push_cast; ring
      have hsum_ge : 10 ^ 18 ≤ xm_a.toNat + ym_a.toNat := by
        rw [hxm_a_def, hym_a_def]
        have : (10 : ℕ) ^ 18 ≤ x.mantissa_.toNat := hx_min
        omega
      have hok_post : (let zm128 : UInt128 := toUInt128 xm_a + toUInt128 ym_a
                       let p : UInt64 × Int × Guard :=
                         if zm128 > toUInt128 largeRange.max || zm128 > toUInt128 maxRep then
                           (toUInt64 (g_aln.doDropDigit128 zm128 e_common).2.1,
                            (g_aln.doDropDigit128 zm128 e_common).2.2,
                            (g_aln.doDropDigit128 zm128 e_common).1)
                         else (toUInt64 zm128, e_common, g_aln)
                       match p.2.2.doRoundUp x.negative_ p.1 p.2.1 largeRange.min largeRange.max
                             .upward "Number::addition overflow" with
                       | .error err => Except.error err
                       | .ok res => res.toNumber.normalize largeRange.min largeRange.max .upward)
                      = .ok result := by
        have h := hok
        simp only [h_xneqyn, if_true] at h
        exact h
      have hxm_a_lt : xm_a.toNat < 10 ^ 19 := by rw [hxm_a_def]; exact hx_mant_bound
      have hym_a_lt : ym_a.toNat < 10 ^ 19 := by rw [hym_a_def]; exact hy_mant_bound
      exact operator_add_post_alignment_upward x.negative_ xm_a ym_a e_common g_aln
        f_aln hf_aln_rep' hxm_a_lt hym_a_lt hsum_ge (x.toRat + y.toRat) htruth_eq
        result hresult hok_post

end XRPL.Model.Protocol
