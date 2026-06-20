import XRPL.Properties.Protocol.Number.Common.Notation
import XRPL.Properties.Protocol.Number.Common.ToRatLemmas
import XRPL.Properties.Protocol.Number.Common.Rounding.DoRoundUp
import XRPL.Properties.Protocol.Number.Signum.Signum


namespace XRPL.Model.Protocol

/-- Strictly positive value: non-negative flag and nonzero mantissa. -/
lemma Number.toRat_pos_of_not_negative (n : Number) (hneg : n.negative_ = false)
    (hm : n.mantissa_ ≠ 0) : 0 < n.toRat := by
  have h1 := Number.toRat_nonneg_of_nonnegative n hneg
  have h2 : n.toRat ≠ 0 := fun h => hm (Number.toRat_eq_zero_iff.mp h)
  exact lt_of_le_of_ne h1 (Ne.symm h2)

/-- Strictly negative value: negative flag and nonzero mantissa. -/
lemma Number.toRat_neg_of_negative' (n : Number) (hneg : n.negative_ = true)
    (hm : n.mantissa_ ≠ 0) : n.toRat < 0 := by
  have h1 := Number.toRat_nonpos_of_negative n hneg
  have h2 : n.toRat ≠ 0 := fun h => hm (Number.toRat_eq_zero_iff.mp h)
  exact lt_of_le_of_ne h1 h2

/-- Exponent dominance: for normalized nonzero values, a strictly smaller
exponent means a strictly smaller magnitude (`m ∈ [10^18, 10^19)` forces it). -/
lemma abs_toRat_lt_of_exp_lt (x y : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hxm : x.mantissa_ ≠ 0) (hym : y.mantissa_ ≠ 0)
    (hexp : x.exponent_ < y.exponent_) :
    |x.toRat| < |y.toRat| := by
  rw [abs_toRat_eq, abs_toRat_eq]
  obtain ⟨_, hxmax⟩ := hx.mantissaBounds hxm
  obtain ⟨hymin, _⟩ := hy.mantissaBounds hym
  have hxm_le : (x.mantissa_.toNat : ℚ) ≤ 9999999999999999999 := by
    have := UInt64.le_iff_toNat_le.mp hxmax
    rw [largeRange_max_val] at this
    exact_mod_cast this
  have hym_ge : (1000000000000000000 : ℚ) ≤ (y.mantissa_.toNat : ℚ) := by
    have := UInt64.le_iff_toNat_le.mp hymin
    rw [largeRange_min_val] at this
    exact_mod_cast this
  have hpow_x : (0 : ℚ) < 10 ^ x.exponent_ := zpow_pos (by norm_num) _
  have hpow_y : (0 : ℚ) < 10 ^ y.exponent_ := zpow_pos (by norm_num) _
  calc (x.mantissa_.toNat : ℚ) * 10 ^ x.exponent_
      ≤ 9999999999999999999 * 10 ^ x.exponent_ :=
        mul_le_mul_of_nonneg_right hxm_le (le_of_lt hpow_x)
    _ < 1000000000000000000 * 10 ^ (x.exponent_ + 1) := by
        rw [zpow_add_one₀ (by norm_num : (10 : ℚ) ≠ 0),
            show (1000000000000000000 : ℚ) * (10 ^ x.exponent_ * 10)
              = 10000000000000000000 * 10 ^ x.exponent_ from by ring]
        exact mul_lt_mul_of_pos_right (by norm_num) hpow_x
    _ ≤ 1000000000000000000 * 10 ^ y.exponent_ := by
        apply mul_le_mul_of_nonneg_left _ (by norm_num)
        exact zpow_le_zpow_right₀ (by norm_num) (by omega)
    _ ≤ (y.mantissa_.toNat : ℚ) * 10 ^ y.exponent_ :=
        mul_le_mul_of_nonneg_right hym_ge (le_of_lt hpow_y)

/-- Same exponent: magnitude order is mantissa order. -/
lemma abs_toRat_lt_iff_of_exp_eq (x y : Number)
    (hexp : x.exponent_ = y.exponent_) :
    |x.toRat| < |y.toRat| ↔ x.mantissa_ < y.mantissa_ := by
  rw [abs_toRat_eq, abs_toRat_eq, hexp]
  have hpow : (0 : ℚ) < 10 ^ y.exponent_ := zpow_pos (by norm_num) _
  constructor
  · intro h
    have := lt_of_mul_lt_mul_right h (le_of_lt hpow)
    rw [UInt64.lt_iff_toNat_lt]
    exact_mod_cast this
  · intro h
    have h' : (x.mantissa_.toNat : ℚ) < y.mantissa_.toNat := by
      exact_mod_cast UInt64.lt_iff_toNat_lt.mp h
    exact mul_lt_mul_of_pos_right h' hpow

/-- The signed value of a negative `Number` is the negated magnitude. -/
private lemma toRat_eq_neg_abs (n : Number) (hneg : n.negative_ = true) :
    n.toRat = -|n.toRat| := by
  rw [abs_of_nonpos (Number.toRat_nonpos_of_negative n hneg)]
  ring

/-- The signed value of a non-negative `Number` is its magnitude. -/
private lemma toRat_eq_abs (n : Number) (hneg : n.negative_ = false) :
    n.toRat = |n.toRat| := by
  rw [abs_of_nonneg (Number.toRat_nonneg_of_nonnegative n hneg)]

/-- **Correctness of `operator_lt`**: on normalized values the comparison agrees
with the rational order. -/
theorem operator_lt_iff_proof (x y : Number)
    (hx : x.isNormalized) (hy : y.isNormalized) :
    x.operator_lt y = true ↔ x.toRat < y.toRat := by
  by_cases hsx : x.negative_ = true
  · by_cases hsy : y.negative_ = true
    · -- both negative: nonzero mantissas, value = −magnitude.
      have hxm : x.mantissa_ ≠ 0 := Number.mantissa_ne_zero_of_negative x hx hsx
      have hym : y.mantissa_ ≠ 0 := Number.mantissa_ne_zero_of_negative y hy hsy
      have hxs := toRat_eq_neg_abs x hsx
      have hys := toRat_eq_neg_abs y hsy
      unfold Number.operator_lt
      rw [hsx, hsy]
      simp only [bne_self_eq_false, Bool.false_eq_true, if_false]
      rw [show (x.mantissa_ == 0) = false from beq_eq_false_iff_ne.mpr hxm,
          show (y.mantissa_ == 0) = false from beq_eq_false_iff_ne.mpr hym]
      simp only [Bool.false_eq_true, if_false]
      rcases lt_trichotomy x.exponent_ y.exponent_ with hexp | hexp | hexp
      · -- ex < ey: |x| < |y| so x > y; the operator returns !lneg = false.
        rw [if_neg (by omega : ¬ x.exponent_ > y.exponent_), if_pos hexp,
            show (!(true : Bool)) = false from rfl]
        simp only [Bool.false_eq_true, false_iff, not_lt]
        have habs := abs_toRat_lt_of_exp_lt x y hx hy hxm hym hexp
        rw [hxs, hys]
        linarith
      · -- ex = ey: sign-aware mantissa compare.
        rw [if_neg (by omega : ¬ x.exponent_ > y.exponent_),
            if_neg (by omega : ¬ x.exponent_ < y.exponent_), if_pos trivial,
            decide_eq_true_iff]
        constructor
        · intro h
          have habs := (abs_toRat_lt_iff_of_exp_eq y x hexp.symm).mpr h
          rw [hxs, hys]
          linarith
        · intro h
          rw [hxs, hys] at h
          exact (abs_toRat_lt_iff_of_exp_eq y x hexp.symm).mp (by linarith)
      · -- ex > ey: |x| > |y| so x < y; the operator returns lneg = true.
        rw [if_pos hexp]
        constructor
        · intro _
          have habs := abs_toRat_lt_of_exp_lt y x hy hx hym hxm hexp
          rw [hxs, hys]
          linarith
        · intro _
          rfl
    · -- x negative, y non-negative: x < y always.
      have hsyf : y.negative_ = false := Bool.not_eq_true _ |>.mp hsy
      have hxm : x.mantissa_ ≠ 0 := Number.mantissa_ne_zero_of_negative x hx hsx
      have heval : x.operator_lt y = true := by
        unfold Number.operator_lt
        rw [hsx, hsyf]
        rfl
      rw [heval]
      simp only [true_iff]
      have h1 := Number.toRat_neg_of_negative' x hsx hxm
      have h2 := Number.toRat_nonneg_of_nonnegative y hsyf
      linarith
  · have hsxf : x.negative_ = false := Bool.not_eq_true _ |>.mp hsx
    by_cases hsy : y.negative_ = true
    · -- x non-negative, y negative: never x < y.
      have hym : y.mantissa_ ≠ 0 := Number.mantissa_ne_zero_of_negative y hy hsy
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
      have hsyf : y.negative_ = false := Bool.not_eq_true _ |>.mp hsy
      have hxs := toRat_eq_abs x hsxf
      have hys := toRat_eq_abs y hsyf
      unfold Number.operator_lt
      rw [hsxf, hsyf]
      simp only [bne_self_eq_false, Bool.false_eq_true, if_false]
      by_cases hxm : x.mantissa_ = 0
      · -- x = 0: x < y iff y is nonzero.
        rw [show (x.mantissa_ == 0) = true from beq_iff_eq.mpr hxm]
        simp only [if_true]
        rw [decide_eq_true_iff]
        have hx0 : x.toRat = 0 := Number.toRat_eq_zero_iff.mpr hxm
        rw [hx0]
        constructor
        · intro h
          have hym : y.mantissa_ ≠ 0 := by
            intro hc
            rw [hc] at h
            exact absurd h (by decide)
          exact Number.toRat_pos_of_not_negative y hsyf hym
        · intro h
          have hym : y.mantissa_ ≠ 0 :=
            fun hc => absurd (Number.toRat_eq_zero_iff.mpr hc) (ne_of_gt h)
          change (0 : UInt64) < y.mantissa_
          rw [UInt64.lt_iff_toNat_lt, show (0 : UInt64).toNat = 0 from rfl]
          have hne : y.mantissa_.toNat ≠ 0 := by
            intro hc
            apply hym
            rw [← UInt64.toNat_inj, hc]
            rfl
          omega
      · rw [show (x.mantissa_ == 0) = false from beq_eq_false_iff_ne.mpr hxm]
        simp only [Bool.false_eq_true, if_false]
        by_cases hym : y.mantissa_ = 0
        · -- y = 0: a positive x is never below it.
          rw [show (y.mantissa_ == 0) = true from beq_iff_eq.mpr hym]
          simp only [if_true]
          have hy0 : y.toRat = 0 := Number.toRat_eq_zero_iff.mpr hym
          rw [hy0]
          simp only [Bool.false_eq_true, false_iff, not_lt]
          exact le_of_lt (Number.toRat_pos_of_not_negative x hsxf hxm)
        · rw [show (y.mantissa_ == 0) = false from beq_eq_false_iff_ne.mpr hym]
          simp only [Bool.false_eq_true, if_false]
          rcases lt_trichotomy x.exponent_ y.exponent_ with hexp | hexp | hexp
          · -- ex < ey: |x| < |y| so x < y; the operator returns !lneg = true.
            rw [if_neg (by omega : ¬ x.exponent_ > y.exponent_), if_pos hexp,
                show (!(false : Bool)) = true from rfl]
            simp only [true_iff]
            have habs := abs_toRat_lt_of_exp_lt x y hx hy hxm hym hexp
            rw [hxs, hys]
            linarith
          · -- ex = ey: mantissa compare.
            rw [if_neg (by omega : ¬ x.exponent_ > y.exponent_),
                if_neg (by omega : ¬ x.exponent_ < y.exponent_),
                decide_eq_true_iff]
            constructor
            · intro h
              have habs := (abs_toRat_lt_iff_of_exp_eq x y hexp).mpr h
              rw [hxs, hys]
              linarith
            · intro h
              rw [hxs, hys] at h
              exact (abs_toRat_lt_iff_of_exp_eq x y hexp).mp (by linarith)
          · -- ex > ey: |x| > |y| so never x < y; the operator returns lneg = false.
            rw [if_pos hexp]
            simp only [Bool.false_eq_true, false_iff, not_lt]
            have habs := abs_toRat_lt_of_exp_lt y x hy hx hym hxm hexp
            rw [hxs, hys]
            linarith

/-- **Correctness of `operator_le`**. -/
theorem operator_le_iff_proof (x y : Number)
    (hx : x.isNormalized) (hy : y.isNormalized) :
    x.operator_le y = true ↔ x.toRat ≤ y.toRat := by
  constructor
  · intro h
    by_contra hc
    push_neg at hc
    have hbt := (operator_lt_iff_proof y x hy hx).mpr hc
    unfold Number.operator_le at h
    rw [hbt] at h
    exact absurd h (by decide)
  · intro h
    unfold Number.operator_le
    have hbf : y.operator_lt x = false := by
      cases hb : y.operator_lt x
      · rfl
      · exact absurd ((operator_lt_iff_proof y x hy hx).mp hb) (not_lt.mpr h)
    rw [hbf]
    rfl

/-- **Correctness of `operator_gt`**. -/
theorem operator_gt_iff_proof (x y : Number)
    (hx : x.isNormalized) (hy : y.isNormalized) :
    x.operator_gt y = true ↔ y.toRat < x.toRat := by
  unfold Number.operator_gt
  exact operator_lt_iff_proof y x hy hx

/-- **Correctness of `operator_ge`**. -/
theorem operator_ge_iff_proof (x y : Number)
    (hx : x.isNormalized) (hy : y.isNormalized) :
    x.operator_ge y = true ↔ y.toRat ≤ x.toRat := by
  constructor
  · intro h
    by_contra hc
    push_neg at hc
    have hbt := (operator_lt_iff_proof x y hx hy).mpr hc
    unfold Number.operator_ge at h
    rw [hbt] at h
    exact absurd h (by decide)
  · intro h
    unfold Number.operator_ge
    have hbf : x.operator_lt y = false := by
      cases hb : x.operator_lt y
      · rfl
      · exact absurd ((operator_lt_iff_proof x y hx hy).mp hb) (not_lt.mpr h)
    rw [hbf]
    rfl

/-- **Correctness of `operator_eq`**: normalized representations are canonical,
so field equality decides rational equality. -/
theorem operator_eq_iff_proof (x y : Number)
    (hx : x.isNormalized) (hy : y.isNormalized) :
    x.operator_eq y = true ↔ x.toRat = y.toRat := by
  constructor
  · intro h
    unfold Number.operator_eq at h
    simp only [Bool.and_eq_true, beq_iff_eq] at h
    obtain ⟨⟨hs, hm⟩, he⟩ := h
    have hxy : x = y := by
      obtain ⟨xn, xm, xe⟩ := x
      obtain ⟨yn, ym, ye⟩ := y
      simp only at hs hm he
      rw [hs, hm, he]
    rw [hxy]
  · intro h
    by_cases hxm : x.mantissa_ = 0
    · -- both are Number.zero.
      have hxr : x.toRat = 0 := Number.toRat_eq_zero_iff.mpr hxm
      have hym : y.mantissa_ = 0 := Number.toRat_eq_zero_iff.mp (by rw [← h, hxr])
      rw [Number.eq_zero_of_mantissa_zero x hx hxm,
          Number.eq_zero_of_mantissa_zero y hy hym]
      rfl
    · have hym : y.mantissa_ ≠ 0 := by
        intro hc
        apply hxm
        exact Number.toRat_eq_zero_iff.mp (by rw [h]; exact Number.toRat_eq_zero_iff.mpr hc)
      -- signs agree (the value is strictly signed).
      have hsign : x.negative_ = y.negative_ := by
        by_cases hsx : x.negative_ = true
        · by_cases hsy : y.negative_ = true
          · rw [hsx, hsy]
          · exfalso
            have h1 := Number.toRat_neg_of_negative' x hsx hxm
            have h2 := Number.toRat_nonneg_of_nonnegative y (Bool.not_eq_true _ |>.mp hsy)
            rw [h] at h1
            linarith
        · by_cases hsy : y.negative_ = true
          · exfalso
            have h1 := Number.toRat_nonneg_of_nonnegative x (Bool.not_eq_true _ |>.mp hsx)
            have h2 := Number.toRat_neg_of_negative' y hsy hym
            rw [h] at h1
            linarith
          · rw [Bool.not_eq_true _ |>.mp hsx, Bool.not_eq_true _ |>.mp hsy]
      -- magnitudes agree, so the exponents agree by dominance.
      have habs : (x.mantissa_.toNat : ℚ) * 10 ^ x.exponent_
          = (y.mantissa_.toNat : ℚ) * 10 ^ y.exponent_ := by
        have := congrArg abs h
        rwa [abs_toRat_eq, abs_toRat_eq] at this
      have hexp : x.exponent_ = y.exponent_ := by
        by_contra hc
        rcases lt_or_gt_of_ne hc with hlt | hgt
        · have hd := abs_toRat_lt_of_exp_lt x y hx hy hxm hym hlt
          rw [abs_toRat_eq, abs_toRat_eq] at hd
          linarith
        · have hd := abs_toRat_lt_of_exp_lt y x hy hx hym hxm hgt
          rw [abs_toRat_eq, abs_toRat_eq] at hd
          linarith
      -- and then the mantissas agree.
      have hmant : x.mantissa_ = y.mantissa_ := by
        rw [hexp] at habs
        have hpow : ((10 : ℚ) ^ y.exponent_) ≠ 0 :=
          ne_of_gt (zpow_pos (by norm_num) _)
        have hcast : (x.mantissa_.toNat : ℚ) = y.mantissa_.toNat :=
          mul_right_cancel₀ hpow habs
        rw [← UInt64.toNat_inj]
        exact_mod_cast hcast
      unfold Number.operator_eq
      rw [hsign, hmant, hexp]
      simp

/-- **Correctness of `operator_ne`**. -/
theorem operator_ne_iff_proof (x y : Number)
    (hx : x.isNormalized) (hy : y.isNormalized) :
    x.operator_ne y = true ↔ x.toRat ≠ y.toRat := by
  constructor
  · intro h hc
    have hbt := (operator_eq_iff_proof x y hx hy).mpr hc
    unfold Number.operator_ne at h
    rw [hbt] at h
    exact absurd h (by decide)
  · intro h
    unfold Number.operator_ne
    have hbf : x.operator_eq y = false := by
      cases hb : x.operator_eq y
      · rfl
      · exact absurd ((operator_eq_iff_proof x y hx hy).mp hb) h
    rw [hbf]
    rfl

end XRPL.Model.Protocol
