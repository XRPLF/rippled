import XRPL.Properties.Protocol.STAmount.Mul.Common.IOU
import XRPL.Properties.Protocol.Number.Common.Rounding.BitVec
import XRPL.Properties.Protocol.STAmount.Add.Common.DirectedSupport
import XRPL.Properties.Protocol.STAmount.Add.Common.Native
import XRPL.Properties.Protocol.Number.Constructors.Constructors
import XRPL.Properties.Protocol.Number.Normalize.Common.ResultFacts

/-! # IOU division rounding (`STAmount.divide`)

`STAmount.divide` computes `q = ⌊numVal·10¹⁷ / denVal⌋` (`muldiv`), adds the `+5`
rounding bias, then re-rounds via `checked`/`canonicalize`. For IOU this re-round
routes `iou → from_rep` (exact 19-digit scale-up) `→ fromNumber = normalizeToRange`,
so it reuses the 16-digit re-round keystone. -/

namespace XRPL.Model.Protocol

set_option maxRecDepth 10000

/-- `muldiv a b c` floors `(a·b)/c` when the product fits in 128 bits and the quotient
in 64 bits. -/
lemma STAmount.muldiv_eq (a b c : UInt64)
    (hprod : a.toNat * b.toNat < 2 ^ 128) (hq : (a.toNat * b.toNat) / c.toNat < 2 ^ 64) :
    ∃ q : UInt64, STAmount.muldiv a b c = .ok q ∧ q.toNat = (a.toNat * b.toNat) / c.toNat := by
  have hprodN : (toUInt128 a * toUInt128 b).toNat = a.toNat * b.toNat :=
    uint128_of_uint64_mul_toNat a b hprod
  have hqN : ((toUInt128 a * toUInt128 b) / toUInt128 c).toNat = (a.toNat * b.toNat) / c.toNat := by
    rw [BitVec.toNat_udiv, hprodN, toNat_toUInt128]
  unfold STAmount.muldiv
  simp only []
  set q128 : UInt128 := (toUInt128 a * toUInt128 b) / toUInt128 c with hq128def
  have hq128_lt : q128.toNat < 2 ^ 64 := by rw [hq128def, hqN]; exact hq
  have hmax : ((2 ^ 64 - 1 : UInt128)).toNat = 18446744073709551615 := by decide
  rw [if_neg (show ¬ (q128 > (2 ^ 64 - 1 : UInt128)) from by
    rw [gt_iff_lt, BitVec.lt_def, hmax]; omega)]
  exact ⟨toUInt64 q128, rfl, by rw [toNat_toUInt64 hq128_lt, hq128def, hqN]⟩

lemma from_rep_RoundsWithin_anyMode (m : Int64) (e : Int) (mode : rounding_mode) (v : Number)
    (hok : Number.from_rep m e largeRange.min largeRange.max mode = .ok v) (hv_ne : v.mantissa_ ≠ 0) :
    RoundsWithin v ((m.toInt : ℚ) * 10 ^ e) mode (10 / (2 ^ 63 + 2 : ℚ)) := by
  cases mode with
  | to_nearest =>
    exact RoundsWithin_mono v _ (5 / (2 ^ 63 + 7 : ℚ)) (10 / (2 ^ 63 + 2 : ℚ)) .to_nearest
      (from_rep_rounds_to_nearest m e v hok hv_ne) (by norm_num)
  | downward => exact from_rep_rounds_downward m e v hok hv_ne
  | upward => exact from_rep_rounds_upward m e v hok hv_ne
  | towards_zero => exact from_rep_rounds_towards_zero m e v hok hv_ne

/-- **Shared `IOUAmount.normalize` decomposition facts** for a 17-to-18-digit signed
mantissa. The normalize routes through the exact 19-digit re-lift (`from_rep`) to a
`Number` `v` then the 16-digit re-round (`ofNumber`). This bundles the `v` facts both
the rel-error bound and the `InRange16` range bound downstream need. -/
lemma IOUAmount.normalize_vfacts (m : Int64) (e : Int) (mode : rounding_mode) (result : IOUAmount)
    (hm_lo : 10 ^ 16 ≤ m.toInt.natAbs) (hm_hi : m.toInt.natAbs < 10 ^ 18)
    (hok : IOUAmount.normalize ⟨m, e⟩ mode = .ok result) (hne : result.mantissa_ ≠ 0) :
    ∃ v : Number, Number.from_rep m e largeRange.min largeRange.max mode = .ok v ∧
      IOUAmount.ofNumber v mode = .ok result ∧ v.mantissa_ ≠ 0 ∧
      10 ^ 18 ≤ v.mantissa_.toNat ∧ v.mantissa_.toNat < 10 ^ 19 ∧
      minExponent ≤ v.exponent_ ∧ v.exponent_ ≤ e := by
  have hm_ne : (⟨m, e⟩ : IOUAmount).mantissa_ ≠ 0 := by
    show m ≠ 0; intro h; rw [h] at hm_lo; simp at hm_lo
  unfold IOUAmount.normalize at hok
  rw [if_neg (by simpa using hm_ne)] at hok
  cases hfr : Number.from_rep m e largeRange.min largeRange.max mode with
  | error err => rw [hfr] at hok; simp at hok
  | ok v =>
    rw [hfr] at hok; simp only at hok
    -- hok : IOUAmount.ofNumber v mode = .ok result
    have hv_ne : v.mantissa_ ≠ 0 := IOUAmount.ofNumber_mantissa_ne_zero v mode result hok hne
    have hfr' : (Number.unchecked (m < 0) m.toInt.natAbs.toUInt64 e).normalize
        largeRange.min largeRange.max mode = .ok v := hfr
    have hun_ne : (Number.unchecked (m < 0) m.toInt.natAbs.toUInt64 e).mantissa_ ≠ 0 := by
      show m.toInt.natAbs.toUInt64 ≠ 0
      have hb : m.toInt.natAbs < 2 ^ 64 := by omega
      have heq : m.toInt.natAbs.toUInt64.toNat = m.toInt.natAbs := UInt64.toNat_ofNat_of_lt hb
      intro h; rw [h] at heq; simp at heq; omega
    have hv_norm : v.isNormalized :=
      normalize_result_isNormalized _ v mode hun_ne hfr' hv_ne
    have hv_mant := hv_norm.mantissaBounds_nat hv_ne
    have hv_exp_lo : minExponent ≤ v.exponent_ := by
      rcases hv_norm with hz | ⟨_, _, _, hlo, _⟩
      · exact absurd (show v.mantissa_ = 0 by rw [hz]; rfl) hv_ne
      · exact hlo
    have hfr_bd := from_rep_RoundsWithin_anyMode m e mode v hfr hv_ne
    -- v.exponent_ ≤ e via the value bound
    have habs := RoundsWithin_abs_le_two v ((m.toInt : ℚ) * 10 ^ e) (10 / (2 ^ 63 + 2 : ℚ)) mode
      hfr_bd (by norm_num)
    rw [show RatValued.toRat v = v.toRat from rfl] at habs
    have hmabs : |(m.toInt : ℚ) * 10 ^ e| = (m.toInt.natAbs : ℚ) * 10 ^ e := by
      rw [abs_mul, abs_of_pos (zpow_pos (by norm_num : (0:ℚ) < 10) _), Nat.cast_natAbs]; push_cast; ring
    have hvabs : |v.toRat| = (v.mantissa_.toNat : ℚ) * 10 ^ v.exponent_ := _root_.XRPL.Model.Protocol.abs_toRat_eq v
    have hvexp : v.exponent_ ≤ e := by
      by_contra hc; push_neg at hc
      have h1 : (10 : ℚ) ^ (e + 1) ≤ 10 ^ v.exponent_ := zpow_le_zpow_right₀ (by norm_num) (by omega)
      have hvm18 : (10 : ℚ) ^ (18 : ℕ) ≤ (v.mantissa_.toNat : ℚ) := by exact_mod_cast hv_mant.1
      have hmlt : (m.toInt.natAbs : ℚ) < (10 : ℚ) ^ (18 : ℕ) := by exact_mod_cast hm_hi
      have hpe : (0 : ℚ) < (10 : ℚ) ^ e := zpow_pos (by norm_num) _
      rw [hvabs, hmabs] at habs
      have hpow1 : (10 : ℚ) ^ (e + 1) = 10 ^ e * 10 := by
        rw [zpow_add₀ (by norm_num : (10:ℚ) ≠ 0)]; norm_num
      nlinarith [habs, h1, hvm18, hmlt, hpe, hpow1]
    exact ⟨v, rfl, hok, hv_ne, hv_mant.1, hv_mant.2, hv_exp_lo, hvexp⟩

/-- **`IOUAmount.normalize` rel-error on a 17-to-18-digit signed mantissa** (the divide
keystone). `m·10^e` with `10^16 ≤ |m| < 10^18` re-rounds via the exact 19-digit lift
then the 16-digit snap, within the composed relative error `ε₁ + ε₂ + ε₁·ε₂`
(`ε₁ = 10/(2⁶³+2)`, `ε₂ = 10⁻¹⁵`). -/
lemma IOUAmount.normalize_rounds_within (m : Int64) (e : Int) (mode : rounding_mode) (result : IOUAmount)
    (hm_lo : 10 ^ 16 ≤ m.toInt.natAbs) (hm_hi : m.toInt.natAbs < 10 ^ 18)
    (he_hi : e + 4 ≤ maxExponent)
    (hok : IOUAmount.normalize ⟨m, e⟩ mode = .ok result) (hne : result.mantissa_ ≠ 0) :
    RoundsWithin result ((m.toInt : ℚ) * 10 ^ e) mode
      (10 / (2 ^ 63 + 2 : ℚ) + (10 : ℚ) ^ (-15 : ℤ) + 10 / (2 ^ 63 + 2 : ℚ) * (10 : ℚ) ^ (-15 : ℤ)) := by
  obtain ⟨v, hfr, hok', hv_ne, hvm_lo, hvm_hi, hv_exp_lo, hvexp⟩ :=
    IOUAmount.normalize_vfacts m e mode result hm_lo hm_hi hok hne
  have hfr_bd := from_rep_RoundsWithin_anyMode m e mode v hfr hv_ne
  have hofn := IOUAmount.ofNumber_rounds_within v mode result hvm_lo hvm_hi
    (by omega) (by omega) hok' hne
  exact RoundsWithin_trans result v ((m.toInt : ℚ) * 10 ^ e) (10 / (2 ^ 63 + 2 : ℚ))
    ((10 : ℚ) ^ (-15 : ℤ)) mode hfr_bd
    (by rw [show RatValued.toRat v = v.toRat from rfl]; exact hofn) (by positivity) (by positivity)

/-- The keystone's normalize output is a canonical 16-digit IOU amount (`InRange16`). -/
lemma IOUAmount.normalize_InRange16 (m : Int64) (e : Int) (mode : rounding_mode) (result : IOUAmount)
    (hm_lo : 10 ^ 16 ≤ m.toInt.natAbs) (hm_hi : m.toInt.natAbs < 10 ^ 18)
    (he_hi : e + 4 ≤ maxExponent)
    (hok : IOUAmount.normalize ⟨m, e⟩ mode = .ok result) (hne : result.mantissa_ ≠ 0) :
    result.InRange16 := by
  obtain ⟨v, hfr, hok', hv_ne, hvm_lo, hvm_hi, hv_exp_lo, hvexp⟩ :=
    IOUAmount.normalize_vfacts m e mode result hm_lo hm_hi hok hne
  exact IOUAmount.ofNumber_InRange16 v mode result hvm_lo hvm_hi (by omega) (by omega) hok' hne

/-- An IOU amount's `signum` is negative exactly when its mantissa is. -/
lemma IOUAmount.signum_neg_iff (i : IOUAmount) : i.signum < 0 ↔ i.mantissa_ < 0 := by
  unfold IOUAmount.signum
  by_cases h_lt : i.mantissa_ < 0
  · rw [if_pos h_lt]; simp [h_lt]
  · rw [if_neg h_lt]; split <;> simp_all

/-- The canonicalize-pack magnitude: `|i.mantissa_|` as a `UInt64` equals the natural
absolute value of the mantissa. -/
lemma IOUAmount.absMant_toNat (i : IOUAmount) (h_fit : i.mantissa_.toInt.natAbs < 2 ^ 63) :
    (if i.signum < 0 then -i.mantissa_ else i.mantissa_).toUInt64.toNat
      = i.mantissa_.toInt.natAbs := by
  have h_absToInt : (if i.signum < 0 then -i.mantissa_ else i.mantissa_).toInt
      = (i.mantissa_.toInt.natAbs : ℤ) := by
    by_cases h_lt : i.mantissa_ < 0
    · rw [if_pos ((IOUAmount.signum_neg_iff i).mpr h_lt)]
      have h_lt' : i.mantissa_.toInt < 0 := by simpa using Int64.lt_iff_toInt_lt.mp h_lt
      rw [Int64.toInt_neg, Int.bmod_eq_iff (by norm_num)]
      constructor <;> omega
    · rw [if_neg (fun h => h_lt ((IOUAmount.signum_neg_iff i).mp h))]
      have hge : 0 ≤ i.mantissa_.toInt := by
        rcases lt_or_ge i.mantissa_.toInt 0 with hh | hh
        · exact absurd (Int64.lt_iff_toInt_lt.mpr (by simpa using hh)) h_lt
        · exact hh
      omega
  have h1 := toUInt64_toNat_of_nonneg (if i.signum < 0 then -i.mantissa_ else i.mantissa_)
    (by rw [h_absToInt]; positivity)
  rw [h_absToInt] at h1
  omega

/-- **The canonicalize IOU pack preserves value.** The `STAmount` record built from a
canonical 16-digit `IOUAmount` `i` (magnitude `|i.mantissa_|`, exponent `i.exponent_`,
sign from `i.signum`) has rational value `i.toRat`. -/
lemma STAmount.iou_pack_toRat (iss : Issue) (i : IOUAmount) (hr : i.InRange16) :
    (⟨.issue iss, (if i.signum < 0 then -i.mantissa_ else i.mantissa_).toUInt64,
       i.exponent_, decide (i.signum < 0)⟩ : STAmount).toRat = i.toRat := by
  have h_fit : i.mantissa_.toInt.natAbs < 2 ^ 63 := by have := hr.mant_hi; omega
  have h_absToNat := IOUAmount.absMant_toNat i h_fit
  have h_decsign : decide (i.signum < 0) = decide (i.mantissa_ < 0) := by
    rw [decide_eq_decide]; exact IOUAmount.signum_neg_iff i
  rw [STAmount.toRat_signed]
  show (if decide (i.signum < 0) then (-1 : ℚ) else 1)
      * (((if i.signum < 0 then -i.mantissa_ else i.mantissa_).toUInt64).toNat : ℚ)
        * 10 ^ i.exponent_ = i.toRat
  rw [h_absToNat, h_decsign, IOUAmount.toRat_eq i]
  have hsign : (if decide (i.mantissa_ < 0) then (-1 : ℚ) else 1) * (i.mantissa_.toInt.natAbs : ℚ)
      = (i.mantissa_.toInt : ℚ) := by
    by_cases hlt : i.mantissa_ < 0
    · rw [if_pos (by simpa using hlt)]
      have hlt' : i.mantissa_.toInt < 0 := by simpa using Int64.lt_iff_toInt_lt.mp hlt
      have hc : (i.mantissa_.toInt.natAbs : ℚ) = -(i.mantissa_.toInt : ℚ) := by
        rw [Nat.cast_natAbs]; push_cast; rw [abs_of_neg (by exact_mod_cast hlt')]
      rw [hc]; ring
    · rw [if_neg (by simpa using hlt)]
      have hge : 0 ≤ i.mantissa_.toInt := by
        rcases lt_or_ge i.mantissa_.toInt 0 with hh | hh
        · exact absurd (Int64.lt_iff_toInt_lt.mpr (by simpa using hh)) hlt
        · exact hh
      have hc : (i.mantissa_.toInt.natAbs : ℚ) = (i.mantissa_.toInt : ℚ) := by
        rw [Nat.cast_natAbs]; push_cast; rw [abs_of_nonneg (by exact_mod_cast hge)]
      rw [hc]; ring
  linear_combination (10 : ℚ) ^ i.exponent_ * hsign

/-- **`STAmount.checked` rel-error on a 17-to-18-digit IOU mantissa** (divide step a).
The `checked`/`canonicalize` re-round of `±mant·10^exp` (an IOU asset) lands within the
keystone relative error of that value, where the result mantissa is nonzero. -/
lemma STAmount.checked_iou_rounds_within (iss : Issue) (mant : UInt64) (exp : Int) (neg : Bool)
    (mode : rounding_mode)
    (h_not_xrp : (Asset.issue iss).isNative = false)
    (h_lo : 10 ^ 16 ≤ mant.toNat) (h_hi : mant.toNat < 10 ^ 18)
    (he_hi : exp + 4 ≤ maxExponent)
    (result : STAmount)
    (hok : STAmount.checked (.issue iss) mant exp neg mode = .ok result) (hresult : result.mValue ≠ 0) :
    RoundsWithin result ((if neg then -(mant.toNat : ℚ) else (mant.toNat : ℚ)) * 10 ^ exp) mode
      (10 / (2 ^ 63 + 2 : ℚ) + (10 : ℚ) ^ (-15 : ℤ) + 10 / (2 ^ 63 + 2 : ℚ) * (10 : ℚ) ^ (-15 : ℤ)) := by
  have h_fit : mant.toNat < 2 ^ 63 := by omega
  set s : STAmount := STAmount.unchecked (.issue iss) mant exp neg with hs_def
  have h_int : ¬ s.integral = true := by
    intro h; exact Bool.noConfusion (h_not_xrp.symm.trans h)
  set sd : Int64 := s.signedDrops.toInt64 with hsd_def
  have hsd_int : sd.toInt = if neg then -(mant.toNat : ℤ) else (mant.toNat : ℤ) := by
    rw [hsd_def, STAmount.signedDrops_toInt64_toInt_of_lt s (show s.mValue.toNat < 2 ^ 63 from h_fit)]
    simp only [hs_def, STAmount.signedDrops, STAmount.unchecked]
  have hsd_natAbs : sd.toInt.natAbs = mant.toNat := by
    rw [hsd_int]; rcases neg <;> simp
  have hiou : s.iou mode = IOUAmount.normalize ⟨sd, exp⟩ mode := by
    unfold STAmount.iou; rw [if_neg h_int]; rfl
  rw [STAmount.checked] at hok
  unfold STAmount.canonicalize at hok
  rw [if_neg h_int, hiou] at hok
  cases h_norm : IOUAmount.normalize ⟨sd, exp⟩ mode with
  | error e => rw [h_norm] at hok; simp at hok
  | ok i =>
    rw [h_norm] at hok; simp only at hok
    have hres_eq : result = ⟨.issue iss,
        (if i.signum < 0 then -i.mantissa_ else i.mantissa_).toUInt64,
        i.exponent_, decide (i.signum < 0)⟩ := (Except.ok.inj hok).symm
    have hi_ne : i.mantissa_ ≠ 0 := by
      intro hm0; apply hresult
      rw [hres_eq]
      show (if i.signum < 0 then -i.mantissa_ else i.mantissa_).toUInt64 = 0
      rw [hm0]; split <;> decide
    have hi_range : i.InRange16 :=
      IOUAmount.normalize_InRange16 sd exp mode i (by rw [hsd_natAbs]; exact h_lo)
        (by rw [hsd_natAbs]; exact h_hi) he_hi h_norm hi_ne
    have hkey := IOUAmount.normalize_rounds_within sd exp mode i (by rw [hsd_natAbs]; exact h_lo)
      (by rw [hsd_natAbs]; exact h_hi) he_hi h_norm hi_ne
    have hval : result.toRat = i.toRat := by rw [hres_eq]; exact STAmount.iou_pack_toRat iss i hi_range
    have htruth : (sd.toInt : ℚ) * 10 ^ exp
        = (if neg then -(mant.toNat : ℚ) else (mant.toNat : ℚ)) * 10 ^ exp := by
      rw [hsd_int]; rcases neg <;> push_cast <;> ring
    rw [htruth] at hkey
    exact RoundsWithin_toRat_congr result i _ _ mode hval hkey

/-- **The divide `+5` rounding-bias bound** (step b). With `q = ⌊N·10¹⁷/D⌋` the
floored quotient, the biased mid-value `±(q+5)·10^(eN−eD−17)` is within relative
error `5/10¹⁶` of the true quotient `±(N/D)·10^(eN−eD)`. The bias is ≤ 5 ULP of the
17-digit quotient and the quotient magnitude `N·10¹⁷/D ≥ 10¹⁶`, giving the bound. -/
lemma STAmount.divide_iou_bias_bound (N D q : ℕ) (eN eD : ℤ) (neg : Bool)
    (hN_lo : 10 ^ 15 ≤ N) (hN_hi : N < 10 ^ 16)
    (hD_lo : 10 ^ 15 ≤ D) (hD_hi : D < 10 ^ 16)
    (hq : q = N * 10 ^ 17 / D) :
    |(if neg then -((q : ℚ) + 5) else ((q : ℚ) + 5)) * 10 ^ (eN - eD - 17)
       - (if neg then (-1 : ℚ) else 1) * ((N : ℚ) / D) * 10 ^ (eN - eD)|
      ≤ |(if neg then (-1 : ℚ) else 1) * ((N : ℚ) / D) * 10 ^ (eN - eD)| * (5 / 10 ^ 16) := by
  have hD0n : 0 < D := by omega
  have hN0n : 0 < N := by omega
  have hDQ : (0 : ℚ) < (D : ℚ) := by exact_mod_cast hD0n
  set P : ℚ := (N : ℚ) * 10 ^ 17 / D with hP
  have hP0 : 0 < P := by rw [hP]; positivity
  have hqD_le : (q : ℚ) * D ≤ (N : ℚ) * 10 ^ 17 := by
    have h1 : q * D ≤ N * 10 ^ 17 := hq ▸ Nat.div_mul_le_self _ _
    exact_mod_cast h1
  have hqD_lt : (N : ℚ) * 10 ^ 17 < ((q : ℚ) + 1) * D := by
    have h1 : N * 10 ^ 17 < (q + 1) * D := (Nat.div_lt_iff_lt_mul hD0n).mp (by rw [← hq]; omega)
    exact_mod_cast h1
  have hP_lo : (q : ℚ) ≤ P := by rw [hP, le_div_iff₀ hDQ]; exact hqD_le
  have hP_hi : P < (q : ℚ) + 1 := by rw [hP, div_lt_iff₀ hDQ]; exact hqD_lt
  have hND : (D : ℚ) ≤ 10 * N := by
    have h1 : D ≤ 10 * N := by omega
    exact_mod_cast h1
  have hP_big : (10 : ℚ) ^ 16 ≤ P := by
    rw [hP, le_div_iff₀ hDQ]
    nlinarith [mul_le_mul_of_nonneg_left hND (by norm_num : (0 : ℚ) ≤ 10 ^ 16)]
  have hcore : |((q : ℚ) + 5) - P| ≤ P * (5 / 10 ^ 16) := by
    rw [abs_of_nonneg (by linarith)]
    have hPbig : (5 : ℚ) ≤ P * (5 / 10 ^ 16) := by
      have := mul_le_mul_of_nonneg_right hP_big (by norm_num : (0 : ℚ) ≤ 5 / 10 ^ 16)
      nlinarith [this]
    linarith
  have hpow0 : (0 : ℚ) < (10 : ℚ) ^ (eN - eD - 17) := zpow_pos (by norm_num) _
  have hDne : (D : ℚ) ≠ 0 := ne_of_gt hDQ
  have hmid_s : (if neg then -((q : ℚ) + 5) else ((q : ℚ) + 5))
      = (if neg then (-1 : ℚ) else 1) * ((q : ℚ) + 5) := by rcases neg <;> simp
  rw [hmid_s]
  set s : ℚ := if neg then (-1 : ℚ) else 1 with hs_def
  have hs_abs : |s| = 1 := by rw [hs_def]; rcases neg <;> norm_num
  have hsa : |s * 10 ^ (eN - eD - 17)| = 10 ^ (eN - eD - 17) := by
    rw [abs_mul, hs_abs, one_mul, abs_of_pos hpow0]
  have h17 : (10 : ℚ) ^ (eN - eD) = (10 : ℚ) ^ (eN - eD - 17) * 10 ^ 17 := by
    rw [show eN - eD = (eN - eD - 17) + 17 from by ring, zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0)]
    norm_num
  have hmid_eq : s * ((q : ℚ) + 5) * 10 ^ (eN - eD - 17)
      - s * ((N : ℚ) / D) * 10 ^ (eN - eD)
      = s * 10 ^ (eN - eD - 17) * ((q : ℚ) + 5 - P) := by
    rw [h17, hP]; generalize (10 : ℚ) ^ (eN - eD - 17) = t; field_simp
  have hT_eq : s * ((N : ℚ) / D) * 10 ^ (eN - eD)
      = s * 10 ^ (eN - eD - 17) * P := by
    rw [h17, hP]; generalize (10 : ℚ) ^ (eN - eD - 17) = t; field_simp
  have key : |s * 10 ^ (eN - eD - 17) * ((q : ℚ) + 5 - P)|
      = 10 ^ (eN - eD - 17) * |(q : ℚ) + 5 - P| := by rw [abs_mul, hsa]
  have key2 : |s * 10 ^ (eN - eD - 17) * P|
      = 10 ^ (eN - eD - 17) * P := by rw [abs_mul, hsa, abs_of_pos hP0]
  rw [hmid_eq, hT_eq, key, key2]
  calc (10 : ℚ) ^ (eN - eD - 17) * |(q : ℚ) + 5 - P|
      ≤ (10 : ℚ) ^ (eN - eD - 17) * (P * (5 / 10 ^ 16)) :=
        mul_le_mul_of_nonneg_left hcore (le_of_lt hpow0)
    _ = (10 : ℚ) ^ (eN - eD - 17) * P * (5 / 10 ^ 16) := by ring

/-- The floored quotient `q = ⌊N·10¹⁷/D⌋` of two canonical 16-digit mantissas is a
17-to-18-digit value: `10¹⁶ ≤ q` and `q + 5 < 10¹⁸`. -/
lemma STAmount.divide_iou_q_bounds (N D q : ℕ)
    (hN_lo : 10 ^ 15 ≤ N) (hN_hi : N < 10 ^ 16) (hD_lo : 10 ^ 15 ≤ D) (hD_hi : D < 10 ^ 16)
    (hq : q = N * 10 ^ 17 / D) : 10 ^ 16 ≤ q ∧ q + 5 < 10 ^ 18 := by
  have hD0 : 0 < D := by omega
  refine ⟨?_, ?_⟩
  · rw [hq, Nat.le_div_iff_mul_le hD0]; nlinarith [hN_lo, hD_hi]
  · have hqD : q * D ≤ N * 10 ^ 17 := hq ▸ Nat.div_mul_le_self _ _
    have hq15 : q * 10 ^ 15 ≤ N * 100 * 10 ^ 15 := by
      have hstep : q * 10 ^ 15 ≤ q * D := by gcongr
      have h2 : N * 10 ^ 17 = N * 100 * 10 ^ 15 := by ring
      omega
    have hqle : q ≤ N * 100 := Nat.le_of_mul_le_mul_right hq15 (by norm_num)
    omega

/- **NOTE: `divide → checked` IOU reduction (the final plumbing) is deferred.**
All the divide *math* is proven above and below: `muldiv_eq` (floored quotient value),
`divide_iou_q_bounds` (17-to-18-digit range of `q+5`), `divide_iou_bias_bound` (the `+5`
bias is within `5/10¹⁶`), and `checked_iou_rounds_within` (the `checked` re-round bound).
The only missing step is the purely-definitional equation
`STAmount.divide num den (.issue iss) mode = STAmount.checked (.issue iss) (q+5) … mode`:
reducing `divide`'s final `match` to its `.ok` arm forces the kernel to `whnf` the
`checked`/`canonicalize` term, whose well-founded recursion (`doNormalize_scaleDown`)
overflows the kernel recursion limit (`(kernel) deep recursion detected`). Every
reduction route tried (`simp`/`dsimp`/`rfl`/`split`/matcher `unfold`) hits the same
ceiling. This is a Lean-kernel limitation, not a gap in the bound; revisit with a
matcher-equation `rw` that keeps `checked` opaque, or a model tweak making the WF
functions kernel-irreducible. -/

end XRPL.Model.Protocol
