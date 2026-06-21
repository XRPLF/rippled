import XRPL.Properties.Protocol.STAmount.Mul.Common.DirectedSupport

/-! # Proof bodies for the IOU multiplication discrete/ULP (`RoundsToRepresentableWithin`)
headlines. The thin headlines live in `Mul.RoundsToRepresentable`. -/

namespace XRPL.Model.Protocol

/-- Proof of `operator_mul_repr_iou` (IOU multiply within 1 ULP, `to_nearest`). -/
theorem STAmount.operator_mul_repr_iou_proof (v1 v2 result : STAmount) (asset : Asset) (iss : Issue)
    (hv1 : v1.mAsset = .issue iss) (hv2 : v2.mAsset = .issue iss) (h_xrp : iss.isXRP = false)
    (ha_iou : asset.holdsIssue = true) (ha_not_xrp : asset.isNative = false)
    (hc1 : v1.IOUCanonical) (hc2 : v2.IOUCanonical)
    (hok : STAmount.multiply v1 v2 asset .to_nearest = .ok result) (hresult : result.mValue ≠ 0) :
    STAmount.RoundsToRepresentableWithin result (v1.toRat * v2.toRat) 1 .to_nearest := by
  obtain ⟨r, hofn, hr_lo, hr_hi, hr_exp_lo, hr_exp_hi, hop⟩ :=
    STAmount.operator_mul_iou_decompose v1 v2 result asset iss hv1 hv2 h_xrp ha_iou ha_not_xrp
      hc1 hc2 hok hresult
  obtain ⟨hsnap_r, hexp⟩ := STAmount.ofNumber_iou_within_half_ulp asset r result ha_iou ha_not_xrp
    hr_lo hr_hi hr_exp_lo hr_exp_hi hofn hresult
  have hsnap : |result.toRat - r.toRat| ≤ (1 / 2) * (10 : ℚ) ^ result.exponent := by
    refine le_trans hsnap_r ?_
    exact mul_le_mul_of_nonneg_left (zpow_le_zpow_right₀ (by norm_num) hexp) (by norm_num)
  have hr_ulp : |r.toRat| ≤ 10 ^ 16 * (10 : ℚ) ^ result.exponent := by
    rw [abs_toRat_eq r]
    have hkey : (r.mantissa_.toNat : ℚ) * (10 : ℚ) ^ r.exponent_
        ≤ 10 ^ 16 * (10 : ℚ) ^ (r.exponent_ + 3) := by
      rw [show (10 : ℚ) ^ (r.exponent_ + 3) = (10 : ℚ) ^ r.exponent_ * (10 : ℚ) ^ (3 : ℤ) by
            rw [zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0)],
          show (10 : ℚ) ^ 16 * ((10 : ℚ) ^ r.exponent_ * (10 : ℚ) ^ (3 : ℤ))
            = ((10 : ℚ) ^ 16 * (10 : ℚ) ^ (3 : ℤ)) * (10 : ℚ) ^ r.exponent_ by ring,
          show (10 : ℚ) ^ 16 * (10 : ℚ) ^ (3 : ℤ) = (10 : ℚ) ^ 19 by norm_num]
      gcongr
      exact_mod_cast le_of_lt hr_hi
    exact le_trans hkey (mul_le_mul_of_nonneg_left (zpow_le_zpow_right₀ (by norm_num) hexp)
      (by positivity))
  exact STAmount.RoundsToRepresentableWithin_of_double_round result r (v1.toRat * v2.toRat)
    .to_nearest 1 (1 / 2) (5 / (2 ^ 63 + 7 : ℚ)) trivial hsnap hr_ulp hop (by positivity)
    (by norm_num) (by norm_num)

set_option maxHeartbeats 1000000 in
-- composes the Number-multiply decompose, the magnitude re-round, and the double-round
-- zpow/nlinarith algebra in one proof; exceeds the default budget
/-- Proof of `operator_mul_repr_iou_directed` (IOU multiply within 2 ULP, directed modes,
non-negative operands). -/
theorem STAmount.operator_mul_repr_iou_directed_proof (v1 v2 result : STAmount) (asset : Asset)
    (iss : Issue) (mode : rounding_mode)
    (hv1 : v1.mAsset = .issue iss) (hv2 : v2.mAsset = .issue iss) (h_xrp : iss.isXRP = false)
    (ha_iou : asset.holdsIssue = true) (ha_not_xrp : asset.isNative = false)
    (hc1 : v1.IOUCanonical) (hc2 : v2.IOUCanonical)
    (hn1 : v1.mIsNegative = false) (hn2 : v2.mIsNegative = false)
    (hok : STAmount.multiply v1 v2 asset mode = .ok result) (hresult : result.mValue ≠ 0) :
    STAmount.RoundsToRepresentableWithin result (v1.toRat * v2.toRat) 2 mode := by
  obtain ⟨r, hofn, hr_lo, hr_hi, hr_exp_lo, hr_exp_hi, hr_neg, hmb⟩ :=
    STAmount.operator_mul_iou_decompose_anyMode v1 v2 result asset iss mode hv1 hv2 h_xrp ha_iou
      ha_not_xrp hc1 hc2 hn1 hn2 hok hresult
  have htruth_nn : 0 ≤ v1.toRat * v2.toRat := by
    have h1 : 0 ≤ v1.toRat := by rw [STAmount.toRat_of_nonneg v1 hn1]; positivity
    have h2 : 0 ≤ v2.toRat := by rw [STAmount.toRat_of_nonneg v2 hn2]; positivity
    positivity
  have hr_nn : 0 ≤ r.toRat := by rw [Number.toRat_of_nonneg r hr_neg]; positivity
  have hop : |r.toRat - v1.toRat * v2.toRat| ≤ |v1.toRat * v2.toRat| * (10 / (2 ^ 63 + 2 : ℚ)) :=
    RoundsWithin_abs_diff_le_of_nonneg r (v1.toRat * v2.toRat) (10 / (2 ^ 63 + 2 : ℚ)) mode hmb
      hr_nn htruth_nn
  obtain ⟨hsnap_r, hexp⟩ := STAmount.ofNumber_iou_within_ulp asset r mode result ha_iou ha_not_xrp
    hr_lo hr_hi hr_exp_lo hr_exp_hi hofn hresult
  have hsnap : |result.toRat - r.toRat| ≤ 1 * (10 : ℚ) ^ result.exponent := by
    rw [one_mul]; exact le_trans hsnap_r (zpow_le_zpow_right₀ (by norm_num) hexp)
  have hr_ulp : |r.toRat| ≤ 10 ^ 16 * (10 : ℚ) ^ result.exponent := by
    rw [abs_toRat_eq r]
    have hkey : (r.mantissa_.toNat : ℚ) * (10 : ℚ) ^ r.exponent_
        ≤ 10 ^ 16 * (10 : ℚ) ^ (r.exponent_ + 3) := by
      rw [show (10 : ℚ) ^ (r.exponent_ + 3) = (10 : ℚ) ^ r.exponent_ * (10 : ℚ) ^ (3 : ℤ) by
            rw [zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0)],
          show (10 : ℚ) ^ 16 * ((10 : ℚ) ^ r.exponent_ * (10 : ℚ) ^ (3 : ℤ))
            = ((10 : ℚ) ^ 16 * (10 : ℚ) ^ (3 : ℤ)) * (10 : ℚ) ^ r.exponent_ by ring,
          show (10 : ℚ) ^ 16 * (10 : ℚ) ^ (3 : ℤ) = (10 : ℚ) ^ 19 by norm_num]
      gcongr
      exact_mod_cast le_of_lt hr_hi
    exact le_trans hkey (mul_le_mul_of_nonneg_left (zpow_le_zpow_right₀ (by norm_num) hexp)
      (by positivity))
  have hsnap_dir := STAmount.ofNumber_iou_rounds_within asset r mode result ha_iou ha_not_xrp
    hr_neg hr_lo hr_hi hr_exp_lo hr_exp_hi hofn hresult
  exact STAmount.RoundsToRepresentableWithin_of_double_round result r (v1.toRat * v2.toRat)
    mode 2 1 (10 / (2 ^ 63 + 2 : ℚ))
    (by cases mode <;>
      first
      | trivial
      | exact le_trans hsnap_dir.1 hmb.1
      | exact le_trans hmb.1 hsnap_dir.1)
    hsnap hr_ulp hop (by positivity) (by norm_num) (by norm_num)

/-- Proof of `operator_mul_iou_within_2ulp` (IOU multiply accuracy 2 ULP, any sign/mode). -/
theorem STAmount.operator_mul_iou_within_2ulp_proof (v1 v2 result : STAmount) (asset : Asset)
    (iss : Issue) (mode : rounding_mode)
    (hv1 : v1.mAsset = .issue iss) (hv2 : v2.mAsset = .issue iss) (h_xrp : iss.isXRP = false)
    (ha_iou : asset.holdsIssue = true) (ha_not_xrp : asset.isNative = false)
    (hc1 : v1.IOUCanonical) (hc2 : v2.IOUCanonical)
    (hok : STAmount.multiply v1 v2 asset mode = .ok result) (hresult : result.mValue ≠ 0) :
    |result.toRat - v1.toRat * v2.toRat| ≤ 2 * (10 : ℚ) ^ result.exponent := by
  obtain ⟨r, hofn, hr_lo, hr_hi, hr_exp_lo, hr_exp_hi, hop, _⟩ :=
    STAmount.operator_mul_iou_decompose_mag v1 v2 result asset iss mode hv1 hv2 h_xrp
      ha_iou ha_not_xrp hc1 hc2 hok hresult
  obtain ⟨hsnap_r, hexp⟩ := STAmount.ofNumber_iou_within_ulp asset r mode result ha_iou
    ha_not_xrp hr_lo hr_hi hr_exp_lo hr_exp_hi hofn hresult
  have hsnap : |result.toRat - r.toRat| ≤ 1 * (10 : ℚ) ^ result.exponent := by
    rw [one_mul]; exact le_trans hsnap_r (zpow_le_zpow_right₀ (by norm_num) hexp)
  have hr_ulp : |r.toRat| ≤ 10 ^ 16 * (10 : ℚ) ^ result.exponent := by
    rw [abs_toRat_eq r]
    have hkey : (r.mantissa_.toNat : ℚ) * (10 : ℚ) ^ r.exponent_
        ≤ 10 ^ 16 * (10 : ℚ) ^ (r.exponent_ + 3) := by
      rw [show (10 : ℚ) ^ (r.exponent_ + 3) = (10 : ℚ) ^ r.exponent_ * (10 : ℚ) ^ (3 : ℤ) by
            rw [zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0)],
          show (10 : ℚ) ^ 16 * ((10 : ℚ) ^ r.exponent_ * (10 : ℚ) ^ (3 : ℤ))
            = ((10 : ℚ) ^ 16 * (10 : ℚ) ^ (3 : ℤ)) * (10 : ℚ) ^ r.exponent_ by ring,
          show (10 : ℚ) ^ 16 * (10 : ℚ) ^ (3 : ℤ) = (10 : ℚ) ^ 19 by norm_num]
      gcongr
      exact_mod_cast le_of_lt hr_hi
    exact le_trans hkey (mul_le_mul_of_nonneg_left (zpow_le_zpow_right₀ (by norm_num) hexp)
      (by positivity))
  exact STAmount.double_round_abs_le result r (v1.toRat * v2.toRat) 2 1 (10 / (2 ^ 63 + 2 : ℚ))
    hsnap hr_ulp hop (by positivity) (by norm_num) (by norm_num)

/-- Proof of `operator_mul_iou_abs_le_towards_zero` (magnitude never increases). -/
theorem STAmount.operator_mul_iou_abs_le_towards_zero_proof (v1 v2 result : STAmount) (asset : Asset)
    (iss : Issue)
    (hv1 : v1.mAsset = .issue iss) (hv2 : v2.mAsset = .issue iss) (h_xrp : iss.isXRP = false)
    (ha_iou : asset.holdsIssue = true) (ha_not_xrp : asset.isNative = false)
    (hc1 : v1.IOUCanonical) (hc2 : v2.IOUCanonical)
    (hok : STAmount.multiply v1 v2 asset .towards_zero = .ok result) (hresult : result.mValue ≠ 0) :
    |result.toRat| ≤ |v1.toRat * v2.toRat| := by
  obtain ⟨r, hofn, hr_lo, hr_hi, hr_exp_lo, hr_exp_hi, _, hmb⟩ :=
    STAmount.operator_mul_iou_decompose_mag v1 v2 result asset iss .towards_zero hv1 hv2 h_xrp
      ha_iou ha_not_xrp hc1 hc2 hok hresult
  exact le_trans
    (STAmount.ofNumber_iou_abs_le_towards_zero asset r result ha_iou ha_not_xrp hr_lo hr_hi
      hr_exp_lo hr_exp_hi hofn hresult)
    hmb.1

end XRPL.Model.Protocol
