import XRPL.Properties.Protocol.Number.Common.ToRatLemmas
import XRPL.Properties.Protocol.Number.Signum.Common.Proofs

/-!
The proof that `Number.operator_lt` is correct, together with the lemmas it is
built from. The headline theorem in `Compare.lean` delegates here.
-/

namespace XRPL.Model.Protocol

/-- Eliminates a `mantissa == 0` guard when the mantissa is zero. -/
private lemma ite_mantissa_eq_zero {α : Sort*} {m : UInt64} (hm : m = 0) (a b : α) :
    (if m == 0 then a else b) = a :=
  if_pos (beq_iff_eq.mpr hm)

/-- Eliminates a `mantissa == 0` guard when the mantissa is nonzero. -/
private lemma ite_mantissa_ne_zero {α : Sort*} {m : UInt64} (hm : m ≠ 0) (a b : α) :
    (if m == 0 then a else b) = b := by
  rw [beq_eq_false_iff_ne.mpr hm, if_neg Bool.false_ne_true]

/-- A non-negative number with a nonzero mantissa has a strictly positive value. -/
lemma Number.toRat_pos_of_not_negative (n : Number) (hneg : n.negative = false)
    (hm : n.mantissa ≠ 0) : 0 < n.toRat := by
  have h1 := Number.toRat_nonneg_of_nonnegative n hneg
  have h2 : n.toRat ≠ 0 := fun h => hm (Number.toRat_eq_zero_iff.mp h)
  exact h1.lt_of_ne' h2

/-- A negative number with a nonzero mantissa has a strictly negative value. -/
lemma Number.toRat_neg_of_negative' (n : Number) (hneg : n.negative = true)
    (hm : n.mantissa ≠ 0) : n.toRat < 0 := by
  have h1 := Number.toRat_nonpos_of_negative n hneg
  have h2 : n.toRat ≠ 0 := fun h => hm (Number.toRat_eq_zero_iff.mp h)
  exact h1.lt_of_ne h2

/-- Exponent dominance: between normalized nonzero values, the smaller exponent always means
the smaller magnitude. Normalization pins the mantissa inside one decade `[10^18, 10^19)`,
so no mantissa can make up for a smaller exponent. This is what lets `operator_lt` compare
exponents before mantissas. -/
lemma abs_toRat_lt_of_exp_lt (x y : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hxm : x.mantissa ≠ 0) (hym : y.mantissa ≠ 0)
    (hexp : x.exponent < y.exponent) :
    |x.toRat| < |y.toRat| := by
  rw [abs_toRat_eq, abs_toRat_eq]
  obtain ⟨_, hxmax⟩ := hx.mantissaBounds hxm
  obtain ⟨hymin, _⟩ := hy.mantissaBounds hym
  have hxm_le : (x.mantissa.toNat : ℚ) ≤ 9999999999999999999 := by
    have := UInt64.le_iff_toNat_le.mp hxmax
    rw [largeRange_max_val] at this
    exact_mod_cast this
  have hym_ge : (1000000000000000000 : ℚ) ≤ (y.mantissa.toNat : ℚ) := by
    have := UInt64.le_iff_toNat_le.mp hymin
    rw [largeRange_min_val] at this
    exact_mod_cast this
  have hpow_x : (0 : ℚ) < 10 ^ x.exponent := zpow_pos (by norm_num) _
  have hpow_y : (0 : ℚ) < 10 ^ y.exponent := zpow_pos (by norm_num) _
  calc (x.mantissa.toNat : ℚ) * 10 ^ x.exponent
      ≤ 9999999999999999999 * 10 ^ x.exponent :=
        mul_le_mul_of_nonneg_right hxm_le hpow_x.le
    _ < 1000000000000000000 * 10 ^ (x.exponent + 1) := by
        rw [zpow_add_one₀ (by norm_num : (10 : ℚ) ≠ 0),
            show (1000000000000000000 : ℚ) * (10 ^ x.exponent * 10)
              = 10000000000000000000 * 10 ^ x.exponent from by ring]
        exact mul_lt_mul_of_pos_right (by norm_num) hpow_x
    _ ≤ 1000000000000000000 * 10 ^ y.exponent := by
        apply mul_le_mul_of_nonneg_left _ (by norm_num)
        exact zpow_le_zpow_right₀ (by norm_num) (by omega)
    _ ≤ (y.mantissa.toNat : ℚ) * 10 ^ y.exponent :=
        mul_le_mul_of_nonneg_right hym_ge hpow_y.le

/-- With equal exponents, comparing magnitudes is exactly comparing mantissas. -/
lemma abs_toRat_lt_iff_of_exp_eq (x y : Number)
    (hexp : x.exponent = y.exponent) :
    |x.toRat| < |y.toRat| ↔ x.mantissa < y.mantissa := by
  rw [abs_toRat_eq, abs_toRat_eq, hexp]
  have hpow : (0 : ℚ) < 10 ^ y.exponent := zpow_pos (by norm_num) _
  rw [mul_lt_mul_iff_of_pos_right hpow, Nat.cast_lt]
  exact UInt64.lt_iff_toNat_lt.symm

/-- The signed value of a negative `Number` is the negated magnitude. -/
private lemma toRat_eq_neg_abs (n : Number) (hneg : n.negative = true) :
    n.toRat = -|n.toRat| := by
  rw [abs_of_nonpos (Number.toRat_nonpos_of_negative n hneg)]
  ring

/-- The signed value of a non-negative `Number` is its magnitude. -/
private lemma toRat_eq_abs (n : Number) (hneg : n.negative = false) :
    n.toRat = |n.toRat| := by
  rw [abs_of_nonneg (Number.toRat_nonneg_of_nonnegative n hneg)]

/-- Between negative numbers, the smaller magnitude is the larger value. -/
private lemma toRat_lt_of_abs_lt_of_neg {x y : Number} (hx : x.negative = true)
    (hy : y.negative = true) (h : |y.toRat| < |x.toRat|) : x.toRat < y.toRat := by
  rw [toRat_eq_neg_abs x hx, toRat_eq_neg_abs y hy]
  exact neg_lt_neg h

/-- Between non-negative numbers, the smaller magnitude is the smaller value. -/
private lemma toRat_lt_of_abs_lt_of_nonneg {x y : Number} (hx : x.negative = false)
    (hy : y.negative = false) (h : |x.toRat| < |y.toRat|) : x.toRat < y.toRat := by
  rw [toRat_eq_abs x hx, toRat_eq_abs y hy]
  exact h

/-- **Correctness of `operator_lt`**: on normalized values the comparison agrees with the
order of the exact rational values. The case split retraces the branches of `operator_lt`:
signs differ, one side is zero, exponents differ, exponents equal. -/
theorem operator_lt_iff_proof (x y : Number)
    (hx : x.isNormalized) (hy : y.isNormalized) :
    x.operator_lt y = true ↔ x.toRat < y.toRat := by
  by_cases hsx : x.negative = true
  · by_cases hsy : y.negative = true
    · -- both negative: nonzero mantissas, value = −magnitude.
      have hxm : x.mantissa ≠ 0 := Number.mantissa_ne_zero_of_negative x hx hsx
      have hym : y.mantissa ≠ 0 := Number.mantissa_ne_zero_of_negative y hy hsy
      have hxs := toRat_eq_neg_abs x hsx
      have hys := toRat_eq_neg_abs y hsy
      unfold Number.operator_lt
      rw [hsx, hsy]
      simp only [bne_self_eq_false, Bool.false_eq_true, if_false]
      rw [ite_mantissa_ne_zero hxm, ite_mantissa_ne_zero hym]
      rcases lt_trichotomy x.exponent y.exponent with hexp | hexp | hexp
      · -- ex < ey: |x| < |y| so x > y; the operator returns !lneg = false.
        rw [if_neg (by omega : ¬ x.exponent > y.exponent), if_pos hexp,
            show (!(true : Bool)) = false from rfl]
        simp only [Bool.false_eq_true, false_iff, not_lt]
        have habs := abs_toRat_lt_of_exp_lt x y hx hy hxm hym hexp
        exact (toRat_lt_of_abs_lt_of_neg hsy hsx habs).le
      · -- ex = ey: sign-aware mantissa compare.
        rw [if_neg (by omega : ¬ x.exponent > y.exponent),
            if_neg (by omega : ¬ x.exponent < y.exponent), if_pos trivial,
            decide_eq_true_iff]
        rw [hxs, hys, neg_lt_neg_iff, abs_toRat_lt_iff_of_exp_eq y x hexp.symm]
      · -- ex > ey: |x| > |y| so x < y; the operator returns lneg = true.
        rw [if_pos hexp]
        constructor
        · intro _
          have habs := abs_toRat_lt_of_exp_lt y x hy hx hym hxm hexp
          exact toRat_lt_of_abs_lt_of_neg hsx hsy habs
        · intro _
          rfl
    · -- x negative, y non-negative: x < y always.
      have hsyf : y.negative = false := Bool.not_eq_true _ |>.mp hsy
      have hxm : x.mantissa ≠ 0 := Number.mantissa_ne_zero_of_negative x hx hsx
      have heval : x.operator_lt y = true := by
        unfold Number.operator_lt
        rw [hsx, hsyf]
        rfl
      rw [heval]
      simp only [true_iff]
      have h1 := Number.toRat_neg_of_negative' x hsx hxm
      have h2 := Number.toRat_nonneg_of_nonnegative y hsyf
      linarith
  · have hsxf : x.negative = false := Bool.not_eq_true _ |>.mp hsx
    by_cases hsy : y.negative = true
    · -- x non-negative, y negative: never x < y.
      have hym : y.mantissa ≠ 0 := Number.mantissa_ne_zero_of_negative y hy hsy
      have heval : x.operator_lt y = false := by
        unfold Number.operator_lt
        rw [hsxf, hsy]
        rfl
      rw [heval]
      simp only [Bool.false_eq_true, false_iff, not_lt]
      have h1 := Number.toRat_nonneg_of_nonnegative x hsxf
      have h2 := Number.toRat_neg_of_negative' y hsy hym
      linarith
    · -- both non-negative.
      have hsyf : y.negative = false := Bool.not_eq_true _ |>.mp hsy
      have hxs := toRat_eq_abs x hsxf
      have hys := toRat_eq_abs y hsyf
      unfold Number.operator_lt
      rw [hsxf, hsyf]
      simp only [bne_self_eq_false, Bool.false_eq_true, if_false]
      by_cases hxm : x.mantissa = 0
      · -- x = 0: x < y iff y is nonzero.
        rw [ite_mantissa_eq_zero hxm, decide_eq_true_iff]
        have hx0 : x.toRat = 0 := Number.toRat_eq_zero_iff.mpr hxm
        rw [hx0]
        constructor
        · intro h
          have hym : y.mantissa ≠ 0 := by
            intro hc
            rw [hc] at h
            exact absurd h (by decide)
          exact Number.toRat_pos_of_not_negative y hsyf hym
        · intro h
          have hym : y.mantissa ≠ 0 :=
            fun hc => absurd (Number.toRat_eq_zero_iff.mpr hc) (ne_of_gt h)
          exact UInt64.pos_iff_ne_zero.mpr hym
      · rw [ite_mantissa_ne_zero hxm]
        by_cases hym : y.mantissa = 0
        · -- y = 0: a positive x is never below it.
          rw [ite_mantissa_eq_zero hym]
          have hy0 : y.toRat = 0 := Number.toRat_eq_zero_iff.mpr hym
          rw [hy0]
          simp only [Bool.false_eq_true, false_iff, not_lt]
          exact le_of_lt (Number.toRat_pos_of_not_negative x hsxf hxm)
        · rw [ite_mantissa_ne_zero hym]
          rcases lt_trichotomy x.exponent y.exponent with hexp | hexp | hexp
          · -- ex < ey: |x| < |y| so x < y; the operator returns !lneg = true.
            rw [if_neg (by omega : ¬ x.exponent > y.exponent), if_pos hexp,
                show (!(false : Bool)) = true from rfl]
            simp only [true_iff]
            have habs := abs_toRat_lt_of_exp_lt x y hx hy hxm hym hexp
            exact toRat_lt_of_abs_lt_of_nonneg hsxf hsyf habs
          · -- ex = ey: mantissa compare.
            rw [if_neg (by omega : ¬ x.exponent > y.exponent),
                if_neg (by omega : ¬ x.exponent < y.exponent),
                decide_eq_true_iff]
            rw [hxs, hys, abs_toRat_lt_iff_of_exp_eq x y hexp]
          · -- ex > ey: |x| > |y| so never x < y; the operator returns lneg = false.
            rw [if_pos hexp]
            simp only [Bool.false_eq_true, false_iff, not_lt]
            have habs := abs_toRat_lt_of_exp_lt y x hy hx hym hxm hexp
            exact (toRat_lt_of_abs_lt_of_nonneg hsyf hsxf habs).le


end XRPL.Model.Protocol
