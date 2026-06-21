import Mathlib.Tactic
import XRPL.Model.Protocol.XRPAmount
import XRPL.Properties.Protocol.XRPAmount.Common.ToRatLemmas

/-! # Proof bodies for the `XRPAmount` accessor correctness headlines.

The `value` accessor is faithful (its integer cast is `toRat`) and `toBool` decides
non-zeroness. Mirrors `Number/Accessors/`. The thin headlines live in
`XRPAmount.Accessors.Accessors`. -/

namespace XRPL.Model.Protocol

/-- **`value` is faithful**: the integer cast of `value` is `toRat`. -/
theorem XRPAmount.value_toRat_proof (x : XRPAmount) :
    ((XRPAmount.value x).toInt : ℚ) = x.toRat := rfl

/-- **`toBool` decides non-zeroness.** -/
theorem XRPAmount.toBool_iff_proof (x : XRPAmount) :
    XRPAmount.toBool x = true ↔ x.toRat ≠ 0 := by
  have hz : x.toRat = 0 ↔ x.drops_ = 0 := by
    have h := XRPAmount.toRat_eq_int_iff x 0
    rwa [show ((0 : Int64).toInt : ℚ) = 0 from by
      norm_num [show (0 : Int64).toInt = 0 from by decide]] at h
  unfold XRPAmount.toBool
  rw [bne_iff_ne, ne_eq, ne_eq, hz]

end XRPL.Model.Protocol
