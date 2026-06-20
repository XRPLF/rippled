import XRPL.Properties.Protocol.Number.Common.Rounding.SmallRange
import XRPL.Properties.Protocol.Number.Common.Int64Lemmas

namespace XRPL.Model.Protocol

/-- 16-digit `doRoundUp`, **carry-cusp** branch: rounding up at the top of the range
(`m = cMaxValue`) drops the trailing `9`, re-rounds (which always fires, since the
dropped digit is `9`), and renormalizes to `(cMinValue, e+1)`. -/
lemma doRoundUp_small_cusp (g : Guard) (neg : Bool) (e : Int) (mode : rounding_mode) (loc : String)
    (hb : (g.round mode == 1 || (g.round mode == 0 && cMaxValue % 2 == 1)) = true)
    (hexp_lo : minExponent ≤ e + 1) (hexp_hi : e + 1 ≤ maxExponent) :
    g.doRoundUp neg cMaxValue e cMinValue cMaxValue mode loc
      = .ok { negative_ := neg, mantissa_ := cMinValue, exponent_ := e + 1 } := by
  have h_le_maxRep : cMaxValue.toNat ≤ maxRep.toNat := by decide
  have h9 : cMaxValue % 10 = 9 := by decide
  have hdiv : cMaxValue / 10 = 999999999999999 := by decide
  have hdivsucc : (999999999999999 : UInt64) + 1 = cMinValue := by decide
  have hdig9 : 9 * 2 ^ 60 ≤ (g.push 9).digits_.toNat := by
    rw [toNat_push_digits, show (9 : UInt64).toNat % 16 = 9 from by decide]; omega
  have hne0 : (g.push 9).digits_ ≠ 0 := by
    intro h
    have : (g.push 9).digits_.toNat = 0 := by rw [h]; rfl
    omega
  have hpush_ne : (g.push 9).empty = false := by unfold Guard.empty; simp [hne0]
  have hpush_sbit : (g.push 9).sbit_ = g.sbit_ := Guard.push_sbit g 9
  -- the re-round bool fires (dropped digit is 9)
  have hroundUp' : ((g.push 9).round mode == 1
      || ((g.push 9).round mode == 0 && (999999999999999 : UInt64) % 2 == 1)) = true := by
    cases mode with
    | to_nearest =>
      have htn : (g.push 9).round .to_nearest = 1 := by
        unfold Guard.round
        rw [if_neg (by rw [hpush_ne]; exact Bool.false_ne_true),
            if_pos (show (g.push 9).digits_ > 0x5000000000000000 from by
              rw [gt_iff_lt, UInt64.lt_iff_toNat_lt,
                  show (0x5000000000000000 : UInt64).toNat = 5764607523034234880 from by decide]
              omega)]
      rw [htn]; rfl
    | towards_zero =>
      exfalso
      have : g.round .towards_zero = -1 ∨ g.round .towards_zero = -2 := by
        unfold Guard.round; by_cases he : g.empty = true
        · rw [if_pos he]; right; rfl
        · rw [if_neg he]; left; rfl
      rcases this with h | h <;> rw [h] at hb <;> simp at hb
    | downward =>
      have hsb : g.sbit_ = true := by
        by_contra hh; rw [Bool.not_eq_true] at hh
        have : g.round .downward = -1 ∨ g.round .downward = -2 := by
          unfold Guard.round; by_cases he : g.empty = true
          · rw [if_pos he]; right; rfl
          · rw [if_neg he, hh]; left; rfl
        rcases this with h | h <;> rw [h] at hb <;> simp at hb
      exact round_bool_downward_neg (g.push 9) 999999999999999 (hpush_sbit.trans hsb) hpush_ne
    | upward =>
      have hsb : g.sbit_ = false := by
        by_contra hh; rw [Bool.not_eq_false] at hh
        have : g.round .upward = -1 ∨ g.round .upward = -2 := by
          unfold Guard.round; by_cases he : g.empty = true
          · rw [if_pos he]; right; rfl
          · rw [if_neg he, hh]; left; rfl
        rcases this with h | h <;> rw [h] at hb <;> simp at hb
      exact round_bool_upward_pos (g.push 9) 999999999999999 (hpush_sbit.trans hsb) hpush_ne
  -- drive the doRoundUp pipeline through the doDropDigit branch
  unfold Guard.doRoundUp
  simp only []
  rw [pushOverflow_noop_of_le_maxRep h_le_maxRep g, hb]
  simp only [if_true]
  rw [if_neg (show ¬ (cMaxValue < cMaxValue ∧ cMaxValue < maxRep) from fun h => absurd (UInt64.lt_iff_toNat_lt.mp h.1) (by omega)),
      if_neg (show ¬ (maxRep < cMaxValue ∧ cMaxValue < maxRepUp) from fun h =>
        absurd (UInt64.lt_iff_toNat_lt.mp h.1) (by rw [maxRep_val, cMaxValue_val]; omega))]
  unfold Guard.doDropDigit
  rw [h9, hdiv]
  simp only []
  rw [if_pos hroundUp', hdivsucc,
      show Guard.bringIntoRange neg cMinValue (e + 1) cMinValue
          = { negative_ := neg, mantissa_ := cMinValue, exponent_ := e + 1 } from by
        rw [bringIntoRange_noscale_result (fun h => absurd (UInt64.lt_iff_toNat_lt.mp h.1) (by omega)),
            if_neg (not_or.mpr ⟨by omega, by decide⟩)]]
  dsimp only
  rw [if_neg (by omega)]

/-- **16-digit re-rounding bound.** Rounding a near-largeRange-bottom mantissa
(`[10^18, 2·10^18]`, the IOU-sum decade) down to the 16-digit range loses at most
the dropped 3-digit tail: the result is within one 16-digit ULP (`10^(exponent+3)`)
of the true value, in every mode. This is the missing piece for IOU arithmetic
rounding bounds (compose with the `Number` operation bound via `rel_error_trans`). -/
theorem normalizeToRange_16_within_ulp (n : Number) (mode : rounding_mode)
    (mant : Int64) (exp : Int)
    (h_lo : 10 ^ 18 ≤ n.mantissa_.toNat) (h_hi : n.mantissa_.toNat < 10 ^ 19)
    (he_lo : minExponent ≤ n.exponent_ + 3) (he_hi : n.exponent_ + 4 ≤ maxExponent)
    (hok : n.normalizeToRange cMinValue cMaxValue mode = .ok (mant, exp)) :
    |(mant.toInt : ℚ) * 10 ^ exp - n.toRat| ≤ (10 : ℚ) ^ (n.exponent_ + 3) := by
  obtain ⟨g, _hrep, _hsbit, _h_empty_of, h_red⟩ :=
    doNormalize_small_facts n.negative_ n.mantissa_ n.exponent_ mode h_lo h_hi he_lo (by omega)
  have hm3 : (n.mantissa_ / 10 / 10 / 10).toNat = n.mantissa_.toNat / 1000 :=
    m_div_thousand_toNat n.mantissa_
  have hcMax : cMaxValue.toNat = 10 ^ 16 - 1 := by decide
  have hcMin : cMinValue.toNat = 10 ^ 15 := by decide
  -- value characterization: the result magnitude is `(M/1000 or M/1000+1)·10^(e+3)`
  have hvalk : ∃ k : ℕ, (k = n.mantissa_.toNat / 1000 ∨ k = n.mantissa_.toNat / 1000 + 1) ∧
      k < 2 ^ 63 ∧
      (mant.toInt : ℚ) * 10 ^ exp
        = (if n.negative_ then (-1 : ℚ) else 1) * ((k : ℚ) * 10 ^ (n.exponent_ + 3)) := by
    by_cases hround : (g.round mode == 1
        || (g.round mode == 0 && (n.mantissa_ / 10 / 10 / 10) % 2 == 1)) = true
    · by_cases hcusp : n.mantissa_ / 10 / 10 / 10 = cMaxValue
      · -- carry-cusp: result `(cMinValue, e+4)`, value `(M/1000+1)·10^(e+3)`
        have hcompute : n.normalizeToRange cMinValue cMaxValue mode
            = .ok (if n.negative_ then -cMinValue.toInt64 else cMinValue.toInt64,
                   (n.exponent_ + 3) + 1) := by
          unfold Number.normalizeToRange
          rw [h_red, hcusp, doRoundUp_small_cusp g n.negative_ (n.exponent_ + 3) mode
            "Number::normalize 2" (by rw [hcusp] at hround; exact hround) (by omega) (by omega)]
          rfl
        rw [hcompute] at hok
        obtain ⟨hmant, hexp⟩ := Prod.mk.inj (Except.ok.inj hok)
        have hMk : n.mantissa_.toNat / 1000 = 10 ^ 16 - 1 := by
          have := congrArg UInt64.toNat hcusp; rw [hm3, hcMax] at this; exact this
        refine ⟨n.mantissa_.toNat / 1000 + 1, Or.inr rfl, by omega, ?_⟩
        rw [← hmant, ← hexp, signed_mantissa_toInt n.negative_ cMinValue (by rw [hcMin]; omega)]
        rw [hcMin, hMk, zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0) (n.exponent_ + 3) 1,
            zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0) n.exponent_ 3]
        push_cast; ring
      · -- fire: result `(M/1000+1, e+3)`
        have hlt : (n.mantissa_ / 10 / 10 / 10).toNat < cMaxValue.toNat := by
          have hne : (n.mantissa_ / 10 / 10 / 10).toNat ≠ cMaxValue.toNat :=
            fun h => hcusp (UInt64.toNat_inj.mp h)
          rw [hcMax, hm3] at hne ⊢; omega
        have hadd : (n.mantissa_ / 10 / 10 / 10 + 1).toNat = n.mantissa_.toNat / 1000 + 1 := by
          rw [UInt64.toNat_add, hm3, show (1 : UInt64).toNat = 1 from rfl]
          exact Nat.mod_eq_of_lt (by omega)
        have hcompute : n.normalizeToRange cMinValue cMaxValue mode
            = .ok (if n.negative_ then -(n.mantissa_ / 10 / 10 / 10 + 1).toInt64
                   else (n.mantissa_ / 10 / 10 / 10 + 1).toInt64, n.exponent_ + 3) := by
          unfold Number.normalizeToRange
          rw [h_red, doRoundUp_small_fire g n.negative_ _ (n.exponent_ + 3) mode
            "Number::normalize 2" hround (by rw [hcMin, hm3]; omega) hlt (by omega) (by omega)]
          rfl
        rw [hcompute] at hok
        obtain ⟨hmant, hexp⟩ := Prod.mk.inj (Except.ok.inj hok)
        refine ⟨(n.mantissa_ / 10 / 10 / 10 + 1).toNat, Or.inr hadd, by rw [hadd]; omega, ?_⟩
        rw [← hmant, ← hexp, signed_mantissa_toInt n.negative_ _ (by rw [hadd]; omega)]
        push_cast; ring
    · -- truncate: result `(M/1000, e+3)`
      have hcompute : n.normalizeToRange cMinValue cMaxValue mode
          = .ok (if n.negative_ then -(n.mantissa_ / 10 / 10 / 10).toInt64
                 else (n.mantissa_ / 10 / 10 / 10).toInt64, n.exponent_ + 3) := by
        unfold Number.normalizeToRange
        rw [h_red, doRoundUp_small_truncate g n.negative_ _ (n.exponent_ + 3) mode
          "Number::normalize 2" (by simpa using hround) (by rw [hcMin, hm3]; omega)
          (by rw [hcMax, hm3]; omega) (by omega) (by omega)]
        rfl
      rw [hcompute] at hok
      obtain ⟨hmant, hexp⟩ := Prod.mk.inj (Except.ok.inj hok)
      refine ⟨(n.mantissa_ / 10 / 10 / 10).toNat, Or.inl hm3, by rw [hm3]; omega, ?_⟩
      rw [← hmant, ← hexp, signed_mantissa_toInt n.negative_ _ (by rw [hm3]; omega)]
      push_cast; ring
  obtain ⟨k, hk, hk_lt, hvalue⟩ := hvalk
  have htruth : n.toRat = (if n.negative_ then (-1 : ℚ) else 1)
      * ((n.mantissa_.toNat : ℚ) * 10 ^ n.exponent_) := by
    by_cases hneg : n.negative_
    · rw [Number.toRat_of_neg n hneg, if_pos hneg]; ring
    · rw [Number.toRat_of_nonneg n (by simpa using hneg), if_neg (by simpa using hneg)]; ring
  rw [hvalue, htruth]
  have hpow : (10 : ℚ) ^ (n.exponent_ + 3) = (10 : ℚ) ^ n.exponent_ * 1000 := by
    rw [zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0)]; norm_num
  have hpe_pos : (0 : ℚ) < (10 : ℚ) ^ n.exponent_ := zpow_pos (by norm_num) _
  set s : ℚ := if n.negative_ then (-1 : ℚ) else 1 with hs_def
  have hs_abs : |s| = 1 := by rw [hs_def]; split <;> norm_num
  have hkey : |s * ((k : ℚ) * 10 ^ (n.exponent_ + 3))
      - s * ((n.mantissa_.toNat : ℚ) * 10 ^ n.exponent_)|
      = (10 : ℚ) ^ n.exponent_ * |(k : ℚ) * 1000 - (n.mantissa_.toNat : ℚ)| := by
    rw [hpow]
    rw [show s * ((k : ℚ) * ((10 : ℚ) ^ n.exponent_ * 1000))
          - s * ((n.mantissa_.toNat : ℚ) * 10 ^ n.exponent_)
        = s * 10 ^ n.exponent_ * ((k : ℚ) * 1000 - (n.mantissa_.toNat : ℚ)) from by ring]
    rw [abs_mul, abs_mul, hs_abs, abs_of_pos hpe_pos]; ring
  rw [hkey]
  have hmod : n.mantissa_.toNat % 1000 < 1000 := Nat.mod_lt _ (by norm_num)
  have hdiv : n.mantissa_.toNat / 1000 * 1000 + n.mantissa_.toNat % 1000 = n.mantissa_.toNat := by
    omega
  have hM : (n.mantissa_.toNat : ℚ)
      = ((n.mantissa_.toNat / 1000 : ℕ) : ℚ) * 1000 + ((n.mantissa_.toNat % 1000 : ℕ) : ℚ) := by
    exact_mod_cast hdiv.symm
  have hm' : ((n.mantissa_.toNat % 1000 : ℕ) : ℚ) < 1000 := by exact_mod_cast hmod
  have hm0 : (0 : ℚ) ≤ ((n.mantissa_.toNat % 1000 : ℕ) : ℚ) := by positivity
  have hbound : |(k : ℚ) * 1000 - (n.mantissa_.toNat : ℚ)| ≤ 1000 := by
    rw [abs_le, hM]
    rcases hk with h | h <;> rw [h] <;> push_cast <;> constructor <;> linarith
  calc (10 : ℚ) ^ n.exponent_ * |(k : ℚ) * 1000 - (n.mantissa_.toNat : ℚ)|
      ≤ (10 : ℚ) ^ n.exponent_ * 1000 :=
        mul_le_mul_of_nonneg_left hbound (le_of_lt hpe_pos)
    _ = (10 : ℚ) ^ (n.exponent_ + 3) := hpow.symm

/-- Companion to `normalizeToRange_16_within_ulp`: the 16-digit re-rounding output
mantissa is itself a canonical 16-digit mantissa — `|mant| ∈ [cMinValue, cMaxValue]`.
Same three-case structure (truncate / fire / carry-cusp). Needed to show an
`IOUAmount.ofNumber` result is `InRange16`. -/
theorem normalizeToRange_16_mantissa_range (n : Number) (mode : rounding_mode)
    (mant : Int64) (exp : Int)
    (h_lo : 10 ^ 18 ≤ n.mantissa_.toNat) (h_hi : n.mantissa_.toNat < 10 ^ 19)
    (he_lo : minExponent ≤ n.exponent_ + 3) (he_hi : n.exponent_ + 4 ≤ maxExponent)
    (hok : n.normalizeToRange cMinValue cMaxValue mode = .ok (mant, exp)) :
    cMinValue.toNat ≤ mant.toInt.natAbs ∧ mant.toInt.natAbs ≤ cMaxValue.toNat := by
  obtain ⟨g, _hrep, _hsbit, _h_empty_of, h_red⟩ :=
    doNormalize_small_facts n.negative_ n.mantissa_ n.exponent_ mode h_lo h_hi he_lo (by omega)
  have hm3 : (n.mantissa_ / 10 / 10 / 10).toNat = n.mantissa_.toNat / 1000 :=
    m_div_thousand_toNat n.mantissa_
  have hcMax : cMaxValue.toNat = 10 ^ 16 - 1 := by decide
  have hcMin : cMinValue.toNat = 10 ^ 15 := by decide
  by_cases hround : (g.round mode == 1
      || (g.round mode == 0 && (n.mantissa_ / 10 / 10 / 10) % 2 == 1)) = true
  · by_cases hcusp : n.mantissa_ / 10 / 10 / 10 = cMaxValue
    · have hcompute : n.normalizeToRange cMinValue cMaxValue mode
          = .ok (if n.negative_ then -cMinValue.toInt64 else cMinValue.toInt64,
                 (n.exponent_ + 3) + 1) := by
        unfold Number.normalizeToRange
        rw [h_red, hcusp, doRoundUp_small_cusp g n.negative_ (n.exponent_ + 3) mode
          "Number::normalize 2" (by rw [hcusp] at hround; exact hround) (by omega) (by omega)]
        rfl
      rw [hcompute] at hok
      obtain ⟨hmant, _⟩ := Prod.mk.inj (Except.ok.inj hok)
      rw [← hmant, signed_mantissa_natAbs n.negative_ cMinValue (by rw [hcMin]; omega),
          hcMin, hcMax]; omega
    · have hlt : n.mantissa_.toNat / 1000 < cMaxValue.toNat := by
        have hne : (n.mantissa_ / 10 / 10 / 10).toNat ≠ cMaxValue.toNat :=
          fun h => hcusp (UInt64.toNat_inj.mp h)
        rw [hcMax, hm3] at hne; rw [hcMax]; omega
      have hadd : (n.mantissa_ / 10 / 10 / 10 + 1).toNat = n.mantissa_.toNat / 1000 + 1 := by
        rw [UInt64.toNat_add, hm3, show (1 : UInt64).toNat = 1 from rfl]
        exact Nat.mod_eq_of_lt (by omega)
      have hcompute : n.normalizeToRange cMinValue cMaxValue mode
          = .ok (if n.negative_ then -(n.mantissa_ / 10 / 10 / 10 + 1).toInt64
                 else (n.mantissa_ / 10 / 10 / 10 + 1).toInt64, n.exponent_ + 3) := by
        unfold Number.normalizeToRange
        rw [h_red, doRoundUp_small_fire g n.negative_ _ (n.exponent_ + 3) mode
          "Number::normalize 2" hround (by rw [hcMin, hm3]; omega) (by rw [hm3]; exact hlt)
          (by omega) (by omega)]
        rfl
      rw [hcompute] at hok
      obtain ⟨hmant, _⟩ := Prod.mk.inj (Except.ok.inj hok)
      rw [← hmant, signed_mantissa_natAbs n.negative_ _ (by rw [hadd]; omega),
          hadd, hcMax]; omega
  · have hcompute : n.normalizeToRange cMinValue cMaxValue mode
        = .ok (if n.negative_ then -(n.mantissa_ / 10 / 10 / 10).toInt64
               else (n.mantissa_ / 10 / 10 / 10).toInt64, n.exponent_ + 3) := by
      unfold Number.normalizeToRange
      rw [h_red, doRoundUp_small_truncate g n.negative_ _ (n.exponent_ + 3) mode
        "Number::normalize 2" (by simpa using hround) (by rw [hcMin, hm3]; omega)
        (by rw [hcMax, hm3]; omega) (by omega) (by omega)]
      rfl
    rw [hcompute] at hok
    obtain ⟨hmant, _⟩ := Prod.mk.inj (Except.ok.inj hok)
    rw [← hmant, signed_mantissa_natAbs n.negative_ _ (by rw [hm3]; omega),
        hm3, hcMin, hcMax]; omega

end XRPL.Model.Protocol
