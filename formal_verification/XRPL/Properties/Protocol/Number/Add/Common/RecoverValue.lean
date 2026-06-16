import Mathlib.Tactic

import XRPL.Model.Protocol.Number
import XRPL.Properties.Protocol.Number.Add.Common.AlignDown
import XRPL.Properties.Protocol.Number.Add.Common.RecoverBasic
import XRPL.Properties.Protocol.Number.Common.Rounding.Guard
import XRPL.Properties.Protocol.Number.Common.Rounding.ScaleDown


namespace XRPL.Model.Protocol


/-! ## Value preservation across the `recover` loop -/

theorem recover_value_preserved
    (upperLimit m : UInt128) (hUL : upperLimit.toNat * 10 < 2 ^ 128)
    (e : Int) (g : Guard) (fuel : ℕ)
    (hm_pos : 1 ≤ m.toNat)
    (hall : allNibblesAtMost9 g.digits_)
    (x : ℚ) :
    ∃ x_after : ℚ,
      ((m.toNat : ℚ)
          - ((decimalValue g.digits_ : ℚ) / 10 ^ 16 + x)) * (10 : ℚ) ^ e
        = (((Number.operator_add.recover upperLimit m e g fuel).1.toNat : ℚ)
            - ((decimalValue (Number.operator_add.recover upperLimit m e g fuel).2.2.digits_ : ℚ)
                  / 10 ^ 16 + x_after))
          * (10 : ℚ) ^ ((Number.operator_add.recover upperLimit m e g fuel).2.1) := by
  induction m, e, g, fuel using Number.operator_add.recover.induct (upperLimit := upperLimit)
    generalizing x with
  | case1 m e g =>
    refine ⟨x, ?_⟩
    rw [recover_noop_zero]
  | case2 m e g fuel hcond g' d hpop IH =>
    obtain ⟨h1, h2⟩ := hcond
    rw [recover_step h1 h2]
    have hpop1 : g.pop.1 = g' := by rw [hpop]
    have hpop2 : g.pop.2 = d := by rw [hpop]
    rw [hpop1, hpop2]
    -- Step parameters: digit bound, no-underflow, no-overflow, etc.
    have hd_le_9 : d.toNat ≤ 9 := by
      rw [← hpop2]; exact pop_digit_le_9 g hall
    have hd_le_mt : toUInt128 d ≤ m * 10 := by
      rw [← hpop2]
      exact pop_digit_le_mul_ten_of_pos128 hUL hm_pos h1 (pop_digit_le_9 g hall)
    have hm_new_nat : (m * 10 - toUInt128 d).toNat = m.toNat * 10 - d.toNat :=
      recover_step_mantissa_nat128 hUL h1 hd_le_mt
    have hm10ge : 10 ≤ m.toNat * 10 := by nlinarith
    -- Preserved invariant: new mantissa is still ≥ 1
    have hm_new_pos : 1 ≤ (m * 10 - toUInt128 d).toNat := by
      rw [hm_new_nat]; omega
    -- `pop` preserves nibble-validity
    have hall_eq : allNibblesAtMost9 g'.digits_ := by
      rw [← hpop1]; exact allNibblesAtMost9_pop g hall
    -- Apply IH with new hidden fraction `10 * x` (one digit pulled out)
    obtain ⟨x_after, hIH_eq⟩ := IH hm_new_pos hall_eq (10 * x)
    refine ⟨x_after, ?_⟩
    rw [← hIH_eq]
    -- Algebraic step: combine `decimalValue_pop_rat` and `recover_step_mantissa_nat`
    have h_dv : (decimalValue g'.digits_ : ℚ)
        = 10 * (decimalValue g.digits_ : ℚ) - (d.toNat : ℚ) * 10 ^ 16 := by
      rw [← hpop1, decimalValue_pop_rat g]
      have hd_eq : (g.pop.2.toNat : ℚ) = (nibble g.digits_ 15 : ℚ) := by
        exact_mod_cast pop_digit_eq_nibble_15 g
      rw [hpop2] at hd_eq
      linarith
    have h_mnew_q : ((m * 10 - toUInt128 d).toNat : ℚ) = (m.toNat : ℚ) * 10 - (d.toNat : ℚ) := by
      rw [hm_new_nat]
      have hle : d.toNat ≤ m.toNat * 10 := by omega
      rw [Nat.cast_sub hle]
      push_cast; ring
    rw [h_mnew_q, h_dv]
    -- Decompose `10 ^ e = 10 ^ (e - 1) * 10`
    have he_pow : (10 : ℚ) ^ e = (10 : ℚ) ^ (e - 1) * 10 := by
      rw [show e = (e - 1) + 1 from by ring, zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0)]
      simp
    rw [he_pow]
    -- Close by clearing denominators and `ring`.
    field_simp
    ring
  | case3 m e g fuel hcond =>
    refine ⟨x, ?_⟩
    have hprop : ¬ (m < upperLimit ∧ ¬ g.empty) := hcond
    rw [recover_noop_exit hprop]

/-- Strengthened clone of `recover_value_preserved`: when the input guard
`g` *represents* `f_g` (full `represents` semantics, not just the algebraic
residue), and the input guard's `xbit_` would not co-occur with a pop step,
then the output guard *represents* some fraction `f_exit`, and the diff-sign
value `(m - f_g) * 10^e` is preserved.

The `hdich` precondition says: if the input guard has a positive hidden tail
(`xbit_ = true`), then the loop does not pop on the first step. This is the
invariant that lets `represents` (with its tight `1/10^16` hidden bound) be
threaded through the loop: whenever a pop actually happens, the popped digit
came from a guard with `xbit_ = false`, hence hidden fraction `0`, so
`represents_pop_of_xbit_false` applies and the fresh guard again has
`xbit_ = false` — re-establishing `hdich` vacuously for the recursive call. -/
theorem recover_preserves_represents
    (upperLimit m : UInt128) (hUL : upperLimit.toNat * 10 < 2 ^ 128)
    (e : Int) (g : Guard) (fuel : ℕ)
    (hm_pos : 1 ≤ m.toNat)
    {f_g : ℚ} (hrep : represents g f_g)
    (hdich : g.xbit_ = true →
      ¬ (m < upperLimit ∧ ¬ g.empty)) :
    ∃ f_exit : ℚ,
      represents (Number.operator_add.recover upperLimit m e g fuel).2.2 f_exit ∧
      ((m.toNat : ℚ) - f_g) * (10 : ℚ) ^ e
        = (((Number.operator_add.recover upperLimit m e g fuel).1.toNat : ℚ) - f_exit)
            * (10 : ℚ) ^ ((Number.operator_add.recover upperLimit m e g fuel).2.1) := by
  induction m, e, g, fuel using Number.operator_add.recover.induct (upperLimit := upperLimit)
    generalizing f_g with
  | case1 m e g =>
    refine ⟨f_g, hrep, ?_⟩
    rw [recover_noop_zero]
  | case2 m e g fuel hcond g' d hpop IH =>
    obtain ⟨h1, h2⟩ := hcond
    -- nibble-validity of `g` (from `represents`)
    obtain ⟨_, _, _, _, _, hall⟩ := id hrep
    -- The pop happens, so `xbit_` must be false (contrapositive of `hdich`).
    have hxbit_false : g.xbit_ = false := by
      by_contra hx
      have hxt : g.xbit_ = true := by
        cases h : g.xbit_ with
        | false => exact absurd h hx
        | true => rfl
      exact (hdich hxt) ⟨h1, h2⟩
    rw [recover_step h1 h2]
    have hpop1 : g.pop.1 = g' := by rw [hpop]
    have hpop2 : g.pop.2 = d := by rw [hpop]
    rw [hpop1, hpop2]
    -- Step parameters: digit bound, no-underflow, no-overflow.
    have hd_le_9 : d.toNat ≤ 9 := by
      rw [← hpop2]; exact pop_digit_le_9 g hall
    have hd_le_mt : toUInt128 d ≤ m * 10 := by
      rw [← hpop2]
      exact pop_digit_le_mul_ten_of_pos128 hUL hm_pos h1 (pop_digit_le_9 g hall)
    have hm_new_nat : (m * 10 - toUInt128 d).toNat = m.toNat * 10 - d.toNat :=
      recover_step_mantissa_nat128 hUL h1 hd_le_mt
    have hm10ge : 10 ≤ m.toNat * 10 := by nlinarith
    have hm_new_pos : 1 ≤ (m * 10 - toUInt128 d).toNat := by
      rw [hm_new_nat]; omega
    -- The popped guard represents `10 * f_g - d` with hidden fraction 0.
    have hrep_pop : represents g.pop.1 (10 * f_g - (g.pop.2.toNat : ℚ)) :=
      represents_pop_of_xbit_false hrep hxbit_false
    rw [hpop1, hpop2] at hrep_pop
    -- The fresh guard `g'` also has `xbit_ = false`, so `hdich'` is vacuous.
    have hg'_xbit : g'.xbit_ = false := by
      have : g.pop.1.xbit_ = g.xbit_ := by rfl
      rw [hpop1] at this
      rw [this, hxbit_false]
    have hdich' : g'.xbit_ = true →
        ¬ ((m * 10 - toUInt128 d) < upperLimit ∧ ¬ g'.empty) := by
      intro hxt
      rw [hg'_xbit] at hxt
      exact absurd hxt (by decide)
    -- Apply IH at the popped state.
    obtain ⟨f_exit, hrep_exit, hIH_eq⟩ := IH hm_new_pos hrep_pop hdich'
    refine ⟨f_exit, hrep_exit, ?_⟩
    rw [← hIH_eq]
    -- One-step value algebra: `(m - f_g) * 10^e = ((m*10-d) - (10 f_g - d)) * 10^(e-1)`.
    have h_mnew_q : ((m * 10 - toUInt128 d).toNat : ℚ) = (m.toNat : ℚ) * 10 - (d.toNat : ℚ) := by
      rw [hm_new_nat]
      have hle : d.toNat ≤ m.toNat * 10 := by omega
      rw [Nat.cast_sub hle]
      push_cast; ring
    rw [h_mnew_q]
    have he_pow : (10 : ℚ) ^ e = (10 : ℚ) ^ (e - 1) * 10 := by
      rw [show e = (e - 1) + 1 from by ring, zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0)]
      simp
    rw [he_pow]
    field_simp
    ring
  | case3 m e g fuel hcond =>
    refine ⟨f_g, ?_, ?_⟩
    · have hprop : ¬ (m < upperLimit ∧ ¬ g.empty) := hcond
      rw [recover_noop_exit hprop]; exact hrep
    · have hprop : ¬ (m < upperLimit ∧ ¬ g.empty) := hcond
      rw [recover_noop_exit hprop]

/-- A guard that is empty and `represents f` forces `f = 0`. -/
lemma represents_zero_of_empty {g : Guard} {f : ℚ}
    (hrep : represents g f) (hempty : g.empty = true) : f = 0 := by
  obtain ⟨hd, hx⟩ : g.digits_ = 0 ∧ g.xbit_ = false := by
    have h : (g.digits_ == 0 && !g.xbit_) = true := hempty
    rw [Bool.and_eq_true] at h
    exact ⟨beq_iff_eq.mp h.1, by simpa using h.2⟩
  exact represents_eq_zero_of_digits_zero_xbit_false hd hx hrep

/-- On the empty-exit path the recover residue is consumed exactly: if the output
guard is empty, the diff-sign value `(m - f) * 10^e` equals the output
`m_out * 10^e_out` with no residue. (The guard's `xbit_` is invariant under
`pop`, so an empty output forces `xbit_ = false` throughout the run, and the
tight `represents` chain threads through every pop.) -/
theorem recover_exact_of_empty
    (upperLimit m : UInt128) (hUL : upperLimit.toNat * 10 < 2 ^ 128)
    (e : Int) (g : Guard) (fuel : ℕ)
    (hm_pos : 1 ≤ m.toNat)
    {f : ℚ} (hrep : represents g f)
    (hempty : (Number.operator_add.recover upperLimit m e g fuel).2.2.empty = true) :
    ((m.toNat : ℚ) - f) * (10 : ℚ) ^ e
      = ((Number.operator_add.recover upperLimit m e g fuel).1.toNat : ℚ)
          * (10 : ℚ) ^ ((Number.operator_add.recover upperLimit m e g fuel).2.1) := by
  induction m, e, g, fuel using Number.operator_add.recover.induct (upperLimit := upperLimit)
    generalizing f with
  | case1 m e g =>
    rw [recover_noop_zero] at hempty ⊢
    rw [represents_zero_of_empty hrep hempty]
    ring
  | case2 m e g fuel hcond g' d hpop IH =>
    obtain ⟨h1, h2⟩ := hcond
    rw [recover_step h1 h2] at hempty ⊢
    have hpop1 : g.pop.1 = g' := by rw [hpop]
    have hpop2 : g.pop.2 = d := by rw [hpop]
    rw [hpop1, hpop2] at hempty ⊢
    -- nibble-validity of `g` (from `represents`)
    obtain ⟨_, _, _, _, _, hall⟩ := id hrep
    -- An empty output forces `xbit_ = false` at the input (pop preserves xbit).
    have hxbit_false : g.xbit_ = false := by
      have hx := recover_xbit upperLimit (m * 10 - toUInt128 d) (e - 1) g' fuel
      have hout_xbit : (Number.operator_add.recover upperLimit (m * 10 - toUInt128 d)
          (e - 1) g' fuel).2.2.xbit_ = false := by
        have h : ((Number.operator_add.recover upperLimit (m * 10 - toUInt128 d)
            (e - 1) g' fuel).2.2.digits_ == 0
            && !(Number.operator_add.recover upperLimit (m * 10 - toUInt128 d)
                  (e - 1) g' fuel).2.2.xbit_) = true := hempty
        rw [Bool.and_eq_true] at h
        simpa using h.2
      rw [hx] at hout_xbit
      have hpop_xbit : g'.xbit_ = g.xbit_ := by rw [← hpop1]; rfl
      rw [hpop_xbit] at hout_xbit
      exact hout_xbit
    -- Step parameters: digit bound, no-underflow, no-overflow.
    have hd_le_9 : d.toNat ≤ 9 := by
      rw [← hpop2]; exact pop_digit_le_9 g hall
    have hd_le_mt : toUInt128 d ≤ m * 10 := by
      rw [← hpop2]
      exact pop_digit_le_mul_ten_of_pos128 hUL hm_pos h1 (pop_digit_le_9 g hall)
    have hm_new_nat : (m * 10 - toUInt128 d).toNat = m.toNat * 10 - d.toNat :=
      recover_step_mantissa_nat128 hUL h1 hd_le_mt
    have hm10ge : 10 ≤ m.toNat * 10 := by nlinarith
    have hm_new_pos : 1 ≤ (m * 10 - toUInt128 d).toNat := by
      rw [hm_new_nat]; omega
    -- The popped guard represents `10 * f - d`.
    have hrep_pop : represents g.pop.1 (10 * f - (g.pop.2.toNat : ℚ)) :=
      represents_pop_of_xbit_false hrep hxbit_false
    rw [hpop1, hpop2] at hrep_pop
    -- Apply IH at the popped state.
    have hIH_eq := IH hm_new_pos hrep_pop hempty
    rw [← hIH_eq]
    -- One-step value algebra.
    have h_mnew_q : ((m * 10 - toUInt128 d).toNat : ℚ) = (m.toNat : ℚ) * 10 - (d.toNat : ℚ) := by
      rw [hm_new_nat]
      have hle : d.toNat ≤ m.toNat * 10 := by omega
      rw [Nat.cast_sub hle]
      push_cast; ring
    rw [h_mnew_q]
    have he_pow : (10 : ℚ) ^ e = (10 : ℚ) ^ (e - 1) * 10 := by
      rw [show e = (e - 1) + 1 from by ring, zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0)]
      simp
    rw [he_pow]
    ring
  | case3 m e g fuel hcond =>
    rw [recover_noop_exit hcond] at hempty ⊢
    rw [represents_zero_of_empty hrep hempty]
    ring

/-- Convenient corollary of `recover_value_preserved`, packaged for the
`represents`-based reasoning that downstream diff-sign proofs use. Given
that `g` represents `f_g`, there is a fraction `f_g_after` (an arbitrary
real, not constrained to lie in `[0, 1)`) such that the diff-sign
combined value `(m.toNat - f_g) * 10 ^ e` equals the corresponding
combined value at the recover output. -/
theorem recover_value_preserved_of_represents
    {upperLimit m : UInt128} (hUL : upperLimit.toNat * 10 < 2 ^ 128)
    {e : Int} {g : Guard} {fuel : ℕ}
    (hm_pos : 1 ≤ m.toNat)
    {f_g : ℚ} (hg : represents g f_g) :
    ∃ f_g_after : ℚ,
      ((m.toNat : ℚ) - f_g) * (10 : ℚ) ^ e
        = (((Number.operator_add.recover upperLimit m e g fuel).1.toNat : ℚ) - f_g_after)
          * (10 : ℚ) ^ ((Number.operator_add.recover upperLimit m e g fuel).2.1) := by
  obtain ⟨x, _, _, hf, _, hall⟩ := hg
  obtain ⟨x_after, heq⟩ := recover_value_preserved upperLimit m hUL e g fuel hm_pos hall x
  refine ⟨(decimalValue (Number.operator_add.recover upperLimit m e g fuel).2.2.digits_ : ℚ) / 10 ^ 16
            + x_after, ?_⟩
  rw [hf]
  exact heq

/-! ## Bounded value preservation: unit-interval slack form

Phase 1b strengthening of `recover_value_preserved_of_represents`: in
addition to value preservation, the output residue `f_g_after` satisfies
`0 ≤ f_g_after < 1 + 10^fuel / 10^16`.

The bound is slack by a factor of `10^fuel / 10^16`: this is because each
`Guard.pop` step pulls one digit out of the captured-digit window,
multiplying the hidden-fraction bound by 10. After `k` pops, the hidden
fraction `x_after` is bounded by `10^k / 10^16`. The decimal part
`decimalValue/10^16` is always in `[0, 1)`, so the total residue is
bounded by `1 + 10^k / 10^16 ≤ 1 + 10^fuel / 10^16`.

For the downstream `doRoundDown` calculus, this slack is small enough to
be absorbed: `fuel = 40` in practice, but only steps where the loop
condition holds count toward `k`. The exit characterization ensures `k`
is bounded by the actual number of executed steps, not `fuel`. -/

/-- Helper: under nibble-validity, `decimalValue d < 10^16`. -/
lemma decimalValue_lt_pow_16 {d : UInt64} (hall : allNibblesAtMost9 d) :
    decimalValue d < 10 ^ 16 := by
  unfold decimalValue
  have hbound : ∀ p ∈ Finset.range 16, nibble d p * 10 ^ p ≤ 9 * 10 ^ p := by
    intro p hp
    have : nibble d p ≤ 9 := hall ⟨p, Finset.mem_range.mp hp⟩
    exact Nat.mul_le_mul_right _ this
  calc ∑ p ∈ Finset.range 16, nibble d p * 10 ^ p
      ≤ ∑ p ∈ Finset.range 16, 9 * 10 ^ p := Finset.sum_le_sum hbound
    _ < 10 ^ 16 := by decide

/-- Strengthened value-preservation lemma: in addition to the algebraic
equation, it tracks the hidden-fraction bound through the loop.

The bound on the hidden fraction is parametrized by an initial bound `B`:
each pop widens the bound by a factor of 10, so after `fuel` steps the
output hidden fraction is bounded by `10^fuel * B`. We also assert
`allNibblesAtMost9` for the output guard (preserved by `Guard.pop`).

For the standard invariant `x < 1 / 10^16`, this gives the output bound
`10^fuel / 10^16`. After 16 pops the bound exceeds 1, so the lemma's
unit-interval slack is only useful when `fuel ≤ 16`; in practice the
recover loop's exit condition (mantissa large enough) ensures it stops
well before fuel exhaustion. -/
theorem recover_value_preserved_bounded
    (upperLimit m : UInt128) (hUL : upperLimit.toNat * 10 < 2 ^ 128)
    (e : Int) (g : Guard) (fuel : ℕ)
    (hm_pos : 1 ≤ m.toNat)
    (hall : allNibblesAtMost9 g.digits_)
    (x B : ℚ) (hB_pos : 0 < B)
    (hx_nn : 0 ≤ x) (hx_lt : x < B) :
    ∃ x_after : ℚ,
      0 ≤ x_after ∧ x_after < (10 : ℚ) ^ fuel * B ∧
      allNibblesAtMost9 (Number.operator_add.recover upperLimit m e g fuel).2.2.digits_ ∧
      ((m.toNat : ℚ)
          - ((decimalValue g.digits_ : ℚ) / 10 ^ 16 + x)) * (10 : ℚ) ^ e
        = (((Number.operator_add.recover upperLimit m e g fuel).1.toNat : ℚ)
            - ((decimalValue (Number.operator_add.recover upperLimit m e g fuel).2.2.digits_ : ℚ)
                  / 10 ^ 16 + x_after))
          * (10 : ℚ) ^ ((Number.operator_add.recover upperLimit m e g fuel).2.1) := by
  induction m, e, g, fuel using Number.operator_add.recover.induct (upperLimit := upperLimit)
    generalizing x B with
  | case1 m e g =>
    refine ⟨x, hx_nn, ?_, ?_, ?_⟩
    · -- x < B = 1 * B = 10^0 * B
      rw [pow_zero, one_mul]; exact hx_lt
    · rw [recover_noop_zero]; exact hall
    · rw [recover_noop_zero]
  | case2 m e g fuel hcond g' d hpop IH =>
    obtain ⟨h1, h2⟩ := hcond
    rw [recover_step h1 h2]
    have hpop1 : g.pop.1 = g' := by rw [hpop]
    have hpop2 : g.pop.2 = d := by rw [hpop]
    rw [hpop1, hpop2]
    -- Step parameters
    have hd_le_9 : d.toNat ≤ 9 := by
      rw [← hpop2]; exact pop_digit_le_9 g hall
    have hd_le_mt : toUInt128 d ≤ m * 10 := by
      rw [← hpop2]
      exact pop_digit_le_mul_ten_of_pos128 hUL hm_pos h1 (pop_digit_le_9 g hall)
    have hm_new_nat : (m * 10 - toUInt128 d).toNat = m.toNat * 10 - d.toNat :=
      recover_step_mantissa_nat128 hUL h1 hd_le_mt
    have hm10ge : 10 ≤ m.toNat * 10 := by nlinarith
    have hm_new_pos : 1 ≤ (m * 10 - toUInt128 d).toNat := by
      rw [hm_new_nat]; omega
    have hall_eq : allNibblesAtMost9 g'.digits_ := by
      rw [← hpop1]; exact allNibblesAtMost9_pop g hall
    -- New hidden fraction: 10 * x. New bound: 10 * B.
    have h10x_nn : 0 ≤ 10 * x := by linarith
    have h10B_pos : 0 < 10 * B := by linarith
    have h10x_lt : 10 * x < 10 * B := by linarith
    obtain ⟨x_after, hxa_nn, hxa_lt, hall_after, heq_IH⟩ :=
      IH hm_new_pos hall_eq (10 * x) (10 * B) h10B_pos h10x_nn h10x_lt
    refine ⟨x_after, hxa_nn, ?_, hall_after, ?_⟩
    · -- 10^fuel * (10 * B) = 10^(fuel+1) * B
      have hpow_eq : (10 : ℚ) ^ fuel * (10 * B) = (10 : ℚ) ^ (fuel + 1) * B := by
        rw [pow_succ]; ring
      have hxa_lt' : x_after < (10 : ℚ) ^ (fuel + 1) * B := by
        rw [← hpow_eq]; exact hxa_lt
      -- Goal definitionally: x_after < 10 ^ Nat.succ fuel * B
      change x_after < (10 : ℚ) ^ (fuel + 1) * B
      exact hxa_lt'
    · -- Value equation: combine `decimalValue_pop_rat` and `recover_step_mantissa_nat`
      rw [← heq_IH]
      have h_dv : (decimalValue g'.digits_ : ℚ)
          = 10 * (decimalValue g.digits_ : ℚ) - (d.toNat : ℚ) * 10 ^ 16 := by
        rw [← hpop1, decimalValue_pop_rat g]
        have hd_eq : (g.pop.2.toNat : ℚ) = (nibble g.digits_ 15 : ℚ) := by
          exact_mod_cast pop_digit_eq_nibble_15 g
        rw [hpop2] at hd_eq
        linarith
      have h_mnew_q : ((m * 10 - toUInt128 d).toNat : ℚ) = (m.toNat : ℚ) * 10 - (d.toNat : ℚ) := by
        rw [hm_new_nat]
        have hle : d.toNat ≤ m.toNat * 10 := by omega
        rw [Nat.cast_sub hle]
        push_cast; ring
      rw [h_mnew_q, h_dv]
      have he_pow : (10 : ℚ) ^ e = (10 : ℚ) ^ (e - 1) * 10 := by
        rw [show e = (e - 1) + 1 from by ring, zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0)]
        simp
      rw [he_pow]
      field_simp
      ring
  | case3 m e g fuel hcond =>
    refine ⟨x, hx_nn, ?_, ?_, ?_⟩
    · -- x < B ≤ 10^(any_fuel) * B (since 10^k ≥ 1 for all k)
      -- In `recover.induct`, case3 corresponds to `fuel.succ` not `fuel`,
      -- but the bound holds uniformly.
      have h1 : (1 : ℚ) ≤ (10 : ℚ) ^ (fuel + 1) :=
        one_le_pow₀ (by norm_num : (1 : ℚ) ≤ 10)
      change x < (10 : ℚ) ^ (fuel + 1) * B
      nlinarith
    · have hprop : ¬ (m < upperLimit ∧ ¬ g.empty) := hcond
      rw [recover_noop_exit hprop]; exact hall
    · have hprop : ¬ (m < upperLimit ∧ ¬ g.empty) := hcond
      rw [recover_noop_exit hprop]

set_option maxHeartbeats 800000 in
-- repeated `Number.operator_add.recover upperLimit m e g fuel` terms are expensive to elaborate
/-- Public corollary: `recover_value_in_unit_interval`.

After `recover m e g fuel` (with `1 ≤ m.toNat` and `g` representing
`f_g ∈ [0, 1)`), the diff-sign value `(m - f_g) * 10^e` is preserved by
some output residue `f_g_after`, and `f_g_after` lies in the
"unit-interval slack" range `[0, 1 + 10^fuel / 10^16)`.

The slack `10^fuel / 10^16` reflects the worst-case widening of the
hidden-fraction bound under `fuel` consecutive `Guard.pop` operations.
For the downstream diff-sign bound proof, this slack is absorbed
into the residue tolerance. -/
theorem recover_value_in_unit_interval
    {upperLimit m : UInt128} (hUL : upperLimit.toNat * 10 < 2 ^ 128)
    {e : Int} {g : Guard} {fuel : ℕ}
    (hm_pos : 1 ≤ m.toNat)
    {f_g : ℚ} (hg : represents g f_g) :
    ∃ f_g_after : ℚ,
      0 ≤ f_g_after ∧
      f_g_after < 1 + (10 : ℚ) ^ fuel / 10 ^ 16 ∧
      ((m.toNat : ℚ) - f_g) * (10 : ℚ) ^ e
        = (((Number.operator_add.recover upperLimit m e g fuel).1.toNat : ℚ) - f_g_after)
          * (10 : ℚ) ^ ((Number.operator_add.recover upperLimit m e g fuel).2.1) := by
  obtain ⟨x, hx_nn, hx_lt, hf_eq, _, hall⟩ := hg
  -- Initial bound B = 1/10^16.
  have hB_pos : (0 : ℚ) < 1 / 10 ^ 16 := by positivity
  obtain ⟨x_after, hxa_nn, hxa_lt, hall_after, heq⟩ :=
    recover_value_preserved_bounded upperLimit m hUL e g fuel hm_pos hall x (1 / 10 ^ 16) hB_pos hx_nn hx_lt
  refine ⟨(decimalValue (Number.operator_add.recover upperLimit m e g fuel).2.2.digits_ : ℚ) / 10 ^ 16
          + x_after, ?_, ?_, ?_⟩
  · -- 0 ≤ residue
    have h16_pos : (0 : ℚ) < 10 ^ 16 := by positivity
    have h_dv_nn :
        (0 : ℚ) ≤ (decimalValue (Number.operator_add.recover upperLimit m e g fuel).2.2.digits_ : ℚ) / 10 ^ 16 := by
      apply div_nonneg
      · exact_mod_cast Nat.zero_le _
      · linarith
    linarith
  · -- residue < 1 + 10^fuel / 10^16
    have h16_pos : (0 : ℚ) < 10 ^ 16 := by positivity
    have h_nat : decimalValue (Number.operator_add.recover upperLimit m e g fuel).2.2.digits_ < 10 ^ 16 :=
      decimalValue_lt_pow_16 hall_after
    have h_q :
        (decimalValue (Number.operator_add.recover upperLimit m e g fuel).2.2.digits_ : ℚ) < (10 : ℚ) ^ 16 := by
      exact_mod_cast h_nat
    have h_dv_lt :
        (decimalValue (Number.operator_add.recover upperLimit m e g fuel).2.2.digits_ : ℚ) / 10 ^ 16 < 1 := by
      rw [div_lt_iff₀ h16_pos]; linarith
    have h_xa_eq : (10 : ℚ) ^ fuel * (1 / 10 ^ 16) = (10 : ℚ) ^ fuel / 10 ^ 16 := by ring
    rw [h_xa_eq] at hxa_lt
    linarith
  · rw [hf_eq]
    exact heq

/-! ## Tight unit-interval residue at the exit condition

The `recover_value_in_unit_interval` lemma above proves `f_g_after < 1 + 10^fuel
/ 10^16`. The slack `10^fuel / 10^16` is the worst case under `fuel`
consecutive pops, but as `fuel = 40` in practice this is enormous.

In reality, `recover` runs `k = e - e_out` pops where `k ≤ fuel`, and the
actual slack is `10^k / 10^16`. Moreover, since the input mantissa
satisfies `1 ≤ m.toNat ≤ maxRep.toNat ≈ 9.22 * 10^18` and the loop exits
at `m_out ≥ 10^18` or `m_out * 10 > maxRep ≈ 9.22 * 10^18`, the actual `k`
is bounded by ~18.

The strict `< 1` bound asked for in the prompt is not mathematically
achievable when the input guard has a positive hidden fraction `x_in > 0`:
the residue `f_g_after = 10^k * f_g - S` can be in `[1, 1 + 10^k/10^16)`
in edge cases where `10^k * f_g` is within `10^k/10^16` of an integer
boundary. We therefore prove the strongest provable variant: the residue
lies in `[0, 1 + 10^k_actual / 10^16)`, packaged together with the exit
characterization (either the output satisfies the loop exit condition, or
exactly `fuel` pops ran). -/

set_option maxHeartbeats 800000 in
-- functional induction over `recover` with branch reindexing is heartbeat-heavy
/-- Tight tracking variant of `recover_value_preserved_bounded`: the
hidden-fraction bound widens by exactly `10^k` where `k = e - e_out` is the
actual number of pops, rather than the worst-case `10^fuel`. This is the
key strengthening behind `recover_value_in_unit_interval_at_exit`.

The argument matches the structure of `recover_value_preserved_bounded`
but tracks `k` (the actual pop count) in addition to fuel. When the loop
exits early (case3), `k = 0` reflects that the bound did not widen at
all. When the loop runs (case2), `k` increases by 1 each step, matching
the widening of the bound by factor 10. -/
theorem recover_value_preserved_tight
    (upperLimit m : UInt128) (hUL : upperLimit.toNat * 10 < 2 ^ 128)
    (e : Int) (g : Guard) (fuel : ℕ)
    (hm_pos : 1 ≤ m.toNat)
    (hall : allNibblesAtMost9 g.digits_)
    (x B : ℚ) (hB_pos : 0 < B)
    (hx_nn : 0 ≤ x) (hx_lt : x < B) :
    ∃ x_after : ℚ, ∃ k : ℕ,
      k ≤ fuel ∧
      (Number.operator_add.recover upperLimit m e g fuel).2.1 = e - k ∧
      0 ≤ x_after ∧ x_after < (10 : ℚ) ^ k * B ∧
      (0 < x → 0 < x_after) ∧
      allNibblesAtMost9 (Number.operator_add.recover upperLimit m e g fuel).2.2.digits_ ∧
      ((m.toNat : ℚ)
          - ((decimalValue g.digits_ : ℚ) / 10 ^ 16 + x)) * (10 : ℚ) ^ e
        = (((Number.operator_add.recover upperLimit m e g fuel).1.toNat : ℚ)
            - ((decimalValue (Number.operator_add.recover upperLimit m e g fuel).2.2.digits_ : ℚ)
                  / 10 ^ 16 + x_after))
          * (10 : ℚ) ^ ((Number.operator_add.recover upperLimit m e g fuel).2.1) := by
  induction m, e, g, fuel using Number.operator_add.recover.induct (upperLimit := upperLimit)
    generalizing x B with
  | case1 m e g =>
    -- fuel = 0: recover is a no-op, k = 0.
    refine ⟨x, 0, le_refl _, ?_, hx_nn, ?_, fun h => h, ?_, ?_⟩
    · rw [recover_noop_zero]; push_cast; ring
    · rw [pow_zero, one_mul]; exact hx_lt
    · rw [recover_noop_zero]; exact hall
    · rw [recover_noop_zero]
  | case2 m e g fuel hcond g' d hpop IH =>
    obtain ⟨h1, h2⟩ := hcond
    rw [recover_step h1 h2]
    have hpop1 : g.pop.1 = g' := by rw [hpop]
    have hpop2 : g.pop.2 = d := by rw [hpop]
    rw [hpop1, hpop2]
    -- Step parameters
    have hd_le_9 : d.toNat ≤ 9 := by
      rw [← hpop2]; exact pop_digit_le_9 g hall
    have hd_le_mt : toUInt128 d ≤ m * 10 := by
      rw [← hpop2]
      exact pop_digit_le_mul_ten_of_pos128 hUL hm_pos h1 (pop_digit_le_9 g hall)
    have hm_new_nat : (m * 10 - toUInt128 d).toNat = m.toNat * 10 - d.toNat :=
      recover_step_mantissa_nat128 hUL h1 hd_le_mt
    have hm10ge : 10 ≤ m.toNat * 10 := by nlinarith
    have hm_new_pos : 1 ≤ (m * 10 - toUInt128 d).toNat := by
      rw [hm_new_nat]; omega
    have hall_eq : allNibblesAtMost9 g'.digits_ := by
      rw [← hpop1]; exact allNibblesAtMost9_pop g hall
    have h10x_nn : 0 ≤ 10 * x := by linarith
    have h10B_pos : 0 < 10 * B := by linarith
    have h10x_lt : 10 * x < 10 * B := by linarith
    obtain ⟨x_after, k', hk'_le, hk'_eq, hxa_nn, hxa_lt, hxa_pos, hall_after, heq_IH⟩ :=
      IH hm_new_pos hall_eq (10 * x) (10 * B) h10B_pos h10x_nn h10x_lt
    refine ⟨x_after, k' + 1, by omega, ?_, hxa_nn, ?_,
      fun hx => hxa_pos (by linarith), hall_after, ?_⟩
    · -- e_out = (e - 1) - k' = e - (k' + 1)
      rw [hk'_eq]; push_cast; ring
    · -- 10^k' * (10 * B) = 10^(k' + 1) * B
      have hpow_eq : (10 : ℚ) ^ k' * (10 * B) = (10 : ℚ) ^ (k' + 1) * B := by
        rw [pow_succ]; ring
      rw [← hpow_eq]; exact hxa_lt
    · -- Value equation: same algebra as in `recover_value_preserved_bounded`.
      rw [← heq_IH]
      have h_dv : (decimalValue g'.digits_ : ℚ)
          = 10 * (decimalValue g.digits_ : ℚ) - (d.toNat : ℚ) * 10 ^ 16 := by
        rw [← hpop1, decimalValue_pop_rat g]
        have hd_eq : (g.pop.2.toNat : ℚ) = (nibble g.digits_ 15 : ℚ) := by
          exact_mod_cast pop_digit_eq_nibble_15 g
        rw [hpop2] at hd_eq
        linarith
      have h_mnew_q : ((m * 10 - toUInt128 d).toNat : ℚ) = (m.toNat : ℚ) * 10 - (d.toNat : ℚ) := by
        rw [hm_new_nat]
        have hle : d.toNat ≤ m.toNat * 10 := by omega
        rw [Nat.cast_sub hle]
        push_cast; ring
      rw [h_mnew_q, h_dv]
      have he_pow : (10 : ℚ) ^ e = (10 : ℚ) ^ (e - 1) * 10 := by
        rw [show e = (e - 1) + 1 from by ring, zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0)]
        simp
      rw [he_pow]
      field_simp
      ring
  | case3 m e g fuel hcond =>
    -- Exit at the start: k = 0, bound is x < B = 10^0 * B.
    have hprop : ¬ (m < upperLimit ∧ ¬ g.empty) := hcond
    refine ⟨x, 0, Nat.zero_le _, ?_, hx_nn, ?_, fun h => h, ?_, ?_⟩
    · rw [recover_noop_exit hprop]; push_cast; ring
    · rw [pow_zero, one_mul]; exact hx_lt
    · rw [recover_noop_exit hprop]; exact hall
    · rw [recover_noop_exit hprop]

/-- After `k = e - e_out` pops, the surviving guard's decimal value is
divisible by `10^k`: each pop shifts the packed digits up one decimal place,
leaving zeros in the bottom positions. Threaded with a starting offset `J`
(`10^J ∣ dv` on entry) so the induction closes; instantiate with `J = 0`.

This is the digit-exactness fact behind the strict residue bound: combined
with `decimalValue < 10^16` it caps the decimal part at `1 - 10^k/10^16`,
exactly absorbing the hidden fraction's `10^k` growth. -/
theorem recover_decimalValue_dvd
    (upperLimit m : UInt128) (hUL : upperLimit.toNat * 10 < 2 ^ 128)
    (e : Int) (g : Guard) (fuel : ℕ)
    (hm_pos : 1 ≤ m.toNat)
    (hall : allNibblesAtMost9 g.digits_)
    (J : ℕ) (hdvd : 10 ^ J ∣ decimalValue g.digits_) :
    ∃ k : ℕ,
      (Number.operator_add.recover upperLimit m e g fuel).2.1 = e - k ∧
      10 ^ (J + k) ∣ decimalValue (Number.operator_add.recover upperLimit m e g fuel).2.2.digits_ := by
  induction m, e, g, fuel using Number.operator_add.recover.induct (upperLimit := upperLimit)
    generalizing J with
  | case1 m e g =>
    refine ⟨0, ?_, ?_⟩
    · rw [recover_noop_zero]; push_cast; ring
    · rw [recover_noop_zero, Nat.add_zero]; exact hdvd
  | case2 m e g fuel hcond g' d hpop IH =>
    obtain ⟨h1, h2⟩ := hcond
    rw [recover_step h1 h2]
    have hpop1 : g.pop.1 = g' := by rw [hpop]
    have hpop2 : g.pop.2 = d := by rw [hpop]
    rw [hpop1, hpop2]
    have hd_le_mt : toUInt128 d ≤ m * 10 := by
      rw [← hpop2]
      exact pop_digit_le_mul_ten_of_pos128 hUL hm_pos h1 (pop_digit_le_9 g hall)
    have hm_new_nat : (m * 10 - toUInt128 d).toNat = m.toNat * 10 - d.toNat :=
      recover_step_mantissa_nat128 hUL h1 hd_le_mt
    have hd_le_9 : d.toNat ≤ 9 := by
      rw [← hpop2]; exact pop_digit_le_9 g hall
    have hm10ge : 10 ≤ m.toNat * 10 := by nlinarith
    have hm_new_pos : 1 ≤ (m * 10 - toUInt128 d).toNat := by
      rw [hm_new_nat]; omega
    have hall_eq : allNibblesAtMost9 g'.digits_ := by
      rw [← hpop1]; exact allNibblesAtMost9_pop g hall
    -- The popped guard's decimal value gains one more factor of 10.
    have hdvd' : 10 ^ (J + 1) ∣ decimalValue g'.digits_ := by
      have heq := decimalValue_pop_eq g
      rw [hpop1] at heq
      by_cases hJ : J ≤ 15
      · have h10dv : 10 ^ (J + 1) ∣ 10 * decimalValue g.digits_ := by
          obtain ⟨c, hc⟩ := hdvd
          exact ⟨c, by rw [hc, pow_succ]; ring⟩
        have hd16 : 10 ^ (J + 1) ∣ nibble g.digits_ 15 * 10 ^ 16 :=
          Dvd.dvd.mul_left (pow_dvd_pow 10 (by omega)) _
        have hsub : decimalValue g'.digits_
            = 10 * decimalValue g.digits_ - nibble g.digits_ 15 * 10 ^ 16 := by omega
        rw [hsub]
        exact Nat.dvd_sub h10dv hd16
      · push_neg at hJ
        have hlt : decimalValue g.digits_ < 10 ^ 16 := decimalValue_lt_pow_16 hall
        have hdv0 : decimalValue g.digits_ = 0 := by
          rcases Nat.eq_zero_or_pos (decimalValue g.digits_) with h | h
          · exact h
          · have hge : 10 ^ J ≤ decimalValue g.digits_ := Nat.le_of_dvd h hdvd
            have h16J : (10 : ℕ) ^ 16 ≤ 10 ^ J := Nat.pow_le_pow_right (by norm_num) (by omega)
            omega
        have hdv'0 : decimalValue g'.digits_ = 0 := by omega
        rw [hdv'0]
        exact dvd_zero _
    obtain ⟨k', hk'_eq, hk'_dvd⟩ := IH hm_new_pos hall_eq (J + 1) hdvd'
    refine ⟨k' + 1, ?_, ?_⟩
    · rw [hk'_eq]; push_cast; ring
    · rw [show J + (k' + 1) = J + 1 + k' from by ring]; exact hk'_dvd
  | case3 m e g fuel hcond =>
    have hprop : ¬ (m < upperLimit ∧ ¬ g.empty) := hcond
    refine ⟨0, ?_, ?_⟩
    · rw [recover_noop_exit hprop]; push_cast; ring
    · rw [recover_noop_exit hprop, Nat.add_zero]; exact hdvd

set_option maxHeartbeats 1600000 in
-- repeated `Number.operator_add.recover upperLimit m e g fuel` terms are expensive to elaborate
/-- Public corollary: `recover_value_in_unit_interval_at_exit`.

Strengthens `recover_value_in_unit_interval` along two axes:

1. The residue bound is `< 1 + 10^k / 10^16` where `k = e - e_out` is the
   *actual* number of pops executed (rather than the worst-case `fuel`).
2. We expose the exit characterization: either `result` satisfies the
   natural loop-exit condition (so further fuel would be a no-op), or
   exactly `fuel` pops ran.

The added `hm_le : m.toNat ≤ maxRep.toNat` hypothesis bounds the input
mantissa, allowing downstream callers to combine this lemma with the
mantissa-bound preservation analysis to derive `k ≤ 18` and hence a
concrete `1 + 10^18 / 10^16 = 101` upper bound when needed.

The unconditional strict `f_g_after < 1` form is *not* attainable for large
`k`: when the input guard's hidden fraction `x_in` is positive (i.e.,
`xbit_ = true`), its bound grows by `10^k`. But for `k ≤ 16` pops the
digit-exactness fact `recover_decimalValue_dvd` caps the decimal part at
`1 - 10^k/10^16`, exactly absorbing that growth — packaged here as the
conditional conjunct `k ≤ 16 → f_g_after < 1`. -/
theorem recover_value_in_unit_interval_at_exit
    (upperLimit m : UInt128) (hUL : upperLimit.toNat * 10 < 2 ^ 128)
    (e : Int) (g : Guard) (f_g : ℚ) (fuel : ℕ)
    (_hf_g_nn : 0 ≤ f_g) (_hf_g_lt : f_g < 1)
    (hrep : represents g f_g)
    (hm_pos : 1 ≤ m.toNat) :
    let result := Number.operator_add.recover upperLimit m e g fuel
    ∃ f_g_after : ℚ, ∃ k : ℕ,
      k ≤ fuel ∧
      result.2.1 = e - k ∧
      0 ≤ f_g_after ∧
      f_g_after < 1 + (10 : ℚ) ^ k / 10 ^ 16 ∧
      (k ≤ 16 → f_g_after < 1) ∧
      (g.xbit_ = true → 0 < f_g_after) ∧
      ((m.toNat : ℚ) - f_g) * (10 : ℚ) ^ e
        = ((result.1.toNat : ℚ) - f_g_after) * (10 : ℚ) ^ result.2.1
      ∧ ((¬ (result.1 < upperLimit ∧ ¬ result.2.2.empty))
         ∨ k = fuel) := by
  obtain ⟨x, hx_nn, hx_lt, hf_eq, hxbit_iff, hall⟩ := hrep
  have hB_pos : (0 : ℚ) < 1 / 10 ^ 16 := by positivity
  obtain ⟨x_after, k, hk_le, hk_eq, hxa_nn, hxa_lt, hxa_pos, hall_after, heq⟩ :=
    recover_value_preserved_tight upperLimit m hUL e g fuel hm_pos hall x (1 / 10 ^ 16) hB_pos hx_nn hx_lt
  obtain ⟨k₂, hk₂_eq, hk₂_dvd⟩ :=
    recover_decimalValue_dvd upperLimit m hUL e g fuel hm_pos hall 0
      (by rw [pow_zero]; exact one_dvd _)
  have hkk : k₂ = k := by
    rw [hk_eq] at hk₂_eq
    omega
  rw [Nat.zero_add, hkk] at hk₂_dvd
  refine ⟨(decimalValue (Number.operator_add.recover upperLimit m e g fuel).2.2.digits_ : ℚ) / 10 ^ 16
            + x_after, k, hk_le, hk_eq, ?_, ?_, ?_, ?_, ?_, ?_⟩
  · -- 0 ≤ residue
    have h16_pos : (0 : ℚ) < 10 ^ 16 := by positivity
    have h_dv_nn :
        (0 : ℚ) ≤ (decimalValue (Number.operator_add.recover upperLimit m e g fuel).2.2.digits_ : ℚ) / 10 ^ 16 := by
      apply div_nonneg
      · exact_mod_cast Nat.zero_le _
      · linarith
    linarith
  · -- residue < 1 + 10^k / 10^16.
    have h16_pos : (0 : ℚ) < 10 ^ 16 := by positivity
    have h_nat : decimalValue (Number.operator_add.recover upperLimit m e g fuel).2.2.digits_ < 10 ^ 16 :=
      decimalValue_lt_pow_16 hall_after
    have h_q :
        (decimalValue (Number.operator_add.recover upperLimit m e g fuel).2.2.digits_ : ℚ) < (10 : ℚ) ^ 16 := by
      exact_mod_cast h_nat
    have h_dv_lt :
        (decimalValue (Number.operator_add.recover upperLimit m e g fuel).2.2.digits_ : ℚ) / 10 ^ 16 < 1 := by
      rw [div_lt_iff₀ h16_pos]; linarith
    have h_xa_eq : (10 : ℚ) ^ k * (1 / 10 ^ 16) = (10 : ℚ) ^ k / 10 ^ 16 := by ring
    rw [h_xa_eq] at hxa_lt
    linarith
  · -- k ≤ 16 → residue < 1: the bottom k decimal digits are zeros, capping the
    -- decimal part at 1 - 10^k/10^16, which absorbs the hidden fraction's growth.
    intro hk16
    obtain ⟨c, hc⟩ := hk₂_dvd
    have hlt_nat : decimalValue (Number.operator_add.recover upperLimit m e g fuel).2.2.digits_ < 10 ^ 16 :=
      decimalValue_lt_pow_16 hall_after
    have hsplit : (10 : ℕ) ^ 16 = 10 ^ k * 10 ^ (16 - k) := by
      rw [← pow_add]; congr 1; omega
    have hc_lt : c < 10 ^ (16 - k) := by
      by_contra hge
      push_neg at hge
      have h2 := Nat.mul_le_mul_left (10 ^ k) hge
      omega
    have hdv_add : decimalValue (Number.operator_add.recover upperLimit m e g fuel).2.2.digits_
        + 10 ^ k ≤ 10 ^ 16 := by
      have h3 := Nat.mul_le_mul_left (10 ^ k) (by omega : c + 1 ≤ 10 ^ (16 - k))
      rw [Nat.mul_add, mul_one] at h3
      omega
    have h16_pos : (0 : ℚ) < 10 ^ 16 := by positivity
    have hdv_q :
        (decimalValue (Number.operator_add.recover upperLimit m e g fuel).2.2.digits_ : ℚ)
          + (10 : ℚ) ^ k ≤ 10 ^ 16 := by
      have hcast : ((decimalValue (Number.operator_add.recover upperLimit m e g fuel).2.2.digits_
          + 10 ^ k : ℕ) : ℚ) ≤ ((10 ^ 16 : ℕ) : ℚ) := by
        exact_mod_cast hdv_add
      push_cast at hcast
      linarith
    have h_xa_eq : (10 : ℚ) ^ k * (1 / 10 ^ 16) = (10 : ℚ) ^ k / 10 ^ 16 := by ring
    rw [h_xa_eq] at hxa_lt
    have hsum_le :
        (decimalValue (Number.operator_add.recover upperLimit m e g fuel).2.2.digits_ : ℚ) / 10 ^ 16
          + (10 : ℚ) ^ k / 10 ^ 16 ≤ 1 := by
      rw [← add_div, div_le_one h16_pos]
      exact hdv_q
    linarith
  · -- xbit → positive residue (the hidden fraction threads positively).
    intro hxb
    have hx_pos : 0 < x := hxbit_iff.mp hxb
    have h16_pos : (0 : ℚ) < 10 ^ 16 := by positivity
    have h_dv_nn :
        (0 : ℚ) ≤ (decimalValue (Number.operator_add.recover upperLimit m e g fuel).2.2.digits_ : ℚ) / 10 ^ 16 := by
      apply div_nonneg
      · exact_mod_cast Nat.zero_le _
      · linarith
    linarith [hxa_pos hx_pos]
  · rw [hf_eq]
    exact heq
  · -- Exit characterization
    rcases recover_output_exits_or_fuel_exhausted upperLimit m e g fuel with hexit | hfeq
    · left; exact hexit
    · right
      -- hfeq : result.2.1 = e - fuel
      -- hk_eq : result.2.1 = e - k
      have : (e - fuel : ℤ) = e - k := by rw [← hfeq, ← hk_eq]
      omega

/-! ## Recover from a zero mantissa

In the degenerate case of the diff-sign branch, the input mantissa to
`recover` is `0` (the two aligned mantissas cancel exactly). The previous
value-preservation lemmas all require `1 ≤ m.toNat`, so we need a separate
analysis for this case.

When `m = 0`:
* Loop entry condition `0 < largeRange.min ∧ 0 * 10 ≤ maxRep` always holds.
* If the popped digit `d = 0`, the new mantissa `(0 * 10 - 0).toNat = 0` and
  the loop continues.
* If the popped digit `d ≥ 1`, the UInt64 subtraction underflows giving
  `(0 * 10 - d).toNat = 2^64 - d ≥ 2^64 - 9 > 10^18 = largeRange.min`, so the
  loop exits at the next iteration.

The key inductive invariant is

  ((decimalValue g.digits_ : ℚ) / 10^16 + x) * 10^k
    ≤ m_out + 1 + 10^k * B

where `B` is the running bound on the hidden fraction `x`. The bound holds
because:
* In the all-zero case, m_out = 0, but the left side is bounded by
  `10^k * (slightly more than B)` since the captured digits get exhausted.
* In the underflow case, m_out ≥ 2^64 - 9 dominates the left side easily. -/

end XRPL.Model.Protocol
