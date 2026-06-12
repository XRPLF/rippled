import XRPL.Properties.Protocol.Number.Common.Notation
import XRPL.Properties.Protocol.Number.Add.Common
import XRPL.Properties.Protocol.Number.Common.Helpers
import XRPL.Properties.Protocol.Number.Rounding.DoRoundUp

set_option linter.style.longLine false
set_option linter.style.emptyLine false

namespace XRPL.Model.Protocol

/-! # Algorithmic decomposition for `operator_add` — same-sign branch (to_nearest) -/

/-- For same-sign Numbers, `|x.toRat + y.toRat| = |x.toRat| + |y.toRat|`. -/
lemma abs_add_eq_of_same_sign {x y : Number} (h : x.negative_ = y.negative_) :
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
lemma represents_initial_guard_eq (xn : Bool) :
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
lemma alignDown_abs_value
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

/-- Helper: post-alignment processing for the same-sign branch.
After the alignment phase, the state is `(xm_a, e_common, ym_a, g_aln)` with `f_aln`
representing the guard. This lemma packages the conditional drop + doRoundUp + normalize.
-/
theorem operator_add_post_alignment
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
                  .to_nearest "Number::addition overflow" with
            | .error err => Except.error err
            | .ok res => res.toNumber.normalize largeRange.min largeRange.max .to_nearest)
           = .ok result) :
    ∃ (zm : UInt64) (ze' : Int) (f : ℚ) (g : Guard) (res_pos : RoundResult),
      mantissaFloor ≤ zm.toNat ∧
      zm.toNat ≤ maxRep.toNat ∧
      0 ≤ f ∧ f < 1 ∧
      (zm.toNat = mantissaFloor → (8 : ℚ) / 10 ≤ f) ∧
      |truth| = ((zm.toNat : ℚ) + f) * 10 ^ ze' ∧
      g.doRoundUp false zm ze' largeRange.min largeRange.max .to_nearest "Number::addition overflow" = .ok res_pos ∧
      |result.toRat| = (res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_ ∧
      res_pos.mantissa_ ≠ 0 ∧
      represents g f ∧
      result.negative_ = xn := by
  have hf_aln_nn : 0 ≤ f_aln := represents_nonneg hrep_aln
  have hf_aln_lt : f_aln < 1 := represents_lt_one hrep_aln
  -- UInt128 sum doesn't overflow
  have hxm_lt' : xm_a.toNat < 10 ^ 19 := hxm_lt
  have hym_lt' : ym_a.toNat < 10 ^ 19 := hym_lt
  -- Bounds for toUInt128
  have hxm_tu : (toUInt128 xm_a).toNat = xm_a.toNat := toNat_toUInt128 xm_a
  have hym_tu : (toUInt128 ym_a).toNat = ym_a.toNat := toNat_toUInt128 ym_a
  have hsum_lt : xm_a.toNat + ym_a.toNat < 2 * 10 ^ 19 := by omega
  have hsum_lt128 : xm_a.toNat + ym_a.toNat < 2 ^ 128 := by
    have : 2 * 10 ^ 19 < 2 ^ 128 := by norm_num
    omega
  -- Sum value
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
  -- The OR condition simplifies: largeRange.max > maxRep so the binding constraint is zm128 > maxRep
  -- when zm128 > maxRep but zm128 ≤ largeRange.max, only second disjunct fires
  -- when zm128 > largeRange.max, both fire
  -- Either way: cond ↔ zm128 > maxRep
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
  -- Now branch on whether the drop fires
  by_cases h_drop : zm128.toNat > maxRep.toNat
  · -- Drop case
    have h_cond_true : (zm128 > toUInt128 largeRange.max
        || zm128 > toUInt128 maxRep) = true := by
      rw [h_cond_iff_sum_gt_maxRep]; exact decide_eq_true h_drop
    -- The dropped digit
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
    -- After drop
    set zm_new : UInt64 := toUInt64 (zm128 / 10) with hzm_new_def
    have h_div_fit : (zm128 / 10).toNat < 2 ^ 64 := by
      rw [h_zm_div_eq]; rw [hzm128_toNat]
      have : (xm_a.toNat + ym_a.toNat) / 10 < 2 * 10 ^ 18 := by omega
      have : 2 * 10 ^ 18 < 2 ^ 64 := by norm_num
      omega
    have hzm_new_toNat : zm_new.toNat = zm128.toNat / 10 := by
      rw [hzm_new_def, toNat_toUInt64 h_div_fit, h_zm_div_eq]
    -- The drop value bounds
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
    -- New guard
    set g_new : Guard := g_aln.push d with hg_new_def
    have hrep_new : represents g_new ((f_aln + d.toNat) / 10) :=
      represents_push hrep_aln hd_lt
    -- New fraction
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
    -- Value equation: |truth| = (zm_new + f_new) * 10^(e_common+1)
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
    -- Floor constraint
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
    -- Reduce the if in hok: drop fires. Resulting `hok'` is the pipeline at (zm_new, e_common+1, g_new).
    have hok' : (match g_new.doRoundUp xn zm_new (e_common + 1) largeRange.min largeRange.max
                       .to_nearest "Number::addition overflow" with
                 | .error err => Except.error err
                 | .ok res => res.toNumber.normalize largeRange.min largeRange.max .to_nearest)
                = .ok result := by
      have h := hok
      simp only [h_cond_true, if_true] at h
      -- The if branch unfolded; show g_aln.doDropDigit128 ... = (g_new, ..., e_common+1)
      have hddd : g_aln.doDropDigit128 zm128 e_common = (g_new, zm128 / 10, e_common + 1) := by
        unfold Guard.doDropDigit128
        rw [← hd_def, hg_new_def]
      rw [hddd] at h
      simp only at h
      simp only [← hzm_new_def] at h
      exact h
    -- Apply doRoundUp
    have h_rup_exists : ∃ res : RoundResult,
        g_new.doRoundUp xn zm_new (e_common + 1) largeRange.min largeRange.max .to_nearest
          "Number::addition overflow" = .ok res := by
      match hg : g_new.doRoundUp xn zm_new (e_common + 1) largeRange.min largeRange.max .to_nearest "Number::addition overflow" with
      | .error e => simp only [hg, reduceCtorEq] at hok'
      | .ok r => exact ⟨r, rfl⟩
    obtain ⟨res, h_rup⟩ := h_rup_exists
    rw [h_rup] at hok'
    have hres_mant_ne : res.mantissa_ ≠ 0 :=
      Number.normalize_mantissa_ne_zero_of_result hok' hresult
    have h_inv := doRoundUp_output_invariants g_new xn zm_new (e_common + 1)
      hzm_new_ge_floor hzm_new_le_maxRep "Number::addition overflow" res h_rup hres_mant_ne
    obtain ⟨h_res_min, h_res_max, h_res_exp, h_res_mod⟩ := h_inv
    -- Pos version
    set res_pos : RoundResult := { negative_ := false, mantissa_ := res.mantissa_, exponent_ := res.exponent_ }
    have hres_pos_mant_ne : res_pos.mantissa_ ≠ 0 := hres_mant_ne
    have h_rup_pos : g_new.doRoundUp false zm_new (e_common + 1) largeRange.min largeRange.max .to_nearest "Number::addition overflow" = .ok res_pos :=
      doRoundUp_false_from_ok g_new xn zm_new (e_common + 1) .to_nearest "Number::addition overflow" res h_rup
    have h_result_eq_res : result = res.toNumber :=
      Number.normalize_eq_of_invariants h_res_min h_res_max h_res_exp h_res_mod hok'
    have h_result_abs : |result.toRat| = (res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_ := by
      rw [h_result_eq_res, abs_toRat_eq res.toNumber]; rfl
    have h_res_neg_eq_xn : result.negative_ = xn := by
      rw [h_result_eq_res]
      exact doRoundUp_negative_of_mant_ne g_new xn zm_new (e_common + 1) _ _ _ "Number::addition overflow" res h_rup hres_mant_ne
    refine ⟨zm_new, e_common + 1, f_new, g_new, res_pos, hzm_new_ge_floor, hzm_new_le_maxRep,
            hf_new_nn, hf_new_lt, h_floor_constraint, h_value_eq, h_rup_pos, h_result_abs,
            hres_pos_mant_ne, hrep_new, h_res_neg_eq_xn⟩
  · -- No drop case: zm128 ≤ maxRep
    push_neg at h_drop
    have h_cond_false : (zm128 > toUInt128 largeRange.max
        || zm128 > toUInt128 maxRep) = false := by
      rw [h_cond_iff_sum_gt_maxRep]; exact decide_eq_false (by omega : ¬ zm128.toNat > maxRep.toNat)
    -- Direct conversion
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
    -- Value
    have h_value_eq : |truth| = ((zm_new.toNat : ℚ) + f_aln) * 10 ^ e_common := by
      rw [htruth_eq, hzm_new_toNat, hzm128_toNat]
      push_cast; ring
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
    -- Reduce the if (no drop). hok' is the pipeline at (zm_new, e_common, g_aln).
    have hok' : (match g_aln.doRoundUp xn zm_new e_common largeRange.min largeRange.max
                       .to_nearest "Number::addition overflow" with
                 | .error err => Except.error err
                 | .ok res => res.toNumber.normalize largeRange.min largeRange.max .to_nearest)
                = .ok result := by
      have h := hok
      simp only [h_cond_false, Bool.false_eq_true, if_false] at h
      simp only [← hzm_new_def] at h
      exact h
    -- Apply doRoundUp
    have h_rup_exists : ∃ res : RoundResult,
        g_aln.doRoundUp xn zm_new e_common largeRange.min largeRange.max .to_nearest
          "Number::addition overflow" = .ok res := by
      match hg : g_aln.doRoundUp xn zm_new e_common largeRange.min largeRange.max .to_nearest "Number::addition overflow" with
      | .error e => simp only [hg, reduceCtorEq] at hok'
      | .ok r => exact ⟨r, rfl⟩
    obtain ⟨res, h_rup⟩ := h_rup_exists
    rw [h_rup] at hok'
    have hres_mant_ne : res.mantissa_ ≠ 0 :=
      Number.normalize_mantissa_ne_zero_of_result hok' hresult
    have h_inv := doRoundUp_output_invariants g_aln xn zm_new e_common
      hzm_new_ge_floor hzm_new_le_maxRep "Number::addition overflow" res h_rup hres_mant_ne
    obtain ⟨h_res_min, h_res_max, h_res_exp, h_res_mod⟩ := h_inv
    set res_pos : RoundResult := { negative_ := false, mantissa_ := res.mantissa_, exponent_ := res.exponent_ }
    have hres_pos_mant_ne : res_pos.mantissa_ ≠ 0 := hres_mant_ne
    have h_rup_pos : g_aln.doRoundUp false zm_new e_common largeRange.min largeRange.max .to_nearest "Number::addition overflow" = .ok res_pos :=
      doRoundUp_false_from_ok g_aln xn zm_new e_common .to_nearest "Number::addition overflow" res h_rup
    have h_result_eq_res : result = res.toNumber :=
      Number.normalize_eq_of_invariants h_res_min h_res_max h_res_exp h_res_mod hok'
    have h_result_abs : |result.toRat| = (res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_ := by
      rw [h_result_eq_res, abs_toRat_eq res.toNumber]; rfl
    have h_res_neg_eq_xn : result.negative_ = xn := by
      rw [h_result_eq_res]
      exact doRoundUp_negative_of_mant_ne g_aln xn zm_new e_common _ _ _ "Number::addition overflow" res h_rup hres_mant_ne
    refine ⟨zm_new, e_common, f_aln, g_aln, res_pos, hzm_new_ge_floor, hzm_new_le_maxRep,
            hf_aln_nn, hf_aln_lt, h_floor_constraint, h_value_eq, h_rup_pos, h_result_abs,
            hres_pos_mant_ne, hrep_aln, h_res_neg_eq_xn⟩


end XRPL.Model.Protocol
