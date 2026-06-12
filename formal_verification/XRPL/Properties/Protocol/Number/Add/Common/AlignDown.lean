import Mathlib.Tactic

import XRPL.Model.Protocol.Number
import XRPL.Properties.Protocol.Number.Rounding.Guard
import XRPL.Properties.Protocol.Number.Rounding.ScaleDown

set_option linter.style.longLine false
set_option linter.style.emptyLine false

namespace XRPL.Model.Protocol

/-! # Shared invariants for `Number.operator_add`

`Number.operator_add` rescales the smaller-exponent operand up to the larger
exponent via a nested helper `alignDown`. Each step is exactly one application
of `Guard.doDropDigit` (mantissa /= 10, exponent += 1, guard absorbs the
dropped digit). This file isolates the algorithmic invariants of `alignDown`
that downstream proofs reuse:

* `alignDown_noop` / `alignDown_step` — equation lemmas (unfold cases).
* `alignDown_e_eq` — output exponent equals `max e target`.
* `alignDown_mantissa_eq` — output mantissa equals `m / 10 ^ (target - e).toNat`.
* `alignDown_value` — algebraic invariant: `m * 10 ^ e` equals
  `m' * 10 ^ e' + (truncated digits) * 10 ^ e`.
* `alignDown_represents` — the guard captures the truncated tail in `ℚ`.

These are statement-level facts about the C++-mirrored recursion; the
per-mode bound proofs combine them with rounding semantics. -/

/-! ## Equation lemmas -/

/-- No-op case: when `e ≥ target`, `alignDown` returns its inputs. -/
lemma alignDown_noop {m : UInt64} {e : Int} {g : Guard} {target : Int}
    (h : ¬ e < target) :
    Number.operator_add.alignDown m e g target = (m, e, g) := by
  rw [Number.operator_add.alignDown.eq_def]
  rw [if_neg h]

/-- Step case: when `e < target`, `alignDown` unrolls one `doDropDigit`. -/
lemma alignDown_step {m : UInt64} {e : Int} {g : Guard} {target : Int}
    (h : e < target) :
    Number.operator_add.alignDown m e g target =
      Number.operator_add.alignDown (m / 10) (e + 1) (g.push (m % 10)) target := by
  rw [Number.operator_add.alignDown.eq_def]
  rw [if_pos h]
  rfl

/-! ## Exponent invariant -/

/-- The output exponent of `alignDown` is always `max e target`. -/
theorem alignDown_e_eq (m : UInt64) (e : Int) (g : Guard) (target : Int) :
    (Number.operator_add.alignDown m e g target).2.1 = max e target := by
  induction m, e, g using Number.operator_add.alignDown.induct target with
  | case1 m e g hlt IH =>
    simp only [Guard.doDropDigit] at IH
    rw [alignDown_step hlt]
    rw [IH]
    -- After one step e becomes e + 1; show max (e+1) target = max e target when e < target.
    have hmax_r : max e target = target := max_eq_right (le_of_lt hlt)
    by_cases h2 : e + 1 ≤ target
    · rw [max_eq_right h2, hmax_r]
    · push_neg at h2
      rw [max_eq_left (le_of_lt h2), hmax_r]; omega
  | case2 m e g hnlt =>
    rw [alignDown_noop hnlt]
    push_neg at hnlt
    exact (max_eq_left hnlt).symm

/-- The exponent of `alignDown` output is at least `target`. -/
lemma alignDown_e_ge_target (m : UInt64) (e : Int) (g : Guard) (target : Int) :
    target ≤ (Number.operator_add.alignDown m e g target).2.1 := by
  rw [alignDown_e_eq]; exact le_max_right _ _

/-- The exponent of `alignDown` output is at least the initial `e`. -/
lemma alignDown_e_ge_e (m : UInt64) (e : Int) (g : Guard) (target : Int) :
    e ≤ (Number.operator_add.alignDown m e g target).2.1 := by
  rw [alignDown_e_eq]; exact le_max_left _ _

/-! ## Mantissa monotonicity / no-overflow -/

/-- The output mantissa of `alignDown` is at most the input mantissa.
Each step divides by 10, so the value can only shrink. -/
theorem alignDown_mantissa_le (m : UInt64) (e : Int) (g : Guard) (target : Int) :
    (Number.operator_add.alignDown m e g target).1.toNat ≤ m.toNat := by
  induction m, e, g using Number.operator_add.alignDown.induct target with
  | case1 m e g hlt IH =>
    simp only [Guard.doDropDigit] at IH
    rw [alignDown_step hlt]
    have hstep : (m / 10).toNat ≤ m.toNat := by
      rw [UInt64.toNat_div]
      exact Nat.div_le_self _ _
    exact le_trans IH hstep
  | case2 m e g hnlt =>
    rw [alignDown_noop hnlt]

/-- `alignDown` preserves the guard's sticky bit (`Guard.push` only updates
`digits_`/`xbit_`). -/
theorem alignDown_sbit (m : UInt64) (e : Int) (g : Guard) (target : Int) :
    (Number.operator_add.alignDown m e g target).2.2.sbit_ = g.sbit_ := by
  induction m, e, g using Number.operator_add.alignDown.induct target with
  | case1 m e g hlt IH =>
    simp only [Guard.doDropDigit] at IH
    rw [alignDown_step hlt, IH]
    rfl
  | case2 m e g hnlt =>
    rw [alignDown_noop hnlt]

/-! ## Value preservation -/

/-- Number of steps performed by `alignDown m e g target`. -/
def alignDown_steps (e target : Int) : ℕ := (target - e).toNat

/-- Output mantissa is `m / 10 ^ k` for `k = (max e target - e).toNat`. -/
theorem alignDown_mantissa_eq (m : UInt64) (e : Int) (g : Guard) (target : Int) :
    (Number.operator_add.alignDown m e g target).1.toNat
      = m.toNat / 10 ^ (max e target - e).toNat := by
  induction m, e, g using Number.operator_add.alignDown.induct target with
  | case1 m e g hlt IH =>
    simp only [Guard.doDropDigit] at IH
    rw [alignDown_step hlt]
    rw [IH]
    -- IH: result.1.toNat = (m / 10).toNat / 10 ^ (max (e+1) target - (e+1)).toNat
    have hm10 : ((m / 10 : UInt64)).toNat = m.toNat / 10 := by
      rw [UInt64.toNat_div]; rfl
    rw [hm10]
    -- max (e+1) target - (e+1) = max e target - e - 1 when e < target.
    have hmax_le : e ≤ max e target := le_max_left _ _
    have htarget_le_max : target ≤ max e target := le_max_right _ _
    have hmax_e1 : max (e + 1) target = max e target := by
      have hmax_r : max e target = target := max_eq_right (le_of_lt hlt)
      rw [hmax_r]
      by_cases h2 : e + 1 ≤ target
      · exact max_eq_right h2
      · push_neg at h2
        rw [max_eq_left (le_of_lt h2)]; omega
    rw [hmax_e1]
    -- Now need: m.toNat / 10 / 10 ^ k = m.toNat / 10 ^ (k+1) where k+1 = (max e target - e).toNat.
    have hpos : 0 < (max e target - e).toNat := by
      have : 0 < max e target - e := by
        have : e < max e target := lt_of_lt_of_le hlt htarget_le_max
        omega
      omega
    set K := (max e target - e).toNat with hK_def
    have hK1 : (max e target - (e + 1)).toNat = K - 1 := by
      rw [hK_def]
      have : max e target - (e + 1) = max e target - e - 1 := by ring
      rw [this]
      omega
    rw [hK1]
    rw [Nat.div_div_eq_div_mul]
    have hKform : 10 * 10 ^ (K - 1) = 10 ^ K := by
      have hKsucc : K = (K - 1) + 1 := by omega
      conv_rhs => rw [hKsucc]
      ring
    rw [hKform]
  | case2 m e g hnlt =>
    rw [alignDown_noop hnlt]
    push_neg at hnlt
    have hmax : max e target = e := max_eq_left hnlt
    rw [hmax]
    simp

/-! ## Guard representation invariant

The guard captures all digits removed during alignment. If the initial guard
represents `f0`, then after `alignDown m e g target`, the output guard
represents `(f0 + (m.toNat % 10 ^ k : ℚ)) / 10 ^ k`, where `k` is the number
of steps. This mirrors the corresponding `scaleDown128_correct` invariant. -/

/-- Guard captures the truncated tail.

If `g0` represents fraction `f0 ∈ [0, 1)`, then after `alignDown m e g0 target`,
the output guard represents `(f0 + (m.toNat % 10 ^ k)) / 10 ^ k`, where
`k = (max e target - e).toNat`. -/
theorem alignDown_represents
    (m : UInt64) (e : Int) (g0 : Guard) (target : Int)
    (f0 : ℚ) (hrep0 : represents g0 f0) :
    represents (Number.operator_add.alignDown m e g0 target).2.2
      ((f0 + ((m.toNat % 10 ^ (max e target - e).toNat : ℕ) : ℚ))
         / 10 ^ (max e target - e).toNat) := by
  induction m, e, g0 using Number.operator_add.alignDown.induct target
    generalizing f0 with
  | case1 m e g0 hlt IH =>
    simp only [Guard.doDropDigit] at IH
    -- One step then IH.
    have h10_uval : (10 : UInt64).toNat = 10 := rfl
    have hd_lt : (m % 10).toNat < 10 := by
      rw [UInt64.toNat_mod, h10_uval]
      omega
    have hpush := represents_push hrep0 hd_lt
    have hd_eq : ((m % 10 : UInt64).toNat : ℚ) = ((m.toNat % 10 : ℕ) : ℚ) := by
      rw [UInt64.toNat_mod, h10_uval]
    have hstep := alignDown_step (m := m) (e := e) (g := g0) (target := target) hlt
    rw [hstep]
    -- IH gives the represents claim on the recursive output with the pushed guard.
    set f1 : ℚ := (f0 + ((m % 10 : UInt64).toNat : ℚ)) / 10 with hf1_def
    have hIH := IH (f0 := f1) hpush
    -- Reindex via the relations between K and K-1.
    have hmax_e1 : max (e + 1) target = max e target := by
      have hmax_r : max e target = target := max_eq_right (le_of_lt hlt)
      rw [hmax_r]
      by_cases h2 : e + 1 ≤ target
      · exact max_eq_right h2
      · push_neg at h2
        rw [max_eq_left (le_of_lt h2)]; omega
    set K : ℕ := (max e target - e).toNat with hK_def
    have hK_pos : 0 < K := by
      rw [hK_def]
      have hlt' : e < max e target := lt_of_lt_of_le hlt (le_max_right _ _)
      omega
    have hKstep : (max (e + 1) target - (e + 1)).toNat = K - 1 := by
      rw [hmax_e1, hK_def]
      have heq : max e target - (e + 1) = max e target - e - 1 := by ring
      rw [heq]; omega
    have hm10 : ((m / 10 : UInt64)).toNat = m.toNat / 10 := by
      rw [UInt64.toNat_div]; rfl
    have hKsucc : K = (K - 1) + 1 := by omega
    have hstep_nat : m.toNat % 10 ^ K
        = 10 * ((m / 10 : UInt64).toNat % 10 ^ (K - 1)) + m.toNat % 10 := by
      rw [hm10]
      conv_lhs => rw [hKsucc]
      exact nat_mod_pow_step _ _
    have hstep_q : ((m.toNat % 10 ^ K : ℕ) : ℚ)
        = 10 * (((m / 10 : UInt64).toNat % 10 ^ (K - 1) : ℕ) : ℚ)
            + ((m.toNat % 10 : ℕ) : ℚ) := by
      exact_mod_cast hstep_nat
    have target_eq :
        (f0 + ((m.toNat % 10 ^ K : ℕ) : ℚ)) / 10 ^ K
          = (f1 + (((m / 10 : UInt64).toNat % 10 ^ (K - 1) : ℕ) : ℚ)) / 10 ^ (K - 1) := by
      have hKK : (10 : ℚ) ^ K = 10 * 10 ^ (K - 1) := by
        conv_lhs => rw [hKsucc]
        rw [pow_succ]; ring
      rw [hstep_q, hKK]
      have hf1_q : f1 = (f0 + ((m.toNat % 10 : ℕ) : ℚ)) / 10 := by
        rw [hf1_def, hd_eq]
      rw [hf1_q]
      have hK1_ne : (10 : ℚ) ^ (K - 1) ≠ 0 := by positivity
      field_simp
      ring
    -- Goal at this point is in terms of K = (max e target - e).toNat.
    -- Rewrite goal using target_eq, then close with IH (after reindexing).
    change represents
      (Number.operator_add.alignDown (m / 10) (e + 1) (g0.push (m % 10)) target).2.2
      ((f0 + ((m.toNat % 10 ^ K : ℕ) : ℚ)) / 10 ^ K)
    rw [target_eq]
    -- hIH gives: represents ((alignDown (m/10) (e+1) (push g0 d) target).2.2)
    --             ((f1 + ((m/10).toNat % 10 ^ (max (e+1) target - (e+1)).toNat))
    --              / 10 ^ (max (e+1) target - (e+1)).toNat)
    rw [hKstep] at hIH
    exact hIH
  | case2 m e g0 hnlt =>
    rw [alignDown_noop hnlt]
    push_neg at hnlt
    have hmax : max e target = e := max_eq_left hnlt
    have hk0 : (max e target - e).toNat = 0 := by rw [hmax]; simp
    rw [hk0]
    simp only [pow_zero, Nat.mod_one, Nat.cast_zero, add_zero, div_one]
    exact hrep0

/-! ## Drops-vs-xbit and aligned-mantissa bounds -/

/-- If the aligned guard's `xbit_` is set (the truncated tail is nonzero in the
deep digits), at least 17 digits were dropped: `17 ≤ k = (max e target - e).toNat`.

Integrality argument: the represented tail `r = (m % 10^k)/10^k` satisfies
`r * 10^16 = decimalValue(guard) + x * 10^16` with `0 < x*10^16 < 1`. If `k ≤ 16`
then `r * 10^16` is a natural number, but it also lies strictly between two
consecutive naturals — contradiction. -/
theorem alignDown_steps_ge_17_of_xbit
    (m : UInt64) (e : Int) (g0 : Guard) (target : Int)
    (hg0 : represents g0 0)
    (hxbit : (Number.operator_add.alignDown m e g0 target).2.2.xbit_ = true) :
    17 ≤ (max e target - e).toNat := by
  set k := (max e target - e).toNat with hk_def
  -- The output guard represents r.
  have hrep := alignDown_represents m e g0 target 0 hg0
  rw [← hk_def] at hrep
  -- Simplify the represented value to r = (m.toNat % 10^k : ℚ) / 10^k.
  have hr_eq :
      ((0 : ℚ) + ((m.toNat % 10 ^ k : ℕ) : ℚ)) / 10 ^ k
        = ((m.toNat % 10 ^ k : ℕ) : ℚ) / 10 ^ k := by
    rw [zero_add]
  rw [hr_eq] at hrep
  obtain ⟨x, hx_nn, hx_lt, hf, hxbit_iff, _hall⟩ := hrep
  -- From hxbit, x > 0.
  have hxpos : x > 0 := hxbit_iff.mp hxbit
  -- Suppose for contradiction k ≤ 16.
  by_contra hk_le
  push_neg at hk_le
  have hk16 : k ≤ 16 := by omega
  -- Name decimalValue guard as a fresh Nat N (keeps field_simp from unfolding alignDown).
  obtain ⟨N, hN_def⟩ :
      ∃ N : ℕ, N = decimalValue (Number.operator_add.alignDown m e g0 target).2.2.digits_ :=
    ⟨_, rfl⟩
  rw [← hN_def] at hf
  -- Name the residue mod as a fresh Nat r0 too.
  obtain ⟨r0, hr0_def⟩ : ∃ r0 : ℕ, r0 = m.toNat % 10 ^ k := ⟨_, rfl⟩
  rw [← hr0_def] at hf
  -- r * 10^16 = N + x * 10^16.
  have h16ne : (10 : ℚ) ^ 16 ≠ 0 := by positivity
  have hkne : (10 : ℚ) ^ k ≠ 0 := by positivity
  -- M := r0 * 10^(16-k), a Nat, equals r * 10^16.
  obtain ⟨M, hM_def⟩ : ∃ M : ℕ, M = r0 * 10 ^ (16 - k) := ⟨_, rfl⟩
  -- 10^16 = 10^(16-k) * 10^k in ℚ.
  have hsplit : (10 : ℚ) ^ 16 = 10 ^ (16 - k) * 10 ^ k := by
    rw [← pow_add]; congr 1; omega
  have hMr : (M : ℚ) = (r0 : ℚ) / 10 ^ k * 10 ^ 16 := by
    rw [hM_def]
    push_cast
    rw [hsplit]
    field_simp
  -- From hf : r = N/10^16 + x, multiply by 10^16.
  have hf16 : (r0 : ℚ) / 10 ^ k * 10 ^ 16 = (N : ℚ) + x * 10 ^ 16 := by
    rw [hf]
    field_simp
  have hMeq : (M : ℚ) = (N : ℚ) + x * 10 ^ 16 := by rw [hMr, hf16]
  -- So x * 10^16 = M - N, with 0 < x*10^16 < 1.
  have hxlow : (0 : ℚ) < x * 10 ^ 16 := by positivity
  have hxhigh : x * 10 ^ 16 < 1 := by
    have : x < 1 / 10 ^ 16 := hx_lt
    have h16pos : (0 : ℚ) < 10 ^ 16 := by positivity
    rw [lt_div_iff₀ h16pos] at this
    linarith
  -- 0 < M - N < 1 for naturals: impossible.
  have hdiff : (M : ℚ) - (N : ℚ) = x * 10 ^ 16 := by linarith [hMeq]
  have hMN_lt : (M : ℚ) < (N : ℚ) + 1 := by linarith
  have hMN_gt : (N : ℚ) < (M : ℚ) := by linarith
  have hMN_lt' : M < N + 1 := by exact_mod_cast hMN_lt
  have hMN_gt' : N < M := by exact_mod_cast hMN_gt
  omega

/-- When the aligned guard's `xbit_` is set and the mantissa fits in 19 digits,
the aligned mantissa is at most 99.

By `alignDown_steps_ge_17_of_xbit`, at least 17 digits are dropped, so the
aligned mantissa `m / 10^k ≤ m / 10^17 ≤ (10^19 - 1) / 10^17 ≤ 99`. -/
theorem alignDown_mantissa_le_99_of_xbit
    (m : UInt64) (e : Int) (g0 : Guard) (target : Int)
    (hg0 : represents g0 0)
    (hm : m.toNat < 10 ^ 19)
    (hxbit : (Number.operator_add.alignDown m e g0 target).2.2.xbit_ = true) :
    (Number.operator_add.alignDown m e g0 target).1.toNat ≤ 99 := by
  have hk17 : 17 ≤ (max e target - e).toNat :=
    alignDown_steps_ge_17_of_xbit m e g0 target hg0 hxbit
  set k := (max e target - e).toNat with hk_def
  rw [alignDown_mantissa_eq]
  rw [← hk_def]
  -- m.toNat / 10^k ≤ m.toNat / 10^17.
  have hpow_le : (10 : ℕ) ^ 17 ≤ 10 ^ k := Nat.pow_le_pow_right (by norm_num) hk17
  have hstep1 : m.toNat / 10 ^ k ≤ m.toNat / 10 ^ 17 :=
    Nat.div_le_div_left hpow_le (by positivity)
  -- m.toNat / 10^17 ≤ 99 since m.toNat < 10^19 = 100 * 10^17.
  have hstep2 : m.toNat / 10 ^ 17 ≤ 99 := by
    have h1019 : (10 : ℕ) ^ 19 = 100 * 10 ^ 17 := by norm_num
    rw [h1019] at hm
    omega
  exact le_trans hstep1 hstep2


end XRPL.Model.Protocol
