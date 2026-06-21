import XRPL.Properties.Protocol.Number.Common.Rounding.SmallRangeBound
import XRPL.Properties.Protocol.Number.Normalize.Common.ResultFacts
import XRPL.Properties.Protocol.Number.Add.RoundsWithin
import XRPL.Properties.Protocol.Number.Add.Common.Rounded
import XRPL.Properties.Protocol.Number.Add.Common.SameSignDecompose
import XRPL.Properties.Protocol.Number.Compare.Compare
import XRPL.Properties.Protocol.STAmount.Common.DiscreteDefs
import XRPL.Properties.Protocol.STAmount.Common.RoundToScaleHelpers
import XRPL.Model.Protocol.STAmount

namespace XRPL.Model.Protocol

/-- `IOUAmount.toRat` in signed-mantissa form. -/
lemma IOUAmount.toRat_eq (a : IOUAmount) :
    a.toRat = (a.mantissa_.toInt : ℚ) * (10 : ℚ) ^ a.exponent_ := by
  have hsm : (if a.mantissa_ < 0 then (-1 : ℤ) else 1) * (a.mantissa_.toInt.natAbs : ℤ)
      = a.mantissa_.toInt := by
    by_cases h : a.mantissa_ < 0
    · rw [if_pos h]
      have hlt : a.mantissa_.toInt < 0 := by simpa using Int64.lt_iff_toInt_lt.mp h
      rw [Int.ofNat_natAbs_of_nonpos (le_of_lt hlt)]; ring
    · rw [if_neg h]
      have hge : 0 ≤ a.mantissa_.toInt := by
        rcases lt_or_ge a.mantissa_.toInt 0 with hh | hh
        · exact absurd (Int64.lt_iff_toInt_lt.mpr (by simpa using hh)) h
        · exact hh
      rw [Int.natAbs_of_nonneg hge]; ring
  unfold IOUAmount.toRat
  by_cases hexp : a.exponent_ ≥ 0
  · rw [if_pos hexp, hsm, Rat.mkRat_one]
    have h_to : a.exponent_ = (a.exponent_.toNat : ℤ) := (Int.toNat_of_nonneg hexp).symm
    rw [h_to, zpow_natCast]; push_cast; rw [Int.toNat_natCast]
  · rw [if_neg hexp, hsm, Rat.mkRat_eq_div]
    push_cast
    have h_neg : a.exponent_ = -((-a.exponent_).toNat : ℤ) := by
      rw [Int.toNat_of_nonneg (by omega : (0 : ℤ) ≤ -a.exponent_)]; ring
    conv_rhs => rw [h_neg, zpow_neg, zpow_natCast]
    field_simp

/-- `|IOUAmount.toRat|` in signed-mantissa form. -/
lemma IOUAmount.abs_toRat_eq (a : IOUAmount) :
    |a.toRat| = (a.mantissa_.toInt.natAbs : ℚ) * (10 : ℚ) ^ a.exponent_ := by
  rw [IOUAmount.toRat_eq a, abs_mul,
      abs_of_pos (zpow_pos (by norm_num : (0 : ℚ) < 10) a.exponent_)]
  congr 1
  rw [Nat.cast_natAbs]; push_cast; rfl

/-- **`IOUAmount.ofNumber` re-rounding bound.** Snapping a 19-digit-normalized
`Number` to the 16-digit IOU range loses at most one 16-digit ULP (provided the
result is in range, `result.mantissa_ ≠ 0` rules out an underflow flush). -/
lemma IOUAmount.ofNumber_within_ulp (sum : Number) (mode : rounding_mode) (result : IOUAmount)
    (h_lo : 10 ^ 18 ≤ sum.mantissa_.toNat) (h_hi : sum.mantissa_.toNat < 10 ^ 19)
    (he_lo : minExponent ≤ sum.exponent_ + 3) (he_hi : sum.exponent_ + 4 ≤ maxExponent)
    (hok : IOUAmount.ofNumber sum mode = .ok result) (hne : result.mantissa_ ≠ 0) :
    |result.toRat - sum.toRat| ≤ (10 : ℚ) ^ (sum.exponent_ + 3) := by
  unfold IOUAmount.ofNumber IOUAmount.fromNumber at hok
  cases hnorm : sum.normalizeToRange cMinValue cMaxValue mode with
  | error err => rw [hnorm] at hok; simp at hok
  | ok me =>
    obtain ⟨m, e⟩ := me
    rw [hnorm] at hok
    simp only at hok
    by_cases hhi : e > cMaxOffset
    · rw [if_pos hhi] at hok; simp at hok
    rw [if_neg hhi] at hok
    by_cases hlo : e < cMinOffset
    · rw [if_pos hlo] at hok
      rw [← Except.ok.inj hok] at hne; exact absurd rfl hne
    rw [if_neg hlo] at hok
    have hres : result = ⟨m, e⟩ := (Except.ok.inj hok).symm
    have hbound := normalizeToRange_16_within_ulp sum mode m e h_lo h_hi he_lo he_hi hnorm
    rw [IOUAmount.toRat_eq result, hres]
    exact hbound

/-- **Tight (half-ULP) `IOUAmount.ofNumber` bound, `to_nearest`.** The 16-digit snap
in round-to-nearest mode lands within *half* a 16-digit ULP of the source. Thin
wrapper over `normalizeToRange_16_within_half_ulp`; halves the re-round error of every
`to_nearest` IOU arithmetic bound. -/
lemma IOUAmount.ofNumber_within_half_ulp (sum : Number) (result : IOUAmount)
    (h_lo : 10 ^ 18 ≤ sum.mantissa_.toNat) (h_hi : sum.mantissa_.toNat < 10 ^ 19)
    (he_lo : minExponent ≤ sum.exponent_ + 3) (he_hi : sum.exponent_ + 4 ≤ maxExponent)
    (hok : IOUAmount.ofNumber sum .to_nearest = .ok result) (hne : result.mantissa_ ≠ 0) :
    |result.toRat - sum.toRat| ≤ (1 / 2 : ℚ) * (10 : ℚ) ^ (sum.exponent_ + 3) := by
  unfold IOUAmount.ofNumber IOUAmount.fromNumber at hok
  cases hnorm : sum.normalizeToRange cMinValue cMaxValue .to_nearest with
  | error err => rw [hnorm] at hok; simp at hok
  | ok me =>
    obtain ⟨m, e⟩ := me
    rw [hnorm] at hok
    simp only at hok
    by_cases hhi : e > cMaxOffset
    · rw [if_pos hhi] at hok; simp at hok
    rw [if_neg hhi] at hok
    by_cases hlo : e < cMinOffset
    · rw [if_pos hlo] at hok
      rw [← Except.ok.inj hok] at hne; exact absurd rfl hne
    rw [if_neg hlo] at hok
    have hres : result = ⟨m, e⟩ := (Except.ok.inj hok).symm
    have hbound := normalizeToRange_16_within_half_ulp sum m e h_lo h_hi he_lo he_hi hnorm
    rw [IOUAmount.toRat_eq result, hres]
    exact hbound

/-- The `IOUAmount.ofNumber` result exponent is at least `sum.exponent_ + 3` (it is
`+3` or `+4`), so `10^(sum.exp+3) ≤ 10^result.exp`, i.e. the re-rounding ULP bound
is at most one *result* ULP. -/
lemma IOUAmount.ofNumber_exp_ge (sum : Number) (mode : rounding_mode) (result : IOUAmount)
    (h_lo : 10 ^ 18 ≤ sum.mantissa_.toNat) (h_hi : sum.mantissa_.toNat < 10 ^ 19)
    (he_lo : minExponent ≤ sum.exponent_ + 3) (he_hi : sum.exponent_ + 4 ≤ maxExponent)
    (hok : IOUAmount.ofNumber sum mode = .ok result) (hne : result.mantissa_ ≠ 0) :
    sum.exponent_ + 3 ≤ result.exponent_ := by
  unfold IOUAmount.ofNumber IOUAmount.fromNumber at hok
  cases hnorm : sum.normalizeToRange cMinValue cMaxValue mode with
  | error err => rw [hnorm] at hok; simp at hok
  | ok me =>
    obtain ⟨m, e⟩ := me
    rw [hnorm] at hok
    simp only at hok
    by_cases hhi : e > cMaxOffset
    · rw [if_pos hhi] at hok; simp at hok
    rw [if_neg hhi] at hok
    by_cases hlo : e < cMinOffset
    · rw [if_pos hlo] at hok
      rw [← Except.ok.inj hok] at hne; exact absurd rfl hne
    rw [if_neg hlo] at hok
    have hres : result = ⟨m, e⟩ := (Except.ok.inj hok).symm
    have herange := normalizeToRange_16_exp_range sum mode m e h_lo h_hi he_lo he_hi hnorm
    rw [hres]; exact herange.1

/-- A nonzero `IOUAmount.ofNumber` result forces a nonzero source mantissa: a
zero-mantissa `Number` normalizes to `(0, _)` and clamps to `IOUAmount.zero`. -/
lemma IOUAmount.ofNumber_mantissa_ne_zero (sum : Number) (mode : rounding_mode) (result : IOUAmount)
    (hofn : IOUAmount.ofNumber sum mode = .ok result) (hresult : result.mantissa_ ≠ 0) :
    sum.mantissa_ ≠ 0 := by
  intro hsm
  apply hresult
  have hbeq : (sum.mantissa_ == 0) = true := by simp [hsm]
  have hdn : doNormalize sum.negative_ sum.mantissa_ sum.exponent_ cMinValue cMaxValue mode
      = .ok Number.zero := by
    unfold doNormalize; rw [if_pos hbeq]
  have hnz : sum.normalizeToRange cMinValue cMaxValue mode
      = .ok ((0 : Int64), (-2147483648 : Int)) := by
    unfold Number.normalizeToRange; rw [hdn]; rfl
  unfold IOUAmount.ofNumber IOUAmount.fromNumber at hofn
  rw [hnz] at hofn
  simp only at hofn
  rw [if_neg (by decide : ¬ ((-2147483648 : Int) > cMaxOffset)),
      if_pos (by decide : (-2147483648 : Int) < cMinOffset)] at hofn
  rw [← Except.ok.inj hofn]; rfl

/-- An `IOUAmount.ofNumber` result (when nonzero) is a canonical 16-digit IOU amount:
mantissa magnitude in `[10^15, 10^16)` (from the re-rounding output range) and exponent
in `[-96, 80]` (from the `cMinOffset`/`cMaxOffset` clamps that `hofn = .ok` passed). -/
lemma IOUAmount.ofNumber_InRange16 (sum : Number) (mode : rounding_mode) (result : IOUAmount)
    (h_lo : 10 ^ 18 ≤ sum.mantissa_.toNat) (h_hi : sum.mantissa_.toNat < 10 ^ 19)
    (he_lo : minExponent ≤ sum.exponent_ + 3) (he_hi : sum.exponent_ + 4 ≤ maxExponent)
    (hofn : IOUAmount.ofNumber sum mode = .ok result) (hresult : result.mantissa_ ≠ 0) :
    result.InRange16 := by
  unfold IOUAmount.ofNumber IOUAmount.fromNumber at hofn
  cases hnorm : sum.normalizeToRange cMinValue cMaxValue mode with
  | error err => rw [hnorm] at hofn; simp at hofn
  | ok me =>
    obtain ⟨m, e⟩ := me
    rw [hnorm] at hofn
    simp only at hofn
    by_cases hhi : e > cMaxOffset
    · rw [if_pos hhi] at hofn; simp at hofn
    rw [if_neg hhi] at hofn
    by_cases hlo : e < cMinOffset
    · rw [if_pos hlo] at hofn
      rw [← Except.ok.inj hofn] at hresult; exact absurd rfl hresult
    rw [if_neg hlo] at hofn
    have hres : result = ⟨m, e⟩ := (Except.ok.inj hofn).symm
    have hmant := normalizeToRange_16_mantissa_range sum mode m e h_lo h_hi he_lo he_hi hnorm
    rw [hres]
    refine ⟨?_, ?_, ?_, ?_⟩
    · show 10 ^ 15 ≤ m.toInt.natAbs
      have h := hmant.1; rwa [show cMinValue.toNat = 10 ^ 15 from by decide] at h
    · show m.toInt.natAbs < 10 ^ 16
      have h := hmant.2; rw [show cMaxValue.toNat = 10 ^ 16 - 1 from by decide] at h; omega
    · show (-96 : ℤ) ≤ e; rw [show cMinOffset = (-96 : ℤ) from rfl] at hlo; omega
    · show e ≤ (80 : ℤ); rw [show cMaxOffset = (80 : ℤ) from rfl] at hhi; omega

/-- **`IOUAmount.toNumber` is exact** on a canonical 16-digit IOU amount: the
19-digit constructor scales the mantissa up by exactly `×1000`, preserving the
value; the result is normalized and nonzero. -/
lemma IOUAmount.toNumber_canonical (i : IOUAmount) (mode : rounding_mode)
    (h_lo : 10 ^ 15 ≤ i.mantissa_.toInt.natAbs) (h_hi : i.mantissa_.toInt.natAbs < 10 ^ 16)
    (he_lo : minExponent + 3 ≤ i.exponent_) (he_hi : i.exponent_ - 3 < maxExponent) :
    ∃ xn : Number, i.toNumber mode = .ok xn ∧ xn.toRat = i.toRat ∧
      xn.isNormalized ∧ xn.mantissa_ ≠ 0 := by
  set m₀ : UInt64 := i.mantissa_.toInt.natAbs.toUInt64 with hm0def
  set neg : Bool := decide (i.mantissa_ < 0) with hnegdef
  have hm0 : m₀.toNat = i.mantissa_.toInt.natAbs := by
    rw [hm0def]; apply UInt64.toNat_ofNat_of_lt; show i.mantissa_.toInt.natAbs < 2 ^ 64; omega
  have hmin : largeRange.min.toNat = 1000000000000000000 := largeRange_min_val
  have hm0_ne : m₀ ≠ 0 := by
    intro h; rw [h] at hm0; simp at hm0; omega
  have hm1 : (m₀ * 10).toNat = m₀.toNat * 10 :=
    m_mul_ten_no_overflow (by rw [hm0, hmin]; omega)
  have hm2 : (m₀ * 10 * 10).toNat = m₀.toNat * 100 := by
    rw [m_mul_ten_no_overflow (by rw [hm1, hm0, hmin]; omega), hm1]; ring
  have hm3 : (m₀ * 10 * 10 * 10).toNat = m₀.toNat * 1000 := by
    rw [m_mul_ten_no_overflow (by rw [hm2, hm0, hmin]; omega), hm2]; ring
  have hm3_ne : m₀ * 10 * 10 * 10 ≠ 0 := by
    intro h; rw [h] at hm3; simp at hm3; omega
  have hdn := doNormalize_large_16digit neg m₀ i.exponent_ mode
    (by rw [hm0]; exact h_lo) (by rw [hm0]; exact h_hi) he_lo he_hi
  have htoNum : i.toNumber mode
      = doNormalize neg m₀ i.exponent_ largeRange.min largeRange.max mode := by
    unfold IOUAmount.toNumber Number.from_rep Number.normalized Number.normalize; rfl
  have hmant : (⟨neg, m₀ * 10 * 10 * 10, i.exponent_ - 3⟩ : Number).mantissa_ = m₀ * 10 * 10 * 10 := rfl
  have hexpn : (⟨neg, m₀ * 10 * 10 * 10, i.exponent_ - 3⟩ : Number).exponent_ = i.exponent_ - 3 := rfl
  refine ⟨⟨neg, m₀ * 10 * 10 * 10, i.exponent_ - 3⟩, by rw [htoNum]; exact hdn, ?_, ?_, hm3_ne⟩
  · -- value preserved
    have hpow : (10 : ℚ) ^ i.exponent_ = (10 : ℚ) ^ (i.exponent_ - 3) * 1000 := by
      rw [show i.exponent_ = (i.exponent_ - 3) + 3 from by ring,
          zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0)]; norm_num
    rw [IOUAmount.toRat_eq i]
    by_cases hsign : i.mantissa_ < 0
    · have hnegtrue : neg = true := by rw [hnegdef]; exact decide_eq_true hsign
      have hlt : i.mantissa_.toInt < 0 := by
        have h := Int64.lt_iff_toInt_lt.mp hsign
        rwa [show (0 : Int64).toInt = 0 from by decide] at h
      have htoInt_q : (i.mantissa_.toInt : ℚ) = -(i.mantissa_.toInt.natAbs : ℚ) := by
        have hcast : (i.mantissa_.toInt.natAbs : ℚ) = |(i.mantissa_.toInt : ℚ)| := by
          rw [Nat.cast_natAbs]; push_cast; rfl
        rw [hcast, abs_of_neg (by exact_mod_cast hlt)]; ring
      rw [Number.toRat_of_neg _ hnegtrue, hmant, hexpn, hm3, hm0, hpow, htoInt_q]
      push_cast; ring
    · have hnegfalse : neg = false := by rw [hnegdef]; exact decide_eq_false hsign
      have hge : 0 ≤ i.mantissa_.toInt := by
        rcases lt_or_ge i.mantissa_.toInt 0 with hh | hh
        · exact absurd (Int64.lt_iff_toInt_lt.mpr
            (by rw [show (0 : Int64).toInt = 0 from by decide]; exact hh)) hsign
        · exact hh
      have htoInt_q : (i.mantissa_.toInt : ℚ) = (i.mantissa_.toInt.natAbs : ℚ) := by
        have hcast : (i.mantissa_.toInt.natAbs : ℚ) = |(i.mantissa_.toInt : ℚ)| := by
          rw [Nat.cast_natAbs]; push_cast; rfl
        rw [hcast, abs_of_nonneg (by exact_mod_cast hge)]
      rw [Number.toRat_of_nonneg _ hnegfalse, hmant, hexpn, hm3, hm0, hpow, htoInt_q]
      push_cast; ring
  · apply normalize_result_isNormalized (Number.unchecked neg m₀ i.exponent_) _ mode
      (by show m₀ ≠ 0; exact hm0_ne)
    · show (Number.unchecked neg m₀ i.exponent_).normalize largeRange.min largeRange.max mode
        = .ok ⟨neg, m₀ * 10 * 10 * 10, i.exponent_ - 3⟩
      unfold Number.normalize; exact hdn
    · show (⟨neg, m₀ * 10 * 10 * 10, i.exponent_ - 3⟩ : Number).mantissa_ ≠ 0; exact hm3_ne

/-- The `operator_add` result (nonzero mantissa) is normalized, for **either**
sign combination: same-sign via the post-alignment spec, diff-sign via the
`doNormalize128` keystone. -/
theorem operator_add_result_isNormalized_anyMode (x y result : Number) (mode : rounding_mode)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (h_not_zero : ¬ x.operator_eq y.operator_neg)
    (hok : Number.operator_add x y mode = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    result.isNormalized := by
  by_cases hs : x.negative_ = y.negative_
  · obtain ⟨_, _, _, _, _, hspec⟩ :=
      operator_add_algorithmic_facts_same_sign_anyMode x y result mode hx hy
        hx_mant_ne hy_mant_ne hs h_not_zero hok
    exact hspec.result_norm
  · exact operator_add_result_isNormalized x y result mode hx hy
      hx_mant_ne hy_mant_ne hs h_not_zero hok hresult

/-- The exact `Number` sum of two in-range IOU amounts has an exponent comfortably
below the `maxExponent` ceiling: `|sum| ≤ (|x|+|y|)(1+ε)` while a normalized `sum`
has `|sum| ≥ 10^18·10^sum.exp`, so `sum.exp` cannot exceed the operands' scale by
more than a couple of digits. This discharges the cusp-rescale side condition
(`sum.exp + 4 ≤ maxExponent`) of the 16-digit re-rounding bound. -/
lemma IOUAmount.add_sum_exponent_hi (x y : IOUAmount) (xn yn sum : Number)
    (hxn_val : xn.toRat = x.toRat) (hyn_val : yn.toRat = y.toRat)
    (hxn_norm : xn.isNormalized) (hyn_norm : yn.isNormalized)
    (hxn_ne : xn.mantissa_ ≠ 0) (hyn_ne : yn.mantissa_ ≠ 0)
    (h_no_cancel : ¬ xn.operator_eq yn.operator_neg)
    (hsum_ne : sum.mantissa_ ≠ 0)
    (hx_hi : x.mantissa_.toInt.natAbs < 10 ^ 16) (hy_hi : y.mantissa_.toInt.natAbs < 10 ^ 16)
    (hxe : x.exponent_ ≤ maxExponent - 4) (hye : y.exponent_ ≤ maxExponent - 4)
    (hadd : Number.operator_add xn yn .to_nearest = .ok sum) :
    sum.exponent_ + 4 ≤ maxExponent := by
  by_contra hcon
  push_neg at hcon
  -- `hcon : maxExponent < sum.exponent_ + 4`, i.e. `maxExponent - 3 ≤ sum.exponent_`.
  have hge : maxExponent - 3 ≤ sum.exponent_ := by omega
  have hsum_norm := operator_add_result_isNormalized_anyMode xn yn sum .to_nearest
    hxn_norm hyn_norm hxn_ne hyn_ne h_no_cancel hadd hsum_ne
  have hsum_mant := hsum_norm.mantissaBounds_nat hsum_ne
  -- Lower bound: 10^(maxExponent+15) ≤ 10^(sum.exp+18) ≤ |sum.toRat|.
  have hlower : (10 : ℚ) ^ (maxExponent + 15) ≤ |sum.toRat| := by
    rw [_root_.XRPL.Model.Protocol.abs_toRat_eq sum]
    have h1018 : (10 : ℚ) ^ (18 : ℤ) ≤ (sum.mantissa_.toNat : ℚ) := by
      have : ((10 ^ 18 : ℕ) : ℚ) ≤ (sum.mantissa_.toNat : ℚ) := by exact_mod_cast hsum_mant.1
      rwa [show ((10 ^ 18 : ℕ) : ℚ) = (10 : ℚ) ^ (18 : ℤ) by push_cast; norm_num] at this
    calc (10 : ℚ) ^ (maxExponent + 15)
        ≤ (10 : ℚ) ^ (sum.exponent_ + 18) :=
          zpow_le_zpow_right₀ (by norm_num) (by omega)
      _ = (10 : ℚ) ^ (18 : ℤ) * (10 : ℚ) ^ sum.exponent_ := by
          rw [← zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0)]; ring_nf
      _ ≤ (sum.mantissa_.toNat : ℚ) * (10 : ℚ) ^ sum.exponent_ := by gcongr
  -- Upper bound: |sum.toRat| < 10^(maxExponent+15), via |sum| ≤ (|x|+|y|)·(1+ε).
  have hadd_bound := operator_add_rounds_to_nearest xn yn sum hxn_norm hyn_norm
    hxn_ne hyn_ne h_no_cancel hadd hsum_ne
  simp only [RoundsWithin] at hadd_bound
  rw [hxn_val, hyn_val, show RatValued.toRat sum = sum.toRat from rfl] at hadd_bound
  -- |x|, |y| ≤ 10^16 · 10^(maxExponent-4) = 10^(maxExponent+12).
  have hbound_xy : ∀ (a : IOUAmount), a.mantissa_.toInt.natAbs < 10 ^ 16 →
      a.exponent_ ≤ maxExponent - 4 → |a.toRat| < (10 : ℚ) ^ (maxExponent + 12) := by
    intro a ha_hi ha_e
    rw [IOUAmount.abs_toRat_eq a]
    have hpow : (10 : ℚ) ^ (maxExponent + 12) = (10 : ℚ) ^ (16 : ℤ) * (10 : ℚ) ^ (maxExponent - 4) := by
      rw [← zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0)]; ring_nf
    rw [hpow]
    have hmant : (a.mantissa_.toInt.natAbs : ℚ) < (10 : ℚ) ^ (16 : ℤ) := by
      have : (a.mantissa_.toInt.natAbs : ℚ) < ((10 ^ 16 : ℕ) : ℚ) := by exact_mod_cast ha_hi
      rwa [show ((10 ^ 16 : ℕ) : ℚ) = (10 : ℚ) ^ (16 : ℤ) by push_cast; norm_num] at this
    have hexp : (10 : ℚ) ^ a.exponent_ ≤ (10 : ℚ) ^ (maxExponent - 4) :=
      zpow_le_zpow_right₀ (by norm_num) ha_e
    calc (a.mantissa_.toInt.natAbs : ℚ) * (10 : ℚ) ^ a.exponent_
        < (10 : ℚ) ^ (16 : ℤ) * (10 : ℚ) ^ a.exponent_ :=
          mul_lt_mul_of_pos_right hmant (zpow_pos (by norm_num) _)
      _ ≤ (10 : ℚ) ^ (16 : ℤ) * (10 : ℚ) ^ (maxExponent - 4) :=
          mul_le_mul_of_nonneg_left hexp (by positivity)
  have hxabs := hbound_xy x hx_hi hxe
  have hyabs := hbound_xy y hy_hi hye
  have hupper : |sum.toRat| < (10 : ℚ) ^ (maxExponent + 15) := by
    have htri : |sum.toRat| ≤ |x.toRat + y.toRat| + |x.toRat + y.toRat| * (6 / (2 ^ 63 - 3 : ℚ)) := by
      have h := abs_sub_abs_le_abs_sub sum.toRat (x.toRat + y.toRat)
      linarith [hadd_bound]
    have hxy : |x.toRat + y.toRat| ≤ |x.toRat| + |y.toRat| := abs_add_le _ _
    have hε : (6 / (2 ^ 63 - 3 : ℚ)) ≤ 1 := by norm_num
    have hε0 : (0 : ℚ) ≤ (6 / (2 ^ 63 - 3 : ℚ)) := by positivity
    have habs_nn : (0 : ℚ) ≤ |x.toRat + y.toRat| := abs_nonneg _
    -- |sum| ≤ |x+y|·(1+ε) ≤ (|x|+|y|)·2 < 4·10^(maxExponent+12) < 10^(maxExponent+15)
    have hstep1 : |sum.toRat| ≤ |x.toRat + y.toRat| * 2 := by nlinarith [htri, habs_nn]
    have hstep2 : |x.toRat + y.toRat| * 2 ≤ (|x.toRat| + |y.toRat|) * 2 := by linarith [hxy]
    have hstep3 : (|x.toRat| + |y.toRat|) * 2 < (10 : ℚ) ^ (maxExponent + 12) * 4 := by
      nlinarith [hxabs, hyabs]
    have hpow15 : (10 : ℚ) ^ (maxExponent + 15) = (10 : ℚ) ^ (maxExponent + 12) * (10 : ℚ) ^ (3 : ℤ) := by
      rw [← zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0)]; ring_nf
    have hcmp : (10 : ℚ) ^ (maxExponent + 12) * 4 < (10 : ℚ) ^ (maxExponent + 15) := by
      rw [hpow15]
      have h3 : (10 : ℚ) ^ (3 : ℤ) = 1000 := by norm_num
      rw [h3]
      have hpos : (0 : ℚ) < (10 : ℚ) ^ (maxExponent + 12) := zpow_pos (by norm_num) _
      nlinarith [hpos]
    linarith
  linarith

/-- **IOU addition double-rounding composition (`to_nearest`).** Given the two
intermediate 19-digit `Number`s (`xn`, `yn`) and their exact `Number` sum (`sum`),
the IOU result rounds within the composed relative error `ε₁ + ε₂ + ε₁·ε₂`, where
`ε₁ = 6/(2^63−3)` is the `Number`-addition bound and `ε₂ = 10^(−15)` is the 16-digit
re-rounding bound. Pure triangle-inequality composition over the two proven layers. -/
lemma IOUAmount.operator_add_compose_to_nearest
    (x y result : IOUAmount) (xn yn sum : Number)
    (hxn_val : xn.toRat = x.toRat) (hyn_val : yn.toRat = y.toRat)
    (hxn_norm : xn.isNormalized) (hyn_norm : yn.isNormalized)
    (hxn_ne : xn.mantissa_ ≠ 0) (hyn_ne : yn.mantissa_ ≠ 0)
    (h_no_cancel : ¬ xn.operator_eq yn.operator_neg)
    (hsum_ne : sum.mantissa_ ≠ 0)
    (hadd : Number.operator_add xn yn .to_nearest = .ok sum)
    (h_lo : 10 ^ 18 ≤ sum.mantissa_.toNat) (h_hi : sum.mantissa_.toNat < 10 ^ 19)
    (he_lo : minExponent ≤ sum.exponent_ + 3) (he_hi : sum.exponent_ + 4 ≤ maxExponent)
    (hofn : IOUAmount.ofNumber sum .to_nearest = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    |result.toRat - (x.toRat + y.toRat)|
      ≤ |x.toRat + y.toRat| *
        (6 / (2 ^ 63 - 3 : ℚ) + (1 / 2) * (10 : ℚ) ^ (-15 : ℤ)
          + 6 / (2 ^ 63 - 3 : ℚ) * ((1 / 2) * (10 : ℚ) ^ (-15 : ℤ))) := by
  -- Layer 1: the Number-addition relative-error bound, with truth = x.toRat + y.toRat.
  have hadd_bound := operator_add_rounds_to_nearest xn yn sum hxn_norm hyn_norm
    hxn_ne hyn_ne h_no_cancel hadd hsum_ne
  simp only [RoundsWithin] at hadd_bound
  rw [hxn_val, hyn_val] at hadd_bound
  have h1 : |sum.toRat - (x.toRat + y.toRat)|
      ≤ |x.toRat + y.toRat| * (6 / (2 ^ 63 - 3 : ℚ)) := hadd_bound
  -- Layer 2: the 16-digit re-rounding bound (tight, half-ULP), recast as a relative error.
  have hofn_bound := IOUAmount.ofNumber_within_half_ulp sum result
    h_lo h_hi he_lo he_hi hofn hresult
  have hsum_abs : |sum.toRat| = (sum.mantissa_.toNat : ℚ) * 10 ^ sum.exponent_ :=
    _root_.XRPL.Model.Protocol.abs_toRat_eq sum
  have hold : (10 : ℚ) ^ (sum.exponent_ + 3) ≤ |sum.toRat| * (10 : ℚ) ^ (-15 : ℤ) := by
    rw [hsum_abs]
    have hpow : (10 : ℚ) ^ (sum.exponent_ + 3)
        = (10 : ℚ) ^ (18 : ℤ) * (10 : ℚ) ^ (sum.exponent_ - 15) := by
      rw [← zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0)]; ring_nf
    have hmul : (sum.mantissa_.toNat : ℚ) * 10 ^ sum.exponent_ * (10 : ℚ) ^ (-15 : ℤ)
        = (sum.mantissa_.toNat : ℚ) * (10 : ℚ) ^ (sum.exponent_ - 15) := by
      rw [mul_assoc, ← zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0)]; ring_nf
    rw [hpow, hmul]
    have hlo_q : (10 : ℚ) ^ (18 : ℤ) ≤ (sum.mantissa_.toNat : ℚ) := by
      have : ((10 ^ 18 : ℕ) : ℚ) ≤ (sum.mantissa_.toNat : ℚ) := by exact_mod_cast h_lo
      rwa [show ((10 ^ 18 : ℕ) : ℚ) = (10 : ℚ) ^ (18 : ℤ) by push_cast; norm_num] at this
    gcongr
  have h2 : |result.toRat - sum.toRat| ≤ |sum.toRat| * ((1 / 2) * (10 : ℚ) ^ (-15 : ℤ)) := by
    calc |result.toRat - sum.toRat| ≤ (1 / 2) * (10 : ℚ) ^ (sum.exponent_ + 3) := hofn_bound
      _ ≤ (1 / 2) * (|sum.toRat| * (10 : ℚ) ^ (-15 : ℤ)) :=
          mul_le_mul_of_nonneg_left hold (by norm_num)
      _ = |sum.toRat| * ((1 / 2) * (10 : ℚ) ^ (-15 : ℤ)) := by ring
  -- Compose by the relative-error triangle inequality.
  exact rel_error_trans h1 h2 (by positivity)

/-- **IOU addition decomposition.** Reduces `IOUAmount.operator_add x y` to its
exact 19-digit lifts `xn`, `yn`, their `Number` sum `sum`, and the closing
`ofNumber sum = result` step, bundling every fact the two downstream consumers (the
relative-error bound and the `InRange16` range bound) need. -/
lemma IOUAmount.operator_add_to_nearest_decompose (x y result : IOUAmount)
    (hx_lo : 10 ^ 15 ≤ x.mantissa_.toInt.natAbs) (hx_hi : x.mantissa_.toInt.natAbs < 10 ^ 16)
    (hy_lo : 10 ^ 15 ≤ y.mantissa_.toInt.natAbs) (hy_hi : y.mantissa_.toInt.natAbs < 10 ^ 16)
    (hxe_lo : minExponent + 3 ≤ x.exponent_) (hxe_hi : x.exponent_ ≤ maxExponent - 4)
    (hye_lo : minExponent + 3 ≤ y.exponent_) (hye_hi : y.exponent_ ≤ maxExponent - 4)
    (h_truth_ne : x.toRat + y.toRat ≠ 0)
    (hok : IOUAmount.operator_add x y .to_nearest = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    ∃ (xn yn sum : Number),
      xn.toRat = x.toRat ∧ yn.toRat = y.toRat ∧
      xn.isNormalized ∧ yn.isNormalized ∧ xn.mantissa_ ≠ 0 ∧ yn.mantissa_ ≠ 0 ∧
      ¬ xn.operator_eq yn.operator_neg ∧ sum.mantissa_ ≠ 0 ∧
      Number.operator_add xn yn .to_nearest = .ok sum ∧
      10 ^ 18 ≤ sum.mantissa_.toNat ∧ sum.mantissa_.toNat < 10 ^ 19 ∧
      minExponent ≤ sum.exponent_ + 3 ∧ sum.exponent_ + 4 ≤ maxExponent ∧
      IOUAmount.ofNumber sum .to_nearest = .ok result := by
  -- Operands are nonzero (mantissa magnitude ≥ 10^15 > 0).
  have hxm : x.mantissa_ ≠ 0 := by
    intro h; rw [h] at hx_lo; simp at hx_lo
  have hym : y.mantissa_ ≠ 0 := by
    intro h; rw [h] at hy_lo; simp at hy_lo
  -- The two 19-digit canonical lifts, exact and normalized.
  obtain ⟨xn, hxn_eq, hxn_val, hxn_norm, hxn_ne⟩ :=
    IOUAmount.toNumber_canonical x .to_nearest hx_lo hx_hi hxe_lo (by omega)
  obtain ⟨yn, hyn_eq, hyn_val, hyn_norm, hyn_ne⟩ :=
    IOUAmount.toNumber_canonical y .to_nearest hy_lo hy_hi hye_lo (by omega)
  -- Reduce `operator_add` to `Number.operator_add` + `ofNumber` on `xn`, `yn`.
  unfold IOUAmount.operator_add at hok
  rw [if_neg (by simpa using hym), if_neg (by simpa using hxm), hxn_eq, hyn_eq] at hok
  simp only at hok
  cases hadd : Number.operator_add xn yn .to_nearest with
  | error e => rw [hadd] at hok; simp at hok
  | ok sum =>
    rw [hadd] at hok
    simp only at hok
    have hofn : IOUAmount.ofNumber sum .to_nearest = .ok result := hok
    -- No cancellation: `x.toRat + y.toRat ≠ 0` ⟹ `xn ≠ -yn`.
    have h_no_cancel : ¬ xn.operator_eq yn.operator_neg := by
      intro hc
      have hyn_neg_norm : yn.operator_neg.isNormalized :=
        Number.operator_neg_isNormalized yn hyn_norm
      have heq := (operator_eq_iff xn yn.operator_neg hxn_norm hyn_neg_norm).mp hc
      rw [Number.toRat_neg, hxn_val, hyn_val] at heq
      exact h_truth_ne (by linarith)
    -- `sum` is nonzero, normalized, in 19-digit range; its exponent stays below the ceiling.
    have hsum_ne : sum.mantissa_ ≠ 0 :=
      IOUAmount.ofNumber_mantissa_ne_zero sum .to_nearest result hofn hresult
    have hsum_norm := operator_add_result_isNormalized_anyMode xn yn sum .to_nearest
      hxn_norm hyn_norm hxn_ne hyn_ne h_no_cancel hadd hsum_ne
    have hsum_mant := hsum_norm.mantissaBounds_nat hsum_ne
    have hsum_exp : minExponent ≤ sum.exponent_ ∧ sum.exponent_ ≤ maxExponent := by
      rcases hsum_norm with hz | ⟨_, _, _, hlo, hhi⟩
      · exact absurd (show sum.mantissa_ = 0 by rw [hz]; rfl) hsum_ne
      · exact ⟨hlo, hhi⟩
    have he_hi : sum.exponent_ + 4 ≤ maxExponent :=
      IOUAmount.add_sum_exponent_hi x y xn yn sum hxn_val hyn_val hxn_norm hyn_norm
        hxn_ne hyn_ne h_no_cancel hsum_ne hx_hi hy_hi hxe_hi hye_hi hadd
    exact ⟨xn, yn, sum, hxn_val, hyn_val, hxn_norm, hyn_norm, hxn_ne, hyn_ne, h_no_cancel,
      hsum_ne, hadd, hsum_mant.1, hsum_mant.2, by omega, he_hi, hofn⟩

/-- **IOU addition rounds within relative error `≈10⁻¹⁵` (`to_nearest`).** The
headline bound for `IOUAmount.operator_add`: two in-range 16-digit IOU amounts add
(via the 19-digit `Number` engine, then a 16-digit re-round) within the composed
relative error `ε₁ + ε₂ + ε₁·ε₂`, `ε₁ = 6/(2⁶³−3)`, `ε₂ = 10⁻¹⁵`. -/
theorem IOUAmount.operator_add_rounds_to_nearest (x y result : IOUAmount)
    (hx_lo : 10 ^ 15 ≤ x.mantissa_.toInt.natAbs) (hx_hi : x.mantissa_.toInt.natAbs < 10 ^ 16)
    (hy_lo : 10 ^ 15 ≤ y.mantissa_.toInt.natAbs) (hy_hi : y.mantissa_.toInt.natAbs < 10 ^ 16)
    (hxe_lo : minExponent + 3 ≤ x.exponent_) (hxe_hi : x.exponent_ ≤ maxExponent - 4)
    (hye_lo : minExponent + 3 ≤ y.exponent_) (hye_hi : y.exponent_ ≤ maxExponent - 4)
    (h_truth_ne : x.toRat + y.toRat ≠ 0)
    (hok : IOUAmount.operator_add x y .to_nearest = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    |result.toRat - (x.toRat + y.toRat)|
      ≤ |x.toRat + y.toRat| *
        (6 / (2 ^ 63 - 3 : ℚ) + (1 / 2) * (10 : ℚ) ^ (-15 : ℤ)
          + 6 / (2 ^ 63 - 3 : ℚ) * ((1 / 2) * (10 : ℚ) ^ (-15 : ℤ))) := by
  obtain ⟨xn, yn, sum, hxn_val, hyn_val, hxn_norm, hyn_norm, hxn_ne, hyn_ne, h_no_cancel,
      hsum_ne, hadd, h_lo, h_hi, he_lo, he_hi, hofn⟩ :=
    IOUAmount.operator_add_to_nearest_decompose x y result hx_lo hx_hi hy_lo hy_hi
      hxe_lo hxe_hi hye_lo hye_hi h_truth_ne hok hresult
  exact IOUAmount.operator_add_compose_to_nearest x y result xn yn sum
    hxn_val hyn_val hxn_norm hyn_norm hxn_ne hyn_ne h_no_cancel hsum_ne hadd
    h_lo h_hi he_lo he_hi hofn hresult

/-- The `IOUAmount.operator_add` result is a canonical 16-digit IOU amount
(`InRange16`): mantissa in `[10^15, 10^16)`, exponent in `[-96, 80]`. -/
theorem IOUAmount.operator_add_InRange16 (x y result : IOUAmount)
    (hx_lo : 10 ^ 15 ≤ x.mantissa_.toInt.natAbs) (hx_hi : x.mantissa_.toInt.natAbs < 10 ^ 16)
    (hy_lo : 10 ^ 15 ≤ y.mantissa_.toInt.natAbs) (hy_hi : y.mantissa_.toInt.natAbs < 10 ^ 16)
    (hxe_lo : minExponent + 3 ≤ x.exponent_) (hxe_hi : x.exponent_ ≤ maxExponent - 4)
    (hye_lo : minExponent + 3 ≤ y.exponent_) (hye_hi : y.exponent_ ≤ maxExponent - 4)
    (h_truth_ne : x.toRat + y.toRat ≠ 0)
    (hok : IOUAmount.operator_add x y .to_nearest = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    result.InRange16 := by
  obtain ⟨_, _, sum, _, _, _, _, _, _, _, _, _, h_lo, h_hi, he_lo, he_hi, hofn⟩ :=
    IOUAmount.operator_add_to_nearest_decompose x y result hx_lo hx_hi hy_lo hy_hi
      hxe_lo hxe_hi hye_lo hye_hi h_truth_ne hok hresult
  exact IOUAmount.ofNumber_InRange16 sum .to_nearest result h_lo h_hi he_lo he_hi hofn hresult

/-! ## STAmount ↔ IOUAmount exact conversions (16-digit canonical) -/

/-- The `STAmount → IOUAmount` conversion is value-exact on canonical inputs. -/
lemma STAmount.iou_canonical_toRat (s : STAmount) (hc : s.IOUCanonical) :
    (⟨s.signedDrops.toInt64, s.mOffset⟩ : IOUAmount).toRat = s.toRat := by
  rw [IOUAmount.toRat_eq]
  show (s.signedDrops.toInt64.toInt : ℚ) * 10 ^ s.mOffset = s.toRat
  rw [STAmount.signedDrops_toInt64_toInt s hc.mant_hi, STAmount.toRat_signed s]
  unfold STAmount.signedDrops
  rcases h : s.mIsNegative with _ | _ <;> simp

/-- A nonzero `ofIOUAmount` result forces a nonzero source mantissa: a zero-mantissa
`IOUAmount` canonicalizes (via the IOU branch) to `IOUAmount.zero`, hence a zero-value
STAmount. -/
lemma STAmount.ofIOUAmount_mantissa_ne_zero (a : IOUAmount) (iss : Issue) (mode : rounding_mode)
    (h_not_xrp : (Asset.issue iss).isNative = false) (res : STAmount)
    (hpack : STAmount.ofIOUAmount a iss mode = .ok res) (hres : res.mValue ≠ 0) :
    a.mantissa_ ≠ 0 := by
  intro hm
  apply hres
  have hsig : ¬ (IOUAmount.signum a < 0) := by unfold IOUAmount.signum; rw [hm]; decide
  have hint : (Asset.issue iss).integral = false := h_not_xrp
  have hti : Int.toInt64 0 = 0 := by decide
  have htu : Int64.toUInt64 0 = 0 := by decide
  have hcompute : STAmount.ofIOUAmount a iss mode = .ok ⟨.issue iss, 0, -100, false⟩ := by
    unfold STAmount.ofIOUAmount STAmount.canonicalize STAmount.iou
    simp [hm, STAmount.unchecked, STAmount.integral, STAmount.signedDrops,
      IOUAmount.ofMantissaExp, IOUAmount.normalize, hint, hti, htu,
      IOUAmount.signum, IOUAmount.zero]
  rw [hpack] at hcompute
  rw [Except.ok.inj hcompute]

/-- The `IOUAmount → STAmount` conversion (`ofIOUAmount`) is value-exact on in-range
16-digit inputs. -/
lemma STAmount.ofIOUAmount_canonical_toRat (a : IOUAmount) (iss : Issue) (mode : rounding_mode)
    (h_not_xrp : (Asset.issue iss).isNative = false) (hr : a.InRange16) (res : STAmount)
    (hpack : STAmount.ofIOUAmount a iss mode = .ok res) :
    res.toRat = a.toRat := by
  rw [STAmount.ofIOUAmount_canonical a iss mode h_not_xrp hr] at hpack
  have hres : res = ⟨.issue iss, a.mantissa_.toInt.natAbs.toUInt64, a.exponent_,
      decide (a.mantissa_ < 0)⟩ := (Except.ok.inj hpack).symm
  have h_fit : a.mantissa_.toInt.natAbs < 2 ^ 64 := by have := hr.mant_hi; omega
  have h_toNat : (a.mantissa_.toInt.natAbs.toUInt64).toNat = a.mantissa_.toInt.natAbs :=
    UInt64.toNat_ofNat_of_lt' (by rw [uint64_size_val]; omega)
  rw [hres, IOUAmount.toRat_eq a, STAmount.toRat_signed]
  show (if decide (a.mantissa_ < 0) then (-1 : ℚ) else 1)
      * ((a.mantissa_.toInt.natAbs.toUInt64).toNat : ℚ) * 10 ^ a.exponent_
      = (a.mantissa_.toInt : ℚ) * 10 ^ a.exponent_
  rw [h_toNat]
  have hsign : (if decide (a.mantissa_ < 0) then (-1 : ℚ) else 1)
      * (a.mantissa_.toInt.natAbs : ℚ) = (a.mantissa_.toInt : ℚ) := by
    by_cases hlt : a.mantissa_ < 0
    · rw [if_pos (by simpa using hlt)]
      have hlt' : a.mantissa_.toInt < 0 := by
        have h := Int64.lt_iff_toInt_lt.mp hlt
        rwa [show (0 : Int64).toInt = 0 from by decide] at h
      have : (a.mantissa_.toInt.natAbs : ℚ) = -(a.mantissa_.toInt : ℚ) := by
        have hc : (a.mantissa_.toInt.natAbs : ℚ) = |(a.mantissa_.toInt : ℚ)| := by
          rw [Nat.cast_natAbs]; push_cast; rfl
        rw [hc, abs_of_neg (by exact_mod_cast hlt')]
      rw [this]; ring
    · rw [if_neg (by simpa using hlt)]
      have hge : 0 ≤ a.mantissa_.toInt := by
        rcases lt_or_ge a.mantissa_.toInt 0 with hh | hh
        · exact absurd (Int64.lt_iff_toInt_lt.mpr
            (by rw [show (0 : Int64).toInt = 0 from by decide]; exact hh)) hlt
        · exact hh
      have : (a.mantissa_.toInt.natAbs : ℚ) = (a.mantissa_.toInt : ℚ) := by
        have hc : (a.mantissa_.toInt.natAbs : ℚ) = |(a.mantissa_.toInt : ℚ)| := by
          rw [Nat.cast_natAbs]; push_cast; rfl
        rw [hc, abs_of_nonneg (by exact_mod_cast hge)]
      rw [this]; ring
  linear_combination (10 : ℚ) ^ a.exponent_ * hsign

/-- The IOU lift's mantissa magnitude equals the source value. -/
lemma STAmount.iou_canonical_mantissa_natAbs (s : STAmount) (hc : s.IOUCanonical) :
    (⟨s.signedDrops.toInt64, s.mOffset⟩ : IOUAmount).mantissa_.toInt.natAbs = s.mValue.toNat := by
  show (s.signedDrops.toInt64).toInt.natAbs = s.mValue.toNat
  rw [STAmount.signedDrops_toInt64_toInt s hc.mant_hi]
  unfold STAmount.signedDrops
  rcases s.mIsNegative <;> simp

/-- **STAmount IOU addition rel-error engine (`to_nearest`).** Both operands are
canonical 16-digit IOU amounts of the same issue; the result rounds within the
composed double-rounding relative error. Converts exactly on both ends
(`iou_canonical_id`/`ofIOUAmount_canonical`) around the `IOUAmount`-level bound. -/
theorem STAmount.operator_add_iou_rel_error (v1 v2 result : STAmount) (iss : Issue)
    (hv1 : v1.mAsset = .issue iss) (h_xrp : iss.isXRP = false)
    (hc1 : v1.IOUCanonical) (hc2 : v2.IOUCanonical)
    (h_truth_ne : v1.toRat + v2.toRat ≠ 0)
    (hok : STAmount.operator_add v1 v2 .to_nearest = .ok result)
    (hresult : result.mValue ≠ 0) :
    |result.toRat - (v1.toRat + v2.toRat)|
      ≤ |v1.toRat + v2.toRat| *
        (6 / (2 ^ 63 - 3 : ℚ) + (1 / 2) * (10 : ℚ) ^ (-15 : ℤ)
          + 6 / (2 ^ 63 - 3 : ℚ) * ((1 / 2) * (10 : ℚ) ^ (-15 : ℤ))) := by
  have h_not_xrp : (Asset.issue iss).isNative = false := h_xrp
  -- comparability is forced by `hok` succeeding
  have hcmp : STAmount.areComparable v1 v2 = true := by
    rcases hb : STAmount.areComparable v1 v2 with _ | _
    · rw [STAmount.operator_add, hb] at hok; simp at hok
    · rfl
  rw [STAmount.operator_add, if_neg (by rw [hcmp]; decide)] at hok
  -- both operands nonzero (`mant_lo : 10^15 ≤ mValue.toNat`)
  have hv2ne : v2.mValue ≠ 0 := by
    intro h; have := hc2.mant_lo; rw [h] at this; simp at this
  have hv1ne : v1.mValue ≠ 0 := by
    intro h; have := hc1.mant_lo; rw [h] at this; simp at this
  rw [if_neg (by simpa using hv2ne), if_neg (by simpa using hv1ne), hv1] at hok
  simp only at hok
  rw [if_neg (show ¬ (iss.isXRP = true) from by rw [h_xrp]; decide)] at hok
  rw [STAmount.iou_canonical_id v1 .to_nearest hc1,
      STAmount.iou_canonical_id v2 .to_nearest hc2] at hok
  simp only at hok
  set i1 : IOUAmount := ⟨v1.signedDrops.toInt64, v1.mOffset⟩ with hi1
  set i2 : IOUAmount := ⟨v2.signedDrops.toInt64, v2.mOffset⟩ with hi2
  cases hadd : IOUAmount.operator_add i1 i2 .to_nearest with
  | error e => rw [hadd] at hok; simp at hok
  | ok sumI =>
    rw [hadd] at hok
    simp only at hok
    -- exact value transport on both ends
    have hi1v : i1.toRat = v1.toRat := STAmount.iou_canonical_toRat v1 hc1
    have hi2v : i2.toRat = v2.toRat := STAmount.iou_canonical_toRat v2 hc2
    have hi1n : i1.mantissa_.toInt.natAbs = v1.mValue.toNat :=
      STAmount.iou_canonical_mantissa_natAbs v1 hc1
    have hi2n : i2.mantissa_.toInt.natAbs = v2.mValue.toNat :=
      STAmount.iou_canonical_mantissa_natAbs v2 hc2
    have hi1e : i1.exponent_ = v1.mOffset := rfl
    have hi2e : i2.exponent_ = v2.mOffset := rfl
    -- `sumI` is nonzero (else `ofIOUAmount` ⟹ zero STAmount, contradicting `hresult`)
    have hsumI_ne : sumI.mantissa_ ≠ 0 :=
      STAmount.ofIOUAmount_mantissa_ne_zero sumI iss .to_nearest h_not_xrp result hok hresult
    -- `sumI` is in 16-digit range, so `ofIOUAmount` is value-exact
    have hsumI_range : sumI.InRange16 :=
      IOUAmount.operator_add_InRange16 i1 i2 sumI
        (by rw [hi1n]; exact hc1.mant_lo) (by rw [hi1n]; exact hc1.mant_hi)
        (by rw [hi2n]; exact hc2.mant_lo) (by rw [hi2n]; exact hc2.mant_hi)
        (by rw [hi1e]; have := hc1.exp_lo; unfold minExponent; omega)
        (by rw [hi1e]; have := hc1.exp_hi; unfold maxExponent; omega)
        (by rw [hi2e]; have := hc2.exp_lo; unfold minExponent; omega)
        (by rw [hi2e]; have := hc2.exp_hi; unfold maxExponent; omega)
        (by rw [hi1v, hi2v]; exact h_truth_ne) hadd hsumI_ne
    have hrv : result.toRat = sumI.toRat :=
      STAmount.ofIOUAmount_canonical_toRat sumI iss .to_nearest h_not_xrp hsumI_range result hok
    -- the `IOUAmount`-level rel-error bound, transported to `v1`, `v2`
    have hbound := IOUAmount.operator_add_rounds_to_nearest i1 i2 sumI
      (by rw [hi1n]; exact hc1.mant_lo) (by rw [hi1n]; exact hc1.mant_hi)
      (by rw [hi2n]; exact hc2.mant_lo) (by rw [hi2n]; exact hc2.mant_hi)
      (by rw [hi1e]; have := hc1.exp_lo; unfold minExponent; omega)
      (by rw [hi1e]; have := hc1.exp_hi; unfold maxExponent; omega)
      (by rw [hi2e]; have := hc2.exp_lo; unfold minExponent; omega)
      (by rw [hi2e]; have := hc2.exp_hi; unfold maxExponent; omega)
      (by rw [hi1v, hi2v]; exact h_truth_ne) hadd hsumI_ne
    rw [hi1v, hi2v] at hbound
    rw [hrv]; exact hbound

end XRPL.Model.Protocol
