import Mathlib.Tactic

import XRPL.Properties.Protocol.Number.Common.Rounding.DoRoundUp

namespace XRPL.Model.Protocol


/-! # `Number.normalize` is an identity for `doRoundUp` output

When the input satisfies `minMantissa ≤ m ≤ maxMantissa`, `minExponent ≤ e ≤ maxExponent`,
and `m > maxRep → m % 10 = 0`, `doNormalize` returns the input unchanged. -/

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

/-- Any EMPTY guard rounds to the sentinel `-2` in any mode. -/
lemma empty_guard_round_neg_two (g : Guard) (mode : rounding_mode) (h_empty : g.empty = true) :
    g.round mode = -2 := by
  unfold Guard.round; rw [if_pos h_empty]

/-- For an EMPTY guard `g`, `m ≥ minMantissa`, `m` NOT strictly in the cusp `(maxRep, maxRepUp)`
(so `pushOverflow` is a no-op), `e ∈ [minExponent, maxExponent]`: `g.doRoundUp` returns
`.ok { negative, m, e }` (identity) in ANY mode. The empty guard forces `round = -2`. -/
lemma empty_guard_doRoundUp_id
    (g : Guard) (mode : rounding_mode)
    (negative : Bool) (m : UInt64) (e : Int) (loc : String)
    (h_empty : g.empty = true)
    (hmin : largeRange.min ≤ m)
    (h_no_push : ¬ (maxRep < m ∧ m < maxRepUp))
    (hexp : minExponent ≤ e)
    (hexp_le : e ≤ maxExponent) :
    g.doRoundUp negative m e largeRange.min largeRange.max mode loc
      = .ok { negative_ := negative, mantissa_ := m, exponent_ := e } := by
  have h_no_resc : ¬ m < largeRange.min := by
    rw [UInt64.lt_iff_toNat_lt]; exact Nat.not_lt.mpr (UInt64.le_iff_toNat_le.mp hmin)
  have h_m_ne : m ≠ 0 := by
    intro h; rw [h] at hmin
    rw [UInt64.le_iff_toNat_le] at hmin; simp [largeRange_min_val] at hmin
  unfold Guard.doRoundUp
  simp only []
  rw [pushOverflow_noop_of_empty g mode h_empty h_no_push,
      empty_guard_round_neg_two g mode h_empty]
  have h_round_false : ((-2 : Int) == 1 || ((-2 : Int) == 0 && m % 2 == 1)) = false := by rfl
  rw [h_round_false]
  simp only [if_false, Bool.false_eq_true]
  rw [if_neg h_no_push]
  unfold Guard.bringIntoRange
  rw [if_neg (show ¬ (m < largeRange.min ∧ m ≠ 0) from fun h => h_no_resc h.1)]
  simp only []
  rw [if_neg (show ¬ (e < minExponent ∨ m = 0) from not_or.mpr ⟨not_lt.mpr hexp, h_m_ne⟩),
      if_neg (not_lt.mpr hexp_le)]

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
  have h10 : (10 : UInt64).toNat = 10 := uint64_ten_toNat
  rw [h10]
  omega

/-- `(m / 10) * 10 = m` when `m % 10 = 0` (in UInt64). -/
lemma mul_div_ten_cancel {m : UInt64}
    (h_mod : m.toNat % 10 = 0)
    (h_le_max : m.toNat ≤ largeRange.max.toNat) :
    ((m / 10) * 10).toNat = m.toNat := by
  rw [UInt64.toNat_mul, UInt64.toNat_div]
  have h10 : (10 : UInt64).toNat = 10 := uint64_ten_toNat
  rw [h10]
  rw [largeRange_max_val] at h_le_max
  have h_mul : m.toNat / 10 * 10 = m.toNat := by omega
  rw [h_mul]
  exact Nat.mod_eq_of_lt (by omega)

/-- For an EMPTY guard, `m > maxRep`, `m ≤ maxMantissa`, `m % 10 = 0`, `e ≤ maxExponent`:
`doRoundUp` after `divu10` (input `m/10 < minMantissa`) rescales back to `.ok (m, e)`. -/
lemma empty_guard_doRoundUp_divu10
    (g : Guard) (mode : rounding_mode)
    (negative : Bool) (m : UInt64) (e : Int) (loc : String)
    (h_empty : g.empty = true)
    (h_gt : m > maxRep)
    (h_le_max : m.toNat ≤ largeRange.max.toNat)
    (h_mod : m.toNat % 10 = 0)
    (hexp : minExponent ≤ e)
    (hexp_le : e ≤ maxExponent) :
    g.doRoundUp negative (m / 10) (e + 1) largeRange.min largeRange.max mode loc
      = .ok { negative_ := negative, mantissa_ := m, exponent_ := e } := by
  have h_resc : m / 10 < largeRange.min := div_ten_lt_minMantissa h_gt h_le_max
  have h_div_le_maxRep : (m / 10).toNat ≤ maxRep.toNat := by
    have h1 := UInt64.lt_iff_toNat_lt.mp h_resc
    rw [largeRange_min_val] at h1; rw [maxRep_val]; omega
  have h_div_ne : m / 10 ≠ 0 := by
    intro hh
    have h0 : (m / 10).toNat = 0 := by rw [hh]; rfl
    rw [UInt64.toNat_div] at h0
    have hm := UInt64.lt_iff_toNat_lt.mp h_gt
    rw [maxRep_val] at hm
    have h10 : (10 : UInt64).toNat = 10 := uint64_ten_toNat
    rw [h10] at h0; omega
  have h_m_ne : m ≠ 0 := by
    intro h; rw [h] at h_gt; exact absurd h_gt (by decide)
  unfold Guard.doRoundUp
  simp only []
  rw [pushOverflow_noop_of_le_maxRep_of_empty h_div_le_maxRep g mode h_empty,
      empty_guard_round_neg_two g mode h_empty]
  have h_round_false :
      ((-2 : Int) == 1 || ((-2 : Int) == 0 && (m / 10) % 2 == 1)) = false := by rfl
  rw [h_round_false]
  simp only [if_false, Bool.false_eq_true]
  have h_not_cusp : ¬ (maxRep < m / 10 ∧ m / 10 < maxRepUp) := by
    intro h; exact absurd (UInt64.lt_iff_toNat_lt.mp h.1) (Nat.not_lt.mpr h_div_le_maxRep)
  rw [if_neg h_not_cusp]
  unfold Guard.bringIntoRange
  rw [if_pos (show m / 10 < largeRange.min ∧ m / 10 ≠ 0 from ⟨h_resc, h_div_ne⟩)]
  simp only []
  have h_mul : ((m / 10) * 10).toNat = m.toNat := mul_div_ten_cancel h_mod h_le_max
  have h_mul_eq : (m / 10) * 10 = m := UInt64.toNat_inj.mp h_mul
  have h_exp_eq : (e + 1 - 1 : ℤ) = e := by ring
  rw [h_mul_eq, h_exp_eq]
  rw [if_neg (show ¬ (e < minExponent ∨ m = 0) from not_or.mpr ⟨not_lt.mpr hexp, h_m_ne⟩),
      if_neg (not_lt.mpr hexp_le)]

/-- For an EMPTY guard, `m ≥ minMantissa`, `m` not in the strict cusp, and `e > maxExponent`:
`doRoundUp` errors (the final exponent check fails). Used to derive exponent bounds from success. -/
lemma empty_guard_doRoundUp_exp_overflow
    (g : Guard) (mode : rounding_mode)
    (negative : Bool) (m : UInt64) (e : Int) (loc : String)
    (h_empty : g.empty = true)
    (hmin : largeRange.min ≤ m)
    (h_no_push : ¬ (maxRep < m ∧ m < maxRepUp))
    (hexp : minExponent ≤ e)
    (h_over : maxExponent < e) :
    g.doRoundUp negative m e largeRange.min largeRange.max mode loc = .error loc := by
  have h_no_resc : ¬ m < largeRange.min := by
    rw [UInt64.lt_iff_toNat_lt]; exact Nat.not_lt.mpr (UInt64.le_iff_toNat_le.mp hmin)
  have h_m_ne : m ≠ 0 := by
    intro h; rw [h] at hmin
    rw [UInt64.le_iff_toNat_le] at hmin; simp [largeRange_min_val] at hmin
  unfold Guard.doRoundUp
  simp only []
  rw [pushOverflow_noop_of_empty g mode h_empty h_no_push,
      empty_guard_round_neg_two g mode h_empty]
  have h_round_false : ((-2 : Int) == 1 || ((-2 : Int) == 0 && m % 2 == 1)) = false := by rfl
  rw [h_round_false]
  simp only [if_false, Bool.false_eq_true]
  rw [if_neg h_no_push]
  unfold Guard.bringIntoRange
  rw [if_neg (show ¬ (m < largeRange.min ∧ m ≠ 0) from fun h => h_no_resc h.1)]
  simp only []
  rw [if_neg (show ¬ (e < minExponent ∨ m = 0) from not_or.mpr ⟨not_lt.mpr hexp, h_m_ne⟩),
      if_pos h_over]

/-- `doNormalize` under the hypotheses returns `.ok` with the input Number. -/
lemma doNormalize_id
    (mode : rounding_mode)
    (negative : Bool) (m : UInt64) (e : Int)
    (hmin : largeRange.min.toNat ≤ m.toNat)
    (hmax : m.toNat ≤ largeRange.max.toNat)
    (hexp : minExponent ≤ e)
    (hm_mod : m.toNat > maxRep.toNat → m.toNat % 10 = 0)
    (h_exp_le : e ≤ maxExponent)
    (h_mru_exp : m.toNat > maxRepUp.toNat → e < maxExponent) :
    doNormalize negative m e largeRange.min largeRange.max mode
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
  by_cases h_mru : m > maxRepUp
  · -- m > maxRepUp: capAtMaxRep divides via divu10.
    have h_mr : m > maxRep := by
      change maxRep < m
      rw [UInt64.lt_iff_toNat_lt, maxRep_val]
      have h1 := UInt64.lt_iff_toNat_lt.mp h_mru
      have hup : maxRepUp.toNat = maxRepUpNat := rfl
      rw [hup] at h1; omega
    have h_mod : m.toNat % 10 = 0 := hm_mod (UInt64.lt_iff_toNat_lt.mp h_mr)
    have h_mod_u : m % 10 = 0 := by
      apply UInt64.toNat_inj.mp
      rw [UInt64.toNat_mod]
      change m.toNat % (10 : UInt64).toNat = 0
      have h10 : (10 : UInt64).toNat = 10 := uint64_ten_toNat
      rw [h10]; exact h_mod
    have h_nge_exp : ¬ e ≥ maxExponent :=
      Int.not_le.mpr (h_mru_exp (UInt64.lt_iff_toNat_lt.mp h_mru))
    have h_cap_eq : ∀ (g : Guard),
        doNormalize_capAtMaxRep m e g = .ok (m / 10, e + 1, g.push 0) := by
      intro g
      unfold doNormalize_capAtMaxRep
      rw [if_pos h_mru, if_neg h_nge_exp]
      simp [divu10, h_mod_u]
    cases negative
    · simp only [Bool.false_eq_true, if_false]
      rw [h_cap_eq Guard.new, guard_new_push_zero]
      simp only []
      rw [empty_guard_doRoundUp_divu10 Guard.new mode false m e "Number::normalize 2"
        (by decide) h_mr hmax h_mod hexp h_exp_le]
      rfl
    · simp only [if_true]
      have h_push_sn : Guard.new.set_negative.push 0 = Guard.new.set_negative := by
        unfold Guard.push Guard.set_negative Guard.new; simp
      rw [h_cap_eq Guard.new.set_negative, h_push_sn]
      simp only []
      rw [empty_guard_doRoundUp_divu10 Guard.new.set_negative mode true m e "Number::normalize 2"
        (by decide) h_mr hmax h_mod hexp h_exp_le]
      rfl
  · -- m ≤ maxRepUp: capAtMaxRep is identity; pushOverflow is a no-op for valid inputs.
    have h_cap_eq : ∀ (g : Guard),
        doNormalize_capAtMaxRep m e g = .ok (m, e, g) := by
      intro g
      unfold doNormalize_capAtMaxRep
      rw [if_neg h_mru]
    -- No multiple-of-10 lies strictly in `(maxRep, maxRepUp)`, so `pushOverflow` is a no-op.
    have h_no_push : ¬ (maxRep < m ∧ m < maxRepUp) := by
      rintro ⟨h1, h2⟩
      have hm10 : m.toNat % 10 = 0 := hm_mod (UInt64.lt_iff_toNat_lt.mp h1)
      have hb1 := UInt64.lt_iff_toNat_lt.mp h1
      have hb2 := UInt64.lt_iff_toNat_lt.mp h2
      rw [maxRep_val] at hb1
      have hup : maxRepUp.toNat = maxRepUpNat := rfl
      rw [hup] at hb2; omega
    cases negative
    · simp only [Bool.false_eq_true, if_false]
      rw [h_cap_eq Guard.new]
      simp only []
      rw [empty_guard_doRoundUp_id Guard.new mode false m e "Number::normalize 2"
        (by decide) h_min_le h_no_push hexp h_exp_le]
      rfl
    · simp only [if_true]
      rw [h_cap_eq Guard.new.set_negative]
      simp only []
      rw [empty_guard_doRoundUp_id Guard.new.set_negative mode true m e "Number::normalize 2"
        (by decide) h_min_le h_no_push hexp h_exp_le]
      rfl

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
lemma Number.normalize_eq_of_invariants {n result : Number} {mode : rounding_mode}
    (h_min : largeRange.min.toNat ≤ n.mantissa_.toNat)
    (h_max : n.mantissa_.toNat ≤ largeRange.max.toNat)
    (h_exp : minExponent ≤ n.exponent_)
    (h_mod : n.mantissa_.toNat > maxRep.toNat → n.mantissa_.toNat % 10 = 0)
    (hok : n.normalize largeRange.min largeRange.max mode = .ok result) :
    result = n := by
  have hm_ne_zero : n.mantissa_ ≠ 0 := by
    intro heq; rw [heq] at h_min; rw [largeRange_min_val] at h_min
    rw [show (0 : UInt64).toNat = 0 from rfl] at h_min; omega
  have h_min_le : largeRange.min ≤ n.mantissa_ := UInt64.le_iff_toNat_le.mpr h_min
  have h_max_le : n.mantissa_ ≤ largeRange.max := UInt64.le_iff_toNat_le.mpr h_max
  have h_no_under_mant : ¬ n.mantissa_ < largeRange.min := by
    rw [UInt64.lt_iff_toNat_lt]; exact Nat.not_lt.mpr h_min
  have h_no_under_exp : ¬ n.exponent_ < minExponent := not_lt.mpr h_exp
  have h_check_false : (n.exponent_ < minExponent || n.mantissa_ < largeRange.min) = false := by
    simp [h_no_under_exp, h_no_under_mant]
  have h_no_push : ¬ (maxRep < n.mantissa_ ∧ n.mantissa_ < maxRepUp) := by
    rintro ⟨h1, h2⟩
    have hm10 := h_mod (UInt64.lt_iff_toNat_lt.mp h1)
    have hb1 := UInt64.lt_iff_toNat_lt.mp h1
    have hb2 := UInt64.lt_iff_toNat_lt.mp h2
    rw [maxRep_val] at hb1
    have hup : maxRepUp.toNat = maxRepUpNat := rfl
    rw [hup] at hb2; omega
  have h_exp_le : n.exponent_ ≤ maxExponent := by
    by_contra h; push_neg at h
    unfold Number.normalize doNormalize at hok
    rw [beq_eq_false_iff_ne.mpr hm_ne_zero] at hok
    simp only [Bool.false_eq_true, if_false] at hok
    rw [doNormalize_scaleUp_id largeRange.min n.mantissa_ n.exponent_ h_min_le] at hok
    rw [doNormalize_scaleDown_id largeRange.max n.mantissa_ n.exponent_ _ h_max_le] at hok
    simp only [] at hok
    rw [h_check_false] at hok
    simp only [Bool.false_eq_true, if_false] at hok
    by_cases h_mru : n.mantissa_ > maxRepUp
    · rw [show doNormalize_capAtMaxRep n.mantissa_ n.exponent_
          (if n.negative_ then Guard.new.set_negative else Guard.new)
          = .error "Number::normalize 1.5" from by
        unfold doNormalize_capAtMaxRep; rw [if_pos h_mru, if_pos (le_of_lt h)]] at hok
      exact absurd hok (by intro hc; cases hc)
    · cases hn : n.negative_
      · simp only [hn, Bool.false_eq_true, if_false] at hok
        rw [show doNormalize_capAtMaxRep n.mantissa_ n.exponent_ Guard.new
            = .ok (n.mantissa_, n.exponent_, Guard.new) from by
          unfold doNormalize_capAtMaxRep; rw [if_neg h_mru]] at hok
        simp only [] at hok
        rw [empty_guard_doRoundUp_exp_overflow Guard.new mode false n.mantissa_ n.exponent_
          "Number::normalize 2" (by decide) h_min_le h_no_push h_exp h] at hok
        exact absurd hok (by intro hc; cases hc)
      · simp only [hn, if_true] at hok
        rw [show doNormalize_capAtMaxRep n.mantissa_ n.exponent_ Guard.new.set_negative
            = .ok (n.mantissa_, n.exponent_, Guard.new.set_negative) from by
          unfold doNormalize_capAtMaxRep; rw [if_neg h_mru]] at hok
        simp only [] at hok
        rw [empty_guard_doRoundUp_exp_overflow Guard.new.set_negative mode true n.mantissa_
          n.exponent_ "Number::normalize 2" (by decide) h_min_le h_no_push h_exp h] at hok
        exact absurd hok (by intro hc; cases hc)
  have h_mru_exp : n.mantissa_.toNat > maxRepUp.toNat → n.exponent_ < maxExponent := by
    intro h_mru_nat
    by_contra h_ge; push_neg at h_ge
    unfold Number.normalize doNormalize at hok
    rw [beq_eq_false_iff_ne.mpr hm_ne_zero] at hok
    simp only [Bool.false_eq_true, if_false] at hok
    rw [doNormalize_scaleUp_id largeRange.min n.mantissa_ n.exponent_ h_min_le] at hok
    rw [doNormalize_scaleDown_id largeRange.max n.mantissa_ n.exponent_ _ h_max_le] at hok
    simp only [] at hok
    rw [h_check_false] at hok
    simp only [Bool.false_eq_true, if_false] at hok
    have h_mru_u64 : n.mantissa_ > maxRepUp := UInt64.lt_iff_toNat_lt.mpr h_mru_nat
    rw [show doNormalize_capAtMaxRep n.mantissa_ n.exponent_
        (if n.negative_ then Guard.new.set_negative else Guard.new)
        = .error "Number::normalize 1.5" from by
      unfold doNormalize_capAtMaxRep; rw [if_pos h_mru_u64, if_pos h_ge]] at hok
    exact absurd hok (by intro hc; cases hc)
  have h_doNorm_id : doNormalize n.negative_ n.mantissa_ n.exponent_
      largeRange.min largeRange.max mode
      = .ok { negative_ := n.negative_, mantissa_ := n.mantissa_, exponent_ := n.exponent_ } :=
    doNormalize_id mode n.negative_ n.mantissa_ n.exponent_ h_min h_max h_exp h_mod h_exp_le h_mru_exp
  unfold Number.normalize at hok
  rw [h_doNorm_id] at hok
  have h_eq := Except.ok.inj hok
  cases n
  simpa using h_eq.symm

/-! ## Mode-generic helpers: extract exponent bounds from `normalize` success -/

lemma Number.normalize_exp_bound {n result : Number} {mode : rounding_mode}
    (h_min : largeRange.min.toNat ≤ n.mantissa_.toNat)
    (h_max : n.mantissa_.toNat ≤ largeRange.max.toNat)
    (h_exp : minExponent ≤ n.exponent_)
    (h_mod : n.mantissa_.toNat > maxRep.toNat → n.mantissa_.toNat % 10 = 0)
    (hok : n.normalize largeRange.min largeRange.max mode = .ok result) :
    n.exponent_ ≤ maxExponent := by
  have hm_ne_zero : n.mantissa_ ≠ 0 := by
    intro heq; rw [heq] at h_min; rw [largeRange_min_val] at h_min
    rw [show (0 : UInt64).toNat = 0 from rfl] at h_min; omega
  have h_min_le : largeRange.min ≤ n.mantissa_ := UInt64.le_iff_toNat_le.mpr h_min
  have h_max_le : n.mantissa_ ≤ largeRange.max := UInt64.le_iff_toNat_le.mpr h_max
  have h_no_under_mant : ¬ n.mantissa_ < largeRange.min := by
    rw [UInt64.lt_iff_toNat_lt]; exact Nat.not_lt.mpr h_min
  have h_no_under_exp : ¬ n.exponent_ < minExponent := not_lt.mpr h_exp
  have h_check_false : (n.exponent_ < minExponent || n.mantissa_ < largeRange.min) = false := by
    simp [h_no_under_exp, h_no_under_mant]
  have h_no_push : ¬ (maxRep < n.mantissa_ ∧ n.mantissa_ < maxRepUp) := by
    rintro ⟨h1, h2⟩
    have hm10 := h_mod (UInt64.lt_iff_toNat_lt.mp h1)
    have hb1 := UInt64.lt_iff_toNat_lt.mp h1
    have hb2 := UInt64.lt_iff_toNat_lt.mp h2
    rw [maxRep_val] at hb1
    have hup : maxRepUp.toNat = maxRepUpNat := rfl
    rw [hup] at hb2; omega
  by_contra h; push_neg at h
  unfold Number.normalize doNormalize at hok
  rw [beq_eq_false_iff_ne.mpr hm_ne_zero] at hok
  simp only [Bool.false_eq_true, if_false] at hok
  rw [doNormalize_scaleUp_id largeRange.min n.mantissa_ n.exponent_ h_min_le] at hok
  rw [doNormalize_scaleDown_id largeRange.max n.mantissa_ n.exponent_ _ h_max_le] at hok
  simp only [] at hok
  rw [h_check_false] at hok
  simp only [Bool.false_eq_true, if_false] at hok
  by_cases h_mru : n.mantissa_ > maxRepUp
  · rw [show doNormalize_capAtMaxRep n.mantissa_ n.exponent_
        (if n.negative_ then Guard.new.set_negative else Guard.new)
        = .error "Number::normalize 1.5" from by
      unfold doNormalize_capAtMaxRep; rw [if_pos h_mru, if_pos (le_of_lt h)]] at hok
    exact absurd hok (by intro hc; cases hc)
  · cases hn : n.negative_
    · simp only [hn, Bool.false_eq_true, if_false] at hok
      rw [show doNormalize_capAtMaxRep n.mantissa_ n.exponent_ Guard.new
          = .ok (n.mantissa_, n.exponent_, Guard.new) from by
        unfold doNormalize_capAtMaxRep; rw [if_neg h_mru]] at hok
      simp only [] at hok
      rw [empty_guard_doRoundUp_exp_overflow Guard.new mode false n.mantissa_ n.exponent_
        "Number::normalize 2" (by decide) h_min_le h_no_push h_exp h] at hok
      exact absurd hok (by intro hc; cases hc)
    · simp only [hn, if_true] at hok
      rw [show doNormalize_capAtMaxRep n.mantissa_ n.exponent_ Guard.new.set_negative
          = .ok (n.mantissa_, n.exponent_, Guard.new.set_negative) from by
        unfold doNormalize_capAtMaxRep; rw [if_neg h_mru]] at hok
      simp only [] at hok
      rw [empty_guard_doRoundUp_exp_overflow Guard.new.set_negative mode true n.mantissa_
        n.exponent_ "Number::normalize 2" (by decide) h_min_le h_no_push h_exp h] at hok
      exact absurd hok (by intro hc; cases hc)

lemma Number.normalize_no_cusp_overflow {n result : Number} {mode : rounding_mode}
    (h_min : largeRange.min.toNat ≤ n.mantissa_.toNat)
    (h_max : n.mantissa_.toNat ≤ largeRange.max.toNat)
    (h_exp : minExponent ≤ n.exponent_)
    (hok : n.normalize largeRange.min largeRange.max mode = .ok result) :
    ¬ (n.mantissa_.toNat > maxRepUp.toNat ∧ n.exponent_ ≥ maxExponent) := by
  intro ⟨h_mr, h_ge⟩
  have hm_ne_zero : n.mantissa_ ≠ 0 := by
    intro heq; rw [heq] at h_mr; rw [show (0 : UInt64).toNat = 0 from rfl] at h_mr
    rw [show maxRepUp.toNat = maxRepUpNat from rfl] at h_mr; omega
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

/-- Given the four `doRoundUp` output invariants for a `RoundResult` `res` and
that `normalize res = result`, the result is normalized. The shared tail of every
`operator_*_result_isNormalized_*`: mode-generic (composes the mode-independent
`normalize_eq_of_invariants` + `normalize_exp_bound`), so the per-mode callers
differ only in the `doRoundUp_output_invariants_*` they feed in. -/
lemma Number.normalize_isNormalized_of_invariants {res : RoundResult} {result : Number}
    {mode : rounding_mode}
    (h_min : largeRange.min.toNat ≤ res.mantissa_.toNat)
    (h_max : res.mantissa_.toNat ≤ largeRange.max.toNat)
    (h_exp : minExponent ≤ res.exponent_)
    (h_mod : res.mantissa_.toNat > maxRep.toNat → res.mantissa_.toNat % 10 = 0)
    (hok : res.toNumber.normalize largeRange.min largeRange.max mode = .ok result) :
    result.isNormalized := by
  have h_result_eq_res : result = res.toNumber :=
    Number.normalize_eq_of_invariants h_min h_max h_exp h_mod hok
  rw [h_result_eq_res]
  right
  refine ⟨?_, ?_, ?_, h_exp, ?_⟩
  · change largeRange.min ≤ res.mantissa_
    rw [UInt64.le_iff_toNat_le]; exact h_min
  · change res.mantissa_ ≤ largeRange.max
    rw [UInt64.le_iff_toNat_le]; exact h_max
  · by_cases h_cusp : res.mantissa_.toNat ≤ maxRep.toNat
    · left
      change res.mantissa_ ≤ maxRep
      rw [UInt64.le_iff_toNat_le]; exact h_cusp
    · push_neg at h_cusp
      right
      change res.mantissa_.toNat % 10 = 0
      exact h_mod h_cusp
  · show res.toNumber.exponent_ ≤ maxExponent
    exact Number.normalize_exp_bound h_min h_max h_exp h_mod hok

/-- Given the same `doRoundUp` output invariants, a normalized result sitting at
or above `maxExponent` has mantissa `≤ maxRepUp`. Shared tail of every
`operator_*_no_overflow_mantissa_*`. -/
lemma Number.normalize_mantissa_le_maxRepUp_of_invariants {res : RoundResult} {result : Number}
    {mode : rounding_mode}
    (h_min : largeRange.min.toNat ≤ res.mantissa_.toNat)
    (h_max : res.mantissa_.toNat ≤ largeRange.max.toNat)
    (h_exp : minExponent ≤ res.exponent_)
    (h_mod : res.mantissa_.toNat > maxRep.toNat → res.mantissa_.toNat % 10 = 0)
    (hok : res.toNumber.normalize largeRange.min largeRange.max mode = .ok result)
    (h_exp_ge : result.exponent_ ≥ maxExponent) :
    result.mantissa_ ≤ maxRepUp := by
  have h_result_eq_res : result = res.toNumber :=
    Number.normalize_eq_of_invariants h_min h_max h_exp h_mod hok
  by_contra h_not
  rw [h_result_eq_res] at h_not h_exp_ge
  have h_gt : res.toNumber.mantissa_.toNat > maxRepUp.toNat :=
    Nat.lt_of_not_le (mt UInt64.le_iff_toNat_le.mpr h_not)
  exact absurd ⟨h_gt, h_exp_ge⟩
    (Number.normalize_no_cusp_overflow h_min h_max h_exp hok)


end XRPL.Model.Protocol
