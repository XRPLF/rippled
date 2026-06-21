import Mathlib.Tactic

import XRPL.Properties.Protocol.STAmount.RoundToScale.Common.Sum


namespace XRPL.Model.Protocol

/-! # `STAmount.roundToScale` is correctly rounded onto the scale grid

The flagship discrete theorem for the quantization layer: `roundToScale`
returns the value rounded onto `10^s·ℤ`, exactly the floor/ceiling per the
directed modes, and one of the two enclosing grid points for `.to_nearest`
(faithful rounding; the tie decision is blurred by double rounding through
the 19-digit stage). -/

/-- A zero-mantissa `Number` renormalizes to the zero `IOUAmount`. -/
private lemma IOUAmount.ofNumber_zero_mantissa (n : Number) (mode : rounding_mode)
    (h : n.mantissa_ = 0) :
    IOUAmount.ofNumber n mode = .ok IOUAmount.zero := by
  have hd : doNormalize n.negative_ n.mantissa_ n.exponent_ cMinValue cMaxValue mode
      = .ok Number.zero := by
    unfold doNormalize
    rw [show (n.mantissa_ == 0) = true from beq_iff_eq.mpr h, if_pos rfl]
  unfold IOUAmount.ofNumber IOUAmount.fromNumber Number.normalizeToRange
  rw [hd]
  rfl

/-- `ofIOUAmount` of the zero `IOUAmount` is the canonical zero record. -/
private lemma STAmount.ofIOUAmount_zero (issue : Issue) (mode : rounding_mode)
    (h_not_xrp : (Asset.issue issue).isNative = false) :
    STAmount.ofIOUAmount IOUAmount.zero issue mode
      = .ok ⟨.issue issue, 0, -100, false⟩ := by
  have h_nat : issue.native = false := h_not_xrp
  unfold STAmount.ofIOUAmount STAmount.canonicalize STAmount.iou
  simp only [STAmount.unchecked, STAmount.integral, Asset.integral, Issue.integral,
    h_nat, Bool.false_eq_true, if_false]
  rfl

/-- Convert the per-mode magnitude characterization into `RoundsToRepresentableAt`. -/
private lemma conclude (result value : STAmount) (s : ℤ) (mode : rounding_mode) (k : ℕ)
    (h_mv : value.mValue ≠ 0)
    (h_res_val : result.toRat
      = (if value.mIsNegative then (-1 : ℚ) else 1) * (k : ℚ) * 10 ^ s)
    (h_tn : mode = .to_nearest →
      (k : ℤ) = ⌊|value.toRat| / 10 ^ s⌋ ∨ (k : ℤ) = ⌈|value.toRat| / 10 ^ s⌉)
    (h_tz : mode = .towards_zero → (k : ℤ) = ⌊|value.toRat| / 10 ^ s⌋)
    (h_dn : mode = .downward → (k : ℤ) = if value.mIsNegative
      then ⌈|value.toRat| / 10 ^ s⌉ else ⌊|value.toRat| / 10 ^ s⌋)
    (h_up : mode = .upward → (k : ℤ) = if value.mIsNegative
      then ⌊|value.toRat| / 10 ^ s⌋ else ⌈|value.toRat| / 10 ^ s⌉) :
    RoundsToRepresentableAt result value.toRat s mode := by
  have h_mv_nat : value.mValue.toNat ≠ 0 := by
    intro h0
    exact h_mv (by rw [← UInt64.toNat_inj, h0]; rfl)
  rcases hn : value.mIsNegative with _ | _
  · -- nonnegative value
    have h_nn : 0 ≤ value.toRat := by
      rw [STAmount.toRat_of_nonneg value hn]
      positivity
    have h_abs : |value.toRat| = value.toRat := abs_of_nonneg h_nn
    rw [hn] at h_res_val
    simp only [Bool.false_eq_true, if_false, one_mul] at h_res_val
    rcases mode with _ | _ | _ | _
    · rcases h_tn rfl with h | h
      · refine Or.inl ?_
        show result.toRat = _
        rw [h_abs] at h
        rw [h_res_val, ← h]
        push_cast
        ring
      · refine Or.inr ?_
        show result.toRat = _
        rw [h_abs] at h
        rw [h_res_val, ← h]
        push_cast
        ring
    · show result.toRat = _
      have h := h_tz rfl
      rw [h_abs] at h
      rw [if_pos (show value.toRat ≥ 0 from h_nn), h_res_val, ← h]
      push_cast
      ring
    · show result.toRat = _
      have h := h_dn rfl
      rw [hn, if_neg Bool.false_ne_true, h_abs] at h
      rw [h_res_val, ← h]
      push_cast
      ring
    · show result.toRat = _
      have h := h_up rfl
      rw [hn, if_neg Bool.false_ne_true, h_abs] at h
      rw [h_res_val, ← h]
      push_cast
      ring
  · -- negative value
    have h_lt : value.toRat < 0 := by
      rw [STAmount.toRat_of_neg value hn]
      have h1 : (0 : ℚ) < (value.mValue.toNat : ℚ) := by
        have : 0 < value.mValue.toNat := Nat.pos_of_ne_zero h_mv_nat
        exact_mod_cast this
      have h2 : (0 : ℚ) < (10 : ℚ) ^ value.mOffset := zpow_pos (by norm_num) _
      nlinarith
    have h_abs : |value.toRat| = -value.toRat := abs_of_neg h_lt
    have h_conv_f : ⌊value.toRat / 10 ^ s⌋ = -⌈|value.toRat| / 10 ^ s⌉ := by
      rw [h_abs, neg_div, Int.ceil_neg]
      omega
    have h_conv_c : ⌈value.toRat / 10 ^ s⌉ = -⌊|value.toRat| / 10 ^ s⌋ := by
      rw [h_abs, neg_div, Int.floor_neg]
      omega
    rw [hn] at h_res_val
    simp only [if_true] at h_res_val
    rcases mode with _ | _ | _ | _
    · rcases h_tn rfl with h | h
      · refine Or.inr ?_
        show result.toRat = _
        rw [h_conv_c, h_res_val, ← h]
        push_cast
        ring
      · refine Or.inl ?_
        show result.toRat = _
        rw [h_conv_f, h_res_val, ← h]
        push_cast
        ring
    · show result.toRat = _
      have h := h_tz rfl
      rw [if_neg (show ¬ value.toRat ≥ 0 from not_le.mpr h_lt), h_conv_c, h_res_val, ← h]
      push_cast
      ring
    · show result.toRat = _
      have h := h_dn rfl
      rw [hn, if_pos rfl] at h
      rw [h_conv_f, h_res_val, ← h]
      push_cast
      ring
    · show result.toRat = _
      have h := h_up rfl
      rw [hn, if_pos rfl] at h
      rw [h_conv_c, h_res_val, ← h]
      push_cast
      ring

/-- A value already on the grid satisfies `RoundsToRepresentableAt` in every mode. -/
private lemma RoundsToRepresentableAt_grid_self (result : STAmount) (truth : ℚ) (s : ℤ)
    (mode : rounding_mode) (t : ℤ)
    (h_truth : truth = (t : ℚ) * 10 ^ s)
    (h_res : result.toRat = truth) :
    RoundsToRepresentableAt result truth s mode := by
  have h_pow_pos : (0 : ℚ) < (10 : ℚ) ^ s := zpow_pos (by norm_num) _
  have h_div : truth / 10 ^ s = (t : ℚ) := by
    rw [h_truth, mul_div_assoc, div_self (ne_of_gt h_pow_pos), mul_one]
  have h_floor : ⌊truth / 10 ^ s⌋ = t := by rw [h_div, Int.floor_intCast]
  have h_ceil : ⌈truth / 10 ^ s⌉ = t := by rw [h_div, Int.ceil_intCast]
  rcases mode with _ | _ | _ | _
  · exact Or.inl (by show result.toRat = _; rw [h_res, h_floor, h_truth])
  · show result.toRat = _
    rw [h_res, h_floor, h_ceil, ite_self, h_truth]
  · show result.toRat = _
    rw [h_res, h_floor, h_truth]
  · show result.toRat = _
    rw [h_res, h_ceil, h_truth]

set_option maxHeartbeats 3200000 in
-- assembles the full pipeline: guards, reference, sum stage, exact subtraction
/-- **`roundToScale` is correctly rounded onto the grid `10^s·ℤ`** (faithfully
for `.to_nearest`): the result is the floor/ceiling of the value at scale `s`
per the directed mode, or one of the two enclosing grid points for
`.to_nearest`. Hypotheses: a canonical nonzero IOU value and a scale in
`[-81, 80]` (below `-81` the 16-digit exponent clamp can flush nonzero
results; above `80` the reference construction overflows). -/
theorem STAmount.roundToScale_rounded_proof (value result : STAmount) (s : ℤ)
    (mode : rounding_mode)
    (hc : value.IOUCanonical)
    (h_s : (-81 : ℤ) ≤ s) (h_s_hi : s ≤ 80)
    (hok : STAmount.roundToScale value s mode = .ok result) :
    RoundsToRepresentableAt result value.toRat s mode := by
  obtain ⟨iss, ha⟩ : ∃ iss, value.mAsset = .issue iss := by
    rcases h : value.mAsset with iss | m
    · exact ⟨iss, rfl⟩
    · exfalso
      have h_iss := hc.iou_asset
      rw [h] at h_iss
      exact Bool.noConfusion h_iss
  have h_not_xrp : iss.isXRP = false := by
    have h_native := hc.not_xrp
    rw [ha] at h_native
    exact h_native
  have h_int : value.integral = false := by
    unfold STAmount.integral
    rw [ha]
    exact h_not_xrp
  have h_mv_ne : value.mValue ≠ 0 := by
    intro h0
    have h1 : value.mValue.toNat = 0 := by rw [h0]; rfl
    have := hc.mant_lo
    omega
  unfold STAmount.roundToScale at hok
  rw [if_neg (by rw [h_int]; exact Bool.false_ne_true),
      if_neg (by
        show ¬ value.isZero = true
        unfold STAmount.isZero
        rw [beq_eq_false_iff_ne.mpr h_mv_ne]
        exact Bool.false_ne_true)] at hok
  by_cases h_exp : value.exponent ≥ s
  · -- The value is already on the grid.
    rw [if_pos h_exp] at hok
    have h_result : result = value := (Except.ok.inj hok).symm
    have h_e_ge : s ≤ value.mOffset := h_exp
    have h_split : (10 : ℚ) ^ value.mOffset
        = (10 : ℚ) ^ ((value.mOffset - s).toNat) * (10 : ℚ) ^ s := by
      rw [show ((10 : ℚ) ^ ((value.mOffset - s).toNat) : ℚ)
            = (10 : ℚ) ^ (((value.mOffset - s).toNat : ℤ)) from
            (zpow_natCast 10 _).symm,
          ← zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0)]
      congr 1
      omega
    set t : ℤ := (if value.mIsNegative then -1 else 1) * (value.mValue.toNat : ℤ)
        * 10 ^ (value.mOffset - s).toNat with ht_def
    apply RoundsToRepresentableAt_grid_self result value.toRat s mode t
    · rw [STAmount.toRat_signed, ht_def, h_split]
      rcases hn : value.mIsNegative with _ | _ <;> simp <;> ring
    · rw [h_result]
  · -- Active branch: reference, sum, subtraction.
    rw [if_neg h_exp] at hok
    push_neg at h_exp
    have h_ev : value.mOffset < s := h_exp
    rw [show STAmount.checked value.asset kMinValue s value.negative mode
          = .ok ⟨value.mAsset, kMinValue, s, value.mIsNegative⟩ from
        STAmount.checked_reference value.mAsset s value.mIsNegative mode
          hc.iou_asset hc.not_xrp (by omega) h_s_hi] at hok
    simp only [] at hok
    set refA : STAmount := ⟨value.mAsset, kMinValue, s, value.mIsNegative⟩ with hrefA_def
    obtain ⟨sum, h_sum_ok, h_sub_ok⟩ : ∃ sum,
        STAmount.operator_add value refA mode = .ok sum ∧
        STAmount.operator_sub sum refA mode = .ok result := by
      match h1 : STAmount.operator_add value refA mode with
      | .error e => rw [h1] at hok; exact absurd hok (by intro h; cases h)
      | .ok sum =>
        rw [h1] at hok
        exact ⟨sum, rfl, hok⟩
    obtain ⟨k, hk_le, h_sum_asset, h_sum_mv, h_sum_off, h_sum_neg,
        h_tn, h_tz, h_dn, h_up⟩ :=
      STAmount.roundToScale_sum_spec value s mode iss ha h_not_xrp hc h_ev
        (by omega) h_s_hi sum h_sum_ok
    -- Stage B: subtract the reference exactly.
    unfold STAmount.operator_sub at h_sub_ok
    have h_negRef : refA.operator_neg = ⟨value.mAsset, kMinValue, s, !value.mIsNegative⟩ := by
      rw [STAmount.operator_neg_of_ne refA (by show kMinValue ≠ 0; decide)]
    rw [h_negRef] at h_sub_ok
    set negRef : STAmount := ⟨value.mAsset, kMinValue, s, !value.mIsNegative⟩
      with hnegRef_def
    have h_kMin_toNat : kMinValue.toNat = 10 ^ 15 := by decide
    have hc_sum : sum.IOUCanonical :=
      { iou_asset := by rw [h_sum_asset]; exact hc.iou_asset
        not_xrp := by rw [h_sum_asset]; exact hc.not_xrp
        mant_lo := by omega
        mant_hi := by omega
        exp_lo := by rw [h_sum_off]; omega
        exp_hi := by rw [h_sum_off]; exact h_s_hi }
    have hc_negRef : negRef.IOUCanonical :=
      { iou_asset := hc.iou_asset
        not_xrp := hc.not_xrp
        mant_lo := by show 10 ^ 15 ≤ kMinValue.toNat; omega
        mant_hi := by show kMinValue.toNat < 10 ^ 16; omega
        exp_lo := by show (-96 : ℤ) ≤ s; omega
        exp_hi := h_s_hi }
    rw [STAmount.operator_add_iou_unfold sum negRef mode iss
        (by rw [h_sum_asset]; exact ha) (by exact ha) h_not_xrp hc_sum hc_negRef]
      at h_sub_ok
    set s1n : Number := ⟨sum.mIsNegative, sum.mValue * 10 * 10 * 10, sum.mOffset - 3⟩
      with hs1n_def
    set s2n : Number := ⟨negRef.mIsNegative, negRef.mValue * 10 * 10 * 10,
      negRef.mOffset - 3⟩ with hs2n_def
    obtain ⟨n₂, h_add₂, hok₃⟩ : ∃ n₂, Number.operator_add s1n s2n mode = .ok n₂ ∧
        (match IOUAmount.ofNumber n₂ mode with
         | .error e => (Except.error e : Except String STAmount)
         | .ok sumI => STAmount.ofIOUAmount sumI iss mode) = .ok result := by
      match h1 : Number.operator_add s1n s2n mode with
      | .error e => rw [h1] at h_sub_ok; exact absurd h_sub_ok (by intro h; cases h)
      | .ok n₂ =>
        rw [h1] at h_sub_ok
        exact ⟨n₂, rfl, h_sub_ok⟩
    obtain ⟨sumI₂, h_of₂, h_pack₂⟩ : ∃ sumI₂, IOUAmount.ofNumber n₂ mode = .ok sumI₂ ∧
        STAmount.ofIOUAmount sumI₂ iss mode = .ok result := by
      match h1 : IOUAmount.ofNumber n₂ mode with
      | .error e => rw [h1] at hok₃; exact absurd hok₃ (by intro h; cases h)
      | .ok sumI₂ =>
        rw [h1] at hok₃
        exact ⟨sumI₂, rfl, hok₃⟩
    clear h_sub_ok hok₃ hok
    -- Operand records of the subtraction.
    have hs1_norm : s1n.isNormalized :=
      lift_isNormalized sum.mIsNegative sum.mValue sum.mOffset
        hc_sum.mant_lo hc_sum.mant_hi
        (by have := hc_sum.exp_lo; unfold minExponent; omega)
        (by have := hc_sum.exp_hi; unfold maxExponent; omega)
    have hs2_norm : s2n.isNormalized :=
      lift_isNormalized negRef.mIsNegative negRef.mValue negRef.mOffset
        hc_negRef.mant_lo hc_negRef.mant_hi
        (by have := hc_negRef.exp_lo; unfold minExponent; omega)
        (by have := hc_negRef.exp_hi; unfold maxExponent; omega)
    have hs1_val : s1n.toRat = sum.toRat := lift_toRat sum hc_sum.mant_hi
    have hs2_val : s2n.toRat = negRef.toRat := lift_toRat negRef hc_negRef.mant_hi
    have hminN : largeRange.min.toNat = 1000000000000000000 := largeRange_min_val
    have hs1_mant : s1n.mantissa_ ≠ 0 := by
      intro h0
      have h1 : s1n.mantissa_.toNat = 0 := by rw [h0]; rfl
      have hM : (sum.mValue * 10 * 10 * 10).toNat = sum.mValue.toNat * 1000 :=
        m_mul_thousand_no_overflow hc_sum.mant_hi
      have h2 : s1n.mantissa_.toNat = sum.mValue.toNat * 1000 := hM
      have := hc_sum.mant_lo
      omega
    have hs2_mant : s2n.mantissa_ ≠ 0 := by
      intro h0
      have h1 : s2n.mantissa_.toNat = 0 := by rw [h0]; rfl
      have h2 : s2n.mantissa_.toNat = 10 ^ 18 := by
        show (kMinValue * 10 * 10 * 10).toNat = 10 ^ 18
        decide
      omega
    have h_diff : s1n.negative_ ≠ s2n.negative_ := by
      show sum.mIsNegative ≠ negRef.mIsNegative
      rw [h_sum_neg]
      show value.mIsNegative ≠ !value.mIsNegative
      rcases hb : value.mIsNegative with _ | _ <;> simp
    -- The exact difference: τ = ±k·10^s.
    have h_pow_s_pos : (0 : ℚ) < (10 : ℚ) ^ s := zpow_pos (by norm_num) _
    have h_sum_val : sum.toRat
        = (if value.mIsNegative then (-1 : ℚ) else 1) * ((10 ^ 15 + k : ℕ) : ℚ) * 10 ^ s := by
      rw [STAmount.toRat_signed, h_sum_neg, h_sum_off, h_sum_mv]
    have h_negRef_val : negRef.toRat
        = (if value.mIsNegative then (1 : ℚ) else -1) * (10 ^ 15 : ℚ) * 10 ^ s := by
      rw [STAmount.toRat_signed]
      show (if !value.mIsNegative then (-1 : ℚ) else 1) * (kMinValue.toNat : ℚ) * 10 ^ s = _
      rw [h_kMin_toNat]
      rcases hb : value.mIsNegative with _ | _
      · simp only [Bool.not_false, if_true, Bool.false_eq_true, if_false]
        push_cast
        ring
      · simp only [Bool.not_true, Bool.false_eq_true, if_false, if_true]
        push_cast
        ring
    set τ : ℚ := s1n.toRat + s2n.toRat with hτ_def
    have hτ_val : τ = (if value.mIsNegative then (-1 : ℚ) else 1) * (k : ℚ) * 10 ^ s := by
      rw [hτ_def, hs1_val, hs2_val, h_sum_val, h_negRef_val]
      rcases hb : value.mIsNegative with _ | _
      · simp only [Bool.false_eq_true, if_false]
        push_cast
        ring
      · simp only [if_true]
        push_cast
        ring
    -- Split on exact cancellation, deriving the result value either way.
    have h_res_val : result.toRat = (if value.mIsNegative then (-1 : ℚ) else 1)
        * (k : ℚ) * 10 ^ s := by
      rcases Nat.eq_zero_or_pos k with hk0 | hk_pos
      · -- k = 0: exact cancellation, zero result.
        subst hk0
        have hτ0 : τ = 0 := by
          rw [hτ_val]
          push_cast
          ring
        have h_n₂_val : n₂.toRat = 0 := by
          have h := operator_add_exact_diff_sign s1n s2n n₂ mode hs1_norm hs2_norm
            hs1_mant hs2_mant h_diff Number.zero (Or.inl rfl)
            (by rw [Number.toRat_zero, ← hτ_def, hτ0]) h_add₂
          rw [h, ← hτ_def, hτ0]
        have h_n₂_mant : n₂.mantissa_ = 0 := Number.toRat_eq_zero_iff.mp h_n₂_val
        rw [IOUAmount.ofNumber_zero_mantissa n₂ mode h_n₂_mant] at h_of₂
        have h_sumI₂ : sumI₂ = IOUAmount.zero := Except.ok.inj h_of₂.symm
        rw [h_sumI₂, STAmount.ofIOUAmount_zero iss mode h_not_xrp] at h_pack₂
        have h_result : result = ⟨.issue iss, 0, -100, false⟩ :=
          Except.ok.inj h_pack₂.symm
        rw [h_result, STAmount.toRat_of_nonneg _ rfl]
        show ((0 : UInt64).toNat : ℚ) * 10 ^ (-100 : ℤ) = _
        rw [show (0 : UInt64).toNat = 0 from rfl]
        push_cast
        ring
      · -- k ≥ 1: the difference is an exactly representable grid point.
        obtain ⟨w₀, hw₀_norm, hw₀_neg, hw₀_mant, hw₀_val, hw₀_mod, hw₀_exp_lo,
            hw₀_exp_hi⟩ :=
          exists_normalized_of_int_mul_pow k s (by omega) (by omega)
            (by unfold minExponent; omega) (by unfold maxExponent; omega)
        set w : Number := if value.mIsNegative then { w₀ with negative_ := true } else w₀
          with hw_def
        have hw_norm : w.isNormalized := by
          rw [hw_def]
          rcases hb : value.mIsNegative with _ | _
          · simp only [Bool.false_eq_true, if_false]
            exact hw₀_norm
          · simp only [if_true]
            rcases hw₀_norm with hz | ⟨h1, h2, h3, h4, h5⟩
            · exfalso
              apply hw₀_mant
              rw [hz]
              rfl
            · exact Or.inr ⟨h1, h2, h3, h4, h5⟩
        have hw_mant : w.mantissa_ = w₀.mantissa_ := by
          rw [hw_def]
          rcases hb : value.mIsNegative with _ | _ <;> simp
        have hw_exp : w.exponent_ = w₀.exponent_ := by
          rw [hw_def]
          rcases hb : value.mIsNegative with _ | _ <;> simp
        have h_w_neg_val : w.negative_ = value.mIsNegative := by
          rw [hw_def]
          rcases hb : value.mIsNegative with _ | _
          · simp only [Bool.false_eq_true, if_false]
            exact hw₀_neg
          · simp only [if_true]
        have hw_val : w.toRat = τ := by
          rw [hτ_val, hw_def]
          rcases hb : value.mIsNegative with _ | _
          · simp only [Bool.false_eq_true, if_false, one_mul]
            exact hw₀_val
          · simp only [if_true]
            rw [Number.toRat_set_neg_true_of_nn w₀ hw₀_neg, hw₀_val]
            ring
        have h_n₂_val : n₂.toRat = τ := by
          have h := operator_add_exact_diff_sign s1n s2n n₂ mode hs1_norm hs2_norm
            hs1_mant hs2_mant h_diff w hw_norm (by rw [hw_val, hτ_def]) h_add₂
          rw [h, hτ_def]
        have hk_q : (0 : ℚ) < (k : ℚ) := by exact_mod_cast hk_pos
        have h_kpow_pos : (0 : ℚ) < (k : ℚ) * 10 ^ s := by positivity
        have hτ_ne : τ ≠ 0 := by
          rw [hτ_val]
          rcases hb : value.mIsNegative with _ | _
          · simp only [Bool.false_eq_true, if_false, one_mul]
            exact ne_of_gt h_kpow_pos
          · simp only [if_true]
            intro h
            have h1 : (k : ℚ) * 10 ^ s = 0 := by linarith
            exact ne_of_gt h_kpow_pos h1
        have h_n₂_mant : n₂.mantissa_ ≠ 0 := by
          intro h0
          apply hτ_ne
          rw [← h_n₂_val]
          exact Number.toRat_eq_zero_iff.mpr h0
        have h_not_zero₂ : ¬ s1n.operator_eq s2n.operator_neg := by
          intro h
          apply hτ_ne
          rw [hτ_def]
          exact add_truth_zero_of_eq_neg s1n s2n h
        have h_n₂_norm : n₂.isNormalized :=
          operator_add_result_isNormalized s1n s2n n₂ mode hs1_norm hs2_norm
            hs1_mant hs2_mant h_diff h_not_zero₂ h_add₂ h_n₂_mant
        have h_n₂_eq_w : n₂ = w :=
          h_n₂_norm.toRat_inj hw_norm (by rw [h_n₂_val, ← hw_val])
        -- 16-digit renormalization of the grid point: exact.
        have hw_bounds := hw_norm.mantissaBounds (by rw [hw_mant]; exact hw₀_mant)
        have hw_lo : 10 ^ 18 ≤ w.mantissa_.toNat := by
          have := UInt64.le_iff_toNat_le.mp hw_bounds.1
          rw [largeRange_min_val] at this
          omega
        have hw_hi : w.mantissa_.toNat < 10 ^ 19 := by
          have := UInt64.le_iff_toNat_le.mp hw_bounds.2
          rw [largeRange_max_val] at this
          omega
        have h_ntr := normalizeToRange_16_exact w mode hw_lo hw_hi
          (by rw [hw_mant]; exact hw₀_mod)
          (by rw [hw_exp]; have := hw₀_exp_lo; unfold minExponent; omega)
          (by rw [hw_exp]; have := hw₀_exp_hi; unfold maxExponent; omega)
        have h_of_explicit : IOUAmount.ofNumber n₂ mode
            = .ok ⟨if w.negative_ then -(w.mantissa_ / 10 / 10 / 10).toInt64
                   else (w.mantissa_ / 10 / 10 / 10).toInt64, w.exponent_ + 3⟩ := by
          rw [h_n₂_eq_w]
          unfold IOUAmount.ofNumber IOUAmount.fromNumber
          rw [h_ntr]
          simp only []
          rw [if_neg (by
              show ¬ w.exponent_ + 3 > cMaxOffset
              rw [hw_exp]
              unfold cMaxOffset
              omega),
            if_neg (by
              show ¬ w.exponent_ + 3 < cMinOffset
              rw [hw_exp]
              unfold cMinOffset
              omega)]
        have h_sumI₂_eq : sumI₂ = ⟨if w.negative_ then -(w.mantissa_ / 10 / 10 / 10).toInt64
            else (w.mantissa_ / 10 / 10 / 10).toInt64, w.exponent_ + 3⟩ :=
          Except.ok.inj (h_of₂.symm.trans h_of_explicit)
        -- Repack.
        have h_q3 : (w.mantissa_ / 10 / 10 / 10).toNat = w.mantissa_.toNat / 1000 :=
          m_div_thousand_toNat w.mantissa_
        have h_q3_lo : 10 ^ 15 ≤ (w.mantissa_ / 10 / 10 / 10).toNat := by
          rw [h_q3]
          omega
        have h_q3_hi : (w.mantissa_ / 10 / 10 / 10).toNat < 10 ^ 16 := by
          rw [h_q3]
          omega
        have h_q3_fit : (w.mantissa_ / 10 / 10 / 10).toNat < 2 ^ 63 := by omega
        have h_mant_toInt : (if w.negative_ then -(w.mantissa_ / 10 / 10 / 10).toInt64
            else (w.mantissa_ / 10 / 10 / 10).toInt64).toInt
            = (if w.negative_ then -1 else 1)
              * ((w.mantissa_ / 10 / 10 / 10).toNat : ℤ) :=
          signed_mantissa_toInt w.negative_ (w.mantissa_ / 10 / 10 / 10) h_q3_fit
        have h_pack_explicit : STAmount.ofIOUAmount
            ⟨if w.negative_ then -(w.mantissa_ / 10 / 10 / 10).toInt64
              else (w.mantissa_ / 10 / 10 / 10).toInt64, w.exponent_ + 3⟩ iss mode
            = .ok ⟨.issue iss, w.mantissa_ / 10 / 10 / 10, w.exponent_ + 3,
                   w.negative_⟩ := by
          have hr : IOUAmount.InRange16
              ⟨if w.negative_ then -(w.mantissa_ / 10 / 10 / 10).toInt64
                else (w.mantissa_ / 10 / 10 / 10).toInt64, w.exponent_ + 3⟩ :=
            { mant_lo := by
                show 10 ^ 15 ≤ (if w.negative_ then -(w.mantissa_ / 10 / 10 / 10).toInt64
                  else (w.mantissa_ / 10 / 10 / 10).toInt64).toInt.natAbs
                rw [h_mant_toInt]
                rcases hb : w.negative_ with _ | _ <;> simp <;> omega
              mant_hi := by
                show (if w.negative_ then -(w.mantissa_ / 10 / 10 / 10).toInt64
                  else (w.mantissa_ / 10 / 10 / 10).toInt64).toInt.natAbs < 10 ^ 16
                rw [h_mant_toInt]
                rcases hb : w.negative_ with _ | _ <;> simp <;> omega
              exp_lo := by
                show (-96 : ℤ) ≤ w.exponent_ + 3
                rw [hw_exp]
                omega
              exp_hi := by
                show w.exponent_ + 3 ≤ 80
                rw [hw_exp]
                omega }
          rw [STAmount.ofIOUAmount_canonical _ iss mode h_not_xrp hr]
          congr 1
          have h_abs_eq : ((if w.negative_ then -(w.mantissa_ / 10 / 10 / 10).toInt64
              else (w.mantissa_ / 10 / 10 / 10).toInt64).toInt.natAbs : ℕ).toUInt64
              = w.mantissa_ / 10 / 10 / 10 := by
            rw [← UInt64.toNat_inj]
            have h1 : (if w.negative_ then -(w.mantissa_ / 10 / 10 / 10).toInt64
                else (w.mantissa_ / 10 / 10 / 10).toInt64).toInt.natAbs
                = (w.mantissa_ / 10 / 10 / 10).toNat :=
              signed_mantissa_natAbs w.negative_ (w.mantissa_ / 10 / 10 / 10) h_q3_fit
            rw [h1, UInt64.toNat_ofNat_of_lt' (by rw [uint64_size_val]; omega)]
          have h_dec : decide ((if w.negative_ then -(w.mantissa_ / 10 / 10 / 10).toInt64
              else (w.mantissa_ / 10 / 10 / 10).toInt64) < 0) = w.negative_ :=
            signed_mantissa_decide_neg w.negative_ (w.mantissa_ / 10 / 10 / 10) h_q3_fit
              (by omega)
          rw [h_abs_eq, h_dec]
        have h_result : result = ⟨.issue iss, w.mantissa_ / 10 / 10 / 10,
            w.exponent_ + 3, w.negative_⟩ := by
          rw [h_sumI₂_eq] at h_pack₂
          exact Except.ok.inj (h_pack₂.symm.trans h_pack_explicit)
        -- The repacked value is exactly τ = ±k·10^s.
        have h_div_mul : (w.mantissa_.toNat / 1000 : ℕ) * 1000 = w.mantissa_.toNat := by
          have h_mod : w.mantissa_.toNat % 1000 = 0 := by
            rw [hw_mant]
            exact hw₀_mod
          omega
        have h_pow3 : (10 : ℚ) ^ (w.exponent_ + 3) = 10 ^ w.exponent_ * 1000 := by
          rw [zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0)]
          norm_num
        have h_cast : ((w.mantissa_.toNat / 1000 : ℕ) : ℚ) * 1000
            = (w.mantissa_.toNat : ℚ) := by
          exact_mod_cast h_div_mul
        have h_split : ((w.mantissa_ / 10 / 10 / 10).toNat : ℚ) * 10 ^ (w.exponent_ + 3)
            = (w.mantissa_.toNat : ℚ) * 10 ^ w.exponent_ := by
          rw [h_q3, h_pow3]
          calc ((w.mantissa_.toNat / 1000 : ℕ) : ℚ) * ((10 : ℚ) ^ w.exponent_ * 1000)
              = (((w.mantissa_.toNat / 1000 : ℕ) : ℚ) * 1000) * (10 : ℚ) ^ w.exponent_ := by
                ring
            _ = (w.mantissa_.toNat : ℚ) * 10 ^ w.exponent_ := by rw [h_cast]
        have h_w_abs : (w.mantissa_.toNat : ℚ) * 10 ^ w.exponent_ = (k : ℚ) * 10 ^ s := by
          have h1 : |w.toRat| = (w.mantissa_.toNat : ℚ) * 10 ^ w.exponent_ :=
            abs_toRat_eq w
          have h2 : |w.toRat| = (k : ℚ) * 10 ^ s := by
            rw [hw_val, hτ_val]
            rcases hb : value.mIsNegative with _ | _
            · simp only [Bool.false_eq_true, if_false, one_mul]
              exact abs_of_nonneg (le_of_lt h_kpow_pos)
            · simp only [if_true]
              rw [show (-1 : ℚ) * (k : ℚ) * 10 ^ s = -((k : ℚ) * 10 ^ s) from by ring,
                  abs_neg]
              exact abs_of_nonneg (le_of_lt h_kpow_pos)
          rw [← h1, h2]
        rw [h_result, STAmount.toRat_signed]
        show (if w.negative_ then (-1 : ℚ) else 1)
            * ((w.mantissa_ / 10 / 10 / 10).toNat : ℚ) * 10 ^ (w.exponent_ + 3) = _
        rw [mul_assoc, h_split, h_w_abs, ← mul_assoc, h_w_neg_val]
    exact conclude result value s mode k h_mv_ne h_res_val h_tn h_tz h_dn h_up

end XRPL.Model.Protocol
