import XRPL.Properties.Protocol.Number.Common.Notation
import Mathlib.Tactic

import XRPL.Properties.Protocol.Number.Common.ToRatLemmas
import XRPL.Properties.Protocol.Number.Rounding.ScaleDown

set_option linter.style.longLine false
set_option linter.style.emptyLine false
set_option linter.unusedTactic false
set_option linter.unusedSimpArgs false
set_option linter.unreachableTactic false

namespace XRPL.Model.Protocol

/-! # Error bound for `Guard.doRoundUp`

Absolute error of `doRoundUp` is at most `8 · 10^e`. The cusp case
(overflow + rescale at `m = maxRep`) dominates; non-cusp branches are ≤ `(1/2) · 10^e`.
-/

/-- `RoundResult` value as a rational. -/
noncomputable def RoundResult.toRat (r : RoundResult) : ℚ := r.toNumber.toRat

/-! ## Helper facts -/

/-- `maxRep.toNat = maxRepNat = 2^63 - 1`. -/
lemma maxRep_val : maxRep.toNat = maxRepNat := by decide

/-- `largeRange.min.toNat = 1000000000000000000`. -/
lemma largeRange_min_val : largeRange.min.toNat = 1000000000000000000 := by decide

/-- `largeRange.max.toNat = 9999999999999999999`. -/
lemma largeRange_max_val : largeRange.max.toNat = 9999999999999999999 := by decide

/-- `m + 1` does not overflow UInt64 when `m ≤ maxRep`. -/
lemma m_add_one_no_overflow {m : UInt64} (h : m.toNat ≤ maxRep.toNat) :
    (m + 1).toNat = m.toNat + 1 := by
  rw [UInt64.toNat_add]
  have h1 : (1 : UInt64).toNat = 1 := rfl
  rw [h1]
  have : m.toNat + 1 < 2 ^ 64 := by
    rw [maxRep_val] at h; omega
  exact Nat.mod_eq_of_lt this

/-- `m * 10` does not overflow UInt64 when `m < largeRange.min`. -/
lemma m_mul_ten_no_overflow {m : UInt64}
    (h : m.toNat < largeRange.min.toNat) :
    (m * 10).toNat = m.toNat * 10 := by
  rw [UInt64.toNat_mul]
  have h10 : (10 : UInt64).toNat = 10 := rfl
  rw [h10]
  have : m.toNat * 10 < 2 ^ 64 := by
    rw [largeRange_min_val] at h
    calc m.toNat * 10 < 1000000000000000000 * 10 := by
          exact (Nat.mul_lt_mul_right (by norm_num : 0 < 10)).mpr h
      _ = tenPow19 := by norm_num
      _ < 2 ^ 64 := by norm_num
  exact Nat.mod_eq_of_lt this

/-! ## Main error bound -/
/-! ## Facts about `represents` -/

/-- `represents g f` implies `0 ≤ f`. -/
lemma represents_nonneg {g : Guard} {f : ℚ} (hrep : represents g f) : 0 ≤ f := by
  obtain ⟨x, hx_nn, _, hf_eq, _, _⟩ := hrep
  rw [hf_eq]
  have : (0 : ℚ) ≤ (decimalValue g.digits_ : ℚ) / 10 ^ 16 := by positivity
  linarith

/-- `represents g f` implies `f < 1`. -/
lemma represents_lt_one {g : Guard} {f : ℚ} (hrep : represents g f) : f < 1 := by
  obtain ⟨x, hx_nn, hx_lt, hf_eq, _, hall⟩ := hrep
  have hdv_lt : decimalValue g.digits_ < 10 ^ 16 := by
    unfold decimalValue
    have hbound : ∀ p ∈ Finset.range 16, nibble g.digits_ p * 10 ^ p ≤ 9 * 10 ^ p := by
      intro p hp
      have : nibble g.digits_ p ≤ 9 := hall ⟨p, Finset.mem_range.mp hp⟩
      exact Nat.mul_le_mul_right _ this
    calc ∑ p ∈ Finset.range 16, nibble g.digits_ p * 10 ^ p
        ≤ ∑ p ∈ Finset.range 16, 9 * 10 ^ p := Finset.sum_le_sum hbound
      _ < 10 ^ 16 := by decide
  rw [hf_eq]
  have hdv_q : (decimalValue g.digits_ : ℚ) < 10 ^ 16 := by exact_mod_cast hdv_lt
  have h16_pos : (0 : ℚ) < 10 ^ 16 := by positivity
  have h_top : (decimalValue g.digits_ : ℚ) / 10 ^ 16 < 1 := by
    rw [div_lt_iff₀ h16_pos]; linarith
  have hx_lt_q : x < 1 / 10 ^ 16 := hx_lt
  have : (decimalValue g.digits_ : ℚ) / 10 ^ 16 + x < 1 / 10 ^ 16 + (1 - 1 / 10 ^ 16) := by
    have hdv_lt' : (decimalValue g.digits_ : ℚ) / 10 ^ 16 ≤ 1 - 1 / 10 ^ 16 := by
      have hdv_le : decimalValue g.digits_ + 1 ≤ 10 ^ 16 := by omega
      have hdv_q' : ((decimalValue g.digits_ + 1 : ℕ) : ℚ) ≤ ((10 ^ 16 : ℕ) : ℚ) := by
        exact_mod_cast hdv_le
      push_cast at hdv_q'
      have : (decimalValue g.digits_ : ℚ) ≤ 10 ^ 16 - 1 := by linarith
      rw [div_le_iff₀ h16_pos]
      have : (10 : ℚ) ^ 16 * (1 - 1 / 10 ^ 16) = 10 ^ 16 - 1 := by
        field_simp
      linarith
    linarith
  linarith

/-! ## Structural helpers for `Guard.doRoundUp` -/

/-- If the output mantissa is non-zero, the output sign equals the input `zn`. -/
lemma doRoundUp_negative_of_mant_ne
    (g : Guard) (zn : Bool) (m : UInt64) (e : Int)
    (minMant maxMant : UInt64) (mode : rounding_mode)
    (loc : String) (res : RoundResult)
    (hok : g.doRoundUp zn m e minMant maxMant mode loc = .ok res)
    (hne : res.mantissa_ ≠ 0) :
    res.negative_ = zn := by
  unfold Guard.doRoundUp Guard.bringIntoRange at hok
  simp only [Guard.doDropDigit] at hok
  split_ifs at hok with h1 h2 h3 h4 h5 h6 h7 h8 h9 h10 <;>
    (try (simp only [Except.ok.injEq] at hok)) <;>
    (try (simp only [reduceCtorEq] at hok)) <;>
    first
    | (subst hok; exact absurd rfl hne)
    | (subst hok; rfl)

/-- Output mantissa is independent of the `negative` flag. -/
lemma doRoundUp_mantissa_indep
    (g : Guard) (neg1 neg2 : Bool) (m : UInt64) (e : Int) (mode : rounding_mode)
    (loc : String) (res1 res2 : RoundResult)
    (hok1 : g.doRoundUp neg1 m e largeRange.min largeRange.max mode loc = .ok res1)
    (hok2 : g.doRoundUp neg2 m e largeRange.min largeRange.max mode loc = .ok res2) :
    res1.mantissa_ = res2.mantissa_ := by
  unfold Guard.doRoundUp Guard.bringIntoRange at hok1 hok2
  simp only [Guard.doDropDigit] at hok1 hok2
  split_ifs at hok1 hok2 with h1 h2 h3 h4 h5 h6 h7 h8 h9 h10 h11 h12 h13 h14 <;>
    (try (simp only [Except.ok.injEq] at hok1)) <;>
    (try (simp only [Except.ok.injEq] at hok2)) <;>
    (try (simp only [reduceCtorEq] at hok1)) <;> (try (simp only [reduceCtorEq] at hok2)) <;>
    (try subst hok1) <;> (try subst hok2) <;> rfl

/-- Output exponent is independent of the `negative` flag. -/
lemma doRoundUp_exponent_indep
    (g : Guard) (neg1 neg2 : Bool) (m : UInt64) (e : Int) (mode : rounding_mode)
    (loc : String) (res1 res2 : RoundResult)
    (hok1 : g.doRoundUp neg1 m e largeRange.min largeRange.max mode loc = .ok res1)
    (hok2 : g.doRoundUp neg2 m e largeRange.min largeRange.max mode loc = .ok res2) :
    res1.exponent_ = res2.exponent_ := by
  unfold Guard.doRoundUp Guard.bringIntoRange at hok1 hok2
  simp only [Guard.doDropDigit] at hok1 hok2
  split_ifs at hok1 hok2 with h1 h2 h3 h4 h5 h6 h7 h8 h9 h10 h11 h12 h13 h14 <;>
    (try (simp only [Except.ok.injEq] at hok1)) <;>
    (try (simp only [Except.ok.injEq] at hok2)) <;>
    (try (simp only [reduceCtorEq] at hok1)) <;> (try (simp only [reduceCtorEq] at hok2)) <;>
    (try subst hok1) <;> (try subst hok2) <;> rfl



set_option maxHeartbeats 1600000 in
-- split_ifs at hok ⊢ with 10 conditions creates ~2^10 cases; extra heartbeats needed
lemma doRoundUp_false_from_ok
    (g : Guard) (zn : Bool) (m : UInt64) (e : Int) (mode : rounding_mode)
    (loc : String) (res : RoundResult)
    (hok : g.doRoundUp zn m e largeRange.min largeRange.max mode loc = .ok res) :
    g.doRoundUp false m e largeRange.min largeRange.max mode loc =
      .ok { negative_ := false, mantissa_ := res.mantissa_, exponent_ := res.exponent_ } := by
  -- Prove via the NO_OVF approach: extract that res.exponent_ ≤ maxExponent from hok,
  -- then use this to show the false case also succeeds.
  -- The exponent of doRoundUp's result doesn't depend on negative_.
  -- Use doRoundUp_exponent_indep (which we first need the false case to be .ok for).
  -- Instead: prove directly using the structure of doRoundUp.
  -- Since `lake env lean` compiles this, we use higher heartbeats via simp_all.
  unfold Guard.doRoundUp Guard.bringIntoRange at hok ⊢
  simp only [Guard.doDropDigit] at hok ⊢
  split_ifs at hok ⊢ with h1 h2 h3 h4 h5 h6 h7 h8 h9 h10 <;>
    (try (simp only [reduceCtorEq] at hok)) <;>
    (try (simp only [Except.ok.injEq] at hok)) <;>
    (try (obtain rfl := Except.ok.inj hok)) <;>
    (try subst hok) <;>
    (try rfl) <;>
    (try ring_nf)

/-! ## Value characterization lemmas

Output value `mantissa * 10^exponent` equals one of three forms depending on
whether round-up fires and whether the cusp (overflow) branch triggers. -/

/-- No round-up: output value equals `m * 10^e`. -/
lemma doRoundUp_value_noRoundUp
    (g : Guard) (m : UInt64) (e : Int)
    (h_no_ru : ¬ (g.round .to_nearest = 1 ∨
                  (g.round .to_nearest = 0 ∧ m % 2 = 1)))
    (loc : String) (res : RoundResult)
    (hok : g.doRoundUp false m e largeRange.min largeRange.max .to_nearest loc = .ok res)
    (hne : res.mantissa_ ≠ 0) :
    (res.mantissa_.toNat : ℚ) * 10 ^ res.exponent_ = (m.toNat : ℚ) * 10 ^ e := by
  have h_ru_false :
      ((g.round .to_nearest == 1) || ((g.round .to_nearest == 0) && (m % 2 == 1))) = false := by
    push_neg at h_no_ru
    obtain ⟨h1, h2⟩ := h_no_ru
    simp only [Bool.or_eq_false_iff, Bool.and_eq_false_iff, beq_eq_false_iff_ne]
    refine ⟨h1, ?_⟩
    by_cases h_rv : g.round .to_nearest = 0
    · right; exact h2 h_rv
    · left; exact h_rv
  -- Extract res = Guard.bringIntoRange ... from hok
  have hres_eq : res = Guard.bringIntoRange false m e largeRange.min := by
    unfold Guard.doRoundUp at hok
    simp only [Guard.doDropDigit] at hok
    rw [show ((g.round .to_nearest == 1) || ((g.round .to_nearest == 0) && (m % 2 == 1))) = false
        from h_ru_false] at hok
    simp only [Bool.false_and, if_false, Bool.false_eq_true] at hok
    by_cases h_ovf : (Guard.bringIntoRange false m e largeRange.min).exponent_ > maxExponent
    · rw [if_pos h_ovf] at hok; exact absurd hok (by intro h; cases h)
    · rw [if_neg h_ovf] at hok; exact (Except.ok.inj hok).symm
  rw [hres_eq]
  -- Now prove about Guard.bringIntoRange
  unfold Guard.bringIntoRange
  simp only
  have hne' : (Guard.bringIntoRange false m e largeRange.min).mantissa_ ≠ 0 := by
    rw [← hres_eq]; exact hne
  by_cases hresc : m < largeRange.min
  · rw [if_pos hresc]; simp only []
    by_cases h_under : e - 1 < minExponent
    · exfalso; apply hne'
      unfold Guard.bringIntoRange; rw [if_pos hresc]; simp only []; rw [if_pos h_under]
    · rw [if_neg h_under]
      have hm_mul_10 : (m * 10).toNat = m.toNat * 10 :=
        m_mul_ten_no_overflow (UInt64.lt_iff_toNat_lt.mp hresc)
      change ((m * 10).toNat : ℚ) * 10 ^ (e - 1) = (m.toNat : ℚ) * 10 ^ e
      rw [hm_mul_10]
      push_cast
      rw [show (e - 1 : ℤ) = e + (-1) from by ring,
          zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0), zpow_neg_one]
      field_simp
  · rw [if_neg hresc]; simp only []
    by_cases h_under : e < minExponent
    · exfalso; apply hne'
      unfold Guard.bringIntoRange; rw [if_neg hresc]; simp only []; rw [if_pos h_under]
    · rw [if_neg h_under]

/-- Round-up without overflow: output value equals `(m+1) * 10^e`. -/
lemma doRoundUp_value_roundUp_noOverflow
    (g : Guard) (m : UInt64) (e : Int)
    (h_ru : g.round .to_nearest = 1 ∨
            (g.round .to_nearest = 0 ∧ m % 2 = 1))
    (h_no_ovf : m.toNat + 1 ≤ maxRep.toNat)
    (loc : String) (res : RoundResult)
    (hok : g.doRoundUp false m e largeRange.min largeRange.max .to_nearest loc = .ok res)
    (hne : res.mantissa_ ≠ 0) :
    (res.mantissa_.toNat : ℚ) * 10 ^ res.exponent_ = ((m.toNat : ℚ) + 1) * 10 ^ e := by
  have h_ru_true :
      ((g.round .to_nearest == 1) || ((g.round .to_nearest == 0) && (m % 2 == 1))) = true := by
    rcases h_ru with h1 | ⟨h0, hodd⟩
    · simp [h1]
    · simp [h0, hodd]
  have h_m_lt_maxRep : m.toNat < maxRep.toNat := by
    rw [maxRep_val] at h_no_ovf ⊢; omega
  have h_m_lt_maxRep_uint : ¬ (m ≥ maxRep) := by
    intro h; have : maxRep.toNat ≤ m.toNat := UInt64.le_iff_toNat_le.mp h; omega
  have h_m_lt_maxMant_uint : ¬ (m ≥ largeRange.max) := by
    intro h; have : largeRange.max.toNat ≤ m.toNat := UInt64.le_iff_toNat_le.mp h
    rw [largeRange_max_val] at this; rw [maxRep_val] at h_m_lt_maxRep; omega
  have h_cusp_cond_false : ((m ≥ largeRange.max) || (m ≥ maxRep)) = false := by
    rw [Bool.or_eq_false_iff]; exact ⟨decide_eq_false h_m_lt_maxMant_uint, decide_eq_false h_m_lt_maxRep_uint⟩
  -- Extract res = Guard.bringIntoRange false (m+1) e largeRange.min
  have hres_eq : res = Guard.bringIntoRange false (m + 1) e largeRange.min := by
    unfold Guard.doRoundUp at hok
    simp only [Guard.doDropDigit] at hok
    rw [show ((g.round .to_nearest == 1) || ((g.round .to_nearest == 0) && (m % 2 == 1))) = true
        from h_ru_true] at hok
    rw [show ((m ≥ largeRange.max) || (m ≥ maxRep)) = false from h_cusp_cond_false] at hok
    simp only [Bool.and_false, if_false, Bool.false_eq_true, if_true] at hok
    by_cases h_ovf : (Guard.bringIntoRange false (m + 1) e largeRange.min).exponent_ > maxExponent
    · rw [if_pos h_ovf] at hok; exact absurd hok (by intro h; cases h)
    · rw [if_neg h_ovf] at hok; exact (Except.ok.inj hok).symm
  rw [hres_eq]
  have h_m_le_maxRep : m.toNat ≤ maxRep.toNat := by omega
  have hm_add1_toNat : (m + 1).toNat = m.toNat + 1 := m_add_one_no_overflow h_m_le_maxRep
  have hne' : (Guard.bringIntoRange false (m + 1) e largeRange.min).mantissa_ ≠ 0 :=
    hres_eq ▸ hne
  unfold Guard.bringIntoRange
  simp only
  by_cases hresc : m + 1 < largeRange.min
  · rw [if_pos hresc]; simp only []
    have h_m1_lt_min : (m + 1).toNat < largeRange.min.toNat := UInt64.lt_iff_toNat_lt.mp hresc
    have h_m1_mul10_toNat : ((m + 1) * 10).toNat = (m + 1).toNat * 10 := by
      rw [UInt64.toNat_mul]
      have h10u : (10 : UInt64).toNat = 10 := rfl; rw [h10u]
      have : (m + 1).toNat * 10 < 2 ^ 64 := by
        rw [largeRange_min_val] at h_m1_lt_min
        calc (m + 1).toNat * 10 < 1000000000000000000 * 10 :=
              Nat.mul_lt_mul_of_pos_right h_m1_lt_min (by norm_num)
          _ < 2 ^ 64 := by norm_num
      exact Nat.mod_eq_of_lt this
    by_cases h_under : e - 1 < minExponent
    · exfalso; apply hne'
      unfold Guard.bringIntoRange; rw [if_pos hresc]; simp only []; rw [if_pos h_under]
    · rw [if_neg h_under]
      change ((((m + 1) * 10)).toNat : ℚ) * 10 ^ (e - 1) = ((m.toNat : ℚ) + 1) * 10 ^ e
      rw [h_m1_mul10_toNat, hm_add1_toNat]
      push_cast
      rw [show (e - 1 : ℤ) = e + (-1) from by ring,
          zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0), zpow_neg_one]
      field_simp
  · rw [if_neg hresc]; simp only []
    by_cases h_under : e < minExponent
    · exfalso; apply hne'
      unfold Guard.bringIntoRange; rw [if_neg hresc]; simp only []; rw [if_pos h_under]
    · rw [if_neg h_under]
      change ((m + 1).toNat : ℚ) * 10 ^ e = ((m.toNat : ℚ) + 1) * 10 ^ e
      rw [hm_add1_toNat]; push_cast; ring

/-- Cusp case (`m = maxRep`, round-up fires): output value is `maxRepCuspTarget * 10^e`.
The cusp branch pushes `maxRep % 10 = 7` into the guard, re-rounds (always +1 since
top nibble is 7 > 5), and rescales: `(maxRep/10 + 1) * 10 = maxRep + 3`. -/
lemma doRoundUp_value_cusp
    (g : Guard) (m : UInt64) (e : Int)
    (h_m_eq : m = maxRep)
    (h_ru : g.round .to_nearest = 1 ∨
            (g.round .to_nearest = 0 ∧ m % 2 = 1))
    (loc : String) (res : RoundResult)
    (hok : g.doRoundUp false m e largeRange.min largeRange.max .to_nearest loc = .ok res)
    (hne : res.mantissa_ ≠ 0) :
    (res.mantissa_.toNat : ℚ) * 10 ^ res.exponent_ = maxRepCuspTarget * 10 ^ e := by
  have h_ru_true :
      ((g.round .to_nearest == 1) || ((g.round .to_nearest == 0) && (m % 2 == 1))) = true := by
    rcases h_ru with h1 | ⟨h0, hodd⟩
    · simp [h1]
    · simp [h0, hodd]
  have h_m_ge_maxRep : decide (m ≥ maxRep) = true := by rw [h_m_eq]; decide
  have h_cusp_cond_true : ((m ≥ largeRange.max) || (m ≥ maxRep)) = true := by
    rw [Bool.or_eq_true]; right; exact h_m_ge_maxRep
  have h_d_eq : m % 10 = 7 := by rw [h_m_eq]; decide
  have h_m_div10_toNat : (m / 10).toNat = mantissaFloor := by
    rw [UInt64.toNat_div, h_m_eq]; decide
  set g' := g.push (m % 10) with hg'_def
  have h_g'_digits_toNat : g'.digits_.toNat = g.digits_.toNat / 16 + (m.toNat % 10 % 16) * 2 ^ 60 := by
    rw [hg'_def]; have := toNat_push_digits g (m % 10); rw [UInt64.toNat_mod] at this
    have h10 : (10 : UInt64).toNat = 10 := rfl; rw [h10] at this; exact this
  have h_m_mod10 : m.toNat % 10 = 7 := by rw [h_m_eq]; decide
  have h_m_mod10_mod16 : m.toNat % 10 % 16 = 7 := by rw [h_m_mod10]
  have h_g'_digits_ge : g'.digits_.toNat ≥ 7 * 2 ^ 60 := by
    rw [h_g'_digits_toNat, h_m_mod10_mod16]; omega
  have h_hex5_lt : (0x5000_0000_0000_0000 : UInt64).toNat < g'.digits_.toNat := by
    have h5 : (0x5000_0000_0000_0000 : UInt64).toNat = 5764607523034234880 := by decide
    rw [h5]
    have h7 : (7 : ℕ) * 2 ^ 60 = 8070450532247928832 := by norm_num
    omega
  have h_g'_gt : g'.digits_ > 0x5000_0000_0000_0000 := by
    change (0x5000_0000_0000_0000 : UInt64) < g'.digits_
    rw [UInt64.lt_iff_toNat_lt]; exact h_hex5_lt
  have h_g'_round : g'.round .to_nearest = 1 := by rw [round_to_nearest_def, if_pos h_g'_gt]
  have h_ru'_true :
      ((g'.round .to_nearest == 1) || ((g'.round .to_nearest == 0) && ((m / 10) % 2 == 1))) = true := by
    rw [h_g'_round]; rfl
  have h_m_div10_le_maxRep : (m / 10).toNat ≤ maxRep.toNat := by
    rw [h_m_div10_toNat, maxRep_val]; norm_num
  have h_m1_toNat : (m / 10 + 1).toNat = mantissaFloorSucc := by
    rw [m_add_one_no_overflow h_m_div10_le_maxRep, h_m_div10_toNat]
  have h_m1_lt_min : (m / 10 + 1) < largeRange.min := by
    rw [UInt64.lt_iff_toNat_lt, h_m1_toNat, largeRange_min_val]; norm_num
  have h_m1_mul10_toNat : ((m / 10 + 1) * 10).toNat = maxRepCuspTarget := by
    rw [UInt64.toNat_mul]; have h10 : (10 : UInt64).toNat = 10 := rfl; rw [h10, h_m1_toNat]
  -- Extract res from hok
  have hres_eq : res = Guard.bringIntoRange false (m / 10 + 1) (e + 1) largeRange.min := by
    unfold Guard.doRoundUp at hok
    simp only [Guard.doDropDigit] at hok
    rw [show ((g.round .to_nearest == 1) || ((g.round .to_nearest == 0) && (m % 2 == 1))) = true
        from h_ru_true] at hok
    rw [show ((m ≥ largeRange.max) || (m ≥ maxRep)) = true from h_cusp_cond_true] at hok
    simp only [Bool.true_and, if_true] at hok
    rw [show ((g'.round .to_nearest == 1) || ((g'.round .to_nearest == 0) && ((m / 10) % 2 == 1))) = true
        from h_ru'_true] at hok
    simp only [if_true] at hok
    by_cases h_ovf : (Guard.bringIntoRange false (m / 10 + 1) (e + 1) largeRange.min).exponent_ > maxExponent
    · rw [if_pos h_ovf] at hok; exact absurd hok (by intro h; cases h)
    · rw [if_neg h_ovf] at hok; exact (Except.ok.inj hok).symm
  rw [hres_eq]
  have hne' : (Guard.bringIntoRange false (m / 10 + 1) (e + 1) largeRange.min).mantissa_ ≠ 0 :=
    hres_eq ▸ hne
  simp only [Guard.bringIntoRange]
  rw [if_pos h_m1_lt_min]; simp only []
  by_cases h_under : e + 1 - 1 < minExponent
  · exfalso; apply hne'
    simp only [Guard.bringIntoRange]; rw [if_pos h_m1_lt_min]; simp only []; rw [if_pos h_under]
  · rw [if_neg h_under]
    change (((m / 10 + 1) * 10).toNat : ℚ) * 10 ^ (e + 1 - 1) = maxRepCuspTarget * 10 ^ e
    rw [h_m1_mul10_toNat]
    have h_exp : (e + 1 - 1 : ℤ) = e := by ring
    rw [h_exp]; push_cast; ring

/-- Output invariants: mantissa in `[minMant, maxMant]`, exponent >= minExp,
and mantissa > maxRep implies mantissa divisible by 10. -/
lemma doRoundUp_output_invariants
    (g : Guard) (neg : Bool) (m : UInt64) (e : Int)
    (h_lb : mantissaFloor ≤ m.toNat)
    (h_ub : m.toNat ≤ maxRep.toNat)
    (loc : String) (res : RoundResult)
    (hok : g.doRoundUp neg m e largeRange.min largeRange.max .to_nearest loc = .ok res)
    (hne : res.mantissa_ ≠ 0) :
    largeRange.min.toNat ≤ res.mantissa_.toNat ∧
    res.mantissa_.toNat ≤ largeRange.max.toNat ∧
    minExponent ≤ res.exponent_ ∧
    (res.mantissa_.toNat > maxRep.toNat → res.mantissa_.toNat % 10 = 0) := by
  have hmaxRep_v : maxRep.toNat = maxRepNat := maxRep_val
  have hminMant_v : largeRange.min.toNat = 1000000000000000000 := largeRange_min_val
  have hmaxMant_v : largeRange.max.toNat = 9999999999999999999 := largeRange_max_val
  have hm_add1_toNat : (m + 1).toNat = m.toNat + 1 := m_add_one_no_overflow h_ub
  have h_m_not_ge_maxMant : ¬ (m ≥ largeRange.max) := by
    intro h
    have : largeRange.max.toNat ≤ m.toNat := UInt64.le_iff_toNat_le.mp h
    rw [hmaxMant_v] at this; rw [hmaxRep_v] at h_ub; omega
  have h_ge_maxMant_false : decide (m ≥ largeRange.max) = false :=
    decide_eq_false h_m_not_ge_maxMant
  -- Unfold doRoundUp to extract the concrete value of res
  unfold Guard.doRoundUp Guard.bringIntoRange at hok
  simp only [Guard.doDropDigit] at hok
  by_cases h_ru : (g.round .to_nearest == 1 || (g.round .to_nearest == 0 && m % 2 == 1)) = true
  · rw [show (g.round .to_nearest == 1 || (g.round .to_nearest == 0 && m % 2 == 1)) = true from h_ru] at hok
    by_cases h_m_eq_maxRep : m = maxRep
    · have h_ge_maxRep : decide (m ≥ maxRep) = true := by rw [h_m_eq_maxRep]; decide
      have h_cusp_cond_true : ((m ≥ largeRange.max) || (m ≥ maxRep)) = true := by
        rw [Bool.or_eq_true]; right; exact h_ge_maxRep
      rw [show ((m ≥ largeRange.max) || (m ≥ maxRep)) = true from h_cusp_cond_true] at hok
      simp only [Bool.and_self, if_true] at hok
      have h_d_eq : m % 10 = 7 := by rw [h_m_eq_maxRep]; decide
      have h_m_div10_toNat : (m / 10).toNat = mantissaFloor := by
        rw [UInt64.toNat_div, h_m_eq_maxRep]; decide
      set g' := g.push (m % 10) with hg'_def
      have h_g'_digits_toNat : g'.digits_.toNat
          = g.digits_.toNat / 16 + (m.toNat % 10 % 16) * 2 ^ 60 := by
        rw [hg'_def]
        have := toNat_push_digits g (m % 10)
        rw [UInt64.toNat_mod] at this
        have h10 : (10 : UInt64).toNat = 10 := rfl
        rw [h10] at this; exact this
      have h_m_mod10 : m.toNat % 10 = 7 := by rw [h_m_eq_maxRep]; decide
      have h_m_mod10_mod16 : m.toNat % 10 % 16 = 7 := by rw [h_m_mod10]
      have h_g'_digits_ge : g'.digits_.toNat ≥ 7 * 2 ^ 60 := by
        rw [h_g'_digits_toNat, h_m_mod10_mod16]; omega
      have h_hex5_lt : (0x5000_0000_0000_0000 : UInt64).toNat < g'.digits_.toNat := by
        have h5 : (0x5000_0000_0000_0000 : UInt64).toNat = 5764607523034234880 := by decide
        have h7 : (7 : ℕ) * 2 ^ 60 = 8070450532247928832 := by norm_num
        rw [h5]; omega
      have h_g'_gt : g'.digits_ > 0x5000_0000_0000_0000 := by
        change (0x5000_0000_0000_0000 : UInt64) < g'.digits_
        rw [UInt64.lt_iff_toNat_lt]; exact h_hex5_lt
      have h_g'_round : g'.round .to_nearest = 1 := by
        rw [round_to_nearest_def]; rw [if_pos h_g'_gt]
      have h_ru'_true :
          (g'.round .to_nearest == 1 || (g'.round .to_nearest == 0 && (m / 10) % 2 == 1)) = true := by
        rw [h_g'_round]; rfl
      rw [show (g'.round .to_nearest == 1 || (g'.round .to_nearest == 0 && (m / 10) % 2 == 1)) = true
          from h_ru'_true] at hok
      simp only [if_true] at hok
      have h_m_div10_le_maxRep : (m / 10).toNat ≤ maxRep.toNat := by
        rw [h_m_div10_toNat, maxRep_val]; norm_num
      have h_m1_toNat : (m / 10 + 1).toNat = mantissaFloorSucc := by
        rw [m_add_one_no_overflow h_m_div10_le_maxRep, h_m_div10_toNat]
      have h_m1_lt_min : (m / 10 + 1) < largeRange.min := by
        rw [UInt64.lt_iff_toNat_lt, h_m1_toNat, largeRange_min_val]; norm_num
      rw [if_pos h_m1_lt_min] at hok; simp only [] at hok
      have h_m1_mul10_toNat : ((m / 10 + 1) * 10).toNat = maxRepCuspTarget := by
        rw [UInt64.toNat_mul]
        have h10 : (10 : UInt64).toNat = 10 := rfl; rw [h10, h_m1_toNat]
      by_cases h_under : e + 1 - 1 < minExponent
      · exfalso; apply hne
        rw [if_pos h_under] at hok
        (try (simp only [Except.ok.injEq] at hok))
        (try (obtain rfl := Except.ok.inj hok))
        (try subst hok)
        rfl
      · rw [if_neg h_under] at hok
        -- hok : (if (e+1-1) > maxExponent then .error else .ok { neg, (m/10+1)*10, e+1-1 }) = .ok res
        -- Since hok = .ok res, the overflow check must be false
        have h_no_ovf : ¬ (e + 1 - 1 > maxExponent) := by
          intro h_ovf
          rw [if_pos h_ovf] at hok
          exact absurd hok (by intro h; cases h)
        rw [if_neg h_no_ovf] at hok
        obtain rfl := Except.ok.inj hok
        refine ⟨?_, ?_, ?_, ?_⟩
        · change largeRange.min.toNat ≤ ((m / 10 + 1) * 10).toNat
          rw [h_m1_mul10_toNat, hminMant_v]; norm_num
        · change ((m / 10 + 1) * 10).toNat ≤ largeRange.max.toNat
          rw [h_m1_mul10_toNat, hmaxMant_v]; norm_num
        · change minExponent ≤ e + 1 - 1; push_neg at h_under; exact h_under
        · intro _; change ((m / 10 + 1) * 10).toNat % 10 = 0; rw [h_m1_mul10_toNat]
    · -- non-cusp
      have h_m_lt_maxRep : m.toNat < maxRep.toNat := by
        have : m.toNat ≠ maxRep.toNat := fun heq => h_m_eq_maxRep (UInt64.toNat_inj.mp heq)
        omega
      have h_m_not_ge_maxRep : ¬ (m ≥ maxRep) := by
        intro h; have : maxRep.toNat ≤ m.toNat := UInt64.le_iff_toNat_le.mp h; omega
      have h_cusp_cond_false : ((m ≥ largeRange.max) || (m ≥ maxRep)) = false := by
        rw [Bool.or_eq_false_iff]
        exact ⟨h_ge_maxMant_false, decide_eq_false h_m_not_ge_maxRep⟩
      rw [show ((m ≥ largeRange.max) || (m ≥ maxRep)) = false from h_cusp_cond_false] at hok
      simp only [Bool.and_false, if_false, Bool.false_eq_true, if_true] at hok
      have h_m1_le_maxRep : (m + 1).toNat ≤ maxRep.toNat := by rw [hm_add1_toNat]; omega
      by_cases h_resc : m + 1 < largeRange.min
      · rw [if_pos h_resc] at hok; simp only [] at hok
        have h_m1_lt_min : (m + 1).toNat < largeRange.min.toNat := UInt64.lt_iff_toNat_lt.mp h_resc
        have h_m1_mul10_toNat : ((m + 1) * 10).toNat = (m + 1).toNat * 10 := by
          rw [UInt64.toNat_mul]; have h10 : (10 : UInt64).toNat = 10 := rfl; rw [h10]
          apply Nat.mod_eq_of_lt; rw [hminMant_v] at h_m1_lt_min
          calc (m + 1).toNat * 10 < 1000000000000000000 * 10 := Nat.mul_lt_mul_of_pos_right h_m1_lt_min (by norm_num)
            _ < 2 ^ 64 := by norm_num
        by_cases h_under : e - 1 < minExponent
        · exfalso
          apply hne
          rw [if_pos h_under] at hok
          (try (simp only [Except.ok.injEq] at hok))
          (try (obtain rfl := Except.ok.inj hok))
          (try subst hok)
          rfl
        · rw [if_neg h_under] at hok
          have h_no_ovf : ¬ (e - 1 > maxExponent) := by
            intro h_ovf; rw [if_pos h_ovf] at hok
            exact absurd hok (by intro h; cases h)
          rw [if_neg h_no_ovf] at hok
          obtain rfl := Except.ok.inj hok
          refine ⟨?_, ?_, ?_, ?_⟩
          · change largeRange.min.toNat ≤ ((m + 1) * 10).toNat
            rw [h_m1_mul10_toNat, hm_add1_toNat, hminMant_v]
            have : (m.toNat + 1) * 10 ≥ maxRepCuspTarget := by omega
            omega
          · change ((m + 1) * 10).toNat ≤ largeRange.max.toNat
            rw [h_m1_mul10_toNat, hmaxMant_v, hminMant_v] at *
            calc (m + 1).toNat * 10 ≤ (1000000000000000000 - 1) * 10 := Nat.mul_le_mul_right _ (by omega)
              _ = maxMul10Witness := by norm_num
              _ ≤ 9999999999999999999 := by norm_num
          · change minExponent ≤ e - 1; push_neg at h_under; exact h_under
          · intro _; change ((m + 1) * 10).toNat % 10 = 0; rw [h_m1_mul10_toNat]; omega
      · rw [if_neg h_resc] at hok; simp only [] at hok
        have h_m1_ge_min : (m + 1).toNat ≥ largeRange.min.toNat := by
          by_contra h; push_neg at h; exact h_resc (UInt64.lt_iff_toNat_lt.mpr h)
        by_cases h_under : e < minExponent
        · exfalso
          apply hne
          rw [if_pos h_under] at hok
          (try (simp only [Except.ok.injEq] at hok))
          (try (obtain rfl := Except.ok.inj hok))
          (try subst hok)
          rfl
        · rw [if_neg h_under] at hok
          have h_no_ovf : ¬ (e > maxExponent) := by
            intro h_ovf; rw [if_pos h_ovf] at hok
            exact absurd hok (by intro h; cases h)
          rw [if_neg h_no_ovf] at hok
          obtain rfl := Except.ok.inj hok
          refine ⟨h_m1_ge_min, ?_, ?_, ?_⟩
          · change (m + 1).toNat ≤ largeRange.max.toNat; rw [hm_add1_toNat, hmaxMant_v]; rw [hmaxRep_v] at h_m_lt_maxRep; omega
          · change minExponent ≤ e; push_neg at h_under; exact h_under
          · change (m + 1).toNat > maxRep.toNat → (m + 1).toNat % 10 = 0
            intro h_gt; exfalso; have : (m + 1).toNat ≤ maxRep.toNat := h_m1_le_maxRep; omega
  · rw [Bool.not_eq_true] at h_ru
    rw [show (g.round .to_nearest == 1 || (g.round .to_nearest == 0 && m % 2 == 1)) = false from h_ru] at hok
    simp only [Bool.false_and, if_false, Bool.false_eq_true] at hok
    by_cases h_resc : m < largeRange.min
    · rw [if_pos h_resc] at hok; simp only [] at hok
      have h_m_lt_min : m.toNat < largeRange.min.toNat := UInt64.lt_iff_toNat_lt.mp h_resc
      have h_m_mul10_toNat : (m * 10).toNat = m.toNat * 10 := m_mul_ten_no_overflow h_m_lt_min
      by_cases h_under : e - 1 < minExponent
      · exfalso
        apply hne
        rw [if_pos h_under] at hok
        (try (simp only [Except.ok.injEq] at hok))
        (try (obtain rfl := Except.ok.inj hok))
        (try subst hok)
        rfl
      · rw [if_neg h_under] at hok
        (try (simp only [Except.ok.injEq] at hok))
        have h_no_ovf : ¬ (e - 1 > maxExponent) := by
          intro h_ovf; rw [if_pos h_ovf] at hok
          exact absurd hok (by intro h; cases h)
        rw [if_neg h_no_ovf] at hok
        obtain rfl := Except.ok.inj hok
        refine ⟨?_, ?_, ?_, ?_⟩
        · change largeRange.min.toNat ≤ (m * 10).toNat
          rw [h_m_mul10_toNat, hminMant_v]
          have : m.toNat * 10 ≥ 9223372036854775800 := by omega
          omega
        · change (m * 10).toNat ≤ largeRange.max.toNat
          rw [h_m_mul10_toNat, hmaxMant_v, hminMant_v] at *
          calc m.toNat * 10 ≤ (1000000000000000000 - 1) * 10 := Nat.mul_le_mul_right _ (by omega)
            _ = maxMul10Witness := by norm_num
            _ ≤ 9999999999999999999 := by norm_num
        · change minExponent ≤ e - 1; push_neg at h_under; exact h_under
        · intro _; change (m * 10).toNat % 10 = 0; rw [h_m_mul10_toNat]; omega
    · rw [if_neg h_resc] at hok; simp only [] at hok
      have h_m_ge_min : m.toNat ≥ largeRange.min.toNat := by
        by_contra h; push_neg at h; exact h_resc (UInt64.lt_iff_toNat_lt.mpr h)
      by_cases h_under : e < minExponent
      · exfalso
        apply hne
        rw [if_pos h_under] at hok
        (try (simp only [Except.ok.injEq] at hok))
        (try (obtain rfl := Except.ok.inj hok))
        (try subst hok)
        rfl
      · rw [if_neg h_under] at hok
        (try (simp only [Except.ok.injEq] at hok))
        have h_no_ovf : ¬ (e > maxExponent) := by
          intro h_ovf; rw [if_pos h_ovf] at hok
          exact absurd hok (by intro h; cases h)
        rw [if_neg h_no_ovf] at hok
        obtain rfl := Except.ok.inj hok
        refine ⟨h_m_ge_min, ?_, ?_, ?_⟩
        · change m.toNat ≤ largeRange.max.toNat; rw [hmaxMant_v]; rw [hmaxRep_v] at h_ub; omega
        · change minExponent ≤ e; push_neg at h_under; exact h_under
        · change m.toNat > maxRep.toNat → m.toNat % 10 = 0; intro h_gt; exfalso; omega

/-- Non-cusp error bound: when `m < maxRep`, error is at most `(1/2) * 10^e`. -/
theorem doRoundUp_error_bound_nonCusp
    (g : Guard) (m : UInt64) (e : Int) (f : ℚ)
    (hrep : represents g f)
    (h_lt_rep : m.toNat < maxRep.toNat)
    (loc : String) (res : RoundResult)
    (hok : g.doRoundUp false m e largeRange.min largeRange.max .to_nearest loc = .ok res)
    (hne : res.mantissa_ ≠ 0) :
    |(res.mantissa_.toNat : ℚ) * 10 ^ res.exponent_
      - ((m.toNat : ℚ) + f) * 10 ^ e|
      ≤ (1/2) * 10 ^ e := by
  have hf_nn := represents_nonneg hrep
  have hf_lt1 := represents_lt_one hrep
  obtain ⟨hround_pos, hround_neg, hround_zero⟩ := round_correct hrep
  have h10e_pos : (0 : ℚ) < 10 ^ e := zpow_pos (by norm_num) _
  have h10e_nn : (0 : ℚ) ≤ 10 ^ e := le_of_lt h10e_pos
  have h_round_values : g.round .to_nearest = 1 ∨ g.round .to_nearest = 0
      ∨ g.round .to_nearest = -1 := by
    unfold Guard.round
    by_cases h1 : g.digits_ > 0x5000_0000_0000_0000
    · left; rw [if_pos h1]
    · rw [if_neg h1]
      by_cases h2 : g.digits_ < 0x5000_0000_0000_0000
      · right; right; rw [if_pos h2]
      · rw [if_neg h2]
        by_cases h3 : g.xbit_ = true
        · left; rw [if_pos h3]
        · right; left; rw [if_neg h3]
  have h_no_ovf : m.toNat + 1 ≤ maxRep.toNat := by omega
  rcases h_round_values with h_up | h_tie | h_down
  · have hf_gt : f > 1 / 2 := hround_pos.mp h_up
    have h_ru_fires : g.round .to_nearest = 1 ∨ (g.round .to_nearest = 0 ∧ m % 2 = 1) :=
      Or.inl h_up
    have h_val := doRoundUp_value_roundUp_noOverflow g m e h_ru_fires h_no_ovf loc res hok hne
    rw [h_val]
    have : ((m.toNat : ℚ) + 1) * 10 ^ e - ((m.toNat : ℚ) + f) * 10 ^ e
        = (1 - f) * 10 ^ e := by ring
    rw [this, abs_mul, abs_of_nonneg h10e_nn]
    have h_abs : |(1 - f : ℚ)| ≤ 1/2 := by
      rw [abs_of_nonneg (by linarith : (0 : ℚ) ≤ 1 - f)]; linarith
    exact mul_le_mul_of_nonneg_right h_abs h10e_nn
  · have hf_eq : f = 1 / 2 := hround_zero.mp h_tie
    by_cases h_odd : m % 2 = 1
    · have h_ru_fires : g.round .to_nearest = 1 ∨ (g.round .to_nearest = 0 ∧ m % 2 = 1) :=
        Or.inr ⟨h_tie, h_odd⟩
      have h_val := doRoundUp_value_roundUp_noOverflow g m e h_ru_fires h_no_ovf loc res hok hne
      rw [h_val, hf_eq]
      have : ((m.toNat : ℚ) + 1) * 10 ^ e - ((m.toNat : ℚ) + 1 / 2) * 10 ^ e
          = (1 / 2) * 10 ^ e := by ring
      rw [this, abs_mul, abs_of_nonneg h10e_nn]
      have h_abs : |((1 / 2) : ℚ)| ≤ 1/2 := by norm_num
      exact mul_le_mul_of_nonneg_right h_abs h10e_nn
    · have h_no_ru : ¬ (g.round .to_nearest = 1 ∨
          (g.round .to_nearest = 0 ∧ m % 2 = 1)) := by
        push_neg
        exact ⟨fun heq => by rw [heq] at h_tie; exact absurd h_tie (by norm_num), fun _ => h_odd⟩
      have h_val := doRoundUp_value_noRoundUp g m e h_no_ru loc res hok hne
      rw [h_val, hf_eq]
      have : (m.toNat : ℚ) * 10 ^ e - ((m.toNat : ℚ) + 1 / 2) * 10 ^ e
          = -(1 / 2) * 10 ^ e := by ring
      rw [this, abs_mul, abs_of_nonneg h10e_nn]
      exact mul_le_mul_of_nonneg_right (by norm_num) h10e_nn
  · have hf_lt : f < 1 / 2 := hround_neg.mp h_down
    have h_no_ru : ¬ (g.round .to_nearest = 1 ∨
        (g.round .to_nearest = 0 ∧ m % 2 = 1)) := by
      push_neg
      exact ⟨fun heq => by rw [heq] at h_down; exact absurd h_down (by norm_num),
             fun heq => by rw [heq] at h_down; exact absurd h_down (by norm_num)⟩
    have h_val := doRoundUp_value_noRoundUp g m e h_no_ru loc res hok hne
    rw [h_val]
    have : (m.toNat : ℚ) * 10 ^ e - ((m.toNat : ℚ) + f) * 10 ^ e = -f * 10 ^ e := by ring
    rw [this, abs_mul, abs_of_nonneg h10e_nn]
    have h_abs : |(-f : ℚ)| ≤ 1/2 := by
      rw [abs_neg, abs_of_nonneg hf_nn]; linarith
    exact mul_le_mul_of_nonneg_right h_abs h10e_nn

/-- Main error bound: `doRoundUp` output differs from `(m + f) * 10^e` by at most `8 * 10^e`. -/
theorem doRoundUp_error_bound
    (g : Guard) (m : UInt64) (e : Int) (f : ℚ)
    (hrep : represents g f)
    (h_le_rep : m.toNat ≤ maxRep.toNat)
    (loc : String) (res : RoundResult)
    (hok : g.doRoundUp false m e largeRange.min largeRange.max .to_nearest loc = .ok res)
    (hne : res.mantissa_ ≠ 0) :
    |(res.mantissa_.toNat : ℚ) * 10 ^ res.exponent_
      - ((m.toNat : ℚ) + f) * 10 ^ e|
      ≤ 8 * 10 ^ e := by
  have hf_nn := represents_nonneg hrep
  have hf_lt1 := represents_lt_one hrep
  obtain ⟨hround_pos, hround_neg, hround_zero⟩ := round_correct hrep
  have h10e_pos : (0 : ℚ) < 10 ^ e := zpow_pos (by norm_num) _
  have h10e_nn : (0 : ℚ) ≤ 10 ^ e := le_of_lt h10e_pos
  have h_round_values : g.round .to_nearest = 1 ∨ g.round .to_nearest = 0
      ∨ g.round .to_nearest = -1 := by
    unfold Guard.round
    by_cases h1 : g.digits_ > 0x5000_0000_0000_0000
    · left; rw [if_pos h1]
    · rw [if_neg h1]
      by_cases h2 : g.digits_ < 0x5000_0000_0000_0000
      · right; right; rw [if_pos h2]
      · rw [if_neg h2]
        by_cases h3 : g.xbit_ = true
        · left; rw [if_pos h3]
        · right; left; rw [if_neg h3]
  rcases h_round_values with h_up | h_tie | h_down
  · have hf_gt : f > 1 / 2 := hround_pos.mp h_up
    have h_ru_fires : g.round .to_nearest = 1 ∨ (g.round .to_nearest = 0 ∧ m % 2 = 1) :=
      Or.inl h_up
    by_cases h_m_max : m = maxRep
    · have h_val := doRoundUp_value_cusp g m e h_m_max h_ru_fires loc res hok hne
      rw [h_val]
      have h_m_nat : (m.toNat : ℚ) = maxRepNat := by rw [h_m_max, maxRep_val]; norm_num
      rw [h_m_nat]
      have : (maxRepCuspTarget : ℚ) * 10 ^ e - (maxRepNat + f) * 10 ^ e
          = (3 - f) * 10 ^ e := by ring
      rw [this, abs_mul, abs_of_nonneg h10e_nn]
      exact mul_le_mul_of_nonneg_right
        (by rw [abs_of_nonneg (by linarith : (0 : ℚ) ≤ 3 - f)]; linarith) h10e_nn
    · have h_no_ovf : m.toNat + 1 ≤ maxRep.toNat := by
        have : m.toNat ≠ maxRep.toNat := fun heq => h_m_max (UInt64.toNat_inj.mp heq); omega
      have h_val := doRoundUp_value_roundUp_noOverflow g m e h_ru_fires h_no_ovf loc res hok hne
      rw [h_val]
      have : ((m.toNat : ℚ) + 1) * 10 ^ e - ((m.toNat : ℚ) + f) * 10 ^ e
          = (1 - f) * 10 ^ e := by ring
      rw [this, abs_mul, abs_of_nonneg h10e_nn]
      exact mul_le_mul_of_nonneg_right
        (by rw [abs_of_nonneg (by linarith : (0 : ℚ) ≤ 1 - f)]; linarith) h10e_nn
  · have hf_eq : f = 1 / 2 := hround_zero.mp h_tie
    by_cases h_odd : m % 2 = 1
    · have h_ru_fires : g.round .to_nearest = 1 ∨ (g.round .to_nearest = 0 ∧ m % 2 = 1) :=
        Or.inr ⟨h_tie, h_odd⟩
      by_cases h_m_max : m = maxRep
      · have h_val := doRoundUp_value_cusp g m e h_m_max h_ru_fires loc res hok hne
        rw [h_val]
        have h_m_nat : (m.toNat : ℚ) = maxRepNat := by rw [h_m_max, maxRep_val]; norm_num
        rw [h_m_nat, hf_eq]
        have : (maxRepCuspTarget : ℚ) * 10 ^ e - (maxRepNat + 1 / 2) * 10 ^ e
            = (5 / 2) * 10 ^ e := by ring
        rw [this, abs_mul, abs_of_nonneg h10e_nn]
        exact mul_le_mul_of_nonneg_right (by norm_num) h10e_nn
      · have h_no_ovf : m.toNat + 1 ≤ maxRep.toNat := by
          have : m.toNat ≠ maxRep.toNat := fun heq => h_m_max (UInt64.toNat_inj.mp heq); omega
        have h_val := doRoundUp_value_roundUp_noOverflow g m e h_ru_fires h_no_ovf loc res hok hne
        rw [h_val, hf_eq]
        have : ((m.toNat : ℚ) + 1) * 10 ^ e - ((m.toNat : ℚ) + 1 / 2) * 10 ^ e
            = (1 / 2) * 10 ^ e := by ring
        rw [this, abs_mul, abs_of_nonneg h10e_nn]
        exact mul_le_mul_of_nonneg_right (by norm_num) h10e_nn
    · have h_no_ru : ¬ (g.round .to_nearest = 1 ∨
          (g.round .to_nearest = 0 ∧ m % 2 = 1)) := by
        push_neg
        exact ⟨fun heq => by rw [heq] at h_tie; exact absurd h_tie (by norm_num), fun _ => h_odd⟩
      have h_val := doRoundUp_value_noRoundUp g m e h_no_ru loc res hok hne
      rw [h_val, hf_eq]
      have : (m.toNat : ℚ) * 10 ^ e - ((m.toNat : ℚ) + 1 / 2) * 10 ^ e
          = -(1 / 2) * 10 ^ e := by ring
      rw [this, abs_mul, abs_of_nonneg h10e_nn]
      exact mul_le_mul_of_nonneg_right (by norm_num) h10e_nn
  · have hf_lt : f < 1 / 2 := hround_neg.mp h_down
    have h_no_ru : ¬ (g.round .to_nearest = 1 ∨
        (g.round .to_nearest = 0 ∧ m % 2 = 1)) := by
      push_neg
      exact ⟨fun heq => by rw [heq] at h_down; exact absurd h_down (by norm_num),
             fun heq => by rw [heq] at h_down; exact absurd h_down (by norm_num)⟩
    have h_val := doRoundUp_value_noRoundUp g m e h_no_ru loc res hok hne
    rw [h_val]
    have : (m.toNat : ℚ) * 10 ^ e - ((m.toNat : ℚ) + f) * 10 ^ e = -f * 10 ^ e := by ring
    rw [this, abs_mul, abs_of_nonneg h10e_nn]
    exact mul_le_mul_of_nonneg_right
      (by rw [abs_neg, abs_of_nonneg hf_nn]; linarith) h10e_nn

/-- Relative error bound: `doRoundUp` output differs from `(zm + f) * 10^ze`
by at most `(zm + f) * 10^ze * (1 / 2^60)` when `zm >= (maxRep+1)/10`. -/
lemma doRoundUp_rounds_to_nearest (g : Guard) (zm : UInt64) (ze : Int) (f : ℚ)
    (hf_rep : represents g f)
    (h_zm_ge : (mantissaFloor : ℕ) ≤ zm.toNat)
    (h_zm_le_max : zm.toNat ≤ maxRep.toNat)
    (loc : String) (res_pos : RoundResult)
    (hok_pos : g.doRoundUp false zm ze largeRange.min largeRange.max .to_nearest loc = .ok res_pos)
    (hres_pos_mant_ne : res_pos.mantissa_ ≠ 0) :
    |(res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_ -
       ((zm.toNat : ℚ) + f) * 10 ^ ze|
      ≤ ((zm.toNat : ℚ) + f) * 10 ^ ze * (1 / 2 ^ 60) := by
  have h_zm_cases : zm.toNat = maxRep.toNat ∨ zm.toNat < maxRep.toNat := by omega
  have hzm_q_ge : (mantissaFloor : ℚ) ≤ (zm.toNat : ℚ) := by exact_mod_cast h_zm_ge
  have h10ze'_pos : (0 : ℚ) < 10 ^ ze := zpow_pos (by norm_num) _
  have h10ze'_nn : (0 : ℚ) ≤ 10 ^ ze := le_of_lt h10ze'_pos
  rcases h_zm_cases with h_cusp | h_nc
  · -- cusp: zm = maxRep
    have hzm_q_eq_maxRep : (zm.toNat : ℚ) = maxRepNat := by
      rw [h_cusp, maxRep_val]; norm_num
    have h_zm_eq_maxRep : zm = maxRep := by
      apply UInt64.toNat_inj.mp; exact h_cusp
    have hf_nn := represents_nonneg hf_rep
    have hf_lt1 := represents_lt_one hf_rep
    obtain ⟨hround_pos, hround_neg, hround_zero⟩ := round_correct hf_rep
    have h_round_values : g.round .to_nearest = 1 ∨ g.round .to_nearest = 0
        ∨ g.round .to_nearest = -1 := by
      unfold Guard.round
      by_cases h1 : g.digits_ > 0x5000_0000_0000_0000
      · left; rw [if_pos h1]
      · rw [if_neg h1]
        by_cases h2 : g.digits_ < 0x5000_0000_0000_0000
        · right; right; rw [if_pos h2]
        · rw [if_neg h2]
          by_cases h3 : g.xbit_ = true
          · left; rw [if_pos h3]
          · right; left; rw [if_neg h3]
    have h_zm_odd : zm % 2 = 1 := by
      rw [h_zm_eq_maxRep]; decide
    rcases h_round_values with h_up | h_tie | h_down
    · have h_ru : g.round .to_nearest = 1 ∨
          (g.round .to_nearest = 0 ∧ zm % 2 = 1) := Or.inl h_up
      have h_cusp_val := doRoundUp_value_cusp g zm ze h_zm_eq_maxRep h_ru loc res_pos hok_pos hres_pos_mant_ne
      simp only at h_cusp_val
      rw [h_cusp_val, hzm_q_eq_maxRep]
      have h_diff : (maxRepCuspTarget : ℚ) * 10 ^ ze - (maxRepNat + f) * 10 ^ ze
          = (3 - f) * 10 ^ ze := by ring
      rw [h_diff, abs_mul, abs_of_nonneg h10ze'_nn]
      have h_abs : |(3 - f : ℚ)| = 3 - f := by
        rw [abs_of_nonneg (by linarith : (0 : ℚ) ≤ 3 - f)]
      rw [h_abs]
      have h_final : (3 - f) ≤ (maxRepNat + f) * (1 / 2 ^ 60) := by
        rw [show (maxRepNat + f) * (1 / 2^60) = (maxRepNat + f) / 2^60 by ring]
        rw [le_div_iff₀ (by norm_num : (0 : ℚ) < 2^60)]
        have h2_60 : (2 : ℚ)^60 = 1152921504606846976 := by norm_num
        rw [h2_60]; linarith
      calc (3 - f) * 10 ^ ze
          ≤ (maxRepNat + f) * (1 / 2 ^ 60) * 10 ^ ze :=
            mul_le_mul_of_nonneg_right h_final h10ze'_nn
        _ = (maxRepNat + f) * 10 ^ ze * (1 / 2 ^ 60) := by ring
    · have h_ru : g.round .to_nearest = 1 ∨
          (g.round .to_nearest = 0 ∧ zm % 2 = 1) := Or.inr ⟨h_tie, h_zm_odd⟩
      have h_cusp_val := doRoundUp_value_cusp g zm ze h_zm_eq_maxRep h_ru loc res_pos hok_pos hres_pos_mant_ne
      simp only at h_cusp_val
      rw [h_cusp_val, hzm_q_eq_maxRep]
      have h_diff : (maxRepCuspTarget : ℚ) * 10 ^ ze - (maxRepNat + f) * 10 ^ ze
          = (3 - f) * 10 ^ ze := by ring
      rw [h_diff, abs_mul, abs_of_nonneg h10ze'_nn]
      have h_abs : |(3 - f : ℚ)| = 3 - f := by
        rw [abs_of_nonneg (by linarith : (0 : ℚ) ≤ 3 - f)]
      rw [h_abs]
      have h_final : (3 - f) ≤ (maxRepNat + f) * (1 / 2 ^ 60) := by
        rw [show (maxRepNat + f) * (1 / 2^60) = (maxRepNat + f) / 2^60 by ring]
        rw [le_div_iff₀ (by norm_num : (0 : ℚ) < 2^60)]
        have h2_60 : (2 : ℚ)^60 = 1152921504606846976 := by norm_num
        rw [h2_60]; linarith
      calc (3 - f) * 10 ^ ze
          ≤ (maxRepNat + f) * (1 / 2 ^ 60) * 10 ^ ze :=
            mul_le_mul_of_nonneg_right h_final h10ze'_nn
        _ = (maxRepNat + f) * 10 ^ ze * (1 / 2 ^ 60) := by ring
    · have h_no_ru : ¬ (g.round .to_nearest = 1 ∨
          (g.round .to_nearest = 0 ∧ zm % 2 = 1)) := by
        intro h
        rcases h with h1 | ⟨h0, _⟩
        · rw [h1] at h_down; exact absurd h_down (by norm_num)
        · rw [h0] at h_down; exact absurd h_down (by norm_num)
      have h_nr_val := doRoundUp_value_noRoundUp g zm ze h_no_ru loc res_pos hok_pos hres_pos_mant_ne
      simp only at h_nr_val
      rw [h_nr_val, hzm_q_eq_maxRep]
      have hf_lt_half : f < 1/2 := hround_neg.mp h_down
      have h_diff : (maxRepNat : ℚ) * 10 ^ ze - (maxRepNat + f) * 10 ^ ze
          = -f * 10 ^ ze := by ring
      rw [h_diff, abs_mul, abs_of_nonneg h10ze'_nn]
      have h_abs : |(-f : ℚ)| = f := by
        rw [abs_neg, abs_of_nonneg hf_nn]
      rw [h_abs]
      have h_final : f ≤ (maxRepNat + f) * (1 / 2 ^ 60) := by
        rw [show (maxRepNat + f) * (1 / 2^60) = (maxRepNat + f) / 2^60 by ring]
        rw [le_div_iff₀ (by norm_num : (0 : ℚ) < 2^60)]
        have h2_60 : (2 : ℚ)^60 = 1152921504606846976 := by norm_num
        rw [h2_60]; linarith
      calc f * 10 ^ ze
          ≤ (maxRepNat + f) * (1 / 2 ^ 60) * 10 ^ ze :=
            mul_le_mul_of_nonneg_right h_final h10ze'_nn
        _ = (maxRepNat + f) * 10 ^ ze * (1 / 2 ^ 60) := by ring
  · -- non-cusp: zm < maxRep
    have h_err_bound := doRoundUp_error_bound_nonCusp g zm ze f hf_rep h_nc loc res_pos hok_pos hres_pos_mant_ne
    calc |(res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_ - ((zm.toNat : ℚ) + f) * 10 ^ ze|
        ≤ (1/2) * 10 ^ ze := h_err_bound
      _ ≤ ((zm.toNat : ℚ) + f) * 10 ^ ze * (1 / 2 ^ 60) := by
          have h_coef : (1/2 : ℚ) ≤ ((zm.toNat : ℚ) + f) * (1 / 2 ^ 60) := by
            rw [show ((zm.toNat : ℚ) + f) * (1 / 2^60) = ((zm.toNat : ℚ) + f) / 2^60 by ring]
            rw [le_div_iff₀ (by norm_num : (0 : ℚ) < 2^60)]
            have hf_nn := represents_nonneg hf_rep
            calc (1/2 : ℚ) * 2 ^ 60
                = 576460752303423488 := by norm_num
              _ ≤ mantissaFloor := by norm_num
              _ ≤ (zm.toNat : ℚ) := hzm_q_ge
              _ ≤ (zm.toNat : ℚ) + f := by linarith
          calc (1/2 : ℚ) * 10 ^ ze
              ≤ ((zm.toNat : ℚ) + f) * (1 / 2 ^ 60) * 10 ^ ze :=
                mul_le_mul_of_nonneg_right h_coef h10ze'_nn
            _ = ((zm.toNat : ℚ) + f) * 10 ^ ze * (1 / 2 ^ 60) := by ring

/-- Tight relative error bound with ratio `5 / (2^63 - 3)`. -/
lemma doRoundUp_rounds_to_nearest_tight (g : Guard) (zm : UInt64) (ze : Int) (f : ℚ)
    (hf_rep : represents g f)
    (h_zm_ge : (mantissaFloor : ℕ) ≤ zm.toNat)
    (h_zm_le_max : zm.toNat ≤ maxRep.toNat)
    (loc : String) (res_pos : RoundResult)
    (hok_pos : g.doRoundUp false zm ze largeRange.min largeRange.max .to_nearest loc = .ok res_pos)
    (hres_pos_mant_ne : res_pos.mantissa_ ≠ 0) :
    |(res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_ -
       ((zm.toNat : ℚ) + f) * 10 ^ ze|
      ≤ ((zm.toNat : ℚ) + f) * 10 ^ ze * (5 / (2 ^ 63 - 3 : ℕ)) := by
  have hzm_q_ge : (mantissaFloor : ℚ) ≤ (zm.toNat : ℚ) := by exact_mod_cast h_zm_ge
  have h10ze'_pos : (0 : ℚ) < 10 ^ ze := zpow_pos (by norm_num) _
  have h10ze'_nn : (0 : ℚ) ≤ 10 ^ ze := le_of_lt h10ze'_pos
  have hf_nn := represents_nonneg hf_rep
  have hf_lt1 := represents_lt_one hf_rep
  obtain ⟨hround_pos, hround_neg, hround_zero⟩ := round_correct hf_rep
  have h_denom_val : (((2 ^ 63 - 3 : ℕ) : ℚ)) = 9223372036854775805 := by
    push_cast; norm_num
  have h_denom_pos : (0 : ℚ) < ((2 ^ 63 - 3 : ℕ) : ℚ) := by
    rw [h_denom_val]; norm_num
  have h_round_values : g.round .to_nearest = 1 ∨ g.round .to_nearest = 0
      ∨ g.round .to_nearest = -1 := by
    unfold Guard.round
    by_cases h1 : g.digits_ > 0x5000_0000_0000_0000
    · left; rw [if_pos h1]
    · rw [if_neg h1]
      by_cases h2 : g.digits_ < 0x5000_0000_0000_0000
      · right; right; rw [if_pos h2]
      · rw [if_neg h2]
        by_cases h3 : g.xbit_ = true
        · left; rw [if_pos h3]
        · right; left; rw [if_neg h3]
  have h_zm_cases : zm.toNat = maxRep.toNat ∨ zm.toNat < maxRep.toNat := by omega
  rcases h_zm_cases with h_cusp | h_nc
  · -- cusp
    have hzm_q_eq_maxRep : (zm.toNat : ℚ) = maxRepNat := by
      rw [h_cusp, maxRep_val]; norm_num
    have h_zm_eq_maxRep : zm = maxRep := UInt64.toNat_inj.mp h_cusp
    have h_zm_odd : zm % 2 = 1 := by rw [h_zm_eq_maxRep]; decide
    rcases h_round_values with h_up | h_tie | h_down
    · have h_ru : g.round .to_nearest = 1 ∨ (g.round .to_nearest = 0 ∧ zm % 2 = 1) := Or.inl h_up
      have hf_gt_half : f > 1/2 := hround_pos.mp h_up
      have h_cusp_val := doRoundUp_value_cusp g zm ze h_zm_eq_maxRep h_ru loc res_pos hok_pos hres_pos_mant_ne
      simp only at h_cusp_val
      rw [h_cusp_val, hzm_q_eq_maxRep]
      have h_diff : (maxRepCuspTarget : ℚ) * 10 ^ ze - (maxRepNat + f) * 10 ^ ze
          = (3 - f) * 10 ^ ze := by ring
      rw [h_diff, abs_mul, abs_of_nonneg h10ze'_nn]
      have h_abs : |(3 - f : ℚ)| = 3 - f := by
        rw [abs_of_nonneg (by linarith : (0 : ℚ) ≤ 3 - f)]
      rw [h_abs]
      have h_final : (3 - f) ≤ (maxRepNat + f) * (5 / ((2 ^ 63 - 3 : ℕ) : ℚ)) := by
        rw [h_denom_val]
        rw [show (maxRepNat + f) * (5 / (9223372036854775805 : ℚ))
              = 5 * (maxRepNat + f) / 9223372036854775805 by ring]
        rw [le_div_iff₀ (by norm_num : (0 : ℚ) < 9223372036854775805)]
        nlinarith [hf_gt_half, hf_lt1]
      calc (3 - f) * 10 ^ ze
          ≤ (maxRepNat + f) * (5 / ((2 ^ 63 - 3 : ℕ) : ℚ)) * 10 ^ ze :=
            mul_le_mul_of_nonneg_right h_final h10ze'_nn
        _ = (maxRepNat + f) * 10 ^ ze * (5 / ((2 ^ 63 - 3 : ℕ) : ℚ)) := by ring
    · have h_ru : g.round .to_nearest = 1 ∨ (g.round .to_nearest = 0 ∧ zm % 2 = 1) :=
        Or.inr ⟨h_tie, h_zm_odd⟩
      have hf_eq : f = 1/2 := hround_zero.mp h_tie
      have h_cusp_val := doRoundUp_value_cusp g zm ze h_zm_eq_maxRep h_ru loc res_pos hok_pos hres_pos_mant_ne
      simp only at h_cusp_val
      rw [h_cusp_val, hzm_q_eq_maxRep, hf_eq]
      have h_diff : (maxRepCuspTarget : ℚ) * 10 ^ ze - (maxRepNat + 1/2) * 10 ^ ze
          = (5/2) * 10 ^ ze := by ring
      rw [h_diff, abs_mul, abs_of_nonneg h10ze'_nn]
      have h_abs : |((5/2) : ℚ)| = 5/2 := by norm_num
      rw [h_abs]
      have h_final : (5/2 : ℚ) ≤ (maxRepNat + 1/2) * (5 / ((2 ^ 63 - 3 : ℕ) : ℚ)) := by
        rw [h_denom_val]
        rw [show (maxRepNat + 1/2 : ℚ) * (5 / 9223372036854775805)
              = 5 * (maxRepNat + 1/2) / 9223372036854775805 by ring]
        rw [le_div_iff₀ (by norm_num : (0 : ℚ) < 9223372036854775805)]
        norm_num
      calc (5/2 : ℚ) * 10 ^ ze
          ≤ (maxRepNat + 1/2) * (5 / ((2 ^ 63 - 3 : ℕ) : ℚ)) * 10 ^ ze :=
            mul_le_mul_of_nonneg_right h_final h10ze'_nn
        _ = (maxRepNat + 1/2) * 10 ^ ze * (5 / ((2 ^ 63 - 3 : ℕ) : ℚ)) := by ring
    · have h_no_ru : ¬ (g.round .to_nearest = 1 ∨
          (g.round .to_nearest = 0 ∧ zm % 2 = 1)) := by
        intro h
        rcases h with h1 | ⟨h0, _⟩
        · rw [h1] at h_down; exact absurd h_down (by norm_num)
        · rw [h0] at h_down; exact absurd h_down (by norm_num)
      have h_nr_val := doRoundUp_value_noRoundUp g zm ze h_no_ru loc res_pos hok_pos hres_pos_mant_ne
      simp only at h_nr_val
      rw [h_nr_val, hzm_q_eq_maxRep]
      have hf_lt_half : f < 1/2 := hround_neg.mp h_down
      have h_diff : (maxRepNat : ℚ) * 10 ^ ze - (maxRepNat + f) * 10 ^ ze
          = -f * 10 ^ ze := by ring
      rw [h_diff, abs_mul, abs_of_nonneg h10ze'_nn]
      have h_abs : |(-f : ℚ)| = f := by rw [abs_neg, abs_of_nonneg hf_nn]
      rw [h_abs]
      have h_final : f ≤ (maxRepNat + f) * (5 / ((2 ^ 63 - 3 : ℕ) : ℚ)) := by
        rw [h_denom_val]
        rw [show (maxRepNat + f) * (5 / (9223372036854775805 : ℚ))
              = 5 * (maxRepNat + f) / 9223372036854775805 by ring]
        rw [le_div_iff₀ (by norm_num : (0 : ℚ) < 9223372036854775805)]
        nlinarith [hf_nn, hf_lt_half]
      calc f * 10 ^ ze
          ≤ (maxRepNat + f) * (5 / ((2 ^ 63 - 3 : ℕ) : ℚ)) * 10 ^ ze :=
            mul_le_mul_of_nonneg_right h_final h10ze'_nn
        _ = (maxRepNat + f) * 10 ^ ze * (5 / ((2 ^ 63 - 3 : ℕ) : ℚ)) := by ring
  · -- non-cusp
    have h_no_ovf : zm.toNat + 1 ≤ maxRep.toNat := by omega
    rcases h_round_values with h_up | h_tie | h_down
    · -- Round = 1: roundUp fires, m1 = zm + 1. Error = (1-f)·10^ze.
      have hf_gt : f > 1/2 := hround_pos.mp h_up
      have h_ru : g.round .to_nearest = 1 ∨ (g.round .to_nearest = 0 ∧ zm % 2 = 1) := Or.inl h_up
      have h_val := doRoundUp_value_roundUp_noOverflow g zm ze h_ru h_no_ovf loc res_pos hok_pos hres_pos_mant_ne
      simp only at h_val
      rw [h_val]
      have h_diff : ((zm.toNat : ℚ) + 1) * 10 ^ ze - ((zm.toNat : ℚ) + f) * 10 ^ ze
          = (1 - f) * 10 ^ ze := by ring
      rw [h_diff, abs_mul, abs_of_nonneg h10ze'_nn]
      have h_abs : |(1 - f : ℚ)| = 1 - f := by
        rw [abs_of_nonneg (by linarith : (0 : ℚ) ≤ 1 - f)]
      rw [h_abs]
      have h_final : (1 - f) ≤ ((zm.toNat : ℚ) + f) * (5 / ((2 ^ 63 - 3 : ℕ) : ℚ)) := by
        rw [h_denom_val]
        rw [show ((zm.toNat : ℚ) + f) * (5 / (9223372036854775805 : ℚ))
              = 5 * ((zm.toNat : ℚ) + f) / 9223372036854775805 by ring]
        rw [le_div_iff₀ (by norm_num : (0 : ℚ) < 9223372036854775805)]
        nlinarith [hf_gt, hf_lt1, hzm_q_ge]
      calc (1 - f) * 10 ^ ze
          ≤ ((zm.toNat : ℚ) + f) * (5 / ((2 ^ 63 - 3 : ℕ) : ℚ)) * 10 ^ ze :=
            mul_le_mul_of_nonneg_right h_final h10ze'_nn
        _ = ((zm.toNat : ℚ) + f) * 10 ^ ze * (5 / ((2 ^ 63 - 3 : ℕ) : ℚ)) := by ring
    · have hf_eq : f = 1/2 := hround_zero.mp h_tie
      by_cases h_odd : zm % 2 = 1
      · have h_ru : g.round .to_nearest = 1 ∨ (g.round .to_nearest = 0 ∧ zm % 2 = 1) :=
          Or.inr ⟨h_tie, h_odd⟩
        have h_val := doRoundUp_value_roundUp_noOverflow g zm ze h_ru h_no_ovf loc res_pos hok_pos hres_pos_mant_ne
        rw [h_val, hf_eq]
        have h_diff : ((zm.toNat : ℚ) + 1) * 10 ^ ze - ((zm.toNat : ℚ) + 1/2) * 10 ^ ze
            = (1/2) * 10 ^ ze := by ring
        rw [h_diff, abs_mul, abs_of_nonneg h10ze'_nn]
        have h_abs : |((1/2) : ℚ)| = 1/2 := by norm_num
        rw [h_abs]
        have h_final : (1/2 : ℚ) ≤ ((zm.toNat : ℚ) + 1/2) * (5 / ((2 ^ 63 - 3 : ℕ) : ℚ)) := by
          rw [h_denom_val]
          rw [show ((zm.toNat : ℚ) + 1/2) * (5 / (9223372036854775805 : ℚ))
                = 5 * ((zm.toNat : ℚ) + 1/2) / 9223372036854775805 by ring]
          rw [le_div_iff₀ (by norm_num : (0 : ℚ) < 9223372036854775805)]
          linarith [hzm_q_ge]
        calc (1/2 : ℚ) * 10 ^ ze
            ≤ ((zm.toNat : ℚ) + 1/2) * (5 / ((2 ^ 63 - 3 : ℕ) : ℚ)) * 10 ^ ze :=
              mul_le_mul_of_nonneg_right h_final h10ze'_nn
          _ = ((zm.toNat : ℚ) + 1/2) * 10 ^ ze * (5 / ((2 ^ 63 - 3 : ℕ) : ℚ)) := by ring
      · -- m even → no roundUp. m1 = zm. Error = (-1/2)·10^ze, |.| = 1/2.
        have h_no_ru : ¬ (g.round .to_nearest = 1 ∨
            (g.round .to_nearest = 0 ∧ zm % 2 = 1)) := by
          push_neg
          refine ⟨?_, ?_⟩
          · intro heq; rw [heq] at h_tie; exact absurd h_tie (by norm_num)
          · intro _; exact h_odd
        have h_val := doRoundUp_value_noRoundUp g zm ze h_no_ru loc res_pos hok_pos hres_pos_mant_ne
        rw [h_val, hf_eq]
        have h_diff : (zm.toNat : ℚ) * 10 ^ ze - ((zm.toNat : ℚ) + 1/2) * 10 ^ ze
            = -(1/2) * 10 ^ ze := by ring
        rw [h_diff, abs_mul, abs_of_nonneg h10ze'_nn]
        have h_abs : |(-(1/2) : ℚ)| = 1/2 := by norm_num
        rw [h_abs]
        have h_final : (1/2 : ℚ) ≤ ((zm.toNat : ℚ) + 1/2) * (5 / ((2 ^ 63 - 3 : ℕ) : ℚ)) := by
          rw [h_denom_val]
          rw [show ((zm.toNat : ℚ) + 1/2) * (5 / (9223372036854775805 : ℚ))
                = 5 * ((zm.toNat : ℚ) + 1/2) / 9223372036854775805 by ring]
          rw [le_div_iff₀ (by norm_num : (0 : ℚ) < 9223372036854775805)]
          linarith [hzm_q_ge]
        calc (1/2 : ℚ) * 10 ^ ze
            ≤ ((zm.toNat : ℚ) + 1/2) * (5 / ((2 ^ 63 - 3 : ℕ) : ℚ)) * 10 ^ ze :=
              mul_le_mul_of_nonneg_right h_final h10ze'_nn
          _ = ((zm.toNat : ℚ) + 1/2) * 10 ^ ze * (5 / ((2 ^ 63 - 3 : ℕ) : ℚ)) := by ring
    · have hf_lt : f < 1/2 := hround_neg.mp h_down
      have h_no_ru : ¬ (g.round .to_nearest = 1 ∨
          (g.round .to_nearest = 0 ∧ zm % 2 = 1)) := by
        push_neg
        refine ⟨?_, ?_⟩
        · intro heq; rw [heq] at h_down; exact absurd h_down (by norm_num)
        · intro heq; rw [heq] at h_down; exact absurd h_down (by norm_num)
      have h_val := doRoundUp_value_noRoundUp g zm ze h_no_ru loc res_pos hok_pos hres_pos_mant_ne
      rw [h_val]
      have h_diff : (zm.toNat : ℚ) * 10 ^ ze - ((zm.toNat : ℚ) + f) * 10 ^ ze
          = -f * 10 ^ ze := by ring
      rw [h_diff, abs_mul, abs_of_nonneg h10ze'_nn]
      have h_abs : |(-f : ℚ)| = f := by rw [abs_neg, abs_of_nonneg hf_nn]
      rw [h_abs]
      have h_final : f ≤ ((zm.toNat : ℚ) + f) * (5 / ((2 ^ 63 - 3 : ℕ) : ℚ)) := by
        rw [h_denom_val]
        rw [show ((zm.toNat : ℚ) + f) * (5 / (9223372036854775805 : ℚ))
              = 5 * ((zm.toNat : ℚ) + f) / 9223372036854775805 by ring]
        rw [le_div_iff₀ (by norm_num : (0 : ℚ) < 9223372036854775805)]
        nlinarith [hf_nn, hf_lt, hzm_q_ge]
      calc f * 10 ^ ze
          ≤ ((zm.toNat : ℚ) + f) * (5 / ((2 ^ 63 - 3 : ℕ) : ℚ)) * 10 ^ ze :=
            mul_le_mul_of_nonneg_right h_final h10ze'_nn
        _ = ((zm.toNat : ℚ) + f) * 10 ^ ze * (5 / ((2 ^ 63 - 3 : ℕ) : ℚ)) := by ring

/-- Supremum-tight relative error bound `5 / (2^63 + 7)`, using the floor-residue
constraint `f >= 8/10` when `zm = mantissaFloor`. -/
lemma doRoundUp_rounds_to_nearest_supTight (g : Guard) (zm : UInt64) (ze : Int) (f : ℚ)
    (hf_rep : represents g f)
    (h_zm_ge : (mantissaFloor : ℕ) ≤ zm.toNat)
    (h_zm_le_max : zm.toNat ≤ maxRep.toNat)
    (h_floor_constraint : zm.toNat = mantissaFloor → (8 : ℚ) / 10 ≤ f)
    (loc : String) (res_pos : RoundResult)
    (hok_pos : g.doRoundUp false zm ze largeRange.min largeRange.max .to_nearest loc = .ok res_pos)
    (hres_pos_mant_ne : res_pos.mantissa_ ≠ 0) :
    |(res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_ -
       ((zm.toNat : ℚ) + f) * 10 ^ ze|
      ≤ ((zm.toNat : ℚ) + f) * 10 ^ ze * (5 / (2 ^ 63 + 7 : ℕ)) := by
  have hzm_q_ge : (mantissaFloor : ℚ) ≤ (zm.toNat : ℚ) := by exact_mod_cast h_zm_ge
  have h10ze'_pos : (0 : ℚ) < 10 ^ ze := zpow_pos (by norm_num) _
  have h10ze'_nn : (0 : ℚ) ≤ 10 ^ ze := le_of_lt h10ze'_pos
  have hf_nn := represents_nonneg hf_rep
  have hf_lt1 := represents_lt_one hf_rep
  obtain ⟨hround_pos, hround_neg, hround_zero⟩ := round_correct hf_rep
  have h_denom_val : (((2 ^ 63 + 7 : ℕ) : ℚ)) = 9223372036854775815 := by
    push_cast; norm_num
  have h_denom_pos : (0 : ℚ) < ((2 ^ 63 + 7 : ℕ) : ℚ) := by
    rw [h_denom_val]; norm_num
  have h_round_values : g.round .to_nearest = 1 ∨ g.round .to_nearest = 0
      ∨ g.round .to_nearest = -1 := by
    unfold Guard.round
    by_cases h1 : g.digits_ > 0x5000_0000_0000_0000
    · left; rw [if_pos h1]
    · rw [if_neg h1]
      by_cases h2 : g.digits_ < 0x5000_0000_0000_0000
      · right; right; rw [if_pos h2]
      · rw [if_neg h2]
        by_cases h3 : g.xbit_ = true
        · left; rw [if_pos h3]
        · right; left; rw [if_neg h3]
  have h_zm_cases : zm.toNat = maxRep.toNat ∨ zm.toNat < maxRep.toNat := by omega
  rcases h_zm_cases with h_cusp | h_nc
  · -- cusp
    have hzm_q_eq_maxRep : (zm.toNat : ℚ) = maxRepNat := by
      rw [h_cusp, maxRep_val]; norm_num
    have h_zm_eq_maxRep : zm = maxRep := UInt64.toNat_inj.mp h_cusp
    have h_zm_odd : zm % 2 = 1 := by rw [h_zm_eq_maxRep]; decide
    rcases h_round_values with h_up | h_tie | h_down
    · have h_ru : g.round .to_nearest = 1 ∨ (g.round .to_nearest = 0 ∧ zm % 2 = 1) := Or.inl h_up
      have hf_gt_half : f > 1/2 := hround_pos.mp h_up
      have h_cusp_val := doRoundUp_value_cusp g zm ze h_zm_eq_maxRep h_ru loc res_pos hok_pos hres_pos_mant_ne
      rw [h_cusp_val, hzm_q_eq_maxRep]
      have h_diff : (maxRepCuspTarget : ℚ) * 10 ^ ze - (maxRepNat + f) * 10 ^ ze
          = (3 - f) * 10 ^ ze := by ring
      rw [h_diff, abs_mul, abs_of_nonneg h10ze'_nn]
      have h_abs : |(3 - f : ℚ)| = 3 - f := by
        rw [abs_of_nonneg (by linarith : (0 : ℚ) ≤ 3 - f)]
      rw [h_abs]
      have h_final : (3 - f) ≤ (maxRepNat + f) * (5 / ((2 ^ 63 + 7 : ℕ) : ℚ)) := by
        rw [h_denom_val]
        rw [show (maxRepNat + f) * (5 / (9223372036854775815 : ℚ))
              = 5 * (maxRepNat + f) / 9223372036854775815 by ring]
        rw [le_div_iff₀ (by norm_num : (0 : ℚ) < 9223372036854775815)]
        nlinarith [hf_gt_half, hf_lt1]
      calc (3 - f) * 10 ^ ze
          ≤ (maxRepNat + f) * (5 / ((2 ^ 63 + 7 : ℕ) : ℚ)) * 10 ^ ze :=
            mul_le_mul_of_nonneg_right h_final h10ze'_nn
        _ = (maxRepNat + f) * 10 ^ ze * (5 / ((2 ^ 63 + 7 : ℕ) : ℚ)) := by ring
    · have h_ru : g.round .to_nearest = 1 ∨ (g.round .to_nearest = 0 ∧ zm % 2 = 1) :=
        Or.inr ⟨h_tie, h_zm_odd⟩
      have hf_eq : f = 1/2 := hround_zero.mp h_tie
      have h_cusp_val := doRoundUp_value_cusp g zm ze h_zm_eq_maxRep h_ru loc res_pos hok_pos hres_pos_mant_ne
      rw [h_cusp_val, hzm_q_eq_maxRep, hf_eq]
      have h_diff : (maxRepCuspTarget : ℚ) * 10 ^ ze - (maxRepNat + 1/2) * 10 ^ ze
          = (5/2) * 10 ^ ze := by ring
      rw [h_diff, abs_mul, abs_of_nonneg h10ze'_nn]
      have h_abs : |((5/2) : ℚ)| = 5/2 := by norm_num
      rw [h_abs]
      have h_final : (5/2 : ℚ) ≤ (maxRepNat + 1/2) * (5 / ((2 ^ 63 + 7 : ℕ) : ℚ)) := by
        rw [h_denom_val]
        rw [show (maxRepNat + 1/2 : ℚ) * (5 / 9223372036854775815)
              = 5 * (maxRepNat + 1/2) / 9223372036854775815 by ring]
        rw [le_div_iff₀ (by norm_num : (0 : ℚ) < 9223372036854775815)]
        norm_num
      calc (5/2 : ℚ) * 10 ^ ze
          ≤ (maxRepNat + 1/2) * (5 / ((2 ^ 63 + 7 : ℕ) : ℚ)) * 10 ^ ze :=
            mul_le_mul_of_nonneg_right h_final h10ze'_nn
        _ = (maxRepNat + 1/2) * 10 ^ ze * (5 / ((2 ^ 63 + 7 : ℕ) : ℚ)) := by ring
    · have h_no_ru : ¬ (g.round .to_nearest = 1 ∨
          (g.round .to_nearest = 0 ∧ zm % 2 = 1)) := by
        intro h
        rcases h with h1 | ⟨h0, _⟩
        · rw [h1] at h_down; exact absurd h_down (by norm_num)
        · rw [h0] at h_down; exact absurd h_down (by norm_num)
      have h_nr_val := doRoundUp_value_noRoundUp g zm ze h_no_ru loc res_pos hok_pos hres_pos_mant_ne
      rw [h_nr_val, hzm_q_eq_maxRep]
      have hf_lt_half : f < 1/2 := hround_neg.mp h_down
      have h_diff : (maxRepNat : ℚ) * 10 ^ ze - (maxRepNat + f) * 10 ^ ze
          = -f * 10 ^ ze := by ring
      rw [h_diff, abs_mul, abs_of_nonneg h10ze'_nn]
      have h_abs : |(-f : ℚ)| = f := by rw [abs_neg, abs_of_nonneg hf_nn]
      rw [h_abs]
      have h_final : f ≤ (maxRepNat + f) * (5 / ((2 ^ 63 + 7 : ℕ) : ℚ)) := by
        rw [h_denom_val]
        rw [show (maxRepNat + f) * (5 / (9223372036854775815 : ℚ))
              = 5 * (maxRepNat + f) / 9223372036854775815 by ring]
        rw [le_div_iff₀ (by norm_num : (0 : ℚ) < 9223372036854775815)]
        nlinarith [hf_nn, hf_lt_half]
      calc f * 10 ^ ze
          ≤ (maxRepNat + f) * (5 / ((2 ^ 63 + 7 : ℕ) : ℚ)) * 10 ^ ze :=
            mul_le_mul_of_nonneg_right h_final h10ze'_nn
        _ = (maxRepNat + f) * 10 ^ ze * (5 / ((2 ^ 63 + 7 : ℕ) : ℚ)) := by ring
  · -- non-cusp
    have h_no_ovf : zm.toNat + 1 ≤ maxRep.toNat := by omega
    by_cases h_floor : zm.toNat = mantissaFloor
    · -- floor case: f >= 8/10
      have hf_ge : (8 : ℚ) / 10 ≤ f := h_floor_constraint h_floor
      have hzm_q_eq_floor : (zm.toNat : ℚ) = mantissaFloor := by
        rw [h_floor]; norm_num
      have h_up : g.round .to_nearest = 1 := by
        rcases h_round_values with h1 | h2 | h3
        · exact h1
        · -- f = 1/2, contradicts f ≥ 8/10.
          have : f = 1/2 := hround_zero.mp h2
          linarith
        · -- f < 1/2, contradicts f ≥ 8/10.
          have : f < 1/2 := hround_neg.mp h3
          linarith
      have h_ru : g.round .to_nearest = 1 ∨ (g.round .to_nearest = 0 ∧ zm % 2 = 1) := Or.inl h_up
      have h_val := doRoundUp_value_roundUp_noOverflow g zm ze h_ru h_no_ovf loc res_pos hok_pos hres_pos_mant_ne
      rw [h_val, hzm_q_eq_floor]
      have h_diff : ((mantissaFloor : ℚ) + 1) * 10 ^ ze - ((mantissaFloor : ℚ) + f) * 10 ^ ze
          = (1 - f) * 10 ^ ze := by ring
      rw [h_diff, abs_mul, abs_of_nonneg h10ze'_nn]
      have h_abs : |(1 - f : ℚ)| = 1 - f := by
        rw [abs_of_nonneg (by linarith : (0 : ℚ) ≤ 1 - f)]
      rw [h_abs]
      have h_final : (1 - f) ≤ (mantissaFloor + f) * (5 / ((2 ^ 63 + 7 : ℕ) : ℚ)) := by
        rw [h_denom_val]
        rw [show ((mantissaFloor : ℚ) + f) * (5 / 9223372036854775815)
              = 5 * ((mantissaFloor : ℚ) + f) / 9223372036854775815 by ring]
        rw [le_div_iff₀ (by norm_num : (0 : ℚ) < 9223372036854775815)]
        nlinarith [hf_ge, hf_lt1]
      calc (1 - f) * 10 ^ ze
          ≤ (mantissaFloor + f) * (5 / ((2 ^ 63 + 7 : ℕ) : ℚ)) * 10 ^ ze :=
            mul_le_mul_of_nonneg_right h_final h10ze'_nn
        _ = (mantissaFloor + f) * 10 ^ ze * (5 / ((2 ^ 63 + 7 : ℕ) : ℚ)) := by ring
    · -- non-floor
      have h_zm_gt : mantissaFloorSucc ≤ zm.toNat := by omega
      have hzm_q_gt : (mantissaFloorSucc : ℚ) ≤ (zm.toNat : ℚ) := by exact_mod_cast h_zm_gt
      rcases h_round_values with h_up | h_tie | h_down
      · -- Round = 1: roundUp fires, m1 = zm + 1. Error = (1-f)·10^ze.
        have hf_gt : f > 1/2 := hround_pos.mp h_up
        have h_ru : g.round .to_nearest = 1 ∨ (g.round .to_nearest = 0 ∧ zm % 2 = 1) := Or.inl h_up
        have h_val := doRoundUp_value_roundUp_noOverflow g zm ze h_ru h_no_ovf loc res_pos hok_pos hres_pos_mant_ne
        rw [h_val]
        have h_diff : ((zm.toNat : ℚ) + 1) * 10 ^ ze - ((zm.toNat : ℚ) + f) * 10 ^ ze
            = (1 - f) * 10 ^ ze := by ring
        rw [h_diff, abs_mul, abs_of_nonneg h10ze'_nn]
        have h_abs : |(1 - f : ℚ)| = 1 - f := by
          rw [abs_of_nonneg (by linarith : (0 : ℚ) ≤ 1 - f)]
        rw [h_abs]
        have h_final : (1 - f) ≤ ((zm.toNat : ℚ) + f) * (5 / ((2 ^ 63 + 7 : ℕ) : ℚ)) := by
          rw [h_denom_val]
          rw [show ((zm.toNat : ℚ) + f) * (5 / (9223372036854775815 : ℚ))
                = 5 * ((zm.toNat : ℚ) + f) / 9223372036854775815 by ring]
          rw [le_div_iff₀ (by norm_num : (0 : ℚ) < 9223372036854775815)]
          nlinarith [hf_gt, hf_lt1, hzm_q_gt]
        calc (1 - f) * 10 ^ ze
            ≤ ((zm.toNat : ℚ) + f) * (5 / ((2 ^ 63 + 7 : ℕ) : ℚ)) * 10 ^ ze :=
              mul_le_mul_of_nonneg_right h_final h10ze'_nn
          _ = ((zm.toNat : ℚ) + f) * 10 ^ ze * (5 / ((2 ^ 63 + 7 : ℕ) : ℚ)) := by ring
      · have hf_eq : f = 1/2 := hround_zero.mp h_tie
        by_cases h_odd : zm % 2 = 1
        · have h_ru : g.round .to_nearest = 1 ∨ (g.round .to_nearest = 0 ∧ zm % 2 = 1) :=
            Or.inr ⟨h_tie, h_odd⟩
          have h_val := doRoundUp_value_roundUp_noOverflow g zm ze h_ru h_no_ovf loc res_pos hok_pos hres_pos_mant_ne
          rw [h_val, hf_eq]
          have h_diff : ((zm.toNat : ℚ) + 1) * 10 ^ ze - ((zm.toNat : ℚ) + 1/2) * 10 ^ ze
              = (1/2) * 10 ^ ze := by ring
          rw [h_diff, abs_mul, abs_of_nonneg h10ze'_nn]
          have h_abs : |((1/2) : ℚ)| = 1/2 := by norm_num
          rw [h_abs]
          have h_final : (1/2 : ℚ) ≤ ((zm.toNat : ℚ) + 1/2) * (5 / ((2 ^ 63 + 7 : ℕ) : ℚ)) := by
            rw [h_denom_val]
            rw [show ((zm.toNat : ℚ) + 1/2) * (5 / (9223372036854775815 : ℚ))
                  = 5 * ((zm.toNat : ℚ) + 1/2) / 9223372036854775815 by ring]
            rw [le_div_iff₀ (by norm_num : (0 : ℚ) < 9223372036854775815)]
            linarith [hzm_q_gt]
          calc (1/2 : ℚ) * 10 ^ ze
              ≤ ((zm.toNat : ℚ) + 1/2) * (5 / ((2 ^ 63 + 7 : ℕ) : ℚ)) * 10 ^ ze :=
                mul_le_mul_of_nonneg_right h_final h10ze'_nn
            _ = ((zm.toNat : ℚ) + 1/2) * 10 ^ ze * (5 / ((2 ^ 63 + 7 : ℕ) : ℚ)) := by ring
        · have h_zm_even : zm.toNat % 2 = 0 := by
            have hne : (zm % 2).toNat ≠ (1 : UInt64).toNat := by
              intro heq; exact h_odd (UInt64.toNat_inj.mp heq)
            have hmod_nat : (zm % 2).toNat = zm.toNat % (2 : UInt64).toNat := by
              rw [UInt64.toNat_mod]
            have h2_nat : (2 : UInt64).toNat = 2 := rfl
            have h1_nat : (1 : UInt64).toNat = 1 := rfl
            rw [hmod_nat, h2_nat, h1_nat] at hne
            have hmod_lt : zm.toNat % 2 < 2 := Nat.mod_lt _ (by norm_num)
            omega
          have h_zm_ge_even : 922337203685477582 ≤ zm.toNat := by
            have h_odd_lb : mantissaFloorSucc % 2 = 1 := by norm_num
            omega
          have hzm_q_ge_even : (922337203685477582 : ℚ) ≤ (zm.toNat : ℚ) := by
            exact_mod_cast h_zm_ge_even
          have h_no_ru : ¬ (g.round .to_nearest = 1 ∨
              (g.round .to_nearest = 0 ∧ zm % 2 = 1)) := by
            push_neg
            refine ⟨?_, ?_⟩
            · intro heq; rw [heq] at h_tie; exact absurd h_tie (by norm_num)
            · intro _; exact h_odd
          have h_val := doRoundUp_value_noRoundUp g zm ze h_no_ru loc res_pos hok_pos hres_pos_mant_ne
          rw [h_val, hf_eq]
          have h_diff : (zm.toNat : ℚ) * 10 ^ ze - ((zm.toNat : ℚ) + 1/2) * 10 ^ ze
              = -(1/2) * 10 ^ ze := by ring
          rw [h_diff, abs_mul, abs_of_nonneg h10ze'_nn]
          have h_abs : |(-(1/2) : ℚ)| = 1/2 := by norm_num
          rw [h_abs]
          have h_final : (1/2 : ℚ) ≤ ((zm.toNat : ℚ) + 1/2) * (5 / ((2 ^ 63 + 7 : ℕ) : ℚ)) := by
            rw [h_denom_val]
            rw [show ((zm.toNat : ℚ) + 1/2) * (5 / (9223372036854775815 : ℚ))
                  = 5 * ((zm.toNat : ℚ) + 1/2) / 9223372036854775815 by ring]
            rw [le_div_iff₀ (by norm_num : (0 : ℚ) < 9223372036854775815)]
            linarith [hzm_q_ge_even]
          calc (1/2 : ℚ) * 10 ^ ze
              ≤ ((zm.toNat : ℚ) + 1/2) * (5 / ((2 ^ 63 + 7 : ℕ) : ℚ)) * 10 ^ ze :=
                mul_le_mul_of_nonneg_right h_final h10ze'_nn
            _ = ((zm.toNat : ℚ) + 1/2) * 10 ^ ze * (5 / ((2 ^ 63 + 7 : ℕ) : ℚ)) := by ring
      · have hf_lt : f < 1/2 := hround_neg.mp h_down
        have h_no_ru : ¬ (g.round .to_nearest = 1 ∨
            (g.round .to_nearest = 0 ∧ zm % 2 = 1)) := by
          push_neg
          refine ⟨?_, ?_⟩
          · intro heq; rw [heq] at h_down; exact absurd h_down (by norm_num)
          · intro heq; rw [heq] at h_down; exact absurd h_down (by norm_num)
        have h_val := doRoundUp_value_noRoundUp g zm ze h_no_ru loc res_pos hok_pos hres_pos_mant_ne
        rw [h_val]
        have h_diff : (zm.toNat : ℚ) * 10 ^ ze - ((zm.toNat : ℚ) + f) * 10 ^ ze
            = -f * 10 ^ ze := by ring
        rw [h_diff, abs_mul, abs_of_nonneg h10ze'_nn]
        have h_abs : |(-f : ℚ)| = f := by rw [abs_neg, abs_of_nonneg hf_nn]
        rw [h_abs]
        have h_final : f ≤ ((zm.toNat : ℚ) + f) * (5 / ((2 ^ 63 + 7 : ℕ) : ℚ)) := by
          rw [h_denom_val]
          rw [show ((zm.toNat : ℚ) + f) * (5 / (9223372036854775815 : ℚ))
                = 5 * ((zm.toNat : ℚ) + f) / 9223372036854775815 by ring]
          rw [le_div_iff₀ (by norm_num : (0 : ℚ) < 9223372036854775815)]
          nlinarith [hf_nn, hf_lt, hzm_q_gt]
        calc f * 10 ^ ze
            ≤ ((zm.toNat : ℚ) + f) * (5 / ((2 ^ 63 + 7 : ℕ) : ℚ)) * 10 ^ ze :=
              mul_le_mul_of_nonneg_right h_final h10ze'_nn
          _ = ((zm.toNat : ℚ) + f) * 10 ^ ze * (5 / ((2 ^ 63 + 7 : ℕ) : ℚ)) := by ring

/-! ## Value characterization wrappers with `shouldRoundUp` predicate -/

/-- Round-up fires when `round = 1` or when `round = 0` and mantissa is odd. -/
def Guard.shouldRoundUp (g : Guard) (m : UInt64) : Prop :=
  g.round .to_nearest = 1 ∨ (g.round .to_nearest = 0 ∧ m % 2 = 1)

/-- No round-up: `result.toRat = zm * 10^ze'`. -/
theorem doRoundUp_value_no_roundUp
    (g : Guard) (zm : UInt64) (ze' : Int)
    (h_no_roundUp : ¬ g.shouldRoundUp zm)
    (loc : String) (res : RoundResult)
    (hok : g.doRoundUp false zm ze' largeRange.min largeRange.max .to_nearest loc = .ok res)
    (h_mant_ne : res.mantissa_ ≠ 0) :
    (res.mantissa_.toNat : ℚ) * 10 ^ res.exponent_ = (zm.toNat : ℚ) * 10 ^ ze' :=
  doRoundUp_value_noRoundUp g zm ze' h_no_roundUp loc res hok h_mant_ne

/-- Round-up without cusp: `result.toRat = (zm + 1) * 10^ze'`. -/
theorem doRoundUp_value_roundUp_noCusp
    (g : Guard) (zm : UInt64) (ze' : Int)
    (h_roundUp : g.shouldRoundUp zm)
    (h_no_cusp : zm.toNat + 1 ≤ maxRep.toNat)
    (loc : String) (res : RoundResult)
    (hok : g.doRoundUp false zm ze' largeRange.min largeRange.max .to_nearest loc = .ok res)
    (h_mant_ne : res.mantissa_ ≠ 0) :
    (res.mantissa_.toNat : ℚ) * 10 ^ res.exponent_ = ((zm.toNat : ℚ) + 1) * 10 ^ ze' :=
  doRoundUp_value_roundUp_noOverflow g zm ze' h_roundUp h_no_cusp loc res hok h_mant_ne

/-- Cusp case (`zm = maxRep`): `result.toRat = maxRepCuspTarget * 10^ze'`. -/
theorem doRoundUp_value_roundUp_cusp
    (g : Guard) (zm : UInt64) (ze' : Int)
    (h_zm_eq_maxRep : zm = maxRep)
    (h_roundUp : g.shouldRoundUp zm)
    (loc : String) (res : RoundResult)
    (hok : g.doRoundUp false zm ze' largeRange.min largeRange.max .to_nearest loc = .ok res)
    (h_mant_ne : res.mantissa_ ≠ 0) :
    (res.mantissa_.toNat : ℚ) * 10 ^ res.exponent_ = maxRepCuspTarget * 10 ^ ze' :=
  doRoundUp_value_cusp g zm ze' h_zm_eq_maxRep h_roundUp loc res hok h_mant_ne

/-! ## `.downward` mode -/

/-- For `.downward`, round-up fires iff `sbit_ = true` and guard content is nonzero. -/
def Guard.shouldRoundUp_downward (g : Guard) : Prop :=
  g.sbit_ = true ∧ (g.digits_ > 0 ∨ g.xbit_ = true)

/-- `g.round .downward = 1` iff `g.shouldRoundUp_downward`. -/
lemma round_downward_eq_one_iff (g : Guard) :
    g.round .downward = 1 ↔ g.shouldRoundUp_downward := by
  constructor
  · intro h
    by_cases hs : g.sbit_ = true
    · refine ⟨hs, ?_⟩
      by_cases hcond_d : g.digits_ > 0
      · exact Or.inl hcond_d
      · by_cases hcond_x : g.xbit_ = true
        · exact Or.inr hcond_x
        · exfalso
          have hd_false : decide (g.digits_ > 0) = false := decide_eq_false hcond_d
          have hx_false : g.xbit_ = false := Bool.not_eq_true _ |>.mp hcond_x
          have h_bool_false : (decide (g.digits_ > 0) || g.xbit_) = false := by
            rw [hd_false, hx_false]; rfl
          have : g.round .downward = -1 := by
            change (if g.sbit_ then
              (if (decide (g.digits_ > 0) || g.xbit_) = true then (1 : Int) else -1)
             else -1) = -1
            rw [hs]
            simp only [if_true]
            rw [h_bool_false]
            simp only [Bool.false_eq_true, if_false]
          rw [this] at h; exact absurd h (by decide)
    · exfalso
      have hs_false : g.sbit_ = false := Bool.not_eq_true _ |>.mp hs
      have : g.round .downward = -1 := by
        change (if g.sbit_ then
              (if (decide (g.digits_ > 0) || g.xbit_) = true then (1 : Int) else -1)
             else -1) = -1
        rw [hs_false]; simp only [Bool.false_eq_true, if_false]
      rw [this] at h; exact absurd h (by decide)
  · intro ⟨hs, hor⟩
    have h_bool_true : (decide (g.digits_ > 0) || g.xbit_) = true := by
      rcases hor with h1 | h2
      · rw [decide_eq_true h1]; rfl
      · rw [h2]; rw [Bool.or_true]
    change (if g.sbit_ then
              (if (decide (g.digits_ > 0) || g.xbit_) = true then (1 : Int) else -1)
             else -1) = 1
    rw [hs]; simp only [if_true]
    rw [h_bool_true]; simp only [if_true]

/-- `g.round .downward = -1` iff `¬ g.shouldRoundUp_downward`. -/
lemma round_downward_eq_neg_one_iff (g : Guard) :
    g.round .downward = -1 ↔ ¬ g.shouldRoundUp_downward := by
  have hvals : g.round .downward = 1 ∨ g.round .downward = -1 := by
    unfold Guard.round
    split_ifs <;> first | (left; rfl) | (right; rfl)
  constructor
  · intro h h_ru
    have h1 : g.round .downward = 1 := (round_downward_eq_one_iff g).mpr h_ru
    rw [h] at h1; exact absurd h1 (by decide)
  · intro h_no
    rcases hvals with h1 | hm1
    · exact absurd ((round_downward_eq_one_iff g).mp h1) h_no
    · exact hm1

/-- For `.downward`, the boolean `roundUp` in `doRoundUp` equals `false`
when `shouldRoundUp_downward` is false. -/
private lemma roundUp_bool_downward_false (g : Guard) (m : UInt64)
    (h_no : ¬ g.shouldRoundUp_downward) :
    ((g.round .downward == 1) || ((g.round .downward == 0) && (m % 2 == 1))) = false := by
  have h_neg1 : g.round .downward = -1 := (round_downward_eq_neg_one_iff g).mpr h_no
  rw [h_neg1]; rfl

/-- For `.downward`, the boolean `roundUp` in `doRoundUp` equals `true`
when `shouldRoundUp_downward` is true. -/
private lemma roundUp_bool_downward_true (g : Guard) (m : UInt64)
    (h_yes : g.shouldRoundUp_downward) :
    ((g.round .downward == 1) || ((g.round .downward == 0) && (m % 2 == 1))) = true := by
  have h1 : g.round .downward = 1 := (round_downward_eq_one_iff g).mpr h_yes
  rw [h1]; rfl

/-- The no-round-up case for `.downward`: `result.mantissa * 10^exp = zm * 10^ze'`. -/
theorem doRoundUp_value_downward_truncate
    (g : Guard) (zn : Bool) (zm : UInt64) (ze' : Int)
    (h_no_roundUp : ¬ g.shouldRoundUp_downward)
    (loc : String) (res : RoundResult)
    (hok : g.doRoundUp zn zm ze' largeRange.min largeRange.max .downward loc = .ok res)
    (h_mant_ne : res.mantissa_ ≠ 0) :
    (res.mantissa_.toNat : ℚ) * 10 ^ res.exponent_ = (zm.toNat : ℚ) * 10 ^ ze' := by
  have h_ru_false :
      ((g.round .downward == 1) || ((g.round .downward == 0) && (zm % 2 == 1))) = false :=
    roundUp_bool_downward_false g zm h_no_roundUp
  have hres_eq : res = Guard.bringIntoRange zn zm ze' largeRange.min := by
    unfold Guard.doRoundUp at hok
    simp only [Guard.doDropDigit] at hok
    rw [h_ru_false] at hok
    simp only [Bool.false_and, if_false, Bool.false_eq_true] at hok
    by_cases h_ovf : (Guard.bringIntoRange zn zm ze' largeRange.min).exponent_ > maxExponent
    · rw [if_pos h_ovf] at hok; exact absurd hok (by intro h; cases h)
    · rw [if_neg h_ovf] at hok; exact (Except.ok.inj hok).symm
  rw [hres_eq]
  have hne' : (Guard.bringIntoRange zn zm ze' largeRange.min).mantissa_ ≠ 0 := hres_eq ▸ h_mant_ne
  unfold Guard.bringIntoRange
  by_cases hresc : zm < largeRange.min
  · rw [if_pos hresc]; simp only []
    by_cases h_under : ze' - 1 < minExponent
    · exfalso; apply hne'; unfold Guard.bringIntoRange; rw [if_pos hresc]; simp only []; rw [if_pos h_under]
    · rw [if_neg h_under]
      have hzm_mul_10 : (zm * 10).toNat = zm.toNat * 10 :=
        m_mul_ten_no_overflow (UInt64.lt_iff_toNat_lt.mp hresc)
      change ((zm * 10).toNat : ℚ) * 10 ^ (ze' - 1) = (zm.toNat : ℚ) * 10 ^ ze'
      rw [hzm_mul_10]; push_cast
      rw [show (ze' - 1 : ℤ) = ze' + (-1) from by ring,
          zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0), zpow_neg_one]
      field_simp
  · rw [if_neg hresc]; simp only []
    by_cases h_under : ze' < minExponent
    · exfalso; apply hne'; unfold Guard.bringIntoRange; rw [if_neg hresc]; simp only []; rw [if_pos h_under]
    · rw [if_neg h_under]

/-- The round-up no-cusp case for `.downward`. -/
theorem doRoundUp_value_downward_roundUp_noCusp
    (g : Guard) (zn : Bool) (zm : UInt64) (ze' : Int)
    (h_roundUp : g.shouldRoundUp_downward)
    (h_no_cusp : zm.toNat + 1 ≤ maxRep.toNat)
    (loc : String) (res : RoundResult)
    (hok : g.doRoundUp zn zm ze' largeRange.min largeRange.max .downward loc = .ok res)
    (h_mant_ne : res.mantissa_ ≠ 0) :
    (res.mantissa_.toNat : ℚ) * 10 ^ res.exponent_ = ((zm.toNat : ℚ) + 1) * 10 ^ ze' := by
  have h_ru_true :
      ((g.round .downward == 1) || ((g.round .downward == 0) && (zm % 2 == 1))) = true :=
    roundUp_bool_downward_true g zm h_roundUp
  have h_m_lt_maxRep : zm.toNat < maxRep.toNat := by rw [maxRep_val] at h_no_cusp ⊢; omega
  have h_m_lt_maxRep_uint : ¬ (zm ≥ maxRep) := by
    intro h; have := UInt64.le_iff_toNat_le.mp h; omega
  have h_m_lt_maxMant_uint : ¬ (zm ≥ largeRange.max) := by
    intro h; have := UInt64.le_iff_toNat_le.mp h
    rw [largeRange_max_val] at this; rw [maxRep_val] at h_m_lt_maxRep; omega
  have h_cusp_cond_false : ((zm ≥ largeRange.max) || (zm ≥ maxRep)) = false := by
    rw [Bool.or_eq_false_iff]
    exact ⟨decide_eq_false h_m_lt_maxMant_uint, decide_eq_false h_m_lt_maxRep_uint⟩
  have hres_eq : res = Guard.bringIntoRange zn (zm + 1) ze' largeRange.min := by
    unfold Guard.doRoundUp at hok
    simp only [Guard.doDropDigit] at hok
    rw [h_ru_true, h_cusp_cond_false] at hok
    simp only [Bool.and_false, if_false, Bool.false_eq_true, if_true] at hok
    by_cases h_ovf : (Guard.bringIntoRange zn (zm + 1) ze' largeRange.min).exponent_ > maxExponent
    · rw [if_pos h_ovf] at hok; exact absurd hok (by intro h; cases h)
    · rw [if_neg h_ovf] at hok; exact (Except.ok.inj hok).symm
  rw [hres_eq]
  have hne' : (Guard.bringIntoRange zn (zm + 1) ze' largeRange.min).mantissa_ ≠ 0 := hres_eq ▸ h_mant_ne
  have h_m_le_maxRep : zm.toNat ≤ maxRep.toNat := by omega
  have hm_add1_toNat : (zm + 1).toNat = zm.toNat + 1 := m_add_one_no_overflow h_m_le_maxRep
  unfold Guard.bringIntoRange
  by_cases hresc : zm + 1 < largeRange.min
  · rw [if_pos hresc]; simp only []
    have h_m1_lt_min : (zm + 1).toNat < largeRange.min.toNat := UInt64.lt_iff_toNat_lt.mp hresc
    have h_m1_mul10_toNat : ((zm + 1) * 10).toNat = (zm + 1).toNat * 10 := by
      rw [UInt64.toNat_mul]; have h10u : (10 : UInt64).toNat = 10 := rfl; rw [h10u]
      have : (zm + 1).toNat * 10 < 2 ^ 64 := by
        rw [largeRange_min_val] at h_m1_lt_min
        calc (zm + 1).toNat * 10 < 1000000000000000000 * 10 :=
              Nat.mul_lt_mul_of_pos_right h_m1_lt_min (by norm_num)
          _ < 2 ^ 64 := by norm_num
      exact Nat.mod_eq_of_lt this
    by_cases h_under : ze' - 1 < minExponent
    · exfalso; apply hne'; unfold Guard.bringIntoRange; rw [if_pos hresc]; simp only []; rw [if_pos h_under]
    · rw [if_neg h_under]
      change ((((zm + 1) * 10)).toNat : ℚ) * 10 ^ (ze' - 1) = ((zm.toNat : ℚ) + 1) * 10 ^ ze'
      rw [h_m1_mul10_toNat, hm_add1_toNat]; push_cast
      rw [show (ze' - 1 : ℤ) = ze' + (-1) from by ring,
          zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0), zpow_neg_one]; field_simp
  · rw [if_neg hresc]; simp only []
    by_cases h_under : ze' < minExponent
    · exfalso; apply hne'; unfold Guard.bringIntoRange; rw [if_neg hresc]; simp only []; rw [if_pos h_under]
    · rw [if_neg h_under]
      change ((zm + 1).toNat : ℚ) * 10 ^ ze' = ((zm.toNat : ℚ) + 1) * 10 ^ ze'
      rw [hm_add1_toNat]; push_cast; ring

/-- `.downward` cusp case (`zm = maxRep`): output is `maxRepCuspTarget * 10^ze'`. -/
theorem doRoundUp_value_downward_roundUp_cusp
    (g : Guard) (zn : Bool) (zm : UInt64) (ze' : Int)
    (h_zm_eq_maxRep : zm = maxRep)
    (h_roundUp : g.shouldRoundUp_downward)
    (loc : String) (res : RoundResult)
    (hok : g.doRoundUp zn zm ze' largeRange.min largeRange.max .downward loc = .ok res)
    (h_mant_ne : res.mantissa_ ≠ 0) :
    (res.mantissa_.toNat : ℚ) * 10 ^ res.exponent_ = maxRepCuspTarget * 10 ^ ze' := by
  have h_ru_true :
      ((g.round .downward == 1) || ((g.round .downward == 0) && (zm % 2 == 1))) = true :=
    roundUp_bool_downward_true g zm h_roundUp
  have h_cusp_cond_true : ((zm ≥ largeRange.max) || (zm ≥ maxRep)) = true := by
    rw [Bool.or_eq_true]; right; rw [h_zm_eq_maxRep]; decide
  set g' := g.push (zm % 10) with hg'_def
  have h_g_sbit : g.sbit_ = true := h_roundUp.1
  have h_g'_sbit : g'.sbit_ = true := by rw [hg'_def]; unfold Guard.push; exact h_g_sbit
  have h_g'_digits_toNat : g'.digits_.toNat = g.digits_.toNat / 16 + (zm.toNat % 10 % 16) * 2 ^ 60 := by
    rw [hg'_def]; have := toNat_push_digits g (zm % 10)
    rw [UInt64.toNat_mod] at this; have h10 : (10 : UInt64).toNat = 10 := rfl; rw [h10] at this; exact this
  have h_zm_mod10 : zm.toNat % 10 = 7 := by rw [h_zm_eq_maxRep]; decide
  have h_zm_mod10_mod16 : zm.toNat % 10 % 16 = 7 := by rw [h_zm_mod10]
  have h_g'_digits_gt : g'.digits_ > 0 := by
    change (0 : UInt64) < g'.digits_; rw [UInt64.lt_iff_toNat_lt]
    rw [show (0 : UInt64).toNat = 0 from rfl, h_g'_digits_toNat, h_zm_mod10_mod16]; omega
  have h_g'_round : g'.round .downward = 1 :=
    (round_downward_eq_one_iff g').mpr ⟨h_g'_sbit, Or.inl h_g'_digits_gt⟩
  have h_ru'_true :
      ((g'.round .downward == 1) || ((g'.round .downward == 0) && ((zm / 10) % 2 == 1))) = true := by
    rw [h_g'_round]; rfl
  have h_m_div10_toNat : (zm / 10).toNat = mantissaFloor := by
    rw [UInt64.toNat_div, h_zm_eq_maxRep]; decide
  have h_m_div10_le_maxRep : (zm / 10).toNat ≤ maxRep.toNat := by
    rw [h_m_div10_toNat, maxRep_val]; norm_num
  have h_m1_toNat : (zm / 10 + 1).toNat = mantissaFloorSucc := by
    rw [m_add_one_no_overflow h_m_div10_le_maxRep, h_m_div10_toNat]
  have h_m1_lt_min : (zm / 10 + 1) < largeRange.min := by
    rw [UInt64.lt_iff_toNat_lt, h_m1_toNat, largeRange_min_val]; norm_num
  have h_m1_mul10_toNat : ((zm / 10 + 1) * 10).toNat = maxRepCuspTarget := by
    rw [UInt64.toNat_mul]; have h10 : (10 : UInt64).toNat = 10 := rfl; rw [h10, h_m1_toNat]
  have hres_eq : res = Guard.bringIntoRange zn (zm / 10 + 1) (ze' + 1) largeRange.min := by
    unfold Guard.doRoundUp at hok
    simp only [Guard.doDropDigit] at hok
    rw [h_ru_true, h_cusp_cond_true] at hok
    simp only [Bool.true_and, if_true] at hok
    rw [h_ru'_true] at hok
    simp only [if_true, if_pos h_m1_lt_min] at hok
    by_cases h_ovf : (Guard.bringIntoRange zn (zm / 10 + 1) (ze' + 1) largeRange.min).exponent_ > maxExponent
    · rw [if_pos h_ovf] at hok; exact absurd hok (by intro h; cases h)
    · rw [if_neg h_ovf] at hok; exact (Except.ok.inj hok).symm
  rw [hres_eq]
  have hne' : (Guard.bringIntoRange zn (zm / 10 + 1) (ze' + 1) largeRange.min).mantissa_ ≠ 0 := hres_eq ▸ h_mant_ne
  simp only [Guard.bringIntoRange, if_pos h_m1_lt_min]
  by_cases h_under : ze' + 1 - 1 < minExponent
  · exfalso; apply hne'; simp only [Guard.bringIntoRange, if_pos h_m1_lt_min, if_pos h_under]
  · rw [if_neg h_under]
    change (((zm / 10 + 1) * 10).toNat : ℚ) * 10 ^ (ze' + 1 - 1) = maxRepCuspTarget * 10 ^ ze'
    rw [h_m1_mul10_toNat]; rw [show (ze' + 1 - 1 : ℤ) = ze' from by ring]; push_cast; ring

/-- `.downward`-mode analogue of `doRoundUp_output_invariants`. -/
lemma doRoundUp_output_invariants_downward
    (g : Guard) (neg : Bool) (m : UInt64) (e : Int)
    (h_lb : mantissaFloor ≤ m.toNat)
    (h_ub : m.toNat ≤ maxRep.toNat)
    (loc : String) (res : RoundResult)
    (hok : g.doRoundUp neg m e largeRange.min largeRange.max .downward loc = .ok res)
    (hne : res.mantissa_ ≠ 0) :
    largeRange.min.toNat ≤ res.mantissa_.toNat ∧
    res.mantissa_.toNat ≤ largeRange.max.toNat ∧
    minExponent ≤ res.exponent_ ∧
    (res.mantissa_.toNat > maxRep.toNat → res.mantissa_.toNat % 10 = 0) := by
  have hmaxRep_v : maxRep.toNat = maxRepNat := maxRep_val
  have hminMant_v : largeRange.min.toNat = 1000000000000000000 := largeRange_min_val
  have hmaxMant_v : largeRange.max.toNat = 9999999999999999999 := largeRange_max_val
  have hm_add1_toNat : (m + 1).toNat = m.toNat + 1 := m_add_one_no_overflow h_ub
  have h_m_not_ge_maxMant : ¬ (m ≥ largeRange.max) := by
    intro h; have := UInt64.le_iff_toNat_le.mp h
    rw [hmaxMant_v] at this; rw [hmaxRep_v] at h_ub; omega
  have h_ge_maxMant_false : decide (m ≥ largeRange.max) = false :=
    decide_eq_false h_m_not_ge_maxMant
  -- Unfold doRoundUp to extract res
  unfold Guard.doRoundUp Guard.bringIntoRange at hok
  simp only [Guard.doDropDigit] at hok
  by_cases h_ru : (g.round .downward == 1 || (g.round .downward == 0 && m % 2 == 1)) = true
  · rw [show (g.round .downward == 1 || (g.round .downward == 0 && m % 2 == 1)) = true from h_ru] at hok
    by_cases h_m_eq_maxRep : m = maxRep
    · -- cusp
      have h_ge_maxRep : decide (m ≥ maxRep) = true := by rw [h_m_eq_maxRep]; decide
      have h_cusp_cond_true : ((m ≥ largeRange.max) || (m ≥ maxRep)) = true := by
        rw [Bool.or_eq_true]; right; exact h_ge_maxRep
      rw [show ((m ≥ largeRange.max) || (m ≥ maxRep)) = true from h_cusp_cond_true] at hok
      simp only [Bool.and_self, if_true] at hok
      have h_m_div10_toNat : (m / 10).toNat = mantissaFloor := by
        rw [UInt64.toNat_div, h_m_eq_maxRep]; decide
      set g' := g.push (m % 10) with hg'_def
      have h_g_round_one : g.round .downward = 1 := by
        rcases Bool.or_eq_true _ _ |>.mp h_ru with h1 | h2
        · exact beq_iff_eq.mp h1
        · exfalso
          rcases Bool.and_eq_true _ _ |>.mp h2 with ⟨hr0, _⟩
          have h_eq_0 : g.round .downward = 0 := beq_iff_eq.mp hr0
          have hvals : g.round .downward = 1 ∨ g.round .downward = -1 := by
            unfold Guard.round; split_ifs <;> first | (left; rfl) | (right; rfl)
          rcases hvals with h1 | hm1
          · rw [h_eq_0] at h1; exact absurd h1 (by decide)
          · rw [h_eq_0] at hm1; exact absurd hm1 (by decide)
      have h_g_sbit : g.sbit_ = true := ((round_downward_eq_one_iff g).mp h_g_round_one).1
      have h_g'_sbit : g'.sbit_ = true := by rw [hg'_def]; unfold Guard.push; exact h_g_sbit
      have h_g'_digits_toNat : g'.digits_.toNat = g.digits_.toNat / 16 + (m.toNat % 10 % 16) * 2 ^ 60 := by
        rw [hg'_def]; have := toNat_push_digits g (m % 10)
        rw [UInt64.toNat_mod] at this; have h10 : (10 : UInt64).toNat = 10 := rfl; rw [h10] at this; exact this
      have h_m_mod10_mod16 : m.toNat % 10 % 16 = 7 := by rw [show m.toNat % 10 = 7 from by rw [h_m_eq_maxRep]; decide]
      have h_g'_digits_gt : g'.digits_ > 0 := by
        change (0 : UInt64) < g'.digits_; rw [UInt64.lt_iff_toNat_lt]
        rw [show (0 : UInt64).toNat = 0 from rfl, h_g'_digits_toNat, h_m_mod10_mod16]; omega
      have h_g'_round : g'.round .downward = 1 :=
        (round_downward_eq_one_iff g').mpr ⟨h_g'_sbit, Or.inl h_g'_digits_gt⟩
      have h_ru'_true :
          (g'.round .downward == 1 || (g'.round .downward == 0 && (m / 10) % 2 == 1)) = true := by
        rw [h_g'_round]; rfl
      rw [h_ru'_true] at hok; simp only [if_true] at hok
      have h_m_div10_le_maxRep : (m / 10).toNat ≤ maxRep.toNat := by
        rw [h_m_div10_toNat, maxRep_val]; norm_num
      have h_m1_toNat : (m / 10 + 1).toNat = mantissaFloorSucc := by
        rw [m_add_one_no_overflow h_m_div10_le_maxRep, h_m_div10_toNat]
      have h_m1_lt_min : (m / 10 + 1) < largeRange.min := by
        rw [UInt64.lt_iff_toNat_lt, h_m1_toNat, largeRange_min_val]; norm_num
      rw [if_pos h_m1_lt_min] at hok; simp only [] at hok
      have h_m1_mul10_toNat : ((m / 10 + 1) * 10).toNat = maxRepCuspTarget := by
        rw [UInt64.toNat_mul]; have h10 : (10 : UInt64).toNat = 10 := rfl; rw [h10, h_m1_toNat]
      by_cases h_under : e + 1 - 1 < minExponent
      · exfalso; apply hne
        rw [if_pos h_under] at hok
        (try (simp only [Except.ok.injEq] at hok))
        (try (obtain rfl := Except.ok.inj hok))
        (try subst hok)
        rfl
      · rw [if_neg h_under] at hok
        have h_no_ovf : ¬ (e + 1 - 1 > maxExponent) := by
          intro h_ovf
          rw [if_pos h_ovf] at hok
          exact absurd hok (by intro h; cases h)
        rw [if_neg h_no_ovf] at hok
        obtain rfl := Except.ok.inj hok
        refine ⟨?_, ?_, ?_, ?_⟩
        · change largeRange.min.toNat ≤ ((m / 10 + 1) * 10).toNat
          rw [h_m1_mul10_toNat, hminMant_v]; norm_num
        · change ((m / 10 + 1) * 10).toNat ≤ largeRange.max.toNat
          rw [h_m1_mul10_toNat, hmaxMant_v]; norm_num
        · change minExponent ≤ e + 1 - 1; push_neg at h_under; exact h_under
        · intro _; change ((m / 10 + 1) * 10).toNat % 10 = 0; rw [h_m1_mul10_toNat]
    · -- non-cusp
      have h_m_lt_maxRep : m.toNat < maxRep.toNat := by
        have : m.toNat ≠ maxRep.toNat := fun heq => h_m_eq_maxRep (UInt64.toNat_inj.mp heq); omega
      have h_cusp_cond_false : ((m ≥ largeRange.max) || (m ≥ maxRep)) = false := by
        rw [Bool.or_eq_false_iff]
        exact ⟨h_ge_maxMant_false, decide_eq_false (by intro h; have := UInt64.le_iff_toNat_le.mp h; omega)⟩
      rw [h_cusp_cond_false] at hok
      simp only [Bool.and_false, if_false, Bool.false_eq_true, if_true] at hok
      have h_m1_le_maxRep : (m + 1).toNat ≤ maxRep.toNat := by rw [hm_add1_toNat]; omega
      by_cases h_resc : m + 1 < largeRange.min
      · rw [if_pos h_resc] at hok; simp only [] at hok
        have h_m1_lt_min : (m + 1).toNat < largeRange.min.toNat := UInt64.lt_iff_toNat_lt.mp h_resc
        have h_m1_mul10_toNat : ((m + 1) * 10).toNat = (m + 1).toNat * 10 := by
          rw [UInt64.toNat_mul]; have h10 : (10 : UInt64).toNat = 10 := rfl; rw [h10]
          apply Nat.mod_eq_of_lt; rw [hminMant_v] at h_m1_lt_min
          calc (m + 1).toNat * 10 < 1000000000000000000 * 10 := Nat.mul_lt_mul_of_pos_right h_m1_lt_min (by norm_num)
            _ < 2 ^ 64 := by norm_num
        by_cases h_under : e - 1 < minExponent
        · exfalso
          apply hne
          rw [if_pos h_under] at hok
          (try (simp only [Except.ok.injEq] at hok))
          (try (obtain rfl := Except.ok.inj hok))
          (try subst hok)
          rfl
        · rw [if_neg h_under] at hok
          have h_no_ovf : ¬ (e - 1 > maxExponent) := by
            intro h_ovf; rw [if_pos h_ovf] at hok
            exact absurd hok (by intro h; cases h)
          rw [if_neg h_no_ovf] at hok
          obtain rfl := Except.ok.inj hok
          refine ⟨?_, ?_, ?_, ?_⟩
          · change largeRange.min.toNat ≤ ((m + 1) * 10).toNat
            rw [h_m1_mul10_toNat, hm_add1_toNat, hminMant_v]
            have : (m.toNat + 1) * 10 ≥ maxRepCuspTarget := by omega
            omega
          · change ((m + 1) * 10).toNat ≤ largeRange.max.toNat
            rw [h_m1_mul10_toNat, hmaxMant_v, hminMant_v] at *
            calc (m + 1).toNat * 10 ≤ (1000000000000000000 - 1) * 10 := Nat.mul_le_mul_right _ (by omega)
              _ = maxMul10Witness := by norm_num
              _ ≤ 9999999999999999999 := by norm_num
          · change minExponent ≤ e - 1; push_neg at h_under; exact h_under
          · intro _; change ((m + 1) * 10).toNat % 10 = 0; rw [h_m1_mul10_toNat]; omega
      · rw [if_neg h_resc] at hok; simp only [] at hok
        have h_m1_ge_min : (m + 1).toNat ≥ largeRange.min.toNat := by
          by_contra h; push_neg at h; exact h_resc (UInt64.lt_iff_toNat_lt.mpr h)
        by_cases h_under : e < minExponent
        · exfalso
          apply hne
          rw [if_pos h_under] at hok
          (try (simp only [Except.ok.injEq] at hok))
          (try (obtain rfl := Except.ok.inj hok))
          (try subst hok)
          rfl
        · rw [if_neg h_under] at hok
          have h_no_ovf : ¬ (e > maxExponent) := by
            intro h_ovf; rw [if_pos h_ovf] at hok
            exact absurd hok (by intro h; cases h)
          rw [if_neg h_no_ovf] at hok
          obtain rfl := Except.ok.inj hok
          refine ⟨h_m1_ge_min, ?_, ?_, ?_⟩
          · change (m + 1).toNat ≤ largeRange.max.toNat
            rw [hm_add1_toNat, hmaxMant_v]; rw [hmaxRep_v] at h_m_lt_maxRep; omega
          · change minExponent ≤ e; push_neg at h_under; exact h_under
          · change (m + 1).toNat > maxRep.toNat → (m + 1).toNat % 10 = 0
            intro h_gt; exfalso; have : (m + 1).toNat ≤ maxRep.toNat := h_m1_le_maxRep; omega
  · rw [Bool.not_eq_true] at h_ru
    rw [show (g.round .downward == 1 || (g.round .downward == 0 && m % 2 == 1)) = false from h_ru] at hok
    simp only [Bool.false_and, if_false, Bool.false_eq_true] at hok
    by_cases h_resc : m < largeRange.min
    · rw [if_pos h_resc] at hok; simp only [] at hok
      have h_m_lt_min : m.toNat < largeRange.min.toNat := UInt64.lt_iff_toNat_lt.mp h_resc
      have h_m_mul10_toNat : (m * 10).toNat = m.toNat * 10 := m_mul_ten_no_overflow h_m_lt_min
      by_cases h_under : e - 1 < minExponent
      · exfalso
        apply hne
        rw [if_pos h_under] at hok
        (try (simp only [Except.ok.injEq] at hok))
        (try (obtain rfl := Except.ok.inj hok))
        (try subst hok)
        rfl
      · rw [if_neg h_under] at hok
        (try (simp only [Except.ok.injEq] at hok))
        have h_no_ovf : ¬ (e - 1 > maxExponent) := by
          intro h_ovf; rw [if_pos h_ovf] at hok
          exact absurd hok (by intro h; cases h)
        rw [if_neg h_no_ovf] at hok
        obtain rfl := Except.ok.inj hok
        refine ⟨?_, ?_, ?_, ?_⟩
        · change largeRange.min.toNat ≤ (m * 10).toNat
          rw [h_m_mul10_toNat, hminMant_v]
          have : m.toNat * 10 ≥ 9223372036854775800 := by omega
          omega
        · change (m * 10).toNat ≤ largeRange.max.toNat
          rw [h_m_mul10_toNat, hmaxMant_v, hminMant_v] at *
          calc m.toNat * 10 ≤ (1000000000000000000 - 1) * 10 := Nat.mul_le_mul_right _ (by omega)
            _ = maxMul10Witness := by norm_num
            _ ≤ 9999999999999999999 := by norm_num
        · change minExponent ≤ e - 1; push_neg at h_under; exact h_under
        · intro _; change (m * 10).toNat % 10 = 0; rw [h_m_mul10_toNat]; omega
    · rw [if_neg h_resc] at hok; simp only [] at hok
      have h_m_ge_min : m.toNat ≥ largeRange.min.toNat := by
        by_contra h; push_neg at h; exact h_resc (UInt64.lt_iff_toNat_lt.mpr h)
      by_cases h_under : e < minExponent
      · exfalso
        apply hne
        rw [if_pos h_under] at hok
        (try (simp only [Except.ok.injEq] at hok))
        (try (obtain rfl := Except.ok.inj hok))
        (try subst hok)
        rfl
      · rw [if_neg h_under] at hok
        (try (simp only [Except.ok.injEq] at hok))
        have h_no_ovf : ¬ (e > maxExponent) := by
          intro h_ovf; rw [if_pos h_ovf] at hok
          exact absurd hok (by intro h; cases h)
        rw [if_neg h_no_ovf] at hok
        obtain rfl := Except.ok.inj hok
        refine ⟨h_m_ge_min, ?_, ?_, ?_⟩
        · change m.toNat ≤ largeRange.max.toNat; rw [hmaxMant_v]; rw [hmaxRep_v] at h_ub; omega
        · change minExponent ≤ e; push_neg at h_under; exact h_under
        · change m.toNat > maxRep.toNat → m.toNat % 10 = 0; intro h_gt; exfalso; omega

/-! ## `.towards_zero` mode

`Guard.round .towards_zero = -1` always, so `roundUp` is always false. -/

/-- `Guard.round g .towards_zero = -1` always. -/
lemma round_towards_zero_eq_neg_one (g : Guard) :
    g.round .towards_zero = -1 := rfl

/-- For `.towards_zero`, the boolean `roundUp` in `doRoundUp` is always `false`. -/
private lemma roundUp_bool_towards_zero_false (g : Guard) (m : UInt64) :
    ((g.round .towards_zero == 1) || ((g.round .towards_zero == 0) && (m % 2 == 1))) = false := by
  rw [round_towards_zero_eq_neg_one]; rfl

/-- For `.towards_zero` the algorithm always truncates:
`result.mantissa * 10^exp = zm * 10^ze'`. -/
theorem doRoundUp_value_towards_zero_truncate
    (g : Guard) (zn : Bool) (zm : UInt64) (ze' : Int)
    (loc : String) (res : RoundResult)
    (hok : g.doRoundUp zn zm ze' largeRange.min largeRange.max .towards_zero loc = .ok res)
    (h_mant_ne : res.mantissa_ ≠ 0) :
    (res.mantissa_.toNat : ℚ) * 10 ^ res.exponent_ = (zm.toNat : ℚ) * 10 ^ ze' := by
  have h_ru_false :
      ((g.round .towards_zero == 1) || ((g.round .towards_zero == 0) && (zm % 2 == 1))) = false :=
    roundUp_bool_towards_zero_false g zm
  have hres_eq : res = Guard.bringIntoRange zn zm ze' largeRange.min := by
    unfold Guard.doRoundUp at hok
    simp only [Guard.doDropDigit] at hok
    rw [h_ru_false] at hok
    simp only [Bool.false_and, if_false, Bool.false_eq_true] at hok
    by_cases h_ovf : (Guard.bringIntoRange zn zm ze' largeRange.min).exponent_ > maxExponent
    · rw [if_pos h_ovf] at hok; exact absurd hok (by intro h; cases h)
    · rw [if_neg h_ovf] at hok; exact (Except.ok.inj hok).symm
  rw [hres_eq]
  have hne' : (Guard.bringIntoRange zn zm ze' largeRange.min).mantissa_ ≠ 0 := hres_eq ▸ h_mant_ne
  unfold Guard.bringIntoRange
  by_cases hresc : zm < largeRange.min
  · rw [if_pos hresc]; simp only []
    by_cases h_under : ze' - 1 < minExponent
    · exfalso; apply hne'; unfold Guard.bringIntoRange; rw [if_pos hresc]; simp only []; rw [if_pos h_under]
    · rw [if_neg h_under]
      have hzm_mul_10 : (zm * 10).toNat = zm.toNat * 10 :=
        m_mul_ten_no_overflow (UInt64.lt_iff_toNat_lt.mp hresc)
      change ((zm * 10).toNat : ℚ) * 10 ^ (ze' - 1) = (zm.toNat : ℚ) * 10 ^ ze'
      rw [hzm_mul_10]; push_cast
      rw [show (ze' - 1 : ℤ) = ze' + (-1) from by ring,
          zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0), zpow_neg_one]; field_simp
  · rw [if_neg hresc]; simp only []
    by_cases h_under : ze' < minExponent
    · exfalso; apply hne'; unfold Guard.bringIntoRange; rw [if_neg hresc]; simp only []; rw [if_pos h_under]
    · rw [if_neg h_under]

/-- `.towards_zero` analogue of `doRoundUp_output_invariants`. -/
lemma doRoundUp_output_invariants_towards_zero
    (g : Guard) (neg : Bool) (m : UInt64) (e : Int)
    (h_lb : mantissaFloor ≤ m.toNat)
    (h_ub : m.toNat ≤ maxRep.toNat)
    (loc : String) (res : RoundResult)
    (hok : g.doRoundUp neg m e largeRange.min largeRange.max .towards_zero loc = .ok res)
    (hne : res.mantissa_ ≠ 0) :
    largeRange.min.toNat ≤ res.mantissa_.toNat ∧
    res.mantissa_.toNat ≤ largeRange.max.toNat ∧
    minExponent ≤ res.exponent_ ∧
    (res.mantissa_.toNat > maxRep.toNat → res.mantissa_.toNat % 10 = 0) := by
  have hmaxRep_v : maxRep.toNat = maxRepNat := maxRep_val
  have hminMant_v : largeRange.min.toNat = 1000000000000000000 := largeRange_min_val
  have hmaxMant_v : largeRange.max.toNat = 9999999999999999999 := largeRange_max_val
  have h_ru_false :
      (g.round .towards_zero == 1 || (g.round .towards_zero == 0 && m % 2 == 1)) = false :=
    roundUp_bool_towards_zero_false g m
  unfold Guard.doRoundUp Guard.bringIntoRange at hok
  simp only [Guard.doDropDigit] at hok
  rw [h_ru_false] at hok
  simp only [Bool.false_and, if_false, Bool.false_eq_true] at hok
  by_cases h_resc : m < largeRange.min
  · rw [if_pos h_resc] at hok; simp only [] at hok
    have h_m_lt_min : m.toNat < largeRange.min.toNat := UInt64.lt_iff_toNat_lt.mp h_resc
    have h_m_mul10_toNat : (m * 10).toNat = m.toNat * 10 := m_mul_ten_no_overflow h_m_lt_min
    by_cases h_under : e - 1 < minExponent
    · exfalso
      apply hne
      rw [if_pos h_under] at hok
      (try (simp only [Except.ok.injEq] at hok))
      (try (obtain rfl := Except.ok.inj hok))
      (try subst hok)
      rfl
    · rw [if_neg h_under] at hok
      have h_no_ovf : ¬ (e - 1 > maxExponent) := by
        intro h_ovf; rw [if_pos h_ovf] at hok
        exact absurd hok (by intro h; cases h)
      rw [if_neg h_no_ovf] at hok
      obtain rfl := Except.ok.inj hok
      refine ⟨?_, ?_, ?_, ?_⟩
      · change largeRange.min.toNat ≤ (m * 10).toNat
        rw [h_m_mul10_toNat, hminMant_v]
        have : m.toNat * 10 ≥ 9223372036854775800 := by omega
        omega
      · change (m * 10).toNat ≤ largeRange.max.toNat
        rw [h_m_mul10_toNat, hmaxMant_v, hminMant_v] at *
        calc m.toNat * 10 ≤ (1000000000000000000 - 1) * 10 := Nat.mul_le_mul_right _ (by omega)
          _ = maxMul10Witness := by norm_num
          _ ≤ 9999999999999999999 := by norm_num
      · change minExponent ≤ e - 1; push_neg at h_under; exact h_under
      · intro _; change (m * 10).toNat % 10 = 0; rw [h_m_mul10_toNat]; omega
  · rw [if_neg h_resc] at hok; simp only [] at hok
    have h_m_ge_min : m.toNat ≥ largeRange.min.toNat := by
      by_contra h; push_neg at h; exact h_resc (UInt64.lt_iff_toNat_lt.mpr h)
    by_cases h_under : e < minExponent
    · exfalso
      apply hne
      rw [if_pos h_under] at hok
      (try (simp only [Except.ok.injEq] at hok))
      (try (obtain rfl := Except.ok.inj hok))
      (try subst hok)
      rfl
    · rw [if_neg h_under] at hok
      have h_no_ovf : ¬ (e > maxExponent) := by
        intro h_ovf; rw [if_pos h_ovf] at hok
        exact absurd hok (by intro h; cases h)
      rw [if_neg h_no_ovf] at hok
      obtain rfl := Except.ok.inj hok
      refine ⟨h_m_ge_min, ?_, ?_, ?_⟩
      · change m.toNat ≤ largeRange.max.toNat; rw [hmaxMant_v]; rw [hmaxRep_v] at h_ub; omega
      · change minExponent ≤ e; push_neg at h_under; exact h_under
      · change m.toNat > maxRep.toNat → m.toNat % 10 = 0; intro h_gt; exfalso; omega

/-! ## `.upward` mode -/

/-- For `.upward`, round-up fires iff `sbit_ = false` and guard content is nonzero. -/
def Guard.shouldRoundUp_upward (g : Guard) : Prop :=
  g.sbit_ = false ∧ (g.digits_ > 0 ∨ g.xbit_ = true)

/-- When `.upward` round-up fires, the represented fraction is strictly positive:
the guard content is nonzero (`digits_ > 0` gives `decimalValue > 0`, or `xbit_`
gives a positive hidden tail), so `f > 0`. -/
lemma represents_pos_of_shouldRoundUp_upward (g : Guard) (f : ℚ)
    (hf_rep : represents g f) (h_sru : g.shouldRoundUp_upward) : 0 < f := by
  obtain ⟨x, hx_nn, _, hf_eq, hxbit_iff, _⟩ := hf_rep
  obtain ⟨_hsbit, hcont⟩ := h_sru
  have hdv_nn : (0 : ℚ) ≤ (decimalValue g.digits_ : ℚ) / 10 ^ 16 := by positivity
  rcases hcont with hdig | hxb
  · have hd_ne : g.digits_ ≠ 0 := by
      intro hc; rw [hc] at hdig; exact absurd hdig (by decide)
    have hdv_pos : 0 < decimalValue g.digits_ := decimalValue_pos_of_ne_zero g.digits_ hd_ne
    have hdv_q : (0 : ℚ) < (decimalValue g.digits_ : ℚ) / 10 ^ 16 := by
      apply div_pos _ (by positivity)
      exact_mod_cast hdv_pos
    rw [hf_eq]; linarith
  · have hx_pos : x > 0 := hxbit_iff.mp hxb
    rw [hf_eq]; linarith

/-- When `.downward` round-up fires (on a negative result, rounding the magnitude
up), the represented fraction is strictly positive: the guard content is nonzero
(`digits_ > 0` gives `decimalValue > 0`, or `xbit_` gives a positive hidden tail),
so `f > 0`. -/
lemma represents_pos_of_shouldRoundUp_downward (g : Guard) (f : ℚ)
    (hf_rep : represents g f) (h_sru : g.shouldRoundUp_downward) : 0 < f := by
  obtain ⟨x, hx_nn, _, hf_eq, hxbit_iff, _⟩ := hf_rep
  obtain ⟨_hsbit, hcont⟩ := h_sru
  have hdv_nn : (0 : ℚ) ≤ (decimalValue g.digits_ : ℚ) / 10 ^ 16 := by positivity
  rcases hcont with hdig | hxb
  · have hd_ne : g.digits_ ≠ 0 := by
      intro hc; rw [hc] at hdig; exact absurd hdig (by decide)
    have hdv_pos : 0 < decimalValue g.digits_ := decimalValue_pos_of_ne_zero g.digits_ hd_ne
    have hdv_q : (0 : ℚ) < (decimalValue g.digits_ : ℚ) / 10 ^ 16 := by
      apply div_pos _ (by positivity)
      exact_mod_cast hdv_pos
    rw [hf_eq]; linarith
  · have hx_pos : x > 0 := hxbit_iff.mp hxb
    rw [hf_eq]; linarith

/-- `g.round .upward = 1` iff `g.shouldRoundUp_upward`. -/
lemma round_upward_eq_one_iff (g : Guard) :
    g.round .upward = 1 ↔ g.shouldRoundUp_upward := by
  constructor
  · intro h
    by_cases hs : g.sbit_ = true
    · exfalso
      have : g.round .upward = -1 := by
        change (if g.sbit_ then (-1 : Int)
            else if (decide (g.digits_ > 0) || g.xbit_) = true then 1 else -1) = -1
        rw [hs]; simp
      rw [this] at h; exact absurd h (by decide)
    · have hs_false : g.sbit_ = false := Bool.not_eq_true _ |>.mp hs
      refine ⟨hs_false, ?_⟩
      by_cases hcond_d : g.digits_ > 0
      · exact Or.inl hcond_d
      · by_cases hcond_x : g.xbit_ = true
        · exact Or.inr hcond_x
        · exfalso
          have hd_false : decide (g.digits_ > 0) = false := decide_eq_false hcond_d
          have hx_false : g.xbit_ = false := Bool.not_eq_true _ |>.mp hcond_x
          have h_bool_false : (decide (g.digits_ > 0) || g.xbit_) = false := by
            rw [hd_false, hx_false]; rfl
          have : g.round .upward = -1 := by
            change (if g.sbit_ then (-1 : Int)
                else if (decide (g.digits_ > 0) || g.xbit_) = true then 1 else -1) = -1
            rw [hs_false]; simp only [Bool.false_eq_true, if_false]
            rw [h_bool_false]; simp only [Bool.false_eq_true, if_false]
          rw [this] at h; exact absurd h (by decide)
  · intro ⟨hs, hor⟩
    have h_bool_true : (decide (g.digits_ > 0) || g.xbit_) = true := by
      rcases hor with h1 | h2
      · rw [decide_eq_true h1]; rfl
      · rw [h2]; rw [Bool.or_true]
    change (if g.sbit_ then (-1 : Int)
        else if (decide (g.digits_ > 0) || g.xbit_) = true then 1 else -1) = 1
    rw [hs]; simp only [Bool.false_eq_true, if_false]
    rw [h_bool_true]; simp only [if_true]

/-- `g.round .upward = -1` iff `¬ g.shouldRoundUp_upward`. -/
lemma round_upward_eq_neg_one_iff (g : Guard) :
    g.round .upward = -1 ↔ ¬ g.shouldRoundUp_upward := by
  have hvals : g.round .upward = 1 ∨ g.round .upward = -1 := by
    unfold Guard.round
    split_ifs <;> first | (left; rfl) | (right; rfl)
  constructor
  · intro h h_ru
    have h1 : g.round .upward = 1 := (round_upward_eq_one_iff g).mpr h_ru
    rw [h] at h1; exact absurd h1 (by decide)
  · intro h_no
    rcases hvals with h1 | hm1
    · exact absurd ((round_upward_eq_one_iff g).mp h1) h_no
    · exact hm1

/-- For `.upward`, the boolean `roundUp` in `doRoundUp` equals `false`
when `shouldRoundUp_upward` is false. -/
private lemma roundUp_bool_upward_false (g : Guard) (m : UInt64)
    (h_no : ¬ g.shouldRoundUp_upward) :
    ((g.round .upward == 1) || ((g.round .upward == 0) && (m % 2 == 1))) = false := by
  have h_neg1 : g.round .upward = -1 := (round_upward_eq_neg_one_iff g).mpr h_no
  rw [h_neg1]; rfl

/-- For `.upward`, the boolean `roundUp` in `doRoundUp` equals `true`
when `shouldRoundUp_upward` is true. -/
private lemma roundUp_bool_upward_true (g : Guard) (m : UInt64)
    (h_yes : g.shouldRoundUp_upward) :
    ((g.round .upward == 1) || ((g.round .upward == 0) && (m % 2 == 1))) = true := by
  have h1 : g.round .upward = 1 := (round_upward_eq_one_iff g).mpr h_yes
  rw [h1]; rfl

/-- The no-round-up case for `.upward`: `result.mantissa * 10^exp = zm * 10^ze'`. -/
theorem doRoundUp_value_upward_truncate
    (g : Guard) (zn : Bool) (zm : UInt64) (ze' : Int)
    (h_no_roundUp : ¬ g.shouldRoundUp_upward)
    (loc : String) (res : RoundResult)
    (hok : g.doRoundUp zn zm ze' largeRange.min largeRange.max .upward loc = .ok res)
    (h_mant_ne : res.mantissa_ ≠ 0) :
    (res.mantissa_.toNat : ℚ) * 10 ^ res.exponent_ = (zm.toNat : ℚ) * 10 ^ ze' := by
  have h_ru_false :
      ((g.round .upward == 1) || ((g.round .upward == 0) && (zm % 2 == 1))) = false :=
    roundUp_bool_upward_false g zm h_no_roundUp
  have hres_eq : res = Guard.bringIntoRange zn zm ze' largeRange.min := by
    unfold Guard.doRoundUp at hok; simp only [Guard.doDropDigit] at hok
    rw [h_ru_false] at hok; simp only [Bool.false_and, if_false, Bool.false_eq_true] at hok
    by_cases h_ovf : (Guard.bringIntoRange zn zm ze' largeRange.min).exponent_ > maxExponent
    · rw [if_pos h_ovf] at hok; exact absurd hok (by intro h; cases h)
    · rw [if_neg h_ovf] at hok; exact (Except.ok.inj hok).symm
  rw [hres_eq]
  have hne' : (Guard.bringIntoRange zn zm ze' largeRange.min).mantissa_ ≠ 0 := hres_eq ▸ h_mant_ne
  unfold Guard.bringIntoRange
  by_cases hresc : zm < largeRange.min
  · rw [if_pos hresc]; simp only []
    by_cases h_under : ze' - 1 < minExponent
    · exfalso; apply hne'; unfold Guard.bringIntoRange; rw [if_pos hresc]; simp only []; rw [if_pos h_under]
    · rw [if_neg h_under]
      have hzm_mul_10 : (zm * 10).toNat = zm.toNat * 10 :=
        m_mul_ten_no_overflow (UInt64.lt_iff_toNat_lt.mp hresc)
      change ((zm * 10).toNat : ℚ) * 10 ^ (ze' - 1) = (zm.toNat : ℚ) * 10 ^ ze'
      rw [hzm_mul_10]; push_cast
      rw [show (ze' - 1 : ℤ) = ze' + (-1) from by ring,
          zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0), zpow_neg_one]; field_simp
  · rw [if_neg hresc]; simp only []
    by_cases h_under : ze' < minExponent
    · exfalso; apply hne'; unfold Guard.bringIntoRange; rw [if_neg hresc]; simp only []; rw [if_pos h_under]
    · rw [if_neg h_under]

/-- The round-up no-cusp case for `.upward`. -/
theorem doRoundUp_value_upward_roundUp_noCusp
    (g : Guard) (zn : Bool) (zm : UInt64) (ze' : Int)
    (h_roundUp : g.shouldRoundUp_upward)
    (h_no_cusp : zm.toNat + 1 ≤ maxRep.toNat)
    (loc : String) (res : RoundResult)
    (hok : g.doRoundUp zn zm ze' largeRange.min largeRange.max .upward loc = .ok res)
    (h_mant_ne : res.mantissa_ ≠ 0) :
    (res.mantissa_.toNat : ℚ) * 10 ^ res.exponent_ = ((zm.toNat : ℚ) + 1) * 10 ^ ze' := by
  have h_ru_true :
      ((g.round .upward == 1) || ((g.round .upward == 0) && (zm % 2 == 1))) = true :=
    roundUp_bool_upward_true g zm h_roundUp
  have h_m_lt_maxRep : zm.toNat < maxRep.toNat := by rw [maxRep_val] at h_no_cusp ⊢; omega
  have h_cusp_cond_false : ((zm ≥ largeRange.max) || (zm ≥ maxRep)) = false := by
    rw [Bool.or_eq_false_iff]
    refine ⟨decide_eq_false ?_, decide_eq_false ?_⟩
    · intro h; have := UInt64.le_iff_toNat_le.mp h
      rw [largeRange_max_val] at this; rw [maxRep_val] at h_m_lt_maxRep; omega
    · intro h; have := UInt64.le_iff_toNat_le.mp h; omega
  have hres_eq : res = Guard.bringIntoRange zn (zm + 1) ze' largeRange.min := by
    unfold Guard.doRoundUp at hok; simp only [Guard.doDropDigit] at hok
    rw [h_ru_true, h_cusp_cond_false] at hok
    simp only [Bool.and_false, if_false, Bool.false_eq_true, if_true] at hok
    by_cases h_ovf : (Guard.bringIntoRange zn (zm + 1) ze' largeRange.min).exponent_ > maxExponent
    · rw [if_pos h_ovf] at hok; exact absurd hok (by intro h; cases h)
    · rw [if_neg h_ovf] at hok; exact (Except.ok.inj hok).symm
  rw [hres_eq]
  have hne' : (Guard.bringIntoRange zn (zm + 1) ze' largeRange.min).mantissa_ ≠ 0 := hres_eq ▸ h_mant_ne
  have h_m_le_maxRep : zm.toNat ≤ maxRep.toNat := by omega
  have hm_add1_toNat : (zm + 1).toNat = zm.toNat + 1 := m_add_one_no_overflow h_m_le_maxRep
  unfold Guard.bringIntoRange
  by_cases hresc : zm + 1 < largeRange.min
  · rw [if_pos hresc]; simp only []
    have h_m1_lt_min : (zm + 1).toNat < largeRange.min.toNat := UInt64.lt_iff_toNat_lt.mp hresc
    have h_m1_mul10_toNat : ((zm + 1) * 10).toNat = (zm + 1).toNat * 10 := by
      rw [UInt64.toNat_mul]; have h10u : (10 : UInt64).toNat = 10 := rfl; rw [h10u]
      apply Nat.mod_eq_of_lt
      rw [largeRange_min_val] at h_m1_lt_min
      calc (zm + 1).toNat * 10 < 1000000000000000000 * 10 := Nat.mul_lt_mul_of_pos_right h_m1_lt_min (by norm_num)
        _ < 2 ^ 64 := by norm_num
    by_cases h_under : ze' - 1 < minExponent
    · exfalso; apply hne'; unfold Guard.bringIntoRange; rw [if_pos hresc]; simp only []; rw [if_pos h_under]
    · rw [if_neg h_under]
      change ((((zm + 1) * 10)).toNat : ℚ) * 10 ^ (ze' - 1) = ((zm.toNat : ℚ) + 1) * 10 ^ ze'
      rw [h_m1_mul10_toNat, hm_add1_toNat]; push_cast
      rw [show (ze' - 1 : ℤ) = ze' + (-1) from by ring,
          zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0), zpow_neg_one]; field_simp
  · rw [if_neg hresc]; simp only []
    by_cases h_under : ze' < minExponent
    · exfalso; apply hne'; unfold Guard.bringIntoRange; rw [if_neg hresc]; simp only []; rw [if_pos h_under]
    · rw [if_neg h_under]
      change ((zm + 1).toNat : ℚ) * 10 ^ ze' = ((zm.toNat : ℚ) + 1) * 10 ^ ze'
      rw [hm_add1_toNat]; push_cast; ring

/-- `.upward` cusp case (`zm = maxRep`): output is `maxRepCuspTarget * 10^ze'`. -/
theorem doRoundUp_value_upward_roundUp_cusp
    (g : Guard) (zn : Bool) (zm : UInt64) (ze' : Int)
    (h_zm_eq_maxRep : zm = maxRep)
    (h_roundUp : g.shouldRoundUp_upward)
    (loc : String) (res : RoundResult)
    (hok : g.doRoundUp zn zm ze' largeRange.min largeRange.max .upward loc = .ok res)
    (h_mant_ne : res.mantissa_ ≠ 0) :
    (res.mantissa_.toNat : ℚ) * 10 ^ res.exponent_ = maxRepCuspTarget * 10 ^ ze' := by
  have h_ru_true :
      ((g.round .upward == 1) || ((g.round .upward == 0) && (zm % 2 == 1))) = true :=
    roundUp_bool_upward_true g zm h_roundUp
  have h_cusp_cond_true : ((zm ≥ largeRange.max) || (zm ≥ maxRep)) = true := by
    rw [Bool.or_eq_true]; right; rw [h_zm_eq_maxRep]; decide
  set g' := g.push (zm % 10) with hg'_def
  have h_g_sbit : g.sbit_ = false := h_roundUp.1
  have h_g'_sbit : g'.sbit_ = false := by rw [hg'_def]; unfold Guard.push; exact h_g_sbit
  have h_g'_digits_toNat : g'.digits_.toNat = g.digits_.toNat / 16 + (zm.toNat % 10 % 16) * 2 ^ 60 := by
    rw [hg'_def]; have := toNat_push_digits g (zm % 10)
    rw [UInt64.toNat_mod] at this; have h10 : (10 : UInt64).toNat = 10 := rfl; rw [h10] at this; exact this
  have h_zm_mod10_mod16 : zm.toNat % 10 % 16 = 7 := by rw [show zm.toNat % 10 = 7 from by rw [h_zm_eq_maxRep]; decide]
  have h_g'_digits_gt : g'.digits_ > 0 := by
    change (0 : UInt64) < g'.digits_; rw [UInt64.lt_iff_toNat_lt]
    rw [show (0 : UInt64).toNat = 0 from rfl, h_g'_digits_toNat, h_zm_mod10_mod16]; omega
  have h_g'_round : g'.round .upward = 1 :=
    (round_upward_eq_one_iff g').mpr ⟨h_g'_sbit, Or.inl h_g'_digits_gt⟩
  have h_ru'_true :
      ((g'.round .upward == 1) || ((g'.round .upward == 0) && ((zm / 10) % 2 == 1))) = true := by
    rw [h_g'_round]; rfl
  have h_m_div10_toNat : (zm / 10).toNat = mantissaFloor := by rw [UInt64.toNat_div, h_zm_eq_maxRep]; decide
  have h_m_div10_le_maxRep : (zm / 10).toNat ≤ maxRep.toNat := by rw [h_m_div10_toNat, maxRep_val]; norm_num
  have h_m1_toNat : (zm / 10 + 1).toNat = mantissaFloorSucc := by
    rw [m_add_one_no_overflow h_m_div10_le_maxRep, h_m_div10_toNat]
  have h_m1_lt_min : (zm / 10 + 1) < largeRange.min := by
    rw [UInt64.lt_iff_toNat_lt, h_m1_toNat, largeRange_min_val]; norm_num
  have h_m1_mul10_toNat : ((zm / 10 + 1) * 10).toNat = maxRepCuspTarget := by
    rw [UInt64.toNat_mul]; have h10 : (10 : UInt64).toNat = 10 := rfl; rw [h10, h_m1_toNat]
  have hres_eq : res = Guard.bringIntoRange zn (zm / 10 + 1) (ze' + 1) largeRange.min := by
    unfold Guard.doRoundUp at hok; simp only [Guard.doDropDigit] at hok
    rw [h_ru_true, h_cusp_cond_true] at hok
    simp only [Bool.true_and, if_true] at hok
    rw [h_ru'_true] at hok; simp only [if_true, if_pos h_m1_lt_min] at hok
    by_cases h_ovf : (Guard.bringIntoRange zn (zm / 10 + 1) (ze' + 1) largeRange.min).exponent_ > maxExponent
    · rw [if_pos h_ovf] at hok; exact absurd hok (by intro h; cases h)
    · rw [if_neg h_ovf] at hok; exact (Except.ok.inj hok).symm
  rw [hres_eq]
  have hne' : (Guard.bringIntoRange zn (zm / 10 + 1) (ze' + 1) largeRange.min).mantissa_ ≠ 0 := hres_eq ▸ h_mant_ne
  simp only [Guard.bringIntoRange, if_pos h_m1_lt_min]
  by_cases h_under : ze' + 1 - 1 < minExponent
  · exfalso; apply hne'; simp only [Guard.bringIntoRange, if_pos h_m1_lt_min, if_pos h_under]
  · rw [if_neg h_under]
    change (((zm / 10 + 1) * 10).toNat : ℚ) * 10 ^ (ze' + 1 - 1) = maxRepCuspTarget * 10 ^ ze'
    rw [h_m1_mul10_toNat]; rw [show (ze' + 1 - 1 : ℤ) = ze' from by ring]; push_cast; ring

/-- `.upward`-mode analogue of `doRoundUp_output_invariants`. -/
lemma doRoundUp_output_invariants_upward
    (g : Guard) (neg : Bool) (m : UInt64) (e : Int)
    (h_lb : mantissaFloor ≤ m.toNat)
    (h_ub : m.toNat ≤ maxRep.toNat)
    (loc : String) (res : RoundResult)
    (hok : g.doRoundUp neg m e largeRange.min largeRange.max .upward loc = .ok res)
    (hne : res.mantissa_ ≠ 0) :
    largeRange.min.toNat ≤ res.mantissa_.toNat ∧
    res.mantissa_.toNat ≤ largeRange.max.toNat ∧
    minExponent ≤ res.exponent_ ∧
    (res.mantissa_.toNat > maxRep.toNat → res.mantissa_.toNat % 10 = 0) := by
  have hmaxRep_v : maxRep.toNat = maxRepNat := maxRep_val
  have hminMant_v : largeRange.min.toNat = 1000000000000000000 := largeRange_min_val
  have hmaxMant_v : largeRange.max.toNat = 9999999999999999999 := largeRange_max_val
  have hm_add1_toNat : (m + 1).toNat = m.toNat + 1 := m_add_one_no_overflow h_ub
  have h_m_not_ge_maxMant : ¬ (m ≥ largeRange.max) := by
    intro h; have := UInt64.le_iff_toNat_le.mp h
    rw [hmaxMant_v] at this; rw [hmaxRep_v] at h_ub; omega
  have h_ge_maxMant_false : decide (m ≥ largeRange.max) = false :=
    decide_eq_false h_m_not_ge_maxMant
  unfold Guard.doRoundUp Guard.bringIntoRange at hok
  simp only [Guard.doDropDigit] at hok
  -- Same case analysis as downward but for .upward mode
  by_cases h_ru : (g.round .upward == 1 || (g.round .upward == 0 && m % 2 == 1)) = true
  · rw [show (g.round .upward == 1 || (g.round .upward == 0 && m % 2 == 1)) = true from h_ru] at hok
    by_cases h_m_eq_maxRep : m = maxRep
    · -- cusp
      have h_cusp_cond_true : ((m ≥ largeRange.max) || (m ≥ maxRep)) = true := by
        rw [Bool.or_eq_true]; right; rw [h_m_eq_maxRep]; decide
      rw [h_cusp_cond_true] at hok; simp only [Bool.and_self, if_true] at hok
      have h_m_div10_toNat : (m / 10).toNat = mantissaFloor := by rw [UInt64.toNat_div, h_m_eq_maxRep]; decide
      set g' := g.push (m % 10) with hg'_def
      have h_g_round_one : g.round .upward = 1 := by
        rcases Bool.or_eq_true _ _ |>.mp h_ru with h1 | h2
        · exact beq_iff_eq.mp h1
        · exfalso; rcases Bool.and_eq_true _ _ |>.mp h2 with ⟨hr0, _⟩
          have h_eq_0 : g.round .upward = 0 := beq_iff_eq.mp hr0
          have hvals : g.round .upward = 1 ∨ g.round .upward = -1 := by
            unfold Guard.round; split_ifs <;> first | (left; rfl) | (right; rfl)
          rcases hvals with h1 | hm1
          · rw [h_eq_0] at h1; exact absurd h1 (by decide)
          · rw [h_eq_0] at hm1; exact absurd hm1 (by decide)
      have h_g_sbit : g.sbit_ = false := ((round_upward_eq_one_iff g).mp h_g_round_one).1
      have h_g'_sbit : g'.sbit_ = false := by rw [hg'_def]; unfold Guard.push; exact h_g_sbit
      have h_g'_digits_toNat : g'.digits_.toNat = g.digits_.toNat / 16 + (m.toNat % 10 % 16) * 2 ^ 60 := by
        rw [hg'_def]; have := toNat_push_digits g (m % 10)
        rw [UInt64.toNat_mod] at this; have h10 : (10 : UInt64).toNat = 10 := rfl; rw [h10] at this; exact this
      have h_m_mod10_mod16 : m.toNat % 10 % 16 = 7 := by rw [show m.toNat % 10 = 7 from by rw [h_m_eq_maxRep]; decide]
      have h_g'_digits_gt : g'.digits_ > 0 := by
        change (0 : UInt64) < g'.digits_; rw [UInt64.lt_iff_toNat_lt]
        rw [show (0 : UInt64).toNat = 0 from rfl, h_g'_digits_toNat, h_m_mod10_mod16]; omega
      have h_g'_round : g'.round .upward = 1 :=
        (round_upward_eq_one_iff g').mpr ⟨h_g'_sbit, Or.inl h_g'_digits_gt⟩
      have h_ru'_true :
          (g'.round .upward == 1 || (g'.round .upward == 0 && (m / 10) % 2 == 1)) = true := by
        rw [h_g'_round]; rfl
      rw [h_ru'_true] at hok; simp only [if_true] at hok
      have h_m_div10_le_maxRep : (m / 10).toNat ≤ maxRep.toNat := by rw [h_m_div10_toNat, maxRep_val]; norm_num
      have h_m1_toNat : (m / 10 + 1).toNat = mantissaFloorSucc := by
        rw [m_add_one_no_overflow h_m_div10_le_maxRep, h_m_div10_toNat]
      have h_m1_lt_min : (m / 10 + 1) < largeRange.min := by
        rw [UInt64.lt_iff_toNat_lt, h_m1_toNat, largeRange_min_val]; norm_num
      rw [if_pos h_m1_lt_min] at hok; simp only [] at hok
      have h_m1_mul10_toNat : ((m / 10 + 1) * 10).toNat = maxRepCuspTarget := by
        rw [UInt64.toNat_mul]; have h10 : (10 : UInt64).toNat = 10 := rfl; rw [h10, h_m1_toNat]
      by_cases h_under : e + 1 - 1 < minExponent
      · exfalso; apply hne
        rw [if_pos h_under] at hok
        (try (simp only [Except.ok.injEq] at hok))
        (try (obtain rfl := Except.ok.inj hok))
        (try subst hok)
        rfl
      · rw [if_neg h_under] at hok
        have h_no_ovf : ¬ (e + 1 - 1 > maxExponent) := by
          intro h_ovf; rw [if_pos h_ovf] at hok
          exact absurd hok (by intro h; cases h)
        rw [if_neg h_no_ovf] at hok
        obtain rfl := Except.ok.inj hok
        refine ⟨?_, ?_, ?_, ?_⟩
        · change largeRange.min.toNat ≤ ((m / 10 + 1) * 10).toNat; rw [h_m1_mul10_toNat, hminMant_v]; norm_num
        · change ((m / 10 + 1) * 10).toNat ≤ largeRange.max.toNat; rw [h_m1_mul10_toNat, hmaxMant_v]; norm_num
        · change minExponent ≤ e + 1 - 1; push_neg at h_under; exact h_under
        · intro _; change ((m / 10 + 1) * 10).toNat % 10 = 0; rw [h_m1_mul10_toNat]
    · -- non-cusp
      have h_m_lt_maxRep : m.toNat < maxRep.toNat := by
        have : m.toNat ≠ maxRep.toNat := fun heq => h_m_eq_maxRep (UInt64.toNat_inj.mp heq); omega
      have h_cusp_cond_false : ((m ≥ largeRange.max) || (m ≥ maxRep)) = false := by
        rw [Bool.or_eq_false_iff]
        exact ⟨h_ge_maxMant_false, decide_eq_false (by intro h; have := UInt64.le_iff_toNat_le.mp h; omega)⟩
      rw [h_cusp_cond_false] at hok
      simp only [Bool.and_false, if_false, Bool.false_eq_true, if_true] at hok
      have h_m1_le_maxRep : (m + 1).toNat ≤ maxRep.toNat := by rw [hm_add1_toNat]; omega
      by_cases h_resc : m + 1 < largeRange.min
      · rw [if_pos h_resc] at hok; simp only [] at hok
        have h_m1_lt_min : (m + 1).toNat < largeRange.min.toNat := UInt64.lt_iff_toNat_lt.mp h_resc
        have h_m1_mul10_toNat : ((m + 1) * 10).toNat = (m + 1).toNat * 10 := by
          rw [UInt64.toNat_mul]; have h10 : (10 : UInt64).toNat = 10 := rfl; rw [h10]
          apply Nat.mod_eq_of_lt; rw [hminMant_v] at h_m1_lt_min
          calc (m + 1).toNat * 10 < 1000000000000000000 * 10 := Nat.mul_lt_mul_of_pos_right h_m1_lt_min (by norm_num)
            _ < 2 ^ 64 := by norm_num
        by_cases h_under : e - 1 < minExponent
        · exfalso
          apply hne
          rw [if_pos h_under] at hok
          (try (simp only [Except.ok.injEq] at hok))
          (try (obtain rfl := Except.ok.inj hok))
          (try subst hok)
          rfl
        · rw [if_neg h_under] at hok
          have h_no_ovf : ¬ (e - 1 > maxExponent) := by
            intro h_ovf; rw [if_pos h_ovf] at hok
            exact absurd hok (by intro h; cases h)
          rw [if_neg h_no_ovf] at hok
          obtain rfl := Except.ok.inj hok
          refine ⟨?_, ?_, ?_, ?_⟩
          · change largeRange.min.toNat ≤ ((m + 1) * 10).toNat
            rw [h_m1_mul10_toNat, hm_add1_toNat, hminMant_v]
            have : (m.toNat + 1) * 10 ≥ maxRepCuspTarget := by omega
            omega
          · change ((m + 1) * 10).toNat ≤ largeRange.max.toNat
            rw [h_m1_mul10_toNat, hmaxMant_v, hminMant_v] at *
            calc (m + 1).toNat * 10 ≤ (1000000000000000000 - 1) * 10 := Nat.mul_le_mul_right _ (by omega)
              _ = maxMul10Witness := by norm_num
              _ ≤ 9999999999999999999 := by norm_num
          · change minExponent ≤ e - 1; push_neg at h_under; exact h_under
          · intro _; change ((m + 1) * 10).toNat % 10 = 0; rw [h_m1_mul10_toNat]; omega
      · rw [if_neg h_resc] at hok; simp only [] at hok
        have h_m1_ge_min : (m + 1).toNat ≥ largeRange.min.toNat := by
          by_contra h; push_neg at h; exact h_resc (UInt64.lt_iff_toNat_lt.mpr h)
        by_cases h_under : e < minExponent
        · exfalso
          apply hne
          rw [if_pos h_under] at hok
          (try (simp only [Except.ok.injEq] at hok))
          (try (obtain rfl := Except.ok.inj hok))
          (try subst hok)
          rfl
        · rw [if_neg h_under] at hok
          have h_no_ovf : ¬ (e > maxExponent) := by
            intro h_ovf; rw [if_pos h_ovf] at hok
            exact absurd hok (by intro h; cases h)
          rw [if_neg h_no_ovf] at hok
          obtain rfl := Except.ok.inj hok
          refine ⟨h_m1_ge_min, ?_, ?_, ?_⟩
          · change (m + 1).toNat ≤ largeRange.max.toNat; rw [hm_add1_toNat, hmaxMant_v]; rw [hmaxRep_v] at h_m_lt_maxRep; omega
          · change minExponent ≤ e; push_neg at h_under; exact h_under
          · change (m + 1).toNat > maxRep.toNat → (m + 1).toNat % 10 = 0
            intro h_gt; exfalso; have : (m + 1).toNat ≤ maxRep.toNat := h_m1_le_maxRep; omega
  · rw [Bool.not_eq_true] at h_ru
    rw [show (g.round .upward == 1 || (g.round .upward == 0 && m % 2 == 1)) = false from h_ru] at hok
    simp only [Bool.false_and, if_false, Bool.false_eq_true] at hok
    by_cases h_resc : m < largeRange.min
    · rw [if_pos h_resc] at hok; simp only [] at hok
      have h_m_lt_min : m.toNat < largeRange.min.toNat := UInt64.lt_iff_toNat_lt.mp h_resc
      have h_m_mul10_toNat : (m * 10).toNat = m.toNat * 10 := m_mul_ten_no_overflow h_m_lt_min
      by_cases h_under : e - 1 < minExponent
      · exfalso
        apply hne
        rw [if_pos h_under] at hok
        (try (simp only [Except.ok.injEq] at hok))
        (try (obtain rfl := Except.ok.inj hok))
        (try subst hok)
        rfl
      · rw [if_neg h_under] at hok
        (try (simp only [Except.ok.injEq] at hok))
        have h_no_ovf : ¬ (e - 1 > maxExponent) := by
          intro h_ovf; rw [if_pos h_ovf] at hok
          exact absurd hok (by intro h; cases h)
        rw [if_neg h_no_ovf] at hok
        obtain rfl := Except.ok.inj hok
        refine ⟨?_, ?_, ?_, ?_⟩
        · change largeRange.min.toNat ≤ (m * 10).toNat
          rw [h_m_mul10_toNat, hminMant_v]
          have : m.toNat * 10 ≥ 9223372036854775800 := by omega
          omega
        · change (m * 10).toNat ≤ largeRange.max.toNat
          rw [h_m_mul10_toNat, hmaxMant_v, hminMant_v] at *
          calc m.toNat * 10 ≤ (1000000000000000000 - 1) * 10 := Nat.mul_le_mul_right _ (by omega)
            _ = maxMul10Witness := by norm_num
            _ ≤ 9999999999999999999 := by norm_num
        · change minExponent ≤ e - 1; push_neg at h_under; exact h_under
        · intro _; change (m * 10).toNat % 10 = 0; rw [h_m_mul10_toNat]; omega
    · rw [if_neg h_resc] at hok; simp only [] at hok
      have h_m_ge_min : m.toNat ≥ largeRange.min.toNat := by
        by_contra h; push_neg at h; exact h_resc (UInt64.lt_iff_toNat_lt.mpr h)
      by_cases h_under : e < minExponent
      · exfalso
        apply hne
        rw [if_pos h_under] at hok
        (try (simp only [Except.ok.injEq] at hok))
        (try (obtain rfl := Except.ok.inj hok))
        (try subst hok)
        rfl
      · rw [if_neg h_under] at hok
        (try (simp only [Except.ok.injEq] at hok))
        have h_no_ovf : ¬ (e > maxExponent) := by
          intro h_ovf; rw [if_pos h_ovf] at hok
          exact absurd hok (by intro h; cases h)
        rw [if_neg h_no_ovf] at hok
        obtain rfl := Except.ok.inj hok
        refine ⟨h_m_ge_min, ?_, ?_, ?_⟩
        · change m.toNat ≤ largeRange.max.toNat; rw [hmaxMant_v]; rw [hmaxRep_v] at h_ub; omega
        · change minExponent ≤ e; push_neg at h_under; exact h_under
        · change m.toNat > maxRep.toNat → m.toNat % 10 = 0; intro h_gt; exfalso; omega

end XRPL.Model.Protocol
