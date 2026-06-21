import XRPL.Properties.Protocol.STAmount.Add.Common.Native
import XRPL.Properties.Protocol.STAmount.Add.Common.MPT
import XRPL.Properties.Protocol.STAmount.Add.Common.IOU
import XRPL.Properties.Protocol.STAmount.Add.Common.DirectedSupport
import XRPL.Properties.Protocol.STAmount.Add.Common.RoundsWithinProofs
import XRPL.Properties.Protocol.STAmount.Common.IOUWitnessTraces
import XRPL.Properties.Protocol.STAmount.Common.IOUDirectedWitnesses
import XRPL.Properties.Protocol.IOUAmount.Common.Defs

namespace XRPL.Model.Protocol

/-- Native (XRP) addition is exact. -/
theorem STAmount.operator_add_rounds_native (v1 v2 result : STAmount) (mode : rounding_mode)
    (hc1 : v1.NativeCanonical) (hc2 : v2.NativeCanonical)
    (hok : STAmount.operator_add v1 v2 mode = .ok result) :
    RoundsWithin result (v1.toRat + v2.toRat) mode 0 :=
  RoundsWithin_of_eq result (v1.toRat + v2.toRat) mode
    (STAmount.operator_add_native_exact v1 v2 result mode hc1 hc2 hok)

/-- MPT addition is exact.
`hsum` keeps the magnitude sum within `maxMPTokenAmount`, so the `Int64` add cannot overflow
(an MPT amount can be as large as `maxMPTokenAmount`, so two of them otherwise could). Safe:
an issuance caps total outstanding at `maxMPTokenAmount`, so two balances of the same MPT
sum within it. -/
theorem STAmount.operator_add_rounds_mpt (v1 v2 result : STAmount) (mode : rounding_mode)
    (mpt : MPTIssue) (hv1 : v1.mAsset = .mptIssue mpt)
    (hc1 : v1.MPTCanonical) (hc2 : v2.MPTCanonical)
    (hsum : v1.mValue.toNat + v2.mValue.toNat ≤ maxMPTokenAmount)
    (hok : STAmount.operator_add v1 v2 mode = .ok result) :
    RoundsWithin result (v1.toRat + v2.toRat) mode 0 :=
  RoundsWithin_of_eq result (v1.toRat + v2.toRat) mode
    (STAmount.operator_add_mpt_exact v1 v2 result mode mpt hv1 hc1 hc2 hsum hok)

/-- **IOU addition rounds within `IOUAmount.εToNearest`.** -/
theorem STAmount.operator_add_rounds_iou (v1 v2 result : STAmount) (iss : Issue)
    (hv1 : v1.mAsset = .issue iss) (h_xrp : iss.isXRP = false)
    (hc1 : v1.IOUCanonical) (hc2 : v2.IOUCanonical)
    (h_truth_ne : v1.toRat + v2.toRat ≠ 0)
    (hok : STAmount.operator_add v1 v2 .to_nearest = .ok result)
    (hresult : result.mValue ≠ 0) :
    RoundsWithin result (v1.toRat + v2.toRat) .to_nearest IOUAmount.εToNearest :=
  STAmount.operator_add_iou_rel_error v1 v2 result iss hv1 h_xrp hc1 hc2 h_truth_ne hok hresult

/-- **Tightness witness for the IOU addition bound.** -/
theorem STAmount.operator_add_rounds_iou_witness :
    ∃ (v1 v2 result : STAmount) (iss : Issue),
      v1.mAsset = .issue iss ∧ v2.mAsset = .issue iss ∧ iss.isXRP = false ∧
      v1.IOUCanonical ∧ v2.IOUCanonical ∧ v1.toRat + v2.toRat ≠ 0 ∧
      STAmount.operator_add v1 v2 .to_nearest = .ok result ∧ result.mValue ≠ 0 ∧
      RoundsWithinWitness result (v1.toRat + v2.toRat) (4 / 10 ^ 16 : ℚ) :=
  STAmount.operator_add_rounds_iou_witness_proof

/-- **IOU addition, `downward`:  rounding within `IOUAmount.εDirected`.** -/
theorem STAmount.operator_add_rounds_iou_downward (v1 v2 result : STAmount) (iss : Issue)
    (hv1 : v1.mAsset = .issue iss) (h_xrp : iss.isXRP = false)
    (hc1 : v1.IOUCanonical) (hc2 : v2.IOUCanonical)
    (h_truth_ne : v1.toRat + v2.toRat ≠ 0)
    (hok : STAmount.operator_add v1 v2 .downward = .ok result)
    (hresult : result.mValue ≠ 0) :
    RoundsWithin result (v1.toRat + v2.toRat) .downward IOUAmount.εDirected :=
  STAmount.operator_add_iou_rounds_directed v1 v2 result iss .downward hv1 h_xrp hc1 hc2
    h_truth_ne hok hresult

/-- **IOU addition, `upward`:  rounding within `IOUAmount.εDirected`.** -/
theorem STAmount.operator_add_rounds_iou_upward (v1 v2 result : STAmount) (iss : Issue)
    (hv1 : v1.mAsset = .issue iss) (h_xrp : iss.isXRP = false)
    (hc1 : v1.IOUCanonical) (hc2 : v2.IOUCanonical)
    (h_truth_ne : v1.toRat + v2.toRat ≠ 0)
    (hok : STAmount.operator_add v1 v2 .upward = .ok result)
    (hresult : result.mValue ≠ 0) :
    RoundsWithin result (v1.toRat + v2.toRat) .upward IOUAmount.εDirected :=
  STAmount.operator_add_iou_rounds_directed v1 v2 result iss .upward hv1 h_xrp hc1 hc2
    h_truth_ne hok hresult

/-- **IOU addition, `towards_zero`: rounding within `IOUAmount.εDirected`.** -/
theorem STAmount.operator_add_rounds_iou_towards_zero (v1 v2 result : STAmount) (iss : Issue)
    (hv1 : v1.mAsset = .issue iss) (h_xrp : iss.isXRP = false)
    (hc1 : v1.IOUCanonical) (hc2 : v2.IOUCanonical)
    (h_truth_ne : v1.toRat + v2.toRat ≠ 0)
    (hok : STAmount.operator_add v1 v2 .towards_zero = .ok result)
    (hresult : result.mValue ≠ 0) :
    RoundsWithin result (v1.toRat + v2.toRat) .towards_zero IOUAmount.εDirected :=
  STAmount.operator_add_iou_rounds_directed v1 v2 result iss .towards_zero hv1 h_xrp hc1 hc2
    h_truth_ne hok hresult

/-- **Tightness witness, IOU addition `downward`.** -/
theorem STAmount.operator_add_rounds_iou_downward_witness :
    ∃ (v1 v2 result : STAmount) (iss : Issue),
      v1.mAsset = .issue iss ∧ v2.mAsset = .issue iss ∧ iss.isXRP = false ∧
      v1.IOUCanonical ∧ v2.IOUCanonical ∧ v1.toRat + v2.toRat ≠ 0 ∧
      STAmount.operator_add v1 v2 .downward = .ok result ∧ result.mValue ≠ 0 ∧
      RoundsWithinWitness result (v1.toRat + v2.toRat) (4 / 10 ^ 16 : ℚ) :=
  add_dir_wit_core .downward 1000000000000000 (by decide) (Or.inl rfl)

/-- **Tightness witness, IOU addition `upward`.** -/
theorem STAmount.operator_add_rounds_iou_upward_witness :
    ∃ (v1 v2 result : STAmount) (iss : Issue),
      v1.mAsset = .issue iss ∧ v2.mAsset = .issue iss ∧ iss.isXRP = false ∧
      v1.IOUCanonical ∧ v2.IOUCanonical ∧ v1.toRat + v2.toRat ≠ 0 ∧
      STAmount.operator_add v1 v2 .upward = .ok result ∧ result.mValue ≠ 0 ∧
      RoundsWithinWitness result (v1.toRat + v2.toRat) (4 / 10 ^ 16 : ℚ) :=
  add_dir_wit_core .upward 1000000000000001 (by decide) (Or.inr (Or.inl rfl))

/-- **Tightness witness, IOU addition `towards_zero`.** -/
theorem STAmount.operator_add_rounds_iou_towards_zero_witness :
    ∃ (v1 v2 result : STAmount) (iss : Issue),
      v1.mAsset = .issue iss ∧ v2.mAsset = .issue iss ∧ iss.isXRP = false ∧
      v1.IOUCanonical ∧ v2.IOUCanonical ∧ v1.toRat + v2.toRat ≠ 0 ∧
      STAmount.operator_add v1 v2 .towards_zero = .ok result ∧ result.mValue ≠ 0 ∧
      RoundsWithinWitness result (v1.toRat + v2.toRat) (4 / 10 ^ 16 : ℚ) :=
  add_dir_wit_core .towards_zero 1000000000000000 (by decide) (Or.inr (Or.inr rfl))

end XRPL.Model.Protocol
