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
  rw [pushOverflow_noop_of_lt_maxRep (by rw [maxRep_val, cMaxValue_val]; omega) g mode, hb]
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

/-- **Tight (half-ULP) 16-digit re-rounding bound, `to_nearest`.** Round-to-nearest
picks the *closer* 16-digit grid point, so the error is at most **half** a 16-digit
ULP (`½·10^(exp+3)`), not a full one. The round decision is tied to the dropped
3-digit tail `M%1000` vs `500` via `round_correct`. This is the tight companion to
`normalizeToRange_16_within_ulp` and halves the `ε₂` term of every `to_nearest` IOU
arithmetic bound. -/
theorem normalizeToRange_16_within_half_ulp (n : Number) (mant : Int64) (exp : Int)
    (h_lo : 10 ^ 18 ≤ n.mantissa_.toNat) (h_hi : n.mantissa_.toNat < 10 ^ 19)
    (he_lo : minExponent ≤ n.exponent_ + 3) (he_hi : n.exponent_ + 4 ≤ maxExponent)
    (hok : n.normalizeToRange cMinValue cMaxValue .to_nearest = .ok (mant, exp)) :
    |(mant.toInt : ℚ) * 10 ^ exp - n.toRat| ≤ (1 / 2 : ℚ) * (10 : ℚ) ^ (n.exponent_ + 3) := by
  obtain ⟨g, hrep, _hsbit, _h_empty_of, h_red⟩ :=
    doNormalize_small_facts n.negative_ n.mantissa_ n.exponent_ .to_nearest h_lo h_hi he_lo (by omega)
  have hm3 : (n.mantissa_ / 10 / 10 / 10).toNat = n.mantissa_.toNat / 1000 :=
    m_div_thousand_toNat n.mantissa_
  have hcMax : cMaxValue.toNat = 10 ^ 16 - 1 := by decide
  have hcMin : cMinValue.toNat = 10 ^ 15 := by decide
  set M : ℕ := n.mantissa_.toNat with hMdef
  -- The round decision is governed by the dropped tail `M % 1000` vs `500`.
  have hround_ne_empty : ∀ {r : Int}, g.round .to_nearest = r → r ≠ -2 → ¬ g.empty := by
    intro r hr hr2 he; unfold Guard.round at hr; rw [if_pos he] at hr; exact hr2 hr.symm
  have hempty_tail : g.empty = true → M % 1000 = 0 := by
    intro he
    simp only [Guard.empty, Bool.and_eq_true, beq_iff_eq, Bool.not_eq_true'] at he
    obtain ⟨hd, hx⟩ := he
    have hf0 : (((M % 1000 : ℕ) : ℚ) / 1000) = 0 :=
      represents_eq_zero_of_digits_zero_xbit_false hd hx hrep
    have h2 : ((M % 1000 : ℕ) : ℚ) = 0 := by
      rcases div_eq_zero_iff.mp hf0 with h | h
      · exact h
      · norm_num at h
    exact_mod_cast h2
  have htail_gt_round1 : 500 < M % 1000 → g.round .to_nearest = 1 := by
    intro hgt
    have hne : ¬ g.empty := by
      intro he; have := hempty_tail he; omega
    refine (round_correct hne hrep).1.mpr ?_
    rw [gt_iff_lt, lt_div_iff₀ (by norm_num : (0:ℚ) < 1000)]
    have : (500 : ℚ) < (M % 1000 : ℕ) := by exact_mod_cast hgt
    linarith
  have htail_ge : (g.round .to_nearest == 1
        || (g.round .to_nearest == 0 && (n.mantissa_ / 10 / 10 / 10) % 2 == 1)) = true
      → 500 ≤ M % 1000 := by
    intro hfire
    rcases Bool.or_eq_true _ _ |>.mp hfire with h1 | h2
    · have hr1 : g.round .to_nearest = 1 := by simpa using h1
      have hne : ¬ g.empty := hround_ne_empty hr1 (by decide)
      have hf := (round_correct hne hrep).1.mp hr1
      rw [gt_iff_lt, lt_div_iff₀ (by norm_num : (0:ℚ) < 1000)] at hf
      have : (500 : ℚ) < (M % 1000 : ℕ) := by linarith
      have : 500 < M % 1000 := by exact_mod_cast this
      omega
    · have hr0 : g.round .to_nearest = 0 := by
        have := (Bool.and_eq_true _ _ |>.mp h2).1; simpa using this
      have hne : ¬ g.empty := hround_ne_empty hr0 (by decide)
      have hf := (round_correct hne hrep).2.2.mp hr0
      rw [div_eq_iff (by norm_num : (1000:ℚ) ≠ 0)] at hf
      have : ((M % 1000 : ℕ) : ℚ) = 500 := by linarith
      have : M % 1000 = 500 := by exact_mod_cast this
      omega
  have htail_le : ¬ (g.round .to_nearest == 1
        || (g.round .to_nearest == 0 && (n.mantissa_ / 10 / 10 / 10) % 2 == 1)) = true
      → M % 1000 ≤ 500 := by
    intro htrunc
    by_contra h; push_neg at h
    have hr1 : g.round .to_nearest = 1 := htail_gt_round1 (by omega)
    exact htrunc (by simp [hr1])
  have hmod_lt : M % 1000 < 1000 := Nat.mod_lt _ (by norm_num)
  have hdiv : M / 1000 * 1000 + M % 1000 = M := by omega
  -- value characterization + tight tail bound `|k·1000 − M| ≤ 500`
  have hvalk : ∃ k : ℕ, (mant.toInt : ℚ) * 10 ^ exp
        = (if n.negative_ then (-1 : ℚ) else 1) * ((k : ℚ) * 10 ^ (n.exponent_ + 3)) ∧
      |(k : ℚ) * 1000 - (M : ℚ)| ≤ 500 := by
    by_cases hround : (g.round .to_nearest == 1
        || (g.round .to_nearest == 0 && (n.mantissa_ / 10 / 10 / 10) % 2 == 1)) = true
    · have htge := htail_ge hround
      by_cases hcusp : n.mantissa_ / 10 / 10 / 10 = cMaxValue
      · have hcompute : n.normalizeToRange cMinValue cMaxValue .to_nearest
            = .ok (if n.negative_ then -cMinValue.toInt64 else cMinValue.toInt64,
                   (n.exponent_ + 3) + 1) := by
          unfold Number.normalizeToRange
          rw [h_red, hcusp, doRoundUp_small_cusp g n.negative_ (n.exponent_ + 3) .to_nearest
            "Number::normalize 2" (by rw [hcusp] at hround; exact hround) (by omega) (by omega)]
          rfl
        rw [hcompute] at hok
        obtain ⟨hmant, hexp⟩ := Prod.mk.inj (Except.ok.inj hok)
        have hMk : M / 1000 = 10 ^ 16 - 1 := by
          have := congrArg UInt64.toNat hcusp; rw [hm3, hcMax] at this; exact this
        refine ⟨M / 1000 + 1, ?_, ?_⟩
        · rw [← hmant, ← hexp, signed_mantissa_toInt n.negative_ cMinValue (by rw [hcMin]; omega)]
          rw [hcMin, hMk, zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0) (n.exponent_ + 3) 1,
              zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0) n.exponent_ 3]
          push_cast; ring
        · have hMq : (M : ℚ) = ((M / 1000 : ℕ) : ℚ) * 1000 + ((M % 1000 : ℕ) : ℚ) := by
            exact_mod_cast hdiv.symm
          rw [abs_le, hMq]; push_cast
          have h5 : (500 : ℚ) ≤ ((M % 1000 : ℕ) : ℚ) := by exact_mod_cast htge
          have h10 : ((M % 1000 : ℕ) : ℚ) < 1000 := by exact_mod_cast hmod_lt
          constructor <;> linarith
      · have hlt : (n.mantissa_ / 10 / 10 / 10).toNat < cMaxValue.toNat := by
          have hne : (n.mantissa_ / 10 / 10 / 10).toNat ≠ cMaxValue.toNat :=
            fun h => hcusp (UInt64.toNat_inj.mp h)
          rw [hcMax, hm3] at hne ⊢; omega
        have hadd : (n.mantissa_ / 10 / 10 / 10 + 1).toNat = M / 1000 + 1 := by
          rw [UInt64.toNat_add, hm3, show (1 : UInt64).toNat = 1 from rfl]
          exact Nat.mod_eq_of_lt (by omega)
        have hcompute : n.normalizeToRange cMinValue cMaxValue .to_nearest
            = .ok (if n.negative_ then -(n.mantissa_ / 10 / 10 / 10 + 1).toInt64
                   else (n.mantissa_ / 10 / 10 / 10 + 1).toInt64, n.exponent_ + 3) := by
          unfold Number.normalizeToRange
          rw [h_red, doRoundUp_small_fire g n.negative_ _ (n.exponent_ + 3) .to_nearest
            "Number::normalize 2" hround (by rw [hcMin, hm3]; omega) hlt (by omega) (by omega)]
          rfl
        rw [hcompute] at hok
        obtain ⟨hmant, hexp⟩ := Prod.mk.inj (Except.ok.inj hok)
        refine ⟨(n.mantissa_ / 10 / 10 / 10 + 1).toNat, ?_, ?_⟩
        · rw [← hmant, ← hexp, signed_mantissa_toInt n.negative_ _ (by rw [hadd]; omega)]
          push_cast; ring
        · rw [hadd]
          have hMq : (M : ℚ) = ((M / 1000 : ℕ) : ℚ) * 1000 + ((M % 1000 : ℕ) : ℚ) := by
            exact_mod_cast hdiv.symm
          rw [abs_le, hMq]; push_cast
          have h5 : (500 : ℚ) ≤ ((M % 1000 : ℕ) : ℚ) := by exact_mod_cast htge
          have h10 : ((M % 1000 : ℕ) : ℚ) < 1000 := by exact_mod_cast hmod_lt
          constructor <;> linarith
    · have htle := htail_le hround
      have hcompute : n.normalizeToRange cMinValue cMaxValue .to_nearest
          = .ok (if n.negative_ then -(n.mantissa_ / 10 / 10 / 10).toInt64
                 else (n.mantissa_ / 10 / 10 / 10).toInt64, n.exponent_ + 3) := by
        unfold Number.normalizeToRange
        rw [h_red, doRoundUp_small_truncate g n.negative_ _ (n.exponent_ + 3) .to_nearest
          "Number::normalize 2" (by simpa using hround) (by rw [hcMin, hm3]; omega)
          (by rw [hcMax, hm3]; omega) (by omega) (by omega)]
        rfl
      rw [hcompute] at hok
      obtain ⟨hmant, hexp⟩ := Prod.mk.inj (Except.ok.inj hok)
      refine ⟨(n.mantissa_ / 10 / 10 / 10).toNat, ?_, ?_⟩
      · rw [← hmant, ← hexp, signed_mantissa_toInt n.negative_ _ (by rw [hm3]; omega)]
        push_cast; ring
      · rw [hm3]
        have hMq : (M : ℚ) = ((M / 1000 : ℕ) : ℚ) * 1000 + ((M % 1000 : ℕ) : ℚ) := by
          exact_mod_cast hdiv.symm
        rw [abs_le, hMq]
        have h5 : ((M % 1000 : ℕ) : ℚ) ≤ 500 := by exact_mod_cast htle
        have h0 : (0 : ℚ) ≤ ((M % 1000 : ℕ) : ℚ) := by positivity
        constructor <;> linarith
  obtain ⟨k, hvalue, hbound⟩ := hvalk
  have htruth : n.toRat = (if n.negative_ then (-1 : ℚ) else 1)
      * ((M : ℚ) * 10 ^ n.exponent_) := by
    by_cases hneg : n.negative_
    · rw [Number.toRat_of_neg n hneg, if_pos hneg]; ring
    · rw [Number.toRat_of_nonneg n (by simpa using hneg), if_neg (by simpa using hneg)]; ring
  rw [hvalue, htruth]
  have hpow : (10 : ℚ) ^ (n.exponent_ + 3) = (10 : ℚ) ^ n.exponent_ * 1000 := by
    rw [zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0)]; norm_num
  have hpe_pos : (0 : ℚ) < (10 : ℚ) ^ n.exponent_ := zpow_pos (by norm_num) _
  set s : ℚ := if n.negative_ then (-1 : ℚ) else 1 with hs_def
  have hs_abs : |s| = 1 := by rw [hs_def]; split <;> norm_num
  have hkey : |s * ((k : ℚ) * 10 ^ (n.exponent_ + 3)) - s * ((M : ℚ) * 10 ^ n.exponent_)|
      = (10 : ℚ) ^ n.exponent_ * |(k : ℚ) * 1000 - (M : ℚ)| := by
    rw [hpow]
    rw [show s * ((k : ℚ) * ((10 : ℚ) ^ n.exponent_ * 1000)) - s * ((M : ℚ) * 10 ^ n.exponent_)
        = s * 10 ^ n.exponent_ * ((k : ℚ) * 1000 - (M : ℚ)) from by ring]
    rw [abs_mul, abs_mul, hs_abs, abs_of_pos hpe_pos]; ring
  rw [hkey]
  calc (10 : ℚ) ^ n.exponent_ * |(k : ℚ) * 1000 - (M : ℚ)|
      ≤ (10 : ℚ) ^ n.exponent_ * 500 := mul_le_mul_of_nonneg_left hbound (le_of_lt hpe_pos)
    _ = (1 / 2 : ℚ) * (10 : ℚ) ^ (n.exponent_ + 3) := by rw [hpow]; ring

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

/-- Companion exponent fact: the 16-digit re-rounding output exponent is
`n.exponent_ + 3` (truncate / fire) or `n.exponent_ + 4` (carry-cusp). -/
theorem normalizeToRange_16_exp_range (n : Number) (mode : rounding_mode)
    (mant : Int64) (exp : Int)
    (h_lo : 10 ^ 18 ≤ n.mantissa_.toNat) (h_hi : n.mantissa_.toNat < 10 ^ 19)
    (he_lo : minExponent ≤ n.exponent_ + 3) (he_hi : n.exponent_ + 4 ≤ maxExponent)
    (hok : n.normalizeToRange cMinValue cMaxValue mode = .ok (mant, exp)) :
    n.exponent_ + 3 ≤ exp ∧ exp ≤ n.exponent_ + 4 := by
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
      obtain ⟨_, hexp⟩ := Prod.mk.inj (Except.ok.inj hok)
      rw [← hexp]; omega
    · have hlt : n.mantissa_.toNat / 1000 < cMaxValue.toNat := by
        have hne : (n.mantissa_ / 10 / 10 / 10).toNat ≠ cMaxValue.toNat :=
          fun h => hcusp (UInt64.toNat_inj.mp h)
        rw [hcMax, hm3] at hne; rw [hcMax]; omega
      have hcompute : n.normalizeToRange cMinValue cMaxValue mode
          = .ok (if n.negative_ then -(n.mantissa_ / 10 / 10 / 10 + 1).toInt64
                 else (n.mantissa_ / 10 / 10 / 10 + 1).toInt64, n.exponent_ + 3) := by
        unfold Number.normalizeToRange
        rw [h_red, doRoundUp_small_fire g n.negative_ _ (n.exponent_ + 3) mode
          "Number::normalize 2" hround (by rw [hcMin, hm3]; omega) (by rw [hm3]; exact hlt)
          (by omega) (by omega)]
        rfl
      rw [hcompute] at hok
      obtain ⟨_, hexp⟩ := Prod.mk.inj (Except.ok.inj hok)
      rw [← hexp]; omega
  · have hcompute : n.normalizeToRange cMinValue cMaxValue mode
        = .ok (if n.negative_ then -(n.mantissa_ / 10 / 10 / 10).toInt64
               else (n.mantissa_ / 10 / 10 / 10).toInt64, n.exponent_ + 3) := by
      unfold Number.normalizeToRange
      rw [h_red, doRoundUp_small_truncate g n.negative_ _ (n.exponent_ + 3) mode
        "Number::normalize 2" (by simpa using hround) (by rw [hcMin, hm3]; omega)
        (by rw [hcMax, hm3]; omega) (by omega) (by omega)]
      rfl
    rw [hcompute] at hok
    obtain ⟨_, hexp⟩ := Prod.mk.inj (Except.ok.inj hok)
    rw [← hexp]; omega

/-! ## `normalizeToRange` at the 16-digit range: directed-mode correct-side (directional)

Companions to `normalizeToRange_16_within_ulp` (magnitude). For the directed modes,
the re-rounded value lands on the correct side of the source: `downward` never
exceeds, `upward` never undershoots, `towards_zero` never grows in magnitude. Same
three-case structure (truncate / fire / carry-cusp), with the round-bit fixed by
sign via `round_bool_{downward,upward}_*`. These give the directional half of the
directed-mode IOU re-rounding bound. -/

theorem normalizeToRange_16_downward (n : Number) (mant : Int64) (exp : Int)
    (h_lo : 10 ^ 18 ≤ n.mantissa_.toNat) (h_hi : n.mantissa_.toNat < 10 ^ 19)
    (he_lo : minExponent ≤ n.exponent_ + 3) (he_hi : n.exponent_ + 4 ≤ maxExponent)
    (hok : n.normalizeToRange cMinValue cMaxValue .downward = .ok (mant, exp)) :
    (mant.toInt : ℚ) * 10 ^ exp ≤ n.toRat := by
  obtain ⟨g, hrep, hsbit, h_empty_of, h_red⟩ :=
    doNormalize_small_facts n.negative_ n.mantissa_ n.exponent_ .downward h_lo h_hi he_lo (by omega)
  have hm3 : (n.mantissa_ / 10 / 10 / 10).toNat = n.mantissa_.toNat / 1000 :=
    m_div_thousand_toNat n.mantissa_
  have hcMax : cMaxValue.toNat = 10 ^ 16 - 1 := by decide
  have hcMin : cMinValue.toNat = 10 ^ 15 := by decide
  have hmod : n.mantissa_.toNat % 1000 < 1000 := Nat.mod_lt _ (by norm_num)
  have hdiv : n.mantissa_.toNat / 1000 * 1000 + n.mantissa_.toNat % 1000 = n.mantissa_.toNat := by omega
  have hpow : (10 : ℚ) ^ (n.exponent_ + 3) = (10 : ℚ) ^ n.exponent_ * 1000 := by
    rw [zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0)]; norm_num
  have hpe_pos : (0 : ℚ) < (10 : ℚ) ^ n.exponent_ := zpow_pos (by norm_num) _
  have htruth : n.toRat = (if n.negative_ then (-1 : ℚ) else 1)
      * ((n.mantissa_.toNat : ℚ) * 10 ^ n.exponent_) := by
    by_cases hneg : n.negative_
    · rw [Number.toRat_of_neg n hneg, if_pos hneg]; ring
    · rw [Number.toRat_of_nonneg n (by simpa using hneg), if_neg (by simpa using hneg)]; ring
  -- The result value is `s · k · 10^(e+3)` for `k` determined by sign.
  -- Goal reduces (via the value formula) to a `k·1000 vs M` comparison.
  by_cases hneg : n.negative_
  · -- negative (sbit = true): downward rounds away from zero ⟹ result ≤ truth
    have hsb : g.sbit_ = true := by rw [hsbit, hneg]
    by_cases hemp : g.empty = true
    · -- empty guard ⟹ M % 1000 = 0 ⟹ exact, result = truth
      have hrem0 : n.mantissa_.toNat % 1000 = 0 := by
        by_contra h
        have hpos : (0 : ℚ) < ((n.mantissa_.toNat % 1000 : ℕ) : ℚ) / 1000 := by
          have : 0 < n.mantissa_.toNat % 1000 := Nat.pos_of_ne_zero h
          positivity
        have := Guard.not_empty_of_represents_pos hrep hpos
        rw [hemp] at this; exact absurd this (by decide)
      have hround : (g.round .downward == 1 || (g.round .downward == 0
          && (n.mantissa_ / 10 / 10 / 10) % 2 == 1)) = false := round_bool_empty g _ .downward hemp
      have hcompute : n.normalizeToRange cMinValue cMaxValue .downward
          = .ok (if n.negative_ then -(n.mantissa_ / 10 / 10 / 10).toInt64
                 else (n.mantissa_ / 10 / 10 / 10).toInt64, n.exponent_ + 3) := by
        unfold Number.normalizeToRange
        rw [h_red, doRoundUp_small_truncate g n.negative_ _ (n.exponent_ + 3) .downward
          "Number::normalize 2" hround (by rw [hcMin, hm3]; omega) (by rw [hcMax, hm3]; omega)
          (by omega) (by omega)]
        rfl
      rw [hcompute] at hok
      obtain ⟨hmant, hexp⟩ := Prod.mk.inj (Except.ok.inj hok)
      rw [← hmant, ← hexp, signed_mantissa_toInt n.negative_ _ (by rw [hm3]; omega), htruth, hpow]
      simp only [if_pos hneg]
      have hcast : ((n.mantissa_ / 10 / 10 / 10).toNat : ℚ) * 1000 = (n.mantissa_.toNat : ℚ) := by
        rw [hm3]
        have h : n.mantissa_.toNat / 1000 * 1000 = n.mantissa_.toNat := by omega
        calc ((n.mantissa_.toNat / 1000 : ℕ) : ℚ) * 1000
            = ((n.mantissa_.toNat / 1000 * 1000 : ℕ) : ℚ) := by push_cast; ring
          _ = (n.mantissa_.toNat : ℚ) := by rw [h]
      have hAM : ((n.mantissa_ / 10 / 10 / 10).toNat : ℚ) * (10 ^ n.exponent_ * 1000)
          = (n.mantissa_.toNat : ℚ) * 10 ^ n.exponent_ := by rw [← hcast]; ring
      push_cast
      linarith [hAM]
    · -- nonempty ⟹ downward (neg) fires ⟹ k = M/1000 + 1 (fire or cusp)
      have hround : (g.round .downward == 1 || (g.round .downward == 0
          && (n.mantissa_ / 10 / 10 / 10) % 2 == 1)) = true :=
        round_bool_downward_neg g _ hsb (by simpa using hemp)
      by_cases hcusp : n.mantissa_ / 10 / 10 / 10 = cMaxValue
      · have hcompute : n.normalizeToRange cMinValue cMaxValue .downward
            = .ok (if n.negative_ then -cMinValue.toInt64 else cMinValue.toInt64, (n.exponent_ + 3) + 1) := by
          unfold Number.normalizeToRange
          rw [h_red, hcusp, doRoundUp_small_cusp g n.negative_ (n.exponent_ + 3) .downward
            "Number::normalize 2" (by rw [hcusp] at hround; exact hround) (by omega) (by omega)]
          rfl
        rw [hcompute] at hok
        obtain ⟨hmant, hexp⟩ := Prod.mk.inj (Except.ok.inj hok)
        have hMk : n.mantissa_.toNat / 1000 = 10 ^ 16 - 1 := by
          have := congrArg UInt64.toNat hcusp; rw [hm3, hcMax] at this; exact this
        rw [← hmant, ← hexp, signed_mantissa_toInt n.negative_ cMinValue (by rw [hcMin]; omega), htruth]
        simp only [if_pos hneg]
        rw [hcMin, show ((n.exponent_ + 3) + 1 : Int) = n.exponent_ + 4 by ring,
            zpow_add₀ (by norm_num : (10:ℚ) ≠ 0) n.exponent_ 4]
        have hge : (n.mantissa_.toNat : ℚ) ≤ (10 ^ 15 : ℚ) * 10 ^ 4 := by
          have : n.mantissa_.toNat < 10 ^ 19 := h_hi
          have h19 : (10 ^ 15 : ℚ) * 10 ^ 4 = 10 ^ 19 := by norm_num
          rw [h19]; exact_mod_cast le_of_lt this
        push_cast
        nlinarith [hpe_pos, hge]
      · have hlt : (n.mantissa_ / 10 / 10 / 10).toNat < cMaxValue.toNat := by
          have hne : (n.mantissa_ / 10 / 10 / 10).toNat ≠ cMaxValue.toNat :=
            fun h => hcusp (UInt64.toNat_inj.mp h)
          rw [hcMax, hm3] at hne ⊢; omega
        have hadd : (n.mantissa_ / 10 / 10 / 10 + 1).toNat = n.mantissa_.toNat / 1000 + 1 := by
          rw [UInt64.toNat_add, hm3, show (1 : UInt64).toNat = 1 from rfl]
          exact Nat.mod_eq_of_lt (by omega)
        have hcompute : n.normalizeToRange cMinValue cMaxValue .downward
            = .ok (if n.negative_ then -(n.mantissa_ / 10 / 10 / 10 + 1).toInt64
                   else (n.mantissa_ / 10 / 10 / 10 + 1).toInt64, n.exponent_ + 3) := by
          unfold Number.normalizeToRange
          rw [h_red, doRoundUp_small_fire g n.negative_ _ (n.exponent_ + 3) .downward
            "Number::normalize 2" hround (by rw [hcMin, hm3]; omega) hlt (by omega) (by omega)]
          rfl
        rw [hcompute] at hok
        obtain ⟨hmant, hexp⟩ := Prod.mk.inj (Except.ok.inj hok)
        rw [← hmant, ← hexp, signed_mantissa_toInt n.negative_ _ (by rw [hadd]; omega), htruth, hpow]
        simp only [if_pos hneg]
        have hval : (n.mantissa_.toNat : ℚ) ≤ ((n.mantissa_ / 10 / 10 / 10 + 1).toNat : ℚ) * 1000 := by
          rw [hadd]
          have h : n.mantissa_.toNat ≤ (n.mantissa_.toNat / 1000 + 1) * 1000 := by omega
          calc (n.mantissa_.toNat : ℚ) ≤ ((n.mantissa_.toNat / 1000 + 1) * 1000 : ℕ) := by exact_mod_cast h
            _ = ((n.mantissa_.toNat / 1000 + 1 : ℕ) : ℚ) * 1000 := by push_cast; ring
        push_cast
        nlinarith [hpe_pos, hval]
  · -- positive (sbit = false): downward truncates ⟹ result ≤ truth
    have hnf : n.negative_ = false := by simpa using hneg
    have hsb : g.sbit_ = false := by rw [hsbit, hnf]
    have hround : (g.round .downward == 1 || (g.round .downward == 0
        && (n.mantissa_ / 10 / 10 / 10) % 2 == 1)) = false := round_bool_downward_pos g _ hsb
    have hcompute : n.normalizeToRange cMinValue cMaxValue .downward
        = .ok (if n.negative_ then -(n.mantissa_ / 10 / 10 / 10).toInt64
               else (n.mantissa_ / 10 / 10 / 10).toInt64, n.exponent_ + 3) := by
      unfold Number.normalizeToRange
      rw [h_red, doRoundUp_small_truncate g n.negative_ _ (n.exponent_ + 3) .downward
        "Number::normalize 2" hround (by rw [hcMin, hm3]; omega) (by rw [hcMax, hm3]; omega)
        (by omega) (by omega)]
      rfl
    rw [hcompute] at hok
    obtain ⟨hmant, hexp⟩ := Prod.mk.inj (Except.ok.inj hok)
    rw [← hmant, ← hexp, signed_mantissa_toInt n.negative_ _ (by rw [hm3]; omega), htruth, hpow]
    simp only [if_neg hneg]
    have hle : ((n.mantissa_ / 10 / 10 / 10).toNat : ℚ) * 1000 ≤ (n.mantissa_.toNat : ℚ) := by
      rw [hm3]
      rw [show ((n.mantissa_.toNat / 1000 : ℕ) : ℚ) * 1000 = ((n.mantissa_.toNat / 1000 * 1000 : ℕ) : ℚ) by push_cast; ring]
      have : n.mantissa_.toNat / 1000 * 1000 ≤ n.mantissa_.toNat := by omega
      exact_mod_cast this
    push_cast
    nlinarith [hpe_pos, hle]


theorem normalizeToRange_16_upward (n : Number) (mant : Int64) (exp : Int)
    (h_lo : 10 ^ 18 ≤ n.mantissa_.toNat) (h_hi : n.mantissa_.toNat < 10 ^ 19)
    (he_lo : minExponent ≤ n.exponent_ + 3) (he_hi : n.exponent_ + 4 ≤ maxExponent)
    (hok : n.normalizeToRange cMinValue cMaxValue .upward = .ok (mant, exp)) :
    n.toRat ≤ (mant.toInt : ℚ) * 10 ^ exp := by
  obtain ⟨g, hrep, hsbit, h_empty_of, h_red⟩ :=
    doNormalize_small_facts n.negative_ n.mantissa_ n.exponent_ .upward h_lo h_hi he_lo (by omega)
  have hm3 : (n.mantissa_ / 10 / 10 / 10).toNat = n.mantissa_.toNat / 1000 :=
    m_div_thousand_toNat n.mantissa_
  have hcMax : cMaxValue.toNat = 10 ^ 16 - 1 := by decide
  have hcMin : cMinValue.toNat = 10 ^ 15 := by decide
  have hmod : n.mantissa_.toNat % 1000 < 1000 := Nat.mod_lt _ (by norm_num)
  have hdiv : n.mantissa_.toNat / 1000 * 1000 + n.mantissa_.toNat % 1000 = n.mantissa_.toNat := by omega
  have hpow : (10 : ℚ) ^ (n.exponent_ + 3) = (10 : ℚ) ^ n.exponent_ * 1000 := by
    rw [zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0)]; norm_num
  have hpe_pos : (0 : ℚ) < (10 : ℚ) ^ n.exponent_ := zpow_pos (by norm_num) _
  have htruth : n.toRat = (if n.negative_ then (-1 : ℚ) else 1)
      * ((n.mantissa_.toNat : ℚ) * 10 ^ n.exponent_) := by
    by_cases hneg : n.negative_
    · rw [Number.toRat_of_neg n hneg, if_pos hneg]; ring
    · rw [Number.toRat_of_nonneg n (by simpa using hneg), if_neg (by simpa using hneg)]; ring
  by_cases hneg : n.negative_
  · -- negative (sbit = true): upward truncates ⟹ truth ≤ result
    have hsb : g.sbit_ = true := by rw [hsbit, hneg]
    have hround : (g.round .upward == 1 || (g.round .upward == 0
        && (n.mantissa_ / 10 / 10 / 10) % 2 == 1)) = false := round_bool_upward_neg g _ hsb
    have hcompute : n.normalizeToRange cMinValue cMaxValue .upward
        = .ok (if n.negative_ then -(n.mantissa_ / 10 / 10 / 10).toInt64
               else (n.mantissa_ / 10 / 10 / 10).toInt64, n.exponent_ + 3) := by
      unfold Number.normalizeToRange
      rw [h_red, doRoundUp_small_truncate g n.negative_ _ (n.exponent_ + 3) .upward
        "Number::normalize 2" hround (by rw [hcMin, hm3]; omega) (by rw [hcMax, hm3]; omega)
        (by omega) (by omega)]
      rfl
    rw [hcompute] at hok
    obtain ⟨hmant, hexp⟩ := Prod.mk.inj (Except.ok.inj hok)
    rw [← hmant, ← hexp, signed_mantissa_toInt n.negative_ _ (by rw [hm3]; omega), htruth, hpow]
    simp only [if_pos hneg]
    push_cast
    have hle : ((n.mantissa_ / 10 / 10 / 10).toNat : ℚ) * 1000 ≤ (n.mantissa_.toNat : ℚ) := by
      rw [hm3]
      rw [show ((n.mantissa_.toNat / 1000 : ℕ) : ℚ) * 1000 = ((n.mantissa_.toNat / 1000 * 1000 : ℕ) : ℚ) by push_cast; ring]
      have : n.mantissa_.toNat / 1000 * 1000 ≤ n.mantissa_.toNat := by omega
      exact_mod_cast this
    nlinarith [hpe_pos, hle]
  · -- positive (sbit = false): upward fires (nonempty) or is exact (empty)
    have hnf : n.negative_ = false := by simpa using hneg
    have hsb : g.sbit_ = false := by rw [hsbit, hnf]
    by_cases hemp : g.empty = true
    · have hrem0 : n.mantissa_.toNat % 1000 = 0 := by
        by_contra h
        have hpos : (0 : ℚ) < ((n.mantissa_.toNat % 1000 : ℕ) : ℚ) / 1000 := by
          have : 0 < n.mantissa_.toNat % 1000 := Nat.pos_of_ne_zero h
          positivity
        have := Guard.not_empty_of_represents_pos hrep hpos
        rw [hemp] at this; exact absurd this (by decide)
      have hround : (g.round .upward == 1 || (g.round .upward == 0
          && (n.mantissa_ / 10 / 10 / 10) % 2 == 1)) = false := round_bool_empty g _ .upward hemp
      have hcompute : n.normalizeToRange cMinValue cMaxValue .upward
          = .ok (if n.negative_ then -(n.mantissa_ / 10 / 10 / 10).toInt64
                 else (n.mantissa_ / 10 / 10 / 10).toInt64, n.exponent_ + 3) := by
        unfold Number.normalizeToRange
        rw [h_red, doRoundUp_small_truncate g n.negative_ _ (n.exponent_ + 3) .upward
          "Number::normalize 2" hround (by rw [hcMin, hm3]; omega) (by rw [hcMax, hm3]; omega)
          (by omega) (by omega)]
        rfl
      rw [hcompute] at hok
      obtain ⟨hmant, hexp⟩ := Prod.mk.inj (Except.ok.inj hok)
      rw [← hmant, ← hexp, signed_mantissa_toInt n.negative_ _ (by rw [hm3]; omega), htruth, hpow]
      simp only [if_neg hneg]
      push_cast
      have hcast : ((n.mantissa_ / 10 / 10 / 10).toNat : ℚ) * 1000 = (n.mantissa_.toNat : ℚ) := by
        rw [hm3]
        have h : n.mantissa_.toNat / 1000 * 1000 = n.mantissa_.toNat := by omega
        calc ((n.mantissa_.toNat / 1000 : ℕ) : ℚ) * 1000
            = ((n.mantissa_.toNat / 1000 * 1000 : ℕ) : ℚ) := by push_cast; ring
          _ = (n.mantissa_.toNat : ℚ) := by rw [h]
      have hAM : ((n.mantissa_ / 10 / 10 / 10).toNat : ℚ) * (10 ^ n.exponent_ * 1000)
          = (n.mantissa_.toNat : ℚ) * 10 ^ n.exponent_ := by rw [← hcast]; ring
      linarith [hAM]
    · have hround : (g.round .upward == 1 || (g.round .upward == 0
          && (n.mantissa_ / 10 / 10 / 10) % 2 == 1)) = true :=
        round_bool_upward_pos g _ hsb (by simpa using hemp)
      by_cases hcusp : n.mantissa_ / 10 / 10 / 10 = cMaxValue
      · have hcompute : n.normalizeToRange cMinValue cMaxValue .upward
            = .ok (if n.negative_ then -cMinValue.toInt64 else cMinValue.toInt64, (n.exponent_ + 3) + 1) := by
          unfold Number.normalizeToRange
          rw [h_red, hcusp, doRoundUp_small_cusp g n.negative_ (n.exponent_ + 3) .upward
            "Number::normalize 2" (by rw [hcusp] at hround; exact hround) (by omega) (by omega)]
          rfl
        rw [hcompute] at hok
        obtain ⟨hmant, hexp⟩ := Prod.mk.inj (Except.ok.inj hok)
        rw [← hmant, ← hexp, signed_mantissa_toInt n.negative_ cMinValue (by rw [hcMin]; omega), htruth]
        simp only [if_neg hneg]
        rw [hcMin, show ((n.exponent_ + 3) + 1 : Int) = n.exponent_ + 4 by ring,
            zpow_add₀ (by norm_num : (10:ℚ) ≠ 0) n.exponent_ 4]
        have hge : (n.mantissa_.toNat : ℚ) ≤ (10 ^ 15 : ℚ) * 10 ^ 4 := by
          have h19 : (10 ^ 15 : ℚ) * 10 ^ 4 = 10 ^ 19 := by norm_num
          rw [h19]; exact_mod_cast le_of_lt h_hi
        push_cast
        nlinarith [hpe_pos, hge]
      · have hlt : (n.mantissa_ / 10 / 10 / 10).toNat < cMaxValue.toNat := by
          have hne : (n.mantissa_ / 10 / 10 / 10).toNat ≠ cMaxValue.toNat :=
            fun h => hcusp (UInt64.toNat_inj.mp h)
          rw [hcMax, hm3] at hne ⊢; omega
        have hadd : (n.mantissa_ / 10 / 10 / 10 + 1).toNat = n.mantissa_.toNat / 1000 + 1 := by
          rw [UInt64.toNat_add, hm3, show (1 : UInt64).toNat = 1 from rfl]
          exact Nat.mod_eq_of_lt (by omega)
        have hcompute : n.normalizeToRange cMinValue cMaxValue .upward
            = .ok (if n.negative_ then -(n.mantissa_ / 10 / 10 / 10 + 1).toInt64
                   else (n.mantissa_ / 10 / 10 / 10 + 1).toInt64, n.exponent_ + 3) := by
          unfold Number.normalizeToRange
          rw [h_red, doRoundUp_small_fire g n.negative_ _ (n.exponent_ + 3) .upward
            "Number::normalize 2" hround (by rw [hcMin, hm3]; omega) hlt (by omega) (by omega)]
          rfl
        rw [hcompute] at hok
        obtain ⟨hmant, hexp⟩ := Prod.mk.inj (Except.ok.inj hok)
        rw [← hmant, ← hexp, signed_mantissa_toInt n.negative_ _ (by rw [hadd]; omega), htruth, hpow]
        simp only [if_neg hneg]
        push_cast
        have hval : (n.mantissa_.toNat : ℚ) ≤ ((n.mantissa_ / 10 / 10 / 10 + 1).toNat : ℚ) * 1000 := by
          rw [hadd]
          have h : n.mantissa_.toNat ≤ (n.mantissa_.toNat / 1000 + 1) * 1000 := by omega
          calc (n.mantissa_.toNat : ℚ) ≤ ((n.mantissa_.toNat / 1000 + 1) * 1000 : ℕ) := by exact_mod_cast h
            _ = ((n.mantissa_.toNat / 1000 + 1 : ℕ) : ℚ) * 1000 := by push_cast; ring
        nlinarith [hpe_pos, hval]

theorem normalizeToRange_16_towards_zero (n : Number) (mant : Int64) (exp : Int)
    (h_lo : 10 ^ 18 ≤ n.mantissa_.toNat) (h_hi : n.mantissa_.toNat < 10 ^ 19)
    (he_lo : minExponent ≤ n.exponent_ + 3) (he_hi : n.exponent_ + 4 ≤ maxExponent)
    (hok : n.normalizeToRange cMinValue cMaxValue .towards_zero = .ok (mant, exp)) :
    |(mant.toInt : ℚ) * 10 ^ exp| ≤ |n.toRat| := by
  obtain ⟨g, hrep, hsbit, h_empty_of, h_red⟩ :=
    doNormalize_small_facts n.negative_ n.mantissa_ n.exponent_ .towards_zero h_lo h_hi he_lo (by omega)
  have hm3 : (n.mantissa_ / 10 / 10 / 10).toNat = n.mantissa_.toNat / 1000 :=
    m_div_thousand_toNat n.mantissa_
  have hcMax : cMaxValue.toNat = 10 ^ 16 - 1 := by decide
  have hcMin : cMinValue.toNat = 10 ^ 15 := by decide
  have hpe_pos : (0 : ℚ) < (10 : ℚ) ^ n.exponent_ := zpow_pos (by norm_num) _
  have hpow : (10 : ℚ) ^ (n.exponent_ + 3) = (10 : ℚ) ^ n.exponent_ * 1000 := by
    rw [zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0)]; norm_num
  have hround : (g.round .towards_zero == 1 || (g.round .towards_zero == 0
      && (n.mantissa_ / 10 / 10 / 10) % 2 == 1)) = false := round_bool_towards_zero g _
  have hcompute : n.normalizeToRange cMinValue cMaxValue .towards_zero
      = .ok (if n.negative_ then -(n.mantissa_ / 10 / 10 / 10).toInt64
             else (n.mantissa_ / 10 / 10 / 10).toInt64, n.exponent_ + 3) := by
    unfold Number.normalizeToRange
    rw [h_red, doRoundUp_small_truncate g n.negative_ _ (n.exponent_ + 3) .towards_zero
      "Number::normalize 2" hround (by rw [hcMin, hm3]; omega) (by rw [hcMax, hm3]; omega)
      (by omega) (by omega)]
    rfl
  rw [hcompute] at hok
  obtain ⟨hmant, hexp⟩ := Prod.mk.inj (Except.ok.inj hok)
  have hval : (mant.toInt : ℚ) * 10 ^ exp
      = (if n.negative_ then (-1 : ℚ) else 1) * (((n.mantissa_.toNat / 1000 : ℕ) : ℚ) * 10 ^ (n.exponent_ + 3)) := by
    rw [← hmant, ← hexp, signed_mantissa_toInt n.negative_ _ (by rw [hm3]; omega), hm3]
    split_ifs <;> simp only [Int.cast_mul, Int.cast_neg, Int.cast_one, Int.cast_natCast] <;> ring
  have hresult_abs : |(mant.toInt : ℚ) * 10 ^ exp|
      = ((n.mantissa_.toNat / 1000 : ℕ) : ℚ) * 10 ^ (n.exponent_ + 3) := by
    rw [hval, abs_mul, show |if n.negative_ then (-1 : ℚ) else 1| = 1 by split_ifs <;> norm_num,
        abs_of_nonneg (by positivity)]
    ring
  have htruth_abs : |n.toRat| = (n.mantissa_.toNat : ℚ) * 10 ^ n.exponent_ := by
    rcases hsg : n.negative_ with _ | _
    · rw [Number.toRat_of_nonneg n (by rw [hsg]), abs_mul, abs_of_nonneg (by positivity),
          abs_of_pos (zpow_pos (by norm_num : (0:ℚ) < 10) _)]
    · rw [Number.toRat_of_neg n (by rw [hsg]), abs_neg, abs_mul, abs_of_nonneg (by positivity),
          abs_of_pos (zpow_pos (by norm_num : (0:ℚ) < 10) _)]
  rw [hresult_abs, htruth_abs, hpow]
  have hle : ((n.mantissa_.toNat / 1000 : ℕ) : ℚ) * 1000 ≤ (n.mantissa_.toNat : ℚ) := by
    rw [show ((n.mantissa_.toNat / 1000 : ℕ) : ℚ) * 1000 = ((n.mantissa_.toNat / 1000 * 1000 : ℕ) : ℚ) by push_cast; ring]
    have : n.mantissa_.toNat / 1000 * 1000 ≤ n.mantissa_.toNat := by omega
    exact_mod_cast this
  nlinarith [hpe_pos, hle]

end XRPL.Model.Protocol
