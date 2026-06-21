import XRPL.Properties.Protocol.STAmount.Add.Common.Native
import XRPL.Properties.Protocol.STAmount.Add.Common.MPT
import XRPL.Properties.Protocol.STAmount.Add.Common.RoundsToRepresentableProofs

namespace XRPL.Model.Protocol

/-! # Addition, discrete/ULP -/

/-- Native (XRP) addition is exact. -/
theorem STAmount.operator_add_repr_native (v1 v2 result : STAmount) (mode : rounding_mode)
    (hc1 : v1.NativeCanonical) (hc2 : v2.NativeCanonical)
    (hok : STAmount.operator_add v1 v2 mode = .ok result) :
    STAmount.RoundsToRepresentableWithin result (v1.toRat + v2.toRat) 0 mode :=
  RoundsToRepresentableWithin_of_eq result (v1.toRat + v2.toRat) mode
    (STAmount.operator_add_native_exact v1 v2 result mode hc1 hc2 hok)

/-- MPT addition lands is exact. -/
theorem STAmount.operator_add_repr_mpt (v1 v2 result : STAmount) (mode : rounding_mode)
    (mpt : MPTIssue) (hv1 : v1.mAsset = .mptIssue mpt)
    (hc1 : v1.MPTCanonical) (hc2 : v2.MPTCanonical)
    (hsum : v1.mValue.toNat + v2.mValue.toNat ≤ maxMPTokenAmount)
    (hok : STAmount.operator_add v1 v2 mode = .ok result) :
    STAmount.RoundsToRepresentableWithin result (v1.toRat + v2.toRat) 0 mode :=
  RoundsToRepresentableWithin_of_eq result (v1.toRat + v2.toRat) mode
    (STAmount.operator_add_mpt_exact v1 v2 result mode mpt hv1 hc1 hc2 hsum hok)

/-- **IOU addition lands within `1` ULP of `v1 + v2` (`to_nearest`).** -/
theorem STAmount.operator_add_repr_iou (v1 v2 result : STAmount) (iss : Issue)
    (hv1 : v1.mAsset = .issue iss) (h_xrp : iss.isXRP = false)
    (hc1 : v1.IOUCanonical) (hc2 : v2.IOUCanonical)
    (h_truth_ne : v1.toRat + v2.toRat ≠ 0)
    (hok : STAmount.operator_add v1 v2 .to_nearest = .ok result) (hresult : result.mValue ≠ 0) :
    STAmount.RoundsToRepresentableWithin result (v1.toRat + v2.toRat) 1 .to_nearest :=
  STAmount.operator_add_repr_iou_proof v1 v2 result iss hv1 h_xrp hc1 hc2 h_truth_ne hok hresult

/-- **IOU addition lands within `2` ULP of `v1 + v2` (directed modes)** -/
theorem STAmount.operator_add_repr_iou_directed (v1 v2 result : STAmount) (iss : Issue)
    (mode : rounding_mode)
    (hv1 : v1.mAsset = .issue iss) (h_xrp : iss.isXRP = false)
    (hc1 : v1.IOUCanonical) (hc2 : v2.IOUCanonical)
    (h_truth_ne : v1.toRat + v2.toRat ≠ 0)
    (hok : STAmount.operator_add v1 v2 mode = .ok result) (hresult : result.mValue ≠ 0) :
    STAmount.RoundsToRepresentableWithin result (v1.toRat + v2.toRat) 2 mode :=
  STAmount.operator_add_repr_iou_directed_proof v1 v2 result iss mode hv1 h_xrp hc1 hc2
    h_truth_ne hok hresult

end XRPL.Model.Protocol
