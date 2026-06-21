import XRPL.Properties.Protocol.STAmount.Common.IOUWitnessTraces
import XRPL.Properties.Protocol.STAmount.Common.IOUDirectedWitnesses

/-! # Proof body for the IOU multiplication `to_nearest` tightness witness. The thin
headline lives in `Mul.RoundsWithin`. -/

namespace XRPL.Model.Protocol

/-- Proof of `operator_mul_rounds_iou_witness`. -/
theorem STAmount.operator_mul_rounds_iou_witness_proof :
    ∃ (v1 v2 result : STAmount) (iss : Issue),
      v1.mAsset = .issue iss ∧ v2.mAsset = .issue iss ∧ iss.isXRP = false ∧
      (Asset.issue iss).holdsIssue = true ∧ (Asset.issue iss).isNative = false ∧
      v1.IOUCanonical ∧ v2.IOUCanonical ∧
      STAmount.multiply v1 v2 (.issue iss) .to_nearest = .ok result ∧ result.mValue ≠ 0 ∧
      RoundsWithinWitness result (v1.toRat * v2.toRat) (4 / 10 ^ 16 : ℚ) := by
  have h1 : (⟨.issue noIssue, 2000000000000001, 0, false⟩ : STAmount).toRat = 2000000000000001 := by
    rw [STAmount.toRat_signed]
    norm_num [show ((2000000000000001 : UInt64).toNat : ℚ) = 2000000000000001 by norm_cast]
  have h2 : (⟨.issue noIssue, 5000000000000000, -15, false⟩ : STAmount).toRat = 5 := by
    rw [STAmount.toRat_signed]
    norm_num [show ((5000000000000000 : UInt64).toNat : ℚ) = 5000000000000000 by norm_cast]
  have hr : (⟨.issue noIssue, 1000000000000000, 1, false⟩ : STAmount).toRat = 10000000000000000 := by
    rw [STAmount.toRat_signed]
    norm_num [show ((1000000000000000 : UInt64).toNat : ℚ) = 1000000000000000 by norm_cast]
  refine ⟨⟨.issue noIssue, 2000000000000001, 0, false⟩, ⟨.issue noIssue, 5000000000000000, -15, false⟩,
          ⟨.issue noIssue, 1000000000000000, 1, false⟩, noIssue, rfl, rfl, by decide, by decide, by decide,
          ⟨by decide, by decide, by decide, by decide, by decide, by decide⟩,
          ⟨by decide, by decide, by decide, by decide, by decide, by decide⟩,
          STAmount.multiply_iou_witness_eq, by decide, ?_⟩
  unfold RoundsWithinWitness
  rw [show RatValued.toRat (⟨.issue noIssue, 1000000000000000, 1, false⟩ : STAmount)
        = (⟨.issue noIssue, 1000000000000000, 1, false⟩ : STAmount).toRat from rfl, hr, h1, h2]
  norm_num

end XRPL.Model.Protocol
