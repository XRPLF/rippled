import XRPL.Properties.Protocol.Number.ToRep.ToRep
import XRPL.Properties.Protocol.STAmount.Add.Common.Native
import XRPL.Properties.Protocol.STAmount.Common.STAmountCommonProps

namespace XRPL.Model.Protocol

/-- `(if k < 0 then -1 else 1) · |k| = k` for an `Int64` `k` (sign flag in `decide`
form), recovers a signed value from its magnitude and sign bit. -/
lemma sign_decide_natAbs (k : Int64) :
    (if decide (k < 0) = true then (-1 : ℚ) else 1) * (k.toInt.natAbs : ℚ) = (k.toInt : ℚ) := by
  have hcast : (k.toInt.natAbs : ℚ) = |(k.toInt : ℚ)| := by rw [Nat.cast_natAbs, Int.cast_abs]
  by_cases hk : k < 0
  · have hki : k.toInt < 0 := by simpa using Int64.lt_iff_toInt_lt.mp hk
    rw [if_pos (by simpa using hk), hcast, abs_of_neg (by exact_mod_cast hki)]; ring
  · have hki : ¬ k.toInt < 0 := fun hc => hk (Int64.lt_iff_toInt_lt.mpr (by simpa using hc))
    rw [if_neg (by simpa using hk), hcast, abs_of_nonneg (by exact_mod_cast not_lt.mp hki)]; ring

/-- The MPT `canonicalize` round-trip (`ofInt64 (.mptIssue _) d 0`) is **exact**:
the integer `d` (within MPT range) passes through `to_rep` unchanged, so the result
represents `d`. -/
lemma STAmount.ofInt64_mpt_toRat (mpt : MPTIssue) (d : Int64) (mode : rounding_mode)
    (result : STAmount)
    (hlo : -(2 ^ 63) < d.toInt) (hhi : d.toInt.natAbs ≤ maxMPTokenAmount)
    (hok : STAmount.ofInt64 (.mptIssue mpt) d 0 mode = .ok result) :
    result.toRat = (d.toInt : ℚ) := by
  have ha2 := Int64.toInt_lt d
  have hmaxR : (maxMPTokenAmount : ℕ) = maxRep.toNat := by decide
  set n0 : STAmount := STAmount.uncheckedFromInt64 (.mptIssue mpt) d 0 with hn0
  -- field facts for `n0`
  have hn0_asset : n0.mAsset = .mptIssue mpt := by
    rw [hn0, STAmount.uncheckedFromInt64]; split <;> rfl
  have hn0_off : n0.mOffset = 0 := by
    rw [hn0, STAmount.uncheckedFromInt64]; split <;> rfl
  have hn0_neg : n0.mIsNegative = decide (d < 0) := by
    rw [hn0, STAmount.uncheckedFromInt64]
    by_cases hd : d < 0
    · rw [if_pos hd]; simp [STAmount.unchecked, hd]
    · rw [if_neg hd]; simp [STAmount.unchecked, hd]
  have hn0_val : n0.mValue.toNat = d.toInt.natAbs := by
    rw [hn0, STAmount.uncheckedFromInt64]
    by_cases hd : d < 0
    · rw [if_pos hd]
      have hdneg : d.toInt < 0 := by simpa using Int64.lt_iff_toInt_lt.mp hd
      have hnegInt : (-d).toInt = -d.toInt := by
        rw [Int64.toInt_neg, Int.bmod_eq_iff (by norm_num)]; refine ⟨?_, ?_⟩ <;> push_cast <;> omega
      have h0 : (0 : ℤ) ≤ (-d).toInt := by rw [hnegInt]; omega
      have hc := toUInt64_toNat_of_nonneg (-d) h0
      show (-d).toUInt64.toNat = d.toInt.natAbs
      have : ((-d).toUInt64.toNat : ℤ) = d.toInt.natAbs := by
        rw [hc, hnegInt]; omega
      exact_mod_cast this
    · rw [if_neg hd]
      have hdpos : (0 : ℤ) ≤ d.toInt := by
        rcases lt_or_ge d.toInt 0 with h | h
        · exact absurd (Int64.lt_iff_toInt_lt.mpr (by simpa using h)) hd
        · exact h
      have hc := toUInt64_toNat_of_nonneg d hdpos
      show d.toUInt64.toNat = d.toInt.natAbs
      have : (d.toUInt64.toNat : ℤ) = d.toInt.natAbs := by rw [hc]; omega
      exact_mod_cast this
  have hn0_int : n0.integral = true := by
    rw [STAmount.integral, hn0_asset]; rfl
  have hn0_native : n0.native = false := by
    rw [STAmount.native, hn0_asset]; rfl
  -- magnitude bound feeding the keystone
  have hval_le : n0.mValue.toNat ≤ maxRep.toNat := by rw [hn0_val, ← hmaxR]; exact hhi
  -- unfold the canonicalize pipeline
  rw [STAmount.ofInt64, ← hn0, STAmount.canonicalize] at hok
  by_cases hz : n0.mValue == 0
  · -- magnitude zero ⟹ d = 0, both sides vanish
    rw [if_pos hn0_int, if_pos (by rw [hz]; rfl)] at hok
    have hd0 : d.toInt = 0 := by
      rw [beq_iff_eq] at hz
      have : n0.mValue.toNat = 0 := by rw [hz]; rfl
      rw [hn0_val] at this; omega
    rw [Except.ok.inj hok.symm, STAmount.toRat_of_offset_zero _ rfl]
    simp only [STAmount.signedDrops]; rw [hd0]; simp
  · have hz' : (n0.mValue == 0) = false := by simpa using hz
    -- navigate canonicalize to the MPT `ofNumber` call and unfold it
    simp only [hn0_int, hz', hn0_off, hn0_native, MPTAmount.ofNumber, if_true,
      Bool.false_or, Bool.false_eq_true, Bool.not_false,
      Bool.true_and, Bool.false_and, gt_iff_lt, Int.reduceLT, decide_false,
      if_false] at hok
    cases hr : (Number.unchecked n0.mIsNegative n0.mValue 0).to_rep mode with
    | error e => rw [hr] at hok; simp at hok
    | ok r =>
      rw [hr] at hok
      simp only [] at hok
      -- keystone: r is exactly the signed magnitude = d
      have hkey := to_rep_exact_of_exponent_zero n0.mIsNegative n0.mValue mode r hval_le hr
      rw [hn0_neg, hn0_val] at hkey
      have hr_eq : (r.toInt : ℚ) = (d.toInt : ℚ) := by rw [hkey]; exact sign_decide_natAbs d
      have hr_int : r.toInt = d.toInt := by exact_mod_cast hr_eq
      have habs : r.toInt.natAbs ≤ maxMPTokenAmount := by rw [hr_int]; exact hhi
      rw [if_neg (not_lt.mpr habs)] at hok
      -- result = { n0 with mValue := |r|, mOffset := 0, mIsNegative := r < 0 }
      rw [Except.ok.inj hok.symm, STAmount.toRat_of_offset_zero _ rfl]
      simp only [STAmount.signedDrops]
      have hres_val : (r.toInt.natAbs.toUInt64).toNat = r.toInt.natAbs := by
        have hb : r.toInt.natAbs < 2 ^ 64 := by
          have : (maxMPTokenAmount : ℕ) < 2 ^ 64 := by decide
          omega
        exact UInt64.toNat_ofNat_of_lt hb
      rw [hres_val, ← hr_int]
      rw [show (if decide (r < 0) then -(r.toInt.natAbs : ℤ) else (r.toInt.natAbs : ℤ)) = r.toInt from by
        by_cases hrneg : r < 0
        · rw [if_pos (by simpa using hrneg)]
          have hri : r.toInt ≤ 0 := le_of_lt (by simpa using Int64.lt_iff_toInt_lt.mp hrneg)
          rw [Int.ofNat_natAbs_of_nonpos hri]; ring
        · rw [if_neg (by simpa using hrneg)]
          have hri : 0 ≤ r.toInt := by
            rcases lt_or_ge r.toInt 0 with h | h
            · exact absurd (Int64.lt_iff_toInt_lt.mpr (by simpa using h)) hrneg
            · exact h
          rw [Int.natAbs_of_nonneg hri]]

/-- The MPT `canonicalize` round-trip is **exact** on an integral (offset-0,
in-range) MPT amount: the value passes through `to_rep` unchanged. -/
lemma STAmount.canonicalize_mpt_toRat (s result : STAmount) (mode : rounding_mode)
    (hmpt : s.mAsset.holdsMPTIssue = true) (hoff : s.mOffset = 0)
    (hrange : s.mValue.toNat ≤ maxMPTokenAmount)
    (hok : s.canonicalize mode = .ok result) :
    result.toRat = s.toRat := by
  have hint : s.integral = true := by
    unfold STAmount.integral; cases hm : s.mAsset with
    | issue i => rw [hm] at hmpt; simp [Asset.holdsMPTIssue] at hmpt
    | mptIssue m => simp [Asset.integral, MPTIssue.integral]
  have hnative : s.native = false := by
    unfold STAmount.native; cases hm : s.mAsset with
    | issue i => rw [hm] at hmpt; simp [Asset.holdsMPTIssue] at hmpt
    | mptIssue m => simp [Asset.isNative, MPTIssue.native]
  have hmaxR : (maxMPTokenAmount : ℕ) = maxRep.toNat := by decide
  have hval_le : s.mValue.toNat ≤ maxRep.toNat := by rw [← hmaxR]; exact hrange
  have hval_lt : s.mValue.toNat < 2 ^ 64 := by
    have : (maxMPTokenAmount : ℕ) < 2 ^ 64 := by decide
    omega
  rw [STAmount.canonicalize, if_pos hint] at hok
  by_cases hz : s.mValue == 0
  · rw [if_pos (by rw [hz]; rfl)] at hok
    rw [beq_iff_eq] at hz
    rw [Except.ok.inj hok.symm, STAmount.toRat_of_offset_zero _ rfl,
        STAmount.toRat_of_offset_zero s hoff]
    simp only [STAmount.signedDrops]; rw [hz]; simp
  · have hz' : (s.mValue == 0) = false := by simpa using hz
    simp only [hoff, hz', hnative, Bool.false_or, Bool.false_eq_true, Bool.not_false,
      Bool.false_and, Bool.true_and, gt_iff_lt, Int.reduceLT, Int.reduceLE, decide_false,
      if_false, MPTAmount.ofNumber] at hok
    cases hr : (Number.unchecked s.mIsNegative s.mValue 0).to_rep mode with
    | error e => rw [hr] at hok; simp at hok
    | ok r =>
      rw [hr] at hok
      simp only [] at hok
      have hkey := to_rep_exact_of_exponent_zero s.mIsNegative s.mValue mode r hval_le hr
      have hr_sd : r.toInt = s.signedDrops := by
        have h1 : (r.toInt : ℚ) = (s.signedDrops : ℚ) := by
          rw [hkey]; simp only [STAmount.signedDrops]
          by_cases hn : s.mIsNegative <;> simp [hn]
        exact_mod_cast h1
      have hnatAbs : r.toInt.natAbs = s.mValue.toNat := by
        rw [hr_sd]; unfold STAmount.signedDrops; split <;> simp
      rw [if_neg (by rw [hnatAbs]; exact not_lt.mpr hrange)] at hok
      rw [Except.ok.inj hok.symm, STAmount.reconstruct_toRat s.mAsset r (by rw [hnatAbs]; exact hval_lt),
          hr_sd, ← STAmount.toRat_of_offset_zero s hoff]

/-- MPT addition is **exact**: when both operands are well-formed MPT amounts of the
same issue whose magnitudes sum within range, the result represents `v1 + v2`
(integer addition, no rounding). -/
theorem STAmount.operator_add_mpt_exact (v1 v2 result : STAmount) (mode : rounding_mode)
    (mpt : MPTIssue) (hv1 : v1.mAsset = .mptIssue mpt)
    (hc1 : v1.MPTCanonical) (hc2 : v2.MPTCanonical)
    (hsum : v1.mValue.toNat + v2.mValue.toNat ≤ maxMPTokenAmount)
    (hok : STAmount.operator_add v1 v2 mode = .ok result) :
    result.toRat = v1.toRat + v2.toRat := by
  -- comparability is forced by `hok` succeeding (otherwise the model errors)
  have hcmp : STAmount.areComparable v1 v2 = true := by
    rcases hb : STAmount.areComparable v1 v2 with _ | _
    · rw [STAmount.operator_add, hb] at hok; simp at hok
    · rfl
  rw [STAmount.operator_add, if_neg (by rw [hcmp]; decide)] at hok
  by_cases hzv2 : v2.mValue == 0
  · -- branch A: `v2 = 0`, result is `v1`
    rw [if_pos hzv2] at hok
    rw [Except.ok.inj hok.symm, STAmount.toRat_zero_aux v2 hc2.offset_zero hzv2]; ring
  rw [if_neg hzv2] at hok
  by_cases hzv1 : v1.mValue == 0
  · -- branch B: `v1 = 0`, result is `canonicalize` of the MPT-asset record carrying `v2`'s value
    rw [if_pos hzv1] at hok
    rw [STAmount.checked] at hok
    have hmpt' : (STAmount.unchecked v1.mAsset v2.mValue v2.mOffset v2.mIsNegative).mAsset.holdsMPTIssue = true := by
      rw [hv1]; rfl
    have hcan := STAmount.canonicalize_mpt_toRat _ result mode hmpt' hc2.offset_zero hc2.value_in_range hok
    rw [STAmount.toRat_zero_aux v1 hc1.offset_zero hzv1, zero_add, hcan]
    rfl
  -- branch C: both nonzero, MPT main path
  rw [if_neg hzv1, hv1] at hok
  simp only at hok
  have hmax63 : (maxMPTokenAmount : ℕ) < 2 ^ 63 := by decide
  have hlt1 : v1.mValue.toNat < 2 ^ 63 := by have := hc1.value_in_range; omega
  have hlt2 : v2.mValue.toNat < 2 ^ 63 := by have := hc2.value_in_range; omega
  have hv1s := STAmount.signedDrops_toInt64_toInt_of_lt v1 hlt1
  have hv2s := STAmount.signedDrops_toInt64_toInt_of_lt v2 hlt2
  obtain ⟨hb1lo, hb1hi⟩ := STAmount.signedDrops_bounds v1
  obtain ⟨hb2lo, hb2hi⟩ := STAmount.signedDrops_bounds v2
  have hsumeq : (v1.signedDrops.toInt64 + v2.signedDrops.toInt64).toInt
      = v1.signedDrops + v2.signedDrops := by
    rw [toInt_add_of_bounds _ _ (by rw [hv1s, hv2s]; omega) (by rw [hv1s, hv2s]; omega),
      hv1s, hv2s]
  have hd_hi : (v1.signedDrops.toInt64 + v2.signedDrops.toInt64).toInt.natAbs ≤ maxMPTokenAmount := by
    rw [hsumeq]; omega
  have hd_lo : -(2 ^ 63) < (v1.signedDrops.toInt64 + v2.signedDrops.toInt64).toInt := by
    rw [hsumeq]; omega
  rw [STAmount.ofInt64_mpt_toRat mpt _ mode result hd_lo hd_hi hok, hsumeq]
  rw [STAmount.toRat_of_offset_zero v1 hc1.offset_zero,
      STAmount.toRat_of_offset_zero v2 hc2.offset_zero]
  push_cast; ring

end XRPL.Model.Protocol
