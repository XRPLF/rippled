import XRPL.Properties.Protocol.XRPAmount.MulRatio.Common.Floor.Proofs
import XRPL.Properties.Protocol.XRPAmount.MulRatio.Common.Ceil.Proofs

/-! # Correctness of `XRPAmount.mulRatio`
Used by the payment engine
. -/

namespace XRPL.Model.Protocol

/-- **`mulRatio` rounds correctly.** On a successful result the drops are either saturated to
`Int64.minValue` (negative underflow) or the correctly-rounded quotient of `amt * num / den` (floor for `roundUp = false`, ceil for `roundUp = true`). -/
theorem XRPAmount.mulRatio_rounds (amt : XRPAmount) (num den : UInt32) (roundUp : Bool)
    (result : XRPAmount)
    (hok : XRPAmount.mulRatio amt num den roundUp = .ok result) :
    result.drops_ = Int64.minValue ∨
      (if roundUp
       then amt.toRat * (num.toNat : ℚ) / (den.toNat : ℚ) ≤ result.toRat ∧
            result.toRat - 1 < amt.toRat * (num.toNat : ℚ) / (den.toNat : ℚ)
       else result.toRat ≤ amt.toRat * (num.toNat : ℚ) / (den.toNat : ℚ) ∧
            amt.toRat * (num.toNat : ℚ) / (den.toNat : ℚ) < result.toRat + 1) :=
  XRPAmount.mulRatio_rounds_proof amt num den roundUp result hok

/-- **`mulRatio` floors when `roundUp = false`.** -/
theorem XRPAmount.mulRatio_rounds_floor (amt : XRPAmount) (num den : UInt32)
    (result : XRPAmount)
    (hok : XRPAmount.mulRatio amt num den false = .ok result) :
    result.drops_ = Int64.minValue ∨
      (result.toRat ≤ amt.toRat * (num.toNat : ℚ) / (den.toNat : ℚ) ∧
       amt.toRat * (num.toNat : ℚ) / (den.toNat : ℚ) < result.toRat + 1) :=
  XRPAmount.mulRatio_rounds_floor_proof amt num den result hok

/-- **`mulRatio` ceils when `roundUp = true`.** -/
theorem XRPAmount.mulRatio_rounds_ceil (amt : XRPAmount) (num den : UInt32)
    (result : XRPAmount)
    (hok : XRPAmount.mulRatio amt num den true = .ok result) :
    result.drops_ = Int64.minValue ∨
      (amt.toRat * (num.toNat : ℚ) / (den.toNat : ℚ) ≤ result.toRat ∧
       result.toRat - 1 < amt.toRat * (num.toNat : ℚ) / (den.toNat : ℚ)) :=
  XRPAmount.mulRatio_rounds_ceil_proof amt num den result hok

end XRPL.Model.Protocol
