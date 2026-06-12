import Mathlib.Tactic

import XRPL.Properties.Protocol.Number.Rounding.DoRoundUp

namespace XRPL.Model.Protocol

set_option linter.style.longLine false

/-! # `Number.normalize` is an identity for `doRoundUp` output

When the input satisfies `minMantissa ≤ m ≤ maxMantissa`, `minExponent ≤ e ≤ maxExponent`,
and `m > maxRep → m % 10 = 0`, `doNormalize` returns the input unchanged. -/

/-- `Guard.new` rounds to `-1` in `.to_nearest` mode. -/
lemma guard_new_round_neg_one : Guard.new.round .to_nearest = -1 := by
  unfold Guard.round Guard.new
  simp

/-- `Guard.new.push 0 = Guard.new`. -/
lemma guard_new_push_zero : Guard.new.push 0 = Guard.new := by
  unfold Guard.push Guard.new
  simp

/-- `doNormalize_scaleUp` is identity when `m ≥ minMantissa`. -/
lemma doNormalize_scaleUp_id (minMant m : UInt64) (e : Int)
    (h : minMant ≤ m) : doNormalize_scaleUp minMant m e = (m, e) := by
  unfold doNormalize_scaleUp
  have h_nlt : ¬ m < minMant := by
    rw [UInt64.lt_iff_toNat_lt]
    exact Nat.not_lt.mpr (UInt64.le_iff_toNat_le.mp h)
  have : ¬ (m < minMant ∧ minExponent < e) := by
    intro ⟨hlt, _⟩; exact h_nlt hlt
  rw [if_neg this]

/-- `doNormalize_scaleDown` is identity when `m ≤ maxMant`. -/
lemma doNormalize_scaleDown_id (maxMant m : UInt64) (e : Int) (g : Guard)
    (h : m ≤ maxMant) : doNormalize_scaleDown maxMant m e g = .ok (m, e, g) := by
  unfold doNormalize_scaleDown
  have h_ngt : ¬ m > maxMant := by
    change ¬ maxMant < m
    rw [UInt64.lt_iff_toNat_lt]
    exact Nat.not_lt.mpr (UInt64.le_iff_toNat_le.mp h)
  rw [dif_neg h_ngt]

/-- For `m ≥ minMantissa`, `e ≥ minExponent`, `e ≤ maxExponent`: `Guard.new.doRoundUp` in
`.to_nearest` mode returns `.ok { negative, m, e }`. -/
lemma guard_new_doRoundUp_id
    (negative : Bool) (m : UInt64) (e : Int) (loc : String)
    (hmin : largeRange.min ≤ m)
    (hexp : minExponent ≤ e)
    (hexp_le : e ≤ maxExponent) :
    Guard.new.doRoundUp negative m e largeRange.min largeRange.max .to_nearest loc
      = .ok { negative_ := negative, mantissa_ := m, exponent_ := e } := by
  unfold Guard.doRoundUp Guard.bringIntoRange Guard.doDropDigit
  simp only []
  rw [guard_new_round_neg_one]
  have h_round_false : ((-1 : Int) == 1 || ((-1 : Int) == 0 && m % 2 == 1)) = false := by rfl
  rw [h_round_false]
  simp only [Bool.false_and, if_false, Bool.false_eq_true]
  have h_no_resc : ¬ m < largeRange.min := by
    rw [UInt64.lt_iff_toNat_lt]; exact Nat.not_lt.mpr (UInt64.le_iff_toNat_le.mp hmin)
  rw [if_neg h_no_resc, if_neg (not_lt.mpr hexp), if_neg (not_lt.mpr hexp_le)]

/-- Key lemma: when `m > maxRep` AND `m ≤ maxMantissa` AND `m % 10 = 0`,
`m / 10 < minMantissa`. -/
lemma div_ten_lt_minMantissa
    {m : UInt64} (h_gt : m > maxRep) (h_le : m.toNat ≤ largeRange.max.toNat) :
    (m / 10) < largeRange.min := by
  rw [UInt64.lt_iff_toNat_lt, UInt64.toNat_div]
  have h_gt_nat : maxRep.toNat < m.toNat := UInt64.lt_iff_toNat_lt.mp h_gt
  rw [maxRep_val] at h_gt_nat
  rw [largeRange_max_val] at h_le
  rw [largeRange_min_val]
  have h10 : (10 : UInt64).toNat = 10 := rfl
  rw [h10]
  omega

/-- `(m / 10) * 10 = m` when `m % 10 = 0` (in UInt64). -/
lemma mul_div_ten_cancel {m : UInt64}
    (h_mod : m.toNat % 10 = 0)
    (h_le_max : m.toNat ≤ largeRange.max.toNat) :
    ((m / 10) * 10).toNat = m.toNat := by
  rw [UInt64.toNat_mul, UInt64.toNat_div]
  have h10 : (10 : UInt64).toNat = 10 := rfl
  rw [h10]
  rw [largeRange_max_val] at h_le_max
  have h_mul : m.toNat / 10 * 10 = m.toNat := by omega
  rw [h_mul]
  exact Nat.mod_eq_of_lt (by omega)

/-- For `m > maxRep`, `m ≤ maxMantissa`, `m % 10 = 0`, `e ≤ maxExponent`: inner doRoundUp after
`divu10` and `push 0` rescales back to `.ok (m, e)`. -/
lemma guard_new_doRoundUp_divu10
    (negative : Bool) (m : UInt64) (e : Int) (loc : String)
    (h_gt : m > maxRep)
    (h_le_max : m.toNat ≤ largeRange.max.toNat)
    (h_mod : m.toNat % 10 = 0)
    (hexp : minExponent ≤ e)
    (hexp_le : e ≤ maxExponent) :
    Guard.new.doRoundUp negative (m / 10) (e + 1) largeRange.min largeRange.max .to_nearest loc
      = .ok { negative_ := negative, mantissa_ := m, exponent_ := e } := by
  unfold Guard.doRoundUp Guard.bringIntoRange Guard.doDropDigit
  simp only []
  rw [guard_new_round_neg_one]
  have h_round_false :
      ((-1 : Int) == 1 || ((-1 : Int) == 0 && (m / 10) % 2 == 1)) = false := by rfl
  rw [h_round_false]
  simp only [Bool.false_and, if_false, Bool.false_eq_true]
  have h_resc : m / 10 < largeRange.min := div_ten_lt_minMantissa h_gt h_le_max
  have h_no_under : ¬ e + 1 - 1 < minExponent := by push_neg; linarith
  rw [if_pos h_resc, if_neg h_no_under]
  have h_mul : ((m / 10) * 10).toNat = m.toNat := mul_div_ten_cancel h_mod h_le_max
  have h_mul_eq : (m / 10) * 10 = m := UInt64.toNat_inj.mp h_mul
  have h_exp_eq : (e + 1 - 1 : ℤ) = e := by ring
  rw [h_mul_eq, h_exp_eq, if_neg (not_lt.mpr hexp_le)]

/-- `doNormalize` under the hypotheses returns `.ok` with the input Number. -/
lemma doNormalize_id
    (negative : Bool) (m : UInt64) (e : Int)
    (hmin : largeRange.min.toNat ≤ m.toNat)
    (hmax : m.toNat ≤ largeRange.max.toNat)
    (hexp : minExponent ≤ e)
    (hm_mod : m.toNat > maxRep.toNat → m.toNat % 10 = 0)
    (h_exp_le : e ≤ maxExponent)
    (h_mr_exp : m.toNat > maxRep.toNat → e < maxExponent) :
    doNormalize negative m e largeRange.min largeRange.max .to_nearest
      = .ok { negative_ := negative, mantissa_ := m, exponent_ := e } := by
  unfold doNormalize
  have hm_ne_zero : m ≠ 0 := by
    intro h
    rw [h] at hmin
    rw [largeRange_min_val] at hmin
    have : (0 : UInt64).toNat = 0 := rfl
    omega
  have h_eq_false : (m == 0) = false := by
    rw [beq_eq_false_iff_ne]; exact hm_ne_zero
  rw [h_eq_false]
  simp only [Bool.false_eq_true, if_false]
  have h_min_le : largeRange.min ≤ m := UInt64.le_iff_toNat_le.mpr hmin
  have h_max_le : m ≤ largeRange.max := UInt64.le_iff_toNat_le.mpr hmax
  have h_no_under_mant : ¬ m < largeRange.min := by
    rw [UInt64.lt_iff_toNat_lt]; exact Nat.not_lt.mpr hmin
  rw [doNormalize_scaleUp_id largeRange.min m e h_min_le]
  rw [doNormalize_scaleDown_id largeRange.max m e _ h_max_le]
  simp only []
  have h_no_under_exp : ¬ e < minExponent := not_lt.mpr hexp
  have h_check_false : (e < minExponent || m < largeRange.min) = false := by
    simp [h_no_under_exp, h_no_under_mant]
  rw [h_check_false]
  simp only [Bool.false_eq_true, if_false]
  by_cases h_mr : m > maxRep
  · have h_mod : m.toNat % 10 = 0 := hm_mod (UInt64.lt_iff_toNat_lt.mp h_mr)
    have h_mod_u : m % 10 = 0 := by
      apply UInt64.toNat_inj.mp
      rw [UInt64.toNat_mod]
      change m.toNat % (10 : UInt64).toNat = 0
      have h10 : (10 : UInt64).toNat = 10 := rfl
      rw [h10]; exact h_mod
    have h_nge_exp : ¬ e ≥ maxExponent :=
      Int.not_le.mpr (h_mr_exp (UInt64.lt_iff_toNat_lt.mp h_mr))
    -- Rewrite capAtMaxRep to .ok.
    have h_cap_eq : ∀ (g : Guard),
        doNormalize_capAtMaxRep m e g = .ok (m / 10, e + 1, g.push 0) := by
      intro g
      unfold doNormalize_capAtMaxRep
      rw [if_pos h_mr, if_neg h_nge_exp]
      simp [divu10, h_mod_u]
    cases negative
    · simp only [Bool.false_eq_true, if_false]
      rw [h_cap_eq Guard.new, guard_new_push_zero]
      simp only []
      have h_rup : Guard.new.doRoundUp false (m / 10) (e + 1) largeRange.min largeRange.max
          .to_nearest "Number::normalize 2"
          = .ok { negative_ := false, mantissa_ := m, exponent_ := e } :=
        guard_new_doRoundUp_divu10 false m e "Number::normalize 2" h_mr hmax h_mod hexp h_exp_le
      rw [h_rup]; rfl
    · simp only [if_true]
      have h_push_sn : Guard.new.set_negative.push 0 = Guard.new.set_negative := by
        unfold Guard.push Guard.set_negative Guard.new; simp
      rw [h_cap_eq Guard.new.set_negative, h_push_sn]
      simp only []
      have h_round_sn : Guard.new.set_negative.round .to_nearest = -1 := by
        unfold Guard.round Guard.set_negative Guard.new; simp
      have h_rup : Guard.new.set_negative.doRoundUp true (m / 10) (e + 1) largeRange.min largeRange.max
          .to_nearest "Number::normalize 2"
          = .ok { negative_ := true, mantissa_ := m, exponent_ := e } := by
        unfold Guard.doRoundUp Guard.bringIntoRange Guard.doDropDigit
        simp only []
        rw [h_round_sn]
        have h_round_false :
            ((-1 : Int) == 1 || ((-1 : Int) == 0 && (m / 10) % 2 == 1)) = false := by rfl
        rw [h_round_false]
        simp only [Bool.false_and, if_false, Bool.false_eq_true]
        have h_resc : m / 10 < largeRange.min := div_ten_lt_minMantissa h_mr hmax
        rw [if_pos h_resc]
        have h_no_under : ¬ e + 1 - 1 < minExponent := by intro h; omega
        rw [if_neg h_no_under]
        have h_mul : ((m / 10) * 10).toNat = m.toNat := mul_div_ten_cancel h_mod hmax
        have h_mul_eq : (m / 10) * 10 = m := UInt64.toNat_inj.mp h_mul
        have h_exp_eq : (e + 1 - 1 : ℤ) = e := by ring
        rw [h_mul_eq, h_exp_eq]
        rw [if_neg (not_lt.mpr h_exp_le)]
      rw [h_rup]; rfl
  · -- m ≤ maxRep branch: capAtMaxRep is identity.
    have h_cap_eq : ∀ (g : Guard),
        doNormalize_capAtMaxRep m e g = .ok (m, e, g) := by
      intro g
      unfold doNormalize_capAtMaxRep
      rw [if_neg h_mr]
    cases negative
    · simp only [Bool.false_eq_true, if_false]
      rw [h_cap_eq Guard.new]
      simp only []
      have h_rup : Guard.new.doRoundUp false m e largeRange.min largeRange.max .to_nearest
          "Number::normalize 2"
          = .ok { negative_ := false, mantissa_ := m, exponent_ := e } :=
        guard_new_doRoundUp_id false m e "Number::normalize 2" h_min_le hexp h_exp_le
      rw [h_rup]; rfl
    · simp only [if_true]
      rw [h_cap_eq Guard.new.set_negative]
      simp only []
      have h_round_sn : Guard.new.set_negative.round .to_nearest = -1 := by
        unfold Guard.round Guard.set_negative Guard.new; simp
      have h_rup : Guard.new.set_negative.doRoundUp true m e largeRange.min largeRange.max .to_nearest
          "Number::normalize 2"
          = .ok { negative_ := true, mantissa_ := m, exponent_ := e } := by
        unfold Guard.doRoundUp Guard.bringIntoRange Guard.doDropDigit
        simp only []
        rw [h_round_sn]
        have h_round_false :
            ((-1 : Int) == 1 || ((-1 : Int) == 0 && m % 2 == 1)) = false := by rfl
        rw [h_round_false]
        simp only [Bool.false_and, if_false, Bool.false_eq_true]
        rw [if_neg h_no_under_mant, if_neg (not_lt.mpr hexp), if_neg (not_lt.mpr h_exp_le)]
      rw [h_rup]; rfl

/-- Non-zero result mantissa implies non-zero input mantissa. -/
lemma Number.normalize_mantissa_ne_zero_of_result {n result : Number}
    {minMant maxMant : UInt64} {mode : rounding_mode}
    (hok : n.normalize minMant maxMant mode = .ok result)
    (hres : result.mantissa_ ≠ 0) :
    n.mantissa_ ≠ 0 := by
  intro h_zero
  unfold Number.normalize doNormalize at hok
  have h_cond : (n.mantissa_ == 0) = true := by rw [beq_iff_eq]; exact h_zero
  simp only [h_cond, if_true] at hok
  have h_res_eq : Number.zero = result := Except.ok.inj hok
  rw [← h_res_eq] at hres
  exact hres rfl

/-- `normalize` is the identity on inputs satisfying the doRoundUp output invariants. -/
lemma Number.normalize_eq_of_invariants {n result : Number}
    (h_min : largeRange.min.toNat ≤ n.mantissa_.toNat)
    (h_max : n.mantissa_.toNat ≤ largeRange.max.toNat)
    (h_exp : minExponent ≤ n.exponent_)
    (h_mod : n.mantissa_.toNat > maxRep.toNat → n.mantissa_.toNat % 10 = 0)
    (hok : n.normalize largeRange.min largeRange.max .to_nearest = .ok result) :
    result = n := by
  have h_exp_le : n.exponent_ ≤ maxExponent := by
    by_contra h
    push_neg at h
    have hm_ne_zero : n.mantissa_ ≠ 0 := by
      intro heq; rw [heq] at h_min; rw [largeRange_min_val] at h_min
      rw [show (0 : UInt64).toNat = 0 from rfl] at h_min; omega
    have h_min_le : largeRange.min ≤ n.mantissa_ := UInt64.le_iff_toNat_le.mpr h_min
    have h_max_le : n.mantissa_ ≤ largeRange.max := UInt64.le_iff_toNat_le.mpr h_max
    have h_no_under_mant : ¬ n.mantissa_ < largeRange.min := by
      rw [UInt64.lt_iff_toNat_lt]; exact Nat.not_lt.mpr h_min
    have h_no_under_exp : ¬ n.exponent_ < minExponent := not_lt.mpr h_exp
    unfold Number.normalize doNormalize at hok
    rw [beq_eq_false_iff_ne.mpr hm_ne_zero] at hok
    simp only [Bool.false_eq_true, if_false] at hok
    rw [doNormalize_scaleUp_id largeRange.min n.mantissa_ n.exponent_ h_min_le] at hok
    rw [doNormalize_scaleDown_id largeRange.max n.mantissa_ n.exponent_ _ h_max_le] at hok
    simp only [] at hok
    have h_check_false : (n.exponent_ < minExponent || n.mantissa_ < largeRange.min) = false := by
      simp [h_no_under_exp, h_no_under_mant]
    rw [h_check_false] at hok
    simp only [Bool.false_eq_true, if_false] at hok
    by_cases h_mr : n.mantissa_ > maxRep
    · rw [show doNormalize_capAtMaxRep n.mantissa_ n.exponent_
          (if n.negative_ then Guard.new.set_negative else Guard.new)
          = .error "Number::normalize 1.5" from by
        unfold doNormalize_capAtMaxRep; rw [if_pos h_mr, if_pos (le_of_lt h)]] at hok
      exact absurd hok (by intro h; cases h)
    · cases hn : n.negative_
      · simp only [hn, Bool.false_eq_true, if_false] at hok
        rw [show doNormalize_capAtMaxRep n.mantissa_ n.exponent_ Guard.new
            = .ok (n.mantissa_, n.exponent_, Guard.new) from by
          unfold doNormalize_capAtMaxRep; rw [if_neg h_mr]] at hok
        simp only [] at hok
        have h_rup_err : Guard.new.doRoundUp false n.mantissa_ n.exponent_
            largeRange.min largeRange.max .to_nearest "Number::normalize 2"
            = .error "Number::normalize 2" := by
          unfold Guard.doRoundUp Guard.bringIntoRange Guard.doDropDigit; simp only []
          rw [guard_new_round_neg_one]
          have : ((-1 : Int) == 1 || ((-1 : Int) == 0 && n.mantissa_ % 2 == 1)) = false := by rfl
          rw [this]; simp only [Bool.false_and, if_false, Bool.false_eq_true]
          rw [if_neg h_no_under_mant, if_neg h_no_under_exp, if_pos h]
        rw [h_rup_err] at hok
        exact absurd hok (by intro hc; cases hc)
      · simp only [hn, if_true] at hok
        rw [show doNormalize_capAtMaxRep n.mantissa_ n.exponent_ Guard.new.set_negative
            = .ok (n.mantissa_, n.exponent_, Guard.new.set_negative) from by
          unfold doNormalize_capAtMaxRep; rw [if_neg h_mr]] at hok
        simp only [] at hok
        have h_rup_err : Guard.new.set_negative.doRoundUp true n.mantissa_ n.exponent_
            largeRange.min largeRange.max .to_nearest "Number::normalize 2"
            = .error "Number::normalize 2" := by
          unfold Guard.doRoundUp Guard.bringIntoRange Guard.doDropDigit; simp only []
          rw [show Guard.new.set_negative.round .to_nearest = -1 from by
            unfold Guard.round Guard.set_negative Guard.new; simp]
          have : ((-1 : Int) == 1 || ((-1 : Int) == 0 && n.mantissa_ % 2 == 1)) = false := by rfl
          rw [this]; simp only [Bool.false_and, if_false, Bool.false_eq_true]
          rw [if_neg h_no_under_mant, if_neg h_no_under_exp, if_pos h]
        rw [h_rup_err] at hok
        exact absurd hok (by intro hc; cases hc)
  have h_mr_exp : n.mantissa_.toNat > maxRep.toNat → n.exponent_ < maxExponent := by
    intro h_mr
    by_contra h_ge
    push_neg at h_ge
    have h_eq_max : n.exponent_ = maxExponent := Int.le_antisymm h_exp_le h_ge
    unfold Number.normalize at hok
    unfold doNormalize at hok
    have hm_ne_zero : n.mantissa_ ≠ 0 := by
      intro h; rw [h] at h_mr; rw [show (0 : UInt64).toNat = 0 from rfl] at h_mr
      exact absurd h_mr (by rw [maxRep_val]; omega)
    rw [beq_eq_false_iff_ne.mpr hm_ne_zero] at hok
    simp only [Bool.false_eq_true, if_false] at hok
    have h_min_le : largeRange.min ≤ n.mantissa_ := UInt64.le_iff_toNat_le.mpr h_min
    have h_max_le : n.mantissa_ ≤ largeRange.max := UInt64.le_iff_toNat_le.mpr h_max
    rw [doNormalize_scaleUp_id largeRange.min n.mantissa_ n.exponent_ h_min_le] at hok
    rw [doNormalize_scaleDown_id largeRange.max n.mantissa_ n.exponent_ _ h_max_le] at hok
    simp only [] at hok
    have h_no_under_mant : ¬ n.mantissa_ < largeRange.min := by
      rw [UInt64.lt_iff_toNat_lt]; exact Nat.not_lt.mpr h_min
    have h_no_under_exp : ¬ n.exponent_ < minExponent := not_lt.mpr h_exp
    have h_check_false : (n.exponent_ < minExponent || n.mantissa_ < largeRange.min) = false := by
      simp [h_no_under_exp, h_no_under_mant]
    rw [h_check_false] at hok
    simp only [Bool.false_eq_true, if_false] at hok
    have h_mr_u64 : n.mantissa_ > maxRep := UInt64.lt_iff_toNat_lt.mpr h_mr
    rw [show doNormalize_capAtMaxRep n.mantissa_ n.exponent_
        (if n.negative_ then Guard.new.set_negative else Guard.new)
        = .error "Number::normalize 1.5" from by
      unfold doNormalize_capAtMaxRep; rw [if_pos h_mr_u64, if_pos h_ge]] at hok
    exact absurd hok (by intro h; cases h)
  have h_doNorm_id : doNormalize n.negative_ n.mantissa_ n.exponent_
      largeRange.min largeRange.max .to_nearest
      = .ok { negative_ := n.negative_, mantissa_ := n.mantissa_, exponent_ := n.exponent_ } :=
    doNormalize_id n.negative_ n.mantissa_ n.exponent_ h_min h_max h_exp h_mod h_exp_le h_mr_exp
  unfold Number.normalize at hok
  rw [h_doNorm_id] at hok
  have h_eq := Except.ok.inj hok
  cases n
  simpa using h_eq.symm

/-! ## Mode-generic helpers: extract exponent bounds from `normalize` success -/

lemma guard_new_round_neg_one_any (mode : rounding_mode) : Guard.new.round mode = -1 := by
  cases mode <;> unfold Guard.round Guard.new <;> simp

lemma guard_new_set_negative_round_neg_one_any (mode : rounding_mode) :
    Guard.new.set_negative.round mode = -1 := by
  cases mode <;> unfold Guard.round Guard.set_negative Guard.new <;> simp

lemma guard_new_doRoundUp_id_any
    (negative : Bool) (m : UInt64) (e : Int) (mode : rounding_mode) (loc : String)
    (hmin : largeRange.min ≤ m) (hexp : minExponent ≤ e) (hexp_le : e ≤ maxExponent) :
    Guard.new.doRoundUp negative m e largeRange.min largeRange.max mode loc
      = .ok { negative_ := negative, mantissa_ := m, exponent_ := e } := by
  unfold Guard.doRoundUp Guard.bringIntoRange Guard.doDropDigit
  simp only []
  rw [guard_new_round_neg_one_any mode]
  have : ((-1 : Int) == 1 || ((-1 : Int) == 0 && m % 2 == 1)) = false := by rfl
  rw [this]
  simp only [Bool.false_and, if_false, Bool.false_eq_true]
  have h_no_resc : ¬ m < largeRange.min := by
    rw [UInt64.lt_iff_toNat_lt]; exact Nat.not_lt.mpr (UInt64.le_iff_toNat_le.mp hmin)
  rw [if_neg h_no_resc, if_neg (not_lt.mpr hexp), if_neg (not_lt.mpr hexp_le)]

lemma guard_new_set_negative_doRoundUp_id_any
    (m : UInt64) (e : Int) (mode : rounding_mode) (loc : String)
    (hmin : largeRange.min ≤ m) (hexp : minExponent ≤ e) (hexp_le : e ≤ maxExponent) :
    Guard.new.set_negative.doRoundUp true m e largeRange.min largeRange.max mode loc
      = .ok { negative_ := true, mantissa_ := m, exponent_ := e } := by
  unfold Guard.doRoundUp Guard.bringIntoRange Guard.doDropDigit
  simp only []
  rw [guard_new_set_negative_round_neg_one_any mode]
  have : ((-1 : Int) == 1 || ((-1 : Int) == 0 && m % 2 == 1)) = false := by rfl
  rw [this]
  simp only [Bool.false_and, if_false, Bool.false_eq_true]
  have h_no_resc : ¬ m < largeRange.min := by
    rw [UInt64.lt_iff_toNat_lt]; exact Nat.not_lt.mpr (UInt64.le_iff_toNat_le.mp hmin)
  rw [if_neg h_no_resc, if_neg (not_lt.mpr hexp), if_neg (not_lt.mpr hexp_le)]

lemma Number.normalize_exp_bound {n result : Number} {mode : rounding_mode}
    (h_min : largeRange.min.toNat ≤ n.mantissa_.toNat)
    (h_max : n.mantissa_.toNat ≤ largeRange.max.toNat)
    (h_exp : minExponent ≤ n.exponent_)
    (hok : n.normalize largeRange.min largeRange.max mode = .ok result) :
    n.exponent_ ≤ maxExponent := by
  by_contra h
  push_neg at h
  have hm_ne_zero : n.mantissa_ ≠ 0 := by
    intro heq; rw [heq] at h_min; rw [largeRange_min_val] at h_min
    have : (0 : UInt64).toNat = 0 := rfl; omega
  have h_min_le : largeRange.min ≤ n.mantissa_ := UInt64.le_iff_toNat_le.mpr h_min
  have h_max_le : n.mantissa_ ≤ largeRange.max := UInt64.le_iff_toNat_le.mpr h_max
  have h_no_under_mant : ¬ n.mantissa_ < largeRange.min := by
    rw [UInt64.lt_iff_toNat_lt]; exact Nat.not_lt.mpr h_min
  have h_no_under_exp : ¬ n.exponent_ < minExponent := not_lt.mpr h_exp
  unfold Number.normalize doNormalize at hok
  rw [beq_eq_false_iff_ne.mpr hm_ne_zero] at hok
  simp only [Bool.false_eq_true, if_false] at hok
  rw [doNormalize_scaleUp_id largeRange.min n.mantissa_ n.exponent_ h_min_le] at hok
  rw [doNormalize_scaleDown_id largeRange.max n.mantissa_ n.exponent_ _ h_max_le] at hok
  simp only [] at hok
  have h_check_false : (n.exponent_ < minExponent || n.mantissa_ < largeRange.min) = false := by
    simp [h_no_under_exp, h_no_under_mant]
  rw [h_check_false] at hok
  simp only [Bool.false_eq_true, if_false] at hok
  by_cases h_mr : n.mantissa_ > maxRep
  · rw [show doNormalize_capAtMaxRep n.mantissa_ n.exponent_
        (if n.negative_ then Guard.new.set_negative else Guard.new)
        = .error "Number::normalize 1.5" from by
      unfold doNormalize_capAtMaxRep; rw [if_pos h_mr, if_pos (le_of_lt h)]] at hok
    exact absurd hok (by intro hc; cases hc)
  · cases hn : n.negative_
    · simp only [hn, Bool.false_eq_true, if_false] at hok
      rw [show doNormalize_capAtMaxRep n.mantissa_ n.exponent_ Guard.new
          = .ok (n.mantissa_, n.exponent_, Guard.new) from by
        unfold doNormalize_capAtMaxRep; rw [if_neg h_mr]] at hok
      simp only [] at hok
      have h_rup_err : Guard.new.doRoundUp false n.mantissa_ n.exponent_
          largeRange.min largeRange.max mode "Number::normalize 2" = .error "Number::normalize 2" := by
        unfold Guard.doRoundUp Guard.bringIntoRange Guard.doDropDigit; simp only []
        rw [guard_new_round_neg_one_any mode]
        have : ((-1 : Int) == 1 || ((-1 : Int) == 0 && n.mantissa_ % 2 == 1)) = false := by rfl
        rw [this]; simp only [Bool.false_and, if_false, Bool.false_eq_true]
        rw [if_neg h_no_under_mant, if_neg h_no_under_exp, if_pos h]
      rw [h_rup_err] at hok
      exact absurd hok (by intro hc; cases hc)
    · simp only [hn, if_true] at hok
      rw [show doNormalize_capAtMaxRep n.mantissa_ n.exponent_ Guard.new.set_negative
          = .ok (n.mantissa_, n.exponent_, Guard.new.set_negative) from by
        unfold doNormalize_capAtMaxRep; rw [if_neg h_mr]] at hok
      simp only [] at hok
      have h_rup_err : Guard.new.set_negative.doRoundUp true n.mantissa_ n.exponent_
          largeRange.min largeRange.max mode "Number::normalize 2" = .error "Number::normalize 2" := by
        unfold Guard.doRoundUp Guard.bringIntoRange Guard.doDropDigit; simp only []
        rw [guard_new_set_negative_round_neg_one_any mode]
        have : ((-1 : Int) == 1 || ((-1 : Int) == 0 && n.mantissa_ % 2 == 1)) = false := by rfl
        rw [this]; simp only [Bool.false_and, if_false, Bool.false_eq_true]
        rw [if_neg h_no_under_mant, if_neg h_no_under_exp, if_pos h]
      rw [h_rup_err] at hok
      exact absurd hok (by intro hc; cases hc)

lemma Number.normalize_no_cusp_overflow {n result : Number} {mode : rounding_mode}
    (h_min : largeRange.min.toNat ≤ n.mantissa_.toNat)
    (h_max : n.mantissa_.toNat ≤ largeRange.max.toNat)
    (h_exp : minExponent ≤ n.exponent_)
    (hok : n.normalize largeRange.min largeRange.max mode = .ok result) :
    ¬ (n.mantissa_.toNat > maxRep.toNat ∧ n.exponent_ ≥ maxExponent) := by
  intro ⟨h_mr, h_ge⟩
  have hm_ne_zero : n.mantissa_ ≠ 0 := by
    intro heq; rw [heq] at h_mr; rw [show (0 : UInt64).toNat = 0 from rfl] at h_mr
    rw [maxRep_val] at h_mr; omega
  have h_min_le : largeRange.min ≤ n.mantissa_ := UInt64.le_iff_toNat_le.mpr h_min
  have h_max_le : n.mantissa_ ≤ largeRange.max := UInt64.le_iff_toNat_le.mpr h_max
  have h_no_under_mant : ¬ n.mantissa_ < largeRange.min := by
    rw [UInt64.lt_iff_toNat_lt]; exact Nat.not_lt.mpr h_min
  have h_no_under_exp : ¬ n.exponent_ < minExponent := not_lt.mpr h_exp
  unfold Number.normalize doNormalize at hok
  rw [beq_eq_false_iff_ne.mpr hm_ne_zero] at hok
  simp only [Bool.false_eq_true, if_false] at hok
  rw [doNormalize_scaleUp_id largeRange.min n.mantissa_ n.exponent_ h_min_le] at hok
  rw [doNormalize_scaleDown_id largeRange.max n.mantissa_ n.exponent_ _ h_max_le] at hok
  simp only [] at hok
  have h_check_false : (n.exponent_ < minExponent || n.mantissa_ < largeRange.min) = false := by
    simp [h_no_under_exp, h_no_under_mant]
  rw [h_check_false] at hok
  simp only [Bool.false_eq_true, if_false] at hok
  rw [show doNormalize_capAtMaxRep n.mantissa_ n.exponent_
      (if n.negative_ then Guard.new.set_negative else Guard.new)
      = .error "Number::normalize 1.5" from by
    unfold doNormalize_capAtMaxRep
    rw [if_pos (UInt64.lt_iff_toNat_lt.mpr h_mr), if_pos h_ge]] at hok
  exact absurd hok (by intro hc; cases hc)

/-! ## `.downward`-mode analogues of `doNormalize_id` / `normalize_eq_of_invariants`. -/

/-- `Guard.new` rounds to `-1` in `.downward` mode (sbit is false). -/
lemma guard_new_round_neg_one_downward : Guard.new.round .downward = -1 := by
  unfold Guard.round Guard.new
  simp

/-- `Guard.new.set_negative` rounds to `-1` in `.downward` mode. -/
lemma guard_new_set_negative_round_neg_one_downward :
    Guard.new.set_negative.round .downward = -1 := by
  unfold Guard.round Guard.set_negative Guard.new
  simp

/-- `.downward`-mode analogue of `guard_new_doRoundUp_id`. -/
lemma guard_new_doRoundUp_id_downward
    (negative : Bool) (m : UInt64) (e : Int) (loc : String)
    (hmin : largeRange.min ≤ m) (hexp : minExponent ≤ e) (hexp_le : e ≤ maxExponent) :
    Guard.new.doRoundUp negative m e largeRange.min largeRange.max .downward loc
      = .ok { negative_ := negative, mantissa_ := m, exponent_ := e } := by
  unfold Guard.doRoundUp Guard.bringIntoRange Guard.doDropDigit
  simp only []
  rw [guard_new_round_neg_one_downward]
  have h_round_false : ((-1 : Int) == 1 || ((-1 : Int) == 0 && m % 2 == 1)) = false := by rfl
  rw [h_round_false]
  simp only [Bool.false_and, if_false, Bool.false_eq_true]
  have h_no_resc : ¬ m < largeRange.min := by
    rw [UInt64.lt_iff_toNat_lt]
    exact Nat.not_lt.mpr (UInt64.le_iff_toNat_le.mp hmin)
  rw [if_neg h_no_resc, if_neg (not_lt.mpr hexp), if_neg (not_lt.mpr hexp_le)]

/-- `.downward`-mode analogue of `guard_new_doRoundUp_divu10`. -/
lemma guard_new_doRoundUp_divu10_downward
    (negative : Bool) (m : UInt64) (e : Int) (loc : String)
    (h_gt : m > maxRep) (h_le_max : m.toNat ≤ largeRange.max.toNat)
    (h_mod : m.toNat % 10 = 0) (hexp : minExponent ≤ e) (hexp_le : e ≤ maxExponent) :
    Guard.new.doRoundUp negative (m / 10) (e + 1) largeRange.min largeRange.max .downward loc
      = .ok { negative_ := negative, mantissa_ := m, exponent_ := e } := by
  unfold Guard.doRoundUp Guard.bringIntoRange Guard.doDropDigit
  simp only []
  rw [guard_new_round_neg_one_downward]
  have h_round_false :
      ((-1 : Int) == 1 || ((-1 : Int) == 0 && (m / 10) % 2 == 1)) = false := by rfl
  rw [h_round_false]
  simp only [Bool.false_and, if_false, Bool.false_eq_true]
  have h_resc : m / 10 < largeRange.min := div_ten_lt_minMantissa h_gt h_le_max
  rw [if_pos h_resc]
  have h_no_under : ¬ e + 1 - 1 < minExponent := by push_neg; linarith
  rw [if_neg h_no_under]
  have h_mul : ((m / 10) * 10).toNat = m.toNat := mul_div_ten_cancel h_mod h_le_max
  have h_mul_eq : (m / 10) * 10 = m := UInt64.toNat_inj.mp h_mul
  have h_exp_eq : (e + 1 - 1 : ℤ) = e := by ring
  rw [h_mul_eq, h_exp_eq, if_neg (not_lt.mpr hexp_le)]

/-- `.downward`-mode analogue of `doNormalize_id`. -/
lemma doNormalize_id_downward
    (negative : Bool) (m : UInt64) (e : Int)
    (hmin : largeRange.min.toNat ≤ m.toNat)
    (hmax : m.toNat ≤ largeRange.max.toNat)
    (hexp : minExponent ≤ e)
    (hm_mod : m.toNat > maxRep.toNat → m.toNat % 10 = 0)
    (h_exp_le : e ≤ maxExponent)
    (h_mr_exp : m.toNat > maxRep.toNat → e < maxExponent) :
    doNormalize negative m e largeRange.min largeRange.max .downward
      = .ok { negative_ := negative, mantissa_ := m, exponent_ := e } := by
  unfold doNormalize
  have hm_ne_zero : m ≠ 0 := by
    intro h
    rw [h] at hmin
    rw [largeRange_min_val] at hmin
    have : (0 : UInt64).toNat = 0 := rfl
    omega
  have h_eq_false : (m == 0) = false := by
    rw [beq_eq_false_iff_ne]; exact hm_ne_zero
  rw [h_eq_false]
  simp only [Bool.false_eq_true, if_false]
  have h_min_le : largeRange.min ≤ m := UInt64.le_iff_toNat_le.mpr hmin
  have h_max_le : m ≤ largeRange.max := UInt64.le_iff_toNat_le.mpr hmax
  have h_no_under_mant : ¬ m < largeRange.min := by
    rw [UInt64.lt_iff_toNat_lt]; exact Nat.not_lt.mpr hmin
  rw [doNormalize_scaleUp_id largeRange.min m e h_min_le]
  rw [doNormalize_scaleDown_id largeRange.max m e _ h_max_le]
  simp only []
  have h_no_under_exp : ¬ e < minExponent := not_lt.mpr hexp
  have h_check_false : (e < minExponent || m < largeRange.min) = false := by
    simp [h_no_under_exp, h_no_under_mant]
  rw [h_check_false]
  simp only [Bool.false_eq_true, if_false]
  by_cases h_mr : m > maxRep
  · have h_mod : m.toNat % 10 = 0 := hm_mod (UInt64.lt_iff_toNat_lt.mp h_mr)
    have h_mod_u : m % 10 = 0 := by
      apply UInt64.toNat_inj.mp
      rw [UInt64.toNat_mod]
      change m.toNat % (10 : UInt64).toNat = 0
      have h10 : (10 : UInt64).toNat = 10 := rfl
      rw [h10]; exact h_mod
    have h_nge_exp : ¬ e ≥ maxExponent :=
      Int.not_le.mpr (h_mr_exp (UInt64.lt_iff_toNat_lt.mp h_mr))
    have h_cap_eq : ∀ (g : Guard),
        doNormalize_capAtMaxRep m e g = .ok (m / 10, e + 1, g.push 0) := by
      intro g; unfold doNormalize_capAtMaxRep
      rw [if_pos h_mr, if_neg h_nge_exp]; simp [divu10, h_mod_u]
    cases negative
    · simp only [Bool.false_eq_true, if_false]
      rw [h_cap_eq Guard.new, guard_new_push_zero]
      simp only []
      have h_rup : Guard.new.doRoundUp false (m / 10) (e + 1) largeRange.min largeRange.max
          .downward "Number::normalize 2"
          = .ok { negative_ := false, mantissa_ := m, exponent_ := e } :=
        guard_new_doRoundUp_divu10_downward false m e "Number::normalize 2" h_mr hmax h_mod hexp h_exp_le
      rw [h_rup]; rfl
    · simp only [if_true]
      have h_push_sn : Guard.new.set_negative.push 0 = Guard.new.set_negative := by
        unfold Guard.push Guard.set_negative Guard.new; simp
      rw [h_cap_eq Guard.new.set_negative, h_push_sn]
      simp only []
      have h_rup : Guard.new.set_negative.doRoundUp true (m / 10) (e + 1) largeRange.min largeRange.max
          .downward "Number::normalize 2"
          = .ok { negative_ := true, mantissa_ := m, exponent_ := e } := by
        unfold Guard.doRoundUp Guard.bringIntoRange Guard.doDropDigit; simp only []
        rw [guard_new_set_negative_round_neg_one_downward]
        have h_round_false :
            ((-1 : Int) == 1 || ((-1 : Int) == 0 && (m / 10) % 2 == 1)) = false := by rfl
        rw [h_round_false]
        simp only [Bool.false_and, if_false, Bool.false_eq_true]
        have h_resc : m / 10 < largeRange.min := div_ten_lt_minMantissa h_mr hmax
        rw [if_pos h_resc]
        have h_no_under : ¬ e + 1 - 1 < minExponent := by intro h; omega
        rw [if_neg h_no_under]
        have h_mul : ((m / 10) * 10).toNat = m.toNat := mul_div_ten_cancel h_mod hmax
        have h_mul_eq : (m / 10) * 10 = m := UInt64.toNat_inj.mp h_mul
        have h_exp_eq : (e + 1 - 1 : ℤ) = e := by ring
        rw [h_mul_eq, h_exp_eq, if_neg (not_lt.mpr h_exp_le)]
      rw [h_rup]; rfl
  · have h_cap_eq : ∀ (g : Guard),
        doNormalize_capAtMaxRep m e g = .ok (m, e, g) := by
      intro g; unfold doNormalize_capAtMaxRep; rw [if_neg h_mr]
    cases negative
    · simp only [Bool.false_eq_true, if_false]
      rw [h_cap_eq Guard.new]
      simp only []
      have h_rup : Guard.new.doRoundUp false m e largeRange.min largeRange.max .downward
          "Number::normalize 2"
          = .ok { negative_ := false, mantissa_ := m, exponent_ := e } :=
        guard_new_doRoundUp_id_downward false m e "Number::normalize 2" h_min_le hexp h_exp_le
      rw [h_rup]; rfl
    · simp only [if_true]
      rw [h_cap_eq Guard.new.set_negative]
      simp only []
      have h_rup : Guard.new.set_negative.doRoundUp true m e largeRange.min largeRange.max .downward
          "Number::normalize 2"
          = .ok { negative_ := true, mantissa_ := m, exponent_ := e } := by
        unfold Guard.doRoundUp Guard.bringIntoRange Guard.doDropDigit; simp only []
        rw [guard_new_set_negative_round_neg_one_downward]
        have h_round_false :
            ((-1 : Int) == 1 || ((-1 : Int) == 0 && m % 2 == 1)) = false := by rfl
        rw [h_round_false]
        simp only [Bool.false_and, if_false, Bool.false_eq_true]
        rw [if_neg h_no_under_mant, if_neg (not_lt.mpr hexp), if_neg (not_lt.mpr h_exp_le)]
      rw [h_rup]; rfl

/-- `.downward`-mode analogue of `Number.normalize_eq_of_invariants`. -/
lemma Number.normalize_eq_of_invariants_downward {n result : Number}
    (h_min : largeRange.min.toNat ≤ n.mantissa_.toNat)
    (h_max : n.mantissa_.toNat ≤ largeRange.max.toNat)
    (h_exp : minExponent ≤ n.exponent_)
    (h_mod : n.mantissa_.toNat > maxRep.toNat → n.mantissa_.toNat % 10 = 0)
    (hok : n.normalize largeRange.min largeRange.max .downward = .ok result) :
    result = n := by
  have h_exp_le : n.exponent_ ≤ maxExponent := by
    by_contra h
    push_neg at h
    have hm_ne_zero : n.mantissa_ ≠ 0 := by
      intro heq; rw [heq] at h_min; rw [largeRange_min_val] at h_min
      rw [show (0 : UInt64).toNat = 0 from rfl] at h_min; omega
    have h_min_le : largeRange.min ≤ n.mantissa_ := UInt64.le_iff_toNat_le.mpr h_min
    have h_max_le : n.mantissa_ ≤ largeRange.max := UInt64.le_iff_toNat_le.mpr h_max
    have h_no_under_mant : ¬ n.mantissa_ < largeRange.min := by
      rw [UInt64.lt_iff_toNat_lt]; exact Nat.not_lt.mpr h_min
    have h_no_under_exp : ¬ n.exponent_ < minExponent := not_lt.mpr h_exp
    unfold Number.normalize doNormalize at hok
    rw [beq_eq_false_iff_ne.mpr hm_ne_zero] at hok
    simp only [Bool.false_eq_true, if_false] at hok
    rw [doNormalize_scaleUp_id largeRange.min n.mantissa_ n.exponent_ h_min_le] at hok
    rw [doNormalize_scaleDown_id largeRange.max n.mantissa_ n.exponent_ _ h_max_le] at hok
    simp only [] at hok
    have h_check_false : (n.exponent_ < minExponent || n.mantissa_ < largeRange.min) = false := by
      simp [h_no_under_exp, h_no_under_mant]
    rw [h_check_false] at hok
    simp only [Bool.false_eq_true, if_false] at hok
    by_cases h_mr : n.mantissa_ > maxRep
    · rw [show doNormalize_capAtMaxRep n.mantissa_ n.exponent_
          (if n.negative_ then Guard.new.set_negative else Guard.new)
          = .error "Number::normalize 1.5" from by
        unfold doNormalize_capAtMaxRep; rw [if_pos h_mr, if_pos (le_of_lt h)]] at hok
      exact absurd hok (by intro h; cases h)
    · cases hn : n.negative_
      · simp only [hn, Bool.false_eq_true, if_false] at hok
        rw [show doNormalize_capAtMaxRep n.mantissa_ n.exponent_ Guard.new
            = .ok (n.mantissa_, n.exponent_, Guard.new) from by
          unfold doNormalize_capAtMaxRep; rw [if_neg h_mr]] at hok
        simp only [] at hok
        have h_rup_err : Guard.new.doRoundUp false n.mantissa_ n.exponent_
            largeRange.min largeRange.max .downward "Number::normalize 2"
            = .error "Number::normalize 2" := by
          unfold Guard.doRoundUp Guard.bringIntoRange Guard.doDropDigit; simp only []
          rw [guard_new_round_neg_one_downward]
          have : ((-1 : Int) == 1 || ((-1 : Int) == 0 && n.mantissa_ % 2 == 1)) = false := by rfl
          rw [this]; simp only [Bool.false_and, if_false, Bool.false_eq_true]
          rw [if_neg h_no_under_mant, if_neg h_no_under_exp, if_pos h]
        rw [h_rup_err] at hok; exact absurd hok (by intro hc; cases hc)
      · simp only [hn, if_true] at hok
        rw [show doNormalize_capAtMaxRep n.mantissa_ n.exponent_ Guard.new.set_negative
            = .ok (n.mantissa_, n.exponent_, Guard.new.set_negative) from by
          unfold doNormalize_capAtMaxRep; rw [if_neg h_mr]] at hok
        simp only [] at hok
        have h_rup_err : Guard.new.set_negative.doRoundUp true n.mantissa_ n.exponent_
            largeRange.min largeRange.max .downward "Number::normalize 2"
            = .error "Number::normalize 2" := by
          unfold Guard.doRoundUp Guard.bringIntoRange Guard.doDropDigit; simp only []
          rw [guard_new_set_negative_round_neg_one_downward]
          have : ((-1 : Int) == 1 || ((-1 : Int) == 0 && n.mantissa_ % 2 == 1)) = false := by rfl
          rw [this]; simp only [Bool.false_and, if_false, Bool.false_eq_true]
          rw [if_neg h_no_under_mant, if_neg h_no_under_exp, if_pos h]
        rw [h_rup_err] at hok; exact absurd hok (by intro hc; cases hc)
  have h_mr_exp : n.mantissa_.toNat > maxRep.toNat → n.exponent_ < maxExponent := by
    intro h_mr
    by_contra h_ge; push_neg at h_ge
    have h_eq_max : n.exponent_ = maxExponent := Int.le_antisymm h_exp_le h_ge
    unfold Number.normalize at hok; unfold doNormalize at hok
    have hm_ne_zero : n.mantissa_ ≠ 0 := by
      intro h; rw [h] at h_mr; rw [show (0 : UInt64).toNat = 0 from rfl] at h_mr
      exact absurd h_mr (by rw [maxRep_val]; omega)
    rw [beq_eq_false_iff_ne.mpr hm_ne_zero] at hok
    simp only [Bool.false_eq_true, if_false] at hok
    have h_min_le : largeRange.min ≤ n.mantissa_ := UInt64.le_iff_toNat_le.mpr h_min
    have h_max_le : n.mantissa_ ≤ largeRange.max := UInt64.le_iff_toNat_le.mpr h_max
    rw [doNormalize_scaleUp_id largeRange.min n.mantissa_ n.exponent_ h_min_le] at hok
    rw [doNormalize_scaleDown_id largeRange.max n.mantissa_ n.exponent_ _ h_max_le] at hok
    simp only [] at hok
    have h_no_under_mant : ¬ n.mantissa_ < largeRange.min := by
      rw [UInt64.lt_iff_toNat_lt]; exact Nat.not_lt.mpr h_min
    have h_no_under_exp : ¬ n.exponent_ < minExponent := not_lt.mpr h_exp
    have h_check_false : (n.exponent_ < minExponent || n.mantissa_ < largeRange.min) = false := by
      simp [h_no_under_exp, h_no_under_mant]
    rw [h_check_false] at hok
    simp only [Bool.false_eq_true, if_false] at hok
    have h_mr_u64 : n.mantissa_ > maxRep := UInt64.lt_iff_toNat_lt.mpr h_mr
    rw [show doNormalize_capAtMaxRep n.mantissa_ n.exponent_
        (if n.negative_ then Guard.new.set_negative else Guard.new)
        = .error "Number::normalize 1.5" from by
      unfold doNormalize_capAtMaxRep; rw [if_pos h_mr_u64, if_pos h_ge]] at hok
    exact absurd hok (by intro h; cases h)
  have h_doNorm_id := doNormalize_id_downward n.negative_ n.mantissa_ n.exponent_
      h_min h_max h_exp h_mod h_exp_le h_mr_exp
  unfold Number.normalize at hok
  rw [h_doNorm_id] at hok
  exact (Except.ok.inj hok ▸ by cases n; rfl)

/-! ## `.towards_zero`-mode analogues of `doNormalize_id` / `normalize_eq_of_invariants` -/

/-- `Guard.new` rounds to `-1` in `.towards_zero` mode. -/
lemma guard_new_round_neg_one_towards_zero : Guard.new.round .towards_zero = -1 := rfl

/-- `Guard.new.set_negative` rounds to `-1` in `.towards_zero` mode. -/
lemma guard_new_set_negative_round_neg_one_towards_zero :
    Guard.new.set_negative.round .towards_zero = -1 := rfl

/-- `.towards_zero`-mode analogue of `guard_new_doRoundUp_id`. -/
lemma guard_new_doRoundUp_id_towards_zero
    (negative : Bool) (m : UInt64) (e : Int) (loc : String)
    (hmin : largeRange.min ≤ m) (hexp : minExponent ≤ e) (hexp_le : e ≤ maxExponent) :
    Guard.new.doRoundUp negative m e largeRange.min largeRange.max .towards_zero loc
      = .ok { negative_ := negative, mantissa_ := m, exponent_ := e } := by
  unfold Guard.doRoundUp Guard.bringIntoRange Guard.doDropDigit
  simp only []
  rw [guard_new_round_neg_one_towards_zero]
  have h_round_false : ((-1 : Int) == 1 || ((-1 : Int) == 0 && m % 2 == 1)) = false := by rfl
  rw [h_round_false]
  simp only [Bool.false_and, if_false, Bool.false_eq_true]
  have h_no_resc : ¬ m < largeRange.min := by
    rw [UInt64.lt_iff_toNat_lt]
    exact Nat.not_lt.mpr (UInt64.le_iff_toNat_le.mp hmin)
  rw [if_neg h_no_resc, if_neg (not_lt.mpr hexp), if_neg (not_lt.mpr hexp_le)]

/-- `.towards_zero`-mode analogue of `guard_new_doRoundUp_divu10`. -/
lemma guard_new_doRoundUp_divu10_towards_zero
    (negative : Bool) (m : UInt64) (e : Int) (loc : String)
    (h_gt : m > maxRep) (h_le_max : m.toNat ≤ largeRange.max.toNat)
    (h_mod : m.toNat % 10 = 0) (hexp : minExponent ≤ e) (hexp_le : e ≤ maxExponent) :
    Guard.new.doRoundUp negative (m / 10) (e + 1) largeRange.min largeRange.max .towards_zero loc
      = .ok { negative_ := negative, mantissa_ := m, exponent_ := e } := by
  unfold Guard.doRoundUp Guard.bringIntoRange Guard.doDropDigit
  simp only []
  rw [guard_new_round_neg_one_towards_zero]
  have h_round_false :
      ((-1 : Int) == 1 || ((-1 : Int) == 0 && (m / 10) % 2 == 1)) = false := by rfl
  rw [h_round_false]
  simp only [Bool.false_and, if_false, Bool.false_eq_true]
  have h_resc : m / 10 < largeRange.min := div_ten_lt_minMantissa h_gt h_le_max
  rw [if_pos h_resc]
  have h_no_under : ¬ e + 1 - 1 < minExponent := by push_neg; linarith
  rw [if_neg h_no_under]
  have h_mul : ((m / 10) * 10).toNat = m.toNat := mul_div_ten_cancel h_mod h_le_max
  have h_mul_eq : (m / 10) * 10 = m := UInt64.toNat_inj.mp h_mul
  have h_exp_eq : (e + 1 - 1 : ℤ) = e := by ring
  rw [h_mul_eq, h_exp_eq, if_neg (not_lt.mpr hexp_le)]

/-- `.towards_zero`-mode analogue of `doNormalize_id`. -/
lemma doNormalize_id_towards_zero
    (negative : Bool) (m : UInt64) (e : Int)
    (hmin : largeRange.min.toNat ≤ m.toNat)
    (hmax : m.toNat ≤ largeRange.max.toNat)
    (hexp : minExponent ≤ e)
    (hm_mod : m.toNat > maxRep.toNat → m.toNat % 10 = 0)
    (h_exp_le : e ≤ maxExponent)
    (h_mr_exp : m.toNat > maxRep.toNat → e < maxExponent) :
    doNormalize negative m e largeRange.min largeRange.max .towards_zero
      = .ok { negative_ := negative, mantissa_ := m, exponent_ := e } := by
  unfold doNormalize
  have hm_ne_zero : m ≠ 0 := by
    intro h
    rw [h] at hmin
    rw [largeRange_min_val] at hmin
    have : (0 : UInt64).toNat = 0 := rfl
    omega
  have h_eq_false : (m == 0) = false := by
    rw [beq_eq_false_iff_ne]; exact hm_ne_zero
  rw [h_eq_false]
  simp only [Bool.false_eq_true, if_false]
  have h_min_le : largeRange.min ≤ m := UInt64.le_iff_toNat_le.mpr hmin
  have h_max_le : m ≤ largeRange.max := UInt64.le_iff_toNat_le.mpr hmax
  have h_no_under_mant : ¬ m < largeRange.min := by
    rw [UInt64.lt_iff_toNat_lt]; exact Nat.not_lt.mpr hmin
  rw [doNormalize_scaleUp_id largeRange.min m e h_min_le]
  rw [doNormalize_scaleDown_id largeRange.max m e _ h_max_le]
  simp only []
  have h_no_under_exp : ¬ e < minExponent := not_lt.mpr hexp
  have h_check_false : (e < minExponent || m < largeRange.min) = false := by
    simp [h_no_under_exp, h_no_under_mant]
  rw [h_check_false]
  simp only [Bool.false_eq_true, if_false]
  by_cases h_mr : m > maxRep
  · have h_mod : m.toNat % 10 = 0 := hm_mod (UInt64.lt_iff_toNat_lt.mp h_mr)
    have h_mod_u : m % 10 = 0 := by
      apply UInt64.toNat_inj.mp
      rw [UInt64.toNat_mod]
      change m.toNat % (10 : UInt64).toNat = 0
      have h10 : (10 : UInt64).toNat = 10 := rfl
      rw [h10]; exact h_mod
    have h_nge_exp : ¬ e ≥ maxExponent :=
      Int.not_le.mpr (h_mr_exp (UInt64.lt_iff_toNat_lt.mp h_mr))
    have h_cap_eq : ∀ (g : Guard),
        doNormalize_capAtMaxRep m e g = .ok (m / 10, e + 1, g.push 0) := by
      intro g; unfold doNormalize_capAtMaxRep
      rw [if_pos h_mr, if_neg h_nge_exp]; simp [divu10, h_mod_u]
    cases negative
    · simp only [Bool.false_eq_true, if_false]
      rw [h_cap_eq Guard.new, guard_new_push_zero]
      simp only []
      have h_rup : Guard.new.doRoundUp false (m / 10) (e + 1) largeRange.min largeRange.max
          .towards_zero "Number::normalize 2"
          = .ok { negative_ := false, mantissa_ := m, exponent_ := e } :=
        guard_new_doRoundUp_divu10_towards_zero false m e "Number::normalize 2" h_mr hmax h_mod hexp h_exp_le
      rw [h_rup]; rfl
    · simp only [if_true]
      have h_push_sn : Guard.new.set_negative.push 0 = Guard.new.set_negative := by
        unfold Guard.push Guard.set_negative Guard.new; simp
      rw [h_cap_eq Guard.new.set_negative, h_push_sn]
      simp only []
      have h_rup : Guard.new.set_negative.doRoundUp true (m / 10) (e + 1) largeRange.min largeRange.max
          .towards_zero "Number::normalize 2"
          = .ok { negative_ := true, mantissa_ := m, exponent_ := e } := by
        unfold Guard.doRoundUp Guard.bringIntoRange Guard.doDropDigit; simp only []
        rw [guard_new_set_negative_round_neg_one_towards_zero]
        have h_round_false :
            ((-1 : Int) == 1 || ((-1 : Int) == 0 && (m / 10) % 2 == 1)) = false := by rfl
        rw [h_round_false]
        simp only [Bool.false_and, if_false, Bool.false_eq_true]
        have h_resc : m / 10 < largeRange.min := div_ten_lt_minMantissa h_mr hmax
        rw [if_pos h_resc]
        have h_no_under : ¬ e + 1 - 1 < minExponent := by intro h; omega
        rw [if_neg h_no_under]
        have h_mul : ((m / 10) * 10).toNat = m.toNat := mul_div_ten_cancel h_mod hmax
        have h_mul_eq : (m / 10) * 10 = m := UInt64.toNat_inj.mp h_mul
        have h_exp_eq : (e + 1 - 1 : ℤ) = e := by ring
        rw [h_mul_eq, h_exp_eq, if_neg (not_lt.mpr h_exp_le)]
      rw [h_rup]; rfl
  · have h_cap_eq : ∀ (g : Guard),
        doNormalize_capAtMaxRep m e g = .ok (m, e, g) := by
      intro g; unfold doNormalize_capAtMaxRep; rw [if_neg h_mr]
    cases negative
    · simp only [Bool.false_eq_true, if_false]
      rw [h_cap_eq Guard.new]
      simp only []
      have h_rup : Guard.new.doRoundUp false m e largeRange.min largeRange.max .towards_zero
          "Number::normalize 2"
          = .ok { negative_ := false, mantissa_ := m, exponent_ := e } :=
        guard_new_doRoundUp_id_towards_zero false m e "Number::normalize 2" h_min_le hexp h_exp_le
      rw [h_rup]; rfl
    · simp only [if_true]
      rw [h_cap_eq Guard.new.set_negative]
      simp only []
      have h_rup : Guard.new.set_negative.doRoundUp true m e largeRange.min largeRange.max .towards_zero
          "Number::normalize 2"
          = .ok { negative_ := true, mantissa_ := m, exponent_ := e } := by
        unfold Guard.doRoundUp Guard.bringIntoRange Guard.doDropDigit; simp only []
        rw [guard_new_set_negative_round_neg_one_towards_zero]
        have h_round_false :
            ((-1 : Int) == 1 || ((-1 : Int) == 0 && m % 2 == 1)) = false := by rfl
        rw [h_round_false]
        simp only [Bool.false_and, if_false, Bool.false_eq_true]
        rw [if_neg h_no_under_mant, if_neg (not_lt.mpr hexp), if_neg (not_lt.mpr h_exp_le)]
      rw [h_rup]; rfl

/-- `.towards_zero`-mode analogue of `Number.normalize_eq_of_invariants`. -/
lemma Number.normalize_eq_of_invariants_towards_zero {n result : Number}
    (h_min : largeRange.min.toNat ≤ n.mantissa_.toNat)
    (h_max : n.mantissa_.toNat ≤ largeRange.max.toNat)
    (h_exp : minExponent ≤ n.exponent_)
    (h_mod : n.mantissa_.toNat > maxRep.toNat → n.mantissa_.toNat % 10 = 0)
    (hok : n.normalize largeRange.min largeRange.max .towards_zero = .ok result) :
    result = n := by
  have h_exp_le : n.exponent_ ≤ maxExponent := by
    by_contra h
    push_neg at h
    have hm_ne_zero : n.mantissa_ ≠ 0 := by
      intro heq; rw [heq] at h_min; rw [largeRange_min_val] at h_min
      rw [show (0 : UInt64).toNat = 0 from rfl] at h_min; omega
    have h_min_le : largeRange.min ≤ n.mantissa_ := UInt64.le_iff_toNat_le.mpr h_min
    have h_max_le : n.mantissa_ ≤ largeRange.max := UInt64.le_iff_toNat_le.mpr h_max
    have h_no_under_mant : ¬ n.mantissa_ < largeRange.min := by
      rw [UInt64.lt_iff_toNat_lt]; exact Nat.not_lt.mpr h_min
    have h_no_under_exp : ¬ n.exponent_ < minExponent := not_lt.mpr h_exp
    unfold Number.normalize doNormalize at hok
    rw [beq_eq_false_iff_ne.mpr hm_ne_zero] at hok
    simp only [Bool.false_eq_true, if_false] at hok
    rw [doNormalize_scaleUp_id largeRange.min n.mantissa_ n.exponent_ h_min_le] at hok
    rw [doNormalize_scaleDown_id largeRange.max n.mantissa_ n.exponent_ _ h_max_le] at hok
    simp only [] at hok
    have h_check_false : (n.exponent_ < minExponent || n.mantissa_ < largeRange.min) = false := by
      simp [h_no_under_exp, h_no_under_mant]
    rw [h_check_false] at hok
    simp only [Bool.false_eq_true, if_false] at hok
    by_cases h_mr : n.mantissa_ > maxRep
    · rw [show doNormalize_capAtMaxRep n.mantissa_ n.exponent_
          (if n.negative_ then Guard.new.set_negative else Guard.new)
          = .error "Number::normalize 1.5" from by
        unfold doNormalize_capAtMaxRep; rw [if_pos h_mr, if_pos (le_of_lt h)]] at hok
      exact absurd hok (by intro h; cases h)
    · cases hn : n.negative_
      · simp only [hn, Bool.false_eq_true, if_false] at hok
        rw [show doNormalize_capAtMaxRep n.mantissa_ n.exponent_ Guard.new
            = .ok (n.mantissa_, n.exponent_, Guard.new) from by
          unfold doNormalize_capAtMaxRep; rw [if_neg h_mr]] at hok
        simp only [] at hok
        have h_rup_err : Guard.new.doRoundUp false n.mantissa_ n.exponent_
            largeRange.min largeRange.max .towards_zero "Number::normalize 2"
            = .error "Number::normalize 2" := by
          unfold Guard.doRoundUp Guard.bringIntoRange Guard.doDropDigit; simp only []
          rw [guard_new_round_neg_one_towards_zero]
          have : ((-1 : Int) == 1 || ((-1 : Int) == 0 && n.mantissa_ % 2 == 1)) = false := by rfl
          rw [this]; simp only [Bool.false_and, if_false, Bool.false_eq_true]
          rw [if_neg h_no_under_mant, if_neg h_no_under_exp, if_pos h]
        rw [h_rup_err] at hok; exact absurd hok (by intro hc; cases hc)
      · simp only [hn, if_true] at hok
        rw [show doNormalize_capAtMaxRep n.mantissa_ n.exponent_ Guard.new.set_negative
            = .ok (n.mantissa_, n.exponent_, Guard.new.set_negative) from by
          unfold doNormalize_capAtMaxRep; rw [if_neg h_mr]] at hok
        simp only [] at hok
        have h_rup_err : Guard.new.set_negative.doRoundUp true n.mantissa_ n.exponent_
            largeRange.min largeRange.max .towards_zero "Number::normalize 2"
            = .error "Number::normalize 2" := by
          unfold Guard.doRoundUp Guard.bringIntoRange Guard.doDropDigit; simp only []
          rw [guard_new_set_negative_round_neg_one_towards_zero]
          have : ((-1 : Int) == 1 || ((-1 : Int) == 0 && n.mantissa_ % 2 == 1)) = false := by rfl
          rw [this]; simp only [Bool.false_and, if_false, Bool.false_eq_true]
          rw [if_neg h_no_under_mant, if_neg h_no_under_exp, if_pos h]
        rw [h_rup_err] at hok; exact absurd hok (by intro hc; cases hc)
  have h_mr_exp : n.mantissa_.toNat > maxRep.toNat → n.exponent_ < maxExponent := by
    intro h_mr
    by_contra h_ge; push_neg at h_ge
    have h_eq_max : n.exponent_ = maxExponent := Int.le_antisymm h_exp_le h_ge
    unfold Number.normalize at hok; unfold doNormalize at hok
    have hm_ne_zero : n.mantissa_ ≠ 0 := by
      intro h; rw [h] at h_mr; rw [show (0 : UInt64).toNat = 0 from rfl] at h_mr
      exact absurd h_mr (by rw [maxRep_val]; omega)
    rw [beq_eq_false_iff_ne.mpr hm_ne_zero] at hok
    simp only [Bool.false_eq_true, if_false] at hok
    have h_min_le : largeRange.min ≤ n.mantissa_ := UInt64.le_iff_toNat_le.mpr h_min
    have h_max_le : n.mantissa_ ≤ largeRange.max := UInt64.le_iff_toNat_le.mpr h_max
    rw [doNormalize_scaleUp_id largeRange.min n.mantissa_ n.exponent_ h_min_le] at hok
    rw [doNormalize_scaleDown_id largeRange.max n.mantissa_ n.exponent_ _ h_max_le] at hok
    simp only [] at hok
    have h_no_under_mant : ¬ n.mantissa_ < largeRange.min := by
      rw [UInt64.lt_iff_toNat_lt]; exact Nat.not_lt.mpr h_min
    have h_no_under_exp : ¬ n.exponent_ < minExponent := not_lt.mpr h_exp
    have h_check_false : (n.exponent_ < minExponent || n.mantissa_ < largeRange.min) = false := by
      simp [h_no_under_exp, h_no_under_mant]
    rw [h_check_false] at hok
    simp only [Bool.false_eq_true, if_false] at hok
    have h_mr_u64 : n.mantissa_ > maxRep := UInt64.lt_iff_toNat_lt.mpr h_mr
    rw [show doNormalize_capAtMaxRep n.mantissa_ n.exponent_
        (if n.negative_ then Guard.new.set_negative else Guard.new)
        = .error "Number::normalize 1.5" from by
      unfold doNormalize_capAtMaxRep; rw [if_pos h_mr_u64, if_pos h_ge]] at hok
    exact absurd hok (by intro h; cases h)
  have h_doNorm_id := doNormalize_id_towards_zero n.negative_ n.mantissa_ n.exponent_
      h_min h_max h_exp h_mod h_exp_le h_mr_exp
  unfold Number.normalize at hok
  rw [h_doNorm_id] at hok
  exact (Except.ok.inj hok ▸ by cases n; rfl)

/-! ## `.upward`-mode analogues of `doNormalize_id` / `normalize_eq_of_invariants` -/

/-- `Guard.new` rounds to `-1` in `.upward` mode. -/
lemma guard_new_round_neg_one_upward : Guard.new.round .upward = -1 := by
  unfold Guard.round Guard.new
  simp

/-- `Guard.new.set_negative` rounds to `-1` in `.upward` mode. -/
lemma guard_new_set_negative_round_neg_one_upward :
    Guard.new.set_negative.round .upward = -1 := by
  unfold Guard.round Guard.set_negative Guard.new
  simp

/-- `.upward`-mode analogue of `guard_new_doRoundUp_id`. -/
lemma guard_new_doRoundUp_id_upward
    (negative : Bool) (m : UInt64) (e : Int) (loc : String)
    (hmin : largeRange.min ≤ m) (hexp : minExponent ≤ e) (hexp_le : e ≤ maxExponent) :
    Guard.new.doRoundUp negative m e largeRange.min largeRange.max .upward loc
      = .ok { negative_ := negative, mantissa_ := m, exponent_ := e } := by
  unfold Guard.doRoundUp Guard.bringIntoRange Guard.doDropDigit
  simp only []
  rw [guard_new_round_neg_one_upward]
  have h_round_false : ((-1 : Int) == 1 || ((-1 : Int) == 0 && m % 2 == 1)) = false := by rfl
  rw [h_round_false]
  simp only [Bool.false_and, if_false, Bool.false_eq_true]
  have h_no_resc : ¬ m < largeRange.min := by
    rw [UInt64.lt_iff_toNat_lt]
    exact Nat.not_lt.mpr (UInt64.le_iff_toNat_le.mp hmin)
  rw [if_neg h_no_resc, if_neg (not_lt.mpr hexp), if_neg (not_lt.mpr hexp_le)]

/-- `.upward`-mode analogue of `guard_new_doRoundUp_divu10`. -/
lemma guard_new_doRoundUp_divu10_upward
    (negative : Bool) (m : UInt64) (e : Int) (loc : String)
    (h_gt : m > maxRep) (h_le_max : m.toNat ≤ largeRange.max.toNat)
    (h_mod : m.toNat % 10 = 0) (hexp : minExponent ≤ e) (hexp_le : e ≤ maxExponent) :
    Guard.new.doRoundUp negative (m / 10) (e + 1) largeRange.min largeRange.max .upward loc
      = .ok { negative_ := negative, mantissa_ := m, exponent_ := e } := by
  unfold Guard.doRoundUp Guard.bringIntoRange Guard.doDropDigit
  simp only []
  rw [guard_new_round_neg_one_upward]
  have h_round_false :
      ((-1 : Int) == 1 || ((-1 : Int) == 0 && (m / 10) % 2 == 1)) = false := by rfl
  rw [h_round_false]
  simp only [Bool.false_and, if_false, Bool.false_eq_true]
  have h_resc : m / 10 < largeRange.min := div_ten_lt_minMantissa h_gt h_le_max
  rw [if_pos h_resc]
  have h_no_under : ¬ e + 1 - 1 < minExponent := by push_neg; linarith
  rw [if_neg h_no_under]
  have h_mul : ((m / 10) * 10).toNat = m.toNat := mul_div_ten_cancel h_mod h_le_max
  have h_mul_eq : (m / 10) * 10 = m := UInt64.toNat_inj.mp h_mul
  have h_exp_eq : (e + 1 - 1 : ℤ) = e := by ring
  rw [h_mul_eq, h_exp_eq, if_neg (not_lt.mpr hexp_le)]

/-- `.upward`-mode analogue of `doNormalize_id`. -/
lemma doNormalize_id_upward
    (negative : Bool) (m : UInt64) (e : Int)
    (hmin : largeRange.min.toNat ≤ m.toNat)
    (hmax : m.toNat ≤ largeRange.max.toNat)
    (hexp : minExponent ≤ e)
    (hm_mod : m.toNat > maxRep.toNat → m.toNat % 10 = 0)
    (h_exp_le : e ≤ maxExponent)
    (h_mr_exp : m.toNat > maxRep.toNat → e < maxExponent) :
    doNormalize negative m e largeRange.min largeRange.max .upward
      = .ok { negative_ := negative, mantissa_ := m, exponent_ := e } := by
  unfold doNormalize
  have hm_ne_zero : m ≠ 0 := by
    intro h
    rw [h] at hmin
    rw [largeRange_min_val] at hmin
    have : (0 : UInt64).toNat = 0 := rfl
    omega
  have h_eq_false : (m == 0) = false := by
    rw [beq_eq_false_iff_ne]; exact hm_ne_zero
  rw [h_eq_false]
  simp only [Bool.false_eq_true, if_false]
  have h_min_le : largeRange.min ≤ m := UInt64.le_iff_toNat_le.mpr hmin
  have h_max_le : m ≤ largeRange.max := UInt64.le_iff_toNat_le.mpr hmax
  have h_no_under_mant : ¬ m < largeRange.min := by
    rw [UInt64.lt_iff_toNat_lt]; exact Nat.not_lt.mpr hmin
  rw [doNormalize_scaleUp_id largeRange.min m e h_min_le]
  rw [doNormalize_scaleDown_id largeRange.max m e _ h_max_le]
  simp only []
  have h_no_under_exp : ¬ e < minExponent := not_lt.mpr hexp
  have h_check_false : (e < minExponent || m < largeRange.min) = false := by
    simp [h_no_under_exp, h_no_under_mant]
  rw [h_check_false]
  simp only [Bool.false_eq_true, if_false]
  by_cases h_mr : m > maxRep
  · have h_mod : m.toNat % 10 = 0 := hm_mod (UInt64.lt_iff_toNat_lt.mp h_mr)
    have h_mod_u : m % 10 = 0 := by
      apply UInt64.toNat_inj.mp
      rw [UInt64.toNat_mod]
      change m.toNat % (10 : UInt64).toNat = 0
      have h10 : (10 : UInt64).toNat = 10 := rfl
      rw [h10]; exact h_mod
    have h_nge_exp : ¬ e ≥ maxExponent :=
      Int.not_le.mpr (h_mr_exp (UInt64.lt_iff_toNat_lt.mp h_mr))
    have h_cap_eq : ∀ (g : Guard),
        doNormalize_capAtMaxRep m e g = .ok (m / 10, e + 1, g.push 0) := by
      intro g; unfold doNormalize_capAtMaxRep
      rw [if_pos h_mr, if_neg h_nge_exp]; simp [divu10, h_mod_u]
    cases negative
    · simp only [Bool.false_eq_true, if_false]
      rw [h_cap_eq Guard.new, guard_new_push_zero]
      simp only []
      have h_rup : Guard.new.doRoundUp false (m / 10) (e + 1) largeRange.min largeRange.max
          .upward "Number::normalize 2"
          = .ok { negative_ := false, mantissa_ := m, exponent_ := e } :=
        guard_new_doRoundUp_divu10_upward false m e "Number::normalize 2" h_mr hmax h_mod hexp h_exp_le
      rw [h_rup]; rfl
    · simp only [if_true]
      have h_push_sn : Guard.new.set_negative.push 0 = Guard.new.set_negative := by
        unfold Guard.push Guard.set_negative Guard.new; simp
      rw [h_cap_eq Guard.new.set_negative, h_push_sn]
      simp only []
      have h_rup : Guard.new.set_negative.doRoundUp true (m / 10) (e + 1) largeRange.min largeRange.max
          .upward "Number::normalize 2"
          = .ok { negative_ := true, mantissa_ := m, exponent_ := e } := by
        unfold Guard.doRoundUp Guard.bringIntoRange Guard.doDropDigit; simp only []
        rw [guard_new_set_negative_round_neg_one_upward]
        have h_round_false :
            ((-1 : Int) == 1 || ((-1 : Int) == 0 && (m / 10) % 2 == 1)) = false := by rfl
        rw [h_round_false]
        simp only [Bool.false_and, if_false, Bool.false_eq_true]
        have h_resc : m / 10 < largeRange.min := div_ten_lt_minMantissa h_mr hmax
        rw [if_pos h_resc]
        have h_no_under : ¬ e + 1 - 1 < minExponent := by intro h; omega
        rw [if_neg h_no_under]
        have h_mul : ((m / 10) * 10).toNat = m.toNat := mul_div_ten_cancel h_mod hmax
        have h_mul_eq : (m / 10) * 10 = m := UInt64.toNat_inj.mp h_mul
        have h_exp_eq : (e + 1 - 1 : ℤ) = e := by ring
        rw [h_mul_eq, h_exp_eq, if_neg (not_lt.mpr h_exp_le)]
      rw [h_rup]; rfl
  · have h_cap_eq : ∀ (g : Guard),
        doNormalize_capAtMaxRep m e g = .ok (m, e, g) := by
      intro g; unfold doNormalize_capAtMaxRep; rw [if_neg h_mr]
    cases negative
    · simp only [Bool.false_eq_true, if_false]
      rw [h_cap_eq Guard.new]
      simp only []
      have h_rup : Guard.new.doRoundUp false m e largeRange.min largeRange.max .upward
          "Number::normalize 2"
          = .ok { negative_ := false, mantissa_ := m, exponent_ := e } :=
        guard_new_doRoundUp_id_upward false m e "Number::normalize 2" h_min_le hexp h_exp_le
      rw [h_rup]; rfl
    · simp only [if_true]
      rw [h_cap_eq Guard.new.set_negative]
      simp only []
      have h_rup : Guard.new.set_negative.doRoundUp true m e largeRange.min largeRange.max .upward
          "Number::normalize 2"
          = .ok { negative_ := true, mantissa_ := m, exponent_ := e } := by
        unfold Guard.doRoundUp Guard.bringIntoRange Guard.doDropDigit; simp only []
        rw [guard_new_set_negative_round_neg_one_upward]
        have h_round_false :
            ((-1 : Int) == 1 || ((-1 : Int) == 0 && m % 2 == 1)) = false := by rfl
        rw [h_round_false]
        simp only [Bool.false_and, if_false, Bool.false_eq_true]
        rw [if_neg h_no_under_mant, if_neg (not_lt.mpr hexp), if_neg (not_lt.mpr h_exp_le)]
      rw [h_rup]; rfl

/-- `.upward`-mode analogue of `Number.normalize_eq_of_invariants`. -/
lemma Number.normalize_eq_of_invariants_upward {n result : Number}
    (h_min : largeRange.min.toNat ≤ n.mantissa_.toNat)
    (h_max : n.mantissa_.toNat ≤ largeRange.max.toNat)
    (h_exp : minExponent ≤ n.exponent_)
    (h_mod : n.mantissa_.toNat > maxRep.toNat → n.mantissa_.toNat % 10 = 0)
    (hok : n.normalize largeRange.min largeRange.max .upward = .ok result) :
    result = n := by
  have h_exp_le : n.exponent_ ≤ maxExponent := by
    by_contra h
    push_neg at h
    have hm_ne_zero : n.mantissa_ ≠ 0 := by
      intro heq; rw [heq] at h_min; rw [largeRange_min_val] at h_min
      rw [show (0 : UInt64).toNat = 0 from rfl] at h_min; omega
    have h_min_le : largeRange.min ≤ n.mantissa_ := UInt64.le_iff_toNat_le.mpr h_min
    have h_max_le : n.mantissa_ ≤ largeRange.max := UInt64.le_iff_toNat_le.mpr h_max
    have h_no_under_mant : ¬ n.mantissa_ < largeRange.min := by
      rw [UInt64.lt_iff_toNat_lt]; exact Nat.not_lt.mpr h_min
    have h_no_under_exp : ¬ n.exponent_ < minExponent := not_lt.mpr h_exp
    unfold Number.normalize doNormalize at hok
    rw [beq_eq_false_iff_ne.mpr hm_ne_zero] at hok
    simp only [Bool.false_eq_true, if_false] at hok
    rw [doNormalize_scaleUp_id largeRange.min n.mantissa_ n.exponent_ h_min_le] at hok
    rw [doNormalize_scaleDown_id largeRange.max n.mantissa_ n.exponent_ _ h_max_le] at hok
    simp only [] at hok
    have h_check_false : (n.exponent_ < minExponent || n.mantissa_ < largeRange.min) = false := by
      simp [h_no_under_exp, h_no_under_mant]
    rw [h_check_false] at hok
    simp only [Bool.false_eq_true, if_false] at hok
    by_cases h_mr : n.mantissa_ > maxRep
    · rw [show doNormalize_capAtMaxRep n.mantissa_ n.exponent_
          (if n.negative_ then Guard.new.set_negative else Guard.new)
          = .error "Number::normalize 1.5" from by
        unfold doNormalize_capAtMaxRep; rw [if_pos h_mr, if_pos (le_of_lt h)]] at hok
      exact absurd hok (by intro h; cases h)
    · cases hn : n.negative_
      · simp only [hn, Bool.false_eq_true, if_false] at hok
        rw [show doNormalize_capAtMaxRep n.mantissa_ n.exponent_ Guard.new
            = .ok (n.mantissa_, n.exponent_, Guard.new) from by
          unfold doNormalize_capAtMaxRep; rw [if_neg h_mr]] at hok
        simp only [] at hok
        have h_rup_err : Guard.new.doRoundUp false n.mantissa_ n.exponent_
            largeRange.min largeRange.max .upward "Number::normalize 2"
            = .error "Number::normalize 2" := by
          unfold Guard.doRoundUp Guard.bringIntoRange Guard.doDropDigit; simp only []
          rw [guard_new_round_neg_one_upward]
          have : ((-1 : Int) == 1 || ((-1 : Int) == 0 && n.mantissa_ % 2 == 1)) = false := by rfl
          rw [this]; simp only [Bool.false_and, if_false, Bool.false_eq_true]
          rw [if_neg h_no_under_mant, if_neg h_no_under_exp, if_pos h]
        rw [h_rup_err] at hok; exact absurd hok (by intro hc; cases hc)
      · simp only [hn, if_true] at hok
        rw [show doNormalize_capAtMaxRep n.mantissa_ n.exponent_ Guard.new.set_negative
            = .ok (n.mantissa_, n.exponent_, Guard.new.set_negative) from by
          unfold doNormalize_capAtMaxRep; rw [if_neg h_mr]] at hok
        simp only [] at hok
        have h_rup_err : Guard.new.set_negative.doRoundUp true n.mantissa_ n.exponent_
            largeRange.min largeRange.max .upward "Number::normalize 2"
            = .error "Number::normalize 2" := by
          unfold Guard.doRoundUp Guard.bringIntoRange Guard.doDropDigit; simp only []
          rw [guard_new_set_negative_round_neg_one_upward]
          have : ((-1 : Int) == 1 || ((-1 : Int) == 0 && n.mantissa_ % 2 == 1)) = false := by rfl
          rw [this]; simp only [Bool.false_and, if_false, Bool.false_eq_true]
          rw [if_neg h_no_under_mant, if_neg h_no_under_exp, if_pos h]
        rw [h_rup_err] at hok; exact absurd hok (by intro hc; cases hc)
  have h_mr_exp : n.mantissa_.toNat > maxRep.toNat → n.exponent_ < maxExponent := by
    intro h_mr
    by_contra h_ge; push_neg at h_ge
    have h_eq_max : n.exponent_ = maxExponent := Int.le_antisymm h_exp_le h_ge
    unfold Number.normalize at hok; unfold doNormalize at hok
    have hm_ne_zero : n.mantissa_ ≠ 0 := by
      intro h; rw [h] at h_mr; rw [show (0 : UInt64).toNat = 0 from rfl] at h_mr
      exact absurd h_mr (by rw [maxRep_val]; omega)
    rw [beq_eq_false_iff_ne.mpr hm_ne_zero] at hok
    simp only [Bool.false_eq_true, if_false] at hok
    have h_min_le : largeRange.min ≤ n.mantissa_ := UInt64.le_iff_toNat_le.mpr h_min
    have h_max_le : n.mantissa_ ≤ largeRange.max := UInt64.le_iff_toNat_le.mpr h_max
    rw [doNormalize_scaleUp_id largeRange.min n.mantissa_ n.exponent_ h_min_le] at hok
    rw [doNormalize_scaleDown_id largeRange.max n.mantissa_ n.exponent_ _ h_max_le] at hok
    simp only [] at hok
    have h_no_under_mant : ¬ n.mantissa_ < largeRange.min := by
      rw [UInt64.lt_iff_toNat_lt]; exact Nat.not_lt.mpr h_min
    have h_no_under_exp : ¬ n.exponent_ < minExponent := not_lt.mpr h_exp
    have h_check_false : (n.exponent_ < minExponent || n.mantissa_ < largeRange.min) = false := by
      simp [h_no_under_exp, h_no_under_mant]
    rw [h_check_false] at hok
    simp only [Bool.false_eq_true, if_false] at hok
    have h_mr_u64 : n.mantissa_ > maxRep := UInt64.lt_iff_toNat_lt.mpr h_mr
    rw [show doNormalize_capAtMaxRep n.mantissa_ n.exponent_
        (if n.negative_ then Guard.new.set_negative else Guard.new)
        = .error "Number::normalize 1.5" from by
      unfold doNormalize_capAtMaxRep; rw [if_pos h_mr_u64, if_pos h_ge]] at hok
    exact absurd hok (by intro h; cases h)
  have h_doNorm_id := doNormalize_id_upward n.negative_ n.mantissa_ n.exponent_
      h_min h_max h_exp h_mod h_exp_le h_mr_exp
  unfold Number.normalize at hok
  rw [h_doNorm_id] at hok
  exact (Except.ok.inj hok ▸ by cases n; rfl)

end XRPL.Model.Protocol
