import XRPL.Properties.Protocol.Number.Common.Notation
import XRPL.Properties.Protocol.Number.Add.Common
import XRPL.Properties.Protocol.Number.Add.ToNearest.AlgorithmicFacts.PostAlignment
import XRPL.Properties.Protocol.Number.Common.Helpers
import XRPL.Properties.Protocol.Number.Rounding.DoRoundUp

set_option linter.style.longLine false
set_option linter.style.emptyLine false

namespace XRPL.Model.Protocol

set_option maxHeartbeats 1600000 in
-- large existential with three alignment cases, each unfolding the post-alignment pipeline
/-- Same-sign branch decomposition of `operator_add` in `.to_nearest` mode. -/
theorem operator_add_algorithmic_facts_same_sign (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (h_same_sign : x.negative_ = y.negative_)
    (h_not_zero : ¬ x.operator_eq y.operator_neg)
    (hok : Number.operator_add x y .to_nearest = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    ∃ (zm : UInt64) (ze' : Int) (f : ℚ) (g : Guard) (res_pos : RoundResult),
      mantissaFloor ≤ zm.toNat ∧
      zm.toNat ≤ maxRep.toNat ∧
      0 ≤ f ∧ f < 1 ∧
      (zm.toNat = mantissaFloor → (8 : ℚ) / 10 ≤ f) ∧
      |x.toRat + y.toRat| = ((zm.toNat : ℚ) + f) * 10 ^ ze' ∧
      g.doRoundUp false zm ze' largeRange.min largeRange.max .to_nearest "Number::addition overflow" = .ok res_pos ∧
      |result.toRat| = (res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_ ∧
      res_pos.mantissa_ ≠ 0 ∧
      represents g f ∧
      result.negative_ = x.negative_ := by
  -- Mantissa bounds from isNormalized
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
  -- Knock out the early returns
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
  -- Unfold operator_add
  unfold Number.operator_add at hok
  simp only [hy_ne_zero, hx_ne_zero, h_not_zero, Bool.false_eq_true, if_false] at hok
  -- xn == yn is true
  have h_xneqyn : (x.negative_ == y.negative_) = true := by
    rw [beq_iff_eq]; exact h_same_sign
  -- |x.toRat + y.toRat| = |x.toRat| + |y.toRat|
  have h_abs_add := abs_add_eq_of_same_sign h_same_sign
  -- 3-way split on xe vs ye
  by_cases h_xe_lt_ye : x.exponent_ < y.exponent_
  · -- Case 1: xe < ye, align x to ye
    rw [if_pos h_xe_lt_ye] at hok
    -- Set up post-alignment state
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
    -- xm_a bound from alignDown_mantissa_le
    have hxm_a_le_xm : xm_a.toNat ≤ x.mantissa_.toNat :=
      alignDown_mantissa_le x.mantissa_ x.exponent_ g₀ y.exponent_
    have hxm_a_lt : xm_a.toNat < 10 ^ 19 := by omega
    -- f_aln from represents
    set K : ℕ := (max x.exponent_ y.exponent_ - x.exponent_).toNat with hK_def
    have hmax_eq : max x.exponent_ y.exponent_ = y.exponent_ :=
      max_eq_right (le_of_lt h_xe_lt_ye)
    set f_aln : ℚ := (0 + ((x.mantissa_.toNat % 10 ^ K : ℕ) : ℚ)) / 10 ^ K with hf_aln_def
    have hf_aln_rep' : represents g_aln f_aln := by
      have h := alignDown_represents x.mantissa_ x.exponent_ g₀ y.exponent_ 0 hg₀_rep
      have hK_eq : (max x.exponent_ y.exponent_ - x.exponent_).toNat = K := by rw [hK_def]
      rw [hK_eq] at h
      exact h
    -- Truth value
    have htruth_eq : |x.toRat + y.toRat|
        = ((xm_a.toNat + y.mantissa_.toNat : ℕ) : ℚ) * 10 ^ e_common
          + f_aln * 10 ^ e_common := by
      rw [h_abs_add]
      -- |x.toRat| + |y.toRat| = (xm_a + f_aln') * 10^ye + ym * 10^ye
      have h_lemma := alignDown_abs_value x g₀ y.exponent_ (le_of_lt h_xe_lt_ye) 0
      -- The lemma gives: |x.toRat| + 0 * 10^xe = (xm_a + f') * 10^ye where f' matches f_aln
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
    -- Apply post_alignment helper
    have hsum_ge : 10 ^ 18 ≤ xm_a.toNat + y.mantissa_.toNat := by
      -- y.mantissa is ≥ 10^18 alone
      have : (10 : ℕ) ^ 18 ≤ y.mantissa_.toNat := hy_min
      omega
    -- Now hok is in alignment-expanded form; rewrite it to call the helper.
    -- After if_pos for xe < ye, the let binding gives (xm', xe', ym, ye, g') = aln_result + (ym, ye)
    have hok_post : (let zm128 : UInt128 := toUInt128 xm_a + toUInt128 y.mantissa_
                     let p : UInt64 × Int × Guard :=
                       if zm128 > toUInt128 largeRange.max || zm128 > toUInt128 maxRep then
                         (toUInt64 (g_aln.doDropDigit128 zm128 e_common).2.1,
                          (g_aln.doDropDigit128 zm128 e_common).2.2,
                          (g_aln.doDropDigit128 zm128 e_common).1)
                       else (toUInt64 zm128, e_common, g_aln)
                     match p.2.2.doRoundUp x.negative_ p.1 p.2.1 largeRange.min largeRange.max
                           .to_nearest "Number::addition overflow" with
                     | .error err => Except.error err
                     | .ok res => res.toNumber.normalize largeRange.min largeRange.max .to_nearest)
                    = .ok result := by
      have h := hok
      simp only [h_xneqyn, if_true] at h
      exact h
    have h_result := operator_add_post_alignment x.negative_ xm_a y.mantissa_ e_common g_aln
      f_aln hf_aln_rep' hxm_a_lt hy_mant_bound hsum_ge (x.toRat + y.toRat) htruth_eq
      result hresult hok_post
    exact h_result
  · push_neg at h_xe_lt_ye
    by_cases h_xe_gt_ye : x.exponent_ > y.exponent_
    · -- Case 2: xe > ye, align y to xe
      rw [if_neg (not_lt.mpr (le_of_lt h_xe_gt_ye)), if_pos h_xe_gt_ye] at hok
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
                         if zm128 > toUInt128 largeRange.max || zm128 > toUInt128 maxRep then
                           (toUInt64 (g_aln.doDropDigit128 zm128 e_common).2.1,
                            (g_aln.doDropDigit128 zm128 e_common).2.2,
                            (g_aln.doDropDigit128 zm128 e_common).1)
                         else (toUInt64 zm128, e_common, g_aln)
                       match p.2.2.doRoundUp x.negative_ p.1 p.2.1 largeRange.min largeRange.max
                             .to_nearest "Number::addition overflow" with
                       | .error err => Except.error err
                       | .ok res => res.toNumber.normalize largeRange.min largeRange.max .to_nearest)
                      = .ok result := by
        have h := hok
        simp only [h_xneqyn, if_true] at h
        rw [he_common_eq]
        exact h
      exact operator_add_post_alignment x.negative_ x.mantissa_ ym_a e_common g_aln
        f_aln hf_aln_rep' hx_mant_bound hym_a_lt hsum_ge (x.toRat + y.toRat) htruth_eq
        result hresult hok_post
    · -- Case 3: xe = ye
      push_neg at h_xe_gt_ye
      have h_xe_eq_ye : x.exponent_ = y.exponent_ := le_antisymm h_xe_gt_ye h_xe_lt_ye
      rw [if_neg (not_lt.mpr h_xe_lt_ye), if_neg (not_lt.mpr h_xe_gt_ye)] at hok
      -- No alignment, g_aln = Guard.new, f_aln = 0
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
                             .to_nearest "Number::addition overflow" with
                       | .error err => Except.error err
                       | .ok res => res.toNumber.normalize largeRange.min largeRange.max .to_nearest)
                      = .ok result := by
        have h := hok
        simp only [h_xneqyn, if_true] at h
        exact h
      have hxm_a_lt : xm_a.toNat < 10 ^ 19 := by rw [hxm_a_def]; exact hx_mant_bound
      have hym_a_lt : ym_a.toNat < 10 ^ 19 := by rw [hym_a_def]; exact hy_mant_bound
      exact operator_add_post_alignment x.negative_ xm_a ym_a e_common g_aln
        f_aln hf_aln_rep' hxm_a_lt hym_a_lt hsum_ge (x.toRat + y.toRat) htruth_eq
        result hresult hok_post


end XRPL.Model.Protocol
