import XRPL.Properties.Protocol.STAmount.Sub.Common.Sub
import XRPL.Properties.Protocol.STAmount.Add.RoundsWithin
import XRPL.Properties.Protocol.STAmount.Common.IOUWitnessTraces
import XRPL.Properties.Protocol.STAmount.Common.IOUDirectedWitnesses

/-! # Proof bodies for the IOU subtraction `RoundsWithin` headlines. Subtraction reduces
to addition via `v1 - v2 = v1 + (-v2)`. The thin headlines live in `Sub.RoundsWithin`. -/

namespace XRPL.Model.Protocol

/-- Proof of `operator_sub_rounds_iou` (IOU subtraction, `to_nearest`). -/
theorem STAmount.operator_sub_rounds_iou_proof (v1 v2 result : STAmount) (iss : Issue)
    (hv1 : v1.mAsset = .issue iss) (_hv2 : v2.mAsset = .issue iss) (h_xrp : iss.isXRP = false)
    (hc1 : v1.IOUCanonical) (hc2 : v2.IOUCanonical)
    (h_truth_ne : v1.toRat - v2.toRat ≠ 0)
    (hok : STAmount.operator_sub v1 v2 .to_nearest = .ok result)
    (hresult : result.mValue ≠ 0) :
    RoundsWithin result (v1.toRat - v2.toRat) .to_nearest IOUAmount.εToNearest := by
  have htruth : v1.toRat + (v2.operator_neg).toRat = v1.toRat - v2.toRat := by
    rw [STAmount.operator_neg_toRat]; ring
  have hok' : STAmount.operator_add v1 v2.operator_neg .to_nearest = .ok result := hok
  rw [← htruth]
  exact STAmount.operator_add_rounds_iou v1 v2.operator_neg result iss hv1 h_xrp hc1
    hc2.operator_neg (by rw [htruth]; exact h_truth_ne) hok' hresult

/-- Proof of `operator_sub_rounds_iou_witness`. -/
theorem STAmount.operator_sub_rounds_iou_witness_proof :
    ∃ (v1 v2 result : STAmount) (iss : Issue),
      v1.mAsset = .issue iss ∧ v2.mAsset = .issue iss ∧ iss.isXRP = false ∧
      v1.IOUCanonical ∧ v2.IOUCanonical ∧ v1.toRat - v2.toRat ≠ 0 ∧
      STAmount.operator_sub v1 v2 .to_nearest = .ok result ∧ result.mValue ≠ 0 ∧
      RoundsWithinWitness result (v1.toRat - v2.toRat) (4 / 10 ^ 16 : ℚ) := by
  have h1 : (⟨.issue noIssue, 5000000000000003, 0, false⟩ : STAmount).toRat = 5000000000000003 := by
    rw [STAmount.toRat_signed]
    norm_num [show ((5000000000000003 : UInt64).toNat : ℚ) = 5000000000000003 by norm_cast]
  have h2 : (⟨.issue noIssue, 5000000000000002, 0, true⟩ : STAmount).toRat = -5000000000000002 := by
    rw [STAmount.toRat_signed]
    norm_num [show ((5000000000000002 : UInt64).toNat : ℚ) = 5000000000000002 by norm_cast]
  have hr : (⟨.issue noIssue, 1000000000000000, 1, false⟩ : STAmount).toRat = 10000000000000000 := by
    rw [STAmount.toRat_signed]
    norm_num [show ((1000000000000000 : UInt64).toNat : ℚ) = 1000000000000000 by norm_cast]
  have hok : STAmount.operator_sub ⟨.issue noIssue, 5000000000000003, 0, false⟩
      ⟨.issue noIssue, 5000000000000002, 0, true⟩ .to_nearest
      = .ok ⟨.issue noIssue, 1000000000000000, 1, false⟩ := STAmount.operator_add_iou_witness_eq
  refine ⟨⟨.issue noIssue, 5000000000000003, 0, false⟩, ⟨.issue noIssue, 5000000000000002, 0, true⟩,
          ⟨.issue noIssue, 1000000000000000, 1, false⟩, noIssue, rfl, rfl, by decide,
          ⟨by decide, by decide, by decide, by decide, by decide, by decide⟩,
          ⟨by decide, by decide, by decide, by decide, by decide, by decide⟩, ?_,
          hok, by decide, ?_⟩
  · rw [h1, h2]; norm_num
  · unfold RoundsWithinWitness
    rw [show RatValued.toRat (⟨.issue noIssue, 1000000000000000, 1, false⟩ : STAmount)
          = (⟨.issue noIssue, 1000000000000000, 1, false⟩ : STAmount).toRat from rfl, hr, h1, h2]
    norm_num

/-- IOU subtraction in any directed mode: `v1 − v2 = v1 + (−v2)`, so the directed addition
bound applies. The `to_nearest` re-round inside `−v2` is value-exact. (Shared by the three
directed `Sub.RoundsWithin` headlines.) -/
theorem STAmount.operator_sub_iou_rounds_directed (v1 v2 result : STAmount) (iss : Issue)
    (mode : rounding_mode)
    (hv1 : v1.mAsset = .issue iss) (_hv2 : v2.mAsset = .issue iss) (h_xrp : iss.isXRP = false)
    (hc1 : v1.IOUCanonical) (hc2 : v2.IOUCanonical)
    (h_truth_ne : v1.toRat - v2.toRat ≠ 0)
    (hok : STAmount.operator_sub v1 v2 mode = .ok result)
    (hresult : result.mValue ≠ 0) :
    RoundsWithin result (v1.toRat - v2.toRat) mode IOUAmount.εDirected := by
  have htruth : v1.toRat + (v2.operator_neg).toRat = v1.toRat - v2.toRat := by
    rw [STAmount.operator_neg_toRat]; ring
  have hok' : STAmount.operator_add v1 v2.operator_neg mode = .ok result := hok
  rw [← htruth]
  exact STAmount.operator_add_iou_rounds_directed v1 v2.operator_neg result iss mode hv1 h_xrp hc1
    hc2.operator_neg (by rw [htruth]; exact h_truth_ne) hok' hresult

end XRPL.Model.Protocol
