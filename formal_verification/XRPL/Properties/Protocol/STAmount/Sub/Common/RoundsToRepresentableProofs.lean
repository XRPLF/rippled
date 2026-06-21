import XRPL.Properties.Protocol.STAmount.Sub.Common.Sub
import XRPL.Properties.Protocol.STAmount.Add.RoundsToRepresentable

/-! # Proof bodies for the IOU subtraction discrete/ULP (`RoundsToRepresentableWithin`)
headlines. Subtraction reduces to addition via `v1 - v2 = v1 + (-v2)`. The thin headlines
live in `Sub.RoundsToRepresentable`. -/

namespace XRPL.Model.Protocol

/-- Proof of `operator_sub_repr_iou` (IOU subtraction within 1 ULP, `to_nearest`). -/
theorem STAmount.operator_sub_repr_iou_proof (v1 v2 result : STAmount) (iss : Issue)
    (hv1 : v1.mAsset = .issue iss) (_hv2 : v2.mAsset = .issue iss) (h_xrp : iss.isXRP = false)
    (hc1 : v1.IOUCanonical) (hc2 : v2.IOUCanonical)
    (h_truth_ne : v1.toRat - v2.toRat ≠ 0)
    (hok : STAmount.operator_sub v1 v2 .to_nearest = .ok result) (hresult : result.mValue ≠ 0) :
    STAmount.RoundsToRepresentableWithin result (v1.toRat - v2.toRat) 1 .to_nearest := by
  have htruth : v1.toRat + (v2.operator_neg).toRat = v1.toRat - v2.toRat := by
    rw [STAmount.operator_neg_toRat]; ring
  have hok' : STAmount.operator_add v1 v2.operator_neg .to_nearest = .ok result := hok
  rw [← htruth]
  exact STAmount.operator_add_repr_iou v1 v2.operator_neg result iss hv1 h_xrp hc1
    hc2.operator_neg (by rw [htruth]; exact h_truth_ne) hok' hresult

/-- Proof of `operator_sub_repr_iou_directed` (IOU subtraction within 2 ULP, directed
modes, any sign). -/
theorem STAmount.operator_sub_repr_iou_directed_proof (v1 v2 result : STAmount) (iss : Issue)
    (mode : rounding_mode)
    (hv1 : v1.mAsset = .issue iss) (_hv2 : v2.mAsset = .issue iss) (h_xrp : iss.isXRP = false)
    (hc1 : v1.IOUCanonical) (hc2 : v2.IOUCanonical)
    (h_truth_ne : v1.toRat - v2.toRat ≠ 0)
    (hok : STAmount.operator_sub v1 v2 mode = .ok result) (hresult : result.mValue ≠ 0) :
    STAmount.RoundsToRepresentableWithin result (v1.toRat - v2.toRat) 2 mode := by
  have htruth : v1.toRat + (v2.operator_neg).toRat = v1.toRat - v2.toRat := by
    rw [STAmount.operator_neg_toRat]; ring
  have hok' : STAmount.operator_add v1 v2.operator_neg mode = .ok result := hok
  rw [← htruth]
  exact STAmount.operator_add_repr_iou_directed v1 v2.operator_neg result iss mode hv1 h_xrp hc1
    hc2.operator_neg (by rw [htruth]; exact h_truth_ne) hok' hresult

end XRPL.Model.Protocol
