import XRPL.Properties.Protocol.STAmount.Sub.Common.Neg
import XRPL.Properties.Protocol.STAmount.Add.Common.Native
import XRPL.Properties.Protocol.STAmount.Add.Common.MPT

namespace XRPL.Model.Protocol

/-! ## Subtraction engines

`operator_sub v1 v2 = operator_add v1 (operator_neg v2)`, so each subtraction
exactness result is the corresponding addition engine applied to the negated
second operand (which stays canonical and keeps its magnitude). -/

/-- Native (XRP) subtraction is exact. -/
theorem STAmount.operator_sub_native_exact (v1 v2 result : STAmount) (mode : rounding_mode)
    (hc1 : v1.NativeCanonical) (hc2 : v2.NativeCanonical)
    (hok : STAmount.operator_sub v1 v2 mode = .ok result) :
    result.toRat = v1.toRat - v2.toRat := by
  rw [STAmount.operator_sub] at hok
  have hexact := STAmount.operator_add_native_exact v1 v2.operator_neg result mode
    hc1 hc2.operator_neg hok
  rw [hexact, STAmount.operator_neg_toRat]; ring

/-- MPT subtraction is exact. -/
theorem STAmount.operator_sub_mpt_exact (v1 v2 result : STAmount) (mode : rounding_mode)
    (mpt : MPTIssue) (hv1 : v1.mAsset = .mptIssue mpt)
    (hc1 : v1.MPTCanonical) (hc2 : v2.MPTCanonical)
    (hsum : v1.mValue.toNat + v2.mValue.toNat ≤ maxMPTokenAmount)
    (hok : STAmount.operator_sub v1 v2 mode = .ok result) :
    result.toRat = v1.toRat - v2.toRat := by
  rw [STAmount.operator_sub] at hok
  have hsum' : v1.mValue.toNat + v2.operator_neg.mValue.toNat ≤ maxMPTokenAmount := by
    rw [STAmount.operator_neg_mValue]; exact hsum
  have hexact := STAmount.operator_add_mpt_exact v1 v2.operator_neg result mode mpt hv1
    hc1 hc2.operator_neg hsum' hok
  rw [hexact, STAmount.operator_neg_toRat]; ring

end XRPL.Model.Protocol
