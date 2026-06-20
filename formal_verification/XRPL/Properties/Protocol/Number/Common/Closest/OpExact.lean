import XRPL.Properties.Protocol.Number.Common.Notation
import Mathlib.Tactic

import XRPL.Properties.Protocol.Number.Common.Closest.GridPoint
import XRPL.Properties.Protocol.Number.Add.Common.Rounded
import XRPL.Properties.Protocol.Number.Add.RoundsToRepresentable


namespace XRPL.Model.Protocol

/-! # Exactness corollaries of the discrete-rounding program

A correctly/faithfully rounded operation returns an exactly representable
result *exactly*, in every mode: the two grid neighbors of a grid point
coincide with it. This file packages that for addition (the diff-sign form
the `roundToScale` proof needs) together with the normalized-representation
uniqueness lemmas it rests on. -/

/-- Two normalized mantissa/exponent pairs in the canonical decade denote
different values unless they are componentwise equal. -/
private lemma decade_uniq {a c : ℕ} {b d : ℤ}
    (ha : 10 ^ 18 ≤ a) (ha' : a < 10 ^ 19) (hc : 10 ^ 18 ≤ c) (hc' : c < 10 ^ 19)
    (h : (a : ℚ) * 10 ^ b = (c : ℚ) * 10 ^ d) : a = c ∧ b = d := by
  have hten : (10 : ℚ) ≠ 0 := by norm_num
  -- Symmetric core: if b ≤ d, divide through by 10^b.
  have core : ∀ (a c : ℕ) (b d : ℤ), 10 ^ 18 ≤ a → a < 10 ^ 19 → 10 ^ 18 ≤ c →
      b ≤ d → (a : ℚ) * 10 ^ b = (c : ℚ) * 10 ^ d → a = c ∧ b = d := by
    intro a c b d ha ha' hc hbd h
    have hkey : (a : ℚ) = (c : ℚ) * 10 ^ (d - b) := by
      have h1 : (a : ℚ) * 10 ^ b * 10 ^ (-b) = (c : ℚ) * 10 ^ d * 10 ^ (-b) := by rw [h]
      rw [mul_assoc, mul_assoc, ← zpow_add₀ hten, ← zpow_add₀ hten,
          add_neg_cancel, zpow_zero, mul_one] at h1
      rw [h1, show d + -b = d - b from by ring]
    have hk_nn : 0 ≤ d - b := by omega
    have hpow_nat : (10 : ℚ) ^ (d - b) = ((10 ^ (d - b).toNat : ℕ) : ℚ) := by
      rw [Nat.cast_pow, Nat.cast_ofNat, ← zpow_natCast]
      congr 1
      omega
    rw [hpow_nat, ← Nat.cast_mul] at hkey
    have hnat : a = c * 10 ^ (d - b).toNat := Nat.cast_injective hkey
    by_cases hzero : (d - b).toNat = 0
    · rw [hzero, pow_zero, mul_one] at hnat
      exact ⟨hnat, by omega⟩
    · exfalso
      have h10 : 10 ≤ 10 ^ (d - b).toNat := by
        calc 10 = 10 ^ 1 := (pow_one 10).symm
          _ ≤ 10 ^ (d - b).toNat := Nat.pow_le_pow_right (by norm_num) (by omega)
      have : 10 ^ 19 ≤ c * 10 ^ (d - b).toNat := by
        calc (10 : ℕ) ^ 19 = 10 ^ 18 * 10 := by norm_num
          _ ≤ c * 10 ^ (d - b).toNat := Nat.mul_le_mul hc h10
      omega
  by_cases hbd : b ≤ d
  · exact core a c b d ha ha' hc hbd h
  · push_neg at hbd
    obtain ⟨h1, h2⟩ := core c a d b hc hc' ha (le_of_lt hbd) h.symm
    exact ⟨h1.symm, h2.symm⟩

/-- A normalized Number's mantissa/exponent are recoverable from any decade
form of its magnitude. -/
lemma Number.normalized_rep_of_abs (x : Number) (hx : x.isNormalized)
    (m : ℕ) (e : ℤ) (hm : 10 ^ 18 ≤ m) (hm' : m < 10 ^ 19)
    (habs : |x.toRat| = (m : ℚ) * 10 ^ e) :
    x.mantissa_.toNat = m ∧ x.exponent_ = e := by
  rcases hx with hz | ⟨hmin, hmax, _, _, _⟩
  · exfalso
    rw [hz, Number.toRat_zero, abs_zero] at habs
    have hpow : (0 : ℚ) < 10 ^ e := zpow_pos (by norm_num) _
    have hm_pos : (0 : ℚ) < (m : ℚ) := by
      have : 0 < m := by omega
      exact_mod_cast this
    nlinarith
  · have h1 := abs_toRat_eq x
    rw [habs] at h1
    have hx_lo : 10 ^ 18 ≤ x.mantissa_.toNat := by
      have := UInt64.le_iff_toNat_le.mp hmin
      rw [largeRange_min_val] at this
      omega
    have hx_hi : x.mantissa_.toNat < 10 ^ 19 := by
      have := UInt64.le_iff_toNat_le.mp hmax
      rw [largeRange_max_val] at this
      omega
    exact decade_uniq hx_lo hx_hi hm hm' h1.symm

/-- The smallest positive normalized magnitude. -/
lemma Number.abs_toRat_ge_spr (x : Number) (hx : x.isNormalized)
    (hx_ne : x.mantissa_ ≠ 0) :
    (10 : ℚ) ^ (18 : ℕ) * (10 : ℚ) ^ (minExponent : ℤ) ≤ |x.toRat| := by
  rcases hx with hz | ⟨hmin, _, _, hemin, _⟩
  · exact absurd (by rw [hz]; rfl) hx_ne
  · rw [abs_toRat_eq x]
    have h_mant : (10 : ℚ) ^ (18 : ℕ) ≤ (x.mantissa_.toNat : ℚ) := by
      have := UInt64.le_iff_toNat_le.mp hmin
      rw [largeRange_min_val] at this
      have : ((10 ^ 18 : ℕ) : ℚ) ≤ (x.mantissa_.toNat : ℚ) := by exact_mod_cast this
      calc (10 : ℚ) ^ (18 : ℕ) = ((10 ^ 18 : ℕ) : ℚ) := by norm_num
        _ ≤ _ := this
    have h_pow : (10 : ℚ) ^ (minExponent : ℤ) ≤ (10 : ℚ) ^ x.exponent_ :=
      zpow_le_zpow_right₀ (by norm_num) hemin
    have h_pow_pos : (0 : ℚ) < (10 : ℚ) ^ (minExponent : ℤ) := zpow_pos (by norm_num) _
    have h_mant_pos : (0 : ℚ) ≤ (x.mantissa_.toNat : ℚ) := by positivity
    calc (10 : ℚ) ^ (18 : ℕ) * (10 : ℚ) ^ (minExponent : ℤ)
        ≤ (x.mantissa_.toNat : ℚ) * (10 : ℚ) ^ (minExponent : ℤ) :=
          mul_le_mul_of_nonneg_right h_mant (le_of_lt h_pow_pos)
      _ ≤ (x.mantissa_.toNat : ℚ) * (10 : ℚ) ^ x.exponent_ :=
          mul_le_mul_of_nonneg_left h_pow h_mant_pos

/-- Normalized Numbers are injectively embedded by `toRat`. -/
lemma Number.isNormalized.toRat_inj {x y : Number} (hx : x.isNormalized) (hy : y.isNormalized)
    (h : x.toRat = y.toRat) : x = y := by
  -- Zero cases first.
  by_cases hx0 : x.mantissa_ = 0
  · have hx_zero : x = Number.zero := by
      rcases hx with hz | ⟨hmin, _, _, _, _⟩
      · exact hz
      · exfalso
        have h1 := UInt64.le_iff_toNat_le.mp hmin
        rw [largeRange_min_val] at h1
        have h2 : x.mantissa_.toNat = 0 := by rw [hx0]; rfl
        omega
    have hy0 : y.toRat = 0 := by
      rw [← h, hx_zero, Number.toRat_zero]
    have hy_zero : y = Number.zero := by
      rcases hy with hz | ⟨hmin, _, _, _, _⟩
      · exact hz
      · exfalso
        have hne := Number.toRat_ne_zero_of_mantissa_ne_zero y (by
          intro hm
          have h1 := UInt64.le_iff_toNat_le.mp hmin
          rw [largeRange_min_val] at h1
          have h2 : y.mantissa_.toNat = 0 := by rw [hm]; rfl
          omega)
        exact hne hy0
    rw [hx_zero, hy_zero]
  · -- x nonzero ⟹ y nonzero.
    have hx_ne : x.toRat ≠ 0 := Number.toRat_ne_zero_of_mantissa_ne_zero x hx0
    have hy_ne : y.toRat ≠ 0 := by rw [← h]; exact hx_ne
    have hy0 : y.mantissa_ ≠ 0 := by
      intro hm
      exact hy_ne (Number.toRat_eq_zero_of_mantissa_zero y hm)
    -- Signs agree.
    have h_sign : x.negative_ = y.negative_ := by
      rcases hxn : x.negative_ with _ | _ <;> rcases hyn : y.negative_ with _ | _
      · rfl
      · exfalso
        have h1 : 0 ≤ x.toRat := Number.toRat_nonneg_of_nonnegative x hxn
        have h2 : y.toRat ≤ 0 := Number.toRat_nonpos_of_negative y hyn
        have h3 : x.toRat = 0 := le_antisymm (h ▸ h2) h1
        exact hx_ne h3
      · exfalso
        have h1 : x.toRat ≤ 0 := Number.toRat_nonpos_of_negative x hxn
        have h2 : 0 ≤ y.toRat := Number.toRat_nonneg_of_nonnegative y hyn
        have h3 : x.toRat = 0 := le_antisymm h1 (h ▸ h2)
        exact hx_ne h3
      · rfl
    -- Magnitudes pin mantissa and exponent.
    have hy_bounds : 10 ^ 18 ≤ y.mantissa_.toNat ∧ y.mantissa_.toNat < 10 ^ 19 := by
      rcases hy with hz | ⟨hmin, hmax, _, _, _⟩
      · exact absurd (by rw [hz]; rfl) hy0
      · constructor
        · have := UInt64.le_iff_toNat_le.mp hmin
          rw [largeRange_min_val] at this
          omega
        · have := UInt64.le_iff_toNat_le.mp hmax
          rw [largeRange_max_val] at this
          omega
    have h_abs : |x.toRat| = (y.mantissa_.toNat : ℚ) * 10 ^ y.exponent_ := by
      rw [h]
      exact abs_toRat_eq y
    obtain ⟨hm_eq, he_eq⟩ := Number.normalized_rep_of_abs x hx y.mantissa_.toNat y.exponent_
      hy_bounds.1 hy_bounds.2 h_abs
    obtain ⟨xn, xm, xe⟩ := x
    obtain ⟨yn, ym, ye⟩ := y
    simp only at h_sign hm_eq he_eq
    rw [h_sign, he_eq, UInt64.toNat_inj.mp hm_eq]

/-- Witness constructor: `k · 10^s` is a (positive) normalized grid point for
any integer `1 ≤ k < 10^16` — with three trailing zero digits and a pinned
exponent window (the 16-digit renormalization consumes both). -/
lemma exists_normalized_of_int_mul_pow (k : ℕ) (s : ℤ)
    (hk : 1 ≤ k) (hk' : k < 10 ^ 16)
    (hs_lo : minExponent + 18 ≤ s) (hs_hi : s ≤ maxExponent) :
    ∃ x : Number, x.isNormalized ∧ x.negative_ = false ∧ x.mantissa_ ≠ 0 ∧
      x.toRat = (k : ℚ) * 10 ^ s ∧
      x.mantissa_.toNat % 1000 = 0 ∧
      s - 18 ≤ x.exponent_ ∧ x.exponent_ ≤ s - 3 := by
  set L : ℕ := Nat.log 10 k with hL_def
  have hlog_lt : L < 16 := by
    rw [hL_def]
    exact Nat.log_lt_of_lt_pow (by omega) hk'
  set j : ℕ := 18 - L with hj_def
  have hj_pos : 3 ≤ j := by omega
  have hj_le : j ≤ 18 := by omega
  have hpow_le : 10 ^ L ≤ k := Nat.pow_log_le_self 10 (by omega)
  have hlt : k < 10 ^ (L + 1) := Nat.lt_pow_succ_log_self (by norm_num) k
  have hm_lo : 10 ^ 18 ≤ k * 10 ^ j := by
    calc (10 : ℕ) ^ 18 = 10 ^ L * 10 ^ j := by
          rw [← pow_add]
          congr 1
          omega
      _ ≤ k * 10 ^ j := Nat.mul_le_mul_right _ hpow_le
  have hm_hi : k * 10 ^ j < 10 ^ 19 := by
    calc k * 10 ^ j < 10 ^ (L + 1) * 10 ^ j := by
          have hpos : 0 < (10 : ℕ) ^ j := Nat.pow_pos (by norm_num)
          exact (Nat.mul_lt_mul_right hpos).mpr hlt
      _ = 10 ^ (L + 1 + j) := by rw [← pow_add]
      _ ≤ 10 ^ 19 := Nat.pow_le_pow_right (by norm_num) (by omega)
  have hfit : k * 10 ^ j < 2 ^ 64 := by
    have h64 : (10 : ℕ) ^ 19 < 2 ^ 64 := by norm_num
    omega
  refine ⟨⟨false, ⟨⟨⟨k * 10 ^ j, hfit⟩⟩⟩, s - j⟩, ?_, rfl, ?_, ?_, ?_, ?_, ?_⟩
  · right
    have h_toNat : ((⟨⟨⟨k * 10 ^ j, hfit⟩⟩⟩ : UInt64)).toNat = k * 10 ^ j := rfl
    refine ⟨?_, ?_, ?_, ?_, ?_⟩
    · show largeRange.min ≤ _
      rw [UInt64.le_iff_toNat_le, h_toNat, largeRange_min_val]
      omega
    · show _ ≤ largeRange.max
      rw [UInt64.le_iff_toNat_le, h_toNat, largeRange_max_val]
      omega
    · right
      show (⟨⟨⟨k * 10 ^ j, hfit⟩⟩⟩ : UInt64).toNat % 10 = 0
      rw [h_toNat]
      obtain ⟨j', hj'⟩ : ∃ j', j = j' + 1 := ⟨j - 1, by omega⟩
      rw [hj', pow_succ, ← mul_assoc]
      exact Nat.mul_mod_left _ 10
    · show minExponent ≤ s - (j : ℤ)
      have hj18 : (j : ℤ) ≤ 18 := by exact_mod_cast hj_le
      have hmin_e : minExponent = -32768 := rfl
      have hmax_e : maxExponent = 32768 := rfl
      omega
    · show s - (j : ℤ) ≤ maxExponent
      have hj1 : (3 : ℤ) ≤ (j : ℤ) := by exact_mod_cast hj_pos
      have hmax_e : maxExponent = 32768 := rfl
      have hmin_e : minExponent = -32768 := rfl
      omega
  · intro hzero
    have h1 : ((⟨⟨⟨k * 10 ^ j, hfit⟩⟩⟩ : UInt64)).toNat = (0 : UInt64).toNat :=
      congrArg UInt64.toNat hzero
    have h2 : ((⟨⟨⟨k * 10 ^ j, hfit⟩⟩⟩ : UInt64)).toNat = k * 10 ^ j := rfl
    have h0 : (0 : UInt64).toNat = 0 := rfl
    have h3 : 0 < k * 10 ^ j := by positivity
    omega
  · rw [Number.toRat_of_nonneg _ rfl]
    show (((⟨⟨⟨k * 10 ^ j, hfit⟩⟩⟩ : UInt64)).toNat : ℚ) * (10 : ℚ) ^ (s - (j : ℤ)) = _
    have h_toNat : ((⟨⟨⟨k * 10 ^ j, hfit⟩⟩⟩ : UInt64)).toNat = k * 10 ^ j := rfl
    rw [h_toNat]
    push_cast
    rw [show (10 : ℚ) ^ (j : ℕ) = (10 : ℚ) ^ ((j : ℕ) : ℤ) from (zpow_natCast 10 j).symm,
        mul_assoc, ← zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0),
        show (j : ℤ) + (s - (j : ℤ)) = s from by ring]
  · show (⟨⟨⟨k * 10 ^ j, hfit⟩⟩⟩ : UInt64).toNat % 1000 = 0
    have h_toNat : ((⟨⟨⟨k * 10 ^ j, hfit⟩⟩⟩ : UInt64)).toNat = k * 10 ^ j := rfl
    rw [h_toNat]
    obtain ⟨j', hj'⟩ : ∃ j', j = j' + 3 := ⟨j - 3, by omega⟩
    rw [hj', pow_add, ← mul_assoc]
    have : (10 : ℕ) ^ 3 = 1000 := by norm_num
    rw [this]
    exact Nat.mul_mod_left _ 1000
  · show s - (j : ℤ) ≥ s - 18
    have hj18 : (j : ℤ) ≤ 18 := by exact_mod_cast hj_le
    omega
  · show s - (j : ℤ) ≤ s - 3
    have hj3 : (3 : ℤ) ≤ (j : ℤ) := by exact_mod_cast hj_pos
    omega

/-- `operator_eq` is reflexive. -/
private lemma operator_eq_refl (x : Number) : x.operator_eq x = true := by
  unfold Number.operator_eq
  simp

/-- Diff-sign addition with an exactly representable sum returns it exactly,
in **every** rounding mode: the two grid neighbors of a grid point coincide. -/
theorem operator_add_exact_diff_sign (x y result : Number) (mode : rounding_mode)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_ne : x.mantissa_ ≠ 0) (hy_ne : y.mantissa_ ≠ 0)
    (h_diff : x.negative_ ≠ y.negative_)
    (w : Number) (hw : w.isNormalized)
    (hw_val : w.toRat = x.toRat + y.toRat)
    (hok : Number.operator_add x y mode = .ok result) :
    result.toRat = x.toRat + y.toRat := by
  by_cases hw0 : w.mantissa_ = 0
  · -- Exact cancellation: the model's structural guard returns the canonical zero.
    have h_truth0 : x.toRat + y.toRat = 0 := by
      rw [← hw_val]
      exact Number.toRat_eq_zero_of_mantissa_zero w hw0
    have hy_neg_norm : y.operator_neg.isNormalized := Number.operator_neg_isNormalized y hy
    have h_x_eq : x = y.operator_neg := by
      apply hx.toRat_inj hy_neg_norm
      rw [Number.toRat_neg]
      linarith
    have h_guard_y : (y.operator_eq Number.zero) = false := by
      rw [Bool.eq_false_iff]
      intro hg
      have hmeq := Number.mantissa_eq_zero_of_operator_eq_zero hg
      exact hy_ne hmeq
    have h_guard_x : (x.operator_eq Number.zero) = false := by
      rw [Bool.eq_false_iff]
      intro hg
      have hmeq := Number.mantissa_eq_zero_of_operator_eq_zero hg
      exact hx_ne hmeq
    have h_guard_eq : (x.operator_eq y.operator_neg) = true := by
      rw [h_x_eq]
      exact operator_eq_refl _
    unfold Number.operator_add at hok
    rw [h_guard_y, h_guard_x, h_guard_eq] at hok
    simp only [Bool.false_eq_true, if_false, if_true] at hok
    have h_result : result = Number.zero :=
      (Except.ok.inj (show (Except.ok Number.zero : Except String Number) = .ok result
        from hok)).symm
    rw [h_result, Number.toRat_zero, h_truth0]
  · -- Nonzero representable sum.
    have hw_ne : w.toRat ≠ 0 := Number.toRat_ne_zero_of_mantissa_ne_zero w hw0
    have h_truth_ne : x.toRat + y.toRat ≠ 0 := by rw [← hw_val]; exact hw_ne
    have h_not_eq : ¬ x.operator_eq y.operator_neg := by
      intro hg
      exact h_truth_ne (add_truth_zero_of_eq_neg x y hg)
    have hresult : result.mantissa_ ≠ 0 := by
      intro hres0
      have h_small := operator_add_underflow_truth_small x y result mode hx hy
        hx_ne hy_ne h_diff h_not_eq hok hres0
      have h_ge := Number.abs_toRat_ge_spr w hw hw0
      rw [hw_val] at h_ge
      linarith
    rcases mode with _ | _ | _ | _
    · -- .to_nearest
      have h := operator_add_rounded_to_nearest x y result hx hy hok
      rcases h with ⟨n, hn_eq, hn_val⟩ | ⟨n, hn_eq, hn_val⟩
      · obtain ⟨n', hn'_eq, hn'_val⟩ := Number.lower_value_self w hw hw_ne
        rw [← hw_val] at hn_eq
        have hnn' : n = n' := Option.some.inj (hn_eq.symm.trans hn'_eq)
        rw [hn_val, hnn', ← hn'_val, hw_val]
      · obtain ⟨n', hn'_eq, hn'_val⟩ := Number.upper_value_self w hw hw_ne
        rw [← hw_val] at hn_eq
        have hnn' : n = n' := Option.some.inj (hn_eq.symm.trans hn'_eq)
        rw [hn_val, hnn', ← hn'_val, hw_val]
    · -- .towards_zero
      have h := operator_add_rounded_towards_zero x y result hx hy hok
      obtain ⟨n, hn_eq, hn_val⟩ := h
      by_cases h_nn : x.toRat + y.toRat ≥ 0
      · rw [if_pos h_nn] at hn_eq
        obtain ⟨n', hn'_eq, hn'_val⟩ := Number.lower_value_self w hw hw_ne
        rw [← hw_val] at hn_eq
        have hnn' : n = n' := Option.some.inj (hn_eq.symm.trans hn'_eq)
        rw [hn_val, hnn', ← hn'_val, hw_val]
      · rw [if_neg h_nn] at hn_eq
        obtain ⟨n', hn'_eq, hn'_val⟩ := Number.upper_value_self w hw hw_ne
        rw [← hw_val] at hn_eq
        have hnn' : n = n' := Option.some.inj (hn_eq.symm.trans hn'_eq)
        rw [hn_val, hnn', ← hn'_val, hw_val]
    · -- .downward
      have h := operator_add_rounded_downward x y result hx hy hok hresult
      obtain ⟨n, hn_eq, hn_val⟩ := h
      obtain ⟨n', hn'_eq, hn'_val⟩ := Number.lower_value_self w hw hw_ne
      rw [← hw_val] at hn_eq
      have hnn' : n = n' := Option.some.inj (hn_eq.symm.trans hn'_eq)
      rw [hn_val, hnn', ← hn'_val, hw_val]
    · -- .upward
      have h := operator_add_rounded_upward x y result hx hy hok hresult
      obtain ⟨n, hn_eq, hn_val⟩ := h
      obtain ⟨n', hn'_eq, hn'_val⟩ := Number.upper_value_self w hw hw_ne
      rw [← hw_val] at hn_eq
      have hnn' : n = n' := Option.some.inj (hn_eq.symm.trans hn'_eq)
      rw [hn_val, hnn', ← hn'_val, hw_val]

end XRPL.Model.Protocol
