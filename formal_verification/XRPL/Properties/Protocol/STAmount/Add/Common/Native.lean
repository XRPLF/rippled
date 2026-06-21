import Mathlib.Tactic

import XRPL.Properties.Protocol.Number.Common.Int64Lemmas
import XRPL.Properties.Protocol.Number.ToRep.ToRep
import XRPL.Properties.Protocol.STAmount.Common.DiscreteDefs
import XRPL.Model.Protocol.STAmount

namespace XRPL.Model.Protocol

/-- For an integral (offset-0) STAmount, the exact value is just its signed drops. -/
lemma STAmount.toRat_of_offset_zero (s : STAmount) (ho : s.mOffset = 0) :
    s.toRat = (s.signedDrops : ℚ) := by
  unfold STAmount.toRat STAmount.signedDrops
  rw [ho]
  simp only [ge_iff_le, le_refl, if_true, Int.toNat_zero, pow_zero, mul_one, Rat.mkRat_one]
  by_cases hneg : s.mIsNegative
  · rw [if_pos hneg, if_pos hneg]; push_cast; ring
  · rw [if_neg hneg, if_neg hneg]; push_cast; ring

/-- `uncheckedFromInt64 asset d 0` represents exactly `d` (for `d` above `Int64.min`);
`toRat` ignores the asset, so this holds for any asset. -/
lemma STAmount.uncheckedFromInt64_toRat (asset : Asset) (d : Int64) (hb : -(2 ^ 63) < d.toInt) :
    (STAmount.uncheckedFromInt64 asset d 0).toRat = (d.toInt : ℚ) := by
  have ha2 := Int64.toInt_lt d
  have h0i : (0 : Int64).toInt = 0 := by decide
  rw [STAmount.uncheckedFromInt64]
  by_cases hd : d < 0
  · have hdneg : d.toInt < 0 := by
      have := Int64.lt_iff_toInt_lt.mp hd; rw [h0i] at this; exact this
    have hnegInt : (-d).toInt = -d.toInt := by
      rw [Int64.toInt_neg, Int.bmod_eq_iff (by norm_num)]
      refine ⟨?_, ?_⟩ <;> push_cast <;> omega
    have h0 : (0 : ℤ) ≤ (-d).toInt := by rw [hnegInt]; omega
    have hc : ((-d).toUInt64.toNat : ℤ) = (-d).toInt := toUInt64_toNat_of_nonneg (-d) h0
    rw [if_pos hd, STAmount.toRat_of_offset_zero _ rfl]
    simp only [STAmount.signedDrops, STAmount.unchecked]
    rw [hc, hnegInt]; push_cast; ring
  · have hdpos : (0 : ℤ) ≤ d.toInt := by
      rcases lt_or_ge d.toInt 0 with h | h
      · exact absurd (Int64.lt_iff_toInt_lt.mpr (by rw [h0i]; exact h)) hd
      · exact h
    have hc : (d.toUInt64.toNat : ℤ) = d.toInt := toUInt64_toNat_of_nonneg d hdpos
    rw [if_neg hd, STAmount.toRat_of_offset_zero _ rfl]
    simp only [STAmount.signedDrops, STAmount.unchecked, reduceCtorEq, if_false]
    rw [hc]

/-- `ofNativeInt64 d` represents exactly `d` drops (for `d` above `Int64.min`). -/
lemma STAmount.ofNativeInt64_toRat (d : Int64) (hb : -(2 ^ 63) < d.toInt) :
    (STAmount.ofNativeInt64 d).toRat = (d.toInt : ℚ) := by
  rw [STAmount.ofNativeInt64]; exact STAmount.uncheckedFromInt64_toRat xrpAsset d hb

/-- Reconstruction: a record carrying magnitude `|r|`, offset `0`, and sign bit
`r < 0` denotes exactly `r`. The shared tail of every canonicalize round-trip
(native, MPT) after `to_rep` produces the signed `Int64` `r`. -/
lemma STAmount.reconstruct_toRat (asset : Asset) (r : Int64) (hb : r.toInt.natAbs < 2 ^ 64) :
    ({ mAsset := asset, mValue := r.toInt.natAbs.toUInt64, mOffset := 0, mIsNegative := decide (r < 0) } : STAmount).toRat = (r.toInt : ℚ) := by
  have hsigned : (if decide (r < 0) then -(r.toInt.natAbs : ℤ) else (r.toInt.natAbs : ℤ))
      = r.toInt := by
    by_cases hrneg : r < 0
    · rw [if_pos (by simpa using hrneg)]
      have hri : r.toInt ≤ 0 := le_of_lt (by simpa using Int64.lt_iff_toInt_lt.mp hrneg)
      rw [Int.ofNat_natAbs_of_nonpos hri]; ring
    · rw [if_neg (by simpa using hrneg)]
      have hri : 0 ≤ r.toInt := by
        rcases lt_or_ge r.toInt 0 with h | h
        · exact absurd (Int64.lt_iff_toInt_lt.mpr (by simpa using h)) hrneg
        · exact h
      rw [Int.natAbs_of_nonneg hri]
  have hres : r.toInt.natAbs.toUInt64.toNat = r.toInt.natAbs := UInt64.toNat_ofNat_of_lt hb
  rw [STAmount.toRat_of_offset_zero _ rfl]
  simp only [STAmount.signedDrops]
  rw [hres, hsigned]

/-- The `Int64` view of `signedDrops` is exact for any magnitude in `Int64` range
(`< 2^63`). -/
lemma STAmount.signedDrops_toInt64_toInt_of_lt (s : STAmount)
    (h_hi : s.mValue.toNat < 2 ^ 63) :
    s.signedDrops.toInt64.toInt = s.signedDrops := by
  apply Int64.toInt_ofInt_of_le
  · unfold STAmount.signedDrops; split <;> omega
  · unfold STAmount.signedDrops; split <;> omega

/-- The `Int64` view of `signedDrops` is exact for native magnitudes
(`≤ kMaxNativeN = 10^17 < 2^63`). -/
lemma STAmount.signedDrops_toInt64_toInt_native (s : STAmount)
    (h_hi : s.mValue.toNat ≤ kMaxNativeN) :
    s.signedDrops.toInt64.toInt = s.signedDrops :=
  STAmount.signedDrops_toInt64_toInt_of_lt s (by unfold kMaxNativeN at h_hi; omega)

/-- `signedDrops` magnitude equals the stored `mValue`. -/
lemma STAmount.signedDrops_bounds (s : STAmount) :
    -(s.mValue.toNat : ℤ) ≤ s.signedDrops ∧ s.signedDrops ≤ (s.mValue.toNat : ℤ) := by
  unfold STAmount.signedDrops; split <;> omega

/-- The native `canonicalize` round-trip is **exact** on an integral (offset-0,
in-range) native amount: the value passes through `to_rep` unchanged. -/
lemma STAmount.canonicalize_native_toRat (s result : STAmount) (mode : rounding_mode)
    (hnative : s.native = true) (hoff : s.mOffset = 0)
    (hrange : s.mValue.toNat ≤ kMaxNativeN)
    (hok : s.canonicalize mode = .ok result) :
    result.toRat = s.toRat := by
  have hint : s.integral = true := by
    unfold STAmount.integral; unfold STAmount.native at hnative
    cases hm : s.mAsset with
    | issue i => rw [hm] at hnative; simpa [Asset.isNative, Asset.integral, Issue.integral] using hnative
    | mptIssue m => rw [hm] at hnative; simp [Asset.isNative, MPTIssue.native] at hnative
  have hval_le : s.mValue.toNat ≤ maxRep.toNat := by
    have hk : kMaxNativeN ≤ maxRep.toNat := by decide
    omega
  have hval_lt : s.mValue.toNat < 2 ^ 64 := by
    have hk : kMaxNativeN < 2 ^ 64 := by decide
    omega
  rw [STAmount.canonicalize, if_pos hint] at hok
  by_cases hz : s.mValue == 0
  · rw [if_pos (by rw [hz]; rfl)] at hok
    rw [beq_iff_eq] at hz
    rw [Except.ok.inj hok.symm, STAmount.toRat_of_offset_zero _ rfl,
        STAmount.toRat_of_offset_zero s hoff]
    simp only [STAmount.signedDrops]; rw [hz]; simp
  · have hz' : (s.mValue == 0) = false := by simpa using hz
    simp only [hoff, hz', hnative, if_true, Bool.false_or, Bool.false_eq_true,
      Bool.not_true, Bool.false_and, Bool.true_and, gt_iff_lt, Int.reduceLT, Int.reduceLE,
      decide_false, if_false, XRPAmount.ofNumber, XRPAmount.value] at hok
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

/-- Value of a zero-magnitude offset-0 STAmount is `0`. -/
lemma STAmount.toRat_zero_aux (s : STAmount) (ho : s.mOffset = 0) (h : s.mValue == 0) :
    s.toRat = 0 := by
  rw [STAmount.toRat_of_offset_zero s ho]
  have : s.mValue.toNat = 0 := by rw [beq_iff_eq] at h; rw [h]; rfl
  unfold STAmount.signedDrops; rw [this]; simp

/-- Native (XRP) addition is **exact**: when both operands are well-formed native
amounts (`NativeCanonical`), the result represents `v1 + v2` with no rounding. -/
theorem STAmount.operator_add_native_exact (v1 v2 result : STAmount) (mode : rounding_mode)
    (hc1 : v1.NativeCanonical) (hc2 : v2.NativeCanonical)
    (hok : STAmount.operator_add v1 v2 mode = .ok result) :
    result.toRat = v1.toRat + v2.toRat := by
  obtain ⟨hv1, ho1, hb1⟩ := hc1
  obtain ⟨hv2, ho2, hb2⟩ := hc2
  have hcmp : STAmount.areComparable v1 v2 = true := by
    unfold STAmount.areComparable; rw [hv1, hv2]; decide
  have hxrp : (xrpIssue).isXRP = true := by decide
  rw [STAmount.operator_add, if_neg (by rw [hcmp]; decide)] at hok
  by_cases hzv2 : v2.mValue == 0
  · -- branch A: `v2 = 0`, result is `v1`
    rw [if_pos hzv2] at hok
    rw [Except.ok.inj hok.symm, STAmount.toRat_zero_aux v2 ho2 hzv2]; ring
  rw [if_neg hzv2] at hok
  by_cases hzv1 : v1.mValue == 0
  · -- branch B: `v1 = 0`, result is `canonicalize v2` (which preserves the value)
    rw [if_pos hzv1] at hok
    have hunch : STAmount.unchecked v1.mAsset v2.mValue v2.mOffset v2.mIsNegative = v2 := by
      rw [hv1, ← hv2]; rfl
    rw [STAmount.checked, hunch] at hok
    have hv2native : v2.native = true := by rw [STAmount.native, hv2]; decide
    rw [STAmount.toRat_zero_aux v1 ho1 hzv1, zero_add]
    exact STAmount.canonicalize_native_toRat v2 result mode hv2native ho2 hb2 hok
  -- branch C: both nonzero, native main path
  rw [if_neg hzv1, hv1] at hok
  simp only [xrpAsset, hxrp, if_true] at hok
  rw [Except.ok.inj hok.symm]
  -- Both drops convert exactly; the sum does not overflow.
  have hsd1 : v1.signedDrops.toInt64.toInt = v1.signedDrops :=
    STAmount.signedDrops_toInt64_toInt_native v1 hb1
  have hsd2 : v2.signedDrops.toInt64.toInt = v2.signedDrops :=
    STAmount.signedDrops_toInt64_toInt_native v2 hb2
  have hbd1 : |v1.signedDrops| ≤ (kMaxNativeN : ℤ) := by
    unfold STAmount.signedDrops; split <;> [rw [abs_neg]; skip] <;>
      rw [abs_of_nonneg (by positivity)] <;> exact_mod_cast hb1
  have hbd2 : |v2.signedDrops| ≤ (kMaxNativeN : ℤ) := by
    unfold STAmount.signedDrops; split <;> [rw [abs_neg]; skip] <;>
      rw [abs_of_nonneg (by positivity)] <;> exact_mod_cast hb2
  have hsum : (v1.signedDrops.toInt64 + v2.signedDrops.toInt64).toInt
      = v1.signedDrops + v2.signedDrops := by
    rw [toInt_add_of_bounds, hsd1, hsd2]
    · rw [hsd1, hsd2]
      have := abs_le.mp hbd1; have := abs_le.mp hbd2; unfold kMaxNativeN at *; omega
    · rw [hsd1, hsd2]
      have := abs_le.mp hbd1; have := abs_le.mp hbd2; unfold kMaxNativeN at *; omega
  -- Evaluate both sides as signed drops.
  have hlo : -(2 ^ 63 : ℤ) < (v1.signedDrops.toInt64 + v2.signedDrops.toInt64).toInt := by
    rw [hsum]
    have := abs_le.mp hbd1; have := abs_le.mp hbd2; unfold kMaxNativeN at *; omega
  rw [STAmount.ofNativeInt64_toRat _ hlo, hsum,
      STAmount.toRat_of_offset_zero v1 ho1, STAmount.toRat_of_offset_zero v2 ho2]
  push_cast; ring

end XRPL.Model.Protocol
