import XRPL.Properties.Protocol.Number.Add.RoundsToRepresentable
import XRPL.Properties.Protocol.Number.Common.ToRatLemmas


namespace XRPL.Model.Protocol

theorem operator_sub_rounded_downward_proof (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hok : Number.operator_sub x y .downward = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    Number.RoundsToRepresentable result (x.toRat - y.toRat) .downward := by
  unfold Number.operator_sub at hok
  have hy_neg_norm : y.operator_neg.isNormalized := Number.operator_neg_isNormalized y hy
  have h_truth_eq : x.toRat - y.toRat = x.toRat + y.operator_neg.toRat := by
    rw [Number.toRat_neg]
    ring
  rw [h_truth_eq]
  exact operator_add_rounded_downward x y.operator_neg result hx hy_neg_norm hok hresult

theorem operator_sub_rounded_upward_proof (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hok : Number.operator_sub x y .upward = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    Number.RoundsToRepresentable result (x.toRat - y.toRat) .upward := by
  unfold Number.operator_sub at hok
  have hy_neg_norm : y.operator_neg.isNormalized := Number.operator_neg_isNormalized y hy
  have h_truth_eq : x.toRat - y.toRat = x.toRat + y.operator_neg.toRat := by
    rw [Number.toRat_neg]
    ring
  rw [h_truth_eq]
  exact operator_add_rounded_upward x y.operator_neg result hx hy_neg_norm hok hresult

theorem operator_sub_rounded_towards_zero_proof (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hok : Number.operator_sub x y .towards_zero = .ok result) :
    Number.RoundsToRepresentable result (x.toRat - y.toRat) .towards_zero := by
  unfold Number.operator_sub at hok
  have hy_neg_norm : y.operator_neg.isNormalized := Number.operator_neg_isNormalized y hy
  have h_truth_eq : x.toRat - y.toRat = x.toRat + y.operator_neg.toRat := by
    rw [Number.toRat_neg]
    ring
  rw [h_truth_eq]
  exact operator_add_rounded_towards_zero x y.operator_neg result hx hy_neg_norm hok

theorem operator_sub_rounded_to_nearest_proof (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hok : Number.operator_sub x y .to_nearest = .ok result) :
    Number.RoundsToRepresentable result (x.toRat - y.toRat) .to_nearest := by
  unfold Number.operator_sub at hok
  have hy_neg_norm : y.operator_neg.isNormalized := Number.operator_neg_isNormalized y hy
  have h_truth_eq : x.toRat - y.toRat = x.toRat + y.operator_neg.toRat := by
    rw [Number.toRat_neg]
    ring
  rw [h_truth_eq]
  exact operator_add_rounded_to_nearest x y.operator_neg result hx hy_neg_norm hok

end XRPL.Model.Protocol
