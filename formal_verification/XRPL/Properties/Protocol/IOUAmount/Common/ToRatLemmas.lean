import XRPL.Properties.Protocol.STAmount.Add.Common.IOU

/-! # `toRat` bridges for `IOUAmount`

Mirrors `Number/Common/ToRatLemmas.lean`. The decimal-float embedding identities
`IOUAmount.toRat_eq` (`toRat = mantissa · 10^exponent`) and `IOUAmount.abs_toRat_eq` were
developed alongside the IOU addition keystone and live in `STAmount.Add.Common.IOU`; this
module surfaces them under the `IOUAmount/Common/` layout for the per-area proof bodies. -/
