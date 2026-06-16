import Mathlib.Tactic

import XRPL.Model.Protocol.Number

namespace XRPL.Model.Protocol

/-! UInt64 / UInt128 arithmetic: exact-toNat equalities under width bounds. -/

/-- Zero-extending a UInt64 to UInt128 preserves `toNat`. -/
lemma toNat_toUInt128 (a : UInt64) : (toUInt128 a).toNat = a.toNat := by
  unfold toUInt128
  rw [BitVec.zeroExtend_eq_setWidth, BitVec.toNat_setWidth]
  exact Nat.mod_eq_of_lt (by have := a.toNat_lt; omega)

/-- UInt128 product of zero-extended UInt64s, when it fits. -/
lemma uint128_of_uint64_mul_toNat
    (a b : UInt64) (h : a.toNat * b.toNat < 2 ^ 128) :
    (toUInt128 a * toUInt128 b).toNat = a.toNat * b.toNat := by
  rw [BitVec.toNat_mul_of_lt (by rw [toNat_toUInt128, toNat_toUInt128]; exact h),
      toNat_toUInt128, toNat_toUInt128]

/-- Truncating a UInt128 to UInt64 is exact when the value fits in 64 bits. -/
lemma toNat_toUInt64 {x : UInt128} (hx : x.toNat < 2 ^ 64) :
    (toUInt64 x).toNat = x.toNat := by
  change (BitVec.setWidth 64 x).toNat = x.toNat
  rw [BitVec.toNat_setWidth]
  exact Nat.mod_eq_of_lt (by omega)

end XRPL.Model.Protocol
