import XRPL.Properties.Protocol.STAmount.Common.RoundToScalePlumbing
import XRPL.Properties.Protocol.STAmount.Common.STAmountCommonProps

namespace XRPL.Model.Protocol

/-! ## `operator_neg` value and field invariants

`operator_neg` flips the sign flag (or is the identity on zero); it preserves the
asset, offset, and magnitude, and negates the exact value. These let the
subtraction theorems reuse the addition engine via `v1 - v2 = v1 + (-v2)`. -/

lemma STAmount.operator_neg_mAsset (s : STAmount) : s.operator_neg.mAsset = s.mAsset := by
  unfold STAmount.operator_neg; split <;> rfl

lemma STAmount.operator_neg_mOffset (s : STAmount) : s.operator_neg.mOffset = s.mOffset := by
  unfold STAmount.operator_neg; split <;> rfl

lemma STAmount.operator_neg_mValue (s : STAmount) : s.operator_neg.mValue = s.mValue := by
  unfold STAmount.operator_neg; split <;> rfl

/-- A zero-magnitude STAmount has value `0`. -/
lemma STAmount.toRat_eq_zero_of_mValue_zero (s : STAmount) (h : s.mValue = 0) : s.toRat = 0 := by
  have habs := STAmount.abs_toRat s
  rw [h] at habs
  simp only [UInt64.toNat_ofNat, Nat.zero_mod, Nat.cast_zero, zero_mul] at habs
  exact abs_eq_zero.mp habs

/-- `operator_neg` negates the exact value. -/
lemma STAmount.operator_neg_toRat (s : STAmount) : s.operator_neg.toRat = -s.toRat := by
  unfold STAmount.operator_neg
  split
  case isTrue h =>
    rw [beq_iff_eq] at h
    rw [STAmount.toRat_eq_zero_of_mValue_zero s h]; ring
  case isFalse h =>
    by_cases hn : s.mIsNegative = true
    · have h1 : ({s with mIsNegative := !s.mIsNegative} : STAmount).toRat
          = (s.mValue.toNat : ℚ) * 10 ^ s.mOffset := STAmount.toRat_of_nonneg _ (by simp [hn])
      have h2 : s.toRat = -((s.mValue.toNat : ℚ) * 10 ^ s.mOffset) := STAmount.toRat_of_neg s hn
      rw [h1, h2, neg_neg]
    · have hf : s.mIsNegative = false := by simpa using hn
      have h1 : ({s with mIsNegative := !s.mIsNegative} : STAmount).toRat
          = -((s.mValue.toNat : ℚ) * 10 ^ s.mOffset) := STAmount.toRat_of_neg _ (by simp [hf])
      have h2 : s.toRat = (s.mValue.toNat : ℚ) * 10 ^ s.mOffset := STAmount.toRat_of_nonneg s hf
      rw [h1, h2]

/-- A `NativeCanonical` amount stays `NativeCanonical` under negation. -/
lemma STAmount.NativeCanonical.operator_neg {s : STAmount} (h : s.NativeCanonical) :
    s.operator_neg.NativeCanonical where
  is_xrp := by rw [STAmount.operator_neg_mAsset]; exact h.is_xrp
  offset_zero := by rw [STAmount.operator_neg_mOffset]; exact h.offset_zero
  in_range := by rw [STAmount.operator_neg_mValue]; exact h.in_range

/-- A `MPTCanonical` amount stays `MPTCanonical` under negation. -/
lemma STAmount.MPTCanonical.operator_neg {s : STAmount} (h : s.MPTCanonical) :
    s.operator_neg.MPTCanonical where
  offset_zero := by rw [STAmount.operator_neg_mOffset]; exact h.offset_zero
  value_in_range := by rw [STAmount.operator_neg_mValue]; exact h.value_in_range
  zero_sign_cleared := by
    intro hmv
    rw [STAmount.operator_neg_mValue] at hmv
    have : s.operator_neg = s := by unfold STAmount.operator_neg; rw [if_pos (by rw [beq_iff_eq]; exact hmv)]
    rw [this]; exact h.zero_sign_cleared hmv

/-- An `IOUCanonical` amount stays `IOUCanonical` under negation (sign-flip only). -/
lemma STAmount.IOUCanonical.operator_neg {s : STAmount} (h : s.IOUCanonical) :
    s.operator_neg.IOUCanonical where
  iou_asset := by rw [STAmount.operator_neg_mAsset]; exact h.iou_asset
  not_xrp := by rw [STAmount.operator_neg_mAsset]; exact h.not_xrp
  mant_lo := by rw [STAmount.operator_neg_mValue]; exact h.mant_lo
  mant_hi := by rw [STAmount.operator_neg_mValue]; exact h.mant_hi
  exp_lo := by rw [STAmount.operator_neg_mOffset]; exact h.exp_lo
  exp_hi := by rw [STAmount.operator_neg_mOffset]; exact h.exp_hi

end XRPL.Model.Protocol
