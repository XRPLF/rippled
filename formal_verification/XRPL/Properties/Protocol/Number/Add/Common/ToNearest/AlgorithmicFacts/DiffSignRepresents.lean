import XRPL.Properties.Protocol.Number.Common.Notation
import XRPL.Properties.Protocol.Number.Add.Common.Decompose
import XRPL.Properties.Protocol.Number.Add.Common.ToNearest.AlgorithmicFacts.DiffSignStructural
import XRPL.Properties.Protocol.Number.Common.Helpers
import XRPL.Properties.Protocol.Number.Common.Rounding.DoRoundUp
import XRPL.Properties.Protocol.Number.Common.Rounding.Normalize


namespace XRPL.Model.Protocol

/-- The model's recover upper limit, in ℕ: `10^21`. -/
private lemma diffSign_UL_val : (toUInt128 largeRange.min * 1000).toNat = 10 ^ 21 := by
  have h1000n : (1000 : UInt64).toNat = 1000 := rfl
  rw [show (1000 : UInt128) = toUInt128 (1000 : UInt64) from rfl,
      uint128_of_uint64_mul_toNat largeRange.min 1000
        (by rw [largeRange_min_toNat, h1000n]; norm_num),
      largeRange_min_toNat, h1000n]
  norm_num

private lemma diffSign_UL_bound : (toUInt128 largeRange.min * 1000).toNat * 10 < 2 ^ 128 := by
  rw [diffSign_UL_val]; norm_num

set_option maxHeartbeats 3200000 in
-- Three-leg recover analysis over large UInt128 terms needs a raised budget.
/-- Post-subtract processing for the diff-sign branch -/
theorem operator_add_post_subtract
    (zn : Bool) (zm_pre : UInt128) (E : Int) (g_aln : Guard) (f_aln : ℚ)
    (mode : rounding_mode)
    (hrep : represents g_aln f_aln)
    (hzm_pos : 1 ≤ zm_pre.toNat)
    (hzm_lt : zm_pre.toNat < 10 ^ 20)
    (hxbit_big : g_aln.xbit_ = true → 10 ^ 18 - 101 ≤ zm_pre.toNat)
    (truth : ℚ)
    (htruth : |truth| = ((zm_pre.toNat : ℚ) - f_aln) * 10 ^ E)
    (result : Number)
    (hok : doNormalize128 zn
        (if (Number.operator_add.recover (toUInt128 largeRange.min * 1000) zm_pre E g_aln 40).2.2.empty = true
          then (Number.operator_add.recover (toUInt128 largeRange.min * 1000) zm_pre E g_aln 40).1
          else (Number.operator_add.recover (toUInt128 largeRange.min * 1000) zm_pre E g_aln 40).1 - 1)
        (Number.operator_add.recover (toUInt128 largeRange.min * 1000) zm_pre E g_aln 40).2.1
        largeRange.min largeRange.max mode
        (!(Number.operator_add.recover (toUInt128 largeRange.min * 1000) zm_pre E g_aln 40).2.2.empty)
        = .ok result) :
    ∃ (M : UInt128) (ze' : Int) (δ : ℚ) (sticky : Bool),
      (0 ≤ δ) ∧ δ ≤ 1 ∧
      (sticky = false → δ = 0) ∧
      1 ≤ M.toNat ∧ M.toNat < 10 ^ 22 ∧
      (sticky = true → 10 ^ 20 ≤ M.toNat) ∧
      |truth| = ((M.toNat : ℚ) + δ) * 10 ^ ze' ∧
      doNormalize128 zn M ze' largeRange.min largeRange.max mode sticky = .ok result ∧
      δ < 1 ∧
      (sticky = true → 0 < δ) := by
  have hUL := diffSign_UL_bound
  have hall : allNibblesAtMost9 g_aln.digits_ := by
    obtain ⟨_, _, _, _, _, h⟩ := hrep; exact h
  have hf_nn : 0 ≤ f_aln := represents_nonneg hrep
  have hf_lt : f_aln < 1 := represents_lt_one hrep
  set R := Number.operator_add.recover (toUInt128 largeRange.min * 1000) zm_pre E g_aln 40
    with hR_def
  have hzm_pos_q : (1 : ℚ) ≤ (zm_pre.toNat : ℚ) := by exact_mod_cast hzm_pos
  have h10E_pos : (0 : ℚ) < (10 : ℚ) ^ E := zpow_pos (by norm_num) _
  -- The recover output stays below 10^22.
  have hupper : R.1.toNat < 10 ^ 22 := by
    have h := recover_mantissa_lt (toUInt128 largeRange.min * 1000) zm_pre hUL E g_aln 40
      hzm_pos hall (by rw [diffSign_UL_val]; omega)
    rw [diffSign_UL_val] at h
    calc R.1.toNat < 10 ^ 21 * 10 := h
      _ = 10 ^ 22 := by norm_num
  -- The exit condition holds at the output.
  have hexit := recover_40_exits zm_pre E g_aln hzm_pos hall
  have hcases := recover_exit_cases hexit
  by_cases hemp : R.2.2.empty = true
  · -- EMPTY leg: the value is exact.
    have hexact := recover_exact_of_empty (toUInt128 largeRange.min * 1000) zm_pre hUL
      E g_aln 40 hzm_pos hrep hemp
    rw [← hR_def] at hexact
    -- The output mantissa is positive (the value is positive).
    have hL_pos : (0 : ℚ) < ((zm_pre.toNat : ℚ) - f_aln) * 10 ^ E := by
      apply mul_pos _ h10E_pos
      linarith
    rw [hexact] at hL_pos
    have hR1_pos : 1 ≤ R.1.toNat := by
      by_contra h0
      push_neg at h0
      have h : R.1.toNat = 0 := by omega
      rw [h] at hL_pos
      norm_num at hL_pos
    refine ⟨R.1, R.2.1, 0, false, by norm_num, by norm_num, fun _ => rfl,
      hR1_pos, hupper, fun h => Bool.noConfusion h, ?_, ?_, by norm_num,
      fun h => Bool.noConfusion h⟩
    · rw [htruth, hexact, add_zero]
    · rw [if_pos hemp, hemp] at hok
      simpa using hok
  · -- NON-EMPTY legs: the mantissa is huge; borrow and pass the sticky tail.
    have hUL_le : (toUInt128 largeRange.min * 1000).toNat ≤ R.1.toNat :=
      hcases.resolve_right hemp
    rw [diffSign_UL_val] at hUL_le
    have hR1_ge1 : (1 : UInt128) ≤ R.1 := by
      rw [BitVec.le_def]
      have h1 : ((1 : UInt128)).toNat = 1 := by decide
      rw [h1]; omega
    have hM_toNat : (R.1 - 1).toNat = R.1.toNat - 1 := by
      rw [BitVec.toNat_sub_of_le hR1_ge1]
      have h1 : ((1 : UInt128)).toNat = 1 := by decide
      rw [h1]
    have hM_cast : ((R.1 - 1).toNat : ℚ) = (R.1.toNat : ℚ) - 1 := by
      rw [hM_toNat, Nat.cast_sub (by omega : 1 ≤ R.1.toNat)]
      norm_num
    -- The residue: tight on both legs (the xbit-true leg via the ≤4-pop
    -- digit-exactness bound), and strictly positive (the exit guard is
    -- non-empty, so its residue cannot vanish).
    have hresidue : ∃ f' : ℚ, 0 ≤ f' ∧ f' < 1 ∧ 0 < f' ∧
        ((zm_pre.toNat : ℚ) - f_aln) * 10 ^ E
          = ((R.1.toNat : ℚ) - f') * 10 ^ R.2.1 := by
      by_cases hxbit : g_aln.xbit_ = true
      · -- xbit leg: the 4-pop bound from the coupled magnitude makes the
        -- digit-exactness conjunct applicable.
        have hzm_big := hxbit_big hxbit
        obtain ⟨f', k, hk_le, hk_eq, hf'_nn, hf'_slack, hstrict, hf'_pos, hval, _⟩ :=
          recover_value_in_unit_interval_at_exit (toUInt128 largeRange.min * 1000) zm_pre hUL
            E g_aln f_aln 40 hf_nn hf_lt hrep hzm_pos
        rw [← hR_def] at hk_eq hval
        -- Extract the per-pop factor: (zm_pre - f_aln) * 10^k = R.1 - f'.
        have hk_pow : (10 : ℚ) ^ E = (10 : ℚ) ^ (E - (k : ℤ)) * (10 : ℚ) ^ (k : ℕ) := by
          rw [← zpow_natCast (10 : ℚ) k, ← zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0)]
          congr 1; ring
        have hcore : ((zm_pre.toNat : ℚ) - f_aln) * (10 : ℚ) ^ (k : ℕ)
            = (R.1.toNat : ℚ) - f' := by
          have hpow_ne : ((10 : ℚ) ^ (E - (k : ℤ))) ≠ 0 :=
            ne_of_gt (zpow_pos (by norm_num) _)
          have h := hval
          rw [hk_eq, hk_pow] at h
          field_simp at h
          -- field_simp cancels the common 10^(E-k) factor
          exact h
        -- k ≤ 4 from the magnitudes.
        have hZ_low : (10 : ℚ) ^ 18 - 102 ≤ (zm_pre.toNat : ℚ) - f_aln := by
          have : ((10 : ℕ) ^ 18 - 101 : ℕ) ≤ zm_pre.toNat := hzm_big
          have hq : (((10 : ℕ) ^ 18 - 101 : ℕ) : ℚ) ≤ (zm_pre.toNat : ℚ) := by
            exact_mod_cast this
          have hcast : (((10 : ℕ) ^ 18 - 101 : ℕ) : ℚ) = (10 : ℚ) ^ 18 - 101 := by
            rw [Nat.cast_sub (by norm_num)]
            push_cast; norm_num
          rw [hcast] at hq
          linarith
        have hR_hi : (R.1.toNat : ℚ) - f' ≤ (10 : ℚ) ^ 22 := by
          have hq : (R.1.toNat : ℚ) ≤ ((10 : ℕ) ^ 22 : ℕ) := by
            exact_mod_cast le_of_lt hupper
          have : (((10 : ℕ) ^ 22 : ℕ) : ℚ) = (10 : ℚ) ^ 22 := by push_cast; norm_num
          linarith [this ▸ hq, hf'_nn]
        have hk4 : k ≤ 4 := by
          by_contra hk
          push_neg at hk
          have h5 : (10 : ℚ) ^ 5 ≤ (10 : ℚ) ^ (k : ℕ) :=
            pow_le_pow_right₀ (by norm_num) hk
          nlinarith [hcore, hZ_low, hR_hi, h5]
        exact ⟨f', hf'_nn, hstrict (by omega), hf'_pos hxbit, hval⟩
      · -- xbit-false leg: the tight chain; positivity from the non-empty exit
        -- guard (xbit stays false, so a nonzero digit survives).
        have hdich : g_aln.xbit_ = true →
            ¬ (zm_pre < toUInt128 largeRange.min * 1000 ∧ ¬ g_aln.empty) :=
          fun hxt => absurd hxt hxbit
        obtain ⟨f', hrep', hval⟩ :=
          recover_preserves_represents (toUInt128 largeRange.min * 1000) zm_pre hUL
            E g_aln 40 hzm_pos hrep hdich
        have hxbit_exit : R.2.2.xbit_ = false := by
          have h := recover_xbit (toUInt128 largeRange.min * 1000) zm_pre E g_aln 40
          rw [← hR_def] at h
          rw [h]
          exact Bool.not_eq_true _ |>.mp hxbit
        have hdig_ne : R.2.2.digits_ ≠ 0 := by
          intro hd0
          apply hemp
          unfold Guard.empty
          rw [hd0, hxbit_exit]
          decide
        have hf'_pos : 0 < f' := by
          obtain ⟨x', hx'_nn, _, hf'_eq, _, _⟩ := hrep'
          have hdv_pos : 0 < decimalValue R.2.2.digits_ :=
            decimalValue_pos_of_ne_zero _ hdig_ne
          have hdv_q : (0 : ℚ) < (decimalValue R.2.2.digits_ : ℚ) / 10 ^ 16 := by
            apply div_pos _ (by positivity)
            exact_mod_cast hdv_pos
          rw [hf'_eq]
          linarith
        exact ⟨f', represents_nonneg hrep', represents_lt_one hrep', hf'_pos, hval⟩
    obtain ⟨f', hf'_nn, hf'_lt, hf'_pos, hval⟩ := hresidue
    refine ⟨R.1 - 1, R.2.1, 1 - f', true, by linarith, by linarith,
      fun h => Bool.noConfusion h, ?_, ?_, fun _ => ?_, ?_, ?_, by linarith,
      fun _ => by linarith⟩
    · rw [hM_toNat]; omega
    · rw [hM_toNat]; omega
    · rw [hM_toNat]
      have : (10 : ℕ) ^ 21 - 1 ≥ 10 ^ 20 := by norm_num
      omega
    · rw [htruth, hval, hM_cast]
      ring
    · have hempf : R.2.2.empty = false := by
        rw [Bool.not_eq_true] at hemp; exact hemp
      rw [if_neg hemp, hempf] at hok
      simpa using hok

/-- For diff-sign Numbers with `|x.toRat| < |y.toRat|`, the sum `x.toRat + y.toRat`
takes the sign of the dominant operand `y`. -/
lemma diff_sign_sum_sign_of_dominant {x y : Number}
    (h_diff_xy : x.negative_ ≠ y.negative_)
    (h_dom : |x.toRat| < |y.toRat|) :
    (y.negative_ = true → x.toRat + y.toRat ≤ 0) ∧
    (y.negative_ = false → 0 ≤ x.toRat + y.toRat) := by
  constructor
  · intro hyn
    have hxn : x.negative_ = false := by
      cases hh : x.negative_
      · rfl
      · rw [hh, hyn] at h_diff_xy; exact absurd rfl h_diff_xy
    have hx_nn : 0 ≤ x.toRat := Number.toRat_nonneg_of_nonnegative x hxn
    have hy_np : y.toRat ≤ 0 := Number.toRat_nonpos_of_negative y hyn
    rw [abs_of_nonneg hx_nn, abs_of_nonpos hy_np] at h_dom
    linarith
  · intro hyn
    have hxn : x.negative_ = true := by
      cases hh : x.negative_
      · rw [hh, hyn] at h_diff_xy; exact absurd rfl h_diff_xy
      · rfl
    have hx_np : x.toRat ≤ 0 := Number.toRat_nonpos_of_negative x hxn
    have hy_nn : 0 ≤ y.toRat := Number.toRat_nonneg_of_nonnegative y hyn
    rw [abs_of_nonpos hx_np, abs_of_nonneg hy_nn] at h_dom
    linarith

set_option maxHeartbeats 8000000 in
-- 8-way diff-sign case analysis; large elaboration requires a raised heartbeat limit
theorem operator_add_algorithmic_facts_diff_sign_represents
    (x y result : Number) (mode : rounding_mode)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (h_diff_sign : x.negative_ ≠ y.negative_)
    (h_not_zero : ¬ x.operator_eq y.operator_neg)
    (hok : Number.operator_add x y mode = .ok result) :
    ∃ (M : UInt128) (ze' : Int) (δ : ℚ) (zn sticky : Bool),
      (0 ≤ δ) ∧ δ ≤ 1 ∧
      (sticky = false → δ = 0) ∧
      1 ≤ M.toNat ∧ M.toNat < 10 ^ 22 ∧
      (sticky = true → 10 ^ 20 ≤ M.toNat) ∧
      |x.toRat + y.toRat| = ((M.toNat : ℚ) + δ) * 10 ^ ze' ∧
      doNormalize128 zn M ze' largeRange.min largeRange.max mode sticky = .ok result ∧
      ((zn = true → x.toRat + y.toRat ≤ 0) ∧ (zn = false → 0 ≤ x.toRat + y.toRat)) ∧
      δ < 1 ∧
      (sticky = true → 0 < δ) := by
  have hx_bounds := hx.mantissaBounds hx_mant_ne
  have hy_bounds := hy.mantissaBounds hy_mant_ne
  have hx_min : (10 : ℕ) ^ 18 ≤ x.mantissa_.toNat := (mantissaBounds_nat_of hx_bounds).1
  have hy_min : (10 : ℕ) ^ 18 ≤ y.mantissa_.toNat := (mantissaBounds_nat_of hy_bounds).1
  have hx_mant_bound : x.mantissa_.toNat < 10 ^ 19 := (mantissaBounds_nat_of hx_bounds).2
  have hy_mant_bound : y.mantissa_.toNat < 10 ^ 19 := (mantissaBounds_nat_of hy_bounds).2
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
  have h_xneqyn : (x.negative_ == y.negative_) = false := by
    rw [beq_eq_false_iff_ne]; exact h_diff_sign
  have h_abs_add := abs_add_eq_of_diff_sign h_diff_sign
  unfold Number.operator_add at hok
  simp only [hy_ne_zero, hx_ne_zero, h_not_zero, Bool.false_eq_true, if_false] at hok
  rw [h_xneqyn] at hok
  simp only [Bool.false_eq_true, if_false] at hok
  by_cases h_xe_lt_ye : x.exponent_ < y.exponent_
  · -- Case 1: xe < ye, align x to ye. Aligned operand = x (smaller magnitude).
    rw [if_pos h_xe_lt_ye] at hok
    simp only at hok
    set g₀ : Guard := if x.negative_ then Guard.new.set_negative else Guard.new with hg₀_def
    have hg₀_rep : represents g₀ 0 := represents_initial_guard_eq x.negative_
    set aln_result : UInt64 × Int × Guard :=
      Number.operator_add.alignDown x.mantissa_ x.exponent_ g₀ y.exponent_ with haln_def
    set xm_a : UInt64 := aln_result.1 with hxm_a_def
    set g_aln : Guard := aln_result.2.2 with hg_aln_def
    have he_common_eq : aln_result.2.1 = y.exponent_ := by
      rw [haln_def]
      have := alignDown_e_eq x.mantissa_ x.exponent_ g₀ y.exponent_
      rw [this]
      exact max_eq_right (le_of_lt h_xe_lt_ye)
    rw [he_common_eq] at hok
    set K : ℕ := (max x.exponent_ y.exponent_ - x.exponent_).toNat with hK_def
    have hmax_eq : max x.exponent_ y.exponent_ = y.exponent_ :=
      max_eq_right (le_of_lt h_xe_lt_ye)
    -- Aligned mantissa is strictly smaller than the other operand: xm_a < 10^18 ≤ y.mantissa_.
    have hK_pos : 1 ≤ K := by
      rw [hK_def, hmax_eq]
      have : x.exponent_ < y.exponent_ := h_xe_lt_ye
      omega
    have hxm_a_eq : xm_a.toNat = x.mantissa_.toNat / 10 ^ K := by
      rw [hxm_a_def, haln_def, alignDown_mantissa_eq, ← hK_def]
    have hxm_a_small : xm_a.toNat < 10 ^ 18 := by
      rw [hxm_a_eq]
      have hpow_ge : (10 : ℕ) ^ 1 ≤ 10 ^ K := Nat.pow_le_pow_right (by norm_num) hK_pos
      have hstep : x.mantissa_.toNat / 10 ^ K ≤ x.mantissa_.toNat / 10 ^ 1 :=
        Nat.div_le_div_left hpow_ge (by positivity)
      have : x.mantissa_.toNat / 10 ^ 1 < 10 ^ 18 := by
        have h1019 : (10 : ℕ) ^ 19 = 10 ^ 18 * 10 := by norm_num
        rw [h1019] at hx_mant_bound; omega
      omega
    have hxm_a_lt_ym : xm_a.toNat < y.mantissa_.toNat := by omega
    set f_aln : ℚ := (0 + ((x.mantissa_.toNat % 10 ^ K : ℕ) : ℚ)) / 10 ^ K with hf_aln_def
    have hf_aln_rep : represents g_aln f_aln := by
      have h := alignDown_represents x.mantissa_ x.exponent_ g₀ y.exponent_ 0 hg₀_rep
      have hK_eq : (max x.exponent_ y.exponent_ - x.exponent_).toNat = K := by rw [hK_def]
      rw [hK_eq] at h
      exact h
    have hf_aln_nn : 0 ≤ f_aln := represents_nonneg hf_aln_rep
    have hf_aln_lt : f_aln < 1 := represents_lt_one hf_aln_rep
    have h_x_abs_pre : |x.toRat| = ((xm_a.toNat : ℚ) + f_aln) * 10 ^ y.exponent_ := by
      have h_lemma := alignDown_abs_value x g₀ y.exponent_ (le_of_lt h_xe_lt_ye) 0
      simp only at h_lemma
      have hK_eq : (y.exponent_ - x.exponent_).toNat = K := by
        rw [hK_def, hmax_eq]
      rw [hK_eq] at h_lemma
      have : |x.toRat| + 0 * 10 ^ x.exponent_ = ((xm_a.toNat : ℚ) + f_aln) * 10 ^ y.exponent_ := by
        rw [hf_aln_def]; rw [hxm_a_def, haln_def] at *
        convert h_lemma using 2
      linarith
    have h_y_abs : |y.toRat| = (y.mantissa_.toNat : ℚ) * 10 ^ y.exponent_ := abs_toRat_eq y
    -- Only the "− f" sub-case is reachable. The other branches need ¬(xm_a > y.mantissa_).
    -- zm_pre = toUInt128 y.mantissa_ - toUInt128 xm_a.
    set zm_pre : UInt128 := toUInt128 y.mantissa_ - toUInt128 xm_a with hzm_pre_def
    have h_ym_ge_xm : toUInt128 xm_a ≤ toUInt128 y.mantissa_ := by
      rw [BitVec.le_def, toNat_toUInt128, toNat_toUInt128]
      exact Nat.le_of_lt hxm_a_lt_ym
    have hzm_pre_toNat : zm_pre.toNat = y.mantissa_.toNat - xm_a.toNat := by
      rw [hzm_pre_def, BitVec.toNat_sub_of_le h_ym_ge_xm, toNat_toUInt128, toNat_toUInt128]
    have hzm_pre_pos : 1 ≤ zm_pre.toNat := by rw [hzm_pre_toNat]; omega
    have h_truth_eq_minus : |x.toRat + y.toRat| =
        ((zm_pre.toNat : ℚ) - f_aln) * 10 ^ y.exponent_ := by
      rw [h_abs_add, h_x_abs_pre, h_y_abs]
      have h_diff_neg : ((xm_a.toNat : ℚ) + f_aln) - (y.mantissa_.toNat : ℚ) ≤ 0 := by
        have hge : (1 : ℚ) ≤ (y.mantissa_.toNat : ℚ) - (xm_a.toNat : ℚ) := by
          have h1 : ((1 : ℕ) : ℚ) ≤ ((y.mantissa_.toNat - xm_a.toNat : ℕ) : ℚ) := by
            exact_mod_cast (by omega : 1 ≤ y.mantissa_.toNat - xm_a.toNat)
          rw [Nat.cast_sub (Nat.le_of_lt hxm_a_lt_ym)] at h1
          push_cast at h1; linarith
        linarith
      rw [show ((xm_a.toNat : ℚ) + f_aln) * 10 ^ y.exponent_
               - (y.mantissa_.toNat : ℚ) * 10 ^ y.exponent_
             = (((xm_a.toNat : ℚ) + f_aln) - (y.mantissa_.toNat : ℚ)) * 10 ^ y.exponent_
           from by ring]
      have h_pow_pos : (0 : ℚ) < 10 ^ y.exponent_ := by positivity
      have h_prod_np : (((xm_a.toNat : ℚ) + f_aln) - (y.mantissa_.toNat : ℚ)) * 10 ^ y.exponent_ ≤ 0 :=
        mul_nonpos_of_nonpos_of_nonneg h_diff_neg (le_of_lt h_pow_pos)
      rw [abs_of_nonpos h_prod_np]
      rw [hzm_pre_toNat]
      have hsub_q : ((y.mantissa_.toNat - xm_a.toNat : ℕ) : ℚ)
          = (y.mantissa_.toNat : ℚ) - (xm_a.toNat : ℚ) := by
        rw [Nat.cast_sub (Nat.le_of_lt hxm_a_lt_ym)]
      rw [hsub_q]; ring
    -- Helper inputs: magnitude bound and the xbit ⇒ huge-mantissa coupling.
    have hzm_pre_lt : zm_pre.toNat < 10 ^ 20 := by rw [hzm_pre_toNat]; omega
    have hxbit_big : g_aln.xbit_ = true → 10 ^ 18 - 101 ≤ zm_pre.toNat := by
      intro hxbit
      have hxbit' : (Number.operator_add.alignDown x.mantissa_ x.exponent_ g₀
          y.exponent_).2.2.xbit_ = true := by
        rw [← haln_def, ← hg_aln_def]; exact hxbit
      have h99 := alignDown_mantissa_le_99_of_xbit x.mantissa_ x.exponent_ g₀ y.exponent_
        hg₀_rep hx_mant_bound hxbit'
      rw [← haln_def, ← hxm_a_def] at h99
      rw [hzm_pre_toNat]
      omega
    have h_not_xm_a_gt : ¬ xm_a > y.mantissa_ := by
      intro h
      have hh : y.mantissa_.toNat < xm_a.toNat := UInt64.lt_iff_toNat_lt.mp h
      omega
    have h_dom : |x.toRat| < |y.toRat| := by
      rw [h_x_abs_pre, h_y_abs]
      have h_pow_pos : (0 : ℚ) < 10 ^ y.exponent_ := by positivity
      have hle : ((xm_a.toNat : ℚ) + 1) ≤ (y.mantissa_.toNat : ℚ) := by
        have : (xm_a.toNat + 1 : ℕ) ≤ y.mantissa_.toNat := by omega
        exact_mod_cast this
      have hf1 : f_aln < 1 := hf_aln_lt
      have h_lt : (xm_a.toNat : ℚ) + f_aln < (y.mantissa_.toNat : ℚ) :=
        lt_of_lt_of_le (by linarith only [hf1]) hle
      exact mul_lt_mul_of_pos_right h_lt h_pow_pos
    have h_sign := diff_sign_sum_sign_of_dominant h_diff_sign h_dom
    simp only [if_neg h_not_xm_a_gt] at hok
    obtain ⟨M, ze', δ, sticky, hδ_low, hδ_le, hsticky_zero, hM_pos, hM_lt, hM_big,
        htruth_out, hok_out, hδ_lt, hsticky_pos⟩ :=
      operator_add_post_subtract y.negative_ zm_pre y.exponent_ g_aln f_aln mode
        hf_aln_rep hzm_pre_pos hzm_pre_lt hxbit_big (x.toRat + y.toRat) h_truth_eq_minus
        result hok
    exact ⟨M, ze', δ, y.negative_, sticky, hδ_low, hδ_le, hsticky_zero, hM_pos, hM_lt,
      hM_big, htruth_out, hok_out, h_sign, hδ_lt, hsticky_pos⟩
  · push_neg at h_xe_lt_ye
    by_cases h_xe_gt_ye : x.exponent_ > y.exponent_
    · -- Case 2: xe > ye, align y to xe. Aligned operand = y (smaller magnitude).
      rw [if_neg (not_lt.mpr h_xe_lt_ye), if_pos h_xe_gt_ye] at hok
      simp only at hok
      set g₀ : Guard := if y.negative_ then Guard.new.set_negative else Guard.new with hg₀_def
      have hg₀_rep : represents g₀ 0 := represents_initial_guard_eq y.negative_
      set aln_result : UInt64 × Int × Guard :=
        Number.operator_add.alignDown y.mantissa_ y.exponent_ g₀ x.exponent_ with haln_def
      set ym_a : UInt64 := aln_result.1 with hym_a_def
      set g_aln : Guard := aln_result.2.2 with hg_aln_def
      set K : ℕ := (max y.exponent_ x.exponent_ - y.exponent_).toNat with hK_def
      have hmax_eq : max y.exponent_ x.exponent_ = x.exponent_ :=
        max_eq_right (le_of_lt h_xe_gt_ye)
      have hK_pos : 1 ≤ K := by
        rw [hK_def, hmax_eq]; omega
      have hym_a_eq : ym_a.toNat = y.mantissa_.toNat / 10 ^ K := by
        rw [hym_a_def, haln_def, alignDown_mantissa_eq, ← hK_def]
      have hym_a_small : ym_a.toNat < 10 ^ 18 := by
        rw [hym_a_eq]
        have hpow_ge : (10 : ℕ) ^ 1 ≤ 10 ^ K := Nat.pow_le_pow_right (by norm_num) hK_pos
        have hstep : y.mantissa_.toNat / 10 ^ K ≤ y.mantissa_.toNat / 10 ^ 1 :=
          Nat.div_le_div_left hpow_ge (by positivity)
        have : y.mantissa_.toNat / 10 ^ 1 < 10 ^ 18 := by
          have h1019 : (10 : ℕ) ^ 19 = 10 ^ 18 * 10 := by norm_num
          rw [h1019] at hy_mant_bound; omega
        omega
      have hym_a_lt_xm : ym_a.toNat < x.mantissa_.toNat := by omega
      set f_aln : ℚ := (0 + ((y.mantissa_.toNat % 10 ^ K : ℕ) : ℚ)) / 10 ^ K with hf_aln_def
      have hf_aln_rep : represents g_aln f_aln := by
        have h := alignDown_represents y.mantissa_ y.exponent_ g₀ x.exponent_ 0 hg₀_rep
        have hK_eq : (max y.exponent_ x.exponent_ - y.exponent_).toNat = K := by rw [hK_def]
        rw [hK_eq] at h
        exact h
      have hf_aln_nn : 0 ≤ f_aln := represents_nonneg hf_aln_rep
      have hf_aln_lt : f_aln < 1 := represents_lt_one hf_aln_rep
      have h_y_abs_pre : |y.toRat| = ((ym_a.toNat : ℚ) + f_aln) * 10 ^ x.exponent_ := by
        have h_lemma := alignDown_abs_value y g₀ x.exponent_ (le_of_lt h_xe_gt_ye) 0
        simp only at h_lemma
        have hK_eq : (x.exponent_ - y.exponent_).toNat = K := by
          rw [hK_def, hmax_eq]
        rw [hK_eq] at h_lemma
        have : |y.toRat| + 0 * 10 ^ y.exponent_ = ((ym_a.toNat : ℚ) + f_aln) * 10 ^ x.exponent_ := by
          rw [hf_aln_def]; rw [hym_a_def, haln_def] at *
          convert h_lemma using 2
        linarith
      have h_x_abs : |x.toRat| = (x.mantissa_.toNat : ℚ) * 10 ^ x.exponent_ := abs_toRat_eq x
      -- Only the "− f" sub-case (xm > ym_a) is reachable.
      have h_xm_gt_uint : x.mantissa_ > ym_a := UInt64.lt_iff_toNat_lt.mpr hym_a_lt_xm
      set zm_pre : UInt128 := toUInt128 x.mantissa_ - toUInt128 ym_a with hzm_pre_def
      have h_ym_le_xm : toUInt128 ym_a ≤ toUInt128 x.mantissa_ := by
        rw [BitVec.le_def, toNat_toUInt128, toNat_toUInt128]
        exact Nat.le_of_lt hym_a_lt_xm
      have hzm_pre_toNat : zm_pre.toNat = x.mantissa_.toNat - ym_a.toNat := by
        rw [hzm_pre_def, BitVec.toNat_sub_of_le h_ym_le_xm, toNat_toUInt128, toNat_toUInt128]
      have hzm_pre_pos : 1 ≤ zm_pre.toNat := by rw [hzm_pre_toNat]; omega
      have h_truth_eq_minus : |x.toRat + y.toRat| =
          ((zm_pre.toNat : ℚ) - f_aln) * 10 ^ x.exponent_ := by
        rw [h_abs_add, h_x_abs, h_y_abs_pre]
        have h_diff_pos : (x.mantissa_.toNat : ℚ) - ((ym_a.toNat : ℚ) + f_aln) ≥ 0 := by
          have hge : (1 : ℚ) ≤ (x.mantissa_.toNat : ℚ) - (ym_a.toNat : ℚ) := by
            have h1 : ((1 : ℕ) : ℚ) ≤ ((x.mantissa_.toNat - ym_a.toNat : ℕ) : ℚ) := by
              exact_mod_cast (by omega : 1 ≤ x.mantissa_.toNat - ym_a.toNat)
            rw [Nat.cast_sub (Nat.le_of_lt hym_a_lt_xm)] at h1
            push_cast at h1; linarith
          linarith
        rw [show (x.mantissa_.toNat : ℚ) * 10 ^ x.exponent_
                 - ((ym_a.toNat : ℚ) + f_aln) * 10 ^ x.exponent_
               = ((x.mantissa_.toNat : ℚ) - ((ym_a.toNat : ℚ) + f_aln)) * 10 ^ x.exponent_
             from by ring]
        have h_pow_pos : (0 : ℚ) < 10 ^ x.exponent_ := by positivity
        have h_prod_nn : (0 : ℚ)
            ≤ ((x.mantissa_.toNat : ℚ) - ((ym_a.toNat : ℚ) + f_aln)) * 10 ^ x.exponent_ :=
          mul_nonneg h_diff_pos (le_of_lt h_pow_pos)
        rw [abs_of_nonneg h_prod_nn]
        rw [hzm_pre_toNat]
        have hsub_q : ((x.mantissa_.toNat - ym_a.toNat : ℕ) : ℚ)
            = (x.mantissa_.toNat : ℚ) - (ym_a.toNat : ℚ) := by
          rw [Nat.cast_sub (Nat.le_of_lt hym_a_lt_xm)]
        rw [hsub_q]; ring
      -- Helper inputs: magnitude bound and the xbit ⇒ huge-mantissa coupling.
      have hzm_pre_lt : zm_pre.toNat < 10 ^ 20 := by rw [hzm_pre_toNat]; omega
      have hxbit_big : g_aln.xbit_ = true → 10 ^ 18 - 101 ≤ zm_pre.toNat := by
        intro hxbit
        have hxbit' : (Number.operator_add.alignDown y.mantissa_ y.exponent_ g₀
            x.exponent_).2.2.xbit_ = true := by
          rw [← haln_def, ← hg_aln_def]; exact hxbit
        have h99 := alignDown_mantissa_le_99_of_xbit y.mantissa_ y.exponent_ g₀ x.exponent_
          hg₀_rep hy_mant_bound hxbit'
        rw [← haln_def, ← hym_a_def] at h99
        rw [hzm_pre_toNat]
        omega
      have h_dom : |y.toRat| < |x.toRat| := by
        rw [h_y_abs_pre, h_x_abs]
        have h_pow_pos : (0 : ℚ) < 10 ^ x.exponent_ := by positivity
        have h_lt : (ym_a.toNat : ℚ) + f_aln < (x.mantissa_.toNat : ℚ) := by
          have : ((ym_a.toNat : ℚ) + 1) ≤ (x.mantissa_.toNat : ℚ) := by
            have : (ym_a.toNat + 1 : ℕ) ≤ x.mantissa_.toNat := by omega
            exact_mod_cast this
          linarith
        exact mul_lt_mul_of_pos_right h_lt h_pow_pos
      have h_sign' := diff_sign_sum_sign_of_dominant (Ne.symm h_diff_sign) h_dom
      have h_sign : (x.negative_ = true → x.toRat + y.toRat ≤ 0) ∧
          (x.negative_ = false → 0 ≤ x.toRat + y.toRat) := by
        rw [add_comm x.toRat y.toRat]; exact h_sign'
      simp only [if_pos h_xm_gt_uint] at hok
      obtain ⟨M, ze', δ, sticky, hδ_low, hδ_le, hsticky_zero, hM_pos, hM_lt, hM_big,
          htruth_out, hok_out, hδ_lt, hsticky_pos⟩ :=
        operator_add_post_subtract x.negative_ zm_pre x.exponent_ g_aln f_aln mode
          hf_aln_rep hzm_pre_pos hzm_pre_lt hxbit_big (x.toRat + y.toRat) h_truth_eq_minus
          result hok
      exact ⟨M, ze', δ, x.negative_, sticky, hδ_low, hδ_le, hsticky_zero, hM_pos, hM_lt,
        hM_big, htruth_out, hok_out, h_sign, hδ_lt, hsticky_pos⟩
    · -- Case 3: xe = ye, no alignment. g = Guard.new, f_aln = 0.
      push_neg at h_xe_gt_ye
      have h_xe_eq_ye : x.exponent_ = y.exponent_ := le_antisymm h_xe_gt_ye h_xe_lt_ye
      rw [if_neg (not_lt.mpr h_xe_lt_ye), if_neg (not_lt.mpr h_xe_gt_ye)] at hok
      simp only at hok
      have h_x_abs : |x.toRat| = (x.mantissa_.toNat : ℚ) * 10 ^ x.exponent_ := abs_toRat_eq x
      have h_y_abs : |y.toRat| = (y.mantissa_.toNat : ℚ) * 10 ^ y.exponent_ := abs_toRat_eq y
      -- The sign-aware initial guard (no alignment in the equal-exponent case).
      set g₀ : Guard := if x.negative_ then Guard.new.set_negative else Guard.new with hg₀_def
      have hg₀_rep : represents g₀ 0 := represents_initial_guard_eq x.negative_
      have hg₀_xbit : g₀.xbit_ = false := by
        rw [hg₀_def]
        cases hxn : x.negative_ <;> rfl
      by_cases h_xm_gt : x.mantissa_.toNat > y.mantissa_.toNat
      · -- xm > ym
        have h_xm_gt_uint : x.mantissa_ > y.mantissa_ := UInt64.lt_iff_toNat_lt.mpr h_xm_gt
        set zm_pre : UInt128 := toUInt128 x.mantissa_ - toUInt128 y.mantissa_ with hzm_pre_def
        have h_ym_le_xm : toUInt128 y.mantissa_ ≤ toUInt128 x.mantissa_ := by
          rw [BitVec.le_def, toNat_toUInt128, toNat_toUInt128]
          exact Nat.le_of_lt h_xm_gt
        have hzm_pre_toNat : zm_pre.toNat = x.mantissa_.toNat - y.mantissa_.toNat := by
          rw [hzm_pre_def, BitVec.toNat_sub_of_le h_ym_le_xm, toNat_toUInt128, toNat_toUInt128]
        have hzm_pre_pos : 1 ≤ zm_pre.toNat := by rw [hzm_pre_toNat]; omega
        have h_truth_eq : |x.toRat + y.toRat| = ((zm_pre.toNat : ℚ) - 0) * 10 ^ x.exponent_ := by
          rw [h_abs_add, h_x_abs, h_y_abs, ← h_xe_eq_ye]
          have h_diff_nn : (0 : ℚ) ≤ (x.mantissa_.toNat : ℚ) - (y.mantissa_.toNat : ℚ) := by
            have : ((y.mantissa_.toNat : ℕ) : ℚ) ≤ ((x.mantissa_.toNat : ℕ) : ℚ) := by
              exact_mod_cast (Nat.le_of_lt h_xm_gt)
            linarith
          rw [show (x.mantissa_.toNat : ℚ) * 10 ^ x.exponent_
                   - (y.mantissa_.toNat : ℚ) * 10 ^ x.exponent_
                 = ((x.mantissa_.toNat : ℚ) - (y.mantissa_.toNat : ℚ)) * 10 ^ x.exponent_ from by ring]
          have h_pow_pos : (0 : ℚ) < 10 ^ x.exponent_ := by positivity
          have h_prod_nn : (0 : ℚ)
              ≤ ((x.mantissa_.toNat : ℚ) - (y.mantissa_.toNat : ℚ)) * 10 ^ x.exponent_ :=
            mul_nonneg h_diff_nn (le_of_lt h_pow_pos)
          rw [abs_of_nonneg h_prod_nn, hzm_pre_toNat]
          have hsub_q : ((x.mantissa_.toNat - y.mantissa_.toNat : ℕ) : ℚ)
              = (x.mantissa_.toNat : ℚ) - (y.mantissa_.toNat : ℚ) := by
            rw [Nat.cast_sub (Nat.le_of_lt h_xm_gt)]
          rw [hsub_q]; ring
        have hzm_pre_lt : zm_pre.toNat < 10 ^ 20 := by rw [hzm_pre_toNat]; omega
        have hxbit_big : g₀.xbit_ = true → 10 ^ 18 - 101 ≤ zm_pre.toNat := by
          intro h; rw [hg₀_xbit] at h; exact Bool.noConfusion h
        have h_dom : |y.toRat| < |x.toRat| := by
          rw [h_x_abs, h_y_abs, ← h_xe_eq_ye]
          have h_pow_pos : (0 : ℚ) < 10 ^ x.exponent_ := by positivity
          have h_lt : (y.mantissa_.toNat : ℚ) < (x.mantissa_.toNat : ℚ) := by
            exact_mod_cast h_xm_gt
          exact mul_lt_mul_of_pos_right h_lt h_pow_pos
        have h_sign' := diff_sign_sum_sign_of_dominant (Ne.symm h_diff_sign) h_dom
        have h_sign : (x.negative_ = true → x.toRat + y.toRat ≤ 0) ∧
            (x.negative_ = false → 0 ≤ x.toRat + y.toRat) := by
          rw [add_comm x.toRat y.toRat]; exact h_sign'
        simp only [if_pos h_xm_gt_uint] at hok
        obtain ⟨M, ze', δ, sticky, hδ_low, hδ_le, hsticky_zero, hM_pos, hM_lt, hM_big,
            htruth_out, hok_out, hδ_lt, hsticky_pos⟩ :=
          operator_add_post_subtract x.negative_ zm_pre x.exponent_ g₀ 0 mode
            hg₀_rep hzm_pre_pos hzm_pre_lt hxbit_big (x.toRat + y.toRat) h_truth_eq
            result hok
        exact ⟨M, ze', δ, x.negative_, sticky, hδ_low, hδ_le, hsticky_zero, hM_pos, hM_lt,
          hM_big, htruth_out, hok_out, h_sign, hδ_lt, hsticky_pos⟩
      · push_neg at h_xm_gt
        by_cases h_xm_eq : x.mantissa_.toNat = y.mantissa_.toNat
        · -- Degenerate: xm = ym ∧ xe = ye ∧ diff-sign means x = -y — contradicts h_not_zero.
          exfalso
          apply h_not_zero
          have h_xm_eq_uint : x.mantissa_ = y.mantissa_ := UInt64.toNat_inj.mp h_xm_eq
          have hxn_eq : x.negative_ = !y.negative_ := by
            cases hxn : x.negative_ <;> cases hyn : y.negative_ <;> simp_all
          unfold Number.operator_eq Number.operator_neg
          rw [if_neg (by simp [hy_mant_ne] : ¬ (y.mantissa_ == 0) = true)]
          simp only [Bool.and_eq_true, beq_iff_eq]
          exact ⟨⟨hxn_eq, h_xm_eq_uint⟩, h_xe_eq_ye⟩
        · -- xm < ym
          have h_xm_lt : x.mantissa_.toNat < y.mantissa_.toNat := by omega
          set zm_pre : UInt128 := toUInt128 y.mantissa_ - toUInt128 x.mantissa_ with hzm_pre_def
          have h_xm_le : toUInt128 x.mantissa_ ≤ toUInt128 y.mantissa_ := by
            rw [BitVec.le_def, toNat_toUInt128, toNat_toUInt128]
            exact Nat.le_of_lt h_xm_lt
          have hzm_pre_toNat : zm_pre.toNat = y.mantissa_.toNat - x.mantissa_.toNat := by
            rw [hzm_pre_def, BitVec.toNat_sub_of_le h_xm_le, toNat_toUInt128, toNat_toUInt128]
          have hzm_pre_pos : 1 ≤ zm_pre.toNat := by rw [hzm_pre_toNat]; omega
          have h_truth_eq : |x.toRat + y.toRat| = ((zm_pre.toNat : ℚ) - 0) * 10 ^ x.exponent_ := by
            rw [h_abs_add, h_x_abs, h_y_abs, ← h_xe_eq_ye]
            have h_diff_nn : (0 : ℚ) ≤ (y.mantissa_.toNat : ℚ) - (x.mantissa_.toNat : ℚ) := by
              have : ((x.mantissa_.toNat : ℕ) : ℚ) ≤ ((y.mantissa_.toNat : ℕ) : ℚ) := by
                exact_mod_cast (Nat.le_of_lt h_xm_lt)
              linarith
            rw [show (x.mantissa_.toNat : ℚ) * 10 ^ x.exponent_
                     - (y.mantissa_.toNat : ℚ) * 10 ^ x.exponent_
                   = -(((y.mantissa_.toNat : ℚ) - (x.mantissa_.toNat : ℚ)) * 10 ^ x.exponent_)
                 from by ring]
            rw [abs_neg]
            have h_pow_pos : (0 : ℚ) < 10 ^ x.exponent_ := by positivity
            have h_prod_nn : (0 : ℚ)
                ≤ ((y.mantissa_.toNat : ℚ) - (x.mantissa_.toNat : ℚ)) * 10 ^ x.exponent_ :=
              mul_nonneg h_diff_nn (le_of_lt h_pow_pos)
            rw [abs_of_nonneg h_prod_nn, hzm_pre_toNat]
            have hsub_q : ((y.mantissa_.toNat - x.mantissa_.toNat : ℕ) : ℚ)
                = (y.mantissa_.toNat : ℚ) - (x.mantissa_.toNat : ℚ) := by
              rw [Nat.cast_sub (Nat.le_of_lt h_xm_lt)]
            rw [hsub_q]; ring
          have hzm_pre_lt : zm_pre.toNat < 10 ^ 20 := by rw [hzm_pre_toNat]; omega
          have hxbit_big : g₀.xbit_ = true → 10 ^ 18 - 101 ≤ zm_pre.toNat := by
            intro h; rw [hg₀_xbit] at h; exact Bool.noConfusion h
          have h_not_xm_gt : ¬ x.mantissa_ > y.mantissa_ := by
            intro h
            have := UInt64.lt_iff_toNat_lt.mp h; omega
          have h_dom : |x.toRat| < |y.toRat| := by
            rw [h_x_abs, h_y_abs, ← h_xe_eq_ye]
            have h_pow_pos : (0 : ℚ) < 10 ^ x.exponent_ := by positivity
            have h_lt : (x.mantissa_.toNat : ℚ) < (y.mantissa_.toNat : ℚ) := by
              exact_mod_cast h_xm_lt
            exact mul_lt_mul_of_pos_right h_lt h_pow_pos
          have h_sign := diff_sign_sum_sign_of_dominant h_diff_sign h_dom
          simp only [if_neg h_not_xm_gt] at hok
          obtain ⟨M, ze', δ, sticky, hδ_low, hδ_le, hsticky_zero, hM_pos, hM_lt, hM_big,
              htruth_out, hok_out, hδ_lt, hsticky_pos⟩ :=
            operator_add_post_subtract y.negative_ zm_pre x.exponent_ g₀ 0 mode
              hg₀_rep hzm_pre_pos hzm_pre_lt hxbit_big (x.toRat + y.toRat) h_truth_eq
              result hok
          exact ⟨M, ze', δ, y.negative_, sticky, hδ_low, hδ_le, hsticky_zero, hM_pos, hM_lt,
            hM_big, htruth_out, hok_out, h_sign, hδ_lt, hsticky_pos⟩

end XRPL.Model.Protocol
