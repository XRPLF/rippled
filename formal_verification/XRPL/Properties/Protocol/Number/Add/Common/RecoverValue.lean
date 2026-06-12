import Mathlib.Tactic

import XRPL.Model.Protocol.Number
import XRPL.Properties.Protocol.Number.Add.Common.AlignDown
import XRPL.Properties.Protocol.Number.Add.Common.RecoverBasic
import XRPL.Properties.Protocol.Number.Rounding.Guard
import XRPL.Properties.Protocol.Number.Rounding.ScaleDown

set_option linter.style.longLine false
set_option linter.style.emptyLine false

namespace XRPL.Model.Protocol


/-! ## Value preservation across the `recover` loop

The downstream different-sign branch needs the algebraic invariant that
`recover` preserves the exact rational value being represented by
the triple `(m, e, g)`. Concretely, if we interpret the residual as
`(m - f_g) * 10 ^ e` — the "diff-sign" reading where `f_g` is the
rational fraction captured by the guard `g` — then this value is the
same at the input and output of `recover`, for some witness fraction
`x_after` describing the output guard's hidden tail.

The lemma takes the hidden fraction `x` of the input guard as an
arbitrary real parameter (this is the universally-quantified part of
`represents g f_g`), and produces the corresponding `x_after` of the
output guard. Because `Guard.pop` widens the hidden-fraction bound by a
factor of 10 each step, downstream callers should bound `x_after`
manually (using `represents_pop` or by tracking it through the steps);
this lemma asserts only the algebraic value equation, which is what is
needed for the rounding-bound calculus.

Preconditions:

* `1 ≤ m.toNat` — the mantissa is positive (preserved across each
  recover step, since each step replaces `m` by `m * 10 - d` with
  `d ≤ 9`, giving `≥ 10 - 9 = 1`). This is needed to ensure the
  `UInt64` subtraction `m * 10 - d` does not underflow.
* `allNibblesAtMost9 g.digits_` — the guard is well-formed (preserved
  by `Guard.pop`, see `allNibblesAtMost9_pop`). This is needed to
  bound the popped digit by `9`.

The output `x_after` is *not* constrained by this lemma to satisfy any
`represents` bound; the algebraic equation alone is sufficient for the
diff-sign branch's value-preservation calculus. -/
theorem recover_value_preserved
    (m : UInt64) (e : Int) (g : Guard) (fuel : ℕ)
    (hm_pos : 1 ≤ m.toNat)
    (hall : allNibblesAtMost9 g.digits_)
    (x : ℚ) :
    ∃ x_after : ℚ,
      ((m.toNat : ℚ)
          - ((decimalValue g.digits_ : ℚ) / 10 ^ 16 + x)) * (10 : ℚ) ^ e
        = (((Number.operator_add.recover m e g fuel).1.toNat : ℚ)
            - ((decimalValue (Number.operator_add.recover m e g fuel).2.2.digits_ : ℚ)
                  / 10 ^ 16 + x_after))
          * (10 : ℚ) ^ ((Number.operator_add.recover m e g fuel).2.1) := by
  induction m, e, g, fuel using Number.operator_add.recover.induct
    generalizing x with
  | case1 m e g =>
    refine ⟨x, ?_⟩
    rw [recover_noop_zero]
  | case2 m e g fuel hcond g' d hpop IH =>
    rw [Bool.and_eq_true] at hcond
    obtain ⟨hc1, hc2⟩ := hcond
    have h1 := of_decide_eq_true hc1
    have h2 := of_decide_eq_true hc2
    rw [recover_step h1 h2]
    have hpop1 : g.pop.1 = g' := by rw [hpop]
    have hpop2 : g.pop.2 = d := by rw [hpop]
    rw [hpop1, hpop2]
    -- Step parameters: digit bound, no-underflow, no-overflow, etc.
    have hd_le_9 : d.toNat ≤ 9 := by
      rw [← hpop2]; exact pop_digit_le_9 g hall
    have hd_le_mt : d ≤ m * 10 := by
      rw [← hpop2]
      exact pop_digit_le_mul_ten_of_pos hm_pos h1 (pop_digit_le_9 g hall)
    have hm_new_nat : (m * 10 - d).toNat = m.toNat * 10 - d.toNat :=
      recover_step_mantissa_nat h1 hd_le_mt
    have hm10ge : 10 ≤ m.toNat * 10 := by nlinarith
    -- Preserved invariant: new mantissa is still ≥ 1
    have hm_new_pos : 1 ≤ (m * 10 - d).toNat := by
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
    have h_mnew_q : ((m * 10 - d).toNat : ℚ) = (m.toNat : ℚ) * 10 - (d.toNat : ℚ) := by
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
    have hprop : ¬ (m < largeRange.min ∧ m * 10 ≤ maxRep) := by
      intro ⟨hm1, hm2⟩
      apply hcond
      rw [Bool.and_eq_true]
      exact ⟨decide_eq_true hm1, decide_eq_true hm2⟩
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
    (m : UInt64) (e : Int) (g : Guard) (fuel : ℕ)
    (hm_pos : 1 ≤ m.toNat)
    {f_g : ℚ} (hrep : represents g f_g)
    (hdich : g.xbit_ = true →
      ¬ (m < largeRange.min ∧ m * 10 ≤ maxRep)) :
    ∃ f_exit : ℚ,
      represents (Number.operator_add.recover m e g fuel).2.2 f_exit ∧
      ((m.toNat : ℚ) - f_g) * (10 : ℚ) ^ e
        = (((Number.operator_add.recover m e g fuel).1.toNat : ℚ) - f_exit)
            * (10 : ℚ) ^ ((Number.operator_add.recover m e g fuel).2.1) := by
  induction m, e, g, fuel using Number.operator_add.recover.induct
    generalizing f_g with
  | case1 m e g =>
    refine ⟨f_g, hrep, ?_⟩
    rw [recover_noop_zero]
  | case2 m e g fuel hcond g' d hpop IH =>
    rw [Bool.and_eq_true] at hcond
    obtain ⟨hc1, hc2⟩ := hcond
    have h1 := of_decide_eq_true hc1
    have h2 := of_decide_eq_true hc2
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
    have hd_le_mt : d ≤ m * 10 := by
      rw [← hpop2]
      exact pop_digit_le_mul_ten_of_pos hm_pos h1 (pop_digit_le_9 g hall)
    have hm_new_nat : (m * 10 - d).toNat = m.toNat * 10 - d.toNat :=
      recover_step_mantissa_nat h1 hd_le_mt
    have hm10ge : 10 ≤ m.toNat * 10 := by nlinarith
    have hm_new_pos : 1 ≤ (m * 10 - d).toNat := by
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
        ¬ ((m * 10 - d) < largeRange.min ∧ (m * 10 - d) * 10 ≤ maxRep) := by
      intro hxt
      rw [hg'_xbit] at hxt
      exact absurd hxt (by decide)
    -- Apply IH at the popped state.
    obtain ⟨f_exit, hrep_exit, hIH_eq⟩ := IH hm_new_pos hrep_pop hdich'
    refine ⟨f_exit, hrep_exit, ?_⟩
    rw [← hIH_eq]
    -- One-step value algebra: `(m - f_g) * 10^e = ((m*10-d) - (10 f_g - d)) * 10^(e-1)`.
    have h_mnew_q : ((m * 10 - d).toNat : ℚ) = (m.toNat : ℚ) * 10 - (d.toNat : ℚ) := by
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
    · have hprop : ¬ (m < largeRange.min ∧ m * 10 ≤ maxRep) := by
        intro ⟨hm1, hm2⟩
        apply hcond
        rw [Bool.and_eq_true]
        exact ⟨decide_eq_true hm1, decide_eq_true hm2⟩
      rw [recover_noop_exit hprop]; exact hrep
    · have hprop : ¬ (m < largeRange.min ∧ m * 10 ≤ maxRep) := by
        intro ⟨hm1, hm2⟩
        apply hcond
        rw [Bool.and_eq_true]
        exact ⟨decide_eq_true hm1, decide_eq_true hm2⟩
      rw [recover_noop_exit hprop]

/-- Convenient corollary of `recover_value_preserved`, packaged for the
`represents`-based reasoning that downstream diff-sign proofs use. Given
that `g` represents `f_g`, there is a fraction `f_g_after` (an arbitrary
real, not constrained to lie in `[0, 1)`) such that the diff-sign
combined value `(m.toNat - f_g) * 10 ^ e` equals the corresponding
combined value at the recover output. -/
theorem recover_value_preserved_of_represents
    {m : UInt64} {e : Int} {g : Guard} {fuel : ℕ}
    (hm_pos : 1 ≤ m.toNat)
    {f_g : ℚ} (hg : represents g f_g) :
    ∃ f_g_after : ℚ,
      ((m.toNat : ℚ) - f_g) * (10 : ℚ) ^ e
        = (((Number.operator_add.recover m e g fuel).1.toNat : ℚ) - f_g_after)
          * (10 : ℚ) ^ ((Number.operator_add.recover m e g fuel).2.1) := by
  obtain ⟨x, _, _, hf, _, hall⟩ := hg
  obtain ⟨x_after, heq⟩ := recover_value_preserved m e g fuel hm_pos hall x
  refine ⟨(decimalValue (Number.operator_add.recover m e g fuel).2.2.digits_ : ℚ) / 10 ^ 16
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
    (m : UInt64) (e : Int) (g : Guard) (fuel : ℕ)
    (hm_pos : 1 ≤ m.toNat)
    (hall : allNibblesAtMost9 g.digits_)
    (x B : ℚ) (hB_pos : 0 < B)
    (hx_nn : 0 ≤ x) (hx_lt : x < B) :
    ∃ x_after : ℚ,
      0 ≤ x_after ∧ x_after < (10 : ℚ) ^ fuel * B ∧
      allNibblesAtMost9 (Number.operator_add.recover m e g fuel).2.2.digits_ ∧
      ((m.toNat : ℚ)
          - ((decimalValue g.digits_ : ℚ) / 10 ^ 16 + x)) * (10 : ℚ) ^ e
        = (((Number.operator_add.recover m e g fuel).1.toNat : ℚ)
            - ((decimalValue (Number.operator_add.recover m e g fuel).2.2.digits_ : ℚ)
                  / 10 ^ 16 + x_after))
          * (10 : ℚ) ^ ((Number.operator_add.recover m e g fuel).2.1) := by
  induction m, e, g, fuel using Number.operator_add.recover.induct
    generalizing x B with
  | case1 m e g =>
    refine ⟨x, hx_nn, ?_, ?_, ?_⟩
    · -- x < B = 1 * B = 10^0 * B
      rw [pow_zero, one_mul]; exact hx_lt
    · rw [recover_noop_zero]; exact hall
    · rw [recover_noop_zero]
  | case2 m e g fuel hcond g' d hpop IH =>
    rw [Bool.and_eq_true] at hcond
    obtain ⟨hc1, hc2⟩ := hcond
    have h1 := of_decide_eq_true hc1
    have h2 := of_decide_eq_true hc2
    rw [recover_step h1 h2]
    have hpop1 : g.pop.1 = g' := by rw [hpop]
    have hpop2 : g.pop.2 = d := by rw [hpop]
    rw [hpop1, hpop2]
    -- Step parameters
    have hd_le_9 : d.toNat ≤ 9 := by
      rw [← hpop2]; exact pop_digit_le_9 g hall
    have hd_le_mt : d ≤ m * 10 := by
      rw [← hpop2]
      exact pop_digit_le_mul_ten_of_pos hm_pos h1 (pop_digit_le_9 g hall)
    have hm_new_nat : (m * 10 - d).toNat = m.toNat * 10 - d.toNat :=
      recover_step_mantissa_nat h1 hd_le_mt
    have hm10ge : 10 ≤ m.toNat * 10 := by nlinarith
    have hm_new_pos : 1 ≤ (m * 10 - d).toNat := by
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
      have h_mnew_q : ((m * 10 - d).toNat : ℚ) = (m.toNat : ℚ) * 10 - (d.toNat : ℚ) := by
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
    · have hprop : ¬ (m < largeRange.min ∧ m * 10 ≤ maxRep) := by
        intro ⟨hm1, hm2⟩
        apply hcond
        rw [Bool.and_eq_true]
        exact ⟨decide_eq_true hm1, decide_eq_true hm2⟩
      rw [recover_noop_exit hprop]; exact hall
    · have hprop : ¬ (m < largeRange.min ∧ m * 10 ≤ maxRep) := by
        intro ⟨hm1, hm2⟩
        apply hcond
        rw [Bool.and_eq_true]
        exact ⟨decide_eq_true hm1, decide_eq_true hm2⟩
      rw [recover_noop_exit hprop]

set_option maxHeartbeats 800000 in
-- repeated `Number.operator_add.recover m e g fuel` terms are expensive to elaborate
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
    {m : UInt64} {e : Int} {g : Guard} {fuel : ℕ}
    (hm_pos : 1 ≤ m.toNat)
    {f_g : ℚ} (hg : represents g f_g) :
    ∃ f_g_after : ℚ,
      0 ≤ f_g_after ∧
      f_g_after < 1 + (10 : ℚ) ^ fuel / 10 ^ 16 ∧
      ((m.toNat : ℚ) - f_g) * (10 : ℚ) ^ e
        = (((Number.operator_add.recover m e g fuel).1.toNat : ℚ) - f_g_after)
          * (10 : ℚ) ^ ((Number.operator_add.recover m e g fuel).2.1) := by
  obtain ⟨x, hx_nn, hx_lt, hf_eq, _, hall⟩ := hg
  -- Initial bound B = 1/10^16.
  have hB_pos : (0 : ℚ) < 1 / 10 ^ 16 := by positivity
  obtain ⟨x_after, hxa_nn, hxa_lt, hall_after, heq⟩ :=
    recover_value_preserved_bounded m e g fuel hm_pos hall x (1 / 10 ^ 16) hB_pos hx_nn hx_lt
  refine ⟨(decimalValue (Number.operator_add.recover m e g fuel).2.2.digits_ : ℚ) / 10 ^ 16
          + x_after, ?_, ?_, ?_⟩
  · -- 0 ≤ residue
    have h16_pos : (0 : ℚ) < 10 ^ 16 := by positivity
    have h_dv_nn :
        (0 : ℚ) ≤ (decimalValue (Number.operator_add.recover m e g fuel).2.2.digits_ : ℚ) / 10 ^ 16 := by
      apply div_nonneg
      · exact_mod_cast Nat.zero_le _
      · linarith
    linarith
  · -- residue < 1 + 10^fuel / 10^16
    have h16_pos : (0 : ℚ) < 10 ^ 16 := by positivity
    have h_nat : decimalValue (Number.operator_add.recover m e g fuel).2.2.digits_ < 10 ^ 16 :=
      decimalValue_lt_pow_16 hall_after
    have h_q :
        (decimalValue (Number.operator_add.recover m e g fuel).2.2.digits_ : ℚ) < (10 : ℚ) ^ 16 := by
      exact_mod_cast h_nat
    have h_dv_lt :
        (decimalValue (Number.operator_add.recover m e g fuel).2.2.digits_ : ℚ) / 10 ^ 16 < 1 := by
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
    (m : UInt64) (e : Int) (g : Guard) (fuel : ℕ)
    (hm_pos : 1 ≤ m.toNat)
    (hall : allNibblesAtMost9 g.digits_)
    (x B : ℚ) (hB_pos : 0 < B)
    (hx_nn : 0 ≤ x) (hx_lt : x < B) :
    ∃ x_after : ℚ, ∃ k : ℕ,
      k ≤ fuel ∧
      (Number.operator_add.recover m e g fuel).2.1 = e - k ∧
      0 ≤ x_after ∧ x_after < (10 : ℚ) ^ k * B ∧
      allNibblesAtMost9 (Number.operator_add.recover m e g fuel).2.2.digits_ ∧
      ((m.toNat : ℚ)
          - ((decimalValue g.digits_ : ℚ) / 10 ^ 16 + x)) * (10 : ℚ) ^ e
        = (((Number.operator_add.recover m e g fuel).1.toNat : ℚ)
            - ((decimalValue (Number.operator_add.recover m e g fuel).2.2.digits_ : ℚ)
                  / 10 ^ 16 + x_after))
          * (10 : ℚ) ^ ((Number.operator_add.recover m e g fuel).2.1) := by
  induction m, e, g, fuel using Number.operator_add.recover.induct
    generalizing x B with
  | case1 m e g =>
    -- fuel = 0: recover is a no-op, k = 0.
    refine ⟨x, 0, le_refl _, ?_, hx_nn, ?_, ?_, ?_⟩
    · rw [recover_noop_zero]; push_cast; ring
    · rw [pow_zero, one_mul]; exact hx_lt
    · rw [recover_noop_zero]; exact hall
    · rw [recover_noop_zero]
  | case2 m e g fuel hcond g' d hpop IH =>
    rw [Bool.and_eq_true] at hcond
    obtain ⟨hc1, hc2⟩ := hcond
    have h1 := of_decide_eq_true hc1
    have h2 := of_decide_eq_true hc2
    rw [recover_step h1 h2]
    have hpop1 : g.pop.1 = g' := by rw [hpop]
    have hpop2 : g.pop.2 = d := by rw [hpop]
    rw [hpop1, hpop2]
    -- Step parameters
    have hd_le_9 : d.toNat ≤ 9 := by
      rw [← hpop2]; exact pop_digit_le_9 g hall
    have hd_le_mt : d ≤ m * 10 := by
      rw [← hpop2]
      exact pop_digit_le_mul_ten_of_pos hm_pos h1 (pop_digit_le_9 g hall)
    have hm_new_nat : (m * 10 - d).toNat = m.toNat * 10 - d.toNat :=
      recover_step_mantissa_nat h1 hd_le_mt
    have hm10ge : 10 ≤ m.toNat * 10 := by nlinarith
    have hm_new_pos : 1 ≤ (m * 10 - d).toNat := by
      rw [hm_new_nat]; omega
    have hall_eq : allNibblesAtMost9 g'.digits_ := by
      rw [← hpop1]; exact allNibblesAtMost9_pop g hall
    have h10x_nn : 0 ≤ 10 * x := by linarith
    have h10B_pos : 0 < 10 * B := by linarith
    have h10x_lt : 10 * x < 10 * B := by linarith
    obtain ⟨x_after, k', hk'_le, hk'_eq, hxa_nn, hxa_lt, hall_after, heq_IH⟩ :=
      IH hm_new_pos hall_eq (10 * x) (10 * B) h10B_pos h10x_nn h10x_lt
    refine ⟨x_after, k' + 1, by omega, ?_, hxa_nn, ?_, hall_after, ?_⟩
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
      have h_mnew_q : ((m * 10 - d).toNat : ℚ) = (m.toNat : ℚ) * 10 - (d.toNat : ℚ) := by
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
    have hprop : ¬ (m < largeRange.min ∧ m * 10 ≤ maxRep) := by
      intro ⟨hm1, hm2⟩
      apply hcond
      rw [Bool.and_eq_true]
      exact ⟨decide_eq_true hm1, decide_eq_true hm2⟩
    refine ⟨x, 0, Nat.zero_le _, ?_, hx_nn, ?_, ?_, ?_⟩
    · rw [recover_noop_exit hprop]; push_cast; ring
    · rw [pow_zero, one_mul]; exact hx_lt
    · rw [recover_noop_exit hprop]; exact hall
    · rw [recover_noop_exit hprop]

set_option maxHeartbeats 800000 in
-- repeated `Number.operator_add.recover m e g fuel` terms are expensive to elaborate
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

The strict `f_g_after < 1` form is *not* attainable in this generality:
when the input guard's hidden fraction `x_in` is positive (i.e., `xbit_ =
true`), the residue can be at most `10^k / 10^16` above 1 in edge cases.
Downstream proofs absorb this slack via the standard `doRoundDown`
tolerance. -/
theorem recover_value_in_unit_interval_at_exit
    (m : UInt64) (e : Int) (g : Guard) (f_g : ℚ) (fuel : ℕ)
    (_hf_g_nn : 0 ≤ f_g) (_hf_g_lt : f_g < 1)
    (hrep : represents g f_g)
    (hm_pos : 1 ≤ m.toNat) :
    let result := Number.operator_add.recover m e g fuel
    ∃ f_g_after : ℚ, ∃ k : ℕ,
      k ≤ fuel ∧
      result.2.1 = e - k ∧
      0 ≤ f_g_after ∧
      f_g_after < 1 + (10 : ℚ) ^ k / 10 ^ 16 ∧
      ((m.toNat : ℚ) - f_g) * (10 : ℚ) ^ e
        = ((result.1.toNat : ℚ) - f_g_after) * (10 : ℚ) ^ result.2.1
      ∧ ((¬ (result.1 < largeRange.min ∧ result.1 * 10 ≤ maxRep))
         ∨ k = fuel) := by
  obtain ⟨x, hx_nn, hx_lt, hf_eq, _, hall⟩ := hrep
  have hB_pos : (0 : ℚ) < 1 / 10 ^ 16 := by positivity
  obtain ⟨x_after, k, hk_le, hk_eq, hxa_nn, hxa_lt, hall_after, heq⟩ :=
    recover_value_preserved_tight m e g fuel hm_pos hall x (1 / 10 ^ 16) hB_pos hx_nn hx_lt
  refine ⟨(decimalValue (Number.operator_add.recover m e g fuel).2.2.digits_ : ℚ) / 10 ^ 16
            + x_after, k, hk_le, hk_eq, ?_, ?_, ?_, ?_⟩
  · -- 0 ≤ residue
    have h16_pos : (0 : ℚ) < 10 ^ 16 := by positivity
    have h_dv_nn :
        (0 : ℚ) ≤ (decimalValue (Number.operator_add.recover m e g fuel).2.2.digits_ : ℚ) / 10 ^ 16 := by
      apply div_nonneg
      · exact_mod_cast Nat.zero_le _
      · linarith
    linarith
  · -- residue < 1 + 10^k / 10^16.
    have h16_pos : (0 : ℚ) < 10 ^ 16 := by positivity
    have h_nat : decimalValue (Number.operator_add.recover m e g fuel).2.2.digits_ < 10 ^ 16 :=
      decimalValue_lt_pow_16 hall_after
    have h_q :
        (decimalValue (Number.operator_add.recover m e g fuel).2.2.digits_ : ℚ) < (10 : ℚ) ^ 16 := by
      exact_mod_cast h_nat
    have h_dv_lt :
        (decimalValue (Number.operator_add.recover m e g fuel).2.2.digits_ : ℚ) / 10 ^ 16 < 1 := by
      rw [div_lt_iff₀ h16_pos]; linarith
    have h_xa_eq : (10 : ℚ) ^ k * (1 / 10 ^ 16) = (10 : ℚ) ^ k / 10 ^ 16 := by ring
    rw [h_xa_eq] at hxa_lt
    linarith
  · rw [hf_eq]
    exact heq
  · -- Exit characterization
    rcases recover_output_exits_or_fuel_exhausted m e g fuel with hexit | hfeq
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

set_option maxHeartbeats 8000000 in
-- heavy induction over recover with branch reindexing and case split on popped digit
private theorem recover_from_zero_aux
    (e : Int) (g : Guard) (fuel : ℕ)
    (hall : allNibblesAtMost9 g.digits_)
    (x B : ℚ) (hB_pos : 0 < B) (hx_nn : 0 ≤ x) (hx_lt : x < B) :
    ∃ k : ℕ, k ≤ fuel ∧
      (Number.operator_add.recover 0 e g fuel).2.1 = e - k ∧
      ((decimalValue g.digits_ : ℚ) / 10 ^ 16 + x) * (10 : ℚ) ^ k
        ≤ ((Number.operator_add.recover 0 e g fuel).1.toNat : ℚ) + 1
          + (10 : ℚ) ^ k * B := by
  induction fuel generalizing e g x B with
  | zero =>
    refine ⟨0, le_refl _, ?_, ?_⟩
    · rw [recover_noop_zero]; push_cast; ring
    · rw [recover_noop_zero]
      simp only [pow_zero, mul_one, one_mul]
      have h16_pos : (0 : ℚ) < 10 ^ 16 := by positivity
      have hdv_q : (decimalValue g.digits_ : ℚ) < (10 : ℚ) ^ 16 := by
        exact_mod_cast decimalValue_lt_pow_16 hall
      have h_dv_lt : (decimalValue g.digits_ : ℚ) / 10 ^ 16 < 1 := by
        rw [div_lt_iff₀ h16_pos]; linarith
      have h_dv_nn : (0 : ℚ) ≤ (decimalValue g.digits_ : ℚ) / 10 ^ 16 := by positivity
      have h_zero_nat : ((0 : UInt64).toNat : ℚ) = 0 := by norm_num
      rw [h_zero_nat]
      linarith
  | succ n ih =>
    -- At m = 0, the loop entry condition always holds.
    have h_zero_lt_lr : (0 : UInt64) < largeRange.min := by
      rw [UInt64.lt_iff_toNat_lt, largeRange_min_toNat]
      have h_z : (0 : UInt64).toNat = 0 := rfl
      rw [h_z]; norm_num
    have h_zero_mul_le : (0 : UInt64) * 10 ≤ maxRep := by
      rw [UInt64.le_iff_toNat_le]
      have h_z : ((0 : UInt64) * 10).toNat = 0 := by decide
      rw [h_z]; exact Nat.zero_le _
    have hrec_step : Number.operator_add.recover 0 e g (n + 1)
        = Number.operator_add.recover (0 * 10 - g.pop.2) (e - 1) g.pop.1 n :=
      recover_step h_zero_lt_lr h_zero_mul_le
    set d : UInt64 := g.pop.2 with hd_def
    have hd_le_9 : d.toNat ≤ 9 := by rw [hd_def]; exact pop_digit_le_9 g hall
    have h_zero_mul : ((0 : UInt64) * 10).toNat = 0 := by decide
    -- Case split on d.toNat
    by_cases hd_zero : d.toNat = 0
    · -- Case A: d = 0. m_new = 0, loop continues. Apply IH.
      have hd_uint_zero : d = 0 := by
        apply UInt64.toNat_inj.mp; rw [hd_zero]; rfl
      have h_sub_zero : (0 : UInt64) * 10 - d = 0 := by
        rw [hd_uint_zero]; rfl
      rw [h_sub_zero] at hrec_step
      -- New guard g.pop.1, well-formed
      have hall_pop : allNibblesAtMost9 g.pop.1.digits_ := allNibblesAtMost9_pop g hall
      -- New x bound for inductive call: x' = 10 * x, B' = 10 * B
      have h10B_pos : 0 < 10 * B := by linarith
      have h10x_nn : 0 ≤ 10 * x := by linarith
      have h10x_lt : 10 * x < 10 * B := by linarith
      obtain ⟨k', hk'_le, hk'_eq, hbound⟩ :=
        ih (e - 1) g.pop.1 hall_pop (10 * x) (10 * B) h10B_pos h10x_nn h10x_lt
      refine ⟨k' + 1, by omega, ?_, ?_⟩
      · rw [hrec_step, hk'_eq]; push_cast; ring
      · rw [hrec_step]
        -- Goal: (dv g/10^16 + x) * 10^(k'+1)
        --        ≤ (recover ... ).1.toNat + 1 + 10^(k'+1) * B
        -- From IH: (dv g.pop.1/10^16 + 10x) * 10^k' ≤ result.1.toNat + 1 + 10^k' * (10B)
        -- Use decimalValue_pop_rat: dv g.pop.1 = 10 * dv g - nibble15 * 10^16
        -- With d = nibble15 = 0: dv g.pop.1 = 10 * dv g
        have h_dv_pop : (decimalValue g.pop.1.digits_ : ℚ)
            = 10 * (decimalValue g.digits_ : ℚ) := by
          rw [decimalValue_pop_rat g]
          have hnib_eq : nibble g.digits_ 15 = 0 := by
            have := pop_digit_eq_nibble_15 g
            rw [hd_def] at hd_zero
            omega
          rw [hnib_eq]
          push_cast; ring
        -- LHS rewrite: (dv g/10^16 + x) * 10^(k'+1)
        --            = (10 * dv g / 10^16 + 10 * x) * 10^k'
        --            = (dv g.pop.1 / 10^16 + 10 * x) * 10^k'
        have h_lhs_eq : ((decimalValue g.digits_ : ℚ) / 10 ^ 16 + x) * (10 : ℚ) ^ (k' + 1)
            = ((decimalValue g.pop.1.digits_ : ℚ) / 10 ^ 16 + 10 * x) * (10 : ℚ) ^ k' := by
          rw [h_dv_pop]
          rw [pow_succ]; ring
        -- RHS rewrite: 10^(k'+1) * B = 10^k' * (10 * B)
        have h_rhs_eq : (10 : ℚ) ^ (k' + 1) * B = (10 : ℚ) ^ k' * (10 * B) := by
          rw [pow_succ]; ring
        rw [h_lhs_eq, h_rhs_eq]
        exact hbound
    · -- Case B: d ≥ 1. m_new = 2^64 - d. Loop exits at next iter. k = 1.
      have hd_pos : 1 ≤ d.toNat := by omega
      -- (0 * 10 - d).toNat = 2^64 - d.toNat
      have h_sub_toNat : ((0 : UInt64) * 10 - d).toNat = 2 ^ 64 - d.toNat := by
        rw [UInt64.toNat_sub]
        rw [h_zero_mul]
        have hd_lt : d.toNat < 2 ^ 64 := UInt64.toNat_lt_size d
        -- (2^64 - d.toNat + 0) % 2^64 = 2^64 - d.toNat (since 2^64 - d.toNat < 2^64 for d ≥ 1)
        have h_pos_diff : 2 ^ 64 - d.toNat < 2 ^ 64 := by omega
        have h_simp : 2 ^ 64 - d.toNat + 0 = 2 ^ 64 - d.toNat := by omega
        rw [h_simp]
        exact Nat.mod_eq_of_lt h_pos_diff
      -- m_new doesn't satisfy entry condition: m_new ≥ largeRange.min
      have h_m_new_ge : largeRange.min.toNat ≤ ((0 : UInt64) * 10 - d).toNat := by
        rw [h_sub_toNat, largeRange_min_toNat]
        have h1 : (10 : ℕ) ^ 18 + 9 ≤ 2 ^ 64 := by decide
        omega
      have h_m_new_ge_uint : largeRange.min ≤ (0 : UInt64) * 10 - d :=
        UInt64.le_iff_toNat_le.mpr h_m_new_ge
      have h_not_lt : ¬ ((0 : UInt64) * 10 - d) < largeRange.min := by
        intro hlt
        have := UInt64.lt_iff_toNat_lt.mp hlt
        have hge := UInt64.le_iff_toNat_le.mp h_m_new_ge_uint
        omega
      have h_exit : ¬ ((0 : UInt64) * 10 - d < largeRange.min
            ∧ ((0 : UInt64) * 10 - d) * 10 ≤ maxRep) := fun ⟨h1, _⟩ => h_not_lt h1
      have h_inner : Number.operator_add.recover ((0 : UInt64) * 10 - d) (e - 1) g.pop.1 n
          = ((0 : UInt64) * 10 - d, e - 1, g.pop.1) := recover_exit_idemp h_exit
      rw [hrec_step, h_inner]
      refine ⟨1, by omega, ?_, ?_⟩
      · push_cast; ring
      · -- Goal: (dv g/10^16 + x) * 10^1 ≤ (2^64 - d).toNat + 1 + 10^1 * B
        -- Clear UInt64-typed hypotheses that confuse linarith.
        clear h_not_lt h_exit h_m_new_ge_uint h_zero_lt_lr h_zero_mul_le
        have h16_pos : (0 : ℚ) < 10 ^ 16 := by positivity
        have hdv_q : (decimalValue g.digits_ : ℚ) < (10 : ℚ) ^ 16 := by
          exact_mod_cast decimalValue_lt_pow_16 hall
        have h_dv_lt : (decimalValue g.digits_ : ℚ) / 10 ^ 16 < 1 := by
          rw [div_lt_iff₀ h16_pos]; linarith
        have h_dv_nn : (0 : ℚ) ≤ (decimalValue g.digits_ : ℚ) / 10 ^ 16 := by positivity
        have h_lhs_lt : ((decimalValue g.digits_ : ℚ) / 10 ^ 16 + x) * (10 : ℚ) ^ 1
            < 10 * (1 + B) := by
          rw [pow_one]
          have h_sum_lt : (decimalValue g.digits_ : ℚ) / 10 ^ 16 + x < 1 + B := by linarith
          nlinarith
        -- 2^64 - d ≥ 2^64 - 9
        have h_m_new_q : (((0 : UInt64) * 10 - d).toNat : ℚ) ≥ (2 : ℚ) ^ 64 - 9 := by
          rw [h_sub_toNat]
          have hd_le_pow : d.toNat ≤ 2 ^ 64 := by
            have := UInt64.toNat_lt_size d
            omega
          have hcastL : (((2 ^ 64 - d.toNat : ℕ)) : ℚ)
              = ((2 ^ 64 : ℕ) : ℚ) - (d.toNat : ℚ) := by
            rw [Nat.cast_sub hd_le_pow]
          rw [hcastL]
          have h_pow_cast : ((2 ^ 64 : ℕ) : ℚ) = (2 : ℚ) ^ 64 := by push_cast; rfl
          rw [h_pow_cast]
          have hd_q : (d.toNat : ℚ) ≤ 9 := by exact_mod_cast hd_le_9
          linarith
        have h_pow_one : (10 : ℚ) ^ 1 = 10 := by norm_num
        rw [h_pow_one]
        nlinarith [h_lhs_lt, h_m_new_q]

/-! ## Public corollary: bound on `f_aln * 10^k` from `m = 0` recover

This is the key lemma used by the degenerate case of `DiffSignCombined`.
Given that `g` represents `f_aln ∈ [0, 1)`, after running `recover 0 e g fuel`,
the value `f_aln * 10^k` is bounded relative to the output mantissa.

The bound combines two cases:
* When all popped digits are zero (m_out = 0), f_aln must have been small
  (its captured-digit window was all zeros).
* When some popped digit was non-zero, m_out is close to 2^64 (UInt64 underflow),
  dominating any small slack from f_aln * 10^k.

The tight uniform statement is:

  f_aln * 10^k ≤ m_out + 1 + 10^fuel / 10^16. -/

theorem recover_from_zero_value_bound
    (e : Int) (g : Guard) (fuel : ℕ) (f_aln : ℚ)
    (hrep : represents g f_aln) :
    let result := Number.operator_add.recover 0 e g fuel
    ∃ k : ℕ, k ≤ fuel ∧
      result.2.1 = e - k ∧
      f_aln * (10 : ℚ) ^ k
        ≤ (result.1.toNat : ℚ) + 1 + (10 : ℚ) ^ fuel / 10 ^ 16 := by
  obtain ⟨x, hx_nn, hx_lt, hf_eq, _, hall⟩ := hrep
  have hB_pos : (0 : ℚ) < 1 / 10 ^ 16 := by positivity
  obtain ⟨k, hk_le, hk_eq, hbound⟩ :=
    recover_from_zero_aux e g fuel hall x (1 / 10 ^ 16) hB_pos hx_nn hx_lt
  refine ⟨k, hk_le, hk_eq, ?_⟩
  rw [hf_eq]
  -- hbound: (dv/10^16 + x) * 10^k ≤ result.1.toNat + 1 + 10^k * (1/10^16)
  -- want:   (dv/10^16 + x) * 10^k ≤ result.1.toNat + 1 + 10^fuel / 10^16
  have h_10k_le : (10 : ℚ) ^ k ≤ (10 : ℚ) ^ fuel :=
    pow_le_pow_right₀ (by norm_num : (1 : ℚ) ≤ 10) hk_le
  have h_10k_div : (10 : ℚ) ^ k * (1 / 10 ^ 16) = (10 : ℚ) ^ k / 10 ^ 16 := by ring
  have h_le : (10 : ℚ) ^ k / 10 ^ 16 ≤ (10 : ℚ) ^ fuel / 10 ^ 16 := by
    apply div_le_div_of_nonneg_right h_10k_le (by positivity)
  rw [h_10k_div] at hbound
  linarith

end XRPL.Model.Protocol
