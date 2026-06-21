import XRPL.Properties.Protocol.STAmount.Add.Common.Native

namespace XRPL.Model.Protocol

/-- The `minV * maxV` product in native/MPT `multiply` does not overflow `UInt64`:
the two overflow guards (`minV ≤ √cMax` and `(maxV ≫ 32)·minV ≤ cMax/2^32`) bound the
product below `2^64`. -/
lemma mul_minmax_no_overflow (minV maxV : ℕ) (h1 : minV ≤ 3000000000)
    (h2 : maxV / 2 ^ 32 * minV ≤ 2095475792) :
    minV * maxV < 2 ^ 64 := by
  set q := maxV / 2 ^ 32 with hq
  have hr : maxV % 2 ^ 32 < 2 ^ 32 := by omega
  have hdm : maxV = q * 2 ^ 32 + maxV % 2 ^ 32 := by omega
  rcases Nat.eq_zero_or_pos q with hq0 | hqpos
  · have hmax : maxV < 2 ^ 32 := by omega
    calc minV * maxV ≤ 3000000000 * maxV := Nat.mul_le_mul_right _ h1
      _ < 3000000000 * 2 ^ 32 := (Nat.mul_lt_mul_left (by norm_num)).mpr hmax
      _ < 2 ^ 64 := by norm_num
  · have hminV : minV ≤ 2095475792 := by
      calc minV = 1 * minV := (one_mul _).symm
        _ ≤ q * minV := Nat.mul_le_mul_right _ hqpos
        _ ≤ 2095475792 := h2
    have hexp : minV * maxV = (q * minV) * 2 ^ 32 + minV * (maxV % 2 ^ 32) := by
      conv_lhs => rw [hdm]
      ring
    rw [hexp]
    have ha : (q * minV) * 2 ^ 32 ≤ 2095475792 * 2 ^ 32 := Nat.mul_le_mul_right _ h2
    have hb : minV * (maxV % 2 ^ 32) ≤ 2095475792 * 2 ^ 32 :=
      Nat.mul_le_mul hminV (le_of_lt hr)
    omega

/-- For a non-negative offset-0 STAmount, the `UInt64` view of its `signedDrops`
reads back `mValue`. -/
lemma STAmount.signed_toUInt64_toNat (s : STAmount) (hn : s.mIsNegative = false)
    (hlt : s.mValue.toNat < 2 ^ 63) :
    s.signedDrops.toInt64.toUInt64.toNat = s.mValue.toNat := by
  have hsd : s.signedDrops = (s.mValue.toNat : ℤ) := by unfold STAmount.signedDrops; rw [hn]; rfl
  have hexact : s.signedDrops.toInt64.toInt = s.signedDrops :=
    STAmount.signedDrops_toInt64_toInt_of_lt s hlt
  have h0 : (0 : ℤ) ≤ s.signedDrops.toInt64.toInt := by rw [hexact, hsd]; positivity
  have hcast := toUInt64_toNat_of_nonneg s.signedDrops.toInt64 h0
  rw [hexact] at hcast
  have hgoal : (s.signedDrops.toInt64.toUInt64.toNat : ℤ) = (s.mValue.toNat : ℤ) := by
    rw [hcast]; exact hsd
  exact_mod_cast hgoal

/-- For a non-negative offset-0 STAmount, `toRat` is just its `mValue`. -/
lemma STAmount.toRat_of_nonneg_offset_zero (s : STAmount) (hn : s.mIsNegative = false)
    (ho : s.mOffset = 0) : s.toRat = (s.mValue.toNat : ℚ) := by
  rw [STAmount.toRat_of_offset_zero s ho]
  unfold STAmount.signedDrops; rw [hn]; simp

/-- Native (XRP) multiplication of two non-negative integral amounts is **exact**:
`result = v1 · v2` (integer product of drops, no rounding). -/
theorem STAmount.operator_mul_native_exact (v1 v2 result : STAmount) (asset : Asset)
    (mode : rounding_mode)
    (hc1 : v1.NativeCanonical) (hc2 : v2.NativeCanonical)
    (hn1 : v1.mIsNegative = false) (hn2 : v2.mIsNegative = false)
    (hasset : asset.isNative = true)
    (hok : STAmount.multiply v1 v2 asset mode = .ok result) :
    result.toRat = v1.toRat * v2.toRat := by
  obtain ⟨hv1, ho1, hb1⟩ := hc1
  obtain ⟨hv2, ho2, hb2⟩ := hc2
  have hlt1 : v1.mValue.toNat < 2 ^ 63 := by unfold kMaxNativeN at hb1; omega
  have hlt2 : v2.mValue.toNat < 2 ^ 63 := by unfold kMaxNativeN at hb2; omega
  have htr1 : v1.toRat = (v1.mValue.toNat : ℚ) := STAmount.toRat_of_nonneg_offset_zero v1 hn1 ho1
  have htr2 : v2.toRat = (v2.mValue.toNat : ℚ) := STAmount.toRat_of_nonneg_offset_zero v2 hn2 ho2
  rw [STAmount.multiply] at hok
  by_cases hz : v1.isZero || v2.isZero
  · -- a zero factor ⟹ product is `0`; the result canonicalizes a zero
    rw [if_pos hz, STAmount.checked] at hok
    have hnat : (STAmount.unchecked asset 0 0 false).native = true := by
      rw [STAmount.native]; exact hasset
    have h0 : result.toRat = (STAmount.unchecked asset 0 0 false).toRat :=
      STAmount.canonicalize_native_toRat _ result mode hnat rfl (Nat.zero_le _) hok
    rw [h0]
    have hz0 : (STAmount.unchecked asset 0 0 false).toRat = 0 := by
      apply STAmount.toRat_zero_aux _ rfl; rfl
    rw [hz0]
    rcases Bool.or_eq_true _ _ |>.mp hz with h | h
    · rw [htr1, show v1.mValue.toNat = 0 from by unfold STAmount.isZero at h; rw [beq_iff_eq] at h; rw [h]; rfl]; simp
    · rw [htr2, show v2.mValue.toNat = 0 from by unfold STAmount.isZero at h; rw [beq_iff_eq] at h; rw [h]; rfl]; simp
  · -- native main branch
    have hguard : (v1.native && v2.native && asset.isNative) = true := by
      rw [STAmount.native, STAmount.native, hv1, hv2, hasset]; rfl
    rw [if_neg hz, if_pos hguard] at hok
    set aS : Int64 := v1.signedDrops.toInt64 with haS_def
    set bS : Int64 := v2.signedDrops.toInt64 with hbS_def
    set minV : UInt64 := (if aS ≤ bS then aS else bS).toUInt64 with hminV_def
    set maxV : UInt64 := (if aS ≤ bS then bS else aS).toUInt64 with hmaxV_def
    by_cases hg1 : minV > 3000000000
    · rw [if_pos hg1] at hok; simp at hok
    rw [if_neg hg1] at hok
    by_cases hg2 : (maxV >>> 32) * minV > 2095475792
    · rw [if_pos hg2] at hok; simp at hok
    rw [if_neg hg2] at hok
    -- guards in `Nat` form
    have hg1n : minV.toNat ≤ 3000000000 := by
      have := UInt64.le_iff_toNat_le.mp (UInt64.not_lt.mp hg1)
      simpa using this
    have hshift : (maxV >>> 32).toNat = maxV.toNat / 2 ^ 32 := by
      have h64 : ((32 : UInt64).toNat % 64) = 32 := by decide
      rw [UInt64.toNat_shiftRight, h64, Nat.shiftRight_eq_div_pow]
    have hmaxlt : maxV.toNat < 2 ^ 64 := UInt64.toNat_lt_size maxV
    have hprodlt : (maxV >>> 32).toNat * minV.toNat < 2 ^ 64 := by
      have hq : (maxV >>> 32).toNat < 2 ^ 32 := by rw [hshift]; omega
      have hmlt : minV.toNat < 2 ^ 32 := by omega
      calc (maxV >>> 32).toNat * minV.toNat < 2 ^ 32 * 2 ^ 32 := Nat.mul_lt_mul'' hq hmlt
        _ = 2 ^ 64 := by norm_num
    have hg2n : maxV.toNat / 2 ^ 32 * minV.toNat ≤ 2095475792 := by
      have h := UInt64.le_iff_toNat_le.mp (UInt64.not_lt.mp hg2)
      rw [UInt64.toNat_mul, Nat.mod_eq_of_lt hprodlt, hshift] at h
      simpa using h
    have hovf := mul_minmax_no_overflow minV.toNat maxV.toNat hg1n hg2n
    -- product equals `v1·v2`
    have haSn : aS.toUInt64.toNat = v1.mValue.toNat :=
      STAmount.signed_toUInt64_toNat v1 hn1 hlt1
    have hbSn : bS.toUInt64.toNat = v2.mValue.toNat :=
      STAmount.signed_toUInt64_toNat v2 hn2 hlt2
    have hminmax : minV.toNat * maxV.toNat = v1.mValue.toNat * v2.mValue.toNat := by
      rw [hminV_def, hmaxV_def]
      by_cases hle : aS ≤ bS
      · rw [if_pos hle, if_pos hle, haSn, hbSn]
      · rw [if_neg hle, if_neg hle, haSn, hbSn, Nat.mul_comm]
    -- the result is the (non-overflowing) product, offset 0
    rw [Except.ok.inj hok.symm, STAmount.toRat_of_nonneg_offset_zero _ rfl rfl]
    show ((STAmount.unchecked xrpAsset (minV * maxV) 0 false).mValue.toNat : ℚ) = _
    rw [htr1, htr2]
    have : (minV * maxV).toNat = v1.mValue.toNat * v2.mValue.toNat := by
      rw [UInt64.toNat_mul, Nat.mod_eq_of_lt hovf, hminmax]
    rw [show (STAmount.unchecked xrpAsset (minV * maxV) 0 false).mValue = minV * maxV from rfl, this]
    push_cast; ring

end XRPL.Model.Protocol
