import Mathlib.Tactic

import XRPL.Properties.Protocol.STAmount.Common.RoundToScalePlumbing


namespace XRPL.Model.Protocol

/-! # Operator-level helpers for the `roundToScale` discrete theorem

`ofIOUAmount` on in-range 16-digit IOUAmounts, `operator_neg`, and the value
forms of the canonical records. -/

/-- An in-range 16-digit IOUAmount. -/
structure IOUAmount.InRange16 (a : IOUAmount) : Prop where
  mant_lo : 10 ^ 15 ≤ a.mantissa_.toInt.natAbs
  mant_hi : a.mantissa_.toInt.natAbs < 10 ^ 16
  exp_lo : (-96 : ℤ) ≤ a.exponent_
  exp_hi : a.exponent_ ≤ 80

/-- `ofIOUAmount` packs an in-range 16-digit IOUAmount exactly. -/
theorem STAmount.ofIOUAmount_canonical (a : IOUAmount) (issue : Issue)
    (mode : rounding_mode)
    (h_not_xrp : (Asset.issue issue).isNative = false)
    (hr : a.InRange16) :
    STAmount.ofIOUAmount a issue mode
      = .ok ⟨.issue issue, a.mantissa_.toInt.natAbs.toUInt64, a.exponent_,
             decide (a.mantissa_ < 0)⟩ := by
  have h_fit : a.mantissa_.toInt.natAbs < 2 ^ 63 := by
    have := hr.mant_hi
    omega
  have h_mant_ne : a.mantissa_.toInt ≠ 0 := by
    intro h0
    have := hr.mant_lo
    rw [h0] at this
    simp at this
  set neg : Bool := decide (IOUAmount.signum a < 0) with hneg_def
  have h_neg_lt : neg = decide (a.mantissa_ < 0) := by
    rw [hneg_def]
    unfold IOUAmount.signum
    rcases h_sign : decide (a.mantissa_ < 0) with _ | _
    · have h_not : ¬ a.mantissa_ < 0 := of_decide_eq_false h_sign
      apply decide_eq_false
      rw [if_neg h_not]
      by_cases h_ne : a.mantissa_ != 0
      · rw [if_pos h_ne]
        norm_num
      · rw [if_neg h_ne]
        norm_num
    · have h_lt : a.mantissa_ < 0 := of_decide_eq_true h_sign
      apply decide_eq_true
      rw [if_pos h_lt]
      norm_num
  set absMant : Int64 := if IOUAmount.signum a < 0 then -a.mantissa_ else a.mantissa_
    with habsMant_def
  have h_abs_toInt : absMant.toInt = (a.mantissa_.toInt.natAbs : ℤ) := by
    rw [habsMant_def]
    by_cases h_lt : a.mantissa_ < 0
    · have h_lt' : a.mantissa_.toInt < 0 := by
        have h1 := Int64.lt_iff_toInt_lt.mp h_lt
        rwa [(by decide : (0 : Int64).toInt = 0)] at h1
      have h_sgn : IOUAmount.signum a < 0 := by
        unfold IOUAmount.signum
        rw [if_pos h_lt]
        norm_num
      rw [if_pos h_sgn, Int64.toInt_neg, Int.bmod_eq_iff (by norm_num)]
      have h_v := Int64.lt_iff_toInt_lt.mp h_lt
      rw [(by decide : (0 : Int64).toInt = 0)] at h_v
      have := hr.mant_hi
      constructor <;> omega
    · have h_v : 0 ≤ a.mantissa_.toInt := by
        by_contra h_c
        push_neg at h_c
        apply h_lt
        rw [Int64.lt_iff_toInt_lt, (by decide : (0 : Int64).toInt = 0)]
        exact h_c
      have h_sgn : ¬ IOUAmount.signum a < 0 := by
        unfold IOUAmount.signum
        rw [if_neg h_lt]
        by_cases h_ne : a.mantissa_ != 0
        · rw [if_pos h_ne]
          norm_num
        · rw [if_neg h_ne]
          norm_num
      rw [if_neg h_sgn]
      omega
  have h_abs_toNat : absMant.toUInt64.toNat = a.mantissa_.toInt.natAbs := by
    have h1 := toUInt64_toNat_of_nonneg absMant (by rw [h_abs_toInt]; positivity)
    rw [h_abs_toInt] at h1
    omega
  have h_abs_eq : absMant.toUInt64 = a.mantissa_.toInt.natAbs.toUInt64 := by
    rw [← UInt64.toNat_inj, h_abs_toNat]
    exact (UInt64.toNat_ofNat_of_lt' (by
      have hsz : UInt64.size = 18446744073709551616 := uint64_size_val
      omega)).symm
  have hc : STAmount.IOUCanonical
      ⟨.issue issue, absMant.toUInt64, a.exponent_, neg⟩ :=
    { iou_asset := rfl
      not_xrp := h_not_xrp
      mant_lo := by rw [show (⟨.issue issue, absMant.toUInt64, a.exponent_, neg⟩
          : STAmount).mValue = absMant.toUInt64 from rfl, h_abs_toNat]; exact hr.mant_lo
      mant_hi := by rw [show (⟨.issue issue, absMant.toUInt64, a.exponent_, neg⟩
          : STAmount).mValue = absMant.toUInt64 from rfl, h_abs_toNat]; exact hr.mant_hi
      exp_lo := hr.exp_lo
      exp_hi := hr.exp_hi }
  unfold STAmount.ofIOUAmount
  calc (STAmount.unchecked (.issue issue) absMant.toUInt64 a.exponent_ neg).canonicalize mode
      = .ok ⟨.issue issue, absMant.toUInt64, a.exponent_, neg⟩ :=
        STAmount.canonicalize_canonical_id _ mode hc
    _ = .ok ⟨.issue issue, a.mantissa_.toInt.natAbs.toUInt64, a.exponent_,
             decide (a.mantissa_ < 0)⟩ := by
        rw [h_abs_eq, h_neg_lt]

/-- Negation of a nonzero STAmount flips only the sign bit. -/
lemma STAmount.operator_neg_of_ne (s : STAmount) (h : s.mValue ≠ 0) :
    s.operator_neg = { s with mIsNegative := !s.mIsNegative } := by
  unfold STAmount.operator_neg
  rw [if_neg (by
    intro hb
    exact h (by exact_mod_cast beq_iff_eq.mp hb))]

/-- The canonical-record value, in signed form. -/
lemma STAmount.toRat_signed (s : STAmount) :
    s.toRat = (if s.mIsNegative then (-1 : ℚ) else 1) * (s.mValue.toNat : ℚ)
      * 10 ^ s.mOffset := by
  rcases h : s.mIsNegative with _ | _
  · rw [STAmount.toRat_of_nonneg s h]
    norm_num
  · rw [STAmount.toRat_of_neg s h, if_pos rfl]
    ring

/-- The 19-digit `Number` view of a canonical STAmount's IOU mantissa: the
exact `×1000` lift. -/
theorem STAmount.iou_toNumber_canonical (s : STAmount) (mode : rounding_mode)
    (hc : s.IOUCanonical) :
    (⟨s.signedDrops.toInt64, s.mOffset⟩ : IOUAmount).toNumber mode
      = .ok ⟨s.mIsNegative, s.mValue * 10 * 10 * 10, s.mOffset - 3⟩ := by
  have h_fit : s.mValue.toNat < 2 ^ 63 := by
    have := hc.mant_hi
    omega
  have h_sd_val : s.signedDrops.toInt64.toInt = s.signedDrops :=
    STAmount.signedDrops_toInt64_toInt s hc.mant_hi
  set sd : Int64 := s.signedDrops.toInt64 with hsd_def
  have h_sd_signed : sd.toInt = if s.mIsNegative then -(s.mValue.toNat : ℤ)
      else (s.mValue.toNat : ℤ) := by
    rw [h_sd_val]
    unfold STAmount.signedDrops
    rcases hneg : s.mIsNegative with _ | _ <;> simp
  have h_sd_abs : sd.toInt.natAbs = s.mValue.toNat := by
    rw [h_sd_signed]
    rcases hneg : s.mIsNegative with _ | _ <;> simp
  have h_neg₀ : decide (sd < 0) = s.mIsNegative := by
    rcases hneg : s.mIsNegative with _ | _
    · apply decide_eq_false
      rw [Int64.lt_iff_toInt_lt, h_sd_signed, hneg,
          (by decide : (0 : Int64).toInt = 0)]
      simp only [Bool.false_eq_true, if_false]
      omega
    · apply decide_eq_true
      rw [Int64.lt_iff_toInt_lt, h_sd_signed, hneg,
          (by decide : (0 : Int64).toInt = 0)]
      simp only [if_true]
      have : 0 < s.mValue.toNat := by have := hc.mant_lo; omega
      omega
  have h_mAbs_toNat : (sd.toInt.natAbs.toUInt64).toNat = s.mValue.toNat := by
    calc (sd.toInt.natAbs.toUInt64).toNat
        = sd.toInt.natAbs := UInt64.toNat_ofNat_of_lt' (by
          have hsz : UInt64.size = 18446744073709551616 := uint64_size_val
          omega)
      _ = s.mValue.toNat := h_sd_abs
  have h_mAbs : sd.toInt.natAbs.toUInt64 = s.mValue := UInt64.toNat_inj.mp h_mAbs_toNat
  show Number.from_rep sd s.mOffset largeRange.min largeRange.max mode = _
  unfold Number.from_rep Number.normalized Number.normalize Number.unchecked
  have h_red := doNormalize_large_16digit (decide (sd < 0)) (sd.toInt.natAbs.toUInt64)
    s.mOffset mode
    (by rw [h_mAbs_toNat]; exact hc.mant_lo)
    (by rw [h_mAbs_toNat]; exact hc.mant_hi)
    (by have := hc.exp_lo; unfold minExponent; omega)
    (by have := hc.exp_hi; unfold maxExponent; omega)
  rw [h_red, h_neg₀, h_mAbs]

/-- The IOU branch of `STAmount.operator_add` on canonical operands reduces to
the Number-level pipeline: 19-digit add, 16-digit renormalize, repack. -/
theorem STAmount.operator_add_iou_unfold (v1 v2 : STAmount) (mode : rounding_mode)
    (iss : Issue)
    (h_asset1 : v1.mAsset = .issue iss) (h_asset2 : v2.mAsset = .issue iss)
    (h_not_xrp : iss.isXRP = false)
    (hc1 : v1.IOUCanonical) (hc2 : v2.IOUCanonical) :
    STAmount.operator_add v1 v2 mode
      = match Number.operator_add
            ⟨v1.mIsNegative, v1.mValue * 10 * 10 * 10, v1.mOffset - 3⟩
            ⟨v2.mIsNegative, v2.mValue * 10 * 10 * 10, v2.mOffset - 3⟩ mode with
        | .error e => .error e
        | .ok n =>
          match IOUAmount.ofNumber n mode with
          | .error e => .error e
          | .ok sumI => STAmount.ofIOUAmount sumI iss mode := by
  have h_mv1 : v1.mValue ≠ 0 := by
    intro h0
    have h1 : v1.mValue.toNat = 0 := by rw [h0]; rfl
    have := hc1.mant_lo
    omega
  have h_mv2 : v2.mValue ≠ 0 := by
    intro h0
    have h1 : v2.mValue.toNat = 0 := by rw [h0]; rfl
    have := hc2.mant_lo
    omega
  have h_cmp : STAmount.areComparable v1 v2 = true := by
    unfold STAmount.areComparable Asset.areComparable
    rw [h_asset1, h_asset2]
    simp only [Bool.and_eq_true]
    exact ⟨beq_self_eq_true' iss.native, beq_self_eq_true' iss.currency.val⟩
  unfold STAmount.operator_add
  rw [h_cmp]
  simp only [Bool.not_true, Bool.false_eq_true, if_false]
  rw [show (v2.mValue == 0) = false from beq_eq_false_iff_ne.mpr h_mv2,
      show (v1.mValue == 0) = false from beq_eq_false_iff_ne.mpr h_mv1]
  simp only [Bool.false_eq_true, if_false]
  rw [h_asset1]
  simp only []
  rw [show iss.isXRP = false from h_not_xrp]
  simp only [Bool.false_eq_true, if_false]
  rw [STAmount.iou_canonical_id v1 mode hc1, STAmount.iou_canonical_id v2 mode hc2]
  simp only []
  -- IOUAmount.operator_add unfold.
  have h_sd1_ne : ¬ ((⟨v1.signedDrops.toInt64, v1.mOffset⟩ : IOUAmount).mantissa_ == 0)
      = true := by
    intro h
    have h_eq : v1.signedDrops.toInt64 = 0 := by exact_mod_cast beq_iff_eq.mp h
    have h_zero : v1.signedDrops.toInt64.toInt = 0 := by rw [h_eq]; rfl
    rw [STAmount.signedDrops_toInt64_toInt v1 hc1.mant_hi] at h_zero
    unfold STAmount.signedDrops at h_zero
    have := hc1.mant_lo
    rcases hneg : v1.mIsNegative with _ | _ <;> rw [hneg] at h_zero <;>
      simp at h_zero <;> omega
  have h_sd2_ne : ¬ ((⟨v2.signedDrops.toInt64, v2.mOffset⟩ : IOUAmount).mantissa_ == 0)
      = true := by
    intro h
    have h_eq : v2.signedDrops.toInt64 = 0 := by exact_mod_cast beq_iff_eq.mp h
    have h_zero : v2.signedDrops.toInt64.toInt = 0 := by rw [h_eq]; rfl
    rw [STAmount.signedDrops_toInt64_toInt v2 hc2.mant_hi] at h_zero
    unfold STAmount.signedDrops at h_zero
    have := hc2.mant_lo
    rcases hneg : v2.mIsNegative with _ | _ <;> rw [hneg] at h_zero <;>
      simp at h_zero <;> omega
  unfold IOUAmount.operator_add
  rw [show ((⟨v2.signedDrops.toInt64, v2.mOffset⟩ : IOUAmount).mantissa_ == 0) = false
        from Bool.eq_false_iff.mpr (fun h => h_sd2_ne h),
      show ((⟨v1.signedDrops.toInt64, v1.mOffset⟩ : IOUAmount).mantissa_ == 0) = false
        from Bool.eq_false_iff.mpr (fun h => h_sd1_ne h)]
  simp only [Bool.false_eq_true, if_false]
  rw [STAmount.iou_toNumber_canonical v1 mode hc1,
      STAmount.iou_toNumber_canonical v2 mode hc2]
  simp only []
  -- Fold the trailing ofNumber/ofIOUAmount composition.
  match h_add : Number.operator_add
      ⟨v1.mIsNegative, v1.mValue * 10 * 10 * 10, v1.mOffset - 3⟩
      ⟨v2.mIsNegative, v2.mValue * 10 * 10 * 10, v2.mOffset - 3⟩ mode with
  | .error e => rfl
  | .ok n =>
    simp only []
    match h_of : IOUAmount.ofNumber n mode with
    | .error e => rfl
    | .ok sumI => rfl

end XRPL.Model.Protocol
