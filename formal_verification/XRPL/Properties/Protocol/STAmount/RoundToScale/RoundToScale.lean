import XRPL.Properties.Protocol.STAmount.RoundToScale.Common.Proofs

/-! # `STAmount.roundToScale` is correctly rounded onto the scale grid

`roundToScale` quantizes an IOU `STAmount` onto the `10^s · ℤ` grid faithfully (in every
mode). Built from the `roundToScale_sum_spec` workhorse in `RoundToScale.Common.Sum`. -/

namespace XRPL.Model.Protocol

/-- **`STAmount.roundToScale` rounds `value.toRat` onto the `10^s` grid faithfully.** -/
theorem STAmount.roundToScale_rounded (value result : STAmount) (s : ℤ)
    (mode : rounding_mode)
    (hc : value.IOUCanonical)
    (h_s : (-81 : ℤ) ≤ s) (h_s_hi : s ≤ 80)
    (hok : STAmount.roundToScale value s mode = .ok result) :
    RoundsToRepresentableAt result value.toRat s mode :=
  STAmount.roundToScale_rounded_proof value result s mode hc h_s h_s_hi hok

end XRPL.Model.Protocol
