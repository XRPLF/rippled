import XRPL.Properties.Protocol.STAmount.Sub.Common.RoundsWithinProofs

namespace XRPL.Model.Protocol

/-- Native (XRP) subtraction is exact. -/
theorem STAmount.operator_sub_rounds_native (v1 v2 result : STAmount) (mode : rounding_mode)
    (hc1 : v1.NativeCanonical) (hc2 : v2.NativeCanonical)
    (hok : STAmount.operator_sub v1 v2 mode = .ok result) :
    RoundsWithin result (v1.toRat - v2.toRat) mode 0 :=
  RoundsWithin_of_eq result (v1.toRat - v2.toRat) mode
    (STAmount.operator_sub_native_exact v1 v2 result mode hc1 hc2 hok)

/-- MPT subtraction is exact. -/
theorem STAmount.operator_sub_rounds_mpt (v1 v2 result : STAmount) (mode : rounding_mode)
    (mpt : MPTIssue) (hv1 : v1.mAsset = .mptIssue mpt)
    (hc1 : v1.MPTCanonical) (hc2 : v2.MPTCanonical)
    (hsum : v1.mValue.toNat + v2.mValue.toNat ≤ maxMPTokenAmount)
    (hok : STAmount.operator_sub v1 v2 mode = .ok result) :
    RoundsWithin result (v1.toRat - v2.toRat) mode 0 :=
  RoundsWithin_of_eq result (v1.toRat - v2.toRat) mode
    (STAmount.operator_sub_mpt_exact v1 v2 result mode mpt hv1 hc1 hc2 hsum hok)

/-- **IOU subtraction rounds within `IOUAmount.εToNearest`.** -/
theorem STAmount.operator_sub_rounds_iou (v1 v2 result : STAmount) (iss : Issue)
    (hv1 : v1.mAsset = .issue iss) (hv2 : v2.mAsset = .issue iss) (h_xrp : iss.isXRP = false)
    (hc1 : v1.IOUCanonical) (hc2 : v2.IOUCanonical)
    (h_truth_ne : v1.toRat - v2.toRat ≠ 0)
    (hok : STAmount.operator_sub v1 v2 .to_nearest = .ok result)
    (hresult : result.mValue ≠ 0) :
    RoundsWithin result (v1.toRat - v2.toRat) .to_nearest IOUAmount.εToNearest :=
  STAmount.operator_sub_rounds_iou_proof v1 v2 result iss hv1 hv2 h_xrp hc1 hc2 h_truth_ne hok hresult

/-- **Tightness witness for the IOU subtraction bound.** -/
theorem STAmount.operator_sub_rounds_iou_witness :
    ∃ (v1 v2 result : STAmount) (iss : Issue),
      v1.mAsset = .issue iss ∧ v2.mAsset = .issue iss ∧ iss.isXRP = false ∧
      v1.IOUCanonical ∧ v2.IOUCanonical ∧ v1.toRat - v2.toRat ≠ 0 ∧
      STAmount.operator_sub v1 v2 .to_nearest = .ok result ∧ result.mValue ≠ 0 ∧
      RoundsWithinWitness result (v1.toRat - v2.toRat) (4 / 10 ^ 16 : ℚ) :=
  STAmount.operator_sub_rounds_iou_witness_proof

/-- **IOU subtraction, `downward` within `IOUAmount.εDirected`.** -/
theorem STAmount.operator_sub_rounds_iou_downward (v1 v2 result : STAmount) (iss : Issue)
    (hv1 : v1.mAsset = .issue iss) (hv2 : v2.mAsset = .issue iss) (h_xrp : iss.isXRP = false)
    (hc1 : v1.IOUCanonical) (hc2 : v2.IOUCanonical)
    (h_truth_ne : v1.toRat - v2.toRat ≠ 0)
    (hok : STAmount.operator_sub v1 v2 .downward = .ok result)
    (hresult : result.mValue ≠ 0) :
    RoundsWithin result (v1.toRat - v2.toRat) .downward IOUAmount.εDirected :=
  STAmount.operator_sub_iou_rounds_directed v1 v2 result iss .downward hv1 hv2 h_xrp hc1 hc2
    h_truth_ne hok hresult

/-- **IOU subtraction, `upward` within `IOUAmount.εDirected`.** -/
theorem STAmount.operator_sub_rounds_iou_upward (v1 v2 result : STAmount) (iss : Issue)
    (hv1 : v1.mAsset = .issue iss) (hv2 : v2.mAsset = .issue iss) (h_xrp : iss.isXRP = false)
    (hc1 : v1.IOUCanonical) (hc2 : v2.IOUCanonical)
    (h_truth_ne : v1.toRat - v2.toRat ≠ 0)
    (hok : STAmount.operator_sub v1 v2 .upward = .ok result)
    (hresult : result.mValue ≠ 0) :
    RoundsWithin result (v1.toRat - v2.toRat) .upward IOUAmount.εDirected :=
  STAmount.operator_sub_iou_rounds_directed v1 v2 result iss .upward hv1 hv2 h_xrp hc1 hc2
    h_truth_ne hok hresult

/-- **IOU subtraction, `towards_zero` within `IOUAmount.εDirected`.** -/
theorem STAmount.operator_sub_rounds_iou_towards_zero (v1 v2 result : STAmount) (iss : Issue)
    (hv1 : v1.mAsset = .issue iss) (hv2 : v2.mAsset = .issue iss) (h_xrp : iss.isXRP = false)
    (hc1 : v1.IOUCanonical) (hc2 : v2.IOUCanonical)
    (h_truth_ne : v1.toRat - v2.toRat ≠ 0)
    (hok : STAmount.operator_sub v1 v2 .towards_zero = .ok result)
    (hresult : result.mValue ≠ 0) :
    RoundsWithin result (v1.toRat - v2.toRat) .towards_zero IOUAmount.εDirected :=
  STAmount.operator_sub_iou_rounds_directed v1 v2 result iss .towards_zero hv1 hv2 h_xrp hc1 hc2
    h_truth_ne hok hresult

/-- **Tightness witness, IOU subtraction `downward`.** -/
theorem STAmount.operator_sub_rounds_iou_downward_witness :
    ∃ (v1 v2 result : STAmount) (iss : Issue),
      v1.mAsset = .issue iss ∧ v2.mAsset = .issue iss ∧ iss.isXRP = false ∧
      v1.IOUCanonical ∧ v2.IOUCanonical ∧ v1.toRat - v2.toRat ≠ 0 ∧
      STAmount.operator_sub v1 v2 .downward = .ok result ∧ result.mValue ≠ 0 ∧
      RoundsWithinWitness result (v1.toRat - v2.toRat) (4 / 10 ^ 16 : ℚ) :=
  sub_dir_wit_core .downward 1000000000000000 (by decide) (Or.inl rfl)

/-- **Tightness witness, IOU subtraction `upward`.** -/
theorem STAmount.operator_sub_rounds_iou_upward_witness :
    ∃ (v1 v2 result : STAmount) (iss : Issue),
      v1.mAsset = .issue iss ∧ v2.mAsset = .issue iss ∧ iss.isXRP = false ∧
      v1.IOUCanonical ∧ v2.IOUCanonical ∧ v1.toRat - v2.toRat ≠ 0 ∧
      STAmount.operator_sub v1 v2 .upward = .ok result ∧ result.mValue ≠ 0 ∧
      RoundsWithinWitness result (v1.toRat - v2.toRat) (4 / 10 ^ 16 : ℚ) :=
  sub_dir_wit_core .upward 1000000000000001 (by decide) (Or.inr (Or.inl rfl))

/-- **Tightness witness, IOU subtraction `towards_zero`.** -/
theorem STAmount.operator_sub_rounds_iou_towards_zero_witness :
    ∃ (v1 v2 result : STAmount) (iss : Issue),
      v1.mAsset = .issue iss ∧ v2.mAsset = .issue iss ∧ iss.isXRP = false ∧
      v1.IOUCanonical ∧ v2.IOUCanonical ∧ v1.toRat - v2.toRat ≠ 0 ∧
      STAmount.operator_sub v1 v2 .towards_zero = .ok result ∧ result.mValue ≠ 0 ∧
      RoundsWithinWitness result (v1.toRat - v2.toRat) (4 / 10 ^ 16 : ℚ) :=
  sub_dir_wit_core .towards_zero 1000000000000000 (by decide) (Or.inr (Or.inr rfl))

end XRPL.Model.Protocol
