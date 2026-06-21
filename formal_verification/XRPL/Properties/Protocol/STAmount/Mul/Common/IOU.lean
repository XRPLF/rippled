import XRPL.Properties.Protocol.Number.Mul.RoundsWithin
import XRPL.Properties.Protocol.Number.Mul.Common.Decompose
import XRPL.Properties.Protocol.Number.Common.Rounding.SmallRangeBound
import XRPL.Properties.Protocol.Number.Common.Rounding.SmallRange
import XRPL.Properties.Protocol.STAmount.Add.Common.IOU
import XRPL.Properties.Protocol.STAmount.Common.RoundToScaleHelpers
import XRPL.Properties.Protocol.STAmount.Common.DiscreteDefs
import XRPL.Properties.Protocol.Number.Common.Int64Lemmas

namespace XRPL.Model.Protocol

/-- **`IOUAmount.normalize` on a canonical 16-digit signed mantissa.** The 19-digit
re-lift then 16-digit re-round round-trips exactly (it's already canonical), so the
only effect is the `[cMinOffset, cMaxOffset]` exponent clamp. -/
lemma IOUAmount.normalize_canonical16 (mant : UInt64) (exp : Int) (neg : Bool) (mode : rounding_mode)
    (h_lo : 10 ^ 15 ≤ mant.toNat) (h_hi : mant.toNat < 10 ^ 16)
    (he_lo : minExponent + 3 ≤ exp) (he_hi : exp ≤ maxExponent) :
    IOUAmount.normalize ⟨if neg then -mant.toInt64 else mant.toInt64, exp⟩ mode
      = (if exp > cMaxOffset then .error "value overflow"
         else if exp < cMinOffset then .ok IOUAmount.zero
         else .ok ⟨if neg then -mant.toInt64 else mant.toInt64, exp⟩) := by
  have h_fit : mant.toNat < 2 ^ 63 := by omega
  set sd : Int64 := if neg then -mant.toInt64 else mant.toInt64 with hsd
  have h_natAbs : sd.toInt.natAbs = mant.toNat := signed_mantissa_natAbs neg mant h_fit
  have h_neg_dec : decide (sd < 0) = neg := signed_mantissa_decide_neg neg mant h_fit (by omega)
  have h_mAbs : sd.toInt.natAbs.toUInt64 = mant := by
    rw [h_natAbs]
    exact UInt64.toNat_inj.mp (UInt64.toNat_ofNat_of_lt' (by rw [uint64_size_val]; omega))
  have h_sd_ne : ¬ ((⟨sd, exp⟩ : IOUAmount).mantissa_ == 0) = true := by
    show ¬ (sd == 0) = true
    rw [beq_iff_eq]; intro h
    have h0 : sd.toInt.natAbs = 0 := by rw [h]; decide
    rw [h_natAbs] at h0; omega
  unfold IOUAmount.normalize
  rw [if_neg h_sd_ne]
  have h_fr : Number.from_rep (⟨sd, exp⟩ : IOUAmount).mantissa_ (⟨sd, exp⟩ : IOUAmount).exponent_
      largeRange.min largeRange.max mode = .ok ⟨neg, mant * 10 * 10 * 10, exp - 3⟩ := by
    show Number.from_rep sd exp largeRange.min largeRange.max mode = _
    unfold Number.from_rep Number.normalized Number.normalize Number.unchecked
    rw [h_neg_dec, h_mAbs]
    exact doNormalize_large_16digit neg mant exp mode h_lo h_hi (by omega) (by omega)
  rw [h_fr]
  simp only []
  set v : Number := ⟨neg, mant * 10 * 10 * 10, exp - 3⟩ with hv_def
  have hM_toNat : (mant * 10 * 10 * 10).toNat = mant.toNat * 1000 :=
    m_mul_thousand_no_overflow h_hi
  have h_fn : IOUAmount.fromNumber v mode
      = .ok { mantissa_ := if neg then -(v.mantissa_ / 10 / 10 / 10).toInt64
                           else (v.mantissa_ / 10 / 10 / 10).toInt64,
              exponent_ := exp } := by
    unfold IOUAmount.fromNumber
    have h_ntr := normalizeToRange_16_exact v mode
      (by show 10 ^ 18 ≤ (mant * 10 * 10 * 10).toNat; rw [hM_toNat]; omega)
      (by show (mant * 10 * 10 * 10).toNat < 10 ^ 19; rw [hM_toNat]; omega)
      (by show (mant * 10 * 10 * 10).toNat % 1000 = 0; rw [hM_toNat]; omega)
      (by show minExponent ≤ exp - 3 + 3; omega)
      (by show exp - 3 + 3 ≤ maxExponent; omega)
    have h_exp_fix : v.exponent_ + 3 = exp := by show exp - 3 + 3 = exp; ring
    rw [h_exp_fix] at h_ntr
    rw [h_ntr]
  rw [h_fn]
  simp only []
  have h_q3 : v.mantissa_ / 10 / 10 / 10 = mant := by
    apply UInt64.toNat_inj.mp
    show ((mant * 10 * 10 * 10) / 10 / 10 / 10).toNat = mant.toNat
    rw [m_div_thousand_toNat (mant * 10 * 10 * 10), hM_toNat]; omega
  rw [h_q3]

/-! # STAmount IOU multiplication rounding

`STAmount.multiply` on the IOU path lifts both operands to 19-digit `Number`s
(`toNumber`), multiplies (`Number.operator_mul`), then snaps back to the 16-digit
IOU grid via `STAmount.ofNumber`/`checked`. This double-rounds, so the headline is
a relative-error bound `εMul = ε₁ + ε₂ + ε₁·ε₂` with `ε₁ = 5/(2⁶³+7)` (the `Number`
multiply bound) and `ε₂ = 10⁻¹⁵` (the 16-digit re-round). -/

/-- **`STAmount.checked` on a canonical 16-digit IOU mantissa.** A successful,
nonzero result forces the exponent into `[cMinOffset, cMaxOffset]` and reproduces
the record exactly (out-of-range exponents would error or flush to zero). -/
lemma STAmount.checked_iou_cases (asset : Asset) (mant : UInt64) (exp : Int) (neg : Bool)
    (mode : rounding_mode)
    (h_iou : asset.holdsIssue = true) (h_not_xrp : asset.isNative = false)
    (h_lo : 10 ^ 15 ≤ mant.toNat) (h_hi : mant.toNat < 10 ^ 16)
    (he_lo : minExponent + 3 ≤ exp) (he_hi : exp ≤ maxExponent)
    (result : STAmount)
    (hok : STAmount.checked asset mant exp neg mode = .ok result) (hresult : result.mValue ≠ 0) :
    (-96 : ℤ) ≤ exp ∧ exp ≤ 80 ∧ result = ⟨asset, mant, exp, neg⟩ := by
  have h_fit : mant.toNat < 2 ^ 63 := by omega
  have h_int : ¬ (STAmount.unchecked asset mant exp neg).integral = true := by
    intro h
    unfold STAmount.integral STAmount.unchecked at h
    rcases ha : asset with i | m
    · rw [ha] at h_not_xrp h
      exact Bool.noConfusion (h_not_xrp.symm.trans (show (Asset.issue i).isNative = true from h))
    · rw [ha] at h_iou
      exact Bool.noConfusion h_iou
  have h_sd : (STAmount.unchecked asset mant exp neg).signedDrops.toInt64
      = if neg then -mant.toInt64 else mant.toInt64 := by
    apply Int64.toInt_inj.mp
    rw [STAmount.signedDrops_toInt64_toInt _
          (show (STAmount.unchecked asset mant exp neg).mValue.toNat < 10 ^ 16 from h_hi),
        signed_mantissa_toInt neg mant h_fit]
    show (STAmount.unchecked asset mant exp neg).signedDrops = _
    unfold STAmount.signedDrops STAmount.unchecked
    rcases neg <;> simp
  have hiou : (STAmount.unchecked asset mant exp neg).iou mode
      = (if exp > cMaxOffset then .error "value overflow"
         else if exp < cMinOffset then .ok IOUAmount.zero
         else .ok ⟨if neg then -mant.toInt64 else mant.toInt64, exp⟩) := by
    unfold STAmount.iou
    rw [if_neg h_int]
    unfold IOUAmount.ofMantissaExp
    rw [h_sd]
    exact IOUAmount.normalize_canonical16 mant exp neg mode h_lo h_hi he_lo he_hi
  by_cases hhi : exp > cMaxOffset
  · exfalso
    have hb : STAmount.checked asset mant exp neg mode = .error "value overflow" := by
      rw [STAmount.checked]; unfold STAmount.canonicalize
      rw [if_neg h_int, hiou, if_pos hhi]
    rw [hb] at hok; simp at hok
  · by_cases hlo : exp < cMinOffset
    · exfalso; apply hresult
      rw [STAmount.checked] at hok
      unfold STAmount.canonicalize at hok
      rw [if_neg h_int, hiou, if_neg hhi, if_pos hlo] at hok
      simp only [] at hok
      rw [← Except.ok.inj hok]
      show (if IOUAmount.zero.signum < 0 then -IOUAmount.zero.mantissa_
            else IOUAmount.zero.mantissa_).toUInt64 = 0
      decide
    · have hexp_lo : (-96 : ℤ) ≤ exp := by unfold cMinOffset at hlo; omega
      have hexp_hi : exp ≤ 80 := by unfold cMaxOffset at hhi; omega
      refine ⟨hexp_lo, hexp_hi, ?_⟩
      have hc : (⟨asset, mant, exp, neg⟩ : STAmount).IOUCanonical :=
        ⟨h_iou, h_not_xrp, h_lo, h_hi, hexp_lo, hexp_hi⟩
      have hcid := STAmount.canonicalize_canonical_id ⟨asset, mant, exp, neg⟩ mode hc
      rw [STAmount.checked,
          show STAmount.unchecked asset mant exp neg = (⟨asset, mant, exp, neg⟩ : STAmount) from rfl,
          hcid] at hok
      exact (Except.ok.inj hok).symm

/-- The exact 19-digit `Number` view of a canonical IOU `STAmount`: `toNumber`
routes through `iou` (canonical round-trip) then the `×1000` lift. -/
lemma STAmount.toNumber_iou_canonical (s : STAmount) (iss : Issue) (mode : rounding_mode)
    (hv : s.mAsset = .issue iss) (h_xrp : iss.isXRP = false) (hc : s.IOUCanonical) :
    s.toNumber mode = .ok ⟨s.mIsNegative, s.mValue * 10 * 10 * 10, s.mOffset - 3⟩ := by
  unfold STAmount.toNumber
  rw [hv]
  simp only []
  rw [if_neg (show ¬ (iss.isXRP = true) from by rw [h_xrp]; decide),
      STAmount.iou_canonical_id s mode hc]
  simp only []
  exact STAmount.iou_toNumber_canonical s mode hc

/-- The lift's value equals the source value. -/
lemma STAmount.toNumber_iou_canonical_toRat (s : STAmount) (hc : s.IOUCanonical) :
    (⟨s.mIsNegative, s.mValue * 10 * 10 * 10, s.mOffset - 3⟩ : Number).toRat = s.toRat := by
  have h_fit : s.mValue.toNat < 2 ^ 63 := by have := hc.mant_hi; omega
  have hmin : largeRange.min.toNat = 1000000000000000000 := largeRange_min_val
  have hm3 : (s.mValue * 10 * 10 * 10).toNat = s.mValue.toNat * 1000 := by
    have hm1 : (s.mValue * 10).toNat = s.mValue.toNat * 10 :=
      m_mul_ten_no_overflow (by have := hc.mant_hi; rw [hmin]; omega)
    have hm2 : (s.mValue * 10 * 10).toNat = s.mValue.toNat * 100 := by
      rw [m_mul_ten_no_overflow (by rw [hm1]; have := hc.mant_hi; rw [hmin]; omega), hm1]; ring
    rw [m_mul_ten_no_overflow (by rw [hm2]; have := hc.mant_hi; rw [hmin]; omega), hm2]; ring
  have hpow : (10 : ℚ) ^ s.mOffset = (10 : ℚ) ^ (s.mOffset - 3) * 1000 := by
    rw [show s.mOffset = (s.mOffset - 3) + 3 from by ring,
        zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0)]; norm_num
  rcases hneg : s.mIsNegative with _ | _
  · rw [Number.toRat_of_nonneg _ rfl, STAmount.toRat_of_nonneg s hneg]
    show (((s.mValue * 10 * 10 * 10).toNat : ℚ)) * 10 ^ (s.mOffset - 3) = _
    rw [hm3, hpow]; push_cast; ring
  · rw [Number.toRat_of_neg _ rfl, STAmount.toRat_of_neg s hneg]
    show -(((s.mValue * 10 * 10 * 10).toNat : ℚ) * 10 ^ (s.mOffset - 3)) = _
    rw [hm3, hpow]; push_cast; ring

/-- The lift is normalized (mantissa `∈ [10¹⁸, 10¹⁹)`). -/
lemma STAmount.toNumber_iou_canonical_isNormalized (s : STAmount) (hc : s.IOUCanonical) :
    (⟨s.mIsNegative, s.mValue * 10 * 10 * 10, s.mOffset - 3⟩ : Number).isNormalized := by
  have hmin : largeRange.min.toNat = 1000000000000000000 := largeRange_min_val
  have hmax : largeRange.max.toNat = 9999999999999999999 := largeRange_max_val
  have hm3 : (s.mValue * 10 * 10 * 10).toNat = s.mValue.toNat * 1000 := by
    have hm1 : (s.mValue * 10).toNat = s.mValue.toNat * 10 :=
      m_mul_ten_no_overflow (by have := hc.mant_hi; rw [hmin]; omega)
    have hm2 : (s.mValue * 10 * 10).toNat = s.mValue.toNat * 100 := by
      rw [m_mul_ten_no_overflow (by rw [hm1]; have := hc.mant_hi; rw [hmin]; omega), hm1]; ring
    rw [m_mul_ten_no_overflow (by rw [hm2]; have := hc.mant_hi; rw [hmin]; omega), hm2]; ring
  right
  refine ⟨?_, ?_, ?_, ?_, ?_⟩
  · show largeRange.min ≤ s.mValue * 10 * 10 * 10
    rw [UInt64.le_iff_toNat_le, hm3, hmin]; have := hc.mant_lo; omega
  · show s.mValue * 10 * 10 * 10 ≤ largeRange.max
    rw [UInt64.le_iff_toNat_le, hm3, hmax]; have := hc.mant_hi; omega
  · right; show (s.mValue * 10 * 10 * 10).toNat % 10 = 0; rw [hm3]; omega
  · show minExponent ≤ s.mOffset - 3; have := hc.exp_lo; unfold minExponent; omega
  · show s.mOffset - 3 ≤ maxExponent; have := hc.exp_hi; unfold maxExponent; omega

/-- **`STAmount.ofNumber` IOU re-rounding bound.** Snapping a 19-digit-normalized
`Number` `r` to the 16-digit IOU grid (via `normalizeToRange` then the
value-preserving `checked`) loses at most one 16-digit ULP. -/
lemma STAmount.ofNumber_iou_within_ulp (asset : Asset) (r : Number) (mode : rounding_mode)
    (result : STAmount)
    (h_iou : asset.holdsIssue = true) (h_not_xrp : asset.isNative = false)
    (hr_lo : 10 ^ 18 ≤ r.mantissa_.toNat) (hr_hi : r.mantissa_.toNat < 10 ^ 19)
    (hre_lo : minExponent ≤ r.exponent_) (hre_hi : r.exponent_ + 4 ≤ maxExponent)
    (hok : STAmount.ofNumber asset r mode = .ok result) (hresult : result.mValue ≠ 0) :
    |result.toRat - r.toRat| ≤ (10 : ℚ) ^ (r.exponent_ + 3)
      ∧ r.exponent_ + 3 ≤ result.exponent := by
  have hr_ne : r.mantissa_ ≠ 0 := by
    intro h; rw [h] at hr_lo; simp at hr_lo
  have hmne : (r.mantissa_ != 0) = true := by simp [hr_ne]
  have hneg_eq : decide (r.signum < 0) = r.negative_ := by
    unfold Number.signum
    rcases hrn : r.negative_ with _ | _ <;> simp only [hmne, if_true, if_false,
      Bool.false_eq_true] <;> decide
  -- `neg`/`working` as in `ofNumber`; `working = |r|` is non-negative, same magnitude/exponent.
  set neg : Bool := decide (r.signum < 0) with hneg_def
  set working : Number := if neg then r.operator_neg else r with hw_def
  have hw_mant : working.mantissa_ = r.mantissa_ := by
    rw [hw_def]; rcases neg with _ | _
    · simp
    · simp [Number.operator_neg_mantissa_of_ne r hr_ne]
  have hw_exp : working.exponent_ = r.exponent_ := by
    rw [hw_def]; rcases neg with _ | _
    · simp
    · simp only [if_true]; unfold Number.operator_neg
      rw [if_neg (by simpa using hr_ne)]
  have hw_neg : working.negative_ = false := by
    rw [hw_def, hneg_eq]
    by_cases hrn : r.negative_ = true
    · rw [if_pos hrn, Number.operator_neg_negative_of_ne r hr_ne]; simp [hrn]
    · rw [if_neg hrn]; simpa using hrn
  have hw_lo : 10 ^ 18 ≤ working.mantissa_.toNat := by rw [hw_mant]; exact hr_lo
  have hw_hi : working.mantissa_.toNat < 10 ^ 19 := by rw [hw_mant]; exact hr_hi
  have hwe_lo : minExponent ≤ working.exponent_ + 3 := by rw [hw_exp]; omega
  have hwe_hi : working.exponent_ + 4 ≤ maxExponent := by rw [hw_exp]; exact hre_hi
  -- `working.toRat = |r.toRat|`, and `r.toRat = (if neg then -1 else 1) * working.toRat`.
  have hw_toRat_nonneg : 0 ≤ working.toRat := by
    rw [Number.toRat_of_nonneg working hw_neg]; positivity
  have hr_toRat : r.toRat = (if neg then (-1 : ℚ) else 1) * working.toRat := by
    rw [hw_def]
    by_cases hn : neg = true
    · rw [if_pos hn, if_pos hn, Number.toRat_neg]; ring
    · rw [if_neg hn, if_neg hn]; ring
  -- `asset` is non-integral, so `ofNumber` reduces to `checked` on the `normalizeToRange` output.
  have h_asset_int : asset.integral = false := by
    rcases ha : asset with i | m
    · rw [ha] at h_not_xrp; exact h_not_xrp
    · rw [ha] at h_iou; exact Bool.noConfusion h_iou
  unfold STAmount.ofNumber at hok
  rw [if_neg (by rw [h_asset_int]; decide), ← hneg_def, ← hw_def] at hok
  cases hnorm : working.normalizeToRange kMinValue kMaxValue mode with
  | error e => rw [hnorm] at hok; exact absurd hok (by simp)
  | ok me =>
    obtain ⟨mant, exp⟩ := me
    rw [hnorm] at hok
    simp only at hok
    -- keystone facts (kMinValue/kMaxValue ≡ cMinValue/cMaxValue).
    have hnorm' : working.normalizeToRange cMinValue cMaxValue mode = .ok (mant, exp) := hnorm
    have hbound := normalizeToRange_16_within_ulp working mode mant exp hw_lo hw_hi hwe_lo hwe_hi hnorm'
    have hmrange := normalizeToRange_16_mantissa_range working mode mant exp hw_lo hw_hi hwe_lo hwe_hi hnorm'
    have herange := normalizeToRange_16_exp_range working mode mant exp hw_lo hw_hi hwe_lo hwe_hi hnorm'
    have hcMin : cMinValue.toNat = 10 ^ 15 := by decide
    have hcMax : cMaxValue.toNat = 10 ^ 16 - 1 := by decide
    -- `mant ≥ 0`: it is within one ULP of the (large, non-negative) `working.toRat`.
    have hwlarge : (10 : ℚ) ^ (working.exponent_ + 18) ≤ working.toRat := by
      rw [Number.toRat_of_nonneg working hw_neg]
      have h1018 : (10 : ℚ) ^ (18 : ℤ) ≤ (working.mantissa_.toNat : ℚ) := by
        have : ((10 ^ 18 : ℕ) : ℚ) ≤ (working.mantissa_.toNat : ℚ) := by exact_mod_cast hw_lo
        rwa [show ((10 ^ 18 : ℕ) : ℚ) = (10 : ℚ) ^ (18 : ℤ) by push_cast; norm_num] at this
      calc (10 : ℚ) ^ (working.exponent_ + 18)
          = (10 : ℚ) ^ (18 : ℤ) * (10 : ℚ) ^ working.exponent_ := by
            rw [← zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0)]; ring_nf
        _ ≤ (working.mantissa_.toNat : ℚ) * (10 : ℚ) ^ working.exponent_ := by gcongr
    have hmant_pos : 0 ≤ mant.toInt := by
      by_contra hc
      push_neg at hc
      have hmant_neg_q : (mant.toInt : ℚ) < 0 := by exact_mod_cast hc
      have hval_neg : (mant.toInt : ℚ) * 10 ^ exp < 0 :=
        mul_neg_of_neg_of_pos hmant_neg_q (zpow_pos (by norm_num) _)
      have hb := abs_le.mp hbound
      have hule : (10 : ℚ) ^ (working.exponent_ + 3) ≤ (10 : ℚ) ^ (working.exponent_ + 18) :=
        zpow_le_zpow_right₀ (by norm_num) (by omega)
      have : working.toRat ≤ (mant.toInt : ℚ) * 10 ^ exp + 10 ^ (working.exponent_ + 3) := by
        linarith [hb.1]
      nlinarith [hwlarge, hule, hval_neg]
    have hmant_natAbs : mant.toInt.natAbs = mant.toUInt64.toNat := by
      have := toUInt64_toNat_of_nonneg mant hmant_pos
      omega
    -- `checked` reproduces the record (exponent forced in range by `hok`/`hresult`).
    have hmtu_lo : 10 ^ 15 ≤ mant.toUInt64.toNat := by
      have := hmrange.1; rw [hcMin] at this; omega
    have hmtu_hi : mant.toUInt64.toNat < 10 ^ 16 := by
      have := hmrange.2; rw [hcMax] at this; omega
    have hcc := STAmount.checked_iou_cases asset mant.toUInt64 exp neg mode h_iou h_not_xrp
      hmtu_lo hmtu_hi (by rw [hw_exp] at herange; omega) (by rw [hw_exp] at herange; omega) result hok hresult
    obtain ⟨_, _, hres_eq⟩ := hcc
    -- value transport.
    have hres_toRat : result.toRat = (if neg then (-1 : ℚ) else 1) * ((mant.toInt : ℚ) * 10 ^ exp) := by
      rw [hres_eq, STAmount.toRat_signed]
      show (if neg then (-1 : ℚ) else 1) * (mant.toUInt64.toNat : ℚ) * 10 ^ exp = _
      have : (mant.toUInt64.toNat : ℚ) = (mant.toInt : ℚ) := by
        exact_mod_cast toUInt64_toNat_of_nonneg mant hmant_pos
      rw [this]; ring
    rw [hres_toRat, hr_toRat]
    rw [show (if neg then (-1 : ℚ) else 1) * ((mant.toInt : ℚ) * 10 ^ exp)
          - (if neg then (-1 : ℚ) else 1) * working.toRat
        = (if neg then (-1 : ℚ) else 1) * ((mant.toInt : ℚ) * 10 ^ exp - working.toRat) from by ring,
        abs_mul]
    have hsabs : |(if neg then (-1 : ℚ) else 1)| = 1 := by rcases neg <;> norm_num
    have hexp : r.exponent_ + 3 ≤ result.exponent := by
      rw [hres_eq]; show r.exponent_ + 3 ≤ exp; rw [hw_exp] at herange; omega
    rw [hsabs, one_mul, hw_exp] at *
    exact ⟨hbound, hexp⟩

/-- **Tight (half-ULP) `STAmount.ofNumber` IOU bound, `to_nearest`.** Identical to
`ofNumber_iou_within_ulp` but the round-to-nearest snap lands within *half* a 16-digit
ULP, the tight `ε₂` for `to_nearest` IOU arithmetic. -/
lemma STAmount.ofNumber_iou_within_half_ulp (asset : Asset) (r : Number)
    (result : STAmount)
    (h_iou : asset.holdsIssue = true) (h_not_xrp : asset.isNative = false)
    (hr_lo : 10 ^ 18 ≤ r.mantissa_.toNat) (hr_hi : r.mantissa_.toNat < 10 ^ 19)
    (hre_lo : minExponent ≤ r.exponent_) (hre_hi : r.exponent_ + 4 ≤ maxExponent)
    (hok : STAmount.ofNumber asset r .to_nearest = .ok result) (hresult : result.mValue ≠ 0) :
    |result.toRat - r.toRat| ≤ (1 / 2 : ℚ) * (10 : ℚ) ^ (r.exponent_ + 3)
      ∧ r.exponent_ + 3 ≤ result.exponent := by
  have hr_ne : r.mantissa_ ≠ 0 := by
    intro h; rw [h] at hr_lo; simp at hr_lo
  have hmne : (r.mantissa_ != 0) = true := by simp [hr_ne]
  have hneg_eq : decide (r.signum < 0) = r.negative_ := by
    unfold Number.signum
    rcases hrn : r.negative_ with _ | _ <;> simp only [hmne, if_true, if_false,
      Bool.false_eq_true] <;> decide
  set neg : Bool := decide (r.signum < 0) with hneg_def
  set working : Number := if neg then r.operator_neg else r with hw_def
  have hw_mant : working.mantissa_ = r.mantissa_ := by
    rw [hw_def]; rcases neg with _ | _
    · simp
    · simp [Number.operator_neg_mantissa_of_ne r hr_ne]
  have hw_exp : working.exponent_ = r.exponent_ := by
    rw [hw_def]; rcases neg with _ | _
    · simp
    · simp only [if_true]; unfold Number.operator_neg
      rw [if_neg (by simpa using hr_ne)]
  have hw_neg : working.negative_ = false := by
    rw [hw_def, hneg_eq]
    by_cases hrn : r.negative_ = true
    · rw [if_pos hrn, Number.operator_neg_negative_of_ne r hr_ne]; simp [hrn]
    · rw [if_neg hrn]; simpa using hrn
  have hw_lo : 10 ^ 18 ≤ working.mantissa_.toNat := by rw [hw_mant]; exact hr_lo
  have hw_hi : working.mantissa_.toNat < 10 ^ 19 := by rw [hw_mant]; exact hr_hi
  have hwe_lo : minExponent ≤ working.exponent_ + 3 := by rw [hw_exp]; omega
  have hwe_hi : working.exponent_ + 4 ≤ maxExponent := by rw [hw_exp]; exact hre_hi
  have hw_toRat_nonneg : 0 ≤ working.toRat := by
    rw [Number.toRat_of_nonneg working hw_neg]; positivity
  have hr_toRat : r.toRat = (if neg then (-1 : ℚ) else 1) * working.toRat := by
    rw [hw_def]
    by_cases hn : neg = true
    · rw [if_pos hn, if_pos hn, Number.toRat_neg]; ring
    · rw [if_neg hn, if_neg hn]; ring
  have h_asset_int : asset.integral = false := by
    rcases ha : asset with i | m
    · rw [ha] at h_not_xrp; exact h_not_xrp
    · rw [ha] at h_iou; exact Bool.noConfusion h_iou
  unfold STAmount.ofNumber at hok
  rw [if_neg (by rw [h_asset_int]; decide), ← hneg_def, ← hw_def] at hok
  cases hnorm : working.normalizeToRange kMinValue kMaxValue .to_nearest with
  | error e => rw [hnorm] at hok; exact absurd hok (by simp)
  | ok me =>
    obtain ⟨mant, exp⟩ := me
    rw [hnorm] at hok
    simp only at hok
    have hnorm' : working.normalizeToRange cMinValue cMaxValue .to_nearest = .ok (mant, exp) := hnorm
    have hbound := normalizeToRange_16_within_half_ulp working mant exp hw_lo hw_hi hwe_lo hwe_hi hnorm'
    have hmrange := normalizeToRange_16_mantissa_range working .to_nearest mant exp hw_lo hw_hi hwe_lo hwe_hi hnorm'
    have herange := normalizeToRange_16_exp_range working .to_nearest mant exp hw_lo hw_hi hwe_lo hwe_hi hnorm'
    have hcMin : cMinValue.toNat = 10 ^ 15 := by decide
    have hcMax : cMaxValue.toNat = 10 ^ 16 - 1 := by decide
    have hwlarge : (10 : ℚ) ^ (working.exponent_ + 18) ≤ working.toRat := by
      rw [Number.toRat_of_nonneg working hw_neg]
      have h1018 : (10 : ℚ) ^ (18 : ℤ) ≤ (working.mantissa_.toNat : ℚ) := by
        have : ((10 ^ 18 : ℕ) : ℚ) ≤ (working.mantissa_.toNat : ℚ) := by exact_mod_cast hw_lo
        rwa [show ((10 ^ 18 : ℕ) : ℚ) = (10 : ℚ) ^ (18 : ℤ) by push_cast; norm_num] at this
      calc (10 : ℚ) ^ (working.exponent_ + 18)
          = (10 : ℚ) ^ (18 : ℤ) * (10 : ℚ) ^ working.exponent_ := by
            rw [← zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0)]; ring_nf
        _ ≤ (working.mantissa_.toNat : ℚ) * (10 : ℚ) ^ working.exponent_ := by gcongr
    have hmant_pos : 0 ≤ mant.toInt := by
      by_contra hc
      push_neg at hc
      have hmant_neg_q : (mant.toInt : ℚ) < 0 := by exact_mod_cast hc
      have hval_neg : (mant.toInt : ℚ) * 10 ^ exp < 0 :=
        mul_neg_of_neg_of_pos hmant_neg_q (zpow_pos (by norm_num) _)
      have hb := abs_le.mp hbound
      have hule : (10 : ℚ) ^ (working.exponent_ + 3) ≤ (10 : ℚ) ^ (working.exponent_ + 18) :=
        zpow_le_zpow_right₀ (by norm_num) (by omega)
      have hppos : (0 : ℚ) < (10 : ℚ) ^ (working.exponent_ + 3) := zpow_pos (by norm_num) _
      have : working.toRat ≤ (mant.toInt : ℚ) * 10 ^ exp + (1 / 2) * 10 ^ (working.exponent_ + 3) := by
        linarith [hb.1]
      nlinarith [hwlarge, hule, hval_neg, hppos]
    have hmant_natAbs : mant.toInt.natAbs = mant.toUInt64.toNat := by
      have := toUInt64_toNat_of_nonneg mant hmant_pos
      omega
    have hmtu_lo : 10 ^ 15 ≤ mant.toUInt64.toNat := by
      have := hmrange.1; rw [hcMin] at this; omega
    have hmtu_hi : mant.toUInt64.toNat < 10 ^ 16 := by
      have := hmrange.2; rw [hcMax] at this; omega
    have hcc := STAmount.checked_iou_cases asset mant.toUInt64 exp neg .to_nearest h_iou h_not_xrp
      hmtu_lo hmtu_hi (by rw [hw_exp] at herange; omega) (by rw [hw_exp] at herange; omega) result hok hresult
    obtain ⟨_, _, hres_eq⟩ := hcc
    have hres_toRat : result.toRat = (if neg then (-1 : ℚ) else 1) * ((mant.toInt : ℚ) * 10 ^ exp) := by
      rw [hres_eq, STAmount.toRat_signed]
      show (if neg then (-1 : ℚ) else 1) * (mant.toUInt64.toNat : ℚ) * 10 ^ exp = _
      have : (mant.toUInt64.toNat : ℚ) = (mant.toInt : ℚ) := by
        exact_mod_cast toUInt64_toNat_of_nonneg mant hmant_pos
      rw [this]; ring
    rw [hres_toRat, hr_toRat]
    rw [show (if neg then (-1 : ℚ) else 1) * ((mant.toInt : ℚ) * 10 ^ exp)
          - (if neg then (-1 : ℚ) else 1) * working.toRat
        = (if neg then (-1 : ℚ) else 1) * ((mant.toInt : ℚ) * 10 ^ exp - working.toRat) from by ring,
        abs_mul]
    have hsabs : |(if neg then (-1 : ℚ) else 1)| = 1 := by rcases neg <;> norm_num
    have hexp : r.exponent_ + 3 ≤ result.exponent := by
      rw [hres_eq]; show r.exponent_ + 3 ≤ exp; rw [hw_exp] at herange; omega
    rw [hsabs, one_mul, hw_exp] at *
    exact ⟨hbound, hexp⟩

/-- The lift's mantissa is nonzero. -/
lemma STAmount.toNumber_iou_canonical_mantissa_ne (s : STAmount) (hc : s.IOUCanonical) :
    (⟨s.mIsNegative, s.mValue * 10 * 10 * 10, s.mOffset - 3⟩ : Number).mantissa_ ≠ 0 := by
  have hmin : largeRange.min.toNat = 1000000000000000000 := largeRange_min_val
  have hm3 : (s.mValue * 10 * 10 * 10).toNat = s.mValue.toNat * 1000 := by
    have hm1 : (s.mValue * 10).toNat = s.mValue.toNat * 10 :=
      m_mul_ten_no_overflow (by have := hc.mant_hi; rw [hmin]; omega)
    have hm2 : (s.mValue * 10 * 10).toNat = s.mValue.toNat * 100 := by
      rw [m_mul_ten_no_overflow (by rw [hm1]; have := hc.mant_hi; rw [hmin]; omega), hm1]; ring
    rw [m_mul_ten_no_overflow (by rw [hm2]; have := hc.mant_hi; rw [hmin]; omega), hm2]; ring
  intro h
  have hM0 : s.mValue * 10 * 10 * 10 = 0 := h
  rw [hM0] at hm3
  simp at hm3
  have := hc.mant_lo; omega

/-- A nonzero `STAmount.ofNumber` result forces a nonzero source mantissa. -/
lemma STAmount.ofNumber_iou_mantissa_ne_zero (asset : Asset) (r : Number) (mode : rounding_mode)
    (result : STAmount) (h_iou : asset.holdsIssue = true) (h_not_xrp : asset.isNative = false)
    (hok : STAmount.ofNumber asset r mode = .ok result) (hresult : result.mValue ≠ 0) :
    r.mantissa_ ≠ 0 := by
  intro hrm
  apply hresult
  have h_asset_int : asset.integral = false := by
    rcases ha : asset with i | m
    · rw [ha] at h_not_xrp; exact h_not_xrp
    · rw [ha] at h_iou; exact Bool.noConfusion h_iou
  set neg : Bool := decide (r.signum < 0) with hneg_def
  set working : Number := if neg then r.operator_neg else r with hw_def
  have hw_mant0 : working.mantissa_ = 0 := by
    rw [hw_def]; by_cases hs : neg = true
    · rw [if_pos hs]; unfold Number.operator_neg
      rw [if_pos (show (r.mantissa_ == 0) = true from by rw [hrm]; rfl)]; rfl
    · rw [if_neg hs]; exact hrm
  have hnz : working.normalizeToRange kMinValue kMaxValue mode
      = .ok ((0 : Int64), Number.zero.exponent_) := by
    unfold Number.normalizeToRange
    rw [show doNormalize working.negative_ working.mantissa_ working.exponent_
          kMinValue kMaxValue mode = .ok Number.zero from by
      unfold doNormalize
      rw [if_pos (show (working.mantissa_ == 0) = true from by rw [hw_mant0]; rfl)]]
    rfl
  have hof : STAmount.ofNumber asset r mode
      = STAmount.checked asset (0 : Int64).toUInt64 Number.zero.exponent_ neg mode := by
    unfold STAmount.ofNumber
    rw [if_neg (by rw [h_asset_int]; decide), ← hneg_def, ← hw_def, hnz]
  rw [hof] at hok
  have h_unint : ¬ (STAmount.unchecked asset (0 : Int64).toUInt64 Number.zero.exponent_ neg).integral = true := by
    show ¬ asset.integral = true; rw [h_asset_int]; decide
  have hsd0 : (STAmount.unchecked asset (0 : Int64).toUInt64 Number.zero.exponent_ neg).signedDrops.toInt64
      = (0 : Int64) := by
    show (if neg then -(((0 : Int64).toUInt64).toNat : ℤ) else (((0 : Int64).toUInt64).toNat : ℤ)).toInt64
        = (0 : Int64)
    rcases neg <;> decide
  have hiou0 : (STAmount.unchecked asset (0 : Int64).toUInt64 Number.zero.exponent_ neg).iou mode
      = .ok IOUAmount.zero := by
    unfold STAmount.iou
    rw [if_neg h_unint]
    unfold IOUAmount.ofMantissaExp IOUAmount.normalize
    rw [hsd0]
    rfl
  rw [STAmount.checked] at hok
  unfold STAmount.canonicalize at hok
  rw [if_neg h_unint, hiou0] at hok
  simp only at hok
  rw [← Except.ok.inj hok]
  show (if IOUAmount.zero.signum < 0 then -IOUAmount.zero.mantissa_
        else IOUAmount.zero.mantissa_).toUInt64 = 0
  decide

/-- The `Number` product of two normalized operands with bounded exponents has an
exponent well below the `maxExponent` ceiling (`|r| ≈ |n1|·|n2|`, so `r.exp ≲
n1.exp + n2.exp`). Discharges the cusp side condition of the 16-digit re-round. -/
lemma operator_mul_exponent_hi (n1 n2 r : Number) (E : Int)
    (hn1_norm : n1.isNormalized) (hn2_norm : n2.isNormalized)
    (hn1_ne : n1.mantissa_ ≠ 0) (hn2_ne : n2.mantissa_ ≠ 0) (hr_ne : r.mantissa_ ≠ 0)
    (hn1e : n1.exponent_ ≤ E) (hn2e : n2.exponent_ ≤ E) (hE : 2 * E + 24 ≤ maxExponent)
    (hmul : Number.operator_mul n1 n2 .to_nearest = .ok r) :
    r.exponent_ + 4 ≤ maxExponent := by
  by_contra hcon
  push_neg at hcon
  have hr_norm : r.isNormalized := operator_mul_result_isNormalized n1 n2 r .to_nearest
    hn1_norm hn2_norm hn1_ne hn2_ne hmul hr_ne
  have hr_mant := hr_norm.mantissaBounds_nat hr_ne
  -- lower bound: 10^(maxExponent+15) ≤ |r.toRat|.
  have hlower : (10 : ℚ) ^ (maxExponent + 15) ≤ |r.toRat| := by
    rw [abs_toRat_eq r]
    have h1018 : (10 : ℚ) ^ (18 : ℤ) ≤ (r.mantissa_.toNat : ℚ) := by
      have : ((10 ^ 18 : ℕ) : ℚ) ≤ (r.mantissa_.toNat : ℚ) := by exact_mod_cast hr_mant.1
      rwa [show ((10 ^ 18 : ℕ) : ℚ) = (10 : ℚ) ^ (18 : ℤ) by push_cast; norm_num] at this
    calc (10 : ℚ) ^ (maxExponent + 15)
        ≤ (10 : ℚ) ^ (r.exponent_ + 18) := zpow_le_zpow_right₀ (by norm_num) (by omega)
      _ = (10 : ℚ) ^ (18 : ℤ) * (10 : ℚ) ^ r.exponent_ := by
          rw [← zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0)]; ring_nf
      _ ≤ (r.mantissa_.toNat : ℚ) * (10 : ℚ) ^ r.exponent_ := by gcongr
  -- upper bound: |r.toRat| < 10^(maxExponent+15).
  have hmb := operator_mul_rounds_to_nearest n1 n2 r hn1_norm hn2_norm hmul hr_ne
  simp only [RoundsWithin, show RatValued.toRat r = r.toRat from rfl] at hmb
  have hbound_n : ∀ (n : Number), n.mantissa_.toNat < 10 ^ 19 → n.exponent_ ≤ E →
      |n.toRat| < (10 : ℚ) ^ (19 : ℤ) * (10 : ℚ) ^ E := by
    intro n hn_hi hne
    rw [abs_toRat_eq n]
    have hmant : (n.mantissa_.toNat : ℚ) < (10 : ℚ) ^ (19 : ℤ) := by
      have : (n.mantissa_.toNat : ℚ) < ((10 ^ 19 : ℕ) : ℚ) := by exact_mod_cast hn_hi
      rwa [show ((10 ^ 19 : ℕ) : ℚ) = (10 : ℚ) ^ (19 : ℤ) by push_cast; norm_num] at this
    have hexp : (10 : ℚ) ^ n.exponent_ ≤ (10 : ℚ) ^ E := zpow_le_zpow_right₀ (by norm_num) hne
    calc (n.mantissa_.toNat : ℚ) * (10 : ℚ) ^ n.exponent_
        < (10 : ℚ) ^ (19 : ℤ) * (10 : ℚ) ^ n.exponent_ :=
          mul_lt_mul_of_pos_right hmant (zpow_pos (by norm_num) _)
      _ ≤ (10 : ℚ) ^ (19 : ℤ) * (10 : ℚ) ^ E := mul_le_mul_of_nonneg_left hexp (by positivity)
  have hn1b := hbound_n n1 (hn1_norm.mantissaBounds_nat hn1_ne).2 hn1e
  have hn2b := hbound_n n2 (hn2_norm.mantissaBounds_nat hn2_ne).2 hn2e
  have hupper : |r.toRat| < (10 : ℚ) ^ (maxExponent + 15) := by
    have hnn : (0 : ℚ) ≤ |n1.toRat * n2.toRat| := abs_nonneg _
    have hr_le : |r.toRat| ≤ |n1.toRat * n2.toRat| + |n1.toRat * n2.toRat| * (5 / (2 ^ 63 + 7 : ℚ)) := by
      have h := abs_sub_abs_le_abs_sub r.toRat (n1.toRat * n2.toRat); linarith [hmb]
    have hr2 : |r.toRat| ≤ |n1.toRat * n2.toRat| * 2 := by nlinarith [hr_le, hnn]
    have hprod : |n1.toRat * n2.toRat| < (10 : ℚ) ^ (2 * E + 38) := by
      rw [abs_mul]
      have hbig : |n1.toRat| * |n2.toRat|
          < ((10 : ℚ) ^ (19 : ℤ) * (10 : ℚ) ^ E) * ((10 : ℚ) ^ (19 : ℤ) * (10 : ℚ) ^ E) :=
        mul_lt_mul'' hn1b hn2b (abs_nonneg _) (abs_nonneg _)
      have hpoweq : ((10 : ℚ) ^ (19 : ℤ) * (10 : ℚ) ^ E) * ((10 : ℚ) ^ (19 : ℤ) * (10 : ℚ) ^ E)
          = (10 : ℚ) ^ (2 * E + 38) := by
        rw [mul_mul_mul_comm, ← zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0),
            ← zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0), ← zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0)]
        congr 1; ring
      rw [hpoweq] at hbig; exact hbig
    have hpos38 : (0 : ℚ) < (10 : ℚ) ^ (2 * E + 38) := zpow_pos (by norm_num) _
    have hfin : (10 : ℚ) ^ (2 * E + 38) * 2 ≤ (10 : ℚ) ^ (maxExponent + 15) := by
      have h39 : (10 : ℚ) ^ (2 * E + 38) * 2 ≤ (10 : ℚ) ^ (2 * E + 38) * 10 := by nlinarith [hpos38]
      have heq : (10 : ℚ) ^ (2 * E + 38) * 10 = (10 : ℚ) ^ (2 * E + 39) := by
        rw [show (2 * E + 39 : ℤ) = (2 * E + 38) + 1 from by ring,
            zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0) (2 * E + 38) 1, zpow_one]
      rw [heq] at h39
      exact le_trans h39 (zpow_le_zpow_right₀ (by norm_num) (by omega))
    calc |r.toRat| ≤ |n1.toRat * n2.toRat| * 2 := hr2
      _ < (10 : ℚ) ^ (2 * E + 38) * 2 := by nlinarith [hprod, hpos38]
      _ ≤ (10 : ℚ) ^ (maxExponent + 15) := hfin
  linarith [hlower, hupper]

/-- **STAmount IOU multiplication rel-error engine (`to_nearest`).** Two canonical
16-digit IOU amounts multiply (lift to 19-digit `Number`s, multiply, snap back to
the 16-digit grid) within the composed double-rounding relative error
`εMul = ε₁ + ε₂ + ε₁·ε₂`, `ε₁ = 5/(2⁶³+7)`, `ε₂ = 10⁻¹⁵`. -/
theorem STAmount.operator_mul_iou_rel_error (v1 v2 result : STAmount) (asset : Asset) (iss : Issue)
    (hv1 : v1.mAsset = .issue iss) (hv2 : v2.mAsset = .issue iss) (h_xrp : iss.isXRP = false)
    (ha_iou : asset.holdsIssue = true) (ha_not_xrp : asset.isNative = false)
    (hc1 : v1.IOUCanonical) (hc2 : v2.IOUCanonical)
    (hok : STAmount.multiply v1 v2 asset .to_nearest = .ok result)
    (hresult : result.mValue ≠ 0) :
    |result.toRat - (v1.toRat * v2.toRat)|
      ≤ |v1.toRat * v2.toRat| *
        (5 / (2 ^ 63 + 7 : ℚ) + (1 / 2) * (10 : ℚ) ^ (-15 : ℤ)
          + 5 / (2 ^ 63 + 7 : ℚ) * ((1 / 2) * (10 : ℚ) ^ (-15 : ℤ))) := by
  have hv1ne : v1.mValue ≠ 0 := by intro h; have := hc1.mant_lo; rw [h] at this; simp at this
  have hv2ne : v2.mValue ≠ 0 := by intro h; have := hc2.mant_lo; rw [h] at this; simp at this
  have hv1nat : v1.native = false := by
    unfold STAmount.native; rw [hv1]; show iss.native = false; rw [← h_xrp]; rfl
  have hv1mpt : v1.holdsMPTIssue = false := by unfold STAmount.holdsMPTIssue; rw [hv1]; rfl
  -- navigate guards down to the `Number` path
  unfold STAmount.multiply at hok
  rw [if_neg (show ¬ (v1.isZero || v2.isZero) = true from by
        simp [STAmount.isZero, hv1ne, hv2ne]),
      if_neg (show ¬ (v1.native && v2.native && asset.isNative) = true from by
        simp [hv1nat]),
      if_neg (show ¬ (v1.holdsMPTIssue && v2.holdsMPTIssue && asset.holdsMPTIssue) = true from by
        simp [hv1mpt]),
      STAmount.toNumber_iou_canonical v1 iss .to_nearest hv1 h_xrp hc1,
      STAmount.toNumber_iou_canonical v2 iss .to_nearest hv2 h_xrp hc2] at hok
  simp only at hok
  set n1 : Number := ⟨v1.mIsNegative, v1.mValue * 10 * 10 * 10, v1.mOffset - 3⟩ with hn1_def
  set n2 : Number := ⟨v2.mIsNegative, v2.mValue * 10 * 10 * 10, v2.mOffset - 3⟩ with hn2_def
  cases hmul : Number.operator_mul n1 n2 .to_nearest with
  | error e => rw [hmul] at hok; simp at hok
  | ok r =>
    rw [hmul] at hok
    simp only at hok
    have hofn : STAmount.ofNumber asset r .to_nearest = .ok result := hok
    have hn1_norm : n1.isNormalized := STAmount.toNumber_iou_canonical_isNormalized v1 hc1
    have hn2_norm : n2.isNormalized := STAmount.toNumber_iou_canonical_isNormalized v2 hc2
    have hn1_ne : n1.mantissa_ ≠ 0 := STAmount.toNumber_iou_canonical_mantissa_ne v1 hc1
    have hn2_ne : n2.mantissa_ ≠ 0 := STAmount.toNumber_iou_canonical_mantissa_ne v2 hc2
    have hn1_val : n1.toRat = v1.toRat := STAmount.toNumber_iou_canonical_toRat v1 hc1
    have hn2_val : n2.toRat = v2.toRat := STAmount.toNumber_iou_canonical_toRat v2 hc2
    have hr_ne : r.mantissa_ ≠ 0 :=
      STAmount.ofNumber_iou_mantissa_ne_zero asset r .to_nearest result ha_iou ha_not_xrp hofn hresult
    have hr_norm := operator_mul_result_isNormalized n1 n2 r .to_nearest
      hn1_norm hn2_norm hn1_ne hn2_ne hmul hr_ne
    have hr_mant := hr_norm.mantissaBounds_nat hr_ne
    have hr_exp_lo : minExponent ≤ r.exponent_ := by
      rcases hr_norm with hz | ⟨_, _, _, hlo, _⟩
      · exact absurd (show r.mantissa_ = 0 by rw [hz]; rfl) hr_ne
      · exact hlo
    have hr_exp_hi : r.exponent_ + 4 ≤ maxExponent :=
      operator_mul_exponent_hi n1 n2 r 77 hn1_norm hn2_norm hn1_ne hn2_ne hr_ne
        (by show v1.mOffset - 3 ≤ 77; have := hc1.exp_hi; omega)
        (by show v2.mOffset - 3 ≤ 77; have := hc2.exp_hi; omega) (by unfold maxExponent; omega) hmul
    -- the 16-digit re-round bound (tight, half-ULP), recast as a relative error in `|r.toRat|`.
    have hofn_bound := STAmount.ofNumber_iou_within_half_ulp asset r result
      ha_iou ha_not_xrp hr_mant.1 hr_mant.2 hr_exp_lo hr_exp_hi hofn hresult
    have hr_abs : |r.toRat| = (r.mantissa_.toNat : ℚ) * 10 ^ r.exponent_ := abs_toRat_eq r
    have hold : (10 : ℚ) ^ (r.exponent_ + 3) ≤ |r.toRat| * (10 : ℚ) ^ (-15 : ℤ) := by
      rw [hr_abs]
      have hpow : (10 : ℚ) ^ (r.exponent_ + 3)
          = (10 : ℚ) ^ (18 : ℤ) * (10 : ℚ) ^ (r.exponent_ - 15) := by
        rw [← zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0)]; ring_nf
      have hmul' : (r.mantissa_.toNat : ℚ) * 10 ^ r.exponent_ * (10 : ℚ) ^ (-15 : ℤ)
          = (r.mantissa_.toNat : ℚ) * (10 : ℚ) ^ (r.exponent_ - 15) := by
        rw [mul_assoc, ← zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0)]; ring_nf
      rw [hpow, hmul']
      have hlo_q : (10 : ℚ) ^ (18 : ℤ) ≤ (r.mantissa_.toNat : ℚ) := by
        have : ((10 ^ 18 : ℕ) : ℚ) ≤ (r.mantissa_.toNat : ℚ) := by exact_mod_cast hr_mant.1
        rwa [show ((10 ^ 18 : ℕ) : ℚ) = (10 : ℚ) ^ (18 : ℤ) by push_cast; norm_num] at this
      gcongr
    have h2 : |result.toRat - r.toRat| ≤ |r.toRat| * ((1 / 2) * (10 : ℚ) ^ (-15 : ℤ)) := by
      calc |result.toRat - r.toRat| ≤ (1 / 2) * (10 : ℚ) ^ (r.exponent_ + 3) := hofn_bound.1
        _ ≤ (1 / 2) * (|r.toRat| * (10 : ℚ) ^ (-15 : ℤ)) := by
            apply mul_le_mul_of_nonneg_left hold (by norm_num)
        _ = |r.toRat| * ((1 / 2) * (10 : ℚ) ^ (-15 : ℤ)) := by ring
    -- the `Number`-multiply relative-error bound, with truth `v1.toRat * v2.toRat`.
    have hmb := operator_mul_rounds_to_nearest n1 n2 r hn1_norm hn2_norm hmul hr_ne
    simp only [RoundsWithin, show RatValued.toRat r = r.toRat from rfl] at hmb
    rw [hn1_val, hn2_val] at hmb
    exact rel_error_trans hmb h2 (by positivity)

/-- **IOU multiply pipeline decomposition (`to_nearest`).** Exposes the 19-digit
`Number` product `r` (`= ofNumber → result`), its range, and the `Number`-multiply
relative-error bound. Feeds both the relative headline and the ULP/repr headline. -/
lemma STAmount.operator_mul_iou_decompose (v1 v2 result : STAmount) (asset : Asset) (iss : Issue)
    (hv1 : v1.mAsset = .issue iss) (hv2 : v2.mAsset = .issue iss) (h_xrp : iss.isXRP = false)
    (ha_iou : asset.holdsIssue = true) (ha_not_xrp : asset.isNative = false)
    (hc1 : v1.IOUCanonical) (hc2 : v2.IOUCanonical)
    (hok : STAmount.multiply v1 v2 asset .to_nearest = .ok result) (hresult : result.mValue ≠ 0) :
    ∃ r : Number, STAmount.ofNumber asset r .to_nearest = .ok result ∧
      10 ^ 18 ≤ r.mantissa_.toNat ∧ r.mantissa_.toNat < 10 ^ 19 ∧
      minExponent ≤ r.exponent_ ∧ r.exponent_ + 4 ≤ maxExponent ∧
      |r.toRat - v1.toRat * v2.toRat| ≤ |v1.toRat * v2.toRat| * (5 / (2 ^ 63 + 7 : ℚ)) := by
  have hv1ne : v1.mValue ≠ 0 := by intro h; have := hc1.mant_lo; rw [h] at this; simp at this
  have hv2ne : v2.mValue ≠ 0 := by intro h; have := hc2.mant_lo; rw [h] at this; simp at this
  have hv1nat : v1.native = false := by
    unfold STAmount.native; rw [hv1]; show iss.native = false; rw [← h_xrp]; rfl
  have hv1mpt : v1.holdsMPTIssue = false := by unfold STAmount.holdsMPTIssue; rw [hv1]; rfl
  unfold STAmount.multiply at hok
  rw [if_neg (show ¬ (v1.isZero || v2.isZero) = true from by simp [STAmount.isZero, hv1ne, hv2ne]),
      if_neg (show ¬ (v1.native && v2.native && asset.isNative) = true from by simp [hv1nat]),
      if_neg (show ¬ (v1.holdsMPTIssue && v2.holdsMPTIssue && asset.holdsMPTIssue) = true from by
        simp [hv1mpt]),
      STAmount.toNumber_iou_canonical v1 iss .to_nearest hv1 h_xrp hc1,
      STAmount.toNumber_iou_canonical v2 iss .to_nearest hv2 h_xrp hc2] at hok
  simp only at hok
  set n1 : Number := ⟨v1.mIsNegative, v1.mValue * 10 * 10 * 10, v1.mOffset - 3⟩ with hn1_def
  set n2 : Number := ⟨v2.mIsNegative, v2.mValue * 10 * 10 * 10, v2.mOffset - 3⟩ with hn2_def
  cases hmul : Number.operator_mul n1 n2 .to_nearest with
  | error e => rw [hmul] at hok; simp at hok
  | ok r =>
    rw [hmul] at hok; simp only at hok
    have hofn : STAmount.ofNumber asset r .to_nearest = .ok result := hok
    have hn1_norm := STAmount.toNumber_iou_canonical_isNormalized v1 hc1
    have hn2_norm := STAmount.toNumber_iou_canonical_isNormalized v2 hc2
    have hn1_ne := STAmount.toNumber_iou_canonical_mantissa_ne v1 hc1
    have hn2_ne := STAmount.toNumber_iou_canonical_mantissa_ne v2 hc2
    have hn1_val := STAmount.toNumber_iou_canonical_toRat v1 hc1
    have hn2_val := STAmount.toNumber_iou_canonical_toRat v2 hc2
    have hr_ne := STAmount.ofNumber_iou_mantissa_ne_zero asset r .to_nearest result ha_iou ha_not_xrp
      hofn hresult
    have hr_norm := operator_mul_result_isNormalized n1 n2 r .to_nearest hn1_norm hn2_norm hn1_ne
      hn2_ne hmul hr_ne
    have hr_mant := hr_norm.mantissaBounds_nat hr_ne
    have hr_exp_lo : minExponent ≤ r.exponent_ := by
      rcases hr_norm with hz | ⟨_, _, _, hlo, _⟩
      · exact absurd (show r.mantissa_ = 0 by rw [hz]; rfl) hr_ne
      · exact hlo
    have hr_exp_hi := operator_mul_exponent_hi n1 n2 r 77 hn1_norm hn2_norm hn1_ne hn2_ne hr_ne
      (by show v1.mOffset - 3 ≤ 77; have := hc1.exp_hi; omega)
      (by show v2.mOffset - 3 ≤ 77; have := hc2.exp_hi; omega) (by unfold maxExponent; omega) hmul
    have hmb := operator_mul_rounds_to_nearest n1 n2 r hn1_norm hn2_norm hmul hr_ne
    simp only [RoundsWithin, show RatValued.toRat r = r.toRat from rfl] at hmb
    rw [hn1_val, hn2_val] at hmb
    exact ⟨r, hofn, hr_mant.1, hr_mant.2, hr_exp_lo, hr_exp_hi, hmb⟩

end XRPL.Model.Protocol
