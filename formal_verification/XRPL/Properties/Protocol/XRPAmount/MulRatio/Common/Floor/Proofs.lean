import XRPL.Properties.Protocol.XRPAmount.MulRatio.Common.Proofs

/-! # `mulRatio` floor direction (`roundUp = false`).

The `roundUp = false` specialization of the unified `mulRatio_rounds_proof`: the result is
either saturated to `Int64.minValue` or **floored**: the largest representable value not
exceeding `amt · num / den`. (Direction analog of `Number`'s per-mode `Downward`.) -/

namespace XRPL.Model.Protocol

/-- **`mulRatio` floors when `roundUp = false`.** -/
theorem XRPAmount.mulRatio_rounds_floor_proof (amt : XRPAmount) (num den : UInt32)
    (result : XRPAmount)
    (hok : XRPAmount.mulRatio amt num den false = .ok result) :
    result.drops_ = Int64.minValue ∨
      (result.toRat ≤ amt.toRat * (num.toNat : ℚ) / (den.toNat : ℚ) ∧
       amt.toRat * (num.toNat : ℚ) / (den.toNat : ℚ) < result.toRat + 1) := by
  simpa using XRPAmount.mulRatio_rounds_proof amt num den false result hok

end XRPL.Model.Protocol
