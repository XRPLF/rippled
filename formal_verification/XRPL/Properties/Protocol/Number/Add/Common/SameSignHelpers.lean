import XRPL.Properties.Protocol.Number.Common.Notation
import XRPL.Properties.Protocol.Number.Common.ToRatLemmas
import XRPL.Properties.Protocol.Number.Common.Rounding.Guard
import XRPL.Properties.Protocol.Number.Add.Common.AlignDown

set_option linter.style.longLine false
set_option linter.style.emptyLine false

namespace XRPL.Model.Protocol

lemma abs_add_eq_of_same_sign {x y : Number} (h : x.negative_ = y.negative_) :
    |x.toRat + y.toRat| = |x.toRat| + |y.toRat| := by
  cases hxn : x.negative_
  · have hyn : y.negative_ = false := h ▸ hxn
    have hx_nn : 0 ≤ x.toRat := Number.toRat_nonneg_of_nonnegative x hxn
    have hy_nn : 0 ≤ y.toRat := Number.toRat_nonneg_of_nonnegative y hyn
    rw [abs_of_nonneg (add_nonneg hx_nn hy_nn), abs_of_nonneg hx_nn, abs_of_nonneg hy_nn]
  · have hyn : y.negative_ = true := h ▸ hxn
    have hx_np : x.toRat ≤ 0 := Number.toRat_nonpos_of_negative x hxn
    have hy_np : y.toRat ≤ 0 := Number.toRat_nonpos_of_negative y hyn
    rw [abs_of_nonpos (add_nonpos hx_np hy_np), abs_of_nonpos hx_np, abs_of_nonpos hy_np]
    ring

lemma represents_initial_guard_eq (xn : Bool) :
    represents (if xn then Guard.new.set_negative else Guard.new) 0 := by
  by_cases hxn : xn
  · rw [if_pos hxn]
    obtain ⟨x_rep, hx_nn, hx_lt, hf_eq, hxbit, hall⟩ := represents_new
    refine ⟨x_rep, hx_nn, hx_lt, ?_, ?_, ?_⟩
    · show (0 : ℚ) = _
      have : Guard.new.set_negative.digits_ = Guard.new.digits_ := rfl
      rw [this]; exact hf_eq
    · have hxbit_eq : Guard.new.set_negative.xbit_ = Guard.new.xbit_ := rfl
      rw [hxbit_eq]; exact hxbit
    · have : Guard.new.set_negative.digits_ = Guard.new.digits_ := rfl
      rw [this]; exact hall
  · rw [if_neg hxn]; exact represents_new

lemma alignDown_abs_value
    (n : Number) (g₀ : Guard) (target : Int) (h_le : n.exponent_ ≤ target)
    (f₀ : ℚ) :
    let r := Number.operator_add.alignDown n.mantissa_ n.exponent_ g₀ target
    let f' : ℚ := (f₀ + ((n.mantissa_.toNat % 10 ^ (target - n.exponent_).toNat : ℕ) : ℚ))
                    / 10 ^ (target - n.exponent_).toNat
    |n.toRat| + f₀ * 10 ^ n.exponent_
      = ((r.1.toNat : ℚ) + f') * 10 ^ target := by
  simp only
  have h_abs := abs_toRat_eq n
  rw [h_abs]
  have hmax : max n.exponent_ target = target := max_eq_right h_le
  have h_me : (Number.operator_add.alignDown n.mantissa_ n.exponent_ g₀ target).1.toNat
      = n.mantissa_.toNat / 10 ^ (max n.exponent_ target - n.exponent_).toNat :=
    alignDown_mantissa_eq n.mantissa_ n.exponent_ g₀ target
  rw [hmax] at h_me
  rw [h_me]
  set K : ℕ := (target - n.exponent_).toNat with hK_def
  have hK_eq : (target : ℤ) - n.exponent_ = (K : ℤ) := by
    rw [hK_def]; exact (Int.toNat_of_nonneg (by linarith)).symm
  have h10K_pos : (0 : ℚ) < 10 ^ K := by positivity
  have h10K_ne : (10 : ℚ) ^ K ≠ 0 := ne_of_gt h10K_pos
  have h_pow_split : (10 : ℚ) ^ target = 10 ^ n.exponent_ * 10 ^ K := by
    have : (target : ℤ) = n.exponent_ + K := by linarith
    rw [this]
    rw [zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0), zpow_natCast]
  rw [h_pow_split]
  have hsplit_q : (n.mantissa_.toNat : ℚ) = ((n.mantissa_.toNat / 10 ^ K : ℕ) : ℚ) * (10 ^ K : ℚ) + ((n.mantissa_.toNat % 10 ^ K : ℕ) : ℚ) := by
    have hsplit : n.mantissa_.toNat = (n.mantissa_.toNat / 10 ^ K) * 10 ^ K + n.mantissa_.toNat % 10 ^ K := by
      have := Nat.div_add_mod n.mantissa_.toNat (10 ^ K); linarith
    have : ((n.mantissa_.toNat : ℕ) : ℚ) = (((n.mantissa_.toNat / 10 ^ K) * 10 ^ K + n.mantissa_.toNat % 10 ^ K : ℕ) : ℚ) := by
      exact_mod_cast hsplit
    push_cast at this; linarith
  rw [hsplit_q]
  field_simp
  ring

lemma alignDown_sbit_preserved (m : UInt64) (e : Int) (g : Guard) (target : Int) :
    (Number.operator_add.alignDown m e g target).2.2.sbit_ = g.sbit_ := by
  induction m, e, g using Number.operator_add.alignDown.induct target with
  | case1 m e g hlt IH =>
    simp only [Guard.doDropDigit] at IH
    rw [alignDown_step hlt]
    rw [IH]
    rfl
  | case2 m e g hnlt =>
    rw [alignDown_noop hnlt]

end XRPL.Model.Protocol
