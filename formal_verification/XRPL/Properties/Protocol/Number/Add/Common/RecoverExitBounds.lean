import XRPL.Properties.Protocol.Number.Common.Notation
import XRPL.Model.Protocol.Number
import XRPL.Properties.Protocol.Number.Common.Rounding.Guard
import XRPL.Properties.Protocol.Number.Common.Rounding.ScaleDown
import XRPL.Properties.Protocol.Number.Add.Common.AlignDown
import XRPL.Properties.Protocol.Number.Add.Common.RecoverBasic
import XRPL.Properties.Protocol.Number.Add.Common.RecoverValue
import Mathlib.Tactic


namespace XRPL.Model.Protocol

/-- The loop-exit condition, case-split: mantissa reached `upperLimit`, or the
guard emptied. -/
lemma recover_exit_cases {upperLimit m : UInt128} {g : Guard}
    (hexit : ¬ (m < upperLimit ∧ ¬ g.empty)) :
    upperLimit.toNat ≤ m.toNat ∨ g.empty = true := by
  rw [not_and_or, not_not] at hexit
  rcases hexit with h1 | h2
  · left
    exact Nat.le_of_not_lt (fun hc => h1 (BitVec.lt_def.mpr hc))
  · right; exact h2

set_option maxHeartbeats 16000000 in
-- heavy: value-preservation instantiation + rational arithmetic (fuel-40 recover terms)
/-- After running `recover` with the model's `upperLimit = 10^21` and fuel `40`,
the loop's exit condition holds at the output. If all 40 iterations had popped,
value preservation would force the output mantissa to at least
`10^(40-16) = 10^24 > 10^21`, contradicting `m < upperLimit`. -/
theorem recover_40_exits
    (m : UInt128) (e : Int) (g : Guard)
    (hm_pos : 1 ≤ m.toNat)
    (hall : allNibblesAtMost9 g.digits_) :
    ¬ (((Number.operator_add.recover (toUInt128 largeRange.min * 1000) m e g 40).1
          < toUInt128 largeRange.min * 1000)
        ∧ ¬ (Number.operator_add.recover (toUInt128 largeRange.min * 1000) m e g 40).2.2.empty) := by
  have h1000n : (1000 : UInt64).toNat = 1000 := rfl
  have hUL_val : (toUInt128 largeRange.min * 1000).toNat = 10 ^ 21 := by
    rw [show (1000 : UInt128) = toUInt128 (1000 : UInt64) from rfl,
        uint128_of_uint64_mul_toNat largeRange.min 1000
          (by rw [largeRange_min_toNat, h1000n]; norm_num),
        largeRange_min_toNat, h1000n]
    norm_num
  have hUL : (toUInt128 largeRange.min * 1000).toNat * 10 < 2 ^ 128 := by
    rw [hUL_val]; norm_num
  intro hcond
  rcases recover_output_exits_or_fuel_exhausted (toUInt128 largeRange.min * 1000) m e g 40
    with hexit | hfuel
  · exact hexit hcond
  · have hB_pos : (0 : ℚ) < 1 / 10 ^ 16 := by positivity
    obtain ⟨x_after, k, hk_le, hk_eq, hxa_nn, _hxa_lt, _hxa_pos, _hall_after, heq⟩ :=
      recover_value_preserved_tight (toUInt128 largeRange.min * 1000) m hUL e g 40 hm_pos hall
        0 (1 / 10 ^ 16) hB_pos (le_refl 0) hB_pos
    set r := Number.operator_add.recover (toUInt128 largeRange.min * 1000) m e g 40 with hr_def
    -- k = 40 from hk_eq and hfuel.
    have hk_40 : k = 40 := by
      have heq2 : (e - (k : ℤ) : ℤ) = e - (40 : ℤ) := by
        rw [← hk_eq]; exact hfuel
      have hkk : (k : ℤ) = 40 := by linarith
      exact_mod_cast hkk
    -- LHS lower bound: (m₀ - dv₀/10^16 - 0) ≥ 1/10^16.
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
      have h_div_le' : (decimalValue g.digits_ : ℚ) / 10 ^ 16 ≤ ((10 : ℚ) ^ 16 - 1) / 10 ^ 16 := by
        apply div_le_div_of_nonneg_right h_dv0_le (by positivity)
      have h_simp : ((10 : ℚ) ^ 16 - 1) / 10 ^ 16 = 1 - 1 / 10 ^ 16 := by
        field_simp
      rw [h_simp] at h_div_le'
      exact h_div_le'
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
    -- Rewrite 10^e = 10^{e-k} * 10^k.
    have h_pow_split : (10 : ℚ) ^ e = (10 : ℚ) ^ (e - (k : ℤ)) * (10 : ℚ) ^ (k : ℤ) := by
      rw [← zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0)]
      congr 1; ring
    have h_pow_nat : (10 : ℚ) ^ (k : ℤ) = (10 : ℚ) ^ k := by rw [zpow_natCast]
    rw [h_pow_split, h_pow_nat] at h_lhs_full_ge
    have h_pow_ek_pos : (0 : ℚ) < (10 : ℚ) ^ (e - (k : ℤ)) := by positivity
    have h_combine : ((10 : ℚ) ^ k / 10 ^ 16) * (10 : ℚ) ^ (e - (k : ℤ)) ≤
        ((r.1.toNat : ℚ) - ((decimalValue r.2.2.digits_ : ℚ) / 10 ^ 16 + x_after)) *
          (10 : ℚ) ^ (e - (k : ℤ)) := by
      have h_rewrite_lhs : (1 : ℚ) / 10 ^ 16 * ((10 : ℚ) ^ (e - (k : ℤ)) * (10 : ℚ) ^ k)
          = ((10 : ℚ) ^ k / 10 ^ 16) * (10 : ℚ) ^ (e - (k : ℤ)) := by ring
      rw [h_rewrite_lhs] at h_lhs_full_ge
      rw [hk_eq] at h_lhs_full_ge
      exact h_lhs_full_ge
    have h_div : (10 : ℚ) ^ k / 10 ^ 16 ≤
        ((r.1.toNat : ℚ) - ((decimalValue r.2.2.digits_ : ℚ) / 10 ^ 16 + x_after)) :=
      le_of_mul_le_mul_right h_combine h_pow_ek_pos
    -- Substitute k = 40: 10^40 / 10^16 = 10^24.
    have h_pow_div : (10 : ℚ) ^ k / 10 ^ 16 = 10 ^ 24 := by
      rw [hk_40]
      rw [show (10 : ℚ) ^ 40 = (10 : ℚ) ^ 24 * 10 ^ 16 from by norm_num]
      field_simp
    rw [h_pow_div] at h_div
    -- So m_k - dv_k/10^16 - x_after ≥ 10^24, hence m_k ≥ 10^24.
    have h_dvk_nn : (0 : ℚ) ≤ (decimalValue r.2.2.digits_ : ℚ) / 10 ^ 16 := by positivity
    have h_mk_ge : (10 : ℚ) ^ 24 ≤ (r.1.toNat : ℚ) := by linarith
    have h_mk_ge_nat : (10 : ℕ) ^ 24 ≤ r.1.toNat := by
      have : ((10 : ℕ) ^ 24 : ℚ) ≤ (r.1.toNat : ℚ) := by
        rw [show ((10 : ℕ) ^ 24 : ℚ) = (10 : ℚ) ^ 24 from by push_cast; rfl]
        exact h_mk_ge
      exact_mod_cast this
    -- Contradiction with hcond: r.1 < upperLimit = 10^21.
    have h_mk_lt : r.1.toNat < 10 ^ 21 := by
      have := BitVec.lt_def.mp hcond.1
      rw [hUL_val] at this; exact this
    omega

/-- The recover output mantissa stays below `upperLimit.toNat * 10`: each popping
step starts from `m < upperLimit`, so the new mantissa `m*10 - d` stays below
`upperLimit * 10`; the exit/noop branches return the input, bounded by hypothesis. -/
lemma recover_mantissa_lt
    (upperLimit m : UInt128) (hUL : upperLimit.toNat * 10 < 2 ^ 128)
    (e : Int) (g : Guard) (fuel : ℕ)
    (hm_pos : 1 ≤ m.toNat)
    (hall : allNibblesAtMost9 g.digits_)
    (hm_lt : m.toNat < upperLimit.toNat * 10) :
    (Number.operator_add.recover upperLimit m e g fuel).1.toNat < upperLimit.toNat * 10 := by
  induction m, e, g, fuel using Number.operator_add.recover.induct (upperLimit := upperLimit) with
  | case1 m e g => rw [recover_noop_zero]; exact hm_lt
  | case2 m e g fuel hcond g' d hpop IH =>
    obtain ⟨h1, h2⟩ := hcond
    rw [recover_step h1 h2]
    have hpop1 : g.pop.1 = g' := by rw [hpop]
    have hpop2 : g.pop.2 = d := by rw [hpop]
    rw [hpop1, hpop2]
    have hd_le' : d.toNat ≤ 9 := by
      rw [← hpop2]; exact pop_digit_le_9 g hall
    have hm_lt_ul : m.toNat < upperLimit.toNat := BitVec.lt_def.mp h1
    have hd_le_m10 : toUInt128 d ≤ m * 10 := by
      rw [← hpop2]
      exact pop_digit_le_mul_ten_of_pos128 hUL hm_pos h1 (pop_digit_le_9 g hall)
    have hnew_toNat : (m * 10 - toUInt128 d).toNat = m.toNat * 10 - d.toNat :=
      recover_step_mantissa_nat128 hUL h1 hd_le_m10
    have hnew_pos : 1 ≤ (m * 10 - toUInt128 d).toNat := by
      rw [hnew_toNat]; omega
    have hnew_lt : (m * 10 - toUInt128 d).toNat < upperLimit.toNat * 10 := by
      rw [hnew_toNat]; omega
    have hall' : allNibblesAtMost9 g'.digits_ := by
      have := allNibblesAtMost9_pop g hall
      rw [hpop1] at this; exact this
    exact IH hnew_pos hall' hnew_lt
  | case3 m e g fuel hcond =>
    rw [recover_noop_exit hcond]; exact hm_lt

end XRPL.Model.Protocol
