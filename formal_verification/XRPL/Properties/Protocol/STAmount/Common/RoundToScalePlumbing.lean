import Mathlib.Tactic

import XRPL.Properties.Protocol.STAmount.Common.DiscreteDefs
import XRPL.Properties.Protocol.Number.Common.Rounding.SmallRange
import XRPL.Properties.Protocol.Number.Common.Int64Lemmas
import XRPL.Properties.Protocol.Number.Common.ToRatLemmas


namespace XRPL.Model.Protocol

/-! # Plumbing for the `roundToScale` discrete theorem

The IOU pipeline (`STAmount.iou` → `IOUAmount.normalize` → `from_rep` at
`largeRange` → `normalizeToRange` at the 16-digit range → exponent clamp)
is the **identity** on canonical 16-digit inputs. This file proves that
round-trip plus the reference-amount construction. -/

/-- A canonical (post-`canonicalize`) nonzero IOU STAmount. -/
structure STAmount.IOUCanonical (s : STAmount) : Prop where
  iou_asset : s.mAsset.holdsIssue = true
  not_xrp : s.mAsset.isNative = false
  mant_lo : 10 ^ 15 ≤ s.mValue.toNat
  mant_hi : s.mValue.toNat < 10 ^ 16
  exp_lo : (-96 : ℤ) ≤ s.mOffset
  exp_hi : s.mOffset ≤ 80

/-- `STAmount.toRat` is definitionally the `Number.toRat` of the same record. -/
lemma STAmount.toRat_eq_number (s : STAmount) :
    s.toRat = (⟨s.mIsNegative, s.mValue, s.mOffset⟩ : Number).toRat := rfl

/-- Value form of a non-negative STAmount. -/
lemma STAmount.toRat_of_nonneg (s : STAmount) (h : s.mIsNegative = false) :
    s.toRat = (s.mValue.toNat : ℚ) * 10 ^ s.mOffset := by
  rw [STAmount.toRat_eq_number]
  exact Number.toRat_of_nonneg _ h

/-- Magnitude form of an arbitrary STAmount. -/
lemma STAmount.abs_toRat (s : STAmount) :
    |s.toRat| = (s.mValue.toNat : ℚ) * 10 ^ s.mOffset := by
  rw [STAmount.toRat_eq_number]
  exact abs_toRat_eq _

/-- Sign form: a negative-flagged STAmount denotes `−(m·10^e)`. -/
lemma STAmount.toRat_of_neg (s : STAmount) (h : s.mIsNegative = true) :
    s.toRat = -((s.mValue.toNat : ℚ) * 10 ^ s.mOffset) := by
  rw [STAmount.toRat_eq_number]
  exact Number.toRat_of_neg _ h

/-- The `Int64` view of `signedDrops` is exact for 16-digit mantissas. -/
lemma STAmount.signedDrops_toInt64_toInt (s : STAmount)
    (h_hi : s.mValue.toNat < 10 ^ 16) :
    s.signedDrops.toInt64.toInt = s.signedDrops := by
  apply Int64.toInt_ofInt_of_le
  · unfold STAmount.signedDrops
    split <;> omega
  · unfold STAmount.signedDrops
    split <;> omega

/-- The full IOU round-trip is the identity on canonical 16-digit inputs:
`value.iou mode = .ok ⟨signedDrops, mOffset⟩` in **every** mode. -/
theorem STAmount.iou_canonical_id (s : STAmount) (mode : rounding_mode)
    (hc : s.IOUCanonical) :
    s.iou mode = .ok ⟨s.signedDrops.toInt64, s.mOffset⟩ := by
  have h_fit : s.mValue.toNat < 2 ^ 63 := by
    have := hc.mant_hi
    omega
  have h_mv_ne : s.mValue.toNat ≠ 0 := by
    have := hc.mant_lo
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
  have h_int : ¬ s.integral = true := by
    intro h
    unfold STAmount.integral at h
    rcases ha : s.mAsset with i | m
    · have h_native := hc.not_xrp
      rw [ha] at h_native
      rw [ha] at h
      -- `Asset.integral (.issue i)` and `Asset.isNative (.issue i)` both reduce to `i.native`.
      exact Bool.noConfusion (h_native.symm.trans
        (show (Asset.issue i).isNative = true from h))
    · have h_iss := hc.iou_asset
      rw [ha] at h_iss
      exact Bool.noConfusion h_iss
  unfold STAmount.iou
  rw [if_neg h_int]
  -- ofMantissaExp → normalize: mantissa ≠ 0.
  unfold IOUAmount.ofMantissaExp IOUAmount.normalize
  have h_sd_ne : ¬ (sd == 0) = true := by
    intro h
    have h_eq : sd = 0 := by exact_mod_cast beq_iff_eq.mp h
    have h_zero : sd.toInt = 0 := by rw [h_eq]; rfl
    rw [h_sd_signed] at h_zero
    rcases hneg : s.mIsNegative with _ | _ <;> rw [hneg] at h_zero <;> simp at h_zero <;>
      omega
  rw [show (sd == 0) = false from Bool.eq_false_iff.mpr (fun h => h_sd_ne h)]
  simp only [Bool.false_eq_true, if_false]
  -- from_rep at largeRange: the ×1000 shift.
  set neg₀ : Bool := decide (sd < 0) with hneg₀_def
  set mAbs : UInt64 := sd.toInt.natAbs.toUInt64 with hmAbs_def
  have h_mAbs_toNat : mAbs.toNat = s.mValue.toNat := by
    have hsz : UInt64.size = 18446744073709551616 := uint64_size_val
    have h_lt : sd.toInt.natAbs < UInt64.size := by
      rw [h_sd_abs, hsz]
      omega
    calc mAbs.toNat = sd.toInt.natAbs := UInt64.toNat_ofNat_of_lt' h_lt
      _ = s.mValue.toNat := h_sd_abs
  have h_fr : Number.from_rep sd s.mOffset largeRange.min largeRange.max mode
      = .ok ⟨neg₀, mAbs * 10 * 10 * 10, s.mOffset - 3⟩ := by
    unfold Number.from_rep Number.normalized Number.normalize Number.unchecked
    exact doNormalize_large_16digit neg₀ mAbs s.mOffset mode
      (by rw [h_mAbs_toNat]; exact hc.mant_lo)
      (by rw [h_mAbs_toNat]; exact hc.mant_hi)
      (by have := hc.exp_lo; unfold minExponent; omega)
      (by have := hc.exp_hi; unfold maxExponent; omega)
  rw [h_fr]
  simp only []
  -- fromNumber at the 16-digit range: exact /1000 shift back.
  set v : Number := ⟨neg₀, mAbs * 10 * 10 * 10, s.mOffset - 3⟩ with hv_def
  have hM_toNat : (mAbs * 10 * 10 * 10).toNat = s.mValue.toNat * 1000 := by
    rw [m_mul_thousand_no_overflow (by rw [h_mAbs_toNat]; exact hc.mant_hi), h_mAbs_toNat]
  have h_fn : IOUAmount.fromNumber v mode
      = .ok { mantissa_ := if neg₀ then -(v.mantissa_ / 10 / 10 / 10).toInt64
                           else (v.mantissa_ / 10 / 10 / 10).toInt64,
              exponent_ := s.mOffset } := by
    unfold IOUAmount.fromNumber
    have h_ntr := normalizeToRange_16_exact v mode
      (by show 10 ^ 18 ≤ (mAbs * 10 * 10 * 10).toNat
          rw [hM_toNat]; have := hc.mant_lo; omega)
      (by show (mAbs * 10 * 10 * 10).toNat < 10 ^ 19
          rw [hM_toNat]; have := hc.mant_hi; omega)
      (by show (mAbs * 10 * 10 * 10).toNat % 1000 = 0
          rw [hM_toNat]; omega)
      (by show minExponent ≤ s.mOffset - 3 + 3
          have := hc.exp_lo; unfold minExponent; omega)
      (by show s.mOffset - 3 + 3 ≤ maxExponent
          have := hc.exp_hi; unfold maxExponent; omega)
    have h_exp_fix : v.exponent_ + 3 = s.mOffset := by
      show s.mOffset - 3 + 3 = s.mOffset
      ring
    rw [h_exp_fix] at h_ntr
    rw [h_ntr]
  rw [h_fn]
  simp only []
  -- The clamp passes: −96 ≤ s.mOffset ≤ 80.
  rw [if_neg (by
        show ¬ s.mOffset > cMaxOffset
        have := hc.exp_hi
        unfold cMaxOffset
        omega),
      if_neg (by
        show ¬ s.mOffset < cMinOffset
        have := hc.exp_lo
        unfold cMinOffset
        omega)]
  -- The mantissa folds back to signedDrops.
  have h_q3 : (v.mantissa_ / 10 / 10 / 10).toNat = s.mValue.toNat := by
    show ((mAbs * 10 * 10 * 10) / 10 / 10 / 10).toNat = s.mValue.toNat
    rw [m_div_thousand_toNat (mAbs * 10 * 10 * 10), hM_toNat]
    omega
  have h_q3_eq : v.mantissa_ / 10 / 10 / 10 = s.mValue := UInt64.toNat_inj.mp h_q3
  rw [h_q3_eq]
  congr 2
  -- ⊢ (if neg₀ then -s.mValue.toInt64 else s.mValue.toInt64) = sd
  have h_neg₀_val : neg₀ = s.mIsNegative := by
    rw [hneg₀_def]
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
      have : 0 < s.mValue.toNat := by omega
      omega
  apply Int64.toInt_inj.mp
  rw [h_neg₀_val]
  rcases hneg : s.mIsNegative with _ | _
  · simp only [Bool.false_eq_true, if_false]
    rw [UInt64.toInt64_toInt_of_lt _ h_fit, h_sd_signed, hneg]
    simp
  · simp only [if_true]
    rw [UInt64.neg_toInt64_toInt_of_lt _ h_fit, h_sd_signed, hneg]
    simp

/-- `canonicalize` is the identity on canonical 16-digit IOU records. -/
theorem STAmount.canonicalize_canonical_id (s : STAmount) (mode : rounding_mode)
    (hc : s.IOUCanonical) :
    s.canonicalize mode = .ok s := by
  have h_fit : s.mValue.toNat < 2 ^ 63 := by
    have := hc.mant_hi
    omega
  have h_int : ¬ s.integral = true := by
    intro h
    unfold STAmount.integral at h
    rcases ha : s.mAsset with i | m
    · have h_native := hc.not_xrp
      rw [ha] at h_native
      rw [ha] at h
      exact Bool.noConfusion (h_native.symm.trans
        (show (Asset.issue i).isNative = true from h))
    · have h_iss := hc.iou_asset
      rw [ha] at h_iss
      exact Bool.noConfusion h_iss
  have h_sd_val : s.signedDrops.toInt64.toInt = s.signedDrops :=
    STAmount.signedDrops_toInt64_toInt s hc.mant_hi
  set sd : Int64 := s.signedDrops.toInt64 with hsd_def
  have h_sd_signed : sd.toInt = if s.mIsNegative then -(s.mValue.toNat : ℤ)
      else (s.mValue.toNat : ℤ) := by
    rw [h_sd_val]
    unfold STAmount.signedDrops
    rcases hneg : s.mIsNegative with _ | _ <;> simp
  unfold STAmount.canonicalize
  rw [if_neg h_int, STAmount.iou_canonical_id s mode hc]
  simp only []
  -- Repack: the sign, magnitude, and exponent all reproduce the record.
  have h_signum_neg : (IOUAmount.signum ⟨sd, s.mOffset⟩ < 0) = (s.mIsNegative = true) := by
    unfold IOUAmount.signum
    simp only []
    rcases hneg : s.mIsNegative with _ | _
    · have h_nn : ¬ sd < 0 := by
        rw [Int64.lt_iff_toInt_lt, h_sd_signed, hneg,
            (by decide : (0 : Int64).toInt = 0)]
        simp only [Bool.false_eq_true, if_false]
        omega
      rw [if_neg h_nn]
      by_cases h_ne : sd != 0
      · rw [if_pos h_ne]
        norm_num
      · rw [if_neg h_ne]
        norm_num
    · have h_lt : sd < 0 := by
        rw [Int64.lt_iff_toInt_lt, h_sd_signed, hneg,
            (by decide : (0 : Int64).toInt = 0)]
        simp only [if_true]
        have : 0 < s.mValue.toNat := by have := hc.mant_lo; omega
        omega
      rw [if_pos h_lt]
      norm_num
  rcases hneg : s.mIsNegative with _ | _
  · -- positive record
    have h_not_neg : ¬ (IOUAmount.signum ⟨sd, s.mOffset⟩ < 0) := by
      rw [h_signum_neg, hneg]
      exact Bool.noConfusion
    rw [if_neg h_not_neg, decide_eq_false h_not_neg]
    have h_abs_toNat : (sd.toUInt64).toNat = s.mValue.toNat := by
      have h0 : 0 ≤ sd.toInt := by
        rw [h_sd_signed, hneg]
        simp only [Bool.false_eq_true, if_false]
        positivity
      have h1 := toUInt64_toNat_of_nonneg sd h0
      rw [h_sd_signed, hneg] at h1
      simp only [Bool.false_eq_true, if_false] at h1
      omega
    have h_abs_eq : sd.toUInt64 = s.mValue := UInt64.toNat_inj.mp h_abs_toNat
    rw [h_abs_eq]
    obtain ⟨a, v, o, n⟩ := s
    simp only at hneg
    rw [hneg]
  · -- negative record
    have h_is_neg : IOUAmount.signum ⟨sd, s.mOffset⟩ < 0 := by
      have := h_signum_neg
      rw [hneg] at this
      rw [this]
    rw [if_pos h_is_neg, decide_eq_true h_is_neg]
    have h_neg_toInt : (-sd).toInt = (s.mValue.toNat : ℤ) := by
      have h_lt : sd.toInt = -(s.mValue.toNat : ℤ) := by
        rw [h_sd_signed, hneg]
        simp
      rw [Int64.toInt_neg, h_lt, Int.bmod_eq_iff (by norm_num)]
      have := hc.mant_hi
      constructor
      · push_cast
        omega
      · push_cast
        omega
    have h_abs_toNat : ((-sd).toUInt64).toNat = s.mValue.toNat := by
      have h1 := toUInt64_toNat_of_nonneg (-sd) (by rw [h_neg_toInt]; positivity)
      rw [h_neg_toInt] at h1
      omega
    have h_abs_eq : (-sd).toUInt64 = s.mValue := UInt64.toNat_inj.mp h_abs_toNat
    rw [h_abs_eq]
    obtain ⟨a, v, o, n⟩ := s
    simp only at hneg
    rw [hneg]

/-- The reference amount `±10^15 · 10^s` constructs exactly, for any IOU asset
and any scale `s ∈ [−96, 80]`. -/
theorem STAmount.checked_reference (asset : Asset) (s : ℤ) (neg : Bool)
    (mode : rounding_mode)
    (h_iou : asset.holdsIssue = true) (h_not_xrp : asset.isNative = false)
    (hs_lo : (-96 : ℤ) ≤ s) (hs_hi : s ≤ 80) :
    STAmount.checked asset kMinValue s neg mode
      = .ok ⟨asset, kMinValue, s, neg⟩ := by
  have hc : STAmount.IOUCanonical ⟨asset, kMinValue, s, neg⟩ :=
    { iou_asset := h_iou
      not_xrp := h_not_xrp
      mant_lo := by show 10 ^ 15 ≤ kMinValue.toNat; rw [(by decide : kMinValue.toNat = 10 ^ 15)]
      mant_hi := by show kMinValue.toNat < 10 ^ 16; rw [(by decide : kMinValue.toNat = 10 ^ 15)]; norm_num
      exp_lo := hs_lo
      exp_hi := hs_hi }
  unfold STAmount.checked STAmount.unchecked
  exact STAmount.canonicalize_canonical_id _ mode hc

end XRPL.Model.Protocol
