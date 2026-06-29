import XRPL.Properties.Protocol.Number.Common.Rounding.DoRoundUp.Basic
import XRPL.Properties.Protocol.Number.Common.ProofTactics

set_option linter.unusedTactic false
set_option linter.unusedSimpArgs false
set_option linter.unreachableTactic false

namespace XRPL.Model.Protocol

/-! ## Value characterization lemmas

Output value `mantissa * 10^exponent` equals one of three forms depending on
whether round-up fires and whether the cusp (overflow) branch triggers. -/

/-- No round-up: output value equals `m * 10^e`. -/
lemma doRoundUp_value_noRoundUp
    (g : Guard) (m : UInt64) (e : Int)
    (h_no_ru : ¬ (g.round .to_nearest = 1 ∨
                  (g.round .to_nearest = 0 ∧ m % 2 = 1)))
    (h_le_maxRep : m.toNat ≤ maxRep.toNat)
    (loc : String) (res : RoundResult)
    (hok : g.doRoundUp false m e largeRange.min largeRange.max .to_nearest loc = .ok res)
    (hne : res.mantissa_ ≠ 0) :
    (res.mantissa_.toNat : ℚ) * 10 ^ res.exponent_ = (m.toNat : ℚ) * 10 ^ e := by
  -- pushOverflow is a no-op when m ≤ maxRep (at m=maxRep, g.round≠1 means no bump at midpoint maxRep+1)
  have h_pof : g.pushOverflow m .to_nearest = g := by
    rcases Nat.lt_or_ge m.toNat maxRep.toNat with hlt | hge
    · exact pushOverflow_noop_of_lt_maxRep hlt g .to_nearest
    · have hm : m = maxRep := UInt64.toNat_inj.mp (by omega)
      subst hm
      push_neg at h_no_ru; obtain ⟨hr1, _⟩ := h_no_ru
      have hne_mid : ((maxRep : UInt64) == maxRep + 1) = false := by decide
      have hbump_false : (g.round .to_nearest == 1 ||
            (g.round .to_nearest == 0 && maxRep == maxRep + 1)) = false := by
        simp only [hne_mid, Bool.and_false, Bool.or_false, beq_eq_false_iff_ne]; exact hr1
      unfold Guard.pushOverflow
      rw [if_pos (by decide : maxRep ≤ maxRep ∧ maxRep < maxRepUp)]
      simp only [show (maxRep : UInt64) % 10 < 9 from by decide, if_true,
                 show (maxRepUp - maxRep : UInt64) / 2 = 1 from by decide,
                 hbump_false, if_false]
      have hmant_val : (if false = true then (maxRep : UInt64) + 1 else maxRep) = maxRep := by decide
      simp only [hmant_val, show ((maxRep : UInt64) == maxRep) = true from by decide, if_true]
  -- The cusp branch (maxRep < m ∧ m < maxRepUp) is impossible
  have h_no_cusp : ¬ (maxRep < m ∧ m < maxRepUp) := by
    intro ⟨h, _⟩; have := UInt64.lt_iff_toNat_lt.mp h; omega
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
    rw [h_pof] at hok
    rw [show ((g.round .to_nearest == 1) || ((g.round .to_nearest == 0) && (m % 2 == 1))) = false
        from h_ru_false] at hok
    simp only [Bool.false_eq_true, ite_false] at hok
    rw [if_neg h_no_cusp] at hok
    by_cases h_ovf : (Guard.bringIntoRange false m e largeRange.min).exponent_ > maxExponent
    · rw [if_pos h_ovf] at hok; simp at hok
    · rw [if_neg h_ovf] at hok; exact (Except.ok.inj hok).symm
  rw [hres_eq]
  -- Now prove about Guard.bringIntoRange
  have hne' : (Guard.bringIntoRange false m e largeRange.min).mantissa_ ≠ 0 := by
    rw [← hres_eq]; exact hne
  by_cases hresc : m < largeRange.min ∧ m ≠ 0
  · rw [bringIntoRange_rescale_result hresc.1 hresc.2]
    have hm_mul_10 : (m * 10).toNat = m.toNat * 10 :=
      m_mul_ten_no_overflow (UInt64.lt_iff_toNat_lt.mp hresc.1)
    by_cases h_under : e - 1 < minExponent ∨ m * 10 = 0
    · exfalso; apply hne'
      rw [bringIntoRange_rescale_result hresc.1 hresc.2, if_pos h_under]
    · push_neg at h_under
      obtain ⟨hexp, hm10⟩ := h_under
      rw [if_neg (by push_neg; exact ⟨hexp, hm10⟩)]
      change ((m * 10).toNat : ℚ) * 10 ^ (e - 1) = (m.toNat : ℚ) * 10 ^ e
      rw [hm_mul_10]; push_cast
      rw [show (e - 1 : ℤ) = e + (-1) from by ring,
          zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0), zpow_neg_one]
      field_simp
  · rw [bringIntoRange_noscale_result hresc]
    by_cases h_under : e < minExponent ∨ m = 0
    · exfalso; apply hne'
      rw [bringIntoRange_noscale_result hresc, if_pos h_under]
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
  have h_m_lt_maxRep : m.toNat < maxRep.toNat := by
    rw [maxRep_val] at h_no_ovf ⊢; omega
  -- pushOverflow is a no-op since m < maxRep
  have h_pof : g.pushOverflow m .to_nearest = g := pushOverflow_noop_of_lt_maxRep (by omega) g .to_nearest
  have h_ru_true :
      ((g.round .to_nearest == 1) || ((g.round .to_nearest == 0) && (m % 2 == 1))) = true := by
    rcases h_ru with h1 | ⟨h0, hodd⟩
    · simp [h1]
    · simp [h0, hodd]
  -- The `m < maxMantissa ∧ m < maxRep` branch fires
  have h_noncusp_branch : m < largeRange.max ∧ m < maxRep := by
    constructor
    · exact UInt64.lt_iff_toNat_lt.mpr (by rw [largeRange_max_val, maxRep_val] at *; omega)
    · exact UInt64.lt_iff_toNat_lt.mpr h_m_lt_maxRep
  -- Extract res = Guard.bringIntoRange false (m+1) e largeRange.min
  have hres_eq : res = Guard.bringIntoRange false (m + 1) e largeRange.min := by
    unfold Guard.doRoundUp at hok
    simp only [Guard.doDropDigit] at hok
    rw [h_pof] at hok
    rw [show ((g.round .to_nearest == 1) || ((g.round .to_nearest == 0) && (m % 2 == 1))) = true
        from h_ru_true] at hok
    rw [if_pos h_noncusp_branch] at hok
    simp only [if_true] at hok
    by_cases h_ovf : (Guard.bringIntoRange false (m + 1) e largeRange.min).exponent_ > maxExponent
    · rw [if_pos h_ovf] at hok; simp at hok
    · rw [if_neg h_ovf] at hok; exact (Except.ok.inj hok).symm
  rw [hres_eq]
  have h_m_le_maxRep : m.toNat ≤ maxRep.toNat := by omega
  have hm_add1_toNat : (m + 1).toNat = m.toNat + 1 := m_add_one_no_overflow h_m_le_maxRep
  have hne' : (Guard.bringIntoRange false (m + 1) e largeRange.min).mantissa_ ≠ 0 :=
    hres_eq ▸ hne
  -- m+1 > 0
  have h_m1_ne : m + 1 ≠ 0 := by
    intro h; have := UInt64.toNat_inj.mpr h; rw [hm_add1_toNat] at this; simp at this
  by_cases hresc : m + 1 < largeRange.min
  · rw [bringIntoRange_rescale_result hresc h_m1_ne]
    have h_m1_lt_min : (m + 1).toNat < largeRange.min.toNat := UInt64.lt_iff_toNat_lt.mp hresc
    have h_m1_mul10_toNat : ((m + 1) * 10).toNat = (m + 1).toNat * 10 := by
      rw [UInt64.toNat_mul]
      have h10u : (10 : UInt64).toNat = 10 := uint64_ten_toNat; rw [h10u]
      have : (m + 1).toNat * 10 < 2 ^ 64 := by
        rw [largeRange_min_val] at h_m1_lt_min
        calc (m + 1).toNat * 10 < 1000000000000000000 * 10 :=
              Nat.mul_lt_mul_of_pos_right h_m1_lt_min (by norm_num)
          _ < 2 ^ 64 := by norm_num
      exact Nat.mod_eq_of_lt this
    by_cases h_under : e - 1 < minExponent ∨ (m + 1) * 10 = 0
    · exfalso; apply hne'
      rw [bringIntoRange_rescale_result hresc h_m1_ne, if_pos h_under]
    · push_neg at h_under
      obtain ⟨hexp, hne10⟩ := h_under
      rw [if_neg (by push_neg; exact ⟨hexp, hne10⟩)]
      change ((((m + 1) * 10)).toNat : ℚ) * 10 ^ (e - 1) = ((m.toNat : ℚ) + 1) * 10 ^ e
      rw [h_m1_mul10_toNat, hm_add1_toNat]; push_cast
      rw [show (e - 1 : ℤ) = e + (-1) from by ring,
          zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0), zpow_neg_one]
      field_simp
  · rw [bringIntoRange_noscale_result (by push_neg; intro h; exact absurd h hresc)]
    by_cases h_under : e < minExponent ∨ m + 1 = 0
    · exfalso; apply hne'
      rw [bringIntoRange_noscale_result (by push_neg; intro h; exact absurd h hresc), if_pos h_under]
    · rw [if_neg h_under]
      change ((m + 1).toNat : ℚ) * 10 ^ e = ((m.toNat : ℚ) + 1) * 10 ^ e
      rw [hm_add1_toNat]; push_cast; ring

/-- Cusp case (`m = maxRep`, tie round-up fires): output value is `maxRepCuspTarget * 10^e`.
The tie (g.round = 0) fires the drop-digit path since pushOverflow is a no-op at m=maxRep
when round = 0 (no bump). The cusp branch pushes `maxRep % 10 = 7` into the guard,
re-rounds (+1 since top nibble is 7 > 5), and rescales: `(maxRep/10 + 1) * 10 = maxRep + 3`. -/
lemma doRoundUp_value_cusp
    (g : Guard) (m : UInt64) (e : Int)
    (h_m_eq : m = maxRep)
    (h_ru : g.round .to_nearest = 0 ∧ m % 2 = 1)
    (loc : String) (res : RoundResult)
    (hok : g.doRoundUp false m e largeRange.min largeRange.max .to_nearest loc = .ok res)
    (hne : res.mantissa_ ≠ 0) :
    (res.mantissa_.toNat : ℚ) * 10 ^ res.exponent_ = maxRepCuspTarget * 10 ^ e := by
  obtain ⟨h_tie, h_odd⟩ := h_ru
  have h_ru_true :
      ((g.round .to_nearest == 1) || ((g.round .to_nearest == 0) && (m % 2 == 1))) = true := by
    simp [h_tie, h_odd]
  have h_m_ge_maxRep : decide (m ≥ maxRep) = true := by rw [h_m_eq]; decide
  have h_cusp_cond_true : ((m ≥ largeRange.max) || (m ≥ maxRep)) = true := by
    rw [Bool.or_eq_true]; right; exact h_m_ge_maxRep
  have h_d_eq : m % 10 = 7 := by rw [h_m_eq]; decide
  have h_m_div10_toNat : (m / 10).toNat = mantissaFloor := by
    rw [UInt64.toNat_div, h_m_eq]; decide
  set g' := g.push (m % 10) with hg'_def
  have h_g'_digits_toNat : g'.digits_.toNat = g.digits_.toNat / 16 + (m.toNat % 10 % 16) * 2 ^ 60 := by
    rw [hg'_def]; have := toNat_push_digits g (m % 10); rw [UInt64.toNat_mod] at this
    have h10 : (10 : UInt64).toNat = 10 := uint64_ten_toNat; rw [h10] at this; exact this
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
  have h_g'_ne : ¬ g'.empty := by
    unfold Guard.empty
    simp only [Bool.and_eq_true, beq_iff_eq, Bool.not_eq_true]
    intro ⟨hd, _⟩
    have hd0 : g'.digits_.toNat = 0 := by
      rw [hd]; rfl
    omega
  have h_g'_round : g'.round .to_nearest = 1 := by rw [round_to_nearest_def h_g'_ne, if_pos h_g'_gt]
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
    rw [UInt64.toNat_mul]; have h10 : (10 : UInt64).toNat = 10 := uint64_ten_toNat; rw [h10, h_m1_toNat]
  -- pushOverflow is a no-op at m=maxRep when g.round=0: bump condition is r==1||(r==0&&m==midpoint),
  -- midpoint=maxRep+1≠maxRep, so no bump and the early-return fires.
  have h_pof : g.pushOverflow m .to_nearest = g := by
    subst h_m_eq
    have hne_mid : ((maxRep : UInt64) == maxRep + 1) = false := by decide
    have hbump_false : (g.round .to_nearest == 1 ||
          (g.round .to_nearest == 0 && maxRep == maxRep + 1)) = false := by
      simp only [hne_mid, Bool.and_false, Bool.or_false, beq_eq_false_iff_ne]
      intro h; rw [h_tie] at h; exact absurd h (by decide)
    unfold Guard.pushOverflow
    rw [if_pos (by decide : maxRep ≤ maxRep ∧ maxRep < maxRepUp)]
    simp only [show (maxRep : UInt64) % 10 < 9 from by decide, if_true,
               show (maxRepUp - maxRep : UInt64) / 2 = 1 from by decide,
               hbump_false, if_false]
    have hmant_val : (if false = true then (maxRep : UInt64) + 1 else maxRep) = maxRep := by decide
    simp only [hmant_val, show ((maxRep : UInt64) == maxRep) = true from by decide, if_true]
  -- m is not in the `m < maxMantissa ∧ m < maxRep` branch (m = maxRep, not < maxRep)
  have h_not_noncusp : ¬ (m < largeRange.max ∧ m < maxRep) := by
    intro ⟨_, h⟩; have := UInt64.lt_iff_toNat_lt.mp h; rw [h_m_eq, maxRep_val] at this; omega
  -- m is not in the `maxRep < m ∧ m < maxRepUp` branch (m = maxRep, not > maxRep)
  have h_not_overflow : ¬ (maxRep < m ∧ m < maxRepUp) := by
    intro ⟨h, _⟩; have := UInt64.lt_iff_toNat_lt.mp h; rw [h_m_eq, maxRep_val] at this; omega
  -- m/10+1 is nonzero
  have h_m1_ne : m / 10 + 1 ≠ 0 := by
    intro h; have := UInt64.toNat_inj.mpr h; rw [m_add_one_no_overflow h_m_div10_le_maxRep, h_m_div10_toNat] at this
    simp at this
  -- Extract res from hok
  have hres_eq : res = Guard.bringIntoRange false (m / 10 + 1) (e + 1) largeRange.min := by
    unfold Guard.doRoundUp at hok
    simp only [Guard.doDropDigit] at hok
    rw [h_pof] at hok
    rw [show ((g.round .to_nearest == 1) || ((g.round .to_nearest == 0) && (m % 2 == 1))) = true
        from h_ru_true] at hok
    rw [if_neg h_not_noncusp, if_neg h_not_overflow] at hok
    -- Now we're in the dropDigit branch: g' = g.push (m % 10), m' = m / 10, e' = e + 1
    -- h_g'_round : g'.round .to_nearest = 1
    -- h_ru'_true : (g'.round .to_nearest == 1 || ...) = true
    rw [show ((g'.round .to_nearest == 1) || ((g'.round .to_nearest == 0) && ((m / 10) % 2 == 1))) = true
        from h_ru'_true] at hok
    -- roundUp' = true, so mantissa becomes m/10 + 1
    simp only [if_true] at hok
    by_cases h_ovf : (Guard.bringIntoRange false (m / 10 + 1) (e + 1) largeRange.min).exponent_ > maxExponent
    · rw [if_pos h_ovf] at hok; simp at hok
    · rw [if_neg h_ovf] at hok; exact (Except.ok.inj hok).symm
  rw [hres_eq]
  have hne' : (Guard.bringIntoRange false (m / 10 + 1) (e + 1) largeRange.min).mantissa_ ≠ 0 :=
    hres_eq ▸ hne
  rw [bringIntoRange_rescale_result h_m1_lt_min h_m1_ne]
  have h_m1_mul10_toNat' : ((m / 10 + 1) * 10).toNat = maxRepCuspTarget := h_m1_mul10_toNat
  by_cases h_under : e + 1 - 1 < minExponent ∨ (m / 10 + 1) * 10 = 0
  · exfalso; apply hne'
    rw [bringIntoRange_rescale_result h_m1_lt_min h_m1_ne, if_pos h_under]
  · push_neg at h_under
    obtain ⟨hexp, hne10⟩ := h_under
    rw [if_neg (by push_neg; exact ⟨hexp, hne10⟩)]
    change (((m / 10 + 1) * 10).toNat : ℚ) * 10 ^ (e + 1 - 1) = maxRepCuspTarget * 10 ^ e
    rw [h_m1_mul10_toNat']
    have h_exp : (e + 1 - 1 : ℤ) = e := by ring
    rw [h_exp]; push_cast; ring

/-- Output invariants: mantissa in `[minMant, maxMant]`, exponent >= minExp,
and mantissa > maxRep implies mantissa divisible by 10. -/
lemma doRoundUp_output_invariants_to_nearest
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
  by_cases h_ru : ((g.pushOverflow m .to_nearest).round .to_nearest == 1
      || ((g.pushOverflow m .to_nearest).round .to_nearest == 0 && m % 2 == 1)) = true
  · rw [show ((g.pushOverflow m .to_nearest).round .to_nearest == 1
        || ((g.pushOverflow m .to_nearest).round .to_nearest == 0 && m % 2 == 1)) = true
        from h_ru] at hok
    by_cases h_m_eq_maxRep : m = maxRep
    · -- cusp case: m = maxRep, so not in `m < maxMantissa ∧ m < maxRep`, and not in `maxRep < m ∧ m < maxRepUp`
      have h_not_noncusp : ¬ (m < largeRange.max ∧ m < maxRep) := by
        intro ⟨_, h⟩; have := UInt64.lt_iff_toNat_lt.mp h; rw [h_m_eq_maxRep, maxRep_val] at this; omega
      have h_not_overflow : ¬ (maxRep < m ∧ m < maxRepUp) := by
        intro ⟨h, _⟩; have := UInt64.lt_iff_toNat_lt.mp h; rw [h_m_eq_maxRep, maxRep_val] at this; omega
      -- pushOverflow is a no-op here: if g.round = 1, push 3 → rounds -1 → roundUp=false,
      -- contradicting h_ru=true. So g.round ≠ 1 and pushOverflow returns g (no bump).
      have h_pof : g.pushOverflow m .to_nearest = g := by
        subst h_m_eq_maxRep
        -- If bump (g.round=1): result = g.push 3, which rounds ≤ 0 → roundUp=false → contradiction
        by_cases hg1 : g.round .to_nearest = 1
        · exfalso
          have hpof_eq : g.pushOverflow maxRep .to_nearest = g.push 3 := by
            unfold Guard.pushOverflow
            rw [if_pos (by decide : maxRep ≤ maxRep ∧ maxRep < maxRepUp)]
            have hbump : (g.round .to_nearest == 1 ||
                (g.round .to_nearest == 0 && maxRep == maxRep + 1)) = true := by simp [hg1]
            have hmid_ne : ((maxRep + 1 : UInt64) == maxRep) = false := by decide
            simp only [show (maxRep : UInt64) % 10 < 9 from by decide, if_true,
                       show (maxRepUp - maxRep : UInt64) / 2 = 1 from by decide,
                       hbump, if_true, hmid_ne, Bool.false_eq_true, if_false,
                       show ((maxRep + 1 : UInt64) - maxRep) = 1 from by decide,
                       show (1 * 10 : UInt64) / (maxRepUp - maxRep) = 3 from by decide]
          have hdig3 : (g.push 3).digits_.toNat < 5764607523034234880 := by
            have hpd := toNat_push_digits g 3
            have h3 : (3 : UInt64).toNat = 3 := rfl
            rw [h3] at hpd
            have hsize : g.digits_.toNat ≤ 2 ^ 64 - 1 := by
              have := UInt64.toNat_lt_size g.digits_; simp only [UInt64.size] at this; omega
            omega
          have h_push3_lt : (g.push 3).digits_ < 0x5000_0000_0000_0000 := by
            rw [UInt64.lt_iff_toNat_lt]
            have h5000 : (0x5000_0000_0000_0000 : UInt64).toNat = 5764607523034234880 := by decide
            rw [h5000]; exact hdig3
          have h_round_neg : (g.pushOverflow maxRep .to_nearest).round .to_nearest = -1
              ∨ (g.pushOverflow maxRep .to_nearest).round .to_nearest = -2 := by
            rw [hpof_eq]
            by_cases hemp : (g.push 3).empty = true
            · right; unfold Guard.round; rw [if_pos hemp]
            · left; rw [round_to_nearest_def hemp]
              have hlt_nat : (g.push 3).digits_.toNat < 5764607523034234880 := by
                rwa [UInt64.lt_iff_toNat_lt, show (0x5000_0000_0000_0000 : UInt64).toNat =
                  5764607523034234880 from by decide] at h_push3_lt
              have hgt_false : ¬ (g.push 3).digits_ > 0x5000_0000_0000_0000 := by
                simp only [UInt64.lt_iff_toNat_lt] at *; omega
              simp only [if_neg hgt_false, if_pos h_push3_lt]
          rcases h_round_neg with hr | hr
          · simp [hr] at h_ru
          · simp [hr] at h_ru
        · -- No bump: mantissa stays maxRep, early return g
          unfold Guard.pushOverflow
          rw [if_pos (by decide : maxRep ≤ maxRep ∧ maxRep < maxRepUp)]
          simp only [show (maxRep : UInt64) % 10 < 9 from by decide,
                     show (maxRepUp - maxRep : UInt64) / 2 = 1 from by decide]
          have hne_mid2 : ((maxRep : UInt64) == maxRep + 1) = false := by decide
          have hbump_false : (g.round .to_nearest == 1 ||
              (g.round .to_nearest == 0 && maxRep == maxRep + 1)) = false := by
            simp only [hne_mid2, Bool.and_false, Bool.or_false, beq_eq_false_iff_ne]; exact hg1
          simp only [hbump_false, if_false]
          have hmant_val2 : (if false = true then (maxRep : UInt64) + 1 else maxRep) = maxRep := by decide
          simp only [hmant_val2, show ((maxRep : UInt64) == maxRep) = true from by decide, if_true]
      rw [h_pof] at hok
      rw [if_neg h_not_noncusp, if_neg h_not_overflow] at hok
      have h_d_eq : m % 10 = 7 := by rw [h_m_eq_maxRep]; decide
      have h_m_div10_toNat : (m / 10).toNat = mantissaFloor := by
        rw [UInt64.toNat_div, h_m_eq_maxRep]; decide
      set g' := g.push (m % 10) with hg'_def
      have h_g'_digits_toNat : g'.digits_.toNat
          = g.digits_.toNat / 16 + (m.toNat % 10 % 16) * 2 ^ 60 := by
        rw [hg'_def]
        have := toNat_push_digits g (m % 10)
        rw [UInt64.toNat_mod] at this
        have h10 : (10 : UInt64).toNat = 10 := uint64_ten_toNat
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
      have h_g'_ne : ¬ g'.empty := by
        unfold Guard.empty
        simp only [Bool.and_eq_true, beq_iff_eq, Bool.not_eq_true]
        intro ⟨hd, _⟩
        have hd0 : g'.digits_.toNat = 0 := by
          rw [hd]; rfl
        omega
      have h_g'_round : g'.round .to_nearest = 1 := by
        rw [round_to_nearest_def h_g'_ne]; rw [if_pos h_g'_gt]
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
      have h_m1_ne : m / 10 + 1 ≠ 0 := by
        intro h; have := UInt64.toNat_inj.mpr h; rw [m_add_one_no_overflow h_m_div10_le_maxRep, h_m_div10_toNat] at this; simp at this
      rw [if_pos (And.intro h_m1_lt_min h_m1_ne)] at hok; simp only [] at hok
      have h_m1_mul10_toNat : ((m / 10 + 1) * 10).toNat = maxRepCuspTarget := by
        rw [UInt64.toNat_mul]
        have h10 : (10 : UInt64).toNat = 10 := uint64_ten_toNat; rw [h10, h_m1_toNat]
      have h_m1_10_ne : (m / 10 + 1) * 10 ≠ 0 := by
        intro h; have := UInt64.toNat_inj.mpr h; rw [h_m1_mul10_toNat] at this; simp at this
      -- Navigate the bringIntoRange result
      -- After rw [if_pos (And.intro h_m1_lt_min h_m1_ne)], hok has the rescaled form
      -- which has condition (e+1-1 < minExp ∨ (m/10+1)*10 = 0)
      by_cases h_under : e + 1 - 1 < minExponent ∨ (m / 10 + 1) * 10 = 0
      · underflow_absurd hne h_under hok
      · push_neg at h_under
        obtain ⟨hexp, hne10⟩ := h_under
        have h_not_under : ¬ (e + 1 - 1 < minExponent ∨ (m / 10 + 1) * 10 = 0) :=
          by push_neg; exact ⟨hexp, hne10⟩
        simp only [if_neg h_not_under] at hok
        -- hok : (if e+1-1 > maxExponent then .error else .ok struct) = .ok res
        have h_no_ovf : ¬ (e + 1 - 1 > maxExponent) := by
          intro h_ovf; simp only [if_pos h_ovf] at hok; simp at hok
        simp only [if_neg h_no_ovf] at hok
        obtain rfl := Except.ok.inj hok
        refine ⟨?_, ?_, ?_, ?_⟩
        · change largeRange.min.toNat ≤ ((m / 10 + 1) * 10).toNat
          rw [h_m1_mul10_toNat, hminMant_v]; norm_num
        · change ((m / 10 + 1) * 10).toNat ≤ largeRange.max.toNat
          rw [h_m1_mul10_toNat, hmaxMant_v]; norm_num
        · change minExponent ≤ e + 1 - 1; exact hexp
        · intro _; change ((m / 10 + 1) * 10).toNat % 10 = 0; rw [h_m1_mul10_toNat]
    · -- non-cusp
      have h_m_lt_maxRep : m.toNat < maxRep.toNat := by
        have : m.toNat ≠ maxRep.toNat := fun heq => h_m_eq_maxRep (UInt64.toNat_inj.mp heq)
        omega
      -- In new model: roundUp fires → first check `m < maxMantissa ∧ m < maxRep`
      have h_noncusp_branch : m < largeRange.max ∧ m < maxRep := by
        constructor
        · exact UInt64.lt_iff_toNat_lt.mpr (by rw [largeRange_max_val, maxRep_val] at *; omega)
        · exact UInt64.lt_iff_toNat_lt.mpr h_m_lt_maxRep
      rw [if_pos h_noncusp_branch] at hok
      simp only [if_true] at hok
      have h_m1_le_maxRep : (m + 1).toNat ≤ maxRep.toNat := by rw [hm_add1_toNat]; omega
      have h_m1_ne : m + 1 ≠ 0 := by
        intro h; have := UInt64.toNat_inj.mpr h; rw [hm_add1_toNat] at this; simp at this
      by_cases h_resc : m + 1 < largeRange.min
      · rw [if_pos (And.intro h_resc h_m1_ne)] at hok; simp only [] at hok
        have h_m1_lt_min : (m + 1).toNat < largeRange.min.toNat := UInt64.lt_iff_toNat_lt.mp h_resc
        have h_m1_mul10_toNat : ((m + 1) * 10).toNat = (m + 1).toNat * 10 := by
          rw [UInt64.toNat_mul]; have h10 : (10 : UInt64).toNat = 10 := uint64_ten_toNat; rw [h10]
          apply Nat.mod_eq_of_lt; rw [hminMant_v] at h_m1_lt_min
          calc (m + 1).toNat * 10 < 1000000000000000000 * 10 := Nat.mul_lt_mul_of_pos_right h_m1_lt_min (by norm_num)
            _ < 2 ^ 64 := by norm_num
        have h_m1_10_ne : (m + 1) * 10 ≠ 0 := by
          intro h
          have hnat : ((m + 1) * 10).toNat = 0 := by rw [h]; rfl
          rw [h_m1_mul10_toNat, hm_add1_toNat] at hnat; omega
        by_cases h_under : e - 1 < minExponent ∨ (m + 1) * 10 = 0
        · underflow_absurd hne h_under hok
        · push_neg at h_under
          obtain ⟨hexp, hne10⟩ := h_under
          have h_not_under : ¬ (e - 1 < minExponent ∨ (m + 1) * 10 = 0) :=
            by push_neg; exact ⟨hexp, hne10⟩
          simp only [if_neg h_not_under] at hok
          have h_no_ovf : ¬ (e - 1 > maxExponent) := by
            intro h_ovf; simp only [if_pos h_ovf] at hok; simp at hok
          simp only [if_neg h_no_ovf] at hok; obtain rfl := Except.ok.inj hok
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
          · change minExponent ≤ e - 1; exact hexp
          · intro _; change ((m + 1) * 10).toNat % 10 = 0; rw [h_m1_mul10_toNat]; omega
      · have h_no_resc : ¬ (m + 1 < largeRange.min ∧ m + 1 ≠ 0) := by
          intro ⟨h, _⟩; exact h_resc h
        rw [if_neg h_no_resc] at hok; simp only [] at hok
        have h_m1_ge_min : (m + 1).toNat ≥ largeRange.min.toNat := by
          by_contra h; push_neg at h; exact h_resc (UInt64.lt_iff_toNat_lt.mpr h)
        by_cases h_under : e < minExponent ∨ m + 1 = 0
        · underflow_absurd hne h_under hok
        · push_neg at h_under
          obtain ⟨hexp, hm1ne⟩ := h_under
          have h_not_under : ¬ (e < minExponent ∨ m + 1 = 0) :=
            by push_neg; exact ⟨hexp, hm1ne⟩
          simp only [if_neg h_not_under] at hok
          have h_no_ovf : ¬ (e > maxExponent) := by
            intro h_ovf; simp only [if_pos h_ovf] at hok; simp at hok
          simp only [if_neg h_no_ovf] at hok; obtain rfl := Except.ok.inj hok
          refine ⟨h_m1_ge_min, ?_, ?_, ?_⟩
          · change (m + 1).toNat ≤ largeRange.max.toNat; rw [hm_add1_toNat, hmaxMant_v]; rw [hmaxRep_v] at h_m_lt_maxRep; omega
          · change minExponent ≤ e; exact hexp
          · change (m + 1).toNat > maxRep.toNat → (m + 1).toNat % 10 = 0
            intro h_gt; exfalso; have : (m + 1).toNat ≤ maxRep.toNat := h_m1_le_maxRep; omega
  · rw [Bool.not_eq_true] at h_ru
    rw [h_ru] at hok
    simp only [Bool.false_eq_true, ite_false] at hok
    -- No-roundUp: check `maxRep < m ∧ m < maxRepUp` (false since m ≤ maxRep)
    have h_no_overflow : ¬ (maxRep < m ∧ m < maxRepUp) := by
      intro ⟨h, _⟩; have := UInt64.lt_iff_toNat_lt.mp h; omega
    rw [if_neg h_no_overflow] at hok
    -- `bringIntoRange m e largeRange.min` branch
    have h_m_ne : m ≠ 0 := by
      intro h; rw [h] at h_lb; simp at h_lb
    by_cases h_resc : m < largeRange.min
    · rw [if_pos (And.intro h_resc h_m_ne)] at hok; simp only [] at hok
      have h_m_lt_min : m.toNat < largeRange.min.toNat := UInt64.lt_iff_toNat_lt.mp h_resc
      have h_m_mul10_toNat : (m * 10).toNat = m.toNat * 10 := m_mul_ten_no_overflow h_m_lt_min
      have h_m10_ne : m * 10 ≠ 0 := by
        intro h; have hnat : (m * 10).toNat = 0 := by rw [h]; rfl
        rw [h_m_mul10_toNat] at hnat; omega
      by_cases h_under : e - 1 < minExponent ∨ m * 10 = 0
      · underflow_absurd hne h_under hok
      · push_neg at h_under
        obtain ⟨hexp, hne10⟩ := h_under
        have h_not_under : ¬ (e - 1 < minExponent ∨ m * 10 = 0) :=
          by push_neg; exact ⟨hexp, hne10⟩
        simp only [if_neg h_not_under] at hok
        have h_no_ovf : ¬ (e - 1 > maxExponent) := by
          intro h_ovf; simp only [if_pos h_ovf] at hok; simp at hok
        simp only [if_neg h_no_ovf] at hok; obtain rfl := Except.ok.inj hok
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
        · change minExponent ≤ e - 1; exact hexp
        · intro _; change (m * 10).toNat % 10 = 0; rw [h_m_mul10_toNat]; omega
    · have h_no_resc : ¬ (m < largeRange.min ∧ m ≠ 0) := by
        intro ⟨h, _⟩; exact h_resc h
      rw [if_neg h_no_resc] at hok; simp only [] at hok
      have h_m_ge_min : m.toNat ≥ largeRange.min.toNat := by
        by_contra h; push_neg at h; exact h_resc (UInt64.lt_iff_toNat_lt.mpr h)
      by_cases h_under : e < minExponent ∨ m = 0
      · underflow_absurd hne h_under hok
      · push_neg at h_under
        obtain ⟨hexp, hmne⟩ := h_under
        have h_not_under : ¬ (e < minExponent ∨ m = 0) :=
          by push_neg; exact ⟨hexp, hmne⟩
        simp only [if_neg h_not_under] at hok
        have h_no_ovf : ¬ (e > maxExponent) := by
          intro h_ovf; simp only [if_pos h_ovf] at hok; simp at hok
        simp only [if_neg h_no_ovf] at hok; obtain rfl := Except.ok.inj hok
        refine ⟨h_m_ge_min, ?_, ?_, ?_⟩
        · change m.toNat ≤ largeRange.max.toNat; rw [hmaxMant_v]; rw [hmaxRep_v] at h_ub; omega
        · change minExponent ≤ e; exact hexp
        · change m.toNat > maxRep.toNat → m.toNat % 10 = 0; intro h_gt; exfalso; omega

/-- Output invariants on the cusp range `maxRep < m ≤ maxRepUp`, reachable in the
same-sign add path (the 128-bit overflow drop fires only above `maxRepUp`).
Mode-generic: whatever the round decision, every branch lands on `maxRep`,
`maxRepUp`, or a rescaled `(maxRepUp/10 + δ) * 10` — all satisfying the invariants. -/
lemma doRoundUp_output_invariants_cusp
    (g : Guard) (neg : Bool) (m : UInt64) (e : Int) (mode : rounding_mode)
    (h_lb : maxRep.toNat < m.toNat)
    (h_ub : m.toNat ≤ maxRepUp.toNat)
    (loc : String) (res : RoundResult)
    (hok : g.doRoundUp neg m e largeRange.min largeRange.max mode loc = .ok res)
    (hne : res.mantissa_ ≠ 0) :
    largeRange.min.toNat ≤ res.mantissa_.toNat ∧
    res.mantissa_.toNat ≤ largeRange.max.toNat ∧
    minExponent ≤ res.exponent_ ∧
    (res.mantissa_.toNat > maxRep.toNat → res.mantissa_.toNat % 10 = 0) := by
  have hmaxRep_v : maxRep.toNat = maxRepNat := maxRep_val
  have hminMant_v : largeRange.min.toNat = 1000000000000000000 := largeRange_min_val
  have hmaxMant_v : largeRange.max.toNat = 9999999999999999999 := largeRange_max_val
  have hmaxRepUp_v : maxRepUp.toNat = maxRepUpNat := rfl
  unfold Guard.doRoundUp Guard.bringIntoRange at hok
  simp only [Guard.doDropDigit] at hok
  by_cases h_eq_up : m = maxRepUp
  · -- m = maxRepUp: pushOverflow is a no-op (the strict `< maxRepUp` fails).
    subst h_eq_up
    have h_pof : g.pushOverflow maxRepUp mode = g := by
      unfold Guard.pushOverflow
      rw [if_neg]
      intro ⟨_, h⟩
      exact absurd (UInt64.lt_iff_toNat_lt.mp h) (lt_irrefl _)
    rw [h_pof] at hok
    have h_not_noncusp : ¬ (maxRepUp < largeRange.max ∧ maxRepUp < maxRep) := by decide
    have h_not_cusp : ¬ (maxRep < maxRepUp ∧ maxRepUp < maxRepUp) := by decide
    by_cases h_ru : (g.round mode == 1 || (g.round mode == 0 && maxRepUp % 2 == 1)) = true
    · -- roundUp at m = maxRepUp: both cusp tests fail; the drop-digit branch runs.
      rw [show (g.round mode == 1 || (g.round mode == 0 && maxRepUp % 2 == 1)) = true
          from h_ru] at hok
      rw [if_pos rfl, if_neg h_not_noncusp, if_neg h_not_cusp] at hok
      -- The dropped state: (g.push (maxRepUp % 10), maxRepUp / 10, e + 1).
      by_cases h_ru' : ((g.push (maxRepUp % 10)).round mode == 1
          || ((g.push (maxRepUp % 10)).round mode == 0 && (maxRepUp / 10) % 2 == 1)) = true
      · -- second round-up: mantissa maxRepUp/10 + 1, rescales to ...5820.
        rw [show ((g.push (maxRepUp % 10)).round mode == 1
            || ((g.push (maxRepUp % 10)).round mode == 0 && (maxRepUp / 10) % 2 == 1)) = true
            from h_ru'] at hok
        simp only [if_true] at hok
        have h_resc : maxRepUp / 10 + 1 < largeRange.min ∧ maxRepUp / 10 + 1 ≠ 0 := by decide
        rw [if_pos h_resc] at hok
        simp only [] at hok
        by_cases h_under : e + 1 - 1 < minExponent ∨ (maxRepUp / 10 + 1) * 10 = 0
        · underflow_absurd hne h_under hok
        · push_neg at h_under
          obtain ⟨hexp, -⟩ := h_under
          have h_not_under : ¬ (e + 1 - 1 < minExponent ∨ (maxRepUp / 10 + 1) * 10 = 0) := by
            push_neg; exact ⟨hexp, by decide⟩
          simp only [if_neg h_not_under] at hok
          have h_no_ovf : ¬ (e + 1 - 1 > maxExponent) := by
            intro h_ovf; simp only [if_pos h_ovf] at hok; simp at hok
          simp only [if_neg h_no_ovf] at hok
          obtain rfl := Except.ok.inj hok
          refine ⟨?_, ?_, hexp, ?_⟩
          · change largeRange.min.toNat ≤ ((maxRepUp / 10 + 1) * 10).toNat; decide
          · change ((maxRepUp / 10 + 1) * 10).toNat ≤ largeRange.max.toNat; decide
          · intro _; change ((maxRepUp / 10 + 1) * 10).toNat % 10 = 0; decide
      · -- no second round-up: mantissa maxRepUp/10, rescales to ...5810 = maxRepUp.
        rw [show ((g.push (maxRepUp % 10)).round mode == 1
            || ((g.push (maxRepUp % 10)).round mode == 0 && (maxRepUp / 10) % 2 == 1)) = false
            from Bool.not_eq_true _ ▸ h_ru'] at hok
        simp only [Bool.false_eq_true, if_false] at hok
        have h_resc : maxRepUp / 10 < largeRange.min ∧ maxRepUp / 10 ≠ 0 := by decide
        rw [if_pos h_resc] at hok
        simp only [] at hok
        by_cases h_under : e + 1 - 1 < minExponent ∨ (maxRepUp / 10) * 10 = 0
        · underflow_absurd hne h_under hok
        · push_neg at h_under
          obtain ⟨hexp, -⟩ := h_under
          have h_not_under : ¬ (e + 1 - 1 < minExponent ∨ (maxRepUp / 10) * 10 = 0) := by
            push_neg; exact ⟨hexp, by decide⟩
          simp only [if_neg h_not_under] at hok
          have h_no_ovf : ¬ (e + 1 - 1 > maxExponent) := by
            intro h_ovf; simp only [if_pos h_ovf] at hok; simp at hok
          simp only [if_neg h_no_ovf] at hok
          obtain rfl := Except.ok.inj hok
          refine ⟨?_, ?_, hexp, ?_⟩
          · change largeRange.min.toNat ≤ ((maxRepUp / 10) * 10).toNat; decide
          · change ((maxRepUp / 10) * 10).toNat ≤ largeRange.max.toNat; decide
          · intro _; change ((maxRepUp / 10) * 10).toNat % 10 = 0; decide
    · -- no round-up at m = maxRepUp: the no-roundUp cusp test also fails; keep maxRepUp.
      rw [Bool.not_eq_true] at h_ru
      rw [show (g.round mode == 1 || (g.round mode == 0 && maxRepUp % 2 == 1)) = false
          from h_ru] at hok
      simp only [Bool.false_eq_true, if_false] at hok
      rw [if_neg h_not_cusp] at hok
      have h_no_resc : ¬ (maxRepUp < largeRange.min ∧ maxRepUp ≠ 0) := by decide
      rw [if_neg h_no_resc] at hok
      simp only [] at hok
      by_cases h_under : e < minExponent ∨ (maxRepUp : UInt64) = 0
      · underflow_absurd hne h_under hok
      · push_neg at h_under
        obtain ⟨hexp, -⟩ := h_under
        have h_not_under : ¬ (e < minExponent ∨ (maxRepUp : UInt64) = 0) := by
          push_neg; exact ⟨hexp, by decide⟩
        simp only [if_neg h_not_under] at hok
        have h_no_ovf : ¬ (e > maxExponent) := by
          intro h_ovf; simp only [if_pos h_ovf] at hok; simp at hok
        simp only [if_neg h_no_ovf] at hok
        obtain rfl := Except.ok.inj hok
        refine ⟨?_, ?_, hexp, ?_⟩
        · change largeRange.min.toNat ≤ maxRepUp.toNat; decide
        · change maxRepUp.toNat ≤ largeRange.max.toNat; decide
        · intro _; change maxRepUp.toNat % 10 = 0; decide
  · -- maxRep < m < maxRepUp: the strict cusp; the output is maxRepUp or maxRep.
    have h_m_lt_up : m < maxRepUp := by
      rw [UInt64.lt_iff_toNat_lt]
      have hne_nat : m.toNat ≠ maxRepUp.toNat := fun h => h_eq_up (UInt64.toNat_inj.mp h)
      omega
    have h_maxRep_lt_m : maxRep < m := UInt64.lt_iff_toNat_lt.mpr h_lb
    have h_cusp_cond : maxRep < m ∧ m < maxRepUp := ⟨h_maxRep_lt_m, h_m_lt_up⟩
    have h_not_noncusp : ¬ (m < largeRange.max ∧ m < maxRep) := by
      intro ⟨_, h⟩
      exact absurd (UInt64.lt_iff_toNat_lt.mp h) (by omega)
    -- pushOverflow fires, but its result only feeds the (opaque) round decision.
    by_cases h_ru : ((g.pushOverflow m mode).round mode == 1
        || ((g.pushOverflow m mode).round mode == 0 && m % 2 == 1)) = true
    · -- round-up: clamp up to maxRepUp.
      rw [show ((g.pushOverflow m mode).round mode == 1
          || ((g.pushOverflow m mode).round mode == 0 && m % 2 == 1)) = true from h_ru] at hok
      rw [if_pos rfl, if_neg h_not_noncusp, if_pos h_cusp_cond] at hok
      have h_no_resc : ¬ (maxRepUp < largeRange.min ∧ maxRepUp ≠ 0) := by decide
      rw [if_neg h_no_resc] at hok
      simp only [] at hok
      by_cases h_under : e < minExponent ∨ (maxRepUp : UInt64) = 0
      · underflow_absurd hne h_under hok
      · push_neg at h_under
        obtain ⟨hexp, -⟩ := h_under
        have h_not_under : ¬ (e < minExponent ∨ (maxRepUp : UInt64) = 0) := by
          push_neg; exact ⟨hexp, by decide⟩
        simp only [if_neg h_not_under] at hok
        have h_no_ovf : ¬ (e > maxExponent) := by
          intro h_ovf; simp only [if_pos h_ovf] at hok; simp at hok
        simp only [if_neg h_no_ovf] at hok
        obtain rfl := Except.ok.inj hok
        refine ⟨?_, ?_, hexp, ?_⟩
        · change largeRange.min.toNat ≤ maxRepUp.toNat; decide
        · change maxRepUp.toNat ≤ largeRange.max.toNat; decide
        · intro _; change maxRepUp.toNat % 10 = 0; decide
    · -- no round-up: clamp down to maxRep.
      rw [Bool.not_eq_true] at h_ru
      rw [show ((g.pushOverflow m mode).round mode == 1
          || ((g.pushOverflow m mode).round mode == 0 && m % 2 == 1)) = false from h_ru] at hok
      simp only [Bool.false_eq_true, if_false] at hok
      rw [if_pos h_cusp_cond] at hok
      have h_no_resc : ¬ (maxRep < largeRange.min ∧ maxRep ≠ 0) := by decide
      rw [if_neg h_no_resc] at hok
      simp only [] at hok
      by_cases h_under : e < minExponent ∨ (maxRep : UInt64) = 0
      · underflow_absurd hne h_under hok
      · push_neg at h_under
        obtain ⟨hexp, -⟩ := h_under
        have h_not_under : ¬ (e < minExponent ∨ (maxRep : UInt64) = 0) := by
          push_neg; exact ⟨hexp, by decide⟩
        simp only [if_neg h_not_under] at hok
        have h_no_ovf : ¬ (e > maxExponent) := by
          intro h_ovf; simp only [if_pos h_ovf] at hok; simp at hok
        simp only [if_neg h_no_ovf] at hok
        obtain rfl := Except.ok.inj hok
        refine ⟨?_, ?_, hexp, ?_⟩
        · change largeRange.min.toNat ≤ maxRep.toNat; decide
        · change maxRep.toNat ≤ largeRange.max.toNat; decide
        · intro h_gt; exfalso; exact absurd h_gt (lt_irrefl _)

/-- Combined output invariants for `mantissaFloor ≤ m ≤ maxRepUp` (to_nearest),
dispatching between the in-range lemma and the cusp lemma. -/
lemma doRoundUp_output_invariants_to_nearest_upTo_maxRepUp
    (g : Guard) (neg : Bool) (m : UInt64) (e : Int)
    (h_lb : mantissaFloor ≤ m.toNat)
    (h_ub : m.toNat ≤ maxRepUp.toNat)
    (loc : String) (res : RoundResult)
    (hok : g.doRoundUp neg m e largeRange.min largeRange.max .to_nearest loc = .ok res)
    (hne : res.mantissa_ ≠ 0) :
    largeRange.min.toNat ≤ res.mantissa_.toNat ∧
    res.mantissa_.toNat ≤ largeRange.max.toNat ∧
    minExponent ≤ res.exponent_ ∧
    (res.mantissa_.toNat > maxRep.toNat → res.mantissa_.toNat % 10 = 0) := by
  by_cases h_le : m.toNat ≤ maxRep.toNat
  · exact doRoundUp_output_invariants_to_nearest g neg m e h_lb h_le loc res hok hne
  · push_neg at h_le
    exact doRoundUp_output_invariants_cusp g neg m e .to_nearest h_le h_ub loc res hok hne

/-- Supremum-tight relative error bound `5 / (2^63 + 7)`, using the floor-residue
constraint `f >= 8/10` when `zm = mantissaFloor`. -/
lemma doRoundUp_rounds_to_nearest_supTight_ne (g : Guard) (zm : UInt64) (ze : Int) (f : ℚ)
    (hf_rep : represents g f)
    (hg_ne : ¬ g.empty)
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
  obtain ⟨hround_pos, hround_neg, hround_zero⟩ := round_correct hg_ne hf_rep
  have h_denom_val : (((2 ^ 63 + 7 : ℕ) : ℚ)) = 9223372036854775815 := by
    push_cast; norm_num
  have h_denom_pos : (0 : ℚ) < ((2 ^ 63 + 7 : ℕ) : ℚ) := by
    rw [h_denom_val]; norm_num
  have h_round_values : g.round .to_nearest = 1 ∨ g.round .to_nearest = 0
      ∨ g.round .to_nearest = -1 := by
    rw [round_to_nearest_def hg_ne]
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
    · have hf_gt_half : f > 1/2 := hround_pos.mp h_up
      have h_pof : g.pushOverflow zm .to_nearest = g.push 3 := by
        subst h_zm_eq_maxRep
        unfold Guard.pushOverflow
        rw [if_pos (by decide : maxRep ≤ maxRep ∧ maxRep < maxRepUp)]
        have hbump : (g.round .to_nearest == 1 ||
            (g.round .to_nearest == 0 && maxRep == maxRep + 1)) = true := by simp [h_up]
        have hmid_ne2 : ((maxRep + 1 : UInt64) == maxRep) = false := by decide
        simp only [show (maxRep : UInt64) % 10 < 9 from by decide, if_true,
                   show (maxRepUp - maxRep : UInt64) / 2 = 1 from by decide,
                   hbump, if_true, hmid_ne2, Bool.false_eq_true, if_false,
                   show ((maxRep + 1 : UInt64) - maxRep) = 1 from by decide,
                   show (1 * 10 : UInt64) / (maxRepUp - maxRep) = 3 from by decide]
      have h_pof_round : (g.pushOverflow zm .to_nearest).round .to_nearest = -1
          ∨ (g.pushOverflow zm .to_nearest).round .to_nearest = -2 := by
        rw [h_pof]
        have hdig3 : (g.push 3).digits_.toNat < 5764607523034234880 := by
          have hpd := toNat_push_digits g 3
          have h3 : (3 : UInt64).toNat = 3 := rfl
          rw [h3] at hpd
          have hsize : g.digits_.toNat ≤ 2 ^ 64 - 1 := by
            have := UInt64.toNat_lt_size g.digits_; simp only [UInt64.size] at this; omega
          omega
        have h_push3_lt : (g.push 3).digits_ < 0x5000_0000_0000_0000 := by
          rw [UInt64.lt_iff_toNat_lt]
          have h5000 : (0x5000_0000_0000_0000 : UInt64).toNat = 5764607523034234880 := by decide
          rw [h5000]; exact hdig3
        by_cases hemp : (g.push 3).empty = true
        · right; unfold Guard.round; rw [if_pos hemp]
        · left
          rw [round_to_nearest_def hemp]
          have hlt_nat2 : (g.push 3).digits_.toNat < 5764607523034234880 := by
            rwa [UInt64.lt_iff_toNat_lt, show (0x5000_0000_0000_0000 : UInt64).toNat =
              5764607523034234880 from by decide] at h_push3_lt
          have h_not_gt : ¬ ((g.push 3).digits_ > 0x5000_0000_0000_0000) := by
            simp only [UInt64.lt_iff_toNat_lt] at *; omega
          simp only [if_neg h_not_gt, if_pos h_push3_lt]
      have h_val : (res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_ = maxRepNat * 10 ^ ze := by
        have h_roundup_false :
            ((g.pushOverflow zm .to_nearest).round .to_nearest == 1
              || ((g.pushOverflow zm .to_nearest).round .to_nearest == 0 && zm % 2 == 1)) = false := by
          rcases h_pof_round with hr | hr <;> simp [hr]
        unfold Guard.doRoundUp Guard.bringIntoRange at hok_pos
        simp only [Guard.doDropDigit] at hok_pos
        rw [h_roundup_false] at hok_pos
        simp only [Bool.false_eq_true, ite_false] at hok_pos
        have h_no_cusp : ¬ (maxRep < zm ∧ zm < maxRepUp) := by
          intro ⟨h, _⟩; have := UInt64.lt_iff_toNat_lt.mp h; rw [h_zm_eq_maxRep, maxRep_val] at this; omega
        rw [if_neg h_no_cusp] at hok_pos
        have h_no_resc : ¬ (zm < largeRange.min ∧ zm ≠ 0) := by
          intro ⟨h, _⟩
          have := UInt64.lt_iff_toNat_lt.mp h
          rw [h_zm_eq_maxRep, maxRep_val, largeRange_min_val] at this; omega
        rw [if_neg h_no_resc] at hok_pos
        have h_not_under : ¬ (ze < minExponent ∨ zm = 0) := by
          push_neg
          constructor
          · by_contra hlt
            have hlt' : ze < minExponent := by omega
            rw [if_pos (Or.inl hlt')] at hok_pos
            have hzexp : ¬ ((-2147483648 : Int) > maxExponent) := by norm_num [maxExponent]
            simp only [if_neg hzexp] at hok_pos
            obtain rfl := Except.ok.inj hok_pos
            exact hres_pos_mant_ne rfl
          · rw [h_zm_eq_maxRep]; decide
        rw [if_neg h_not_under] at hok_pos
        have h_no_ovf : ¬ (ze > maxExponent) := by
          intro h_ovf; simp only [if_pos h_ovf] at hok_pos; simp at hok_pos
        rw [if_neg h_no_ovf] at hok_pos
        obtain rfl := Except.ok.inj hok_pos
        change (zm.toNat : ℚ) * 10 ^ ze = maxRepNat * 10 ^ ze
        rw [h_cusp, maxRep_val]; norm_num
      rw [h_val, hzm_q_eq_maxRep]
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
        nlinarith [hf_gt_half, hf_lt1, hf_nn]
      calc f * 10 ^ ze
          ≤ (maxRepNat + f) * (5 / ((2 ^ 63 + 7 : ℕ) : ℚ)) * 10 ^ ze :=
            mul_le_mul_of_nonneg_right h_final h10ze'_nn
        _ = (maxRepNat + f) * 10 ^ ze * (5 / ((2 ^ 63 + 7 : ℕ) : ℚ)) := by ring
    · have hf_eq : f = 1/2 := hround_zero.mp h_tie
      have h_cusp_val := doRoundUp_value_cusp g zm ze h_zm_eq_maxRep ⟨h_tie, h_zm_odd⟩ loc res_pos hok_pos hres_pos_mant_ne
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
      have h_nr_val := doRoundUp_value_noRoundUp g zm ze h_no_ru (by omega) loc res_pos hok_pos hres_pos_mant_ne
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
          have h_val := doRoundUp_value_noRoundUp g zm ze h_no_ru (by omega) loc res_pos hok_pos hres_pos_mant_ne
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
        have h_val := doRoundUp_value_noRoundUp g zm ze h_no_ru (by omega) loc res_pos hok_pos hres_pos_mant_ne
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

/-! ## Value characterization wrappers with `shouldRoundUp_to_nearest` predicate -/

/-- Round-up fires when `round = 1` or when `round = 0` and mantissa is odd. -/
def Guard.shouldRoundUp_to_nearest (g : Guard) (m : UInt64) : Prop :=
  g.round .to_nearest = 1 ∨ (g.round .to_nearest = 0 ∧ m % 2 = 1)

/-- No round-up: `result.toRat = zm * 10^ze'`. -/
theorem doRoundUp_value_no_roundUp
    (g : Guard) (zm : UInt64) (ze' : Int)
    (h_no_roundUp : ¬ g.shouldRoundUp_to_nearest zm)
    (h_le_maxRep : zm.toNat ≤ maxRep.toNat)
    (loc : String) (res : RoundResult)
    (hok : g.doRoundUp false zm ze' largeRange.min largeRange.max .to_nearest loc = .ok res)
    (h_mant_ne : res.mantissa_ ≠ 0) :
    (res.mantissa_.toNat : ℚ) * 10 ^ res.exponent_ = (zm.toNat : ℚ) * 10 ^ ze' :=
  doRoundUp_value_noRoundUp g zm ze' h_no_roundUp h_le_maxRep loc res hok h_mant_ne

/-- Supremum-tight `to_nearest` bound, handling the empty guard internally (empty ⟹ `f = 0`,
no round-up, exact result, error `0`). Delegates the non-empty case to `…_supTight_ne`. -/
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
  by_cases hg_emp : g.empty = true
  · obtain ⟨hd, hx⟩ : g.digits_ = 0 ∧ g.xbit_ = false := by
      have h : (g.digits_ == 0 && !g.xbit_) = true := hg_emp
      rw [Bool.and_eq_true] at h
      exact ⟨beq_iff_eq.mp h.1, by simpa using h.2⟩
    have hf0 : f = 0 := represents_eq_zero_of_digits_zero_xbit_false hd hx hf_rep
    have h_no_ru : ¬ g.shouldRoundUp_to_nearest zm := by
      unfold Guard.shouldRoundUp_to_nearest
      rw [show g.round .to_nearest = -2 from by unfold Guard.round; rw [if_pos hg_emp]]
      rintro (h | ⟨h, _⟩) <;> exact absurd h (by decide)
    have h_val := doRoundUp_value_no_roundUp g zm ze h_no_ru h_zm_le_max loc res_pos hok_pos
      hres_pos_mant_ne
    rw [h_val, hf0, add_zero, sub_self, abs_zero]
    positivity
  · exact doRoundUp_rounds_to_nearest_supTight_ne g zm ze f hf_rep hg_emp h_zm_ge h_zm_le_max
      h_floor_constraint loc res_pos hok_pos hres_pos_mant_ne

/-- Supremum-tight `to_nearest` bound on the cusp range `maxRep < zm ≤ maxRepUp`
(reachable in the same-sign add path). The cusp clamps to `maxRep` or `maxRepUp`
(gap 3 around a value ≥ 2^63), so the absolute error is `< 3` while the allowance
`(zm+f)·5/(2^63+7)` is `≈ 5`. At `zm = maxRepUp` the round-up path drops a `0`
digit and rescales straight back to `maxRepUp` (the pushed-zero guard always
rounds down), so the error there is just `f < 1`. -/
lemma doRoundUp_rounds_to_nearest_supTight_cusp (g : Guard) (zm : UInt64) (ze : Int) (f : ℚ)
    (hf_rep : represents g f)
    (h_zm_gt : maxRep.toNat < zm.toNat)
    (h_zm_le : zm.toNat ≤ maxRepUp.toNat)
    (loc : String) (res_pos : RoundResult)
    (hok_pos : g.doRoundUp false zm ze largeRange.min largeRange.max .to_nearest loc = .ok res_pos)
    (hres_pos_mant_ne : res_pos.mantissa_ ≠ 0) :
    |(res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_ -
       ((zm.toNat : ℚ) + f) * 10 ^ ze|
      ≤ ((zm.toNat : ℚ) + f) * 10 ^ ze * (5 / (2 ^ 63 + 7 : ℕ)) := by
  have hf_nn := represents_nonneg hf_rep
  have hf_lt1 := represents_lt_one hf_rep
  have h10ze_pos : (0 : ℚ) < (10 : ℚ) ^ ze := zpow_pos (by norm_num) _
  have h10ze_nn : (0 : ℚ) ≤ (10 : ℚ) ^ ze := le_of_lt h10ze_pos
  have h_denom : (((2 ^ 63 + 7 : ℕ)) : ℚ) = 9223372036854775815 := by push_cast; norm_num
  have hzm_ge_nat : (9223372036854775808 : ℕ) ≤ zm.toNat := by
    rw [maxRep_val] at h_zm_gt; omega
  have hzm_ge_q : (9223372036854775808 : ℚ) ≤ (zm.toNat : ℚ) := by exact_mod_cast hzm_ge_nat
  -- A reusable closer: |E| ≤ 3 suffices against the allowance.
  have h_close : ∀ E : ℚ, |E| ≤ 3 →
      |E| * (10 : ℚ) ^ ze ≤ ((zm.toNat : ℚ) + f) * 10 ^ ze * (5 / ((2 ^ 63 + 7 : ℕ) : ℚ)) := by
    intro E hE
    rw [h_denom]
    rw [show ((zm.toNat : ℚ) + f) * 10 ^ ze * (5 / 9223372036854775815)
          = (5 * ((zm.toNat : ℚ) + f) / 9223372036854775815) * 10 ^ ze from by ring]
    apply mul_le_mul_of_nonneg_right _ h10ze_nn
    rw [le_div_iff₀ (by norm_num : (0 : ℚ) < 9223372036854775815)]
    nlinarith [hE, hzm_ge_q, hf_nn]
  unfold Guard.doRoundUp Guard.bringIntoRange at hok_pos
  simp only [Guard.doDropDigit] at hok_pos
  by_cases h_eq_up : zm = maxRepUp
  · -- zm = maxRepUp: pushOverflow no-op; both round paths land on value maxRepUp · 10^ze.
    subst h_eq_up
    have h_pof : g.pushOverflow maxRepUp .to_nearest = g := by
      unfold Guard.pushOverflow
      rw [if_neg]
      intro ⟨_, h⟩
      exact absurd (UInt64.lt_iff_toNat_lt.mp h) (lt_irrefl _)
    rw [h_pof] at hok_pos
    have h_not_noncusp : ¬ (maxRepUp < largeRange.max ∧ maxRepUp < maxRep) := by decide
    have h_not_cusp : ¬ (maxRep < maxRepUp ∧ maxRepUp < maxRepUp) := by decide
    have hup_q : ((maxRepUp.toNat : ℕ) : ℚ) = 9223372036854775810 := by
      rw [show maxRepUp.toNat = maxRepUpNat from rfl]; norm_num
    by_cases h_ru : (g.round .to_nearest == 1 || (g.round .to_nearest == 0 && maxRepUp % 2 == 1)) = true
    · -- round-up: drop-digit path; pushed-zero guard always rounds down.
      rw [show (g.round .to_nearest == 1 || (g.round .to_nearest == 0 && maxRepUp % 2 == 1)) = true
          from h_ru] at hok_pos
      rw [if_pos rfl, if_neg h_not_noncusp, if_neg h_not_cusp] at hok_pos
      have hdig : (g.push (maxRepUp % 10)).digits_.toNat < 5764607523034234880 := by
        have h := toNat_push_digits g (maxRepUp % 10)
        have h0 : (maxRepUp % 10).toNat = 0 := by decide
        rw [h0] at h
        have hlt : g.digits_.toNat < 2 ^ 64 := by
          have hsz := UInt64.toNat_lt_size g.digits_
          rwa [show UInt64.size = 2 ^ 64 from rfl] at hsz
        omega
      have h_ru'_false : ((g.push (maxRepUp % 10)).round .to_nearest == 1
          || ((g.push (maxRepUp % 10)).round .to_nearest == 0 && (maxRepUp / 10) % 2 == 1)) = false := by
        by_cases hemp : (g.push (maxRepUp % 10)).empty = true
        · rw [show (g.push (maxRepUp % 10)).round .to_nearest = -2 from by
            unfold Guard.round; rw [if_pos hemp]]
          rfl
        · rw [round_to_nearest_def hemp]
          have h5 : (0x5000_0000_0000_0000 : UInt64).toNat = 5764607523034234880 := by decide
          have h_not_gt : ¬ ((g.push (maxRepUp % 10)).digits_ > 0x5000_0000_0000_0000) := by
            intro h
            have := UInt64.lt_iff_toNat_lt.mp h
            rw [h5] at this; omega
          have h_lt : (g.push (maxRepUp % 10)).digits_ < 0x5000_0000_0000_0000 := by
            rw [UInt64.lt_iff_toNat_lt, h5]; exact hdig
          rw [if_neg h_not_gt, if_pos h_lt]
          rfl
      rw [show ((g.push (maxRepUp % 10)).round .to_nearest == 1
          || ((g.push (maxRepUp % 10)).round .to_nearest == 0 && (maxRepUp / 10) % 2 == 1)) = false
          from h_ru'_false] at hok_pos
      simp only [Bool.false_eq_true, if_false] at hok_pos
      have h_resc : maxRepUp / 10 < largeRange.min ∧ maxRepUp / 10 ≠ 0 := by decide
      rw [if_pos h_resc] at hok_pos
      simp only [] at hok_pos
      by_cases h_under : ze + 1 - 1 < minExponent ∨ (maxRepUp / 10) * 10 = 0
      · exfalso; apply hres_pos_mant_ne
        simp only [if_pos h_under] at hok_pos
        have hzexp : ¬ ((-2147483648 : Int) > maxExponent) := by norm_num [maxExponent]
        simp only [hzexp, if_false] at hok_pos
        exact (Except.ok.inj hok_pos).symm ▸ rfl
      · push_neg at h_under
        obtain ⟨hexp, -⟩ := h_under
        have h_not_under : ¬ (ze + 1 - 1 < minExponent ∨ (maxRepUp / 10) * 10 = 0) := by
          push_neg; exact ⟨hexp, by decide⟩
        simp only [if_neg h_not_under] at hok_pos
        have h_no_ovf : ¬ (ze + 1 - 1 > maxExponent) := by
          intro h_ovf; simp only [if_pos h_ovf] at hok_pos; simp at hok_pos
        simp only [if_neg h_no_ovf] at hok_pos
        obtain rfl := Except.ok.inj hok_pos
        change |(((maxRepUp / 10) * 10).toNat : ℚ) * 10 ^ (ze + 1 - 1) -
            ((maxRepUp.toNat : ℚ) + f) * 10 ^ ze|
          ≤ ((maxRepUp.toNat : ℚ) + f) * 10 ^ ze * (5 / ((2 ^ 63 + 7 : ℕ) : ℚ))
        rw [show ((maxRepUp / 10) * 10).toNat = 9223372036854775810 from by decide,
            show (ze + 1 - 1 : ℤ) = ze from by ring, hup_q,
            show (((9223372036854775810 : ℕ)) : ℚ) = (9223372036854775810 : ℚ) from by norm_num]
        rw [show (9223372036854775810 : ℚ) * 10 ^ ze - (9223372036854775810 + f) * 10 ^ ze
              = -(f * 10 ^ ze) from by ring, abs_neg, abs_mul,
            abs_of_nonneg h10ze_nn, abs_of_nonneg hf_nn]
        have hcf := h_close f (by rw [abs_of_nonneg hf_nn]; linarith)
        rw [abs_of_nonneg hf_nn, hup_q] at hcf
        exact hcf
    · -- no round-up: the no-roundUp cusp test fails; keep maxRepUp.
      rw [Bool.not_eq_true] at h_ru
      rw [show (g.round .to_nearest == 1 || (g.round .to_nearest == 0 && maxRepUp % 2 == 1)) = false
          from h_ru] at hok_pos
      simp only [Bool.false_eq_true, if_false] at hok_pos
      rw [if_neg h_not_cusp] at hok_pos
      have h_no_resc : ¬ (maxRepUp < largeRange.min ∧ maxRepUp ≠ 0) := by decide
      rw [if_neg h_no_resc] at hok_pos
      simp only [] at hok_pos
      by_cases h_under : ze < minExponent ∨ (maxRepUp : UInt64) = 0
      · exfalso; apply hres_pos_mant_ne
        simp only [if_pos h_under] at hok_pos
        have hzexp : ¬ ((-2147483648 : Int) > maxExponent) := by norm_num [maxExponent]
        simp only [hzexp, if_false] at hok_pos
        exact (Except.ok.inj hok_pos).symm ▸ rfl
      · push_neg at h_under
        obtain ⟨hexp, -⟩ := h_under
        have h_not_under : ¬ (ze < minExponent ∨ (maxRepUp : UInt64) = 0) := by
          push_neg; exact ⟨hexp, by decide⟩
        simp only [if_neg h_not_under] at hok_pos
        have h_no_ovf : ¬ (ze > maxExponent) := by
          intro h_ovf; simp only [if_pos h_ovf] at hok_pos; simp at hok_pos
        simp only [if_neg h_no_ovf] at hok_pos
        obtain rfl := Except.ok.inj hok_pos
        change |(maxRepUp.toNat : ℚ) * 10 ^ ze - ((maxRepUp.toNat : ℚ) + f) * 10 ^ ze|
          ≤ ((maxRepUp.toNat : ℚ) + f) * 10 ^ ze * (5 / ((2 ^ 63 + 7 : ℕ) : ℚ))
        rw [show (maxRepUp.toNat : ℚ) * 10 ^ ze - ((maxRepUp.toNat : ℚ) + f) * 10 ^ ze
              = -(f * 10 ^ ze) from by ring, abs_neg, abs_mul,
            abs_of_nonneg h10ze_nn, abs_of_nonneg hf_nn]
        have := h_close f (by rw [abs_of_nonneg hf_nn]; linarith)
        rw [abs_of_nonneg hf_nn] at this
        exact this
  · -- maxRep < zm < maxRepUp: clamp to maxRepUp (round-up) or maxRep (no round-up).
    have h_lt_up_nat : zm.toNat < maxRepUp.toNat := by
      have hne : zm.toNat ≠ maxRepUp.toNat := fun h => h_eq_up (UInt64.toNat_inj.mp h)
      omega
    have hzm_le_q : (zm.toNat : ℚ) ≤ 9223372036854775809 := by
      exact_mod_cast (by rw [show maxRepUp.toNat = maxRepUpNat from rfl] at h_lt_up_nat
                        ; omega : zm.toNat ≤ 9223372036854775809)
    have h_m_lt_up : zm < maxRepUp := UInt64.lt_iff_toNat_lt.mpr h_lt_up_nat
    have h_maxRep_lt_m : maxRep < zm := UInt64.lt_iff_toNat_lt.mpr h_zm_gt
    have h_cusp_cond : maxRep < zm ∧ zm < maxRepUp := ⟨h_maxRep_lt_m, h_m_lt_up⟩
    have h_not_noncusp : ¬ (zm < largeRange.max ∧ zm < maxRep) := by
      intro ⟨_, h⟩
      exact absurd (UInt64.lt_iff_toNat_lt.mp h) (by omega)
    by_cases h_ru : ((g.pushOverflow zm .to_nearest).round .to_nearest == 1
        || ((g.pushOverflow zm .to_nearest).round .to_nearest == 0 && zm % 2 == 1)) = true
    · -- round-up: clamp up to maxRepUp.
      rw [show ((g.pushOverflow zm .to_nearest).round .to_nearest == 1
          || ((g.pushOverflow zm .to_nearest).round .to_nearest == 0 && zm % 2 == 1)) = true from h_ru] at hok_pos
      rw [if_pos rfl, if_neg h_not_noncusp, if_pos h_cusp_cond] at hok_pos
      have h_no_resc : ¬ (maxRepUp < largeRange.min ∧ maxRepUp ≠ 0) := by decide
      rw [if_neg h_no_resc] at hok_pos
      simp only [] at hok_pos
      by_cases h_under : ze < minExponent ∨ (maxRepUp : UInt64) = 0
      · exfalso; apply hres_pos_mant_ne
        simp only [if_pos h_under] at hok_pos
        have hzexp : ¬ ((-2147483648 : Int) > maxExponent) := by norm_num [maxExponent]
        simp only [hzexp, if_false] at hok_pos
        exact (Except.ok.inj hok_pos).symm ▸ rfl
      · push_neg at h_under
        obtain ⟨hexp, -⟩ := h_under
        have h_not_under : ¬ (ze < minExponent ∨ (maxRepUp : UInt64) = 0) := by
          push_neg; exact ⟨hexp, by decide⟩
        simp only [if_neg h_not_under] at hok_pos
        have h_no_ovf : ¬ (ze > maxExponent) := by
          intro h_ovf; simp only [if_pos h_ovf] at hok_pos; simp at hok_pos
        simp only [if_neg h_no_ovf] at hok_pos
        obtain rfl := Except.ok.inj hok_pos
        change |(maxRepUp.toNat : ℚ) * 10 ^ ze - ((zm.toNat : ℚ) + f) * 10 ^ ze|
          ≤ ((zm.toNat : ℚ) + f) * 10 ^ ze * (5 / ((2 ^ 63 + 7 : ℕ) : ℚ))
        rw [show (maxRepUp.toNat : ℚ) * 10 ^ ze - ((zm.toNat : ℚ) + f) * 10 ^ ze
              = ((maxRepUp.toNat : ℚ) - (zm.toNat : ℚ) - f) * 10 ^ ze from by ring,
            abs_mul, abs_of_nonneg h10ze_nn]
        apply h_close
        have hup_q : ((maxRepUp.toNat : ℕ) : ℚ) = 9223372036854775810 := by
          rw [show maxRepUp.toNat = maxRepUpNat from rfl]; norm_num
        rw [abs_le]
        constructor
        · rw [hup_q]; linarith
        · rw [hup_q]; linarith
    · -- no round-up: clamp down to maxRep.
      rw [Bool.not_eq_true] at h_ru
      rw [show ((g.pushOverflow zm .to_nearest).round .to_nearest == 1
          || ((g.pushOverflow zm .to_nearest).round .to_nearest == 0 && zm % 2 == 1)) = false from h_ru] at hok_pos
      simp only [Bool.false_eq_true, if_false] at hok_pos
      rw [if_pos h_cusp_cond] at hok_pos
      have h_no_resc : ¬ (maxRep < largeRange.min ∧ maxRep ≠ 0) := by decide
      rw [if_neg h_no_resc] at hok_pos
      simp only [] at hok_pos
      by_cases h_under : ze < minExponent ∨ (maxRep : UInt64) = 0
      · exfalso; apply hres_pos_mant_ne
        simp only [if_pos h_under] at hok_pos
        have hzexp : ¬ ((-2147483648 : Int) > maxExponent) := by norm_num [maxExponent]
        simp only [hzexp, if_false] at hok_pos
        exact (Except.ok.inj hok_pos).symm ▸ rfl
      · push_neg at h_under
        obtain ⟨hexp, -⟩ := h_under
        have h_not_under : ¬ (ze < minExponent ∨ (maxRep : UInt64) = 0) := by
          push_neg; exact ⟨hexp, by decide⟩
        simp only [if_neg h_not_under] at hok_pos
        have h_no_ovf : ¬ (ze > maxExponent) := by
          intro h_ovf; simp only [if_pos h_ovf] at hok_pos; simp at hok_pos
        simp only [if_neg h_no_ovf] at hok_pos
        obtain rfl := Except.ok.inj hok_pos
        change |(maxRep.toNat : ℚ) * 10 ^ ze - ((zm.toNat : ℚ) + f) * 10 ^ ze|
          ≤ ((zm.toNat : ℚ) + f) * 10 ^ ze * (5 / ((2 ^ 63 + 7 : ℕ) : ℚ))
        rw [show (maxRep.toNat : ℚ) * 10 ^ ze - ((zm.toNat : ℚ) + f) * 10 ^ ze
              = ((maxRep.toNat : ℚ) - (zm.toNat : ℚ) - f) * 10 ^ ze from by ring,
            abs_mul, abs_of_nonneg h10ze_nn]
        apply h_close
        have hrep_q : ((maxRep.toNat : ℕ) : ℚ) = 9223372036854775807 := by
          rw [maxRep_val]; norm_num
        rw [abs_le]
        constructor
        · rw [hrep_q]; linarith
        · rw [hrep_q]; linarith

/-- Supremum-tight `to_nearest` bound for `mantissaFloor ≤ zm ≤ maxRepUp`, dispatching
between the in-range bound and the cusp bound. This is the form the same-sign add
bound proofs consume (their drop threshold is `maxRepUp`). -/
lemma doRoundUp_rounds_to_nearest_supTight_upTo_maxRepUp
    (g : Guard) (zm : UInt64) (ze : Int) (f : ℚ)
    (hf_rep : represents g f)
    (h_zm_ge : (mantissaFloor : ℕ) ≤ zm.toNat)
    (h_zm_le_max : zm.toNat ≤ maxRepUp.toNat)
    (h_floor_constraint : zm.toNat = mantissaFloor → (8 : ℚ) / 10 ≤ f)
    (loc : String) (res_pos : RoundResult)
    (hok_pos : g.doRoundUp false zm ze largeRange.min largeRange.max .to_nearest loc = .ok res_pos)
    (hres_pos_mant_ne : res_pos.mantissa_ ≠ 0) :
    |(res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_ -
       ((zm.toNat : ℚ) + f) * 10 ^ ze|
      ≤ ((zm.toNat : ℚ) + f) * 10 ^ ze * (5 / (2 ^ 63 + 7 : ℕ)) := by
  by_cases h_le : zm.toNat ≤ maxRep.toNat
  · exact doRoundUp_rounds_to_nearest_supTight g zm ze f hf_rep h_zm_ge h_le
      h_floor_constraint loc res_pos hok_pos hres_pos_mant_ne
  · push_neg at h_le
    exact doRoundUp_rounds_to_nearest_supTight_cusp g zm ze f hf_rep h_le h_zm_le_max
      loc res_pos hok_pos hres_pos_mant_ne

/-- Round-up without cusp: `result.toRat = (zm + 1) * 10^ze'`. -/
theorem doRoundUp_value_to_nearest_roundUp_noCusp
    (g : Guard) (zm : UInt64) (ze' : Int)
    (h_roundUp : g.shouldRoundUp_to_nearest zm)
    (h_no_cusp : zm.toNat + 1 ≤ maxRep.toNat)
    (loc : String) (res : RoundResult)
    (hok : g.doRoundUp false zm ze' largeRange.min largeRange.max .to_nearest loc = .ok res)
    (h_mant_ne : res.mantissa_ ≠ 0) :
    (res.mantissa_.toNat : ℚ) * 10 ^ res.exponent_ = ((zm.toNat : ℚ) + 1) * 10 ^ ze' :=
  doRoundUp_value_roundUp_noOverflow g zm ze' h_roundUp h_no_cusp loc res hok h_mant_ne

/-- Cusp case (`zm = maxRep`, tie round-up): `result.toRat = maxRepCuspTarget * 10^ze'`. -/
theorem doRoundUp_value_to_nearest_roundUp_cusp
    (g : Guard) (zm : UInt64) (ze' : Int)
    (h_zm_eq_maxRep : zm = maxRep)
    (h_roundUp : g.round .to_nearest = 0 ∧ zm % 2 = 1)
    (loc : String) (res : RoundResult)
    (hok : g.doRoundUp false zm ze' largeRange.min largeRange.max .to_nearest loc = .ok res)
    (h_mant_ne : res.mantissa_ ≠ 0) :
    (res.mantissa_.toNat : ℚ) * 10 ^ res.exponent_ = maxRepCuspTarget * 10 ^ ze' :=
  doRoundUp_value_cusp g zm ze' h_zm_eq_maxRep h_roundUp loc res hok h_mant_ne

/-- Cusp case (`zm = maxRep`, `round = 1`): `result.toRat = maxRep.toNat * 10^ze'`. -/
theorem doRoundUp_value_to_nearest_roundUp_cusp_round1
    (g : Guard) (zm : UInt64) (ze' : Int)
    (h_zm_eq_maxRep : zm = maxRep)
    (h_roundUp : g.round .to_nearest = 1)
    (loc : String) (res : RoundResult)
    (hok : g.doRoundUp false zm ze' largeRange.min largeRange.max .to_nearest loc = .ok res)
    (h_mant_ne : res.mantissa_ ≠ 0) :
    (res.mantissa_.toNat : ℚ) * 10 ^ res.exponent_ = (maxRep.toNat : ℚ) * 10 ^ ze' := by
  subst h_zm_eq_maxRep
  have h_pof : g.pushOverflow maxRep .to_nearest = g.push 3 := by
    unfold Guard.pushOverflow
    rw [if_pos (by decide : maxRep ≤ maxRep ∧ maxRep < maxRepUp)]
    have hbump : (g.round .to_nearest == 1 ||
        (g.round .to_nearest == 0 && maxRep == maxRep + 1)) = true := by simp [h_roundUp]
    have hmid_ne2 : ((maxRep + 1 : UInt64) == maxRep) = false := by decide
    simp only [show (maxRep : UInt64) % 10 < 9 from by decide, if_true,
               show (maxRepUp - maxRep : UInt64) / 2 = 1 from by decide,
               hbump, if_true, hmid_ne2, Bool.false_eq_true, if_false,
               show ((maxRep + 1 : UInt64) - maxRep) = 1 from by decide,
               show (1 * 10 : UInt64) / (maxRepUp - maxRep) = 3 from by decide]
  have h_pof_round : (g.pushOverflow maxRep .to_nearest).round .to_nearest = -1
      ∨ (g.pushOverflow maxRep .to_nearest).round .to_nearest = -2 := by
    rw [h_pof]
    have hdig3 : (g.push 3).digits_.toNat < 5764607523034234880 := by
      have hpd := toNat_push_digits g 3
      have h3 : (3 : UInt64).toNat = 3 := rfl
      rw [h3] at hpd
      have hsize : g.digits_.toNat ≤ 2 ^ 64 - 1 := by
        have := UInt64.toNat_lt_size g.digits_; simp only [UInt64.size] at this; omega
      omega
    have h_push3_lt : (g.push 3).digits_ < 0x5000_0000_0000_0000 := by
      rw [UInt64.lt_iff_toNat_lt]
      have h5000 : (0x5000_0000_0000_0000 : UInt64).toNat = 5764607523034234880 := by decide
      rw [h5000]; exact hdig3
    by_cases hemp : (g.push 3).empty = true
    · right; unfold Guard.round; rw [if_pos hemp]
    · left
      rw [round_to_nearest_def hemp]
      have hlt_nat2 : (g.push 3).digits_.toNat < 5764607523034234880 := by
        rwa [UInt64.lt_iff_toNat_lt, show (0x5000_0000_0000_0000 : UInt64).toNat =
          5764607523034234880 from by decide] at h_push3_lt
      have h_not_gt : ¬ ((g.push 3).digits_ > 0x5000_0000_0000_0000) := by
        simp only [UInt64.lt_iff_toNat_lt] at *; omega
      simp only [if_neg h_not_gt, if_pos h_push3_lt]
  have h_roundup_false :
      ((g.pushOverflow maxRep .to_nearest).round .to_nearest == 1
        || ((g.pushOverflow maxRep .to_nearest).round .to_nearest == 0
            && maxRep % 2 == 1)) = false := by
    rcases h_pof_round with hr | hr <;> simp [hr]
  unfold Guard.doRoundUp Guard.bringIntoRange at hok
  simp only [Guard.doDropDigit] at hok
  rw [h_roundup_false] at hok
  simp only [Bool.false_eq_true, ite_false] at hok
  have h_no_cusp : ¬ (maxRep < maxRep ∧ maxRep < maxRepUp) := by decide
  rw [if_neg h_no_cusp] at hok
  have h_no_resc : ¬ (maxRep < largeRange.min ∧ maxRep ≠ 0) := by decide
  rw [if_neg h_no_resc] at hok
  have h_not_under : ¬ (ze' < minExponent ∨ (maxRep : UInt64) = 0) := by
    push_neg; constructor
    · by_contra hlt
      have hlt' : ze' < minExponent := by omega
      rw [if_pos (Or.inl hlt')] at hok
      have hzexp : ¬ ((-2147483648 : Int) > maxExponent) := by norm_num [maxExponent]
      simp only [if_neg hzexp] at hok
      obtain rfl := Except.ok.inj hok
      exact h_mant_ne rfl
    · decide
  rw [if_neg h_not_under] at hok
  have h_no_ovf : ¬ (ze' > maxExponent) := by
    intro h_ovf; simp only [if_pos h_ovf] at hok; simp at hok
  rw [if_neg h_no_ovf] at hok
  obtain rfl := Except.ok.inj hok
  rfl

end XRPL.Model.Protocol
