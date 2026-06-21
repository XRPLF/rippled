import XRPL.Properties.Protocol.STAmount.Common.IOUWitnessTraces
import XRPL.Properties.Protocol.STAmount.Common.IOUDirectedWitnesses

/-! # Proof body for the IOU addition `to_nearest` tightness witness. The thin headline
lives in `Add.RoundsWithin`. -/

namespace XRPL.Model.Protocol

/-- Proof of `operator_add_rounds_iou_witness`. -/
theorem STAmount.operator_add_rounds_iou_witness_proof :
    ∃ (v1 v2 result : STAmount) (iss : Issue),
      v1.mAsset = .issue iss ∧ v2.mAsset = .issue iss ∧ iss.isXRP = false ∧
      v1.IOUCanonical ∧ v2.IOUCanonical ∧ v1.toRat + v2.toRat ≠ 0 ∧
      STAmount.operator_add v1 v2 .to_nearest = .ok result ∧ result.mValue ≠ 0 ∧
      RoundsWithinWitness result (v1.toRat + v2.toRat) (4 / 10 ^ 16 : ℚ) := by
  have h1 : (⟨.issue noIssue, 5000000000000003, 0, false⟩ : STAmount).toRat = 5000000000000003 := by
    rw [STAmount.toRat_signed]
    norm_num [show ((5000000000000003 : UInt64).toNat : ℚ) = 5000000000000003 by norm_cast]
  have h2 : (⟨.issue noIssue, 5000000000000002, 0, false⟩ : STAmount).toRat = 5000000000000002 := by
    rw [STAmount.toRat_signed]
    norm_num [show ((5000000000000002 : UInt64).toNat : ℚ) = 5000000000000002 by norm_cast]
  have hr : (⟨.issue noIssue, 1000000000000000, 1, false⟩ : STAmount).toRat = 10000000000000000 := by
    rw [STAmount.toRat_signed]
    norm_num [show ((1000000000000000 : UInt64).toNat : ℚ) = 1000000000000000 by norm_cast]
  refine ⟨⟨.issue noIssue, 5000000000000003, 0, false⟩, ⟨.issue noIssue, 5000000000000002, 0, false⟩,
          ⟨.issue noIssue, 1000000000000000, 1, false⟩, noIssue, rfl, rfl, by decide,
          ⟨by decide, by decide, by decide, by decide, by decide, by decide⟩,
          ⟨by decide, by decide, by decide, by decide, by decide, by decide⟩, ?_,
          STAmount.operator_add_iou_witness_eq, by decide, ?_⟩
  · rw [h1, h2]; norm_num
  · unfold RoundsWithinWitness
    rw [show RatValued.toRat (⟨.issue noIssue, 1000000000000000, 1, false⟩ : STAmount)
          = (⟨.issue noIssue, 1000000000000000, 1, false⟩ : STAmount).toRat from rfl, hr, h1, h2]
    norm_num

end XRPL.Model.Protocol
