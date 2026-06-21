import XRPL.Properties.Protocol.XRPAmount.MulRatio.Common.Proofs

/-! # `mulRatio` ceil direction (`roundUp = true`).

The `roundUp = true` specialization of the unified `mulRatio_rounds_proof`: the result is
either saturated to `Int64.minValue` or **ceiled**: the smallest representable value not
below `amt · num / den`. (Direction analog of `Number`'s per-mode `Upward`.) -/

namespace XRPL.Model.Protocol

/-- **`mulRatio` ceils when `roundUp = true`.** -/
theorem XRPAmount.mulRatio_rounds_ceil_proof (amt : XRPAmount) (num den : UInt32)
    (result : XRPAmount)
    (hok : XRPAmount.mulRatio amt num den true = .ok result) :
    result.drops_ = Int64.minValue ∨
      (amt.toRat * (num.toNat : ℚ) / (den.toNat : ℚ) ≤ result.toRat ∧
       result.toRat - 1 < amt.toRat * (num.toNat : ℚ) / (den.toNat : ℚ)) := by
  simpa using XRPAmount.mulRatio_rounds_proof amt num den true result hok

end XRPL.Model.Protocol
