import XRPL.Properties.Protocol.Number.Common.Rounding.DoRoundUp.ValueChar

set_option linter.unusedTactic false
set_option linter.unusedSimpArgs false
set_option linter.unreachableTactic false

namespace XRPL.Model.Protocol

/-! ## Mode-generic (any-round) supremum bound -/

/-- Value form of `bringIntoRange` at the live `largeRange.min`: whenever the
output mantissa is nonzero, the output value equals `m * 10^e` exactly (the
rescale branch is a pure `×10 / e−1` shift). -/
lemma bringIntoRange_value_q (zn : Bool) (m : UInt64) (e : Int)
    (hne : (Guard.bringIntoRange zn m e largeRange.min).mantissa_ ≠ 0) :
    ((Guard.bringIntoRange zn m e largeRange.min).mantissa_.toNat : ℚ)
        * 10 ^ (Guard.bringIntoRange zn m e largeRange.min).exponent_
      = (m.toNat : ℚ) * 10 ^ e := by
  have hm_ne : m ≠ 0 := by
    intro h
    apply hne
    have hnresc : ¬ (m < largeRange.min ∧ m ≠ 0) := by
      intro hc
      exact hc.2 h
    rw [bringIntoRange_noscale_result hnresc]
    simp [h]
  by_cases hresc : m < largeRange.min
  · rw [bringIntoRange_rescale_result hresc hm_ne] at hne ⊢
    by_cases h_under : e - 1 < minExponent ∨ m * 10 = 0
    · rw [if_pos h_under] at hne
      exact absurd rfl hne
    · rw [if_neg h_under]
      have hm_mul_10 : (m * 10).toNat = m.toNat * 10 :=
        m_mul_ten_no_overflow (UInt64.lt_iff_toNat_lt.mp hresc)
      change ((m * 10).toNat : ℚ) * 10 ^ (e - 1) = (m.toNat : ℚ) * 10 ^ e
      rw [hm_mul_10]
      push_cast
      rw [show (e - 1 : ℤ) = e + (-1) from by ring,
          zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0), zpow_neg_one]
      field_simp
  · have hnresc : ¬ (m < largeRange.min ∧ m ≠ 0) := by
      intro hc
      exact hresc hc.1
    rw [bringIntoRange_noscale_result hnresc] at hne ⊢
    by_cases h_under : e < minExponent ∨ m = 0
    · rw [if_pos h_under] at hne
      exact absurd rfl hne
    · rw [if_neg h_under]

set_option maxHeartbeats 3200000 in
-- 8-leaf navigation with per-leaf literal arithmetic needs a raised budget.
/-- Mode-generic (any rounding mode, any round decision) relative bound for
`doRoundUp` on `mantissaFloor ≤ zm ≤ maxRepUp`: every leaf — truncate, round-up,
the cusp clamps, and both drop-digit leaves — moves the value by at most `10`
units at a scale `≥ mantissaFloor`, i.e. relatively by at most
`10/(2^63 − 8) = 1/mantissaFloor`. The directed-mode `doNormalize128` bounds
consume this (they cannot relate the round decision to the fraction `f`). -/
lemma doRoundUp_rounds_any_supTight_upTo_maxRepUp
    (g : Guard) (zm : UInt64) (ze : Int) (f : ℚ) (mode : rounding_mode)
    (hf_rep : represents g f)
    (h_zm_ge : (mantissaFloor : ℕ) ≤ zm.toNat)
    (h_zm_le_max : zm.toNat ≤ maxRepUp.toNat)
    (loc : String) (res_pos : RoundResult)
    (hok_pos : g.doRoundUp false zm ze largeRange.min largeRange.max mode loc = .ok res_pos)
    (hres_pos_mant_ne : res_pos.mantissa_ ≠ 0) :
    |(res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_ -
       ((zm.toNat : ℚ) + f) * 10 ^ ze|
      ≤ ((zm.toNat : ℚ) + f) * 10 ^ ze * (10 / (2 ^ 63 - 8 : ℚ)) := by
  have hf_nn : 0 ≤ f := represents_nonneg hf_rep
  have hf_lt : f < 1 := represents_lt_one hf_rep
  have h10ze_pos : (0 : ℚ) < (10 : ℚ) ^ ze := zpow_pos (by norm_num) _
  have h10ze_nn : (0 : ℚ) ≤ (10 : ℚ) ^ ze := le_of_lt h10ze_pos
  have hmaxRepUp_toNat : maxRepUp.toNat = maxRepUpNat := rfl
  have hzm_q : (922337203685477580 : ℚ) ≤ (zm.toNat : ℚ) := by exact_mod_cast h_zm_ge
  have hzm_q_le : (zm.toNat : ℚ) ≤ 9223372036854775810 := by
    rw [hmaxRepUp_toNat] at h_zm_le_max; exact_mod_cast h_zm_le_max
  -- Shared closing step: factor out 10^ze, then bound |X − (zm+f)| ≤ allowance.
  have h_close : ∀ X : ℚ,
      |X - ((zm.toNat : ℚ) + f)| ≤ ((zm.toNat : ℚ) + f) * (10 / (2 ^ 63 - 8 : ℚ)) →
      |X * 10 ^ ze - ((zm.toNat : ℚ) + f) * 10 ^ ze|
        ≤ ((zm.toNat : ℚ) + f) * 10 ^ ze * (10 / (2 ^ 63 - 8 : ℚ)) := by
    intro X hX
    rw [show X * 10 ^ ze - ((zm.toNat : ℚ) + f) * 10 ^ ze
          = (X - ((zm.toNat : ℚ) + f)) * 10 ^ ze from by ring,
        abs_mul, abs_of_nonneg h10ze_nn,
        show ((zm.toNat : ℚ) + f) * 10 ^ ze * (10 / (2 ^ 63 - 8 : ℚ))
          = (((zm.toNat : ℚ) + f) * (10 / (2 ^ 63 - 8 : ℚ))) * 10 ^ ze from by ring]
    exact mul_le_mul_of_nonneg_right hX h10ze_nn
  unfold Guard.doRoundUp at hok_pos
  simp only [Guard.doDropDigit] at hok_pos
  set gP : Guard := g.pushOverflow zm mode with hgP_def
  by_cases hb : ((gP.round mode == 1) || ((gP.round mode == 0) && (zm % 2 == 1))) = true
  · rw [if_pos hb] at hok_pos
    have hlt_max : zm < largeRange.max := by
      rw [UInt64.lt_iff_toNat_lt, largeRange_max_val]
      omega
    by_cases hcusp1 : zm < maxRep
    · -- Leaf C: round-up in range, value (zm+1)·10^ze.
      have hC : zm < largeRange.max ∧ zm < maxRep := ⟨hlt_max, hcusp1⟩
      rw [if_pos hC] at hok_pos
      by_cases h_ovf : (Guard.bringIntoRange false (zm + 1) ze largeRange.min).exponent_ > maxExponent
      · rw [if_pos h_ovf] at hok_pos; simp at hok_pos
      · rw [if_neg h_ovf] at hok_pos
        have hres_eq : res_pos = Guard.bringIntoRange false (zm + 1) ze largeRange.min :=
          (Except.ok.inj hok_pos).symm
        have hval : (res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_
            = (((zm + 1).toNat : ℚ)) * 10 ^ ze := by
          rw [hres_eq]
          exact bringIntoRange_value_q false _ _ (by rw [← hres_eq]; exact hres_pos_mant_ne)
        have hm1 : ((zm + 1).toNat : ℚ) = (zm.toNat : ℚ) + 1 := by
          rw [m_add_one_no_overflow (le_of_lt (UInt64.lt_iff_toNat_lt.mp hcusp1))]
          push_cast; ring
        rw [hval, hm1]
        apply h_close
        rw [show (zm.toNat : ℚ) + 1 - ((zm.toNat : ℚ) + f) = 1 - f from by ring,
            abs_of_nonneg (by linarith : (0 : ℚ) ≤ 1 - f)]
        nlinarith [hzm_q, hf_nn]
    · have hge : maxRep.toNat ≤ zm.toNat := by
        rw [← Nat.not_lt]
        intro h
        exact hcusp1 (UInt64.lt_iff_toNat_lt.mpr h)
      have hnC : ¬ (zm < largeRange.max ∧ zm < maxRep) := fun h => hcusp1 h.2
      clear hcusp1
      rw [if_neg hnC] at hok_pos
      by_cases hcuspI : maxRep < zm ∧ zm < maxRepUp
      · -- Leaf D: round-up cusp clamp, value maxRepUp·10^ze.
        rw [if_pos hcuspI] at hok_pos
        by_cases h_ovf : (Guard.bringIntoRange false maxRepUp ze largeRange.min).exponent_ > maxExponent
        · rw [if_pos h_ovf] at hok_pos; simp at hok_pos
        · rw [if_neg h_ovf] at hok_pos
          have hres_eq : res_pos = Guard.bringIntoRange false maxRepUp ze largeRange.min :=
            (Except.ok.inj hok_pos).symm
          have hval : (res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_
              = ((maxRepUp.toNat : ℚ)) * 10 ^ ze := by
            rw [hres_eq]
            exact bringIntoRange_value_q false _ _ (by rw [← hres_eq]; exact hres_pos_mant_ne)
          have hzm_gt : (9223372036854775807 : ℚ) < (zm.toNat : ℚ) := by
            have := UInt64.lt_iff_toNat_lt.mp hcuspI.1
            rw [maxRep_val] at this
            exact_mod_cast this
          have hzm_lt_up : (zm.toNat : ℚ) ≤ 9223372036854775809 := by
            have h := UInt64.lt_iff_toNat_lt.mp hcuspI.2
            rw [hmaxRepUp_toNat] at h
            have h2 : zm.toNat ≤ 9223372036854775809 := by omega
            exact_mod_cast h2
          rw [hval, show (maxRepUp.toNat : ℚ) = 9223372036854775810 from by
            rw [hmaxRepUp_toNat]; norm_num]
          apply h_close
          rw [abs_of_nonneg (by linarith : (0 : ℚ) ≤ 9223372036854775810 - ((zm.toNat : ℚ) + f))]
          nlinarith [hzm_gt, hf_nn]
      · -- Drop leaves: zm = maxRep or zm = maxRepUp.
        rw [if_neg hcuspI] at hok_pos
        have hzm_cases : zm = maxRep ∨ zm = maxRepUp := by
          have h2 : ¬ (maxRep.toNat < zm.toNat ∧ zm.toNat < maxRepUp.toNat) := fun ⟨a, b⟩ =>
            hcuspI ⟨UInt64.lt_iff_toNat_lt.mpr a, UInt64.lt_iff_toNat_lt.mpr b⟩
          rcases (by omega : zm.toNat = maxRep.toNat ∨ zm.toNat = maxRepUp.toNat) with h | h
          · exact Or.inl (UInt64.toNat_inj.mp h)
          · exact Or.inr (UInt64.toNat_inj.mp h)
        rcases hzm_cases with hzm_eq | hzm_eq
        · -- Leaf E1: zm = maxRep; drop to (floor, ze+1), round again.
          subst hzm_eq
          by_cases hb' : (((gP.push (maxRep % 10)).round mode == 1)
              || (((gP.push (maxRep % 10)).round mode == 0) && (maxRep / 10 % 2 == 1))) = true
          · rw [if_pos hb'] at hok_pos
            by_cases h_ovf : (Guard.bringIntoRange false (maxRep / 10 + 1) (ze + 1) largeRange.min).exponent_ > maxExponent
            · rw [if_pos h_ovf] at hok_pos; simp at hok_pos
            · rw [if_neg h_ovf] at hok_pos
              have hres_eq : res_pos = Guard.bringIntoRange false (maxRep / 10 + 1) (ze + 1) largeRange.min :=
                (Except.ok.inj hok_pos).symm
              have hval : (res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_
                  = (((maxRep / 10 + 1).toNat : ℚ)) * 10 ^ (ze + 1) := by
                rw [hres_eq]
                exact bringIntoRange_value_q false _ _ (by rw [← hres_eq]; exact hres_pos_mant_ne)
              have hlit : ((maxRep / 10 + 1).toNat : ℚ) = 922337203685477581 := by
                rw [show (maxRep / 10 + 1).toNat = 922337203685477581 from by decide]
                norm_num
              have hmaxRep_q : (maxRep.toNat : ℚ) = 9223372036854775807 := by
                rw [maxRep_val]; norm_num
              rw [hval, hlit, show (10 : ℚ) ^ (ze + 1) = 10 ^ ze * 10 from by
                rw [zpow_add_one₀ (by norm_num : (10 : ℚ) ≠ 0)],
                show (922337203685477581 : ℚ) * (10 ^ ze * 10) = 9223372036854775810 * 10 ^ ze from by ring]
              apply h_close
              rw [hmaxRep_q,
                  abs_of_nonneg (by linarith : (0 : ℚ) ≤ 9223372036854775810 - ((9223372036854775807 : ℚ) + f))]
              nlinarith [hf_nn]
          · rw [if_neg hb'] at hok_pos
            by_cases h_ovf : (Guard.bringIntoRange false (maxRep / 10) (ze + 1) largeRange.min).exponent_ > maxExponent
            · rw [if_pos h_ovf] at hok_pos; simp at hok_pos
            · rw [if_neg h_ovf] at hok_pos
              have hres_eq : res_pos = Guard.bringIntoRange false (maxRep / 10) (ze + 1) largeRange.min :=
                (Except.ok.inj hok_pos).symm
              have hval : (res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_
                  = (((maxRep / 10).toNat : ℚ)) * 10 ^ (ze + 1) := by
                rw [hres_eq]
                exact bringIntoRange_value_q false _ _ (by rw [← hres_eq]; exact hres_pos_mant_ne)
              have hlit : ((maxRep / 10).toNat : ℚ) = 922337203685477580 := by
                rw [show (maxRep / 10).toNat = 922337203685477580 from by decide]
                norm_num
              have hmaxRep_q : (maxRep.toNat : ℚ) = 9223372036854775807 := by
                rw [maxRep_val]; norm_num
              rw [hval, hlit, show (10 : ℚ) ^ (ze + 1) = 10 ^ ze * 10 from by
                rw [zpow_add_one₀ (by norm_num : (10 : ℚ) ≠ 0)],
                show (922337203685477580 : ℚ) * (10 ^ ze * 10) = 9223372036854775800 * 10 ^ ze from by ring]
              apply h_close
              rw [hmaxRep_q,
                  abs_of_nonpos (by linarith : (9223372036854775800 : ℚ) - ((9223372036854775807 : ℚ) + f) ≤ 0)]
              nlinarith [hf_nn, hf_lt]
        · -- Leaf E2: zm = maxRepUp; drop to (maxRepUp/10, ze+1), round again.
          subst hzm_eq
          by_cases hb' : (((gP.push (maxRepUp % 10)).round mode == 1)
              || (((gP.push (maxRepUp % 10)).round mode == 0) && (maxRepUp / 10 % 2 == 1))) = true
          · rw [if_pos hb'] at hok_pos
            by_cases h_ovf : (Guard.bringIntoRange false (maxRepUp / 10 + 1) (ze + 1) largeRange.min).exponent_ > maxExponent
            · rw [if_pos h_ovf] at hok_pos; simp at hok_pos
            · rw [if_neg h_ovf] at hok_pos
              have hres_eq : res_pos = Guard.bringIntoRange false (maxRepUp / 10 + 1) (ze + 1) largeRange.min :=
                (Except.ok.inj hok_pos).symm
              have hval : (res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_
                  = (((maxRepUp / 10 + 1).toNat : ℚ)) * 10 ^ (ze + 1) := by
                rw [hres_eq]
                exact bringIntoRange_value_q false _ _ (by rw [← hres_eq]; exact hres_pos_mant_ne)
              have hlit : ((maxRepUp / 10 + 1).toNat : ℚ) = 922337203685477582 := by
                rw [show (maxRepUp / 10 + 1).toNat = 922337203685477582 from by decide]
                norm_num
              have hmaxRepUp_q : (maxRepUp.toNat : ℚ) = 9223372036854775810 := by
                rw [hmaxRepUp_toNat]; norm_num
              rw [hval, hlit, show (10 : ℚ) ^ (ze + 1) = 10 ^ ze * 10 from by
                rw [zpow_add_one₀ (by norm_num : (10 : ℚ) ≠ 0)],
                show (922337203685477582 : ℚ) * (10 ^ ze * 10) = 9223372036854775820 * 10 ^ ze from by ring]
              apply h_close
              rw [hmaxRepUp_q,
                  abs_of_nonneg (by linarith : (0 : ℚ) ≤ 9223372036854775820 - ((9223372036854775810 : ℚ) + f))]
              nlinarith [hf_nn]
          · rw [if_neg hb'] at hok_pos
            by_cases h_ovf : (Guard.bringIntoRange false (maxRepUp / 10) (ze + 1) largeRange.min).exponent_ > maxExponent
            · rw [if_pos h_ovf] at hok_pos; simp at hok_pos
            · rw [if_neg h_ovf] at hok_pos
              have hres_eq : res_pos = Guard.bringIntoRange false (maxRepUp / 10) (ze + 1) largeRange.min :=
                (Except.ok.inj hok_pos).symm
              have hval : (res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_
                  = (((maxRepUp / 10).toNat : ℚ)) * 10 ^ (ze + 1) := by
                rw [hres_eq]
                exact bringIntoRange_value_q false _ _ (by rw [← hres_eq]; exact hres_pos_mant_ne)
              have hlit : ((maxRepUp / 10).toNat : ℚ) = 922337203685477581 := by
                rw [show (maxRepUp / 10).toNat = 922337203685477581 from by decide]
                norm_num
              have hmaxRepUp_q : (maxRepUp.toNat : ℚ) = 9223372036854775810 := by
                rw [hmaxRepUp_toNat]; norm_num
              rw [hval, hlit, show (10 : ℚ) ^ (ze + 1) = 10 ^ ze * 10 from by
                rw [zpow_add_one₀ (by norm_num : (10 : ℚ) ≠ 0)],
                show (922337203685477581 : ℚ) * (10 ^ ze * 10) = 9223372036854775810 * 10 ^ ze from by ring]
              apply h_close
              rw [hmaxRepUp_q,
                  show (9223372036854775810 : ℚ) - ((9223372036854775810 : ℚ) + f) = -f from by ring,
                  abs_neg, abs_of_nonneg hf_nn]
              nlinarith [hf_nn, hf_lt]
  · rw [if_neg hb] at hok_pos
    by_cases hcuspI : maxRep < zm ∧ zm < maxRepUp
    · -- Leaf B: truncate cusp clamp, value maxRep·10^ze.
      rw [if_pos hcuspI] at hok_pos
      by_cases h_ovf : (Guard.bringIntoRange false maxRep ze largeRange.min).exponent_ > maxExponent
      · rw [if_pos h_ovf] at hok_pos; simp at hok_pos
      · rw [if_neg h_ovf] at hok_pos
        have hres_eq : res_pos = Guard.bringIntoRange false maxRep ze largeRange.min :=
          (Except.ok.inj hok_pos).symm
        have hval : (res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_
            = ((maxRep.toNat : ℚ)) * 10 ^ ze := by
          rw [hres_eq]
          exact bringIntoRange_value_q false _ _ (by rw [← hres_eq]; exact hres_pos_mant_ne)
        have hzm_gt : (9223372036854775807 : ℚ) < (zm.toNat : ℚ) := by
          have := UInt64.lt_iff_toNat_lt.mp hcuspI.1
          rw [maxRep_val] at this
          exact_mod_cast this
        rw [hval, show (maxRep.toNat : ℚ) = 9223372036854775807 from by rw [maxRep_val]; norm_num]
        apply h_close
        rw [abs_of_nonpos (by linarith : (9223372036854775807 : ℚ) - ((zm.toNat : ℚ) + f) ≤ 0)]
        nlinarith [hzm_gt, hzm_q_le, hf_nn, hf_lt]
    · -- Leaf A: plain truncate, value zm·10^ze.
      rw [if_neg hcuspI] at hok_pos
      by_cases h_ovf : (Guard.bringIntoRange false zm ze largeRange.min).exponent_ > maxExponent
      · rw [if_pos h_ovf] at hok_pos; simp at hok_pos
      · rw [if_neg h_ovf] at hok_pos
        have hres_eq : res_pos = Guard.bringIntoRange false zm ze largeRange.min :=
          (Except.ok.inj hok_pos).symm
        have hval : (res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_
            = ((zm.toNat : ℚ)) * 10 ^ ze := by
          rw [hres_eq]
          exact bringIntoRange_value_q false _ _ (by rw [← hres_eq]; exact hres_pos_mant_ne)
        rw [hval]
        apply h_close
        rw [show (zm.toNat : ℚ) - ((zm.toNat : ℚ) + f) = -f from by ring,
            abs_neg, abs_of_nonneg hf_nn]
        nlinarith [hzm_q, hf_nn, hf_lt]

/-- At a cusp-interior mantissa `pushOverflow` pushes a positive digit
(`(zm − maxRep)·10/3 ∈ {3, 6}`), so the pushed guard has nonzero digit content;
the sign bit is preserved. -/
lemma pushOverflow_cusp_interior_facts (g : Guard) (zm : UInt64) (mode : rounding_mode)
    (h_lb : maxRep.toNat < zm.toNat) (h_ub : zm.toNat < maxRepUp.toNat) :
    (g.pushOverflow zm mode).digits_ > 0 ∧ (g.pushOverflow zm mode).sbit_ = g.sbit_ := by
  have hmr_u : maxRep < zm := UInt64.lt_iff_toNat_lt.mpr h_lb
  have hlt_u : zm < maxRepUp := UInt64.lt_iff_toNat_lt.mpr h_ub
  have hdiff_bound : zm.toNat = maxRep.toNat + 1 ∨ zm.toNat = maxRep.toNat + 2 := by
    rw [maxRep_val] at h_lb; rw [show maxRepUp.toNat = maxRepUpNat from rfl] at h_ub
    rw [maxRep_val]; omega
  have hdiff : (zm - maxRep).toNat = zm.toNat - maxRep.toNat :=
    UInt64.toNat_sub_of_le _ _ (UInt64.le_iff_toNat_le.mpr (Nat.le_of_lt h_lb))
  have hzm1_toNat : (zm + 1).toNat = zm.toNat + 1 := by
    rw [UInt64.toNat_add, show (1 : UInt64).toNat = 1 from rfl]
    apply Nat.mod_eq_of_lt
    have := zm.toNat_lt_size
    rw [show maxRepUp.toNat = maxRepUpNat from rfl] at h_ub
    simp only [UInt64.size] at this; omega
  have hne : zm ≠ maxRep := by rintro rfl; simp [maxRep_val] at h_lb
  have hne1 : zm + 1 ≠ maxRep := by
    rintro heq
    have := congrArg UInt64.toNat heq
    rw [hzm1_toNat] at this; omega
  unfold Guard.pushOverflow
  rw [if_pos ⟨UInt64.le_of_lt hmr_u, hlt_u⟩]
  by_cases h9 : zm % (10 : UInt64) < 9
  · have hzm_val : zm.toNat = maxRep.toNat + 1 := by
      rcases hdiff_bound with h | h
      · exact h
      · exfalso
        have : zm.toNat % 10 = 9 := by rw [h, maxRep_val]
        have hmod : (zm % 10).toNat = zm.toNat % 10 := UInt64.toNat_mod _ _
        exact absurd (UInt64.lt_iff_toNat_lt.mp h9) (by rw [hmod, this]; decide)
    have hmid_eq : maxRep + (maxRepUp - maxRep) / 2 = zm := by
      apply UInt64.toNat_inj.mp; rw [hzm_val, maxRep_val]; decide
    simp only [if_pos h9]
    rw [hmid_eq]
    by_cases hbump : (g.round mode == 1 ||
        (g.round mode == 0 && zm == zm)) = true
    · rw [if_pos hbump]
      simp only [show (zm + 1 == maxRep) = false from beq_eq_false_iff_ne.mpr hne1,
                 Bool.false_eq_true, ite_false]
      constructor
      · change (0 : UInt64) < (g.push _).digits_
        rw [UInt64.lt_iff_toNat_lt]
        rw [toNat_push_digits]
        have hsub : (zm + 1 - maxRep).toNat = 2 := by
          rw [UInt64.toNat_sub_of_le _ _
              (by rw [UInt64.le_iff_toNat_le, hzm1_toNat, hzm_val, maxRep_val]; norm_num)]
          rw [hzm1_toNat, hzm_val, maxRep_val]
        rw [UInt64.toNat_div, show (maxRepUp - maxRep).toNat = 3 from by decide,
            UInt64.toNat_mul, show (10 : UInt64).toNat = 10 from rfl, hsub]
        norm_num
      · rfl
    · rw [Bool.not_eq_true] at hbump
      simp only [hbump, Bool.false_eq_true, ite_false,
                 show (zm == maxRep) = false from beq_eq_false_iff_ne.mpr hne]
      constructor
      · change (0 : UInt64) < (g.push _).digits_
        rw [UInt64.lt_iff_toNat_lt, toNat_push_digits]
        rw [UInt64.toNat_div, show (maxRepUp - maxRep).toNat = 3 from by decide,
            UInt64.toNat_mul, show (10 : UInt64).toNat = 10 from rfl, hdiff, hzm_val, maxRep_val]
        norm_num
      · rfl
  · have hzm_val : zm.toNat = maxRep.toNat + 2 := by
      rcases hdiff_bound with h | h
      · exfalso
        have : zm.toNat % 10 = 8 := by rw [h, maxRep_val]
        have hmod : (zm % 10).toNat = zm.toNat % 10 := UInt64.toNat_mod _ _
        exact h9 (UInt64.lt_iff_toNat_lt.mpr (by rw [hmod, this]; decide))
      · exact h
    simp only [if_neg h9]
    simp only [show (zm == maxRep) = false from beq_eq_false_iff_ne.mpr hne,
               Bool.false_eq_true, ite_false]
    constructor
    · change (0 : UInt64) < (g.push _).digits_
      rw [UInt64.lt_iff_toNat_lt, toNat_push_digits]
      rw [UInt64.toNat_div, show (maxRepUp - maxRep).toNat = 3 from by decide,
          UInt64.toNat_mul, show (10 : UInt64).toNat = 10 from rfl, hdiff, hzm_val, maxRep_val]
      norm_num
    · rfl

set_option maxHeartbeats 1600000 in
-- Cusp-range navigation (6 leaves) with literal arithmetic needs a raised budget.
/-- Value cases for `doRoundUp` on the cusp range `maxRep < zm ≤ maxRepUp`,
mode-generic: the output value is `maxRep`, `maxRepUp` (= `maxRepNat + 3`), or —
only at `zm = maxRepUp` with the round decision fired — `maxRepNat + 13`, each at
scale `10^ze`. The same-sign directed `BoundProof`s consume this; the
disjuncts' couplings let them refute or absorb each leaf per mode and sign.

The `+3` disjunct distinguishes its two sources: at `zm = maxRepUp` the value is
kept either because the first round decision was dead or because the
dropped-digit re-round was dead (in directed modes either one forces an empty
guard content for the rounding-away sign, hence an exact result); at
`zm < maxRepUp` it is the fired cusp-interior clamp. -/
lemma doRoundUp_value_cuspRange_cases
    (g : Guard) (zm : UInt64) (ze : Int) (mode : rounding_mode)
    (h_lb : maxRep.toNat < zm.toNat)
    (h_ub : zm.toNat ≤ maxRepUp.toNat)
    (loc : String) (res_pos : RoundResult)
    (hok_pos : g.doRoundUp false zm ze largeRange.min largeRange.max mode loc = .ok res_pos)
    (hres_ne : res_pos.mantissa_ ≠ 0) :
    ∃ v : ℚ, (res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_ = v * 10 ^ ze
      ∧ ((v = maxRepNat ∧ zm.toNat < maxRepUp.toNat
            ∧ (((g.pushOverflow zm mode).round mode == 1)
               || (((g.pushOverflow zm mode).round mode == 0) && (zm % 2 == 1))) = false)
         ∨ (v = maxRepNat + 3 ∧
              ((zm.toNat = maxRepUp.toNat
                  ∧ (((g.round mode == 1) || ((g.round mode == 0) && (zm % 2 == 1))) = false
                     ∨ (((g.push (maxRepUp % 10)).round mode == 1)
                        || (((g.push (maxRepUp % 10)).round mode == 0) && (maxRepUp / 10 % 2 == 1))) = false))
               ∨ (zm.toNat < maxRepUp.toNat
                  ∧ (((g.pushOverflow zm mode).round mode == 1)
                     || (((g.pushOverflow zm mode).round mode == 0) && (zm % 2 == 1))) = true)))
         ∨ (v = maxRepNat + 13 ∧ zm.toNat = maxRepUp.toNat
            ∧ ((g.round mode == 1) || ((g.round mode == 0) && (zm % 2 == 1))) = true)) := by
  have hmaxRepUp_toNat : maxRepUp.toNat = maxRepUpNat := rfl
  unfold Guard.doRoundUp at hok_pos
  simp only [Guard.doDropDigit] at hok_pos
  set gP : Guard := g.pushOverflow zm mode with hgP_def
  have hncusp1 : ¬ zm < maxRep := by
    rw [UInt64.lt_iff_toNat_lt]
    omega
  have hnC : ¬ (zm < largeRange.max ∧ zm < maxRep) := fun h => hncusp1 h.2
  by_cases hb : ((gP.round mode == 1) || ((gP.round mode == 0) && (zm % 2 == 1))) = true
  · rw [if_pos hb, if_neg hnC] at hok_pos
    by_cases hcuspI : maxRep < zm ∧ zm < maxRepUp
    · -- cusp-interior round-up → maxRepUp.
      rw [if_pos hcuspI] at hok_pos
      by_cases h_ovf : (Guard.bringIntoRange false maxRepUp ze largeRange.min).exponent_ > maxExponent
      · rw [if_pos h_ovf] at hok_pos; simp at hok_pos
      · rw [if_neg h_ovf] at hok_pos
        have hres_eq : res_pos = Guard.bringIntoRange false maxRepUp ze largeRange.min :=
          (Except.ok.inj hok_pos).symm
        have hval := bringIntoRange_value_q false maxRepUp ze (by rw [← hres_eq]; exact hres_ne)
        refine ⟨maxRepNat + 3, ?_,
          Or.inr (Or.inl ⟨rfl, Or.inr ⟨UInt64.lt_iff_toNat_lt.mp hcuspI.2, hb⟩⟩)⟩
        rw [hres_eq, hval, show (maxRepUp.toNat : ℚ) = maxRepNat + 3 from by
          rw [hmaxRepUp_toNat]; norm_num]
    · -- zm = maxRepUp: drop leaf.
      have hzm_eq : zm = maxRepUp := by
        have h2 : ¬ (maxRep.toNat < zm.toNat ∧ zm.toNat < maxRepUp.toNat) := fun ⟨a, b⟩ =>
          hcuspI ⟨UInt64.lt_iff_toNat_lt.mpr a, UInt64.lt_iff_toNat_lt.mpr b⟩
        exact UInt64.toNat_inj.mp (by omega)
      subst hzm_eq
      have hgP_eq : gP = g := by
        rw [hgP_def]
        unfold Guard.pushOverflow
        rw [if_neg]
        intro ⟨_, h⟩
        exact absurd (UInt64.lt_iff_toNat_lt.mp h) (lt_irrefl _)
      rw [if_neg hcuspI] at hok_pos
      by_cases hb' : (((gP.push (maxRepUp % 10)).round mode == 1)
          || (((gP.push (maxRepUp % 10)).round mode == 0) && (maxRepUp / 10 % 2 == 1))) = true
      · rw [if_pos hb'] at hok_pos
        by_cases h_ovf : (Guard.bringIntoRange false (maxRepUp / 10 + 1) (ze + 1) largeRange.min).exponent_ > maxExponent
        · rw [if_pos h_ovf] at hok_pos; simp at hok_pos
        · rw [if_neg h_ovf] at hok_pos
          have hres_eq : res_pos = Guard.bringIntoRange false (maxRepUp / 10 + 1) (ze + 1) largeRange.min :=
            (Except.ok.inj hok_pos).symm
          have hval := bringIntoRange_value_q false (maxRepUp / 10 + 1) (ze + 1)
            (by rw [← hres_eq]; exact hres_ne)
          refine ⟨maxRepNat + 13, ?_, Or.inr (Or.inr ⟨rfl, rfl, by rw [← hgP_eq]; exact hb⟩)⟩
          rw [hres_eq, hval,
              show ((maxRepUp / 10 + 1).toNat : ℚ) = 922337203685477582 from by
                rw [show (maxRepUp / 10 + 1).toNat = 922337203685477582 from by decide]; norm_num,
              show (10 : ℚ) ^ (ze + 1) = 10 ^ ze * 10 from by
                rw [zpow_add_one₀ (by norm_num : (10 : ℚ) ≠ 0)],
              show (maxRepNat : ℚ) + 13 = 9223372036854775820 from by norm_num]
          ring
      · rw [if_neg hb'] at hok_pos
        by_cases h_ovf : (Guard.bringIntoRange false (maxRepUp / 10) (ze + 1) largeRange.min).exponent_ > maxExponent
        · rw [if_pos h_ovf] at hok_pos; simp at hok_pos
        · rw [if_neg h_ovf] at hok_pos
          have hres_eq : res_pos = Guard.bringIntoRange false (maxRepUp / 10) (ze + 1) largeRange.min :=
            (Except.ok.inj hok_pos).symm
          have hval := bringIntoRange_value_q false (maxRepUp / 10) (ze + 1)
            (by rw [← hres_eq]; exact hres_ne)
          have hb'_false : (((g.push (maxRepUp % 10)).round mode == 1)
              || (((g.push (maxRepUp % 10)).round mode == 0) && (maxRepUp / 10 % 2 == 1))) = false := by
            rw [← hgP_eq]; exact (Bool.not_eq_true _).mp hb'
          refine ⟨maxRepNat + 3, ?_, Or.inr (Or.inl ⟨rfl, Or.inl ⟨rfl, Or.inr hb'_false⟩⟩)⟩
          rw [hres_eq, hval,
              show ((maxRepUp / 10).toNat : ℚ) = 922337203685477581 from by
                rw [show (maxRepUp / 10).toNat = 922337203685477581 from by decide]; norm_num,
              show (10 : ℚ) ^ (ze + 1) = 10 ^ ze * 10 from by
                rw [zpow_add_one₀ (by norm_num : (10 : ℚ) ≠ 0)],
              show (maxRepNat : ℚ) + 3 = 9223372036854775810 from by norm_num]
          ring
  · rw [if_neg hb] at hok_pos
    by_cases hcuspI : maxRep < zm ∧ zm < maxRepUp
    · -- truncate cusp clamp → maxRep.
      rw [if_pos hcuspI] at hok_pos
      by_cases h_ovf : (Guard.bringIntoRange false maxRep ze largeRange.min).exponent_ > maxExponent
      · rw [if_pos h_ovf] at hok_pos; simp at hok_pos
      · rw [if_neg h_ovf] at hok_pos
        have hres_eq : res_pos = Guard.bringIntoRange false maxRep ze largeRange.min :=
          (Except.ok.inj hok_pos).symm
        have hval := bringIntoRange_value_q false maxRep ze (by rw [← hres_eq]; exact hres_ne)
        refine ⟨maxRepNat, ?_,
          Or.inl ⟨rfl, UInt64.lt_iff_toNat_lt.mp hcuspI.2, (Bool.not_eq_true _).mp hb⟩⟩
        rw [hres_eq, hval, show (maxRep.toNat : ℚ) = maxRepNat from by
          rw [maxRep_val]; norm_num]
    · -- zm = maxRepUp truncate-keep → maxRepUp.
      rw [if_neg hcuspI] at hok_pos
      have hzm_eq : zm.toNat = maxRepUp.toNat := by
        have h2 : ¬ (maxRep.toNat < zm.toNat ∧ zm.toNat < maxRepUp.toNat) := fun ⟨a, b⟩ =>
          hcuspI ⟨UInt64.lt_iff_toNat_lt.mpr a, UInt64.lt_iff_toNat_lt.mpr b⟩
        omega
      by_cases h_ovf : (Guard.bringIntoRange false zm ze largeRange.min).exponent_ > maxExponent
      · rw [if_pos h_ovf] at hok_pos; simp at hok_pos
      · rw [if_neg h_ovf] at hok_pos
        have hres_eq : res_pos = Guard.bringIntoRange false zm ze largeRange.min :=
          (Except.ok.inj hok_pos).symm
        have hval := bringIntoRange_value_q false zm ze (by rw [← hres_eq]; exact hres_ne)
        have hgP_eq : gP = g := by
          rw [hgP_def]
          unfold Guard.pushOverflow
          rw [if_neg]
          intro ⟨_, h⟩
          have hlt := UInt64.lt_iff_toNat_lt.mp h
          omega
        have hb_false : ((g.round mode == 1) || ((g.round mode == 0) && (zm % 2 == 1))) = false := by
          rw [← hgP_eq]; exact (Bool.not_eq_true _).mp hb
        refine ⟨maxRepNat + 3, ?_, Or.inr (Or.inl ⟨rfl, Or.inl ⟨hzm_eq, Or.inl hb_false⟩⟩)⟩
        rw [hres_eq, hval, show ((zm.toNat : ℕ) : ℚ) = maxRepNat + 3 from by
          rw [hzm_eq, hmaxRepUp_toNat]; norm_num]

/-! ## `.downward` mode -/

/-- For `.downward`, round-up fires iff `sbit_ = true` and guard content is nonzero. -/
def Guard.shouldRoundUp_downward (g : Guard) : Prop :=
  g.sbit_ = true ∧ (g.digits_ > 0 ∨ g.xbit_ = true)

/-- `g.round .downward = 1` iff `g.shouldRoundUp_downward`. -/
lemma round_downward_eq_one_iff (g : Guard) :
    g.round .downward = 1 ↔ g.shouldRoundUp_downward := by
  constructor
  · intro h
    by_cases hemp : g.empty
    · unfold Guard.round at h; rw [if_pos hemp] at h; exact absurd h (by decide)
    · unfold Guard.round at h; rw [if_neg hemp] at h
      by_cases hs : g.sbit_ = true
      · refine ⟨hs, ?_⟩; show g.digits_ > 0 ∨ g.xbit_ = true
        by_cases hcond_d : g.digits_ > 0
        · exact Or.inl hcond_d
        · by_cases hcond_x : g.xbit_ = true
          · exact Or.inr hcond_x
          · exfalso
            have hd_false : decide (g.digits_ > 0) = false := decide_eq_false hcond_d
            have hx_false : g.xbit_ = false := Bool.not_eq_true _ |>.mp hcond_x
            have h_bool_false : (decide (g.digits_ > 0) || g.xbit_) = false := by
              rw [hd_false, hx_false]; rfl
            simp only [hs, if_true, h_bool_false, Bool.false_eq_true, if_false] at h
            exact absurd h (by decide)
      · exfalso
        have hs_false : g.sbit_ = false := Bool.not_eq_true _ |>.mp hs
        simp only [hs_false, Bool.false_eq_true, if_false] at h
        exact absurd h (by decide)
  · intro ⟨hs, hor⟩
    have h_bool_true : (decide (g.digits_ > 0) || g.xbit_) = true := by
      rcases hor with h1 | h2
      · rw [decide_eq_true h1]; rfl
      · rw [h2]; rw [Bool.or_true]
    unfold Guard.round
    by_cases hemp : g.empty
    · -- g.empty: digits_ = 0 and xbit_ = false; shouldRoundUp_downward requires digits > 0 or xbit = true
      exfalso
      unfold Guard.empty at hemp
      simp only [Bool.and_eq_true, beq_iff_eq] at hemp
      obtain ⟨hd0, hx_true⟩ := hemp
      have hd : g.digits_ = 0 := by exact_mod_cast hd0
      have hx : g.xbit_ = false := Bool.eq_false_iff.mpr (by simpa using hx_true)
      rcases hor with h1 | h2
      · simp [hd] at h1
      · simp [hx] at h2
    · rw [if_neg hemp]
      simp only [hs, if_true, h_bool_true, if_true]

-- `round_downward_eq_neg_one_iff` is unused (removed, handled in roundUp_bool_downward_false).

/-- With the sign bit set, a dead `.downward` round decision forces empty guard
content: no packed digits and a clear sticky bit. -/
lemma content_empty_of_not_shouldRoundUp_downward (g : Guard)
    (h_sbit : g.sbit_ = true) (h_nsru : ¬ g.shouldRoundUp_downward) :
    g.digits_ = 0 ∧ g.xbit_ = false := by
  have h_no_or : ¬ (g.digits_ > 0 ∨ g.xbit_ = true) := fun h => h_nsru ⟨h_sbit, h⟩
  have h_digits_not_pos : ¬ g.digits_ > 0 := fun h => h_no_or (Or.inl h)
  have h_xbit : g.xbit_ = false := Bool.not_eq_true _ |>.mp (fun h => h_no_or (Or.inr h))
  refine ⟨?_, h_xbit⟩
  have h_dn : g.digits_.toNat = 0 := by
    by_contra h_ne
    apply h_digits_not_pos
    change (0 : UInt64) < g.digits_
    rw [UInt64.lt_iff_toNat_lt, show (0 : UInt64).toNat = 0 from rfl]
    omega
  rw [← UInt64.toNat_inj, h_dn]; rfl

/-- Bool form of `content_empty_of_not_shouldRoundUp_downward`: with the sign
bit set, a false `roundUp` decision boolean forces empty guard content. -/
lemma roundUp_bool_downward_false_content (g : Guard) (m : UInt64)
    (h_sbit : g.sbit_ = true)
    (hb : ((g.round .downward == 1) || ((g.round .downward == 0) && (m % 2 == 1))) = false) :
    g.digits_ = 0 ∧ g.xbit_ = false := by
  apply content_empty_of_not_shouldRoundUp_downward g h_sbit
  intro h_sru
  have h1 : g.round .downward = 1 := (round_downward_eq_one_iff g).mpr h_sru
  rw [h1, show ((1 : Int) == 1) = true from rfl, Bool.true_or] at hb
  exact Bool.noConfusion hb

/-- For `.downward`, the boolean `roundUp` in `doRoundUp` equals `false`
when `shouldRoundUp_downward` is false. -/
private lemma roundUp_bool_downward_false (g : Guard) (m : UInt64)
    (h_no : ¬ g.shouldRoundUp_downward) :
    ((g.round .downward == 1) || ((g.round .downward == 0) && (m % 2 == 1))) = false := by
  have h_ne1 : g.round .downward ≠ 1 := by
    intro h; exact h_no ((round_downward_eq_one_iff g).mp h)
  have h_ne0 : g.round .downward ≠ 0 := by
    unfold Guard.round; split_ifs <;> simp_all (config := { decide := true })
  simp [beq_iff_eq, h_ne1, h_ne0]

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
    (h_zm_le : zm.toNat ≤ maxRep.toNat)
    (loc : String) (res : RoundResult)
    (hok : g.doRoundUp zn zm ze' largeRange.min largeRange.max .downward loc = .ok res)
    (h_mant_ne : res.mantissa_ ≠ 0) :
    (res.mantissa_.toNat : ℚ) * 10 ^ res.exponent_ = (zm.toNat : ℚ) * 10 ^ ze' := by
  have h_pof : g.pushOverflow zm .downward = g :=
    pushOverflow_noop_of_le_maxRep_of_round_ne_one h_zm_le g .downward
      (fun hr => absurd ((round_downward_eq_one_iff g).mp hr) h_no_roundUp)
  have h_no_cusp : ¬ (maxRep < zm ∧ zm < maxRepUp) := by
    intro ⟨h, _⟩; have := UInt64.lt_iff_toNat_lt.mp h; omega
  have h_ru_false :
      ((g.round .downward == 1) || ((g.round .downward == 0) && (zm % 2 == 1))) = false :=
    roundUp_bool_downward_false g zm h_no_roundUp
  have hres_eq : res = Guard.bringIntoRange zn zm ze' largeRange.min := by
    unfold Guard.doRoundUp at hok
    simp only [Guard.doDropDigit] at hok
    rw [h_pof] at hok
    rw [h_ru_false] at hok
    simp only [Bool.false_eq_true, ite_false] at hok
    rw [if_neg h_no_cusp] at hok
    by_cases h_ovf : (Guard.bringIntoRange zn zm ze' largeRange.min).exponent_ > maxExponent
    · rw [if_pos h_ovf] at hok; simp at hok
    · rw [if_neg h_ovf] at hok; exact (Except.ok.inj hok).symm
  rw [hres_eq]
  have hne' : (Guard.bringIntoRange zn zm ze' largeRange.min).mantissa_ ≠ 0 := hres_eq ▸ h_mant_ne
  -- zm ≠ 0 since bringIntoRange with zm = 0 gives mantissa_ = 0 which contradicts hne'
  have hzm_ne : zm ≠ 0 := by
    intro h
    apply hne'
    rw [bringIntoRange_noscale_result (by intro ⟨_, hne⟩; exact hne h)]
    simp [h]
  by_cases hresc : zm < largeRange.min
  · rw [bringIntoRange_rescale_result hresc hzm_ne]
    have hzm_mul_10 : (zm * 10).toNat = zm.toNat * 10 :=
      m_mul_ten_no_overflow (UInt64.lt_iff_toNat_lt.mp hresc)
    by_cases h_under : ze' - 1 < minExponent ∨ zm * 10 = 0
    · exfalso; apply hne'
      rw [bringIntoRange_rescale_result hresc hzm_ne, if_pos h_under]
    · push_neg at h_under; obtain ⟨hexp, hne10⟩ := h_under
      rw [if_neg (by push_neg; exact ⟨hexp, hne10⟩)]
      change ((zm * 10).toNat : ℚ) * 10 ^ (ze' - 1) = (zm.toNat : ℚ) * 10 ^ ze'
      rw [hzm_mul_10]; push_cast
      rw [show (ze' - 1 : ℤ) = ze' + (-1) from by ring,
          zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0), zpow_neg_one]
      field_simp
  · rw [bringIntoRange_noscale_result (by push_neg; intro h; exact absurd h hresc)]
    by_cases h_under : ze' < minExponent ∨ zm = 0
    · exfalso; apply hne'
      rw [bringIntoRange_noscale_result (by push_neg; intro h; exact absurd h hresc), if_pos h_under]
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
  have h_m_lt_maxRep : zm.toNat < maxRep.toNat := by rw [maxRep_val] at h_no_cusp ⊢; omega
  have h_pof : g.pushOverflow zm .downward = g := pushOverflow_noop_of_lt_maxRep h_m_lt_maxRep g .downward
  have h_ru_true :
      ((g.round .downward == 1) || ((g.round .downward == 0) && (zm % 2 == 1))) = true :=
    roundUp_bool_downward_true g zm h_roundUp
  -- noncusp branch fires: zm < largeRange.max ∧ zm < maxRep
  have h_noncusp_branch : zm < largeRange.max ∧ zm < maxRep := by
    constructor
    · exact UInt64.lt_iff_toNat_lt.mpr (by rw [largeRange_max_val, maxRep_val] at *; omega)
    · exact UInt64.lt_iff_toNat_lt.mpr h_m_lt_maxRep
  have hres_eq : res = Guard.bringIntoRange zn (zm + 1) ze' largeRange.min := by
    unfold Guard.doRoundUp at hok
    simp only [Guard.doDropDigit] at hok
    rw [h_pof] at hok
    rw [h_ru_true] at hok
    rw [if_pos h_noncusp_branch] at hok
    simp only [if_true] at hok
    by_cases h_ovf : (Guard.bringIntoRange zn (zm + 1) ze' largeRange.min).exponent_ > maxExponent
    · rw [if_pos h_ovf] at hok; simp at hok
    · rw [if_neg h_ovf] at hok; exact (Except.ok.inj hok).symm
  rw [hres_eq]
  have hne' : (Guard.bringIntoRange zn (zm + 1) ze' largeRange.min).mantissa_ ≠ 0 := hres_eq ▸ h_mant_ne
  have h_m_le_maxRep : zm.toNat ≤ maxRep.toNat := by omega
  have hm_add1_toNat : (zm + 1).toNat = zm.toNat + 1 := m_add_one_no_overflow h_m_le_maxRep
  have h_m1_ne : zm + 1 ≠ 0 := by
    intro h; have := UInt64.toNat_inj.mpr h; rw [hm_add1_toNat] at this; simp at this
  by_cases hresc : zm + 1 < largeRange.min
  · rw [bringIntoRange_rescale_result hresc h_m1_ne]
    have h_m1_lt_min : (zm + 1).toNat < largeRange.min.toNat := UInt64.lt_iff_toNat_lt.mp hresc
    have h_m1_mul10_toNat : ((zm + 1) * 10).toNat = (zm + 1).toNat * 10 := by
      rw [UInt64.toNat_mul]; have h10u : (10 : UInt64).toNat = 10 := uint64_ten_toNat; rw [h10u]
      have : (zm + 1).toNat * 10 < 2 ^ 64 := by
        rw [largeRange_min_val] at h_m1_lt_min
        calc (zm + 1).toNat * 10 < 1000000000000000000 * 10 :=
              Nat.mul_lt_mul_of_pos_right h_m1_lt_min (by norm_num)
          _ < 2 ^ 64 := by norm_num
      exact Nat.mod_eq_of_lt this
    by_cases h_under : ze' - 1 < minExponent ∨ (zm + 1) * 10 = 0
    · exfalso; apply hne'; rw [bringIntoRange_rescale_result hresc h_m1_ne, if_pos h_under]
    · push_neg at h_under; obtain ⟨hexp, hne10⟩ := h_under
      rw [if_neg (by push_neg; exact ⟨hexp, hne10⟩)]
      change ((((zm + 1) * 10)).toNat : ℚ) * 10 ^ (ze' - 1) = ((zm.toNat : ℚ) + 1) * 10 ^ ze'
      rw [h_m1_mul10_toNat, hm_add1_toNat]; push_cast
      rw [show (ze' - 1 : ℤ) = ze' + (-1) from by ring,
          zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0), zpow_neg_one]; field_simp
  · rw [bringIntoRange_noscale_result (by push_neg; intro h; exact absurd h hresc)]
    by_cases h_under : ze' < minExponent ∨ zm + 1 = 0
    · exfalso; apply hne'; rw [bringIntoRange_noscale_result (by push_neg; intro h; exact absurd h hresc), if_pos h_under]
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
  have h_g_round_one : g.round .downward = 1 := (round_downward_eq_one_iff g).mpr h_roundUp
  have h_pof_eq : g.pushOverflow zm .downward = g.push 3 := by
    unfold Guard.pushOverflow
    rw [h_zm_eq_maxRep, h_g_round_one]
    rfl
  have h_ru_true :
      ((g.round .downward == 1) || ((g.round .downward == 0) && (zm % 2 == 1))) = true :=
    roundUp_bool_downward_true g zm h_roundUp
  have h_not_noncusp : ¬ (zm < largeRange.max ∧ zm < maxRep) := by
    intro ⟨_, h⟩; have := UInt64.lt_iff_toNat_lt.mp h; rw [h_zm_eq_maxRep, maxRep_val] at this; omega
  have h_not_overflow : ¬ (maxRep < zm ∧ zm < maxRepUp) := by
    intro ⟨h, _⟩; have := UInt64.lt_iff_toNat_lt.mp h; rw [h_zm_eq_maxRep, maxRep_val] at this; omega
  set gP : Guard := g.pushOverflow zm .downward with hgP_def
  have h_gP_eq3 : gP = g.push 3 := h_pof_eq ▸ rfl
  set g' := gP.push (zm % 10) with hg'_def
  have h_g_sbit : g.sbit_ = true := h_roundUp.1
  have h_gP_sbit : gP.sbit_ = true := by rw [h_gP_eq3]; unfold Guard.push; exact h_g_sbit
  have h_gP_digits_toNat : gP.digits_.toNat = g.digits_.toNat / 16 + (3 % 16) * 2 ^ 60 := by
    rw [h_gP_eq3]; exact toNat_push_digits g 3
  have h_gP_digits_gt : gP.digits_ > 0 := by
    change (0 : UInt64) < gP.digits_; rw [UInt64.lt_iff_toNat_lt]
    rw [show (0 : UInt64).toNat = 0 from rfl, h_gP_digits_toNat]; omega
  have h_gP_round : gP.round .downward = 1 :=
    (round_downward_eq_one_iff gP).mpr ⟨h_gP_sbit, Or.inl h_gP_digits_gt⟩
  have h_g'_sbit : g'.sbit_ = true := by rw [hg'_def]; unfold Guard.push; exact h_gP_sbit
  have h_g'_digits_toNat : g'.digits_.toNat = gP.digits_.toNat / 16 + (zm.toNat % 10 % 16) * 2 ^ 60 := by
    rw [hg'_def]; have := toNat_push_digits gP (zm % 10)
    rw [UInt64.toNat_mod] at this; have h10 : (10 : UInt64).toNat = 10 := uint64_ten_toNat; rw [h10] at this; exact this
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
    rw [UInt64.toNat_mul]; have h10 : (10 : UInt64).toNat = 10 := uint64_ten_toNat; rw [h10, h_m1_toNat]
  have h_m1_ne : zm / 10 + 1 ≠ 0 := by
    intro h; have := UInt64.toNat_inj.mpr h; rw [m_add_one_no_overflow h_m_div10_le_maxRep, h_m_div10_toNat] at this; simp at this
  have hres_eq : res = Guard.bringIntoRange zn (zm / 10 + 1) (ze' + 1) largeRange.min := by
    unfold Guard.doRoundUp at hok
    simp only [Guard.doDropDigit] at hok
    have h_gP_ru_true : ((gP.round .downward == 1) || ((gP.round .downward == 0) && (zm % 2 == 1))) = true := by
      rw [h_gP_round]; rfl
    rw [h_gP_ru_true] at hok
    rw [if_neg h_not_noncusp, if_neg h_not_overflow] at hok
    rw [h_ru'_true] at hok
    simp only [if_true] at hok
    -- m/10 + 1 case: noncusp branch
    have h_m1_lt_maxRep : zm / 10 + 1 < maxRep := by
      rw [UInt64.lt_iff_toNat_lt, m_add_one_no_overflow h_m_div10_le_maxRep, h_m_div10_toNat, maxRep_val]; norm_num
    have h_m1_lt_max : zm / 10 + 1 < largeRange.max := by
      rw [UInt64.lt_iff_toNat_lt, m_add_one_no_overflow h_m_div10_le_maxRep, h_m_div10_toNat, largeRange_max_val]; norm_num
    -- After roundUp' = true, m/10+1 is used directly in bringIntoRange
    by_cases h_ovf : (Guard.bringIntoRange zn (zm / 10 + 1) (ze' + 1) largeRange.min).exponent_ > maxExponent
    · rw [if_pos h_ovf] at hok; simp at hok
    · rw [if_neg h_ovf] at hok; exact (Except.ok.inj hok).symm
  rw [hres_eq]
  have hne' : (Guard.bringIntoRange zn (zm / 10 + 1) (ze' + 1) largeRange.min).mantissa_ ≠ 0 := hres_eq ▸ h_mant_ne
  rw [bringIntoRange_rescale_result h_m1_lt_min h_m1_ne]
  by_cases h_under : ze' + 1 - 1 < minExponent ∨ (zm / 10 + 1) * 10 = 0
  · exfalso; apply hne'; rw [bringIntoRange_rescale_result h_m1_lt_min h_m1_ne, if_pos h_under]
  · push_neg at h_under; obtain ⟨hexp, hne10⟩ := h_under
    rw [if_neg (by push_neg; exact ⟨hexp, hne10⟩)]
    change (((zm / 10 + 1) * 10).toNat : ℚ) * 10 ^ (ze' + 1 - 1) = maxRepCuspTarget * 10 ^ ze'
    rw [h_m1_mul10_toNat]; rw [show (ze' + 1 - 1 : ℤ) = ze' from by ring]; push_cast; ring

/-- `.downward`-mode analogue of `doRoundUp_output_invariants_to_nearest`. -/
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
  set gP : Guard := g.pushOverflow m .downward with hgP_def
  by_cases h_ru : (gP.round .downward == 1 || (gP.round .downward == 0 && m % 2 == 1)) = true
  · rw [show (gP.round .downward == 1 || (gP.round .downward == 0 && m % 2 == 1)) = true from h_ru] at hok
    by_cases h_m_eq_maxRep : m = maxRep
    · -- cusp case: m = maxRep, gP = g.push 3 (since g.round .downward = 1)
      have h_not_noncusp : ¬ (m < largeRange.max ∧ m < maxRep) := by
        intro ⟨_, h⟩; have := UInt64.lt_iff_toNat_lt.mp h; rw [h_m_eq_maxRep, maxRep_val] at this; omega
      have h_not_overflow : ¬ (maxRep < m ∧ m < maxRepUp) := by
        intro ⟨h, _⟩; have := UInt64.lt_iff_toNat_lt.mp h; rw [h_m_eq_maxRep, maxRep_val] at this; omega
      rw [if_neg h_not_noncusp, if_neg h_not_overflow] at hok
      have h_m_div10_toNat : (m / 10).toNat = mantissaFloor := by
        rw [UInt64.toNat_div, h_m_eq_maxRep]; decide
      have h_g_round_one : g.round .downward = 1 := by
        by_contra h_ne
        have h_r_ne1 : g.round .downward ≠ 1 := h_ne
        have h_pof_noop : gP = g :=
          hgP_def ▸ pushOverflow_noop_of_le_maxRep_of_round_ne_one h_ub g .downward h_r_ne1
        rw [h_pof_noop] at h_ru
        have h_ne0 : g.round .downward ≠ 0 := by
          have hvals : g.round .downward = 1 ∨ g.round .downward = -1 ∨ g.round .downward = -2 := by
            unfold Guard.round; split_ifs <;>
              first | (left; rfl) | (right; left; rfl) | (right; right; rfl)
          rcases hvals with h | h | h <;> omega
        simp [beq_iff_eq, h_r_ne1, h_ne0] at h_ru
      have h_gP_eq3 : gP = g.push 3 := by
        rw [hgP_def, h_m_eq_maxRep]
        unfold Guard.pushOverflow
        rw [h_g_round_one]
        rfl
      have h_g_sbit : g.sbit_ = true := ((round_downward_eq_one_iff g).mp h_g_round_one).1
      have h_gP_sbit : gP.sbit_ = true := by rw [h_gP_eq3]; unfold Guard.push; exact h_g_sbit
      have h_gP_digits_gt : gP.digits_ > 0 := by
        change (0 : UInt64) < gP.digits_; rw [UInt64.lt_iff_toNat_lt]
        rw [show (0 : UInt64).toNat = 0 from rfl]
        have hpd : gP.digits_.toNat = g.digits_.toNat / 16 + (3 % 16) * 2 ^ 60 := by
          rw [h_gP_eq3]; exact toNat_push_digits g 3
        rw [hpd]; omega
      set g' := gP.push (m % 10) with hg'_def
      have h_g'_sbit : g'.sbit_ = true := by rw [hg'_def]; unfold Guard.push; exact h_gP_sbit
      have h_g'_digits_toNat : g'.digits_.toNat = gP.digits_.toNat / 16 + (m.toNat % 10 % 16) * 2 ^ 60 := by
        rw [hg'_def]; have := toNat_push_digits gP (m % 10)
        rw [UInt64.toNat_mod] at this; have h10 : (10 : UInt64).toNat = 10 := uint64_ten_toNat; rw [h10] at this; exact this
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
      have h_m1_ne_d : m / 10 + 1 ≠ 0 := by
        intro h; have := UInt64.toNat_inj.mpr h; rw [m_add_one_no_overflow h_m_div10_le_maxRep, h_m_div10_toNat] at this; simp at this
      -- After roundUp' = true, result is bringIntoRange (m/10+1) (e+1)
      have h_m1_mul10_toNat : ((m / 10 + 1) * 10).toNat = maxRepCuspTarget := by
        rw [UInt64.toNat_mul]; have h10 : (10 : UInt64).toNat = 10 := uint64_ten_toNat; rw [h10, h_m1_toNat]
      have h_m1_10_ne_d : (m / 10 + 1) * 10 ≠ 0 := by
        intro h; have := UInt64.toNat_inj.mpr h; rw [h_m1_mul10_toNat] at this; simp at this
      rw [if_pos (And.intro h_m1_lt_min h_m1_ne_d)] at hok
      simp only [] at hok
      by_cases h_under : e + 1 - 1 < minExponent ∨ (m / 10 + 1) * 10 = 0
      · underflow_absurd hne h_under hok
      · push_neg at h_under; obtain ⟨hexp, hne10⟩ := h_under
        have h_not_under : ¬ (e + 1 - 1 < minExponent ∨ (m / 10 + 1) * 10 = 0) :=
          by push_neg; exact ⟨hexp, hne10⟩
        simp only [if_neg h_not_under] at hok
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
        have : m.toNat ≠ maxRep.toNat := fun heq => h_m_eq_maxRep (UInt64.toNat_inj.mp heq); omega
      -- noncusp branch fires
      have h_noncusp_d : m < largeRange.max ∧ m < maxRep := by
        constructor
        · exact UInt64.lt_iff_toNat_lt.mpr (by rw [largeRange_max_val, maxRep_val] at *; omega)
        · exact UInt64.lt_iff_toNat_lt.mpr h_m_lt_maxRep
      rw [if_pos h_noncusp_d] at hok
      simp only [if_true] at hok
      have h_m1_le_maxRep : (m + 1).toNat ≤ maxRep.toNat := by rw [hm_add1_toNat]; omega
      have h_m1_ne_nc : m + 1 ≠ 0 := by
        intro h; have := UInt64.toNat_inj.mpr h; rw [hm_add1_toNat] at this; simp at this
      by_cases h_resc : m + 1 < largeRange.min
      · rw [if_pos (And.intro h_resc h_m1_ne_nc)] at hok; simp only [] at hok
        have h_m1_lt_min : (m + 1).toNat < largeRange.min.toNat := UInt64.lt_iff_toNat_lt.mp h_resc
        have h_m1_mul10_toNat : ((m + 1) * 10).toNat = (m + 1).toNat * 10 := by
          rw [UInt64.toNat_mul]; have h10 : (10 : UInt64).toNat = 10 := uint64_ten_toNat; rw [h10]
          apply Nat.mod_eq_of_lt; rw [hminMant_v] at h_m1_lt_min
          calc (m + 1).toNat * 10 < 1000000000000000000 * 10 := Nat.mul_lt_mul_of_pos_right h_m1_lt_min (by norm_num)
            _ < 2 ^ 64 := by norm_num
        have h_m1_10_ne_nc : (m + 1) * 10 ≠ 0 := by
          intro h; have hnat : ((m + 1) * 10).toNat = 0 := by rw [h]; rfl
          rw [h_m1_mul10_toNat, hm_add1_toNat] at hnat; omega
        by_cases h_under : e - 1 < minExponent ∨ (m + 1) * 10 = 0
        · underflow_absurd hne h_under hok
        · have h_nu := h_under
          push_neg at h_under; obtain ⟨hexp, hne10⟩ := h_under
          simp only [if_neg h_nu] at hok
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
      · have h_no_resc_nc : ¬ (m + 1 < largeRange.min ∧ m + 1 ≠ 0) := by
          intro ⟨h, _⟩; exact h_resc h
        rw [if_neg h_no_resc_nc] at hok; simp only [] at hok
        have h_m1_ge_min : (m + 1).toNat ≥ largeRange.min.toNat := by
          by_contra h; push_neg at h; exact h_resc (UInt64.lt_iff_toNat_lt.mpr h)
        by_cases h_under : e < minExponent ∨ m + 1 = 0
        · underflow_absurd hne h_under hok
        · have h_nu := h_under
          push_neg at h_under; obtain ⟨hexp, hm1ne⟩ := h_under
          simp only [if_neg h_nu] at hok
          have h_no_ovf : ¬ (e > maxExponent) := by
            intro h_ovf; simp only [if_pos h_ovf] at hok; simp at hok
          simp only [if_neg h_no_ovf] at hok; obtain rfl := Except.ok.inj hok
          refine ⟨h_m1_ge_min, ?_, ?_, ?_⟩
          · change (m + 1).toNat ≤ largeRange.max.toNat
            rw [hm_add1_toNat, hmaxMant_v]; rw [hmaxRep_v] at h_m_lt_maxRep; omega
          · change minExponent ≤ e; exact hexp
          · change (m + 1).toNat > maxRep.toNat → (m + 1).toNat % 10 = 0
            intro h_gt; exfalso; have : (m + 1).toNat ≤ maxRep.toNat := h_m1_le_maxRep; omega
  · rw [Bool.not_eq_true] at h_ru
    have h_g_round_ne1 : g.round .downward ≠ 1 := by
      intro hone
      rcases Nat.lt_or_ge m.toNat maxRep.toNat with hlt | hge
      · have h_noop : gP = g := hgP_def ▸ pushOverflow_noop_of_lt_maxRep hlt g .downward
        have : gP.round .downward = 1 := h_noop ▸ hone
        simp [this] at h_ru
      · have hm_eq : m = maxRep := UInt64.toNat_inj.mp (by omega)
        have h_gP_eq3 : gP = g.push 3 := by
          rw [hgP_def, hm_eq]; unfold Guard.pushOverflow
          rw [if_pos (by decide)]
          simp only [show maxRep % (10 : UInt64) < 9 from by decide]
          rw [show maxRep + (maxRepUp - maxRep) / 2 = (maxRep + 1 : UInt64) from by decide]
          have hr_eq : (g.round .downward == 1) = true := by rw [beq_iff_eq]; exact hone
          simp only [hr_eq, Bool.true_or, ite_true]
          have hne'' : ((maxRep + 1 : UInt64) == maxRep) = false := by decide
          simp only [hne'', ite_false]; rfl
        have h_g_sbit : g.sbit_ = true := ((round_downward_eq_one_iff g).mp hone).1
        have h_gP_sbit : gP.sbit_ = true := by rw [h_gP_eq3]; unfold Guard.push; exact h_g_sbit
        have h_gP_dgt : gP.digits_ > 0 := by
          change (0 : UInt64) < gP.digits_; rw [UInt64.lt_iff_toNat_lt]
          rw [show (0 : UInt64).toNat = 0 from rfl]
          have hpd : gP.digits_.toNat = g.digits_.toNat / 16 + (3 % 16) * 2 ^ 60 := by
            rw [h_gP_eq3]; exact toNat_push_digits g 3
          rw [hpd]; omega
        have h_gP_r1 : gP.round .downward = 1 :=
          (round_downward_eq_one_iff gP).mpr ⟨h_gP_sbit, Or.inl h_gP_dgt⟩
        simp [h_gP_r1] at h_ru
    have h_pof_noop : gP = g :=
      hgP_def ▸ pushOverflow_noop_of_le_maxRep_of_round_ne_one h_ub g .downward h_g_round_ne1
    rw [h_pof_noop] at h_ru hok
    rw [show (g.round .downward == 1 || (g.round .downward == 0 && m % 2 == 1)) = false from h_ru] at hok
    simp only [Bool.false_eq_true, ite_false] at hok
    have h_no_overflow_d : ¬ (maxRep < m ∧ m < maxRepUp) := by
      intro ⟨h, _⟩; have := UInt64.lt_iff_toNat_lt.mp h; omega
    rw [if_neg h_no_overflow_d] at hok
    have h_m_ne_d : m ≠ 0 := by intro h; rw [h] at h_lb; simp at h_lb
    by_cases h_resc : m < largeRange.min
    · rw [if_pos (And.intro h_resc h_m_ne_d)] at hok; simp only [] at hok
      have h_m_lt_min : m.toNat < largeRange.min.toNat := UInt64.lt_iff_toNat_lt.mp h_resc
      have h_m_mul10_toNat : (m * 10).toNat = m.toNat * 10 := m_mul_ten_no_overflow h_m_lt_min
      have h_m10_ne_d : m * 10 ≠ 0 := by
        intro h; have hnat : (m * 10).toNat = 0 := by rw [h]; rfl
        rw [h_m_mul10_toNat] at hnat; omega
      by_cases h_under : e - 1 < minExponent ∨ m * 10 = 0
      · underflow_absurd hne h_under hok
      · have h_nu := h_under
        push_neg at h_under; obtain ⟨hexp, hne10⟩ := h_under
        simp only [if_neg h_nu] at hok
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
    · have h_no_resc_d : ¬ (m < largeRange.min ∧ m ≠ 0) := by
        intro ⟨h, _⟩; exact h_resc h
      rw [if_neg h_no_resc_d] at hok; simp only [] at hok
      have h_m_ge_min : m.toNat ≥ largeRange.min.toNat := by
        by_contra h; push_neg at h; exact h_resc (UInt64.lt_iff_toNat_lt.mpr h)
      by_cases h_under : e < minExponent ∨ m = 0
      · underflow_absurd hne h_under hok
      · have h_nu := h_under
        push_neg at h_under; obtain ⟨hexp, hmne⟩ := h_under
        simp only [if_neg h_nu] at hok
        have h_no_ovf : ¬ (e > maxExponent) := by
          intro h_ovf; simp only [if_pos h_ovf] at hok; simp at hok
        simp only [if_neg h_no_ovf] at hok; obtain rfl := Except.ok.inj hok
        refine ⟨h_m_ge_min, ?_, ?_, ?_⟩
        · change m.toNat ≤ largeRange.max.toNat; rw [hmaxMant_v]; rw [hmaxRep_v] at h_ub; omega
        · change minExponent ≤ e; exact hexp
        · change m.toNat > maxRep.toNat → m.toNat % 10 = 0; intro h_gt; exfalso; omega

/-! ## `.towards_zero` mode

`Guard.round .towards_zero = -1` always, so `roundUp` is always false. -/

/-- `Guard.round g .towards_zero ∈ {-2, -1}`, so `roundUp` is always false. -/
lemma roundUp_bool_towards_zero_false (g : Guard) (m : UInt64) :
    ((g.round .towards_zero == 1) || ((g.round .towards_zero == 0) && (m % 2 == 1))) = false := by
  unfold Guard.round
  split_ifs <;> rfl

/-- For `.towards_zero` the algorithm always truncates:
`result.mantissa * 10^exp = zm * 10^ze'`. -/
theorem doRoundUp_value_towards_zero_truncate
    (g : Guard) (zn : Bool) (zm : UInt64) (ze' : Int)
    (h_zm_le : zm.toNat ≤ maxRep.toNat)
    (loc : String) (res : RoundResult)
    (hok : g.doRoundUp zn zm ze' largeRange.min largeRange.max .towards_zero loc = .ok res)
    (h_mant_ne : res.mantissa_ ≠ 0) :
    (res.mantissa_.toNat : ℚ) * 10 ^ res.exponent_ = (zm.toNat : ℚ) * 10 ^ ze' := by
  have h_pof : g.pushOverflow zm .towards_zero = g :=
    pushOverflow_noop_of_le_maxRep_of_round_ne_one h_zm_le g .towards_zero
      (by unfold Guard.round; split_ifs <;> intro h <;> simp at h)
  have h_no_cusp : ¬ (maxRep < zm ∧ zm < maxRepUp) := by
    intro ⟨h, _⟩; have := UInt64.lt_iff_toNat_lt.mp h; omega
  have h_ru_false :
      ((g.round .towards_zero == 1) || ((g.round .towards_zero == 0) && (zm % 2 == 1))) = false :=
    roundUp_bool_towards_zero_false g zm
  have hres_eq : res = Guard.bringIntoRange zn zm ze' largeRange.min := by
    unfold Guard.doRoundUp at hok
    simp only [Guard.doDropDigit] at hok
    rw [h_pof] at hok
    rw [h_ru_false] at hok
    simp only [Bool.false_eq_true, ite_false] at hok
    rw [if_neg h_no_cusp] at hok
    by_cases h_ovf : (Guard.bringIntoRange zn zm ze' largeRange.min).exponent_ > maxExponent
    · rw [if_pos h_ovf] at hok; simp at hok
    · rw [if_neg h_ovf] at hok; exact (Except.ok.inj hok).symm
  rw [hres_eq]
  have hne' : (Guard.bringIntoRange zn zm ze' largeRange.min).mantissa_ ≠ 0 := hres_eq ▸ h_mant_ne
  have hzm_ne : zm ≠ 0 := by
    intro h
    apply hne'
    rw [bringIntoRange_noscale_result (by intro ⟨_, hne⟩; exact hne h)]
    simp [h]
  by_cases hresc : zm < largeRange.min
  · rw [bringIntoRange_rescale_result hresc hzm_ne]
    have hzm_mul_10 : (zm * 10).toNat = zm.toNat * 10 :=
      m_mul_ten_no_overflow (UInt64.lt_iff_toNat_lt.mp hresc)
    by_cases h_under : ze' - 1 < minExponent ∨ zm * 10 = 0
    · exfalso; apply hne'; rw [bringIntoRange_rescale_result hresc hzm_ne, if_pos h_under]
    · push_neg at h_under; obtain ⟨hexp, hne10⟩ := h_under
      rw [if_neg (by push_neg; exact ⟨hexp, hne10⟩)]
      change ((zm * 10).toNat : ℚ) * 10 ^ (ze' - 1) = (zm.toNat : ℚ) * 10 ^ ze'
      rw [hzm_mul_10]; push_cast
      rw [show (ze' - 1 : ℤ) = ze' + (-1) from by ring,
          zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0), zpow_neg_one]; field_simp
  · rw [bringIntoRange_noscale_result (by push_neg; intro h; exact absurd h hresc)]
    by_cases h_under : ze' < minExponent ∨ zm = 0
    · exfalso; apply hne'; rw [bringIntoRange_noscale_result (by push_neg; intro h; exact absurd h hresc), if_pos h_under]
    · rw [if_neg h_under]

/-- `.towards_zero` analogue of `doRoundUp_output_invariants_to_nearest`. -/
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
  have h_pof : g.pushOverflow m .towards_zero = g :=
    pushOverflow_noop_of_le_maxRep_of_round_ne_one h_ub g .towards_zero
      (by unfold Guard.round; split_ifs <;> intro h <;> simp at h)
  have h_ru_false :
      (g.round .towards_zero == 1 || (g.round .towards_zero == 0 && m % 2 == 1)) = false :=
    roundUp_bool_towards_zero_false g m
  have h_no_overflow_tz : ¬ (maxRep < m ∧ m < maxRepUp) := by
    intro ⟨h, _⟩; have := UInt64.lt_iff_toNat_lt.mp h; omega
  have h_m_ne : m ≠ 0 := by intro h; rw [h] at h_lb; simp at h_lb
  unfold Guard.doRoundUp Guard.bringIntoRange at hok
  simp only [Guard.doDropDigit] at hok
  rw [h_pof] at hok
  rw [h_ru_false] at hok
  simp only [Bool.false_eq_true, ite_false] at hok
  rw [if_neg h_no_overflow_tz] at hok
  by_cases h_resc : m < largeRange.min
  · rw [if_pos (And.intro h_resc h_m_ne)] at hok; simp only [] at hok
    have h_m_lt_min : m.toNat < largeRange.min.toNat := UInt64.lt_iff_toNat_lt.mp h_resc
    have h_m_mul10_toNat : (m * 10).toNat = m.toNat * 10 := m_mul_ten_no_overflow h_m_lt_min
    have h_m10_ne : m * 10 ≠ 0 := by
      intro h; have hnat : (m * 10).toNat = 0 := by rw [h]; rfl
      rw [h_m_mul10_toNat] at hnat; omega
    by_cases h_under : e - 1 < minExponent ∨ m * 10 = 0
    · underflow_absurd hne h_under hok
    · have h_nu := h_under
      push_neg at h_under; obtain ⟨hexp, hne10⟩ := h_under
      simp only [if_neg h_nu] at hok
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
  · have h_no_resc_tz : ¬ (m < largeRange.min ∧ m ≠ 0) := by
      intro ⟨h, _⟩; exact h_resc h
    rw [if_neg h_no_resc_tz] at hok; simp only [] at hok
    have h_m_ge_min : m.toNat ≥ largeRange.min.toNat := by
      by_contra h; push_neg at h; exact h_resc (UInt64.lt_iff_toNat_lt.mpr h)
    by_cases h_under : e < minExponent ∨ m = 0
    · underflow_absurd hne h_under hok
    · have h_nu := h_under
      push_neg at h_under; obtain ⟨hexp, hmne⟩ := h_under
      simp only [if_neg h_nu] at hok
      have h_no_ovf : ¬ (e > maxExponent) := by
        intro h_ovf; simp only [if_pos h_ovf] at hok; simp at hok
      simp only [if_neg h_no_ovf] at hok; obtain rfl := Except.ok.inj hok
      refine ⟨h_m_ge_min, ?_, ?_, ?_⟩
      · change m.toNat ≤ largeRange.max.toNat; rw [hmaxMant_v]; rw [hmaxRep_v] at h_ub; omega
      · change minExponent ≤ e; exact hexp
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
    by_cases hemp : g.empty
    · unfold Guard.round at h; rw [if_pos hemp] at h; exact absurd h (by decide)
    · unfold Guard.round at h; rw [if_neg hemp] at h
      by_cases hs : g.sbit_ = true
      · exfalso
        simp only [hs, if_true] at h; exact absurd h (by decide)
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
            simp only [hs_false, Bool.false_eq_true, if_false, h_bool_false, Bool.false_eq_true, if_false] at h
            exact absurd h (by decide)
  · intro ⟨hs, hor⟩
    have h_bool_true : (decide (g.digits_ > 0) || g.xbit_) = true := by
      rcases hor with h1 | h2
      · rw [decide_eq_true h1]; rfl
      · rw [h2]; rw [Bool.or_true]
    unfold Guard.round
    by_cases hemp : g.empty
    · -- g.empty means digits = 0 and xbit = false, contradicts shouldRoundUp_upward
      exfalso
      unfold Guard.empty at hemp
      simp only [Bool.and_eq_true, beq_iff_eq] at hemp
      obtain ⟨hd0, hx_true⟩ := hemp
      have hd : g.digits_ = 0 := by exact_mod_cast hd0
      have hx : g.xbit_ = false := Bool.eq_false_iff.mpr (by simpa using hx_true)
      rcases hor with h1 | h2
      · simp [hd] at h1
      · simp [hx] at h2
    · rw [if_neg hemp]
      simp only [hs, Bool.false_eq_true, if_false, h_bool_true, if_true]

-- `round_upward_eq_neg_one_iff` is removed (now handled in roundUp_bool_upward_false).

/-- For `.upward`, the boolean `roundUp` in `doRoundUp` equals `false`
when `shouldRoundUp_upward` is false. -/
private lemma roundUp_bool_upward_false (g : Guard) (m : UInt64)
    (h_no : ¬ g.shouldRoundUp_upward) :
    ((g.round .upward == 1) || ((g.round .upward == 0) && (m % 2 == 1))) = false := by
  have h_ne1 : g.round .upward ≠ 1 := by
    intro h; exact h_no ((round_upward_eq_one_iff g).mp h)
  have h_ne0 : g.round .upward ≠ 0 := by
    unfold Guard.round; split_ifs <;> simp_all (config := { decide := true })
  simp [beq_iff_eq, h_ne1, h_ne0]

/-- For `.upward`, the boolean `roundUp` in `doRoundUp` equals `true`
when `shouldRoundUp_upward` is true. -/
private lemma roundUp_bool_upward_true (g : Guard) (m : UInt64)
    (h_yes : g.shouldRoundUp_upward) :
    ((g.round .upward == 1) || ((g.round .upward == 0) && (m % 2 == 1))) = true := by
  have h1 : g.round .upward = 1 := (round_upward_eq_one_iff g).mpr h_yes
  rw [h1]; rfl

/-- With the sign bit clear, a dead `.upward` round decision forces empty guard
content: no packed digits and a clear sticky bit. -/
lemma content_empty_of_not_shouldRoundUp_upward (g : Guard)
    (h_sbit : g.sbit_ = false) (h_nsru : ¬ g.shouldRoundUp_upward) :
    g.digits_ = 0 ∧ g.xbit_ = false := by
  have h_no_or : ¬ (g.digits_ > 0 ∨ g.xbit_ = true) := fun h => h_nsru ⟨h_sbit, h⟩
  have h_digits_not_pos : ¬ g.digits_ > 0 := fun h => h_no_or (Or.inl h)
  have h_xbit : g.xbit_ = false := Bool.not_eq_true _ |>.mp (fun h => h_no_or (Or.inr h))
  refine ⟨?_, h_xbit⟩
  have h_dn : g.digits_.toNat = 0 := by
    by_contra h_ne
    apply h_digits_not_pos
    change (0 : UInt64) < g.digits_
    rw [UInt64.lt_iff_toNat_lt, show (0 : UInt64).toNat = 0 from rfl]
    omega
  rw [← UInt64.toNat_inj, h_dn]; rfl

/-- Bool form of `content_empty_of_not_shouldRoundUp_upward`: with the sign
bit clear, a false `roundUp` decision boolean forces empty guard content. -/
lemma roundUp_bool_upward_false_content (g : Guard) (m : UInt64)
    (h_sbit : g.sbit_ = false)
    (hb : ((g.round .upward == 1) || ((g.round .upward == 0) && (m % 2 == 1))) = false) :
    g.digits_ = 0 ∧ g.xbit_ = false := by
  apply content_empty_of_not_shouldRoundUp_upward g h_sbit
  intro h_sru
  have h1 : g.round .upward = 1 := (round_upward_eq_one_iff g).mpr h_sru
  rw [h1, show ((1 : Int) == 1) = true from rfl, Bool.true_or] at hb
  exact Bool.noConfusion hb

/-- The no-round-up case for `.upward`: `result.mantissa * 10^exp = zm * 10^ze'`. -/
theorem doRoundUp_value_upward_truncate
    (g : Guard) (zn : Bool) (zm : UInt64) (ze' : Int)
    (h_no_roundUp : ¬ g.shouldRoundUp_upward)
    (h_zm_le : zm.toNat ≤ maxRep.toNat)
    (loc : String) (res : RoundResult)
    (hok : g.doRoundUp zn zm ze' largeRange.min largeRange.max .upward loc = .ok res)
    (h_mant_ne : res.mantissa_ ≠ 0) :
    (res.mantissa_.toNat : ℚ) * 10 ^ res.exponent_ = (zm.toNat : ℚ) * 10 ^ ze' := by
  have h_pof : g.pushOverflow zm .upward = g :=
    pushOverflow_noop_of_le_maxRep_of_round_ne_one h_zm_le g .upward
      (fun hr => absurd ((round_upward_eq_one_iff g).mp hr) h_no_roundUp)
  have h_no_cusp : ¬ (maxRep < zm ∧ zm < maxRepUp) := by
    intro ⟨h, _⟩; have := UInt64.lt_iff_toNat_lt.mp h; omega
  have h_ru_false :
      ((g.round .upward == 1) || ((g.round .upward == 0) && (zm % 2 == 1))) = false :=
    roundUp_bool_upward_false g zm h_no_roundUp
  have hres_eq : res = Guard.bringIntoRange zn zm ze' largeRange.min := by
    unfold Guard.doRoundUp at hok; simp only [Guard.doDropDigit] at hok
    rw [h_pof] at hok
    rw [h_ru_false] at hok
    simp only [Bool.false_eq_true, ite_false] at hok
    rw [if_neg h_no_cusp] at hok
    by_cases h_ovf : (Guard.bringIntoRange zn zm ze' largeRange.min).exponent_ > maxExponent
    · rw [if_pos h_ovf] at hok; simp at hok
    · rw [if_neg h_ovf] at hok; exact (Except.ok.inj hok).symm
  rw [hres_eq]
  have hne' : (Guard.bringIntoRange zn zm ze' largeRange.min).mantissa_ ≠ 0 := hres_eq ▸ h_mant_ne
  have hzm_ne : zm ≠ 0 := by
    intro h
    apply hne'
    rw [bringIntoRange_noscale_result (by intro ⟨_, hne⟩; exact hne h)]
    simp [h]
  by_cases hresc : zm < largeRange.min
  · rw [bringIntoRange_rescale_result hresc hzm_ne]
    have hzm_mul_10 : (zm * 10).toNat = zm.toNat * 10 :=
      m_mul_ten_no_overflow (UInt64.lt_iff_toNat_lt.mp hresc)
    by_cases h_under : ze' - 1 < minExponent ∨ zm * 10 = 0
    · exfalso; apply hne'; rw [bringIntoRange_rescale_result hresc hzm_ne, if_pos h_under]
    · push_neg at h_under; obtain ⟨hexp, hne10⟩ := h_under
      rw [if_neg (by push_neg; exact ⟨hexp, hne10⟩)]
      change ((zm * 10).toNat : ℚ) * 10 ^ (ze' - 1) = (zm.toNat : ℚ) * 10 ^ ze'
      rw [hzm_mul_10]; push_cast
      rw [show (ze' - 1 : ℤ) = ze' + (-1) from by ring,
          zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0), zpow_neg_one]; field_simp
  · rw [bringIntoRange_noscale_result (by push_neg; intro h; exact absurd h hresc)]
    by_cases h_under : ze' < minExponent ∨ zm = 0
    · exfalso; apply hne'; rw [bringIntoRange_noscale_result (by push_neg; intro h; exact absurd h hresc), if_pos h_under]
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
  have h_m_lt_maxRep : zm.toNat < maxRep.toNat := by rw [maxRep_val] at h_no_cusp ⊢; omega
  have h_pof : g.pushOverflow zm .upward = g := pushOverflow_noop_of_lt_maxRep h_m_lt_maxRep g .upward
  have h_ru_true :
      ((g.round .upward == 1) || ((g.round .upward == 0) && (zm % 2 == 1))) = true :=
    roundUp_bool_upward_true g zm h_roundUp
  -- noncusp branch fires: zm < largeRange.max ∧ zm < maxRep
  have h_noncusp_branch : zm < largeRange.max ∧ zm < maxRep := by
    constructor
    · exact UInt64.lt_iff_toNat_lt.mpr (by rw [largeRange_max_val, maxRep_val] at *; omega)
    · exact UInt64.lt_iff_toNat_lt.mpr h_m_lt_maxRep
  have hres_eq : res = Guard.bringIntoRange zn (zm + 1) ze' largeRange.min := by
    unfold Guard.doRoundUp at hok; simp only [Guard.doDropDigit] at hok
    rw [h_pof] at hok
    rw [h_ru_true] at hok
    rw [if_pos h_noncusp_branch] at hok
    simp only [if_true] at hok
    by_cases h_ovf : (Guard.bringIntoRange zn (zm + 1) ze' largeRange.min).exponent_ > maxExponent
    · rw [if_pos h_ovf] at hok; simp at hok
    · rw [if_neg h_ovf] at hok; exact (Except.ok.inj hok).symm
  rw [hres_eq]
  have hne' : (Guard.bringIntoRange zn (zm + 1) ze' largeRange.min).mantissa_ ≠ 0 := hres_eq ▸ h_mant_ne
  have h_m_le_maxRep : zm.toNat ≤ maxRep.toNat := by omega
  have hm_add1_toNat : (zm + 1).toNat = zm.toNat + 1 := m_add_one_no_overflow h_m_le_maxRep
  have h_m1_ne : zm + 1 ≠ 0 := by
    intro h; have := UInt64.toNat_inj.mpr h; rw [hm_add1_toNat] at this; simp at this
  by_cases hresc : zm + 1 < largeRange.min
  · rw [bringIntoRange_rescale_result hresc h_m1_ne]
    have h_m1_lt_min : (zm + 1).toNat < largeRange.min.toNat := UInt64.lt_iff_toNat_lt.mp hresc
    have h_m1_mul10_toNat : ((zm + 1) * 10).toNat = (zm + 1).toNat * 10 := by
      rw [UInt64.toNat_mul]; have h10u : (10 : UInt64).toNat = 10 := uint64_ten_toNat; rw [h10u]
      have : (zm + 1).toNat * 10 < 2 ^ 64 := by
        rw [largeRange_min_val] at h_m1_lt_min
        calc (zm + 1).toNat * 10 < 1000000000000000000 * 10 :=
              Nat.mul_lt_mul_of_pos_right h_m1_lt_min (by norm_num)
          _ < 2 ^ 64 := by norm_num
      exact Nat.mod_eq_of_lt this
    by_cases h_under : ze' - 1 < minExponent ∨ (zm + 1) * 10 = 0
    · exfalso; apply hne'; rw [bringIntoRange_rescale_result hresc h_m1_ne, if_pos h_under]
    · push_neg at h_under; obtain ⟨hexp, hne10⟩ := h_under
      rw [if_neg (by push_neg; exact ⟨hexp, hne10⟩)]
      change ((((zm + 1) * 10)).toNat : ℚ) * 10 ^ (ze' - 1) = ((zm.toNat : ℚ) + 1) * 10 ^ ze'
      rw [h_m1_mul10_toNat, hm_add1_toNat]; push_cast
      rw [show (ze' - 1 : ℤ) = ze' + (-1) from by ring,
          zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0), zpow_neg_one]; field_simp
  · rw [bringIntoRange_noscale_result (by push_neg; intro h; exact absurd h hresc)]
    by_cases h_under : ze' < minExponent ∨ zm + 1 = 0
    · exfalso; apply hne'; rw [bringIntoRange_noscale_result (by push_neg; intro h; exact absurd h hresc), if_pos h_under]
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
  have h_g_round_one : g.round .upward = 1 := (round_upward_eq_one_iff g).mpr h_roundUp
  have h_pof_eq : g.pushOverflow zm .upward = g.push 3 := by
    unfold Guard.pushOverflow
    rw [h_zm_eq_maxRep, h_g_round_one]
    rfl
  have h_not_noncusp : ¬ (zm < largeRange.max ∧ zm < maxRep) := by
    intro ⟨_, h⟩; have := UInt64.lt_iff_toNat_lt.mp h; rw [h_zm_eq_maxRep, maxRep_val] at this; omega
  have h_not_overflow : ¬ (maxRep < zm ∧ zm < maxRepUp) := by
    intro ⟨h, _⟩; have := UInt64.lt_iff_toNat_lt.mp h; rw [h_zm_eq_maxRep, maxRep_val] at this; omega
  set gP : Guard := g.pushOverflow zm .upward with hgP_def
  have h_gP_eq3 : gP = g.push 3 := h_pof_eq ▸ rfl
  set g' := gP.push (zm % 10) with hg'_def
  have h_g_sbit : g.sbit_ = false := h_roundUp.1
  have h_gP_sbit : gP.sbit_ = false := by rw [h_gP_eq3]; unfold Guard.push; exact h_g_sbit
  have h_gP_digits_toNat : gP.digits_.toNat = g.digits_.toNat / 16 + (3 % 16) * 2 ^ 60 := by
    rw [h_gP_eq3]; exact toNat_push_digits g 3
  have h_gP_digits_gt : gP.digits_ > 0 := by
    change (0 : UInt64) < gP.digits_; rw [UInt64.lt_iff_toNat_lt]
    rw [show (0 : UInt64).toNat = 0 from rfl, h_gP_digits_toNat]; omega
  have h_gP_round : gP.round .upward = 1 :=
    (round_upward_eq_one_iff gP).mpr ⟨h_gP_sbit, Or.inl h_gP_digits_gt⟩
  have h_g'_sbit : g'.sbit_ = false := by rw [hg'_def]; unfold Guard.push; exact h_gP_sbit
  have h_g'_digits_toNat : g'.digits_.toNat = gP.digits_.toNat / 16 + (zm.toNat % 10 % 16) * 2 ^ 60 := by
    rw [hg'_def]; have := toNat_push_digits gP (zm % 10)
    rw [UInt64.toNat_mod] at this; have h10 : (10 : UInt64).toNat = 10 := uint64_ten_toNat; rw [h10] at this; exact this
  have h_zm_mod10_mod16 : zm.toNat % 10 % 16 = 7 := by rw [show zm.toNat % 10 = 7 from by rw [h_zm_eq_maxRep]; decide]
  have h_g'_digits_gt : g'.digits_ > 0 := by
    change (0 : UInt64) < g'.digits_; rw [UInt64.lt_iff_toNat_lt]
    rw [show (0 : UInt64).toNat = 0 from rfl, h_g'_digits_toNat, h_zm_mod10_mod16, h_gP_digits_toNat]; omega
  have h_g'_round : g'.round .upward = 1 :=
    (round_upward_eq_one_iff g').mpr ⟨h_g'_sbit, Or.inl h_g'_digits_gt⟩
  have h_ru'_true :
      ((g'.round .upward == 1) || ((g'.round .upward == 0) && ((zm / 10) % 2 == 1))) = true := by
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
    rw [UInt64.toNat_mul]; have h10 : (10 : UInt64).toNat = 10 := uint64_ten_toNat; rw [h10, h_m1_toNat]
  have h_m1_ne : zm / 10 + 1 ≠ 0 := by
    intro h; have := UInt64.toNat_inj.mpr h
    rw [m_add_one_no_overflow h_m_div10_le_maxRep, h_m_div10_toNat] at this; simp at this
  have hres_eq : res = Guard.bringIntoRange zn (zm / 10 + 1) (ze' + 1) largeRange.min := by
    unfold Guard.doRoundUp at hok; simp only [Guard.doDropDigit] at hok
    have h_gP_ru_true : ((gP.round .upward == 1) || ((gP.round .upward == 0) && (zm % 2 == 1))) = true := by
      rw [h_gP_round]; rfl
    rw [h_gP_ru_true] at hok
    rw [if_neg h_not_noncusp, if_neg h_not_overflow] at hok
    rw [h_ru'_true] at hok
    simp only [if_true] at hok
    by_cases h_ovf : (Guard.bringIntoRange zn (zm / 10 + 1) (ze' + 1) largeRange.min).exponent_ > maxExponent
    · rw [if_pos h_ovf] at hok; simp at hok
    · rw [if_neg h_ovf] at hok; exact (Except.ok.inj hok).symm
  rw [hres_eq]
  have hne' : (Guard.bringIntoRange zn (zm / 10 + 1) (ze' + 1) largeRange.min).mantissa_ ≠ 0 := hres_eq ▸ h_mant_ne
  rw [bringIntoRange_rescale_result h_m1_lt_min h_m1_ne]
  by_cases h_under : ze' + 1 - 1 < minExponent ∨ (zm / 10 + 1) * 10 = 0
  · exfalso; apply hne'; rw [bringIntoRange_rescale_result h_m1_lt_min h_m1_ne, if_pos h_under]
  · push_neg at h_under; obtain ⟨hexp, hne10⟩ := h_under
    rw [if_neg (by push_neg; exact ⟨hexp, hne10⟩)]
    change (((zm / 10 + 1) * 10).toNat : ℚ) * 10 ^ (ze' + 1 - 1) = maxRepCuspTarget * 10 ^ ze'
    rw [h_m1_mul10_toNat]; rw [show (ze' + 1 - 1 : ℤ) = ze' from by ring]; push_cast; ring

/-- `.upward`-mode analogue of `doRoundUp_output_invariants_to_nearest`. -/
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
  unfold Guard.doRoundUp Guard.bringIntoRange at hok
  simp only [Guard.doDropDigit] at hok
  set gP : Guard := g.pushOverflow m .upward with hgP_def
  by_cases h_ru : (gP.round .upward == 1 || (gP.round .upward == 0 && m % 2 == 1)) = true
  · rw [show (gP.round .upward == 1 || (gP.round .upward == 0 && m % 2 == 1)) = true from h_ru] at hok
    by_cases h_m_eq_maxRep : m = maxRep
    · -- cusp case: m = maxRep → ¬(m < maxMantissa ∧ m < maxRep), ¬(maxRep < m ∧ ...)
      have h_not_noncusp : ¬ (m < largeRange.max ∧ m < maxRep) := by
        intro ⟨_, h⟩; have := UInt64.lt_iff_toNat_lt.mp h; rw [h_m_eq_maxRep, maxRep_val] at this; omega
      have h_not_overflow : ¬ (maxRep < m ∧ m < maxRepUp) := by
        intro ⟨h, _⟩; have := UInt64.lt_iff_toNat_lt.mp h; rw [h_m_eq_maxRep, maxRep_val] at this; omega
      rw [if_neg h_not_noncusp, if_neg h_not_overflow] at hok
      have h_m_div10_toNat : (m / 10).toNat = mantissaFloor := by
        rw [UInt64.toNat_div, h_m_eq_maxRep]; decide
      have h_gP_round_one : gP.round .upward = 1 := by
        rcases Bool.or_eq_true _ _ |>.mp h_ru with h1 | h2
        · exact beq_iff_eq.mp h1
        · exfalso
          rcases Bool.and_eq_true _ _ |>.mp h2 with ⟨hr0, _⟩
          have h_eq_0 : gP.round .upward = 0 := beq_iff_eq.mp hr0
          have hvals : gP.round .upward = 1 ∨ gP.round .upward = -1 ∨ gP.round .upward = -2 := by
            unfold Guard.round; split_ifs <;> first | (left; rfl) | (right; left; rfl) | (right; right; rfl)
          rcases hvals with h1 | hm1 | hm2
          · rw [h_eq_0] at h1; exact absurd h1 (by decide)
          · rw [h_eq_0] at hm1; exact absurd hm1 (by decide)
          · rw [h_eq_0] at hm2; exact absurd hm2 (by decide)
      have h_g_round_one : g.round .upward = 1 := by
        by_contra h_ne
        have h_pof_noop : gP = g :=
          hgP_def ▸ pushOverflow_noop_of_le_maxRep_of_round_ne_one h_ub g .upward h_ne
        rw [h_pof_noop] at h_gP_round_one
        exact h_ne h_gP_round_one
      have h_gP_eq3 : gP = g.push 3 := by
        rw [hgP_def, h_m_eq_maxRep]
        unfold Guard.pushOverflow
        rw [h_g_round_one]
        rfl
      have h_g_sbit : g.sbit_ = false := ((round_upward_eq_one_iff g).mp h_g_round_one).1
      set g' := gP.push (m % 10) with hg'_def
      have h_g'_sbit : g'.sbit_ = false := by
        rw [hg'_def]; unfold Guard.push; rw [h_gP_eq3]; unfold Guard.push; exact h_g_sbit
      have h_g'_digits_toNat : g'.digits_.toNat = gP.digits_.toNat / 16 + (m.toNat % 10 % 16) * 2 ^ 60 := by
        rw [hg'_def]; have := toNat_push_digits gP (m % 10)
        rw [UInt64.toNat_mod] at this; have h10 : (10 : UInt64).toNat = 10 := uint64_ten_toNat; rw [h10] at this; exact this
      have h_gP_digits_toNat : gP.digits_.toNat = g.digits_.toNat / 16 + (3 % 16) * 2 ^ 60 := by
        rw [h_gP_eq3]; exact toNat_push_digits g 3
      have h_m_mod10_mod16 : m.toNat % 10 % 16 = 7 := by rw [show m.toNat % 10 = 7 from by rw [h_m_eq_maxRep]; decide]
      have h_g'_digits_gt : g'.digits_ > 0 := by
        change (0 : UInt64) < g'.digits_; rw [UInt64.lt_iff_toNat_lt]
        rw [show (0 : UInt64).toNat = 0 from rfl, h_g'_digits_toNat, h_m_mod10_mod16, h_gP_digits_toNat]; omega
      have h_g'_round : g'.round .upward = 1 :=
        (round_upward_eq_one_iff g').mpr ⟨h_g'_sbit, Or.inl h_g'_digits_gt⟩
      have h_ru'_true :
          (g'.round .upward == 1 || (g'.round .upward == 0 && (m / 10) % 2 == 1)) = true := by
        rw [h_g'_round]; rfl
      rw [h_ru'_true] at hok; simp only [if_true] at hok
      have h_m_div10_le_maxRep : (m / 10).toNat ≤ maxRep.toNat := by
        rw [h_m_div10_toNat, maxRep_val]; norm_num
      have h_m1_toNat : (m / 10 + 1).toNat = mantissaFloorSucc := by
        rw [m_add_one_no_overflow h_m_div10_le_maxRep, h_m_div10_toNat]
      have h_m1_lt_min : (m / 10 + 1) < largeRange.min := by
        rw [UInt64.lt_iff_toNat_lt, h_m1_toNat, largeRange_min_val]; norm_num
      have h_m1_ne : m / 10 + 1 ≠ 0 := by
        intro h; have := UInt64.toNat_inj.mpr h; rw [m_add_one_no_overflow h_m_div10_le_maxRep, h_m_div10_toNat] at this; simp at this
      have h_m1_mul10_toNat : ((m / 10 + 1) * 10).toNat = maxRepCuspTarget := by
        rw [UInt64.toNat_mul]; have h10 : (10 : UInt64).toNat = 10 := uint64_ten_toNat; rw [h10, h_m1_toNat]
      rw [if_pos (And.intro h_m1_lt_min h_m1_ne)] at hok
      simp only [] at hok
      by_cases h_under : e + 1 - 1 < minExponent ∨ (m / 10 + 1) * 10 = 0
      · underflow_absurd hne h_under hok
      · push_neg at h_under; obtain ⟨hexp, hne10⟩ := h_under
        have h_not_under : ¬ (e + 1 - 1 < minExponent ∨ (m / 10 + 1) * 10 = 0) :=
          by push_neg; exact ⟨hexp, hne10⟩
        simp only [if_neg h_not_under] at hok
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
        have : m.toNat ≠ maxRep.toNat := fun heq => h_m_eq_maxRep (UInt64.toNat_inj.mp heq); omega
      -- noncusp branch fires
      have h_noncusp_d : m < largeRange.max ∧ m < maxRep := by
        constructor
        · exact UInt64.lt_iff_toNat_lt.mpr (by rw [largeRange_max_val, maxRep_val] at *; omega)
        · exact UInt64.lt_iff_toNat_lt.mpr h_m_lt_maxRep
      rw [if_pos h_noncusp_d] at hok
      simp only [if_true] at hok
      have h_m1_le_maxRep : (m + 1).toNat ≤ maxRep.toNat := by rw [hm_add1_toNat]; omega
      have h_m1_ne_nc : m + 1 ≠ 0 := by
        intro h; have := UInt64.toNat_inj.mpr h; rw [hm_add1_toNat] at this; simp at this
      by_cases h_resc : m + 1 < largeRange.min
      · rw [if_pos (And.intro h_resc h_m1_ne_nc)] at hok; simp only [] at hok
        have h_m1_lt_min : (m + 1).toNat < largeRange.min.toNat := UInt64.lt_iff_toNat_lt.mp h_resc
        have h_m1_mul10_toNat : ((m + 1) * 10).toNat = (m + 1).toNat * 10 := by
          rw [UInt64.toNat_mul]; have h10 : (10 : UInt64).toNat = 10 := uint64_ten_toNat; rw [h10]
          apply Nat.mod_eq_of_lt; rw [hminMant_v] at h_m1_lt_min
          calc (m + 1).toNat * 10 < 1000000000000000000 * 10 := Nat.mul_lt_mul_of_pos_right h_m1_lt_min (by norm_num)
            _ < 2 ^ 64 := by norm_num
        have h_m1_10_ne_nc : (m + 1) * 10 ≠ 0 := by
          intro h; have hnat : ((m + 1) * 10).toNat = 0 := by rw [h]; rfl
          rw [h_m1_mul10_toNat, hm_add1_toNat] at hnat; omega
        by_cases h_under : e - 1 < minExponent ∨ (m + 1) * 10 = 0
        · underflow_absurd hne h_under hok
        · have h_nu := h_under
          push_neg at h_under; obtain ⟨hexp, hne10⟩ := h_under
          simp only [if_neg h_nu] at hok
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
      · have h_no_resc_nc : ¬ (m + 1 < largeRange.min ∧ m + 1 ≠ 0) := by
          intro ⟨h, _⟩; exact h_resc h
        rw [if_neg h_no_resc_nc] at hok; simp only [] at hok
        have h_m1_ge_min : (m + 1).toNat ≥ largeRange.min.toNat := by
          by_contra h; push_neg at h; exact h_resc (UInt64.lt_iff_toNat_lt.mpr h)
        by_cases h_under : e < minExponent ∨ m + 1 = 0
        · underflow_absurd hne h_under hok
        · have h_nu := h_under
          push_neg at h_under; obtain ⟨hexp, hm1ne⟩ := h_under
          simp only [if_neg h_nu] at hok
          have h_no_ovf : ¬ (e > maxExponent) := by
            intro h_ovf; simp only [if_pos h_ovf] at hok; simp at hok
          simp only [if_neg h_no_ovf] at hok; obtain rfl := Except.ok.inj hok
          refine ⟨h_m1_ge_min, ?_, ?_, ?_⟩
          · change (m + 1).toNat ≤ largeRange.max.toNat
            rw [hm_add1_toNat, hmaxMant_v]; rw [hmaxRep_v] at h_m_lt_maxRep; omega
          · change minExponent ≤ e; exact hexp
          · change (m + 1).toNat > maxRep.toNat → (m + 1).toNat % 10 = 0
            intro h_gt; exfalso; have : (m + 1).toNat ≤ maxRep.toNat := h_m1_le_maxRep; omega
  · rw [Bool.not_eq_true] at h_ru
    rw [show (gP.round .upward == 1 || (gP.round .upward == 0 && m % 2 == 1)) = false from h_ru] at hok
    simp only [Bool.false_eq_true, ite_false] at hok
    have h_no_overflow_d : ¬ (maxRep < m ∧ m < maxRepUp) := by
      intro ⟨h, _⟩; have := UInt64.lt_iff_toNat_lt.mp h; omega
    rw [if_neg h_no_overflow_d] at hok
    have h_m_ne_d : m ≠ 0 := by intro h; rw [h] at h_lb; simp at h_lb
    by_cases h_resc : m < largeRange.min
    · rw [if_pos (And.intro h_resc h_m_ne_d)] at hok; simp only [] at hok
      have h_m_lt_min : m.toNat < largeRange.min.toNat := UInt64.lt_iff_toNat_lt.mp h_resc
      have h_m_mul10_toNat : (m * 10).toNat = m.toNat * 10 := m_mul_ten_no_overflow h_m_lt_min
      have h_m10_ne_d : m * 10 ≠ 0 := by
        intro h; have hnat : (m * 10).toNat = 0 := by rw [h]; rfl
        rw [h_m_mul10_toNat] at hnat; omega
      by_cases h_under : e - 1 < minExponent ∨ m * 10 = 0
      · underflow_absurd hne h_under hok
      · have h_nu := h_under
        push_neg at h_under; obtain ⟨hexp, hne10⟩ := h_under
        simp only [if_neg h_nu] at hok
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
    · have h_no_resc_d : ¬ (m < largeRange.min ∧ m ≠ 0) := by
        intro ⟨h, _⟩; exact h_resc h
      rw [if_neg h_no_resc_d] at hok; simp only [] at hok
      have h_m_ge_min : m.toNat ≥ largeRange.min.toNat := by
        by_contra h; push_neg at h; exact h_resc (UInt64.lt_iff_toNat_lt.mpr h)
      by_cases h_under : e < minExponent ∨ m = 0
      · underflow_absurd hne h_under hok
      · have h_nu := h_under
        push_neg at h_under; obtain ⟨hexp, hmne⟩ := h_under
        simp only [if_neg h_nu] at hok
        have h_no_ovf : ¬ (e > maxExponent) := by
          intro h_ovf; simp only [if_pos h_ovf] at hok; simp at hok
        simp only [if_neg h_no_ovf] at hok; obtain rfl := Except.ok.inj hok
        refine ⟨h_m_ge_min, ?_, ?_, ?_⟩
        · change m.toNat ≤ largeRange.max.toNat; rw [hmaxMant_v]; rw [hmaxRep_v] at h_ub; omega
        · change minExponent ≤ e; exact hexp
        · change m.toNat > maxRep.toNat → m.toNat % 10 = 0; intro h_gt; exfalso; omega

/-! ## Combined invariants up to `maxRepUp` — directed-mode dispatchers -/

/-- `.downward` invariants for `mantissaFloor ≤ m ≤ maxRepUp`. -/
lemma doRoundUp_output_invariants_downward_upTo_maxRepUp
    (g : Guard) (neg : Bool) (m : UInt64) (e : Int)
    (h_lb : mantissaFloor ≤ m.toNat)
    (h_ub : m.toNat ≤ maxRepUp.toNat)
    (loc : String) (res : RoundResult)
    (hok : g.doRoundUp neg m e largeRange.min largeRange.max .downward loc = .ok res)
    (hne : res.mantissa_ ≠ 0) :
    largeRange.min.toNat ≤ res.mantissa_.toNat ∧
    res.mantissa_.toNat ≤ largeRange.max.toNat ∧
    minExponent ≤ res.exponent_ ∧
    (res.mantissa_.toNat > maxRep.toNat → res.mantissa_.toNat % 10 = 0) := by
  by_cases h_le : m.toNat ≤ maxRep.toNat
  · exact doRoundUp_output_invariants_downward g neg m e h_lb h_le loc res hok hne
  · push_neg at h_le
    exact doRoundUp_output_invariants_cusp g neg m e .downward h_le h_ub loc res hok hne

/-- `.towards_zero` invariants for `mantissaFloor ≤ m ≤ maxRepUp`. -/
lemma doRoundUp_output_invariants_towards_zero_upTo_maxRepUp
    (g : Guard) (neg : Bool) (m : UInt64) (e : Int)
    (h_lb : mantissaFloor ≤ m.toNat)
    (h_ub : m.toNat ≤ maxRepUp.toNat)
    (loc : String) (res : RoundResult)
    (hok : g.doRoundUp neg m e largeRange.min largeRange.max .towards_zero loc = .ok res)
    (hne : res.mantissa_ ≠ 0) :
    largeRange.min.toNat ≤ res.mantissa_.toNat ∧
    res.mantissa_.toNat ≤ largeRange.max.toNat ∧
    minExponent ≤ res.exponent_ ∧
    (res.mantissa_.toNat > maxRep.toNat → res.mantissa_.toNat % 10 = 0) := by
  by_cases h_le : m.toNat ≤ maxRep.toNat
  · exact doRoundUp_output_invariants_towards_zero g neg m e h_lb h_le loc res hok hne
  · push_neg at h_le
    exact doRoundUp_output_invariants_cusp g neg m e .towards_zero h_le h_ub loc res hok hne

/-- `.upward` invariants for `mantissaFloor ≤ m ≤ maxRepUp`. -/
lemma doRoundUp_output_invariants_upward_upTo_maxRepUp
    (g : Guard) (neg : Bool) (m : UInt64) (e : Int)
    (h_lb : mantissaFloor ≤ m.toNat)
    (h_ub : m.toNat ≤ maxRepUp.toNat)
    (loc : String) (res : RoundResult)
    (hok : g.doRoundUp neg m e largeRange.min largeRange.max .upward loc = .ok res)
    (hne : res.mantissa_ ≠ 0) :
    largeRange.min.toNat ≤ res.mantissa_.toNat ∧
    res.mantissa_.toNat ≤ largeRange.max.toNat ∧
    minExponent ≤ res.exponent_ ∧
    (res.mantissa_.toNat > maxRep.toNat → res.mantissa_.toNat % 10 = 0) := by
  by_cases h_le : m.toNat ≤ maxRep.toNat
  · exact doRoundUp_output_invariants_upward g neg m e h_lb h_le loc res hok hne
  · push_neg at h_le
    exact doRoundUp_output_invariants_cusp g neg m e .upward h_le h_ub loc res hok hne

/-- Mode-generic combined invariants for `mantissaFloor ≤ m ≤ maxRepUp` — dispatches
on `mode` to the four directed lemmas above. Lets callers (mul/normalize) skip an
inline `cases mode`. -/
lemma doRoundUp_output_invariants_upTo_maxRepUp_anyMode
    (g : Guard) (neg : Bool) (m : UInt64) (e : Int) (mode : rounding_mode)
    (h_lb : mantissaFloor ≤ m.toNat)
    (h_ub : m.toNat ≤ maxRepUp.toNat)
    (loc : String) (res : RoundResult)
    (hok : g.doRoundUp neg m e largeRange.min largeRange.max mode loc = .ok res)
    (hne : res.mantissa_ ≠ 0) :
    largeRange.min.toNat ≤ res.mantissa_.toNat ∧
    res.mantissa_.toNat ≤ largeRange.max.toNat ∧
    minExponent ≤ res.exponent_ ∧
    (res.mantissa_.toNat > maxRep.toNat → res.mantissa_.toNat % 10 = 0) := by
  cases mode with
  | to_nearest =>
    exact doRoundUp_output_invariants_to_nearest_upTo_maxRepUp g neg m e h_lb h_ub loc res hok hne
  | towards_zero =>
    exact doRoundUp_output_invariants_towards_zero_upTo_maxRepUp g neg m e h_lb h_ub loc res hok hne
  | downward =>
    exact doRoundUp_output_invariants_downward_upTo_maxRepUp g neg m e h_lb h_ub loc res hok hne
  | upward =>
    exact doRoundUp_output_invariants_upward_upTo_maxRepUp g neg m e h_lb h_ub loc res hok hne

/-- Mode-generic combined invariants for the non-cusp range `mantissaFloor ≤ m ≤ maxRep`
— dispatches on `mode` to the four directed lemmas. -/
lemma doRoundUp_output_invariants_anyMode
    (g : Guard) (neg : Bool) (m : UInt64) (e : Int) (mode : rounding_mode)
    (h_lb : mantissaFloor ≤ m.toNat)
    (h_ub : m.toNat ≤ maxRep.toNat)
    (loc : String) (res : RoundResult)
    (hok : g.doRoundUp neg m e largeRange.min largeRange.max mode loc = .ok res)
    (hne : res.mantissa_ ≠ 0) :
    largeRange.min.toNat ≤ res.mantissa_.toNat ∧
    res.mantissa_.toNat ≤ largeRange.max.toNat ∧
    minExponent ≤ res.exponent_ ∧
    (res.mantissa_.toNat > maxRep.toNat → res.mantissa_.toNat % 10 = 0) := by
  cases mode with
  | to_nearest =>
    exact doRoundUp_output_invariants_to_nearest g neg m e h_lb h_ub loc res hok hne
  | towards_zero =>
    exact doRoundUp_output_invariants_towards_zero g neg m e h_lb h_ub loc res hok hne
  | downward =>
    exact doRoundUp_output_invariants_downward g neg m e h_lb h_ub loc res hok hne
  | upward =>
    exact doRoundUp_output_invariants_upward g neg m e h_lb h_ub loc res hok hne

set_option maxHeartbeats 1600000 in
-- 8-leaf navigation with per-leaf flush analysis.
/-- A flush (zero output mantissa) in `doRoundUp` forces the input frame to sit
at the representable floor: `(zm+1)·10^ze ≤ 10^18·10^minExponent`. Mode-generic;
the underflow branches of the discrete-rounding theorems consume this (the
exact value is `< (zm+1)·10^ze`, hence strictly below the smallest positive
representable). -/
lemma doRoundUp_flush_value_small
    (g : Guard) (zn : Bool) (zm : UInt64) (ze : Int) (mode : rounding_mode)
    (h_zm_ge : (mantissaFloor : ℕ) ≤ zm.toNat)
    (h_zm_le : zm.toNat ≤ maxRepUp.toNat)
    (loc : String) (res : RoundResult)
    (hok : g.doRoundUp zn zm ze largeRange.min largeRange.max mode loc = .ok res)
    (hres0 : res.mantissa_ = 0) :
    ((zm.toNat : ℚ) + 1) * 10 ^ ze ≤ (10 : ℚ) ^ (18 : ℕ) * (10 : ℚ) ^ (minExponent : ℤ) := by
  have hmaxRepUp_toNat : maxRepUp.toNat = maxRepUpNat := rfl
  -- The two closing shapes.
  have h_close_small : zm.toNat + 1 ≤ 10 ^ 18 → ze ≤ minExponent →
      ((zm.toNat : ℚ) + 1) * 10 ^ ze
        ≤ (10 : ℚ) ^ (18 : ℕ) * (10 : ℚ) ^ (minExponent : ℤ) := by
    intro hz he
    have hzq : ((zm.toNat : ℚ) + 1) ≤ (10 : ℚ) ^ (18 : ℕ) := by exact_mod_cast hz
    have hzp : (10 : ℚ) ^ ze ≤ (10 : ℚ) ^ (minExponent : ℤ) :=
      zpow_le_zpow_right₀ (by norm_num) he
    have h1 : (0 : ℚ) ≤ (10 : ℚ) ^ ze := le_of_lt (zpow_pos (by norm_num) _)
    have h2 : (0 : ℚ) ≤ ((zm.toNat : ℚ) + 1) := by positivity
    calc ((zm.toNat : ℚ) + 1) * 10 ^ ze
        ≤ (10 : ℚ) ^ (18 : ℕ) * 10 ^ ze := mul_le_mul_of_nonneg_right hzq h1
      _ ≤ (10 : ℚ) ^ (18 : ℕ) * (10 : ℚ) ^ (minExponent : ℤ) :=
          mul_le_mul_of_nonneg_left hzp (by positivity)
  have h_close_big : ze ≤ minExponent - 1 →
      ((zm.toNat : ℚ) + 1) * 10 ^ ze
        ≤ (10 : ℚ) ^ (18 : ℕ) * (10 : ℚ) ^ (minExponent : ℤ) := by
    intro he
    have hzq : ((zm.toNat : ℚ) + 1) ≤ (10 : ℚ) ^ (19 : ℕ) := by
      have hz : zm.toNat + 1 ≤ 10 ^ 19 := by
        rw [hmaxRepUp_toNat] at h_zm_le
        omega
      exact_mod_cast hz
    have hzp : (10 : ℚ) ^ ze ≤ (10 : ℚ) ^ (minExponent - 1 : ℤ) :=
      zpow_le_zpow_right₀ (by norm_num) he
    have h1 : (0 : ℚ) ≤ (10 : ℚ) ^ ze := le_of_lt (zpow_pos (by norm_num) _)
    have h_eq : (10 : ℚ) ^ (19 : ℕ) * (10 : ℚ) ^ (minExponent - 1 : ℤ)
        = (10 : ℚ) ^ (18 : ℕ) * (10 : ℚ) ^ (minExponent : ℤ) := by
      rw [show ((10 : ℚ) ^ (19 : ℕ)) = (10 : ℚ) ^ ((19 : ℕ) : ℤ) from (zpow_natCast 10 19).symm,
          show ((10 : ℚ) ^ (18 : ℕ)) = (10 : ℚ) ^ ((18 : ℕ) : ℤ) from (zpow_natCast 10 18).symm,
          ← zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0),
          ← zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0)]
      congr 1
    calc ((zm.toNat : ℚ) + 1) * 10 ^ ze
        ≤ (10 : ℚ) ^ (19 : ℕ) * 10 ^ ze := mul_le_mul_of_nonneg_right hzq h1
      _ ≤ (10 : ℚ) ^ (19 : ℕ) * (10 : ℚ) ^ (minExponent - 1 : ℤ) :=
          mul_le_mul_of_nonneg_left hzp (by positivity)
      _ = (10 : ℚ) ^ (18 : ℕ) * (10 : ℚ) ^ (minExponent : ℤ) := h_eq
  -- Per-leaf flush analysis: each leaf is a `bringIntoRange` whose zero output
  -- pins the exponent (and, on the rescale path, the mantissa) to the floor.
  have h_leaf : ∀ (m' : UInt64) (e' : Int),
      m' ≠ 0 →
      (Guard.bringIntoRange zn m' e' largeRange.min).mantissa_ = 0 →
      (m'.toNat < 10 ^ 18 ∧ e' - 1 < minExponent) ∨ (10 ^ 18 ≤ m'.toNat ∧ e' < minExponent) := by
    intro m' e' hm_ne h0
    have hmin_v : largeRange.min.toNat = 1000000000000000000 := largeRange_min_val
    have hm_pos : 0 < m'.toNat := by
      rcases Nat.eq_zero_or_pos m'.toNat with h | h
      · exact absurd (UInt64.toNat_inj.mp (by rw [h]; rfl)) hm_ne
      · exact h
    by_cases hresc : m' < largeRange.min
    · left
      have hm_lt : m'.toNat < 10 ^ 18 := by
        have h1 := UInt64.lt_iff_toNat_lt.mp hresc
        omega
      refine ⟨hm_lt, ?_⟩
      rw [bringIntoRange_rescale_result hresc hm_ne] at h0
      have h10 : (m' * 10).toNat = m'.toNat * 10 :=
        m_mul_ten_no_overflow (by omega)
      have h10_ne : m' * 10 ≠ 0 := by
        intro h
        have h1 : (m' * 10).toNat = 0 := by rw [h]; rfl
        omega
      by_cases h_under : e' - 1 < minExponent ∨ m' * 10 = 0
      · rcases h_under with h | h
        · exact h
        · exact absurd h h10_ne
      · rw [if_neg h_under] at h0
        have h0' : m' * 10 = 0 := h0
        exact absurd h0' h10_ne
    · right
      have hm_ge : 10 ^ 18 ≤ m'.toNat := by
        by_contra hlt
        push_neg at hlt
        apply hresc
        rw [UInt64.lt_iff_toNat_lt]
        omega
      refine ⟨hm_ge, ?_⟩
      rw [bringIntoRange_noscale_result (by intro ⟨h, _⟩; exact hresc h)] at h0
      by_cases h_under : e' < minExponent ∨ m' = 0
      · rcases h_under with h | h
        · exact h
        · exact absurd h hm_ne
      · rw [if_neg h_under] at h0
        have h0' : m' = 0 := h0
        exact absurd h0' hm_ne
  -- Navigate the doRoundUp leaves.
  unfold Guard.doRoundUp at hok
  simp only [Guard.doDropDigit] at hok
  set gP : Guard := g.pushOverflow zm mode with hgP_def
  have h_zm_ne : zm ≠ 0 := by
    intro h
    have : zm.toNat = 0 := by rw [h]; rfl
    omega
  -- Extract `res` from the overflow check.
  have h_extract : ∀ r' : RoundResult,
      (if r'.exponent_ > maxExponent then (.error loc : Except String RoundResult)
       else .ok r') = .ok res → res = r' := by
    intro r' h
    by_cases h_ovf : r'.exponent_ > maxExponent
    · rw [if_pos h_ovf] at h; cases h
    · rw [if_neg h_ovf] at h; exact (Except.ok.inj h).symm
  by_cases hb : ((gP.round mode == 1) || ((gP.round mode == 0) && (zm % 2 == 1))) = true
  · rw [if_pos hb] at hok
    by_cases hC1 : zm < largeRange.max ∧ zm < maxRep
    · -- round-up, in-range: m' = zm + 1.
      rw [if_pos hC1] at hok
      have hres_eq := h_extract _ hok
      have h_zm1_ne : zm + 1 ≠ 0 := by
        intro h
        have h1 : (zm + 1).toNat = 0 := by rw [h]; rfl
        have h2 : (zm + 1).toNat = zm.toNat + 1 := by
          rw [UInt64.toNat_add]
          have : zm.toNat < maxRep.toNat := UInt64.lt_iff_toNat_lt.mp hC1.2
          rw [maxRep_val] at this
          have : zm.toNat + 1 < 2 ^ 64 := by omega
          rw [Nat.mod_eq_of_lt (by simpa using this)]
          rfl
        omega
      have h0 : (Guard.bringIntoRange zn (zm + 1) ze largeRange.min).mantissa_ = 0 := by
        rw [← hres_eq]; exact hres0
      have h_zm1_toNat : (zm + 1).toNat = zm.toNat + 1 := by
        rw [UInt64.toNat_add]
        have : zm.toNat < maxRep.toNat := UInt64.lt_iff_toNat_lt.mp hC1.2
        rw [maxRep_val] at this
        rw [Nat.mod_eq_of_lt (by simpa using (by omega : zm.toNat + 1 < 2 ^ 64))]
        rfl
      rcases h_leaf (zm + 1) ze h_zm1_ne h0 with ⟨hm_lt, he⟩ | ⟨_, he⟩
      · exact h_close_small (by omega) (by omega)
      · exact h_close_big (by omega)
    · rw [if_neg hC1] at hok
      by_cases hC2 : maxRep < zm ∧ zm < maxRepUp
      · -- round-up, cusp interior: m' = maxRepUp ≥ 10^18, noscale.
        rw [if_pos hC2] at hok
        have hres_eq := h_extract _ hok
        have h0 : (Guard.bringIntoRange zn maxRepUp ze largeRange.min).mantissa_ = 0 := by
          rw [← hres_eq]; exact hres0
        rcases h_leaf maxRepUp ze (by decide) h0 with ⟨hm_lt, _⟩ | ⟨_, he⟩
        · exact absurd hm_lt (by decide)
        · exact h_close_big (by omega)
      · -- round-up, drop leaf: m' ∈ {zm/10, zm/10 + 1} < 10^18 at exponent ze + 1.
        rw [if_neg hC2] at hok
        have h_div_lt : zm.toNat / 10 + 1 < 10 ^ 18 := by omega
        have h_div_toNat : (zm / 10).toNat = zm.toNat / 10 := by
          rw [UInt64.toNat_div]; rfl
        by_cases hb' : (((gP.push (zm % 10)).round mode == 1)
            || (((gP.push (zm % 10)).round mode == 0) && (zm / 10 % 2 == 1))) = true
        · rw [if_pos hb'] at hok
          have hres_eq := h_extract _ hok
          have h_ne : zm / 10 + 1 ≠ 0 := by
            intro h
            have h1 : (zm / 10 + 1).toNat = 0 := by rw [h]; rfl
            have h2 : (zm / 10 + 1).toNat = zm.toNat / 10 + 1 := by
              rw [UInt64.toNat_add, h_div_toNat]
              rw [Nat.mod_eq_of_lt (by simpa using (by omega : zm.toNat / 10 + 1 < 2 ^ 64))]
              rfl
            omega
          have h0 : (Guard.bringIntoRange zn (zm / 10 + 1) (ze + 1) largeRange.min).mantissa_
              = 0 := by
            rw [← hres_eq]; exact hres0
          have h_toNat : (zm / 10 + 1).toNat = zm.toNat / 10 + 1 := by
            rw [UInt64.toNat_add, h_div_toNat]
            rw [Nat.mod_eq_of_lt (by simpa using (by omega : zm.toNat / 10 + 1 < 2 ^ 64))]
            rfl
          rcases h_leaf (zm / 10 + 1) (ze + 1) h_ne h0 with ⟨_, he⟩ | ⟨hm_ge, _⟩
          · exact h_close_big (by omega)
          · exfalso; omega
        · rw [if_neg hb'] at hok
          have hres_eq := h_extract _ hok
          have h_ne : zm / 10 ≠ 0 := by
            intro h
            have h1 : (zm / 10).toNat = 0 := by rw [h]; rfl
            rw [h_div_toNat] at h1
            omega
          have h0 : (Guard.bringIntoRange zn (zm / 10) (ze + 1) largeRange.min).mantissa_
              = 0 := by
            rw [← hres_eq]; exact hres0
          rcases h_leaf (zm / 10) (ze + 1) h_ne h0 with ⟨_, he⟩ | ⟨hm_ge, _⟩
          · exact h_close_big (by omega)
          · exfalso
            rw [h_div_toNat] at hm_ge
            omega
  · rw [if_neg hb] at hok
    by_cases hC2 : maxRep < zm ∧ zm < maxRepUp
    · -- truncate, cusp interior: m' = maxRep ≥ 10^18, noscale.
      rw [if_pos hC2] at hok
      have hres_eq := h_extract _ hok
      have h0 : (Guard.bringIntoRange zn maxRep ze largeRange.min).mantissa_ = 0 := by
        rw [← hres_eq]; exact hres0
      rcases h_leaf maxRep ze (by decide) h0 with ⟨hm_lt, _⟩ | ⟨_, he⟩
      · exact absurd hm_lt (by decide)
      · exact h_close_big (by omega)
    · -- truncate: m' = zm.
      rw [if_neg hC2] at hok
      have hres_eq := h_extract _ hok
      have h0 : (Guard.bringIntoRange zn zm ze largeRange.min).mantissa_ = 0 := by
        rw [← hres_eq]; exact hres0
      rcases h_leaf zm ze h_zm_ne h0 with ⟨hm_lt, he⟩ | ⟨_, he⟩
      · exact h_close_small (by omega) (by omega)
      · exact h_close_big (by omega)

end XRPL.Model.Protocol
