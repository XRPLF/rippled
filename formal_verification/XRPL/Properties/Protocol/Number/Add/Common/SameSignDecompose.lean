import XRPL.Properties.Protocol.Number.Common.Notation
import XRPL.Properties.Protocol.Number.Add.Common.Decompose
import XRPL.Properties.Protocol.Number.Common.Helpers
import XRPL.Properties.Protocol.Number.Common.Rounding.DoRoundUp
import XRPL.Properties.Protocol.Number.Common.Rounding.Normalize
import XRPL.Properties.Protocol.Number.Normalize.Common.ResultFacts


namespace XRPL.Model.Protocol

structure PostAlignSpec (truth : ℚ) (xn : Bool) (result : Number) (mode : rounding_mode)
    (zm : UInt64) (ze' : Int) (f : ℚ) (g : Guard) (res_pos : RoundResult) : Prop where
  zm_ge_floor : mantissaFloor ≤ zm.toNat
  zm_le_maxRepUp : zm.toNat ≤ maxRepUp.toNat
  f_nonneg : 0 ≤ f
  f_lt_one : f < 1
  floor_cusp : zm.toNat = mantissaFloor → (8 : ℚ) / 10 ≤ f
  value_eq : |truth| = ((zm.toNat : ℚ) + f) * 10 ^ ze'
  rounds : g.doRoundUp false zm ze' largeRange.min largeRange.max mode
    "Number::addition overflow" = .ok res_pos
  result_abs : |result.toRat| = (res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_
  res_mant_ne : res_pos.mantissa_ ≠ 0
  represents_f : represents g f
  result_neg : result.negative_ = xn
  sbit : g.sbit_ = xn
  zm_succ : mantissaFloorSucc ≤ zm.toNat
  result_norm : result.isNormalized
  truth_top : |truth| < 10 ^ 19 * (10 : ℚ) ^ (maxExponent : ℤ)
  result_mant_ne : result.mantissa_ ≠ 0

private theorem operator_add_post_alignment_anyMode
    {mode : rounding_mode} (xn : Bool) (xm_a ym_a : UInt64) (e_common : Int) (g_aln : Guard)
    (f_aln : ℚ) (hrep_aln : represents g_aln f_aln)
    (h_g_aln_sbit : g_aln.sbit_ = xn)
    (hxm_lt : xm_a.toNat < 10 ^ 19) (hym_lt : ym_a.toNat < 10 ^ 19)
    (hsum_ge : 10 ^ 18 ≤ xm_a.toNat + ym_a.toNat)
    (he_common : minExponent ≤ e_common)
    (he_common_le : e_common ≤ maxExponent)
    (truth : ℚ)
    (htruth_eq : |truth| = ((xm_a.toNat + ym_a.toNat : ℕ) : ℚ) * 10 ^ e_common
                            + f_aln * 10 ^ e_common)
    (result : Number)
    (hok : (let zm128 : UInt128 := toUInt128 xm_a + toUInt128 ym_a
            let p : UInt64 × Int × Guard :=
              if zm128 > toUInt128 largeRange.max || zm128 > toUInt128 maxRepUp then
                (toUInt64 (g_aln.doDropDigit128 zm128 e_common).2.1,
                 (g_aln.doDropDigit128 zm128 e_common).2.2,
                 (g_aln.doDropDigit128 zm128 e_common).1)
              else (toUInt64 zm128, e_common, g_aln)
            match p.2.2.doRoundUp xn p.1 p.2.1 largeRange.min largeRange.max
                  mode "Number::addition overflow" with
            | .error err => Except.error err
            | .ok res => res.toNumber.normalize largeRange.min largeRange.max mode)
           = .ok result) :
    ∃ (zm : UInt64) (ze' : Int) (f : ℚ) (g : Guard) (res_pos : RoundResult),
      PostAlignSpec truth xn result mode zm ze' f g res_pos := by
  have hf_aln_nn : 0 ≤ f_aln := represents_nonneg hrep_aln
  have hf_aln_lt : f_aln < 1 := represents_lt_one hrep_aln
  have hxm_lt' : xm_a.toNat < 10 ^ 19 := hxm_lt
  have hym_lt' : ym_a.toNat < 10 ^ 19 := hym_lt
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
  have hmaxRepUp_v : maxRepUp.toNat = maxRepUpNat := rfl
  have htu_maxRepUp_v : (toUInt128 maxRepUp).toNat = 9223372036854775810 := by
    rw [toNat_toUInt128]
    exact hmaxRepUp_v
  have h_cond_iff_sum_gt_maxRepUp : (zm128 > toUInt128 largeRange.max
        || zm128 > toUInt128 maxRepUp) = decide (zm128.toNat > maxRepUp.toNat) := by
    by_cases hgt : zm128.toNat > maxRepUp.toNat
    · have h_zm_gt_maxRepUp : zm128 > toUInt128 maxRepUp := by
        change toUInt128 maxRepUp < zm128
        rw [BitVec.lt_def, htu_maxRepUp_v]
        rw [hmaxRepUp_v] at hgt; exact hgt
      rw [decide_eq_true hgt]
      have : (decide (toUInt128 maxRepUp < zm128) : Bool) = true := decide_eq_true h_zm_gt_maxRepUp
      change (decide (toUInt128 largeRange.max < zm128)
              || decide (toUInt128 maxRepUp < zm128)) = true
      rw [this]; simp
    · push_neg at hgt
      have h_zm_le : zm128.toNat ≤ maxRepUp.toNat := hgt
      have h_zm_not_gt_maxRepUp : ¬ zm128 > toUInt128 maxRepUp := by
        intro h
        have : (toUInt128 maxRepUp).toNat < zm128.toNat := BitVec.lt_def.mp h
        rw [htu_maxRepUp_v] at this
        rw [hmaxRepUp_v] at h_zm_le; omega
      have h_zm_not_gt_lrmax : ¬ zm128 > toUInt128 largeRange.max := by
        intro h
        have : (toUInt128 largeRange.max).toNat < zm128.toNat := BitVec.lt_def.mp h
        rw [htu_max_v] at this
        rw [hmaxRepUp_v] at h_zm_le; omega
      rw [decide_eq_false (by omega : ¬ zm128.toNat > maxRepUp.toNat)]
      change (decide (toUInt128 largeRange.max < zm128)
              || decide (toUInt128 maxRepUp < zm128)) = false
      rw [decide_eq_false h_zm_not_gt_lrmax, decide_eq_false h_zm_not_gt_maxRepUp]
      rfl
  by_cases h_drop : zm128.toNat > maxRepUp.toNat
  · -- Drop case
    have h_cond_true : (zm128 > toUInt128 largeRange.max
        || zm128 > toUInt128 maxRepUp) = true := by
      rw [h_cond_iff_sum_gt_maxRepUp]; exact decide_eq_true h_drop
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
    have hzm_new_le_maxRepUp : zm_new.toNat ≤ maxRepUp.toNat := by
      rw [hmaxRepUp_v]; rw [hmaxRep_v] at hzm_new_le_maxRep; omega
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
    -- Floor constraint: vacuous in the drop branch — `zm_new = floor` would force
    -- `zm128 ≤ 9223372036854775809 < maxRepUp + 1 ≤ zm128`, a contradiction.
    have h_floor_constraint : zm_new.toNat = mantissaFloor → (8 : ℚ) / 10 ≤ f_new := by
      intro h_floor
      exfalso
      have hd : zm_new.toNat = zm128.toNat / 10 := hzm_new_toNat
      have h_div_eq : zm128.toNat / 10 = mantissaFloor := h_floor ▸ hd.symm
      rw [hmaxRepUp_v] at h_drop
      omega
    have hok' : (match g_new.doRoundUp xn zm_new (e_common + 1) largeRange.min largeRange.max
                       mode "Number::addition overflow" with
                 | .error err => Except.error err
                 | .ok res => res.toNumber.normalize largeRange.min largeRange.max mode)
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
        g_new.doRoundUp xn zm_new (e_common + 1) largeRange.min largeRange.max mode
          "Number::addition overflow" = .ok res := by
      match hg : g_new.doRoundUp xn zm_new (e_common + 1) largeRange.min largeRange.max mode "Number::addition overflow" with
      | .error e => simp only [hg, reduceCtorEq] at hok'
      | .ok r => exact ⟨r, rfl⟩
    obtain ⟨res, h_rup⟩ := h_rup_exists
    rw [h_rup] at hok'
    -- No flush: the same-sign value sits at or above the smallest representable.
    have hres_mant_ne : res.mantissa_ ≠ 0 := by
      intro hres0
      have hflush := doRoundUp_flush_value_small g_new xn zm_new (e_common + 1) mode
        hzm_new_ge_floor hzm_new_le_maxRepUp "Number::addition overflow" res h_rup hres0
      have hzm_q : (mantissaFloor : ℚ) ≤ (zm_new.toNat : ℚ) := by
        exact_mod_cast hzm_new_ge_floor
      have hA : ((mantissaFloor : ℚ) + 1) * (10 : ℚ) ^ ((minExponent : ℤ) + 1)
          ≤ ((zm_new.toNat : ℚ) + 1) * (10 : ℚ) ^ (e_common + 1) :=
        mul_le_mul (by linarith) (zpow_le_zpow_right₀ (by norm_num) (by omega))
          (le_of_lt (zpow_pos (by norm_num) _)) (by positivity)
      have hB : (10 : ℚ) ^ (18 : ℕ) * (10 : ℚ) ^ (minExponent : ℤ)
          < ((mantissaFloor : ℚ) + 1) * (10 : ℚ) ^ ((minExponent : ℤ) + 1) := by
        have h18 : (10 : ℚ) ^ (18 : ℕ) = 1000000000000000000 := by norm_num
        rw [h18, zpow_add_one₀ (by norm_num : (10 : ℚ) ≠ 0)]
        nlinarith [zpow_pos (by norm_num : (0 : ℚ) < 10) (minExponent : ℤ)]
      linarith
    have h_inv := doRoundUp_output_invariants_anyMode g_new xn zm_new (e_common + 1) mode
      hzm_new_ge_floor hzm_new_le_maxRep "Number::addition overflow" res h_rup hres_mant_ne
    obtain ⟨h_res_min, h_res_max, h_res_exp, h_res_mod⟩ := h_inv
    set res_pos : RoundResult := { negative_ := false, mantissa_ := res.mantissa_, exponent_ := res.exponent_ }
    have hres_pos_mant_ne : res_pos.mantissa_ ≠ 0 := hres_mant_ne
    have h_rup_pos : g_new.doRoundUp false zm_new (e_common + 1) largeRange.min largeRange.max mode "Number::addition overflow" = .ok res_pos :=
      doRoundUp_false_from_ok g_new xn zm_new (e_common + 1) mode "Number::addition overflow" res h_rup
    have h_result_eq_res : result = res.toNumber :=
      Number.normalize_eq_of_invariants h_res_min h_res_max h_res_exp h_res_mod hok'
    have h_result_abs : |result.toRat| = (res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_ := by
      rw [h_result_eq_res, abs_toRat_eq res.toNumber]; rfl
    have h_res_neg_eq_xn : result.negative_ = xn := by
      rw [h_result_eq_res]
      exact doRoundUp_negative_of_mant_ne g_new xn zm_new (e_common + 1) _ _ _ "Number::addition overflow" res h_rup hres_mant_ne
    have h_g_new_sbit : g_new.sbit_ = xn := by
      rw [hg_new_def]
      have h_push_sbit : (g_aln.push d).sbit_ = g_aln.sbit_ := rfl
      rw [h_push_sbit]
      exact h_g_aln_sbit
    have hzm_new_succ : mantissaFloorSucc ≤ zm_new.toNat := by
      rw [hzm_new_toNat]
      have h := h_drop
      rw [hmaxRepUp_v] at h
      omega
    have h_result_norm : result.isNormalized := by
      rw [h_result_eq_res]
      right
      refine ⟨UInt64.le_iff_toNat_le.mpr h_res_min, UInt64.le_iff_toNat_le.mpr h_res_max, ?_,
        h_res_exp,
        doRoundUp_exponent_le_max _ xn zm_new (e_common + 1) mode
          "Number::addition overflow" res h_rup⟩
      by_cases hc : res.mantissa_.toNat ≤ maxRep.toNat
      · left
        exact UInt64.le_iff_toNat_le.mpr hc
      · right
        exact h_res_mod (by omega)
    have h_truth_top : |truth| < 10 ^ 19 * (10 : ℚ) ^ (maxExponent : ℤ) :=
      doRoundUp_stage_truth_top truth g_new false zm_new (e_common + 1) f_new mode
        "Number::addition overflow" res_pos h_value_eq hzm_new_le_maxRepUp hf_new_nn hf_new_lt
        (by omega) h_rup_pos hres_pos_mant_ne
    have hresult_ne : result.mantissa_ ≠ 0 := by
      rw [h_result_eq_res]
      exact hres_mant_ne
    exact ⟨zm_new, e_common + 1, f_new, g_new, res_pos,
      ⟨hzm_new_ge_floor, hzm_new_le_maxRepUp, hf_new_nn, hf_new_lt, h_floor_constraint,
       h_value_eq, h_rup_pos, h_result_abs, hres_pos_mant_ne, hrep_new, h_res_neg_eq_xn,
       h_g_new_sbit, hzm_new_succ, h_result_norm, h_truth_top, hresult_ne⟩⟩
  · -- No drop case: zm128 ≤ maxRepUp (the cusp range (maxRep, maxRepUp] is reachable)
    push_neg at h_drop
    have h_cond_false : (zm128 > toUInt128 largeRange.max
        || zm128 > toUInt128 maxRepUp) = false := by
      rw [h_cond_iff_sum_gt_maxRepUp]
      exact decide_eq_false (by omega : ¬ zm128.toNat > maxRepUp.toNat)
    have h_zm_fit : zm128.toNat < 2 ^ 64 := by
      have : zm128.toNat ≤ maxRepUp.toNat := h_drop
      rw [hmaxRepUp_v] at this
      omega
    set zm_new : UInt64 := toUInt64 zm128 with hzm_new_def
    have hzm_new_toNat : zm_new.toNat = zm128.toNat := by
      rw [hzm_new_def, toNat_toUInt64 h_zm_fit]
    have hzm_new_ge_floor : mantissaFloor ≤ zm_new.toNat := by
      rw [hzm_new_toNat, hzm128_toNat]
      have h18 : (10 : ℕ) ^ 18 = 1000000000000000000 := by norm_num
      omega
    have hzm_new_le_maxRepUp : zm_new.toNat ≤ maxRepUp.toNat := by
      rw [hzm_new_toNat]; exact h_drop
    have h_value_eq : |truth| = ((zm_new.toNat : ℚ) + f_aln) * 10 ^ e_common := by
      rw [htruth_eq, hzm_new_toNat, hzm128_toNat]
      push_cast; ring
    have h_floor_constraint : zm_new.toNat = mantissaFloor → (8 : ℚ) / 10 ≤ f_aln := by
      intro h_floor
      exfalso
      have : (10 : ℕ) ^ 18 ≤ zm_new.toNat := by
        rw [hzm_new_toNat, hzm128_toNat]
        have h18 : (10 : ℕ) ^ 18 = 1000000000000000000 := by norm_num
        omega
      have : (10 : ℕ) ^ 18 ≤ mantissaFloor := h_floor ▸ this
      norm_num at this
    have hok' : (match g_aln.doRoundUp xn zm_new e_common largeRange.min largeRange.max
                       mode "Number::addition overflow" with
                 | .error err => Except.error err
                 | .ok res => res.toNumber.normalize largeRange.min largeRange.max mode)
                = .ok result := by
      have h := hok
      simp only [h_cond_false, Bool.false_eq_true, if_false] at h
      simp only [← hzm_new_def] at h
      exact h
    have h_rup_exists : ∃ res : RoundResult,
        g_aln.doRoundUp xn zm_new e_common largeRange.min largeRange.max mode
          "Number::addition overflow" = .ok res := by
      match hg : g_aln.doRoundUp xn zm_new e_common largeRange.min largeRange.max mode "Number::addition overflow" with
      | .error e => simp only [hg, reduceCtorEq] at hok'
      | .ok r => exact ⟨r, rfl⟩
    obtain ⟨res, h_rup⟩ := h_rup_exists
    rw [h_rup] at hok'
    -- No flush: the same-sign value sits at or above the smallest representable.
    have hres_mant_ne : res.mantissa_ ≠ 0 := by
      intro hres0
      have hflush := doRoundUp_flush_value_small g_aln xn zm_new e_common mode
        hzm_new_ge_floor hzm_new_le_maxRepUp "Number::addition overflow" res h_rup hres0
      have hzm18 : (1000000000000000000 : ℚ) ≤ (zm_new.toNat : ℚ) := by
        have h : 1000000000000000000 ≤ zm_new.toNat := by
          rw [hzm_new_toNat, hzm128_toNat]
          have h18 : (10 : ℕ) ^ 18 = 1000000000000000000 := by norm_num
          omega
        exact_mod_cast h
      have hA : ((1000000000000000000 : ℚ) + 1) * (10 : ℚ) ^ (minExponent : ℤ)
          ≤ ((zm_new.toNat : ℚ) + 1) * (10 : ℚ) ^ e_common :=
        mul_le_mul (by linarith) (zpow_le_zpow_right₀ (by norm_num) he_common)
          (le_of_lt (zpow_pos (by norm_num) _)) (by positivity)
      have hB : (10 : ℚ) ^ (18 : ℕ) * (10 : ℚ) ^ (minExponent : ℤ)
          < ((1000000000000000000 : ℚ) + 1) * (10 : ℚ) ^ (minExponent : ℤ) := by
        have h18 : (10 : ℚ) ^ (18 : ℕ) = 1000000000000000000 := by norm_num
        rw [h18]
        exact mul_lt_mul_of_pos_right (by norm_num) (zpow_pos (by norm_num) _)
      linarith
    have h_inv := doRoundUp_output_invariants_upTo_maxRepUp_anyMode g_aln xn zm_new e_common mode
      hzm_new_ge_floor hzm_new_le_maxRepUp "Number::addition overflow" res h_rup hres_mant_ne
    obtain ⟨h_res_min, h_res_max, h_res_exp, h_res_mod⟩ := h_inv
    set res_pos : RoundResult := { negative_ := false, mantissa_ := res.mantissa_, exponent_ := res.exponent_ }
    have hres_pos_mant_ne : res_pos.mantissa_ ≠ 0 := hres_mant_ne
    have h_rup_pos : g_aln.doRoundUp false zm_new e_common largeRange.min largeRange.max mode "Number::addition overflow" = .ok res_pos :=
      doRoundUp_false_from_ok g_aln xn zm_new e_common mode "Number::addition overflow" res h_rup
    have h_result_eq_res : result = res.toNumber :=
      Number.normalize_eq_of_invariants h_res_min h_res_max h_res_exp h_res_mod hok'
    have h_result_abs : |result.toRat| = (res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_ := by
      rw [h_result_eq_res, abs_toRat_eq res.toNumber]; rfl
    have h_res_neg_eq_xn : result.negative_ = xn := by
      rw [h_result_eq_res]
      exact doRoundUp_negative_of_mant_ne g_aln xn zm_new e_common _ _ _ "Number::addition overflow" res h_rup hres_mant_ne
    have hzm_new_succ : mantissaFloorSucc ≤ zm_new.toNat := by
      rw [hzm_new_toNat, hzm128_toNat]
      have h18 : (10 : ℕ) ^ 18 = 1000000000000000000 := by norm_num
      omega
    have h_result_norm : result.isNormalized := by
      rw [h_result_eq_res]
      right
      refine ⟨UInt64.le_iff_toNat_le.mpr h_res_min, UInt64.le_iff_toNat_le.mpr h_res_max, ?_,
        h_res_exp,
        doRoundUp_exponent_le_max _ xn zm_new e_common mode
          "Number::addition overflow" res h_rup⟩
      by_cases hc : res.mantissa_.toNat ≤ maxRep.toNat
      · left
        exact UInt64.le_iff_toNat_le.mpr hc
      · right
        exact h_res_mod (by omega)
    have h_truth_top : |truth| < 10 ^ 19 * (10 : ℚ) ^ (maxExponent : ℤ) :=
      doRoundUp_stage_truth_top truth g_aln false zm_new e_common f_aln mode
        "Number::addition overflow" res_pos h_value_eq hzm_new_le_maxRepUp hf_aln_nn hf_aln_lt
        (by omega) h_rup_pos hres_pos_mant_ne
    have hresult_ne : result.mantissa_ ≠ 0 := by
      rw [h_result_eq_res]
      exact hres_mant_ne
    exact ⟨zm_new, e_common, f_aln, g_aln, res_pos,
      ⟨hzm_new_ge_floor, hzm_new_le_maxRepUp, hf_aln_nn, hf_aln_lt, h_floor_constraint,
       h_value_eq, h_rup_pos, h_result_abs, hres_pos_mant_ne, hrep_aln, h_res_neg_eq_xn,
       h_g_aln_sbit, hzm_new_succ, h_result_norm, h_truth_top, hresult_ne⟩⟩

set_option maxHeartbeats 1600000 in
-- The mode-generic same-sign decomposition discharges all 16 conjuncts of the
-- post-alignment spec in one pass; the combined elaboration exceeds the default
-- heartbeat budget.
theorem operator_add_algorithmic_facts_same_sign_anyMode (x y result : Number) (mode : rounding_mode)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (h_same_sign : x.negative_ = y.negative_)
    (h_not_zero : ¬ x.operator_eq y.operator_neg)
    (hok : Number.operator_add x y mode = .ok result) :
    ∃ (zm : UInt64) (ze' : Int) (f : ℚ) (g : Guard) (res_pos : RoundResult),
      PostAlignSpec (x.toRat + y.toRat) x.negative_ result mode zm ze' f g res_pos := by
  have hxe_min : minExponent ≤ x.exponent_ := by
    rcases hx with hz | ⟨_, _, _, hemin, _⟩
    · exfalso; apply hx_mant_ne; rw [hz]; rfl
    · exact hemin
  have hye_min : minExponent ≤ y.exponent_ := by
    rcases hy with hz | ⟨_, _, _, hemin, _⟩
    · exfalso; apply hy_mant_ne; rw [hz]; rfl
    · exact hemin
  have hx_bounds := hx.mantissaBounds hx_mant_ne
  have hy_bounds := hy.mantissaBounds hy_mant_ne
  have hx_min : (10 : ℕ) ^ 18 ≤ x.mantissa_.toNat := (mantissaBounds_nat_of hx_bounds).1
  have hy_min : (10 : ℕ) ^ 18 ≤ y.mantissa_.toNat := (mantissaBounds_nat_of hy_bounds).1
  have hx_mant_bound : x.mantissa_.toNat < 10 ^ 19 := (mantissaBounds_nat_of hx_bounds).2
  have hy_mant_bound : y.mantissa_.toNat < 10 ^ 19 := (mantissaBounds_nat_of hy_bounds).2
  have hxe_max : x.exponent_ ≤ maxExponent := by
    rcases hx with hz | ⟨_, _, _, _, hemax⟩
    · exfalso; apply hx_mant_ne; rw [hz]; rfl
    · exact hemax
  have hye_max : y.exponent_ ≤ maxExponent := by
    rcases hy with hz | ⟨_, _, _, _, hemax⟩
    · exfalso; apply hy_mant_ne; rw [hz]; rfl
    · exact hemax
  have hx_ne_zero : ¬ x.operator_eq Number.zero := by
    intro h
    have hmeq := Number.mantissa_eq_zero_of_operator_eq_zero h
    have hh : x.mantissa_.toNat = 0 := by rw [hmeq]; rfl
    have : (10 : ℕ) ^ 18 ≤ 0 := hh ▸ hx_min
    norm_num at this
  have hy_ne_zero : ¬ y.operator_eq Number.zero := by
    intro h
    have hmeq := Number.mantissa_eq_zero_of_operator_eq_zero h
    have hh : y.mantissa_.toNat = 0 := by rw [hmeq]; rfl
    have : (10 : ℕ) ^ 18 ≤ 0 := hh ▸ hy_min
    norm_num at this
  unfold Number.operator_add at hok
  simp only [hy_ne_zero, hx_ne_zero, h_not_zero, Bool.false_eq_true, if_false] at hok
  have h_xneqyn : (x.negative_ == y.negative_) = true := by
    rw [beq_iff_eq]; exact h_same_sign
  have h_abs_add := abs_add_eq_of_same_sign h_same_sign
  by_cases h_xe_lt_ye : x.exponent_ < y.exponent_
  · rw [if_pos h_xe_lt_ye] at hok
    set g₀ : Guard := if x.negative_ then Guard.new.set_negative else Guard.new with hg₀_def
    have hg₀_rep : represents g₀ 0 := represents_initial_guard_eq x.negative_
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
      have h_lemma := alignDown_abs_value x g₀ y.exponent_ (le_of_lt h_xe_lt_ye) 0
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
                       if zm128 > toUInt128 largeRange.max || zm128 > toUInt128 maxRepUp then
                         (toUInt64 (g_aln.doDropDigit128 zm128 e_common).2.1,
                          (g_aln.doDropDigit128 zm128 e_common).2.2,
                          (g_aln.doDropDigit128 zm128 e_common).1)
                       else (toUInt64 zm128, e_common, g_aln)
                     match p.2.2.doRoundUp x.negative_ p.1 p.2.1 largeRange.min largeRange.max
                           mode "Number::addition overflow" with
                     | .error err => Except.error err
                     | .ok res => res.toNumber.normalize largeRange.min largeRange.max mode)
                    = .ok result := by
      have h := hok
      simp only [h_xneqyn, if_true] at h
      exact h
    have h_g_aln_sbit : g_aln.sbit_ = x.negative_ := by
      rw [hg_aln_def, haln_def, alignDown_sbit_preserved, hg₀_def]
      cases hxn : x.negative_ <;> rfl
    have h_result := operator_add_post_alignment_anyMode x.negative_ xm_a y.mantissa_ e_common g_aln
      f_aln hf_aln_rep' h_g_aln_sbit hxm_a_lt hy_mant_bound hsum_ge
      (by rw [he_common_eq]; exact hye_min)
      (by rw [he_common_eq]; exact hye_max) (x.toRat + y.toRat) htruth_eq
      result hok_post
    exact h_result
  · push_neg at h_xe_lt_ye
    by_cases h_xe_gt_ye : x.exponent_ > y.exponent_
    · rw [if_neg (not_lt.mpr (le_of_lt h_xe_gt_ye)), if_pos h_xe_gt_ye] at hok
      set g₀ : Guard := if y.negative_ then Guard.new.set_negative else Guard.new with hg₀_def
      have hg₀_rep : represents g₀ 0 := represents_initial_guard_eq y.negative_
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
        have h_lemma := alignDown_abs_value y g₀ x.exponent_ (le_of_lt h_xe_gt_ye) 0
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
                         if zm128 > toUInt128 largeRange.max || zm128 > toUInt128 maxRepUp then
                           (toUInt64 (g_aln.doDropDigit128 zm128 e_common).2.1,
                            (g_aln.doDropDigit128 zm128 e_common).2.2,
                            (g_aln.doDropDigit128 zm128 e_common).1)
                         else (toUInt64 zm128, e_common, g_aln)
                       match p.2.2.doRoundUp x.negative_ p.1 p.2.1 largeRange.min largeRange.max
                             mode "Number::addition overflow" with
                       | .error err => Except.error err
                       | .ok res => res.toNumber.normalize largeRange.min largeRange.max mode)
                      = .ok result := by
        have h := hok
        simp only [h_xneqyn, if_true] at h
        rw [he_common_eq]
        exact h
      have h_g_aln_sbit : g_aln.sbit_ = x.negative_ := by
        rw [hg_aln_def, haln_def, alignDown_sbit_preserved, hg₀_def, h_same_sign]
        cases hyn : y.negative_ <;> rfl
      exact operator_add_post_alignment_anyMode x.negative_ x.mantissa_ ym_a e_common g_aln
        f_aln hf_aln_rep' h_g_aln_sbit hx_mant_bound hym_a_lt hsum_ge
        (by rw [he_common_eq]; exact hxe_min)
        (by rw [he_common_eq]; exact hxe_max) (x.toRat + y.toRat) htruth_eq
        result hok_post
    · push_neg at h_xe_gt_ye
      have h_xe_eq_ye : x.exponent_ = y.exponent_ := le_antisymm h_xe_gt_ye h_xe_lt_ye
      rw [if_neg (not_lt.mpr h_xe_lt_ye), if_neg (not_lt.mpr h_xe_gt_ye)] at hok
      set xm_a : UInt64 := x.mantissa_ with hxm_a_def
      set ym_a : UInt64 := y.mantissa_ with hym_a_def
      set e_common : Int := x.exponent_ with he_common_def
      -- Bug-1 fix: the equal-exponent branch now seeds a sign-aware guard.
      set g_aln : Guard := if x.negative_ then Guard.new.set_negative else Guard.new
        with hg_aln_def
      set f_aln : ℚ := 0 with hf_aln_def
      have hf_aln_rep' : represents g_aln f_aln := by
        rw [hg_aln_def, hf_aln_def]; exact represents_initial_guard_eq x.negative_
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
                         if zm128 > toUInt128 largeRange.max || zm128 > toUInt128 maxRepUp then
                           (toUInt64 (g_aln.doDropDigit128 zm128 e_common).2.1,
                            (g_aln.doDropDigit128 zm128 e_common).2.2,
                            (g_aln.doDropDigit128 zm128 e_common).1)
                         else (toUInt64 zm128, e_common, g_aln)
                       match p.2.2.doRoundUp x.negative_ p.1 p.2.1 largeRange.min largeRange.max
                             mode "Number::addition overflow" with
                       | .error err => Except.error err
                       | .ok res => res.toNumber.normalize largeRange.min largeRange.max mode)
                      = .ok result := by
        have h := hok
        simp only [h_xneqyn, if_true] at h
        exact h
      have hxm_a_lt : xm_a.toNat < 10 ^ 19 := by rw [hxm_a_def]; exact hx_mant_bound
      have hym_a_lt : ym_a.toNat < 10 ^ 19 := by rw [hym_a_def]; exact hy_mant_bound
      have h_g_aln_sbit : g_aln.sbit_ = x.negative_ := by
        rw [hg_aln_def]
        cases hxn : x.negative_ <;> rfl
      exact operator_add_post_alignment_anyMode x.negative_ xm_a ym_a e_common g_aln
        f_aln hf_aln_rep' h_g_aln_sbit hxm_a_lt hym_a_lt hsum_ge
        (by rw [he_common_def]; exact hxe_min)
        (by rw [he_common_def]; exact hxe_max) (x.toRat + y.toRat) htruth_eq
        result hok_post

end XRPL.Model.Protocol
