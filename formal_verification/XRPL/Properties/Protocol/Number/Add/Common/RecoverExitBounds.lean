import XRPL.Properties.Protocol.Number.Common.Notation
import XRPL.Model.Protocol.Number
import XRPL.Properties.Protocol.Number.Rounding.Guard
import XRPL.Properties.Protocol.Number.Rounding.ScaleDown
import XRPL.Properties.Protocol.Number.Add.Common.AlignDown
import XRPL.Properties.Protocol.Number.Add.Common.RecoverBasic
import XRPL.Properties.Protocol.Number.Add.Common.RecoverValue
import Mathlib.Tactic

set_option linter.style.longLine false
set_option linter.style.emptyLine false

namespace XRPL.Model.Protocol

set_option maxHeartbeats 4000000 in
-- heavy induction over recover.induct with multiple cases
/-- After running `recover` with fuel `≥ 34`, the loop's exit condition
holds at the output. The proof uses the value-preservation invariant
in `ℚ` to derive that when the loop has popped exactly 34 times (fuel
exhausted at fuel = 34), the output mantissa is at least `10^18`. -/
theorem recover_34_exits
    (m : UInt64) (e : Int) (g : Guard)
    (hm_pos : 1 ≤ m.toNat)
    (hall : allNibblesAtMost9 g.digits_) :
    ¬ ((Number.operator_add.recover m e g 34).1 < largeRange.min
        ∧ (Number.operator_add.recover m e g 34).1 * 10 ≤ maxRep) := by
  intro hcond
  -- Case split on whether the loop exits or fuel is exhausted.
  rcases recover_output_exits_or_fuel_exhausted m e g 34 with hexit | hfuel
  · exact hexit hcond
  · -- hfuel : (recover m e g 34).2.1 = e - 34. So k = 34.
    -- Use value-preservation to bound m_34.toNat ≥ 10^18.
    have hB_pos : (0 : ℚ) < 1 / 10 ^ 16 := by positivity
    obtain ⟨x_after, k, hk_le, hk_eq, hxa_nn, _hxa_lt, _hall_after, heq⟩ :=
      recover_value_preserved_tight m e g 34 hm_pos hall 0 (1 / 10 ^ 16)
        hB_pos (le_refl 0) hB_pos
    set r := Number.operator_add.recover m e g 34 with hr_def
    -- k = 34 from hk_eq and hfuel.
    have hk_34 : k = 34 := by
      have heq2 : (e - (k : ℤ) : ℤ) = e - (34 : ℤ) := by
        rw [← hk_eq]; exact hfuel
      have hkk : (k : ℤ) = 34 := by linarith
      exact_mod_cast hkk
    -- LHS lower bound: (m_0 - dv_0/10^16 - 0) ≥ 1/10^16.
    have h_dv0_le : (decimalValue g.digits_ : ℚ) ≤ (10 : ℚ) ^ 16 - 1 := by
      have h_nat : decimalValue g.digits_ < 10 ^ 16 := decimalValue_lt_pow_16 hall
      have h_nat_le : decimalValue g.digits_ ≤ 10 ^ 16 - 1 := by omega
      have h_q_le : (decimalValue g.digits_ : ℚ) ≤ ((10 ^ 16 - 1 : ℕ) : ℚ) := by
        exact_mod_cast h_nat_le
      have h_simp : (((10 ^ 16 - 1 : ℕ) : ℚ)) = (10 : ℚ) ^ 16 - 1 := by
        push_cast; norm_num
      rw [h_simp] at h_q_le
      exact h_q_le
    have h_div_le : (decimalValue g.digits_ : ℚ) / 10 ^ 16 ≤ 1 - 1 / 10 ^ 16 := by
      have h_div_le : (decimalValue g.digits_ : ℚ) / 10 ^ 16 ≤ ((10 : ℚ) ^ 16 - 1) / 10 ^ 16 := by
        apply div_le_div_of_nonneg_right h_dv0_le (by positivity)
      have h_simp : ((10 : ℚ) ^ 16 - 1) / 10 ^ 16 = 1 - 1 / 10 ^ 16 := by
        field_simp
      rw [h_simp] at h_div_le
      exact h_div_le
    have h_m0_q : (1 : ℚ) ≤ (m.toNat : ℚ) := by exact_mod_cast hm_pos
    have h_lhs_ge : (1 : ℚ) / 10 ^ 16 ≤
        ((m.toNat : ℚ) - ((decimalValue g.digits_ : ℚ) / 10 ^ 16 + 0)) := by
      linarith
    -- Multiply by 10^e:
    have h_pow_e_pos : (0 : ℚ) < (10 : ℚ) ^ e := by positivity
    have h_lhs_full_ge : (1 : ℚ) / 10 ^ 16 * (10 : ℚ) ^ e ≤
        ((m.toNat : ℚ) - ((decimalValue g.digits_ : ℚ) / 10 ^ 16 + 0)) * (10 : ℚ) ^ e := by
      apply mul_le_mul_of_nonneg_right h_lhs_ge (le_of_lt h_pow_e_pos)
    rw [heq] at h_lhs_full_ge
    -- Now h_lhs_full_ge : 1/10^16 * 10^e ≤ (m_k - dv_k/10^16 - x_after) * 10^{e-k}
    -- Substitute k = 34 and rewrite 10^e = 10^{e-34} * 10^34.
    have h_pow_split : (10 : ℚ) ^ e = (10 : ℚ) ^ (e - (k : ℤ)) * (10 : ℚ) ^ (k : ℤ) := by
      rw [← zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0)]
      congr 1; ring
    have h_pow_nat : (10 : ℚ) ^ (k : ℤ) = (10 : ℚ) ^ k := by rw [zpow_natCast]
    rw [h_pow_split, h_pow_nat] at h_lhs_full_ge
    have h_pow_ek_pos : (0 : ℚ) < (10 : ℚ) ^ (e - (k : ℤ)) := by positivity
    -- LHS: (1/10^16) * (10^{e-k} * 10^k) = (10^k / 10^16) * 10^{e-k}
    -- RHS: (m_k - dv_k/10^16 - x_after) * 10^{r.2.1} = (...) * 10^{e-k}
    have h_combine : ((10 : ℚ) ^ k / 10 ^ 16) * (10 : ℚ) ^ (e - (k : ℤ)) ≤
        ((r.1.toNat : ℚ) - ((decimalValue r.2.2.digits_ : ℚ) / 10 ^ 16 + x_after)) *
          (10 : ℚ) ^ (e - (k : ℤ)) := by
      have h_rewrite_lhs : (1 : ℚ) / 10 ^ 16 * ((10 : ℚ) ^ (e - (k : ℤ)) * (10 : ℚ) ^ k)
          = ((10 : ℚ) ^ k / 10 ^ 16) * (10 : ℚ) ^ (e - (k : ℤ)) := by ring
      rw [h_rewrite_lhs] at h_lhs_full_ge
      rw [hk_eq] at h_lhs_full_ge
      exact h_lhs_full_ge
    have h_div : (10 : ℚ) ^ k / 10 ^ 16 ≤
        ((r.1.toNat : ℚ) - ((decimalValue r.2.2.digits_ : ℚ) / 10 ^ 16 + x_after)) := by
      -- Cancel the (* 10^{e-k}) factor on both sides since 10^{e-k} > 0.
      exact le_of_mul_le_mul_right h_combine h_pow_ek_pos
    -- Substitute k = 34: 10^34 / 10^16 = 10^18.
    have h_pow_div : (10 : ℚ) ^ k / 10 ^ 16 = 10 ^ 18 := by
      rw [hk_34]
      rw [show (10 : ℚ) ^ 34 = (10 : ℚ) ^ 18 * 10 ^ 16 from by norm_num]
      field_simp
    rw [h_pow_div] at h_div
    -- So m_k - dv_k/10^16 - x_after ≥ 10^18, hence m_k ≥ 10^18.
    have h_dvk_nn : (0 : ℚ) ≤ (decimalValue r.2.2.digits_ : ℚ) / 10 ^ 16 := by positivity
    have h_mk_ge : (10 : ℚ) ^ 18 ≤ (r.1.toNat : ℚ) := by linarith
    -- Cast to Nat: r.1.toNat ≥ 10^18.
    have h_mk_ge_nat : (10 : ℕ) ^ 18 ≤ r.1.toNat := by
      have : ((10 : ℕ) ^ 18 : ℚ) ≤ (r.1.toNat : ℚ) := by
        rw [show ((10 : ℕ) ^ 18 : ℚ) = (10 : ℚ) ^ 18 from by push_cast; rfl]
        exact h_mk_ge
      exact_mod_cast this
    -- Now derive contradiction with hcond: r.1 < largeRange.min = 10^18.
    have h_mk_lt : r.1.toNat < 10 ^ 18 := by
      have := UInt64.lt_iff_toNat_lt.mp hcond.1
      rw [largeRange_min_toNat] at this; exact this
    omega

set_option maxHeartbeats 400000 in
-- elaboration of large existential / induction
/-- For any fuel `≥ 34`, `recover m e g fuel = recover m e g 34`.
This follows from `recover_34_exits` and `recover_fuel_add_of_exits`. -/
theorem recover_eq_34_of_ge
    (m : UInt64) (e : Int) (g : Guard) (fuel : ℕ)
    (hm_pos : 1 ≤ m.toNat)
    (hall : allNibblesAtMost9 g.digits_)
    (hfuel : 34 ≤ fuel) :
    Number.operator_add.recover m e g fuel = Number.operator_add.recover m e g 34 := by
  obtain ⟨extra, hextra⟩ := Nat.exists_eq_add_of_le hfuel
  rw [hextra]
  exact recover_fuel_add_of_exits (recover_34_exits m e g hm_pos hall) extra

/-- Strict version of the recover floor: the exit condition forces the mantissa
**strictly** above `mantissaFloor`. Both disjuncts dominate strictly:
`m ≥ 10^18 = 1000000000000000000 > floor`, and
`m * 10 > maxRep ⟹ m ≥ mantissaFloorSucc > floor`. -/
lemma recover_exit_mantissa_gt_floor {m : UInt64}
    (hexit : ¬ (m < largeRange.min ∧ m * 10 ≤ maxRep)) :
    (mantissaFloor : ℕ) < m.toNat := by
  rw [not_and_or] at hexit
  rcases hexit with h1 | h2
  · rw [UInt64.lt_iff_toNat_lt, largeRange_min_toNat] at h1
    omega
  · rw [UInt64.le_iff_toNat_le, maxRep_toNat_val, UInt64.toNat_mul] at h2
    have h10 : (10 : UInt64).toNat = 10 := rfl
    rw [h10] at h2
    have hmod_le : (m.toNat * 10) % 2 ^ 64 ≤ m.toNat * 10 := Nat.mod_le _ _
    omega

/-- Strict recover floor: the recover output mantissa is **strictly** above the
floor `mantissaFloor`. -/
lemma recover_mantissa_gt_floor
    (m : UInt64) (e : Int) (g : Guard) (fuel : ℕ)
    (hm_pos : 1 ≤ m.toNat)
    (hall : allNibblesAtMost9 g.digits_)
    (hfuel : 34 ≤ fuel) :
    (mantissaFloor : ℕ) < (Number.operator_add.recover m e g fuel).1.toNat := by
  rw [recover_eq_34_of_ge m e g fuel hm_pos hall hfuel]
  exact recover_exit_mantissa_gt_floor (recover_34_exits m e g hm_pos hall)

/-- The recover output mantissa stays below `10^19`. In the step branch the new
mantissa is `m*10 - d ≤ m*10 ≤ maxRep < 10^19`; in the exit/noop branch it equals
the input, which is `< 10^19` by hypothesis. -/
lemma recover_mantissa_lt
    (m : UInt64) (e : Int) (g : Guard) (fuel : ℕ)
    (hm_pos : 1 ≤ m.toNat)
    (hall : allNibblesAtMost9 g.digits_)
    (hm_lt : m.toNat < 10 ^ 19) :
    (Number.operator_add.recover m e g fuel).1.toNat < 10 ^ 19 := by
  induction m, e, g, fuel using Number.operator_add.recover.induct with
  | case1 m e g => rw [recover_noop_zero]; exact hm_lt
  | case2 m e g fuel hcond g' d hpop IH =>
    rw [Bool.and_eq_true] at hcond
    obtain ⟨hc1, hc2⟩ := hcond
    have h1 := of_decide_eq_true hc1
    have h2 := of_decide_eq_true hc2
    rw [recover_step h1 h2]
    have hpop1 : g.pop.1 = g' := by rw [hpop]
    have hpop2 : g.pop.2 = d := by rw [hpop]
    rw [hpop1, hpop2]
    have hd_le : g.pop.2.toNat ≤ 9 := pop_digit_le_9 g hall
    have hd_le' : d.toNat ≤ 9 := by rw [hpop2] at hd_le; exact hd_le
    have hm_lt_min : m.toNat < largeRange.min.toNat := UInt64.lt_iff_toNat_lt.mp h1
    have hm10_toNat : (m * 10).toNat = m.toNat * 10 := mul_ten_no_overflow_of_lt_lr_min h1
    have hd_le_m10 : d ≤ m * 10 := pop_digit_le_mul_ten_of_pos hm_pos h1 hd_le'
    have hnew_toNat : (m * 10 - d).toNat = m.toNat * 10 - d.toNat :=
      recover_step_mantissa_nat h1 hd_le_m10
    have hnew_pos : 1 ≤ (m * 10 - d).toNat := by rw [hnew_toNat]; omega
    have hnew_lt : (m * 10 - d).toNat < 10 ^ 19 := by
      rw [UInt64.le_iff_toNat_le, maxRep_toNat_val, hm10_toNat] at h2
      rw [hnew_toNat]; omega
    have hall' : allNibblesAtMost9 g'.digits_ := by
      have := allNibblesAtMost9_pop g hall
      rw [hpop1] at this; exact this
    exact IH hnew_pos hall' hnew_lt
  | case3 m e g fuel hcond =>
    have hprop : ¬ (m < largeRange.min ∧ m * 10 ≤ maxRep) := by
      intro ⟨hm1, hm2⟩
      apply hcond
      rw [Bool.and_eq_true]
      exact ⟨decide_eq_true hm1, decide_eq_true hm2⟩
    rw [recover_noop_exit hprop]; exact hm_lt


end XRPL.Model.Protocol
