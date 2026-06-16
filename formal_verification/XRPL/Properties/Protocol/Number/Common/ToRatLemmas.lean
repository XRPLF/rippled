import Mathlib.Tactic

import XRPL.Model.Protocol.Number
import XRPL.Properties.Protocol.Number.Common.Constants


namespace XRPL.Model.Protocol

/-! # Helper lemmas about `Number.toRat` and absolute-difference reasoning.

These are the basic facts about how `Number.toRat` interacts with sign,
absolute value, and multiplication. They're used to reduce the signed
rational arithmetic in `operator_mul_rounding_bound_to_nearest` to non-negative
mantissa arithmetic over `ℚ`. -/

/-- `Number.zero.toRat = 0`. Manually splits to avoid exposing `10^2147483648`
(the C++ `INT_MIN` exponent sentinel). -/
lemma Number.toRat_zero : Number.zero.toRat = 0 := by
  change (let sign : Int := if Number.zero.negative_ then -1 else 1
          let m : Int := Number.zero.mantissa_.toNat
          if Number.zero.exponent_ ≥ 0 then
            mkRat (sign * m * (10 : Int) ^ Number.zero.exponent_.toNat) 1
          else
            mkRat (sign * m) ((10 : Nat) ^ (-Number.zero.exponent_).toNat)) = 0
  have hm : Number.zero.mantissa_.toNat = 0 := rfl
  have hneg : Number.zero.negative_ = false := rfl
  simp only [hneg, hm, Nat.cast_zero, if_false, Bool.false_eq_true]
  split_ifs <;> simp

/-- Extract mantissa bounds from `isNormalized` when mantissa is nonzero. -/
lemma Number.isNormalized.mantissaBounds {n : Number}
    (h : n.isNormalized) (hne : n.mantissa_ ≠ 0) :
    largeRange.min ≤ n.mantissa_ ∧ n.mantissa_ ≤ largeRange.max := by
  rcases h with h_zero | ⟨hmin, hmax, _, _, _⟩
  · exfalso; apply hne; rw [h_zero]; rfl
  · exact ⟨hmin, hmax⟩

/-- The normalized mantissa `.toNat` bounds `[10^18, 10^19)`, derived from the
`UInt64` `mantissaBounds` pair. Absorbs the recurring `UInt64.le_iff_toNat_le …
rw [largeRange_min_val/max_val]` extraction blocks. -/
lemma mantissaBounds_nat_of {n : Number}
    (hb : largeRange.min ≤ n.mantissa_ ∧ n.mantissa_ ≤ largeRange.max) :
    10 ^ 18 ≤ n.mantissa_.toNat ∧ n.mantissa_.toNat < 10 ^ 19 := by
  obtain ⟨hlo, hhi⟩ := hb
  rw [UInt64.le_iff_toNat_le, largeRange_min_val] at hlo
  rw [UInt64.le_iff_toNat_le, largeRange_max_val] at hhi
  exact ⟨hlo, by omega⟩

/-- Direct `.toNat` mantissa bounds from `isNormalized` + nonzero mantissa. -/
lemma Number.isNormalized.mantissaBounds_nat {n : Number}
    (h : n.isNormalized) (hne : n.mantissa_ ≠ 0) :
    10 ^ 18 ≤ n.mantissa_.toNat ∧ n.mantissa_.toNat < 10 ^ 19 :=
  mantissaBounds_nat_of (h.mantissaBounds hne)

/-- A normalized mantissa is positive (`.toNat`), from the `mantissaBounds` pair. -/
lemma mantissa_toNat_pos_of_bounds {n : Number}
    (hb : largeRange.min ≤ n.mantissa_ ∧ n.mantissa_ ≤ largeRange.max) :
    0 < n.mantissa_.toNat := by
  have := (mantissaBounds_nat_of hb).1; omega

/-- `|n.toRat| = n.mantissa_.toNat * 10^n.exponent_`. -/
lemma abs_toRat_eq (n : Number) :
    |n.toRat| = (n.mantissa_.toNat : ℚ) * 10 ^ n.exponent_ := by
  unfold Number.toRat
  split_ifs with hneg hexp hexp
  · rw [Rat.mkRat_one]
    have h_to : n.exponent_ = (n.exponent_.toNat : ℤ) := (Int.toNat_of_nonneg hexp).symm
    have h_neg_val : ((-1 * (n.mantissa_.toNat : Int) * (10 : Int) ^ n.exponent_.toNat : Int) : ℚ)
        = -((n.mantissa_.toNat : ℚ) * (10 : ℚ) ^ n.exponent_) := by
      push_cast
      conv_rhs => rw [h_to]; rw [zpow_natCast]
      ring
    rw [h_neg_val, abs_neg]
    rw [abs_of_nonneg (by positivity)]
  · have hneg_exp : n.exponent_ < 0 := not_le.mp hexp
    have h_pow : (0 : ℚ) < (10 : ℚ) ^ n.exponent_ := zpow_pos (by norm_num) _
    rw [Rat.mkRat_eq_div]
    push_cast
    have h_neg_exp : n.exponent_ = -((-n.exponent_).toNat : ℤ) := by
      rw [Int.toNat_of_nonneg (by omega : (0 : ℤ) ≤ -n.exponent_)]; ring
    have h_val : (-1 * (n.mantissa_.toNat : ℚ)) / (10 : ℚ) ^ (-n.exponent_).toNat
        = -((n.mantissa_.toNat : ℚ) * (10 : ℚ) ^ n.exponent_) := by
      conv_rhs => rw [h_neg_exp, zpow_neg, zpow_natCast]
      field_simp
    rw [h_val, abs_neg, abs_of_nonneg (by positivity)]
  · rw [Rat.mkRat_one]
    have h_to : n.exponent_ = (n.exponent_.toNat : ℤ) := (Int.toNat_of_nonneg hexp).symm
    have h_val : ((1 * (n.mantissa_.toNat : Int) * (10 : Int) ^ n.exponent_.toNat : Int) : ℚ)
        = (n.mantissa_.toNat : ℚ) * (10 : ℚ) ^ n.exponent_ := by
      push_cast
      conv_rhs => rw [h_to]; rw [zpow_natCast]
      ring
    rw [h_val, abs_of_nonneg (by positivity)]
  · have hneg_exp : n.exponent_ < 0 := not_le.mp hexp
    rw [Rat.mkRat_eq_div]
    push_cast
    have h_neg_exp : n.exponent_ = -((-n.exponent_).toNat : ℤ) := by
      rw [Int.toNat_of_nonneg (by omega : (0 : ℤ) ≤ -n.exponent_)]; ring
    have h_val : (1 * (n.mantissa_.toNat : ℚ)) / (10 : ℚ) ^ (-n.exponent_).toNat
        = (n.mantissa_.toNat : ℚ) * (10 : ℚ) ^ n.exponent_ := by
      conv_rhs => rw [h_neg_exp, zpow_neg, zpow_natCast]
      field_simp
    rw [h_val, abs_of_nonneg (by positivity)]

/-- `n.toRat ≤ 0` when `negative_ = true`. -/
lemma Number.toRat_nonpos_of_negative (n : Number) (hneg : n.negative_ = true) :
    n.toRat ≤ 0 := by
  unfold Number.toRat
  rw [hneg]; simp only [if_true]
  split_ifs with hexp
  · rw [Rat.mkRat_one]; push_cast
    have : (0 : ℚ) ≤ (n.mantissa_.toNat : ℚ) * (10 : ℚ) ^ n.exponent_.toNat := by positivity
    linarith
  · rw [Rat.mkRat_eq_div]; push_cast
    have h_m_nn : (0 : ℚ) ≤ (n.mantissa_.toNat : ℚ) := Nat.cast_nonneg _
    have h_pow_pos : (0 : ℚ) < (10 : ℚ) ^ (-n.exponent_).toNat := by positivity
    have h_frac_nn : (0 : ℚ) ≤ (n.mantissa_.toNat : ℚ) / (10 : ℚ) ^ (-n.exponent_).toNat :=
      div_nonneg h_m_nn (le_of_lt h_pow_pos)
    have : -(1 : ℚ) * (n.mantissa_.toNat : ℚ) / (10 : ℚ) ^ (-n.exponent_).toNat
        = -((n.mantissa_.toNat : ℚ) / (10 : ℚ) ^ (-n.exponent_).toNat) := by
      field_simp
    rw [this]; linarith

/-- `0 ≤ n.toRat` when `negative_ = false`. -/
lemma Number.toRat_nonneg_of_nonnegative (n : Number) (hneg : n.negative_ = false) :
    0 ≤ n.toRat := by
  unfold Number.toRat
  rw [hneg]; simp only [Bool.false_eq_true, if_false]
  split_ifs
  · rw [Rat.mkRat_one]; push_cast; positivity
  · rw [Rat.mkRat_eq_div]; push_cast; positivity

/-- For a non-negative Number, `toRat = m · 10^e` as rationals. -/
lemma Number.toRat_of_nonneg (n : Number) (hneg : n.negative_ = false) :
    n.toRat = (n.mantissa_.toNat : ℚ) * (10 : ℚ) ^ n.exponent_ := by
  unfold Number.toRat
  simp only [hneg]
  by_cases hexp : n.exponent_ ≥ 0
  · rw [if_pos hexp, Rat.mkRat_one]
    have h_to : n.exponent_ = (n.exponent_.toNat : ℤ) := (Int.toNat_of_nonneg hexp).symm
    rw [h_to, zpow_natCast]
    push_cast
    rw [Int.toNat_natCast]
    ring
  · rw [if_neg hexp]
    have hneg_exp : n.exponent_ < 0 := not_le.mp hexp
    have h_neg_pos : 0 ≤ -n.exponent_ := by omega
    rw [Rat.mkRat_eq_div]
    push_cast
    have h_neg : n.exponent_ = -((-n.exponent_).toNat : ℤ) := by
      rw [Int.toNat_of_nonneg h_neg_pos]; ring
    conv_rhs => rw [h_neg, zpow_neg, zpow_natCast]
    field_simp

/-- For a negative Number `n`, `n.toRat = -(m * 10^e)`. -/
lemma Number.toRat_of_neg (n : Number) (hneg : n.negative_ = true) :
    n.toRat = -((n.mantissa_.toNat : ℚ) * (10 : ℚ) ^ n.exponent_) := by
  have h_abs : |n.toRat| = (n.mantissa_.toNat : ℚ) * 10 ^ n.exponent_ := abs_toRat_eq n
  have h_np : n.toRat ≤ 0 := Number.toRat_nonpos_of_negative n hneg
  rw [abs_of_nonpos h_np] at h_abs
  linarith

/-- Sign of the product: same signs => nonneg, different signs => nonpos. -/
lemma toRat_mul_sign (x y : Number) :
    (x.negative_ = y.negative_ → 0 ≤ x.toRat * y.toRat) ∧
    (x.negative_ ≠ y.negative_ → x.toRat * y.toRat ≤ 0) := by
  refine ⟨?_, ?_⟩
  · intro h
    cases hxneg : x.negative_
    · have hyneg : y.negative_ = false := h ▸ hxneg
      exact mul_nonneg (Number.toRat_nonneg_of_nonnegative x hxneg)
        (Number.toRat_nonneg_of_nonnegative y hyneg)
    · have hyneg : y.negative_ = true := h ▸ hxneg
      exact mul_nonneg_of_nonpos_of_nonpos (Number.toRat_nonpos_of_negative x hxneg)
        (Number.toRat_nonpos_of_negative y hyneg)
  · intro h
    cases hxneg : x.negative_
    · have hyneg : y.negative_ = true := by
        cases hyneg' : y.negative_
        · exfalso; apply h; rw [hxneg, hyneg']
        · rfl
      exact mul_nonpos_of_nonneg_of_nonpos (Number.toRat_nonneg_of_nonnegative x hxneg)
        (Number.toRat_nonpos_of_negative y hyneg)
    · have hyneg : y.negative_ = false := by
        cases hyneg' : y.negative_
        · rfl
        · exfalso; apply h; rw [hxneg, hyneg']
      exact mul_nonpos_of_nonpos_of_nonneg (Number.toRat_nonpos_of_negative x hxneg)
        (Number.toRat_nonneg_of_nonnegative y hyneg)

/-- `n.toRat = 0` iff `n.mantissa_ = 0`. -/
lemma Number.toRat_eq_zero_iff {n : Number} :
    n.toRat = 0 ↔ n.mantissa_ = 0 := by
  constructor
  · intro h
    have habs := abs_toRat_eq n
    rw [h, abs_zero] at habs
    have h10_pos : (0 : ℚ) < 10 ^ n.exponent_ := zpow_pos (by norm_num) _
    have hm_zero : (n.mantissa_.toNat : ℚ) = 0 := by
      nlinarith [Nat.cast_nonneg (α := ℚ) n.mantissa_.toNat]
    have : n.mantissa_.toNat = 0 := by exact_mod_cast hm_zero
    exact UInt64.ext this
  · intro h
    have hm : n.mantissa_.toNat = 0 := by rw [h]; rfl
    unfold Number.toRat
    simp only [hm, Nat.cast_zero]
    split_ifs <;> simp

/-- `n.mantissa_ ≠ 0 ↔ n.toRat ≠ 0` — the contrapositive of `toRat_eq_zero_iff`.
Absorbs the recurring "mantissa is nonzero because the value is nonzero/pos/neg"
inline blocks: feed it `h.ne'` (from `0 < toRat`), `h.ne` (from `toRat < 0`), or a
`toRat ≠ 0` directly. -/
lemma Number.mantissa_ne_zero_iff {n : Number} : n.mantissa_ ≠ 0 ↔ n.toRat ≠ 0 :=
  (not_congr Number.toRat_eq_zero_iff).symm

/-- A nonzero value has a nonzero mantissa. -/
lemma Number.mantissa_ne_zero_of_toRat_ne_zero {n : Number} (h : n.toRat ≠ 0) :
    n.mantissa_ ≠ 0 :=
  Number.mantissa_ne_zero_iff.mpr h

/-- `x.operator_eq Number.zero` forces a zero mantissa. Absorbs the recurring
3-line `unfold operator_eq … ; simp … ; obtain ⟨⟨_, ·⟩, _⟩` incantation. -/
lemma Number.mantissa_eq_zero_of_operator_eq_zero {x : Number}
    (h : x.operator_eq Number.zero = true) : x.mantissa_ = 0 := by
  unfold Number.operator_eq Number.zero at h
  simp only [Bool.and_eq_true, beq_iff_eq] at h
  obtain ⟨⟨_, hmeq⟩, _⟩ := h
  exact hmeq

/-- A nonzero mantissa rules out `operator_eq Number.zero`. Absorbs the recurring
`intro h; mantissa_eq_zero_of_operator_eq_zero; … ; omega` guard blocks. -/
lemma Number.not_operator_eq_zero_of_mantissa_ne {x : Number} (h : x.mantissa_ ≠ 0) :
    ¬ x.operator_eq Number.zero = true :=
  fun heq => h (Number.mantissa_eq_zero_of_operator_eq_zero heq)

/-- Two 19-digit mantissas multiply below `2^128` (the `UInt128` product fits).
Absorbs the recurring `calc … < 10^19*10^19 = 10^38 < 2^128` block. -/
lemma nat_mul_lt_two_pow_128 {a b : ℕ} (ha : a < 10 ^ 19) (hb : b < 10 ^ 19) :
    a * b < 2 ^ 128 :=
  calc a * b < 10 ^ 19 * 10 ^ 19 := Nat.mul_lt_mul'' ha hb
    _ = 10 ^ 38 := by norm_num
    _ < 2 ^ 128 := by norm_num

/-- When `a` and `b` share a sign, `|a - b| = ||a| - |b||`. -/
lemma abs_sub_eq_abs_sub_abs {a b : ℚ}
    (h : (0 ≤ a ∧ 0 ≤ b) ∨ (a ≤ 0 ∧ b ≤ 0)) :
    |a - b| = |(|a| - |b|)| := by
  rcases h with ⟨ha, hb⟩ | ⟨ha, hb⟩
  · rw [abs_of_nonneg ha, abs_of_nonneg hb]
  · rw [abs_of_nonpos ha, abs_of_nonpos hb]
    rw [show a - b = -((-a) - (-b)) from by ring, abs_neg, show -a - -b = -a + b from by ring,
        show (-a + b : ℚ) = -(a - b) from by ring, abs_neg]

/-- When result's sign agrees with truth's sign, `|result - truth| = ||result| - |truth||`. -/
lemma abs_diff_eq_abs_sub_abs_of_sign_aligned (result : Number) (truth : ℚ)
    (h_neg_imp : result.negative_ = true → truth ≤ 0)
    (h_nonneg_imp : result.negative_ = false → 0 ≤ truth) :
    |result.toRat - truth| = |(|result.toRat| - |truth|)| := by
  rcases h_neg : result.negative_ with _ | _
  · exact abs_sub_eq_abs_sub_abs (Or.inl
      ⟨Number.toRat_nonneg_of_nonnegative _ h_neg, h_nonneg_imp h_neg⟩)
  · exact abs_sub_eq_abs_sub_abs (Or.inr
      ⟨Number.toRat_nonpos_of_negative _ h_neg, h_neg_imp h_neg⟩)

/-- For a non-zero mantissa, `operator_neg` preserves the mantissa. -/
lemma Number.operator_neg_mantissa_of_ne (n : Number) (hne : n.mantissa_ ≠ 0) :
    n.operator_neg.mantissa_ = n.mantissa_ := by
  unfold Number.operator_neg
  have hm_bool : (n.mantissa_ == 0) = false := by
    rw [beq_eq_false_iff_ne]; exact hne
  rw [if_neg (by rw [hm_bool]; decide)]

/-- For a non-zero mantissa, `operator_neg` flips the sign bit. -/
lemma Number.operator_neg_negative_of_ne (n : Number) (hne : n.mantissa_ ≠ 0) :
    n.operator_neg.negative_ = !n.negative_ := by
  unfold Number.operator_neg
  have hm_bool : (n.mantissa_ == 0) = false := by
    rw [beq_eq_false_iff_ne]; exact hne
  rw [if_neg (by rw [hm_bool]; decide)]

/-- `operator_neg` preserves `isNormalized` (for non-zero mantissa, it flips the
sign bit but keeps the other fields; for zero mantissa, it returns `Number.zero`
which is normalized by the `n = Number.zero` disjunct). -/
lemma Number.operator_neg_isNormalized (n : Number) (h : n.isNormalized) :
    n.operator_neg.isNormalized := by
  unfold Number.operator_neg
  by_cases hm : n.mantissa_ == 0
  · rw [if_pos hm]; left; rfl
  · rw [if_neg hm]
    rcases h with h_zero | ⟨h1, h2, h3, h4, h5⟩
    · exfalso; apply hm
      have : n.mantissa_ = 0 := by rw [h_zero]; rfl
      rw [this]; decide
    · right
      refine ⟨h1, h2, h3, h4, h5⟩

/-- `Number.operator_neg` negates the rational value. For a non-zero mantissa, the
sign bit is flipped; for zero mantissa it returns `Number.zero` which has `toRat = 0`. -/
lemma Number.toRat_neg (n : Number) : n.operator_neg.toRat = -n.toRat := by
  unfold Number.operator_neg
  by_cases hm : n.mantissa_ == 0
  · rw [if_pos hm]
    have hm_eq : n.mantissa_ = 0 := by
      rw [beq_iff_eq] at hm; exact hm
    have hn_zero : n.toRat = 0 := Number.toRat_eq_zero_iff.mpr hm_eq
    rw [Number.toRat_zero, hn_zero, neg_zero]
  · rw [if_neg hm]
    -- The negated Number has the same mantissa and exponent, flipped negative bit.
    set n' : Number := { n with negative_ := !n.negative_ } with hn'_def
    have hn'_mant : n'.mantissa_ = n.mantissa_ := rfl
    have hn'_exp : n'.exponent_ = n.exponent_ := rfl
    have hn'_neg : n'.negative_ = !n.negative_ := rfl
    cases hneg : n.negative_
    · -- n.negative_ = false → n'.negative_ = true
      have hn'_neg_eq : n'.negative_ = true := by rw [hn'_neg, hneg]; rfl
      rw [Number.toRat_of_neg n' hn'_neg_eq]
      rw [hn'_mant, hn'_exp]
      rw [Number.toRat_of_nonneg n hneg]
    · -- n.negative_ = true → n'.negative_ = false
      have hn'_neg_eq : n'.negative_ = false := by rw [hn'_neg, hneg]; rfl
      rw [Number.toRat_of_nonneg n' hn'_neg_eq]
      rw [hn'_mant, hn'_exp]
      rw [Number.toRat_of_neg n hneg]
      ring

/-- Double negation on a nonzero-mantissa `Number` is the identity (the sign bit
flips twice). Shared by the four `Sub` rounding-bound files. -/
lemma neg_neg_of_mant_ne {y : Number} (hy_mant_ne : y.mantissa_ ≠ 0) :
    y.operator_neg.operator_neg = y := by
  obtain ⟨yn, ym, ye⟩ := y
  have hy_mant_ne' : ym ≠ 0 := hy_mant_ne
  have hbool : (ym == 0) = false := by
    rw [beq_eq_false_iff_ne]; exact hy_mant_ne'
  unfold Number.operator_neg
  simp only [hbool, Bool.false_eq_true, if_false]
  cases yn <;> rfl

/-- Subtraction as addition of the negation, on `toRat`. Shared by `Sub`. -/
lemma sub_eq_add_neg_toRat (x y : Number) :
    x.toRat - y.toRat = x.toRat + y.operator_neg.toRat := by
  rw [Number.toRat_neg]; ring

end XRPL.Model.Protocol
