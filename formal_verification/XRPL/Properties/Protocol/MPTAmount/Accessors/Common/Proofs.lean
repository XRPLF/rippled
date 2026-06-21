import Mathlib.Tactic
import XRPL.Model.Protocol.MPTAmount
import XRPL.Properties.Protocol.MPTAmount.Common.ToRatLemmas

/-! # Proof bodies for the `MPTAmount` accessor correctness headlines. -/

namespace XRPL.Model.Protocol

/-- **`value` is faithful**: the integer cast of `value` is `toRat`. -/
theorem MPTAmount.value_toRat_proof (x : MPTAmount) :
    ((MPTAmount.value x).toInt : ℚ) = x.toRat := rfl

/-- **`toBool` decides non-zeroness.** -/
theorem MPTAmount.toBool_iff_proof (x : MPTAmount) :
    MPTAmount.toBool x = true ↔ x.toRat ≠ 0 := by
  have hz : x.toRat = 0 ↔ x.value_ = 0 := by
    have h := MPTAmount.toRat_eq_int_iff x 0
    rwa [show ((0 : Int64).toInt : ℚ) = 0 from by
      norm_num [show (0 : Int64).toInt = 0 from by decide]] at h
  unfold MPTAmount.toBool
  rw [bne_iff_ne, ne_eq, ne_eq, hz]

end XRPL.Model.Protocol
