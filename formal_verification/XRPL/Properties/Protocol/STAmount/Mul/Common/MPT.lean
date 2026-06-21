import XRPL.Properties.Protocol.STAmount.Mul.Common.Native
import XRPL.Properties.Protocol.STAmount.Add.Common.MPT

namespace XRPL.Model.Protocol

/-- MPT multiplication of two non-negative MPT amounts whose product is within range
is **exact**: `result = v1 · v2` (integer product, no rounding). -/
theorem STAmount.operator_mul_mpt_exact (v1 v2 result : STAmount) (asset : Asset)
    (mode : rounding_mode)
    (hv1 : v1.mAsset.holdsMPTIssue = true) (hv2 : v2.mAsset.holdsMPTIssue = true)
    (hasset : asset.holdsMPTIssue = true)
    (hc1 : v1.MPTCanonical) (hc2 : v2.MPTCanonical)
    (hn1 : v1.mIsNegative = false) (hn2 : v2.mIsNegative = false)
    (hbound : v1.mValue.toNat * v2.mValue.toNat ≤ maxMPTokenAmount)
    (hok : STAmount.multiply v1 v2 asset mode = .ok result) :
    result.toRat = v1.toRat * v2.toRat := by
  have hmaxlt : (maxMPTokenAmount : ℕ) < 2 ^ 64 := by decide
  have hlt1 : v1.mValue.toNat < 2 ^ 63 := by
    have := hc1.value_in_range; unfold maxMPTokenAmount at this; omega
  have hlt2 : v2.mValue.toNat < 2 ^ 63 := by
    have := hc2.value_in_range; unfold maxMPTokenAmount at this; omega
  rw [STAmount.multiply] at hok
  by_cases hz : v1.isZero || v2.isZero
  · rw [if_pos hz, STAmount.checked] at hok
    have h0 := STAmount.canonicalize_mpt_toRat _ result mode hasset rfl (Nat.zero_le _) hok
    rw [h0, STAmount.toRat_zero_aux _ rfl rfl]
    rcases Bool.or_eq_true _ _ |>.mp hz with h | h
    · rw [show v1.toRat = 0 from STAmount.toRat_zero_aux v1 hc1.offset_zero
        (by unfold STAmount.isZero at h; exact h)]; ring
    · rw [show v2.toRat = 0 from STAmount.toRat_zero_aux v2 hc2.offset_zero
        (by unfold STAmount.isZero at h; exact h)]; ring
  · -- native guard is false (MPT is not native); MPT guard fires
    have hnotnative : (v1.native && v2.native && asset.isNative) = false := by
      have hvn : v1.native = false := by
        unfold STAmount.native; cases hm : v1.mAsset with
        | issue i => rw [hm] at hv1; simp [Asset.holdsMPTIssue] at hv1
        | mptIssue m => simp [Asset.isNative, MPTIssue.native]
      rw [hvn]; rfl
    have hmptguard : (v1.holdsMPTIssue && v2.holdsMPTIssue && asset.holdsMPTIssue) = true := by
      simp [STAmount.holdsMPTIssue, hv1, hv2, hasset]
    rw [if_neg hz, if_neg (by rw [hnotnative]; decide), if_pos hmptguard] at hok
    set aS : Int64 := v1.signedDrops.toInt64 with haS_def
    set bS : Int64 := v2.signedDrops.toInt64 with hbS_def
    set minV : UInt64 := (if aS ≤ bS then aS else bS).toUInt64 with hminV_def
    set maxV : UInt64 := (if aS ≤ bS then bS else aS).toUInt64 with hmaxV_def
    by_cases hg1 : minV > 3037000499
    · rw [if_pos hg1] at hok; simp at hok
    rw [if_neg hg1] at hok
    by_cases hg2 : (maxV >>> 32) * minV > 2147483648
    · rw [if_pos hg2] at hok; simp at hok
    rw [if_neg hg2, STAmount.checked] at hok
    have haSn : aS.toUInt64.toNat = v1.mValue.toNat := STAmount.signed_toUInt64_toNat v1 hn1 hlt1
    have hbSn : bS.toUInt64.toNat = v2.mValue.toNat := STAmount.signed_toUInt64_toNat v2 hn2 hlt2
    have hminmax : minV.toNat * maxV.toNat = v1.mValue.toNat * v2.mValue.toNat := by
      rw [hminV_def, hmaxV_def]
      by_cases hle : aS ≤ bS
      · rw [if_pos hle, if_pos hle, haSn, hbSn]
      · rw [if_neg hle, if_neg hle, haSn, hbSn, Nat.mul_comm]
    have hprodlt : minV.toNat * maxV.toNat < 2 ^ 64 := by rw [hminmax]; omega
    have hprodtoNat : (minV * maxV).toNat = v1.mValue.toNat * v2.mValue.toNat := by
      rw [UInt64.toNat_mul, Nat.mod_eq_of_lt hprodlt, hminmax]
    have hrange : (STAmount.unchecked asset (minV * maxV) 0 false).mValue.toNat ≤ maxMPTokenAmount := by
      show (minV * maxV).toNat ≤ maxMPTokenAmount
      rw [hprodtoNat]; exact hbound
    have hcan := STAmount.canonicalize_mpt_toRat _ result mode hasset rfl hrange hok
    rw [hcan, STAmount.toRat_of_nonneg_offset_zero _ rfl rfl]
    show ((STAmount.unchecked asset (minV * maxV) 0 false).mValue.toNat : ℚ) = _
    rw [show (STAmount.unchecked asset (minV * maxV) 0 false).mValue = minV * maxV from rfl,
        hprodtoNat,
        STAmount.toRat_of_nonneg_offset_zero v1 hn1 hc1.offset_zero,
        STAmount.toRat_of_nonneg_offset_zero v2 hn2 hc2.offset_zero]
    push_cast; ring

end XRPL.Model.Protocol
