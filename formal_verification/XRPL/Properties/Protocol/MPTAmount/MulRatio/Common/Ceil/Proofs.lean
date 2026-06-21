import XRPL.Properties.Protocol.MPTAmount.MulRatio.Common.Proofs

/-! # `mulRatio` ceil direction (`roundUp = true`).

The `roundUp = true` specialization of the unified `mulRatio_rounds_proof`: the result is
either saturated to `Int64.minValue` or **ceiled**: the smallest representable value not
below `amt · num / den`. (Direction analog of `Number`'s per-mode `Upward`.) -/

namespace XRPL.Model.Protocol

/-- **`mulRatio` ceils when `roundUp = true`.** -/
theorem MPTAmount.mulRatio_rounds_ceil_proof (amt : MPTAmount) (num den : UInt32)
    (result : MPTAmount)
    (hok : MPTAmount.mulRatio amt num den true = .ok result) :
    result.value_ = Int64.minValue ∨
      (amt.toRat * (num.toNat : ℚ) / (den.toNat : ℚ) ≤ result.toRat ∧
       result.toRat - 1 < amt.toRat * (num.toNat : ℚ) / (den.toNat : ℚ)) := by
  simpa using MPTAmount.mulRatio_rounds_proof amt num den true result hok

end XRPL.Model.Protocol
