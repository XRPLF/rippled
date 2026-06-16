import Mathlib.Data.Nat.Bitwise
import Mathlib.Tactic

import XRPL.Properties.Protocol.Number.Common.Rounding.BitVec

namespace XRPL.Model.Protocol

/-! # Guard semantics

Packed-nibble representation of the fractional part dropped during
mantissa scaling. -/

/-- The `p`-th packed nibble of `d`, as a `Nat` in `[0, 16)`. -/
def nibble (d : UInt64) (p : ℕ) : ℕ := (d.toNat / 16 ^ p) % 16

/-- Decimal value of `digits_` interpreted as 16 packed decimal digits,
positional-notation style (nibble at position `p` contributes `· 10^p`). -/
def decimalValue (d : UInt64) : ℕ :=
  ∑ p ∈ Finset.range 16, nibble d p * 10 ^ p

/-- Every packed nibble encodes a decimal digit (0..9), not a hex digit (0..15). -/
def allNibblesAtMost9 (d : UInt64) : Prop :=
  ∀ p : Fin 16, nibble d p.val ≤ 9

/-- `g` represents rational fraction `f ∈ [0, 1)`. -/
def represents (g : Guard) (f : ℚ) : Prop :=
  ∃ x : ℚ,
    0 ≤ x ∧ x < 1 / 10 ^ 16 ∧
    f = (decimalValue g.digits_ : ℚ) / 10 ^ 16 + x ∧
    (g.xbit_ = true ↔ x > 0) ∧
    allNibblesAtMost9 g.digits_

/-! ## Core lemmas -/

/-- Every nibble of `(0 : UInt64)` is `0`. -/
lemma nibble_zero (p : ℕ) : nibble 0 p = 0 := by
  simp [nibble]

/-- `decimalValue 0 = 0`. -/
lemma decimalValue_zero : decimalValue (0 : UInt64) = 0 := by
  simp [decimalValue, nibble_zero]

/-- `allNibblesAtMost9 0`. -/
lemma allNibblesAtMost9_zero : allNibblesAtMost9 (0 : UInt64) := by
  intro p; simp [nibble_zero]

/-- `Guard.new` represents zero. -/
lemma represents_new : represents Guard.new 0 := by
  refine ⟨0, le_refl 0, ?_, ?_, ?_, ?_⟩
  · positivity
  · simp [Guard.new, decimalValue_zero]
  · simp [Guard.new]
  · exact allNibblesAtMost9_zero

/-- `Guard.new` and `Guard.new.set_negative` both represent the fraction `0`.
Shared by the `Normalize` and `Mul` algorithmic-facts files (the initial guard
seeded by sign). -/
lemma g0_represents_zero (negative : Bool) :
    represents (if negative then Guard.new.set_negative else Guard.new) 0 := by
  cases negative
  · simp only [Bool.false_eq_true, if_false]; exact represents_new
  · simp only [if_true]
    obtain ⟨x_rep, hx_nn, hx_lt, hf_eq, hxbit, hall⟩ := represents_new
    refine ⟨x_rep, hx_nn, hx_lt, ?_, ?_, ?_⟩
    · have : Guard.new.set_negative.digits_ = Guard.new.digits_ := rfl
      rw [this]; exact hf_eq
    · have hxbit_eq : Guard.new.set_negative.xbit_ = Guard.new.xbit_ := rfl
      rw [hxbit_eq]; exact hxbit
    · have : Guard.new.set_negative.digits_ = Guard.new.digits_ := rfl
      rw [this]; exact hall

/-- A guard with no packed digits and a clear overflow bit represents exactly
the fraction `0`: `decimalValue` of `digits_ = 0` is `0`, and `xbit_ = false`
forces the hidden tail `x` to be `0`. -/
lemma represents_eq_zero_of_digits_zero_xbit_false {g : Guard} {f : ℚ}
    (hd : g.digits_ = 0) (hx : g.xbit_ = false) (hrep : represents g f) :
    f = 0 := by
  obtain ⟨x, hx_nn, _hx_lt, hf_eq, hxbit_iff, _hall⟩ := hrep
  have hx_zero : x = 0 := by
    by_contra hxne
    have hx_pos : x > 0 := lt_of_le_of_ne hx_nn (Ne.symm hxne)
    have : g.xbit_ = true := hxbit_iff.mpr hx_pos
    rw [hx] at this; exact Bool.noConfusion this
  rw [hf_eq, hd, decimalValue_zero, hx_zero]
  norm_num

/-! ### Supporting lemmas for `represents_push` -/

/-- A UInt64's low nibble equals its `.toNat` mod 16. -/
lemma UInt64.and_15_toNat (a : UInt64) : (a &&& 15).toNat = a.toNat % 16 := by
  rw [UInt64.toNat_and]; exact Nat.and_two_pow_sub_one_eq_mod _ 4

/-- `xbit_` update under `push`: true iff old xbit or old bottom nibble was nonzero. -/
lemma xbit_push (g : Guard) (d : UInt64) :
    (g.push d).xbit_ = (g.xbit_ || decide (nibble g.digits_ 0 ≠ 0)) := by
  unfold Guard.push
  simp only [nibble, pow_zero, Nat.div_one]
  congr 1
  have h16 : (g.digits_ &&& 15).toNat = g.digits_.toNat % 16 := UInt64.and_15_toNat _
  rcases Nat.eq_zero_or_pos (g.digits_.toNat % 16) with h | h
  · have hz : g.digits_ &&& 15 = 0 := by
      rw [← UInt64.toNat_inj]; rw [h16, h]; rfl
    rw [hz]; simp [h]
  · have hnz : g.digits_ &&& 15 ≠ 0 := by
      intro heq
      have : (g.digits_ &&& 15).toNat = 0 := by rw [heq]; rfl
      omega
    have lhs : (g.digits_ &&& 15 != 0) = true := bne_iff_ne.mpr hnz
    have rhs : decide (g.digits_.toNat % 16 ≠ 0) = true := decide_eq_true (by omega)
    rw [lhs, rhs]

/-- Nat-level formula for the `push`ed guard's `digits_`. -/
lemma toNat_push_digits (g : Guard) (d : UInt64) :
    (g.push d).digits_.toNat = g.digits_.toNat / 16 + (d.toNat % 16) * 2 ^ 60 := by
  unfold Guard.push
  change (g.digits_ >>> 4 ||| (d &&& 15) <<< 60).toNat = _
  rw [UInt64.toNat_or, UInt64.toNat_shiftRight, UInt64.toNat_shiftLeft,
      UInt64.and_15_toNat]
  rw [show (4 : UInt64).toNat = 4 from rfl, show (60 : UInt64).toNat = 60 from rfl,
      show (4 : Nat) % 64 = 4 from rfl, show (60 : Nat) % 64 = 60 from rfl,
      Nat.shiftRight_eq_div_pow, Nat.shiftLeft_eq]
  have hshift : d.toNat % 16 * 2 ^ 60 < 2 ^ 64 := by
    have h16 : d.toNat % 16 < 16 := Nat.mod_lt _ (by decide)
    calc d.toNat % 16 * 2^60 ≤ 15 * 2^60 := by
          exact Nat.mul_le_mul_right _ (by omega)
      _ < 2^64 := by norm_num
  rw [Nat.mod_eq_of_lt hshift]
  have hdiv : g.digits_.toNat / 2^4 < 2^60 := by
    have h64 := g.digits_.toNat_lt; omega
  rw [Nat.lor_comm, Nat.mul_comm (d.toNat % 16) (2^60)]
  rw [← Nat.two_pow_add_eq_or_of_lt hdiv]
  have : (2 : Nat)^4 = 16 := by norm_num
  rw [this]; ring

/-- `push` cannot empty a guard's content: if the pushed guard has zero digits
and a clear sticky bit, the source guard had zero digits and a clear sticky bit
(`push` shifts the old low nibble into `xbit_` and keeps the other nibbles). -/
lemma push_content_empty (g : Guard) (d : UInt64)
    (hd : (g.push d).digits_ = 0) (hx : (g.push d).xbit_ = false) :
    g.digits_ = 0 ∧ g.xbit_ = false := by
  have hxp := xbit_push g d
  rw [hx] at hxp
  have hor : (g.xbit_ || decide (nibble g.digits_ 0 ≠ 0)) = false := hxp.symm
  have hgx : g.xbit_ = false := by
    cases h : g.xbit_
    · rfl
    · rw [h, Bool.true_or] at hor; exact absurd hor Bool.noConfusion
  have hnib : nibble g.digits_ 0 = 0 := by
    by_contra hne
    rw [hgx, Bool.false_or, decide_eq_true hne] at hor
    exact absurd hor Bool.noConfusion
  have hnib_eq : nibble g.digits_ 0 = g.digits_.toNat % 16 := by
    unfold nibble; rw [pow_zero, Nat.div_one]
  have hdn : (g.push d).digits_.toNat = 0 := by rw [hd]; rfl
  rw [toNat_push_digits] at hdn
  refine ⟨?_, hgx⟩
  rw [← UInt64.toNat_inj, show (0 : UInt64).toNat = 0 from rfl]
  omega

/-- Top nibble of `push`ed guard is `d mod 16`. Follows from `toNat_push_digits`. -/
lemma nibble_push_top (g : Guard) (d : UInt64) :
    nibble (g.push d).digits_ 15 = d.toNat % 16 := by
  unfold nibble
  rw [toNat_push_digits]
  have h1 : g.digits_.toNat / 16 < 2 ^ 60 := by have := g.digits_.toNat_lt; omega
  have h2 : (16 : Nat) ^ 15 = 2 ^ 60 := by norm_num
  rw [h2, Nat.mul_comm (d.toNat % 16) (2 ^ 60),
      Nat.add_mul_div_left _ _ (by positivity : (0 : Nat) < 2 ^ 60),
      Nat.div_eq_of_lt h1, Nat.zero_add, Nat.mod_mod]

/-- Non-top nibbles of `push`ed guard are the old nibbles shifted up. -/
lemma nibble_push_lt {g : Guard} {d : UInt64} {p : ℕ} (hp : p < 15) :
    nibble (g.push d).digits_ p = nibble g.digits_ (p + 1) := by
  unfold nibble
  rw [toNat_push_digits]
  have h16p_eq : (16 : Nat) ^ p = 2 ^ (4 * p) := by
    rw [show (16 : Nat) = 2 ^ 4 from rfl, ← pow_mul]
  have hdvd : (16 : Nat) ^ p ∣ 2 ^ 60 := by
    rw [h16p_eq]; exact pow_dvd_pow 2 (by omega)
  obtain ⟨k, hk⟩ := hdvd
  rw [hk, show g.digits_.toNat / 16 + d.toNat % 16 * (16 ^ p * k)
         = g.digits_.toNat / 16 + 16 ^ p * (d.toNat % 16 * k) from by ring]
  rw [Nat.add_mul_div_left _ _ (by positivity : (0 : Nat) < 16 ^ p)]
  rw [Nat.div_div_eq_div_mul, show (16 : Nat) * 16 ^ p = 16 ^ (p + 1) from by ring]
  have hk15 : k = 16 ^ (15 - p) := by
    have h60 : (2 : Nat) ^ 60 = 16 ^ 15 := by norm_num
    have : 16 ^ p * k = 16 ^ p * 16 ^ (15 - p) := by
      rw [← pow_add, show p + (15 - p) = 15 from by omega, ← h60, ← hk]
    exact Nat.eq_of_mul_eq_mul_left (by positivity) this
  rw [hk15]
  obtain ⟨q, hq⟩ : ∃ q, 15 - p = q + 1 := ⟨15 - p - 1, by omega⟩
  rw [hq]
  rw [show d.toNat % 16 * 16 ^ (q + 1) = d.toNat % 16 * 16 ^ q * 16 from by
        rw [pow_succ]; ring]
  exact Nat.add_mul_mod_self_right _ _ _

/-! ### `represents_push` helpers -/

/-- Key identity relating `decimalValue` before and after `push`, in `ℕ`. -/
lemma decimalValue_push (g : Guard) (d : UInt64) :
    10 * decimalValue (g.push d).digits_ + nibble g.digits_ 0
      = decimalValue g.digits_ + (d.toNat % 16) * 10 ^ 16 := by
  have lhs_expand : 10 * decimalValue (g.push d).digits_
      = (d.toNat % 16) * 10 ^ 16
        + ∑ p ∈ Finset.range 15, nibble g.digits_ (p + 1) * 10 ^ (p + 1) := by
    unfold decimalValue
    rw [Finset.sum_range_succ, nibble_push_top, Nat.mul_add]
    have sum_eq : 10 * ∑ p ∈ Finset.range 15, nibble (g.push d).digits_ p * 10 ^ p
        = ∑ p ∈ Finset.range 15, nibble g.digits_ (p + 1) * 10 ^ (p + 1) := by
      rw [Finset.mul_sum]
      refine Finset.sum_congr rfl ?_
      intro p hp
      have hp' : p < 15 := Finset.mem_range.mp hp
      rw [nibble_push_lt hp', pow_succ]; ring
    rw [sum_eq]
    ring
  have rhs_expand : decimalValue g.digits_
      = nibble g.digits_ 0
        + ∑ p ∈ Finset.range 15, nibble g.digits_ (p + 1) * 10 ^ (p + 1) := by
    unfold decimalValue
    rw [Finset.sum_range_succ' (fun p => nibble g.digits_ p * 10 ^ p) 15]
    simp [pow_zero, Nat.add_comm]
  rw [lhs_expand, rhs_expand]; ring

/-! ### Per-bullet helpers for `represents_push` -/

/-- `1/10^16 = 10/10^17`. -/
lemma rat_inv_16_eq : (1 : ℚ) / 10 ^ 16 = 10 / 10 ^ 17 := by
  rw [div_eq_div_iff (by positivity) (by positivity)]; ring

/-- Common-denominator reshape. -/
lemma rat_lhs_reshape (x : ℚ) (n : ℕ) :
    x / 10 + (n : ℚ) / 10 ^ 17 = (x * 10 ^ 16 + n) / 10 ^ 17 := by
  rw [eq_div_iff (by positivity : (10 : ℚ) ^ 17 ≠ 0)]
  rw [add_mul, div_mul_cancel₀ _ (by positivity : (10 : ℚ) ^ 17 ≠ 0)]
  congr 1
  rw [show (10 : ℚ) ^ 17 = 10 * 10 ^ 16 from by ring, ← mul_assoc,
      div_mul_cancel₀ x (by norm_num : (10 : ℚ) ≠ 0)]

/-- Bound on the new hidden fraction: `x/10 + nibble/10^17 < 1/10^16`. -/
lemma represents_push_x_bound (x : ℚ) (n : ℕ) (_hx_nn : 0 ≤ x) (hx_lt : x < 1 / 10 ^ 16)
    (hn : n ≤ 9) :
    x / 10 + (n : ℚ) / 10 ^ 17 < 1 / 10 ^ 16 := by
  have hn_q : (n : ℚ) ≤ 9 := by exact_mod_cast hn
  have h16_pos : (0 : ℚ) < 10 ^ 16 := by positivity
  have h17_pos : (0 : ℚ) < 10 ^ 17 := by positivity
  have hx16 : x * 10 ^ 16 < 1 := (lt_div_iff₀ h16_pos).mp hx_lt
  have key : x * 10 ^ 16 + (n : ℚ) < 10 := by linarith
  rw [rat_lhs_reshape, rat_inv_16_eq]
  exact (div_lt_div_iff_of_pos_right h17_pos).mpr key

/-- ℚ-level form of `decimalValue_push`: compatible with field arithmetic. -/
lemma decimalValue_push_rat (g : Guard) (d : UInt64) (hd : d.toNat < 10) :
    (10 : ℚ) * (decimalValue (g.push d).digits_ : ℚ) + (nibble g.digits_ 0 : ℚ)
      = (decimalValue g.digits_ : ℚ) + (d.toNat : ℚ) * 10 ^ 16 := by
  have h := decimalValue_push g d
  have d_eq : d.toNat % 16 = d.toNat := Nat.mod_eq_of_lt (by omega)
  have hcast : ((10 * decimalValue (g.push d).digits_ + nibble g.digits_ 0 : ℕ) : ℚ)
       = ((decimalValue g.digits_ + (d.toNat % 16) * 10 ^ 16 : ℕ) : ℚ) := by
    exact_mod_cast h
  push_cast at hcast
  rw [d_eq] at hcast
  linarith

/-- Main algebraic identity for `represents_push`. -/
lemma represents_push_formula {g : Guard} {x : ℚ} (d : UInt64) (hd : d.toNat < 10)
    (f : ℚ) (hf : f = (decimalValue g.digits_ : ℚ) / 10 ^ 16 + x) :
    (f + d.toNat) / 10
      = (decimalValue (g.push d).digits_ : ℚ) / 10 ^ 16
        + (x / 10 + (nibble g.digits_ 0 : ℚ) / 10 ^ 17) := by
  have dvq := decimalValue_push_rat g d hd
  rw [hf]
  linear_combination -dvq / 10 ^ 17

/-- xbit correctness for `represents_push`. -/
lemma represents_push_xbit {g : Guard} {x : ℚ} (d : UInt64)
    (hx_nn : 0 ≤ x) (hxbit : g.xbit_ = true ↔ x > 0) :
    (g.push d).xbit_ = true ↔ x / 10 + (nibble g.digits_ 0 : ℚ) / 10 ^ 17 > 0 := by
  rw [xbit_push]
  constructor
  · intro h
    rcases Bool.or_eq_true_iff.mp h with h | h
    · have hxpos := hxbit.mp h
      have hnn : (0 : ℚ) ≤ (nibble g.digits_ 0 : ℚ) / 10 ^ 17 := by positivity
      have : 0 < x / 10 := by positivity
      linarith
    · have hne_nib : nibble g.digits_ 0 ≠ 0 := of_decide_eq_true h
      have hnpos : 0 < nibble g.digits_ 0 := Nat.pos_of_ne_zero hne_nib
      have hq : (0 : ℚ) < (nibble g.digits_ 0 : ℚ) / 10 ^ 17 :=
        div_pos (by exact_mod_cast hnpos) (by positivity)
      have : (0 : ℚ) ≤ x / 10 := by positivity
      linarith
  · intro h
    by_contra hne
    have hor_false : (g.xbit_ || decide (nibble g.digits_ 0 ≠ 0)) = false := by
      cases heq : (g.xbit_ || decide (nibble g.digits_ 0 ≠ 0)) with
      | true => exact absurd heq hne
      | false => rfl
    have hxb : g.xbit_ = false := by
      rcases Bool.or_eq_false_iff.mp hor_false with ⟨h1, _⟩
      exact h1
    have hnz : ¬ (nibble g.digits_ 0 ≠ 0) := by
      have := (Bool.or_eq_false_iff.mp hor_false).2
      exact of_decide_eq_false this
    have hx0 : x = 0 := by
      have hnp : ¬ (x > 0) := fun hp =>
        Bool.false_ne_true (hxb ▸ hxbit.mpr hp)
      linarith
    have hnz' : nibble g.digits_ 0 = 0 := not_not.mp hnz
    rw [hx0, hnz'] at h
    norm_num at h

/-- `Guard.push` maps `f` to `(f + d)/10`, provided `d < 10`. -/
theorem represents_push {g : Guard} {f : ℚ} {d : UInt64}
    (hg : represents g f) (hd : d.toNat < 10) :
    represents (g.push d) ((f + d.toNat) / 10) := by
  obtain ⟨x, hx_nn, hx_lt, hf, hxbit, hall⟩ := hg
  have d_eq : d.toNat % 16 = d.toNat := Nat.mod_eq_of_lt (by omega)
  have hnib_le : nibble g.digits_ 0 ≤ 9 := hall ⟨0, by omega⟩
  refine ⟨x / 10 + (nibble g.digits_ 0 : ℚ) / 10 ^ 17, ?_, ?_, ?_, ?_, ?_⟩
  · positivity
  · exact represents_push_x_bound x _ hx_nn hx_lt hnib_le
  · exact represents_push_formula d hd f hf
  · exact represents_push_xbit d hx_nn hxbit
  · intro p
    by_cases hp : p.val < 15
    · rw [nibble_push_lt hp]
      exact hall ⟨p.val + 1, by omega⟩
    · have hp15 : p.val = 15 := by omega
      rw [hp15, nibble_push_top, d_eq]
      omega

/-! ### `Guard.pop` semantics

`pop` is the inverse-direction primitive of `push`: it returns the top
nibble (position 15) and shifts the remaining digits up by one nibble
(filling the bottom with zero). This block proves nibble-level identities,
the algebraic `decimalValue` relation, and a "loose" represents
counterpart — the precision of the hidden fraction `x` degrades by a
factor of 10 with each pop, so the bound widens from `1/10^16` to
`1/10^15`. -/

/-- Nat-level formula for the `pop`ped guard's `digits_`. -/
lemma toNat_pop_digits (g : Guard) :
    (g.pop.1.digits_).toNat = 16 * (g.digits_.toNat % 16 ^ 15) := by
  change ((g.digits_ <<< 4) &&& 0xFFFF_FFFF_FFFF_FFFF).toNat = _
  rw [UInt64.toNat_and, UInt64.toNat_shiftLeft]
  rw [show ((4 : UInt64).toNat) = 4 from rfl, show ((4 : Nat) % 64) = 4 from rfl,
      show ((0xFFFF_FFFF_FFFF_FFFF : UInt64).toNat) = 2 ^ 64 - 1 from rfl,
      Nat.shiftLeft_eq, show (2 : Nat) ^ 4 = 16 from rfl]
  rw [Nat.and_two_pow_sub_one_eq_mod, Nat.mod_mod]
  have h264 : (2 : Nat) ^ 64 = 16 * 16 ^ 15 := by norm_num
  rw [h264, Nat.mul_comm g.digits_.toNat 16, Nat.mul_mod_mul_left]

/-- After pop, the bottom nibble is always zero (shifted in from the LSB). -/
lemma nibble_pop_zero (g : Guard) : nibble (g.pop.1.digits_) 0 = 0 := by
  unfold nibble
  rw [toNat_pop_digits]
  simp [pow_zero, Nat.div_one]

/-- For `1 ≤ p ≤ 15`, the popped guard's nibble at position `p` equals
the original guard's nibble at position `p - 1`. -/
lemma nibble_pop_succ (g : Guard) {p : ℕ} (hp : p < 15) :
    nibble (g.pop.1.digits_) (p + 1) = nibble g.digits_ p := by
  unfold nibble
  rw [toNat_pop_digits]
  conv_lhs => rw [show (16 : ℕ) ^ (p + 1) = 16 * 16 ^ p from by ring]
  rw [← Nat.div_div_eq_div_mul]
  rw [Nat.mul_div_cancel_left _ (by decide : (0 : ℕ) < 16)]
  have hsplit : (16 : ℕ) ^ 15 = 16 ^ p * 16 ^ (15 - p) := by
    rw [← pow_add]; congr 1; omega
  rw [hsplit, Nat.mod_mul_right_div_self]
  obtain ⟨q, hq⟩ : ∃ q, 15 - p = q + 1 := ⟨14 - p, by omega⟩
  rw [hq, show (16 : ℕ) ^ (q + 1) = 16 * 16 ^ q from by ring, Nat.mod_mul_right_mod]

/-- `pop` preserves nibble-validity: all output nibbles are decimal digits. -/
lemma allNibblesAtMost9_pop (g : Guard) (h : allNibblesAtMost9 g.digits_) :
    allNibblesAtMost9 (g.pop.1.digits_) := by
  intro p
  by_cases hp : p.val = 0
  · rw [hp, nibble_pop_zero]; omega
  · obtain ⟨q, hq⟩ : ∃ q, p.val = q + 1 := ⟨p.val - 1, by omega⟩
    have hq_lt : q < 15 := by have := p.isLt; omega
    rw [hq, nibble_pop_succ g hq_lt]
    exact h ⟨q, by omega⟩

/-- Key algebraic identity for `pop`: pulling out the top nibble multiplies the
decimal-interpreted value by 10 and subtracts the popped digit at position 16. -/
lemma decimalValue_pop_eq (g : Guard) :
    decimalValue (g.pop.1.digits_) + nibble g.digits_ 15 * 10 ^ 16
      = 10 * decimalValue g.digits_ := by
  have lhs_split :
      decimalValue (g.pop.1.digits_)
        = ∑ p ∈ Finset.range 15,
            nibble (g.pop.1.digits_) (p + 1) * 10 ^ (p + 1) := by
    unfold decimalValue
    rw [Finset.sum_range_succ' (fun p => nibble (g.pop.1.digits_) p * 10 ^ p) 15]
    rw [nibble_pop_zero g]; ring
  have sum_eq :
      ∑ p ∈ Finset.range 15,
          nibble (g.pop.1.digits_) (p + 1) * 10 ^ (p + 1)
        = ∑ p ∈ Finset.range 15, nibble g.digits_ p * 10 ^ (p + 1) := by
    refine Finset.sum_congr rfl ?_
    intro p hp
    have hp' : p < 15 := Finset.mem_range.mp hp
    rw [nibble_pop_succ g hp']
  have factor10 :
      ∑ p ∈ Finset.range 15, nibble g.digits_ p * 10 ^ (p + 1)
        = 10 * ∑ p ∈ Finset.range 15, nibble g.digits_ p * 10 ^ p := by
    rw [Finset.mul_sum]
    refine Finset.sum_congr rfl ?_
    intro p _
    rw [pow_succ]; ring
  have rhs_split :
      decimalValue g.digits_
        = ∑ p ∈ Finset.range 15, nibble g.digits_ p * 10 ^ p
          + nibble g.digits_ 15 * 10 ^ 15 := by
    unfold decimalValue
    rw [Finset.sum_range_succ]
  rw [lhs_split, sum_eq, factor10, rhs_split]; ring

/-- ℚ-version of `decimalValue_pop_eq`, suitable for field arithmetic. -/
lemma decimalValue_pop_rat (g : Guard) :
    (decimalValue (g.pop.1.digits_) : ℚ)
      = 10 * (decimalValue g.digits_ : ℚ) - (nibble g.digits_ 15 : ℚ) * 10 ^ 16 := by
  have h := decimalValue_pop_eq g
  have hcast : ((decimalValue (g.pop.1.digits_) + nibble g.digits_ 15 * 10 ^ 16 : ℕ) : ℚ)
      = ((10 * decimalValue g.digits_ : ℕ) : ℚ) := by exact_mod_cast h
  push_cast at hcast
  linarith

/-- The popped digit equals the top nibble (position 15) of `g.digits_`.
(Inlined to keep the `pop` block self-contained — `nibble_15` is defined
later, so we expand the definition of `nibble` directly.) -/
lemma pop_digit_eq_nibble_15 (g : Guard) :
    g.pop.2.toNat = nibble g.digits_ 15 := by
  change ((g.digits_ >>> 60) &&& 0xF).toNat = _
  unfold nibble
  rw [UInt64.toNat_and]
  change g.digits_.toNat >>> 60 &&& 15 = g.digits_.toNat / 16 ^ 15 % 16
  rw [Nat.shiftRight_eq_div_pow]
  have h60 : (2 : ℕ) ^ 60 = 16 ^ 15 := by decide
  rw [h60]
  -- LHS: g.digits_.toNat / 16^15 &&& 15
  -- Replace `15` by `2^4 - 1` on LHS only (via conv_lhs):
  conv_lhs => rw [show (15 : ℕ) = 2 ^ 4 - 1 from by decide]
  rw [Nat.and_two_pow_sub_one_eq_mod]
  -- Goal: g.digits_.toNat / 16^15 % 2^4 = g.digits_.toNat / 16^15 % 16
  congr 1

/-- `Guard.pop` semantics, "loose" form. If `g` represents `f`, then the
popped guard `g'` represents the value `10 * f - d.toNat` with a precision
loss: the hidden fraction `x'` is bounded by `1/10^15` instead of `1/10^16`,
because one decimal digit has been pulled out of the captured-digit window.

Like `represents_push`, this returns the full "represents" structure (modulo
the looser `x` bound), making it a drop-in algebraic primitive for the
`recover` loop of `Number.operator_add`'s different-sign branch. -/
theorem represents_pop {g : Guard} {f : ℚ} (hg : represents g f) :
    ∃ x' : ℚ, 0 ≤ x' ∧ x' < 1 / 10 ^ 15 ∧
      10 * f - (g.pop.2.toNat : ℚ)
        = (decimalValue g.pop.1.digits_ : ℚ) / 10 ^ 16 + x' ∧
      (g.pop.1.xbit_ = true ↔ x' > 0) ∧
      allNibblesAtMost9 g.pop.1.digits_ := by
  obtain ⟨x, hx_nn, hx_lt, hf, hxbit, hall⟩ := hg
  refine ⟨10 * x, by linarith, ?_, ?_, ?_, allNibblesAtMost9_pop g hall⟩
  · -- 10 * x < 1 / 10^15
    have heq : (10 : ℚ) * (1 / 10 ^ 16) = 1 / 10 ^ 15 := by
      rw [show (10 : ℚ) ^ 16 = 10 ^ 15 * 10 from by ring]
      field_simp
    have : (10 : ℚ) * x < 10 * (1 / 10 ^ 16) := by linarith
    linarith
  · -- value equation
    have hd_nat : g.pop.2.toNat = nibble g.digits_ 15 := pop_digit_eq_nibble_15 g
    have hd_q : ((g.pop.2.toNat : ℕ) : ℚ) = (nibble g.digits_ 15 : ℚ) := by
      exact_mod_cast hd_nat
    rw [hd_q, hf, decimalValue_pop_rat g]
    field_simp
    ring
  · -- xbit_ correctness: g.pop.1.xbit_ = g.xbit_
    have hpop_xbit : g.pop.1.xbit_ = g.xbit_ := by rfl
    rw [hpop_xbit, hxbit]
    constructor
    · intro hxpos; linarith
    · intro h10pos
      by_contra hxnp
      push_neg at hxnp
      have hx0 : x = 0 := le_antisymm hxnp hx_nn
      rw [hx0] at h10pos
      linarith

/-- Refinement of `represents_pop` when the input guard's `xbit_` is `false`.

When `g.xbit_ = false`, the input hidden fraction `x` is forced to `0` (by
the `xbit_ ↔ x > 0` clause of `represents`), so the popped value
`10 * f - d.toNat` has hidden fraction `0` as well. This means the popped
guard `g.pop.1` genuinely *represents* `10 * f - d.toNat` in the full
`represents` sense (hidden fraction in `[0, 1/10^16)`), not just the looser
`1/10^15` bound of `represents_pop`. -/
theorem represents_pop_of_xbit_false {g : Guard} {f : ℚ}
    (hg : represents g f) (hx : g.xbit_ = false) :
    represents g.pop.1 (10 * f - (g.pop.2.toNat : ℚ)) := by
  obtain ⟨x, hx_nn, _, hf, hxbit, hall⟩ := hg
  -- xbit_ = false forces x = 0
  have hx0 : x = 0 := by
    by_contra hxne
    have hxpos : x > 0 := lt_of_le_of_ne hx_nn (Ne.symm hxne)
    have hxbit_true : g.xbit_ = true := hxbit.mpr hxpos
    rw [hx] at hxbit_true
    exact absurd hxbit_true (by decide)
  refine ⟨0, le_refl 0, by positivity, ?_, ?_, allNibblesAtMost9_pop g hall⟩
  · -- value equation with hidden fraction 0
    have hd_nat : g.pop.2.toNat = nibble g.digits_ 15 := pop_digit_eq_nibble_15 g
    have hd_q : ((g.pop.2.toNat : ℕ) : ℚ) = (nibble g.digits_ 15 : ℚ) := by
      exact_mod_cast hd_nat
    rw [hd_q, hf, hx0, decimalValue_pop_rat g]
    field_simp
    ring
  · -- xbit iff: g.pop.1.xbit_ = g.xbit_ = false, RHS (0 > 0) false
    have hpop_xbit : g.pop.1.xbit_ = g.xbit_ := by rfl
    rw [hpop_xbit, hx]
    constructor
    · intro h; exact absurd h (by decide)
    · intro h; exact absurd h (lt_irrefl 0)

/-! ### Hex-vs-decimal helpers for `round_correct` -/

/-- The magic constant `0x5000_0000_0000_0000.toNat = 5 * 16^15`. -/
lemma toNat_half_mark : (0x5000_0000_0000_0000 : UInt64).toNat = 5 * 16 ^ 15 := rfl

/-! ### Hex-vs-decimal equivalence (nibble-level) -/

/-- The top nibble of `d` equals `d.toNat / 16^15`. -/
lemma nibble_15 (d : UInt64) : nibble d 15 = d.toNat / 16 ^ 15 := by
  unfold nibble
  have h15 : (16 : Nat) ^ 15 < 2 ^ 64 := by decide
  have hd := d.toNat_lt
  omega

/-- Decomposition: `d.toNat = nibble d 15 * 16^15 + lower`, with `lower < 16^15`. -/
lemma toNat_decomp (d : UInt64) :
    d.toNat = nibble d 15 * 16 ^ 15 + d.toNat % 16 ^ 15 ∧ d.toNat % 16 ^ 15 < 16 ^ 15 := by
  refine ⟨?_, Nat.mod_lt _ (by positivity)⟩
  rw [nibble_15]; omega

/-- Decomposition: `decimalValue d = nibble d 15 * 10^15 + lowerDec`,
    with `lowerDec < 10^15` under nibble validity. -/
lemma decimalValue_decomp (d : UInt64) (hall : allNibblesAtMost9 d) :
    decimalValue d
      = nibble d 15 * 10 ^ 15 + ∑ p ∈ Finset.range 15, nibble d p * 10 ^ p
    ∧ ∑ p ∈ Finset.range 15, nibble d p * 10 ^ p < 10 ^ 15 := by
  refine ⟨?_, ?_⟩
  · unfold decimalValue
    rw [Finset.sum_range_succ]; ring
  · -- Each nibble ≤ 9, sum ≤ 9 * sum_{p<15} 10^p = 10^15 - 1.
    calc ∑ p ∈ Finset.range 15, nibble d p * 10 ^ p
        ≤ ∑ p ∈ Finset.range 15, 9 * 10 ^ p := by
          apply Finset.sum_le_sum
          intro p hp
          have : nibble d p ≤ 9 := hall ⟨p, by have := Finset.mem_range.mp hp; omega⟩
          exact Nat.mul_le_mul_right _ this
      _ = 9 * ∑ p ∈ Finset.range 15, 10 ^ p := by rw [Finset.mul_sum]
      _ < 10 ^ 15 := by decide

/-- Auxiliary: `d.toNat % 16^k = 0 ↔ ∀ p < k, nibble d p = 0`. -/
lemma mod_pow_16_zero_iff (d : UInt64) : ∀ k : ℕ,
    d.toNat % 16 ^ k = 0 ↔ ∀ p < k, nibble d p = 0
  | 0 => by
    refine ⟨fun _ p hp => absurd hp (Nat.not_lt_zero _), fun _ => ?_⟩
    simp [Nat.mod_one]
  | k + 1 => by
    constructor
    · intro h p hp
      have hdvd : 16 ^ (k + 1) ∣ d.toNat := Nat.dvd_of_mod_eq_zero h
      have hpk : p + 1 ≤ k + 1 := by omega
      have : 16 ^ (p + 1) ∣ 16 ^ (k + 1) := pow_dvd_pow 16 hpk
      have hdvd' : 16 ^ (p + 1) ∣ d.toNat := this.trans hdvd
      unfold nibble
      have h16p : (16 : ℕ) ^ p ∣ d.toNat := (pow_dvd_pow 16 (by omega)).trans hdvd'
      have : d.toNat / 16 ^ p * 16 ^ p = d.toNat := Nat.div_mul_cancel h16p
      have h16 : 16 ∣ d.toNat / 16 ^ p := by
        obtain ⟨q, hq⟩ := hdvd'
        have h16p_pos : 0 < (16 : ℕ) ^ p := by positivity
        have : d.toNat / 16 ^ p = q * 16 := by
          rw [hq, pow_succ]
          rw [show (16 : ℕ) ^ p * 16 * q = (q * 16) * 16 ^ p from by ring]
          exact Nat.mul_div_cancel _ h16p_pos
        rw [this]; exact ⟨q, by ring⟩
      exact Nat.mod_eq_zero_of_dvd h16
    · intro h
      have ih := (mod_pow_16_zero_iff d k).mpr (fun p hp => h p (by omega))
      have hk0 : 16 ^ k ∣ d.toNat := Nat.dvd_of_mod_eq_zero ih
      have hnk : nibble d k = 0 := h k (by omega)
      have hn_dvd : 16 ∣ d.toNat / 16 ^ k := Nat.dvd_of_mod_eq_zero hnk
      have : 16 ^ (k + 1) ∣ d.toNat := by
        rw [pow_succ]
        obtain ⟨q, hq⟩ := hn_dvd
        have : d.toNat = d.toNat / 16 ^ k * 16 ^ k := (Nat.div_mul_cancel hk0).symm
        rw [this, hq]
        exact ⟨q, by ring⟩
      exact Nat.mod_eq_zero_of_dvd this

/-- A nonzero packed-digit word has strictly positive `decimalValue`. -/
lemma decimalValue_pos_of_ne_zero (d : UInt64) (hd : d ≠ 0) : 0 < decimalValue d := by
  rw [Nat.pos_iff_ne_zero]
  intro h
  apply hd
  have hbits : ∀ p ∈ Finset.range 16, nibble d p = 0 := by
    have hz := (Finset.sum_eq_zero_iff
      (s := Finset.range 16) (f := fun p => nibble d p * 10 ^ p)).mp h
    intro p hp
    have := hz p hp
    simpa [Nat.mul_eq_zero, Nat.pow_eq_zero] using this
  have hbits' : ∀ p < 16, nibble d p = 0 := by
    intro p hp; exact hbits p (Finset.mem_range.mpr hp)
  have hmod : d.toNat % 16 ^ 16 = 0 := (mod_pow_16_zero_iff d 16).mpr hbits'
  have hlt : d.toNat < 16 ^ 16 := by
    have hsz := UInt64.toNat_lt_size d
    have h216 : (16 : ℕ) ^ 16 = 2 ^ 64 := by norm_num
    rw [h216]; simpa using hsz
  rw [Nat.mod_eq_of_lt hlt] at hmod
  exact UInt64.toNat_inj.mp (by rw [hmod]; rfl)

/-- `d.toNat % 16^15 = 0 ↔ decimal lower sum = 0`. -/
lemma lower_zero_iff (d : UInt64) :
    d.toNat % 16 ^ 15 = 0 ↔ ∑ p ∈ Finset.range 15, nibble d p * 10 ^ p = 0 := by
  rw [mod_pow_16_zero_iff]
  rw [Finset.sum_eq_zero_iff_of_nonneg (fun _ _ => Nat.zero_le _)]
  constructor
  · intro h p hp
    exact Nat.mul_eq_zero.mpr (Or.inl (h p (Finset.mem_range.mp hp)))
  · intro h p hp
    have := h p (Finset.mem_range.mpr hp)
    have h10 : (10 : ℕ) ^ p > 0 := by positivity
    rcases Nat.mul_eq_zero.mp this with hnp | hzero
    · exact hnp
    · omega

/-- Under nibble-validity, hex `>` matches decimal `>` for the `5*base^15` threshold. -/
lemma hex_gt_iff_dec_gt (d : UInt64) (hall : allNibblesAtMost9 d) :
    d > 0x5000_0000_0000_0000 ↔ decimalValue d > 5 * 10 ^ 15 := by
  change (0x5000_0000_0000_0000 : UInt64).toNat < d.toNat ↔ _
  rw [toNat_half_mark]
  obtain ⟨heq, hlt⟩ := toNat_decomp d
  obtain ⟨heq', hlt'⟩ := decimalValue_decomp d hall
  rw [heq, heq']
  have hn15 : nibble d 15 ≤ 9 := hall ⟨15, by omega⟩
  rcases lt_or_ge (nibble d 15) 5 with h | h
  · have h1 : ¬ (5 * 16 ^ 15 < nibble d 15 * 16 ^ 15 + d.toNat % 16 ^ 15) := by
      have : nibble d 15 ≤ 4 := by omega
      have : nibble d 15 * 16 ^ 15 ≤ 4 * 16 ^ 15 :=
        Nat.mul_le_mul_right _ this
      have h16 : (16 : Nat) ^ 15 ≥ 1 := Nat.one_le_iff_ne_zero.mpr (by positivity)
      omega
    have h2 : ¬ (5 * 10 ^ 15 < nibble d 15 * 10 ^ 15
                  + ∑ p ∈ Finset.range 15, nibble d p * 10 ^ p) := by
      have : nibble d 15 ≤ 4 := by omega
      have : nibble d 15 * 10 ^ 15 ≤ 4 * 10 ^ 15 := Nat.mul_le_mul_right _ this
      omega
    exact ⟨fun h => absurd h h1, fun h => absurd h h2⟩
  rcases eq_or_lt_of_le h with h | h
  · rw [← h]
    have lzi := lower_zero_iff d
    constructor
    · intro hgt
      have : d.toNat % 16 ^ 15 > 0 := by omega
      have : d.toNat % 16 ^ 15 ≠ 0 := Nat.pos_iff_ne_zero.mp this
      have : ∑ p ∈ Finset.range 15, nibble d p * 10 ^ p ≠ 0 :=
        fun h => this (lzi.mpr h)
      omega
    · intro hgt
      have : ∑ p ∈ Finset.range 15, nibble d p * 10 ^ p > 0 := by omega
      have : ∑ p ∈ Finset.range 15, nibble d p * 10 ^ p ≠ 0 := Nat.pos_iff_ne_zero.mp this
      have : d.toNat % 16 ^ 15 ≠ 0 := fun h => this (lzi.mp h)
      omega
  · have h1 : 5 * 16 ^ 15 < nibble d 15 * 16 ^ 15 + d.toNat % 16 ^ 15 := by
      have : 6 ≤ nibble d 15 := h
      have : 6 * 16 ^ 15 ≤ nibble d 15 * 16 ^ 15 := Nat.mul_le_mul_right _ this
      have h16 : 16 ^ 15 > 0 := by positivity
      omega
    have h2 : 5 * 10 ^ 15 < nibble d 15 * 10 ^ 15
                 + ∑ p ∈ Finset.range 15, nibble d p * 10 ^ p := by
      have : 6 ≤ nibble d 15 := h
      have : 6 * 10 ^ 15 ≤ nibble d 15 * 10 ^ 15 := Nat.mul_le_mul_right _ this
      have h10 : (10 : Nat) ^ 15 > 0 := by positivity
      omega
    exact ⟨fun _ => h2, fun _ => h1⟩

/-- Under nibble-validity, hex `<` matches decimal `<` for the `5*base^15` threshold. -/
lemma hex_lt_iff_dec_lt (d : UInt64) (hall : allNibblesAtMost9 d) :
    d < 0x5000_0000_0000_0000 ↔ decimalValue d < 5 * 10 ^ 15 := by
  rw [UInt64.lt_iff_toNat_lt, toNat_half_mark]
  obtain ⟨heq, hlt⟩ := toNat_decomp d
  obtain ⟨heq', hlt'⟩ := decimalValue_decomp d hall
  rw [heq, heq']
  have hn15 : nibble d 15 ≤ 9 := hall ⟨15, by omega⟩
  rcases lt_or_ge (nibble d 15) 5 with h | h
  · have h1 : nibble d 15 * 16 ^ 15 + d.toNat % 16 ^ 15 < 5 * 16 ^ 15 := by
      have : nibble d 15 ≤ 4 := by omega
      have : nibble d 15 * 16 ^ 15 ≤ 4 * 16 ^ 15 := Nat.mul_le_mul_right _ this
      have h16 : 16 ^ 15 > 0 := by positivity
      omega
    have h2 : nibble d 15 * 10 ^ 15
             + ∑ p ∈ Finset.range 15, nibble d p * 10 ^ p < 5 * 10 ^ 15 := by
      have : nibble d 15 ≤ 4 := by omega
      have : nibble d 15 * 10 ^ 15 ≤ 4 * 10 ^ 15 := Nat.mul_le_mul_right _ this
      omega
    exact ⟨fun _ => h2, fun _ => h1⟩
  · have h1 : ¬ (nibble d 15 * 16 ^ 15 + d.toNat % 16 ^ 15 < 5 * 16 ^ 15) := by
      have : 5 * 16 ^ 15 ≤ nibble d 15 * 16 ^ 15 := Nat.mul_le_mul_right _ h
      omega
    have h2 : ¬ (nibble d 15 * 10 ^ 15
                 + ∑ p ∈ Finset.range 15, nibble d p * 10 ^ p < 5 * 10 ^ 15) := by
      have : 5 * 10 ^ 15 ≤ nibble d 15 * 10 ^ 15 := Nat.mul_le_mul_right _ h
      omega
    exact ⟨fun h => absurd h h1, fun h => absurd h h2⟩

/-- Under nibble-validity, `d = 0x5000…` iff `decimalValue d = 5 * 10^15`. -/
lemma hex_eq_iff_dec_eq (d : UInt64) (hall : allNibblesAtMost9 d) :
    ¬ (d > 0x5000_0000_0000_0000) ∧ ¬ (d < 0x5000_0000_0000_0000)
      ↔ decimalValue d = 5 * 10 ^ 15 := by
  rw [hex_gt_iff_dec_gt d hall, hex_lt_iff_dec_lt d hall]
  omega

/-- `Guard.round .to_nearest` simplifies to a nested `if` on `digits_` and `xbit_`,
when the guard is non-empty (the `g.empty → -2` early-exit is ruled out). -/
lemma round_to_nearest_def {g : Guard} (hne : ¬ g.empty) :
    g.round .to_nearest =
      (if g.digits_ > 0x5000_0000_0000_0000 then 1
       else if g.digits_ < 0x5000_0000_0000_0000 then -1
       else if g.xbit_ then 1 else 0) := by
  unfold Guard.round
  rw [if_neg hne]

/-- `Guard.round` in `to_nearest` mode agrees with the rational-level oracle.
Requires `¬ g.empty`; an empty guard returns the exact-sentinel `-2` (and `f = 0`). -/
theorem round_correct {g : Guard} {f : ℚ} (hne : ¬ g.empty) (hg : represents g f) :
    (g.round .to_nearest = 1 ↔ f > 1 / 2) ∧
    (g.round .to_nearest = -1 ↔ f < 1 / 2) ∧
    (g.round .to_nearest = 0 ↔ f = 1 / 2) := by
  obtain ⟨x, hx_nn, hx_lt, hf, hxbit, hall⟩ := hg
  have h16_pos : (0 : ℚ) < 10 ^ 16 := by positivity
  have key : f - 1 / 2 = ((decimalValue g.digits_ : ℚ) - 5 * 10 ^ 15) / 10 ^ 16 + x := by
    rw [hf]
    field_simp
    linarith
  rw [round_to_nearest_def hne]
  by_cases hgt : g.digits_ > 0x5000_0000_0000_0000
  · rw [if_pos hgt]
    have hdec := (hex_gt_iff_dec_gt g.digits_ hall).mp hgt
    have hq : (decimalValue g.digits_ : ℚ) > 5 * 10 ^ 15 := by exact_mod_cast hdec
    have hpos : ((decimalValue g.digits_ : ℚ) - 5 * 10 ^ 15) / 10 ^ 16 > 0 :=
      div_pos (by linarith) h16_pos
    have fpos : f > 1 / 2 := by linarith [key, hx_nn]
    refine ⟨⟨fun _ => fpos, fun _ => rfl⟩, ?_, ?_⟩
    · exact ⟨fun h => absurd h (by decide), fun h => absurd (lt_asymm h) (not_not.mpr fpos)⟩
    · exact ⟨fun h => absurd h (by decide), fun h => absurd fpos (by rw [h]; exact lt_irrefl _)⟩
  · rw [if_neg hgt]
    by_cases hlt : g.digits_ < 0x5000_0000_0000_0000
    · rw [if_pos hlt]
      have hdec := (hex_lt_iff_dec_lt g.digits_ hall).mp hlt
      have hq' : (decimalValue g.digits_ : ℚ) ≤ 5 * 10 ^ 15 - 1 := by
        have h_nat : decimalValue g.digits_ + 1 ≤ 5 * 10 ^ 15 := hdec
        have h1 : ((decimalValue g.digits_ + 1 : ℕ) : ℚ) ≤ ((5 * 10 ^ 15 : ℕ) : ℚ) := by
          exact_mod_cast h_nat
        push_cast at h1
        clear hgt hlt
        linarith
      have hbound : ((decimalValue g.digits_ : ℚ) - 5 * 10 ^ 15) / 10 ^ 16 ≤ -1 / 10 ^ 16 := by
        rw [div_le_iff₀ h16_pos]
        clear hgt hlt
        linarith
      have fneg : f < 1 / 2 := by
        clear hgt hlt
        linarith [key, hx_lt]
      refine ⟨?_, ⟨fun _ => fneg, fun _ => rfl⟩, ?_⟩
      · exact ⟨fun h => absurd h (by decide), fun h => absurd (lt_asymm h) (not_not.mpr fneg)⟩
      · exact ⟨fun h => absurd h (by decide), fun h => absurd fneg (by rw [h]; exact lt_irrefl _)⟩
    · rw [if_neg hlt]
      have hne : decimalValue g.digits_ = 5 * 10 ^ 15 :=
        (hex_eq_iff_dec_eq g.digits_ hall).mp ⟨hgt, hlt⟩
      have hneq : ((decimalValue g.digits_ : ℚ) - 5 * 10 ^ 15) / 10 ^ 16 = 0 := by
        have : (decimalValue g.digits_ : ℚ) = 5 * 10 ^ 15 := by exact_mod_cast hne
        rw [this]; simp
      by_cases hxb : g.xbit_ = true
      · rw [if_pos hxb]
        have hxp : x > 0 := hxbit.mp hxb
        have fpos : f > 1 / 2 := by clear hgt hlt; linarith [key]
        refine ⟨⟨fun _ => fpos, fun _ => rfl⟩, ?_, ?_⟩
        · exact ⟨fun h => absurd h (by decide),
                 fun h => absurd (lt_asymm h) (not_not.mpr fpos)⟩
        · exact ⟨fun h => absurd h (by decide),
                 fun h => absurd fpos (by rw [h]; exact lt_irrefl _)⟩
      · rw [if_neg hxb]
        have hxb' : g.xbit_ = false := Bool.bool_iff_false.mp hxb
        have hx0 : x = 0 := by
          have hnp : ¬ (x > 0) := fun hp => by
            have := hxbit.mpr hp
            rw [hxb'] at this
            exact Bool.false_ne_true this
          clear hgt hlt
          linarith
        have feq : f = 1 / 2 := by clear hgt hlt; linarith [key]
        refine ⟨?_, ?_, ⟨fun _ => feq, fun _ => rfl⟩⟩
        · exact ⟨fun h => absurd h (by decide),
                 fun h => absurd feq (by intro hh; rw [hh] at h; exact lt_irrefl _ h)⟩
        · exact ⟨fun h => absurd h (by decide),
                 fun h => absurd feq (by intro hh; rw [hh] at h; exact lt_irrefl _ h)⟩

/-! ## `round_correct` corollaries that derive `¬ g.empty` from the round value / `f`. -/

/-- A `round = -2` ⟺ empty bridge used to discharge the non-emptiness side condition. -/
private lemma round_neg_two_iff_empty (g : Guard) : g.round .to_nearest = -2 ↔ g.empty = true := by
  constructor
  · intro h; by_contra he; rw [round_to_nearest_def he] at h
    revert h; split_ifs <;> decide
  · intro he; unfold Guard.round; rw [if_pos he]

/-- `round = 1` (which forces the guard non-empty) characterizes `f > 1/2`. -/
lemma represents_round_eq_one {g : Guard} {f : ℚ} (hg : represents g f)
    (h : g.round .to_nearest = 1) : f > 1 / 2 := by
  have hne : ¬ g.empty := by
    intro he; rw [(round_neg_two_iff_empty g).mpr he] at h; exact absurd h (by decide)
  exact (round_correct hne hg).1.mp h

/-- `round = 0` (which forces the guard non-empty) characterizes `f = 1/2`. -/
lemma represents_round_eq_zero {g : Guard} {f : ℚ} (hg : represents g f)
    (h : g.round .to_nearest = 0) : f = 1 / 2 := by
  have hne : ¬ g.empty := by
    intro he; rw [(round_neg_two_iff_empty g).mpr he] at h; exact absurd h (by decide)
  exact (round_correct hne hg).2.2.mp h

/-- `f > 1/2` forces the guard non-empty (else `f = 0`), giving `round = 1`. -/
lemma represents_f_gt_half {g : Guard} {f : ℚ} (hg : represents g f)
    (h : f > 1 / 2) : g.round .to_nearest = 1 := by
  have hne : ¬ g.empty := by
    intro he
    obtain ⟨hd, hx⟩ : g.digits_ = 0 ∧ g.xbit_ = false := by
      have hh : (g.digits_ == 0 && !g.xbit_) = true := he
      rw [Bool.and_eq_true] at hh; exact ⟨beq_iff_eq.mp hh.1, by simpa using hh.2⟩
    have hf0 : f = 0 := represents_eq_zero_of_digits_zero_xbit_false hd hx hg
    rw [hf0] at h; norm_num at h
  exact (round_correct hne hg).1.mpr h

end XRPL.Model.Protocol
