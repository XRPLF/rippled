import XRPL.Properties.Protocol.MPTAmount.MulRatio.Common.Floor.Proofs
import XRPL.Properties.Protocol.MPTAmount.MulRatio.Common.Ceil.Proofs

/-! # Correctness of `MPTAmount.mulRatio` -/

namespace XRPL.Model.Protocol

/-- **`mulRatio` rounds correctly.** On a successful result the value is either saturated to
`Int64.minValue` (negative underflow) or the correctly-rounded quotient of `amt * num / den` (floor for `roundUp = false`, ceil for `roundUp = true`). -/
theorem MPTAmount.mulRatio_rounds (amt : MPTAmount) (num den : UInt32) (roundUp : Bool)
    (result : MPTAmount)
    (hok : MPTAmount.mulRatio amt num den roundUp = .ok result) :
    result.value_ = Int64.minValue ∨
      (if roundUp
       then amt.toRat * (num.toNat : ℚ) / (den.toNat : ℚ) ≤ result.toRat ∧
            result.toRat - 1 < amt.toRat * (num.toNat : ℚ) / (den.toNat : ℚ)
       else result.toRat ≤ amt.toRat * (num.toNat : ℚ) / (den.toNat : ℚ) ∧
            amt.toRat * (num.toNat : ℚ) / (den.toNat : ℚ) < result.toRat + 1) :=
  MPTAmount.mulRatio_rounds_proof amt num den roundUp result hok

/-- **`mulRatio` floors when `roundUp = false`.** -/
theorem MPTAmount.mulRatio_rounds_floor (amt : MPTAmount) (num den : UInt32)
    (result : MPTAmount)
    (hok : MPTAmount.mulRatio amt num den false = .ok result) :
    result.value_ = Int64.minValue ∨
      (result.toRat ≤ amt.toRat * (num.toNat : ℚ) / (den.toNat : ℚ) ∧
       amt.toRat * (num.toNat : ℚ) / (den.toNat : ℚ) < result.toRat + 1) :=
  MPTAmount.mulRatio_rounds_floor_proof amt num den result hok

/-- **`mulRatio` ceils when `roundUp = true`.** -/
theorem MPTAmount.mulRatio_rounds_ceil (amt : MPTAmount) (num den : UInt32)
    (result : MPTAmount)
    (hok : MPTAmount.mulRatio amt num den true = .ok result) :
    result.value_ = Int64.minValue ∨
      (amt.toRat * (num.toNat : ℚ) / (den.toNat : ℚ) ≤ result.toRat ∧
       result.toRat - 1 < amt.toRat * (num.toNat : ℚ) / (den.toNat : ℚ)) :=
  MPTAmount.mulRatio_rounds_ceil_proof amt num den result hok

end XRPL.Model.Protocol
