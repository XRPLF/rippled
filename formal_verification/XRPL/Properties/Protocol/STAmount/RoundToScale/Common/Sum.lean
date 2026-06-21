import Mathlib.Tactic

import XRPL.Properties.Protocol.STAmount.Common.RoundToScaleHelpers
import XRPL.Properties.Protocol.Number.Common.Closest.OpExact
import XRPL.Properties.Protocol.Number.Common.Closest.DecadeBridge


namespace XRPL.Model.Protocol

/-! # Stage A of the `roundToScale` discrete theorem

Characterizes `value + reference`: the sum lands at `(10^15 + k)·10^s` with
`k` the per-mode rounding of `|value|/10^s`. -/

/-- The `×1000` lift of a 16-digit record is normalized. -/
lemma lift_isNormalized (neg : Bool) (m : UInt64) (e : Int)
    (h_lo : 10 ^ 15 ≤ m.toNat) (h_hi : m.toNat < 10 ^ 16)
    (he_lo : minExponent ≤ e - 3) (he_hi : e - 3 ≤ maxExponent) :
    (⟨neg, m * 10 * 10 * 10, e - 3⟩ : Number).isNormalized := by
  have hM : (m * 10 * 10 * 10).toNat = m.toNat * 1000 := m_mul_thousand_no_overflow h_hi
  right
  refine ⟨?_, ?_, Or.inr ?_, he_lo, he_hi⟩
  · show largeRange.min ≤ _
    rw [UInt64.le_iff_toNat_le]
    show largeRange.min.toNat ≤ (m * 10 * 10 * 10).toNat
    rw [largeRange_min_val, hM]
    omega
  · show _ ≤ largeRange.max
    rw [UInt64.le_iff_toNat_le]
    show (m * 10 * 10 * 10).toNat ≤ largeRange.max.toNat
    rw [largeRange_max_val, hM]
    omega
  · show (m * 10 * 10 * 10).toNat % 10 = 0
    rw [hM]
    omega

/-- The `×1000` lift preserves the value. -/
lemma lift_toRat (s : STAmount) (h_hi : s.mValue.toNat < 10 ^ 16) :
    (⟨s.mIsNegative, s.mValue * 10 * 10 * 10, s.mOffset - 3⟩ : Number).toRat = s.toRat := by
  have hM : (s.mValue * 10 * 10 * 10).toNat = s.mValue.toNat * 1000 :=
    m_mul_thousand_no_overflow h_hi
  have h_pow : (1000 : ℚ) * (10 : ℚ) ^ (s.mOffset - 3) = (10 : ℚ) ^ s.mOffset := by
    rw [show s.mOffset = (s.mOffset - 3) + 3 from by ring,
        zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0)]
    ring_nf
  rw [STAmount.toRat_signed]
  rcases hneg : s.mIsNegative with _ | _
  · rw [Number.toRat_of_nonneg _ rfl, hM]
    push_cast
    rw [show (s.mValue.toNat : ℚ) * 1000 * (10 : ℚ) ^ (s.mOffset - 3)
        = (s.mValue.toNat : ℚ) * ((1000 : ℚ) * (10 : ℚ) ^ (s.mOffset - 3)) from by ring,
        h_pow]
    norm_num
  · rw [Number.toRat_of_neg _ rfl, hM]
    push_cast
    rw [show (s.mValue.toNat : ℚ) * 1000 * (10 : ℚ) ^ (s.mOffset - 3)
        = (s.mValue.toNat : ℚ) * ((1000 : ℚ) * (10 : ℚ) ^ (s.mOffset - 3)) from by ring,
        h_pow, if_pos rfl]
    ring

/-- Nat floor-division by 1000, as an integer floor. -/
private lemma nat_floorDiv_cast (a : ℕ) :
    ((a / 1000 : ℕ) : ℤ) = ⌊(a : ℚ) / 1000⌋ := by
  have h := Rat.floor_intCast_div_natCast (a : ℤ) 1000
  push_cast at h ⊢
  omega

/-- Nat ceiling-division by 1000 (`/1000` plus a bump on a nonzero remainder),
as an integer ceiling. -/
private lemma nat_ceilDiv_cast (a : ℕ) :
    ((a / 1000 + if a % 1000 ≠ 0 then 1 else 0 : ℕ) : ℤ) = ⌈(a : ℚ) / 1000⌉ := by
  have h := Rat.ceil_intCast_div_natCast (a : ℤ) 1000
  push_cast at h ⊢
  by_cases hmod : a % 1000 = 0
  · rw [if_neg (by exact fun hh => hh hmod)]
    omega
  · rw [if_pos hmod]
    omega

set_option maxHeartbeats 3200000 in
-- single pass through the full two-stage rounding pipeline, 4 modes × 2 signs
/-- Stage A: the sum of a canonical value (with `mOffset < s`) and the
reference `±10^15·10^s` is the record `(10^15 + k)·10^s` with `k` the
per-mode rounding of `|value|/10^s`. -/
theorem STAmount.roundToScale_sum_spec (value : STAmount) (s : ℤ) (mode : rounding_mode)
    (iss : Issue)
    (h_asset : value.mAsset = .issue iss) (h_not_xrp : iss.isXRP = false)
    (hc : value.IOUCanonical) (h_ev : value.mOffset < s)
    (h_s : (-96 : ℤ) ≤ s) (h_s_hi : s ≤ 80)
    (sum : STAmount)
    (hok : STAmount.operator_add value ⟨value.mAsset, kMinValue, s, value.mIsNegative⟩ mode
      = .ok sum) :
    ∃ k : ℕ, k ≤ 10 ^ 15 ∧
      sum.mAsset = value.mAsset ∧
      sum.mValue.toNat = 10 ^ 15 + k ∧
      sum.mOffset = s ∧
      sum.mIsNegative = value.mIsNegative ∧
      (mode = .to_nearest →
        (k : ℤ) = ⌊|value.toRat| / 10 ^ s⌋ ∨ (k : ℤ) = ⌈|value.toRat| / 10 ^ s⌉) ∧
      (mode = .towards_zero → (k : ℤ) = ⌊|value.toRat| / 10 ^ s⌋) ∧
      (mode = .downward → (k : ℤ) = if value.mIsNegative then ⌈|value.toRat| / 10 ^ s⌉
        else ⌊|value.toRat| / 10 ^ s⌋) ∧
      (mode = .upward → (k : ℤ) = if value.mIsNegative then ⌊|value.toRat| / 10 ^ s⌋
        else ⌈|value.toRat| / 10 ^ s⌉) := by
  set refA : STAmount := ⟨value.mAsset, kMinValue, s, value.mIsNegative⟩ with hrefA_def
  have h_kMin_toNat : kMinValue.toNat = 10 ^ 15 := by decide
  have hcr : refA.IOUCanonical :=
    { iou_asset := by rw [hrefA_def]; show value.mAsset.holdsIssue = true; exact hc.iou_asset
      not_xrp := by rw [hrefA_def]; show value.mAsset.isNative = false; exact hc.not_xrp
      mant_lo := by show 10 ^ 15 ≤ kMinValue.toNat; rw [h_kMin_toNat]
      mant_hi := by show kMinValue.toNat < 10 ^ 16; rw [h_kMin_toNat]; norm_num
      exp_lo := h_s
      exp_hi := h_s_hi }
  -- Reduce the STAmount add to the Number pipeline.
  rw [STAmount.operator_add_iou_unfold value refA mode iss h_asset
      (by rw [hrefA_def]; exact h_asset) h_not_xrp hc hcr] at hok
  set neg : Bool := value.mIsNegative with hneg_def
  set v1n : Number := ⟨value.mIsNegative, value.mValue * 10 * 10 * 10, value.mOffset - 3⟩
    with hv1n_def
  set v2n : Number := ⟨refA.mIsNegative, refA.mValue * 10 * 10 * 10, refA.mOffset - 3⟩
    with hv2n_def
  -- Destructure the chain.
  obtain ⟨n₁, h_add, hok₂⟩ : ∃ n₁, Number.operator_add v1n v2n mode = .ok n₁ ∧
      (match IOUAmount.ofNumber n₁ mode with
       | .error e => (Except.error e : Except String STAmount)
       | .ok sumI => STAmount.ofIOUAmount sumI iss mode) = .ok sum := by
    match h_add : Number.operator_add v1n v2n mode with
    | .error e => rw [h_add] at hok; exact absurd hok (by intro h; cases h)
    | .ok n₁ =>
      rw [h_add] at hok
      exact ⟨n₁, rfl, hok⟩
  obtain ⟨sumI, h_of, h_pack⟩ : ∃ sumI, IOUAmount.ofNumber n₁ mode = .ok sumI ∧
      STAmount.ofIOUAmount sumI iss mode = .ok sum := by
    match h_of : IOUAmount.ofNumber n₁ mode with
    | .error e => rw [h_of] at hok₂; exact absurd hok₂ (by intro h; cases h)
    | .ok sumI =>
      rw [h_of] at hok₂
      exact ⟨sumI, rfl, hok₂⟩
  clear hok hok₂
  -- Operand records: normalized, valued, nonzero.
  have hv1_norm : v1n.isNormalized :=
    lift_isNormalized _ _ _ hc.mant_lo hc.mant_hi
      (by have := hc.exp_lo; unfold minExponent; omega)
      (by have := hc.exp_hi; unfold maxExponent; omega)
  have hv2_norm : v2n.isNormalized :=
    lift_isNormalized _ _ _ hcr.mant_lo hcr.mant_hi
      (by have := hcr.exp_lo; unfold minExponent; omega)
      (by have := hcr.exp_hi; unfold maxExponent; omega)
  have hv1_val : v1n.toRat = value.toRat := lift_toRat value hc.mant_hi
  have hv2_val : v2n.toRat = refA.toRat := lift_toRat refA hcr.mant_hi
  have hv1_mant : v1n.mantissa_ ≠ 0 := by
    intro h0
    have h1 : v1n.mantissa_.toNat = 0 := by rw [h0]; rfl
    have h2 : 10 ^ 15 ≤ value.mValue.toNat := hc.mant_lo
    have hM : (value.mValue * 10 * 10 * 10).toNat = value.mValue.toNat * 1000 :=
      m_mul_thousand_no_overflow hc.mant_hi
    have : v1n.mantissa_.toNat = value.mValue.toNat * 1000 := hM
    omega
  have hv2_mant : v2n.mantissa_ ≠ 0 := by
    intro h0
    have h1 : v2n.mantissa_.toNat = 0 := by rw [h0]; rfl
    have h2 : (kMinValue * 10 * 10 * 10).toNat = 10 ^ 18 := by decide
    have : v2n.mantissa_.toNat = 10 ^ 18 := h2
    omega
  have h_same_sign : v1n.negative_ = v2n.negative_ := rfl
  have h_not_zero : ¬ v1n.operator_eq v2n.operator_neg := by
    intro h
    have h_neg_neg : v2n.operator_neg.negative_ = !v2n.negative_ := by
      unfold Number.operator_neg
      rw [if_neg (by
        intro hb
        exact hv2_mant (by exact_mod_cast beq_iff_eq.mp hb))]
    unfold Number.operator_eq at h
    simp only [Bool.and_eq_true, beq_iff_eq] at h
    have h_signs := h.1.1
    rw [h_neg_neg] at h_signs
    have h_eq : v1n.negative_ = v2n.negative_ := h_same_sign
    rw [h_eq] at h_signs
    rcases hb : v2n.negative_ with _ | _ <;> rw [hb] at h_signs <;> simp at h_signs
  -- The exact sum and its magnitude decomposition.
  set V : ℚ := |value.toRat| with hV_def
  set E : ℤ := s - 3 with hE_def
  have h_pow_E_pos : (0 : ℚ) < (10 : ℚ) ^ E := zpow_pos (by norm_num) _
  have h_pow_s_pos : (0 : ℚ) < (10 : ℚ) ^ s := zpow_pos (by norm_num) _
  have h_pow_shift : (10 : ℚ) ^ s = 1000 * (10 : ℚ) ^ E := by
    rw [hE_def, show s = (s - 3) + 3 from by ring,
        zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0)]
    ring_nf
  have h_refA_val : refA.toRat = (if neg then (-1 : ℚ) else 1) * (10 ^ 15 * 10 ^ s) := by
    rw [STAmount.toRat_signed]
    show (if neg then (-1 : ℚ) else 1) * (kMinValue.toNat : ℚ) * 10 ^ s = _
    rw [h_kMin_toNat]
    push_cast
    ring
  have h_value_signed : value.toRat = (if neg then (-1 : ℚ) else 1) * V := by
    rw [hV_def]
    rcases hn : neg with _ | _
    · have hn' : value.mIsNegative = false := hn
      rw [abs_of_nonneg (by
        rw [STAmount.toRat_of_nonneg value hn']
        positivity)]
      norm_num
    · have hn' : value.mIsNegative = true := hn
      rw [abs_of_nonpos (by
        rw [STAmount.toRat_of_neg value hn']
        have : (0 : ℚ) ≤ (value.mValue.toNat : ℚ) * 10 ^ value.mOffset := by positivity
        linarith)]
      norm_num
  have hV_pos : 0 < V := by
    rw [hV_def]
    have h_ne : value.toRat ≠ 0 := by
      intro h0
      have h1 : (value.mValue.toNat : ℚ) * 10 ^ value.mOffset = 0 := by
        rw [← STAmount.abs_toRat, h0, abs_zero]
      have h_pow : (0 : ℚ) < (10 : ℚ) ^ value.mOffset := zpow_pos (by norm_num) _
      have h2 : (value.mValue.toNat : ℚ) = 0 := by
        rcases mul_eq_zero.mp h1 with h | h
        · exact h
        · exact absurd h (ne_of_gt h_pow)
      have h3 : value.mValue.toNat = 0 := by exact_mod_cast h2
      have := hc.mant_lo
      omega
    exact abs_pos.mpr h_ne
  have hV_lt : V < 10 ^ 15 * 10 ^ s := by
    rw [hV_def, STAmount.abs_toRat]
    have h_m : (value.mValue.toNat : ℚ) < 10 ^ 16 := by
      have := hc.mant_hi
      exact_mod_cast this
    have h_e : value.mOffset ≤ s - 1 := by omega
    have h_pow_le : (10 : ℚ) ^ value.mOffset ≤ (10 : ℚ) ^ (s - 1) :=
      zpow_le_zpow_right₀ (by norm_num) h_e
    have h_pow_eq : (10 : ℚ) ^ 16 * (10 : ℚ) ^ (s - 1) = 10 ^ 15 * 10 ^ s := by
      rw [show s = (s - 1) + 1 from by ring]
      rw [zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0) (s - 1) 1]
      ring_nf
    calc (value.mValue.toNat : ℚ) * 10 ^ value.mOffset
        < 10 ^ 16 * 10 ^ value.mOffset := by
          have := zpow_pos (show (0:ℚ) < 10 by norm_num) value.mOffset
          nlinarith
      _ ≤ 10 ^ 16 * (10 : ℚ) ^ (s - 1) := by
          have : (0 : ℚ) ≤ (10 : ℚ) ^ 16 := by positivity
          nlinarith
      _ = 10 ^ 15 * 10 ^ s := h_pow_eq
  set σ : ℚ := v1n.toRat + v2n.toRat with hσ_def
  have hσ_signed : σ = (if neg then (-1 : ℚ) else 1) * (V + 10 ^ 18 * 10 ^ E) := by
    rw [hσ_def, hv1_val, hv2_val, h_refA_val, h_value_signed]
    have h1518 : (10 : ℚ) ^ 15 * (10 : ℚ) ^ s = 10 ^ 18 * (10 : ℚ) ^ E := by
      rw [h_pow_shift]
      ring
    rw [← h1518]
    ring
  set A : ℚ := V + 10 ^ 18 * 10 ^ E with hA_def
  have hA_lo : 10 ^ 18 * (10 : ℚ) ^ E ≤ A := by
    rw [hA_def]
    linarith
  have hA_hi : A < 2 * 10 ^ 18 * (10 : ℚ) ^ E := by
    rw [hA_def]
    have h1518 : (10 : ℚ) ^ 15 * (10 : ℚ) ^ s = 10 ^ 18 * (10 : ℚ) ^ E := by
      rw [h_pow_shift]
      ring
    rw [← h1518] at *
    nlinarith [hV_lt]
  have hE_lo : minExponent ≤ E := by rw [hE_def]; unfold minExponent; omega
  have hE_hi : E ≤ maxExponent := by rw [hE_def]; unfold maxExponent; omega
  have hA_pos : 0 < A := by
    rw [hA_def]
    positivity
  have hσ_abs : |σ| = A := by
    rw [hσ_signed]
    rcases hn : neg with _ | _
    · simp only [Bool.false_eq_true, if_false, one_mul]
      exact abs_of_pos hA_pos
    · simp only [if_true]
      rw [show (-1 : ℚ) * A = -A from by ring, abs_neg]
      exact abs_of_pos hA_pos
  set Fσ : ℤ := ⌊A / 10 ^ E⌋ with hFσ_def
  set Cσ : ℤ := ⌈A / 10 ^ E⌉ with hCσ_def
  have hFσ_lo : (10 ^ 18 : ℤ) ≤ Fσ := by
    apply Int.le_floor.mpr
    rw [le_div_iff₀ h_pow_E_pos]
    push_cast
    exact hA_lo
  have hCσ_hi : Cσ ≤ 2 * 10 ^ 18 := by
    apply Int.ceil_le.mpr
    rw [div_le_iff₀ h_pow_E_pos]
    push_cast
    linarith
  have hFC : Fσ ≤ Cσ := Int.floor_le_ceil _
  have hCF : Cσ ≤ Fσ + 1 := Int.ceil_le_floor_add_one _
  -- n₁'s mantissa is nonzero and the record is normalized (mode-cased facts).
  have h_basics : n₁.mantissa_ ≠ 0 ∧ n₁.isNormalized := by
    rcases mode with _ | _ | _ | _
    · obtain ⟨_, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, h_ne, h_norm, _⟩ :=
        operator_add_algorithmic_facts_same_sign_to_nearest v1n v2n n₁ hv1_norm hv2_norm
          hv1_mant hv2_mant h_same_sign h_not_zero h_add
      exact ⟨h_ne, h_norm⟩
    · obtain ⟨_, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, h_ne, h_norm, _⟩ :=
        operator_add_algorithmic_facts_same_sign_towards_zero v1n v2n n₁ hv1_norm hv2_norm
          hv1_mant hv2_mant h_same_sign h_not_zero h_add
      exact ⟨h_ne, h_norm⟩
    · obtain ⟨_, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, h_norm, _, h_ne⟩ :=
        operator_add_algorithmic_facts_same_sign_downward v1n v2n n₁ hv1_norm hv2_norm
          hv1_mant hv2_mant h_same_sign h_not_zero h_add
      exact ⟨h_ne, h_norm⟩
    · obtain ⟨_, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, h_norm, _, h_ne⟩ :=
        operator_add_algorithmic_facts_same_sign_upward v1n v2n n₁ hv1_norm hv2_norm
          hv1_mant hv2_mant h_same_sign h_not_zero h_add
      exact ⟨h_ne, h_norm⟩
  obtain ⟨h_n₁_ne, h_n₁_norm⟩ := h_basics
  -- n₁'s value: floor/ceil of σ on the 10^E grid, per mode and sign.
  have hM_spec : ∃ M₁ : ℤ,
      n₁.toRat = (if neg then (-1 : ℚ) else 1) * (M₁ : ℚ) * 10 ^ E ∧
      (mode = .to_nearest → M₁ = Fσ ∨ M₁ = Cσ) ∧
      (mode = .towards_zero → M₁ = Fσ) ∧
      (mode = .downward → M₁ = if neg then Cσ else Fσ) ∧
      (mode = .upward → M₁ = if neg then Fσ else Cσ) := by
    have h_dec_lo : (10 : ℚ) ^ (18 : ℕ) * 10 ^ E ≤ A := by
      calc (10 : ℚ) ^ (18 : ℕ) * 10 ^ E = 10 ^ 18 * (10 : ℚ) ^ E := by norm_num
        _ ≤ A := hA_lo
    have h_dec_hi : A < 2 * 10 ^ (18 : ℕ) * (10 : ℚ) ^ E := by
      calc A < 2 * 10 ^ 18 * (10 : ℚ) ^ E := hA_hi
        _ = 2 * 10 ^ (18 : ℕ) * (10 : ℚ) ^ E := by norm_num
    rcases hn : neg with _ | _
    · -- positive σ
      have hσ_pos_eq : σ = A := by
        rw [hσ_signed, hn]
        norm_num
      have h_floor_val : ∀ n', Number.lower σ = some n' →
          n'.toRat = (Fσ : ℚ) * 10 ^ E := by
        intro n' hn'
        obtain ⟨n'', hn''_eq, hn''_val⟩ :=
          Number.lower_eq_floor_in_decade σ E hE_lo hE_hi
            (by rw [hσ_pos_eq]; exact h_dec_lo) (by rw [hσ_pos_eq]; exact h_dec_hi)
        have : n' = n'' := Option.some.inj (hn'.symm.trans hn''_eq)
        rw [this, hn''_val, hσ_pos_eq, hFσ_def]
      have h_ceil_val : ∀ n', Number.upper σ = some n' →
          n'.toRat = (Cσ : ℚ) * 10 ^ E := by
        intro n' hn'
        obtain ⟨n'', hn''_eq, hn''_val⟩ :=
          Number.upper_eq_ceil_in_decade σ E hE_lo hE_hi
            (by rw [hσ_pos_eq]; exact h_dec_lo) (by rw [hσ_pos_eq]; exact h_dec_hi)
        have : n' = n'' := Option.some.inj (hn'.symm.trans hn''_eq)
        rw [this, hn''_val, hσ_pos_eq, hCσ_def]
      rcases mode with _ | _ | _ | _
      · have h := operator_add_rounded_to_nearest v1n v2n n₁ hv1_norm hv2_norm h_add
        rw [← hσ_def] at h
        rcases h with ⟨n', hn'_eq, hn'_val⟩ | ⟨n', hn'_eq, hn'_val⟩
        · exact ⟨Fσ, by rw [hn'_val, h_floor_val n' hn'_eq]; norm_num,
            fun _ => Or.inl rfl, fun h => rounding_mode.noConfusion h,
            fun h => rounding_mode.noConfusion h, fun h => rounding_mode.noConfusion h⟩
        · exact ⟨Cσ, by rw [hn'_val, h_ceil_val n' hn'_eq]; norm_num,
            fun _ => Or.inr rfl, fun h => rounding_mode.noConfusion h,
            fun h => rounding_mode.noConfusion h, fun h => rounding_mode.noConfusion h⟩
      · have h := operator_add_rounded_towards_zero v1n v2n n₁ hv1_norm hv2_norm h_add
        rw [← hσ_def] at h
        obtain ⟨n', hn'_eq, hn'_val⟩ := h
        rw [if_pos (by rw [hσ_pos_eq]; exact le_of_lt hA_pos : σ ≥ 0)] at hn'_eq
        exact ⟨Fσ, by rw [hn'_val, h_floor_val n' hn'_eq]; norm_num,
          fun h => rounding_mode.noConfusion h, fun _ => rfl,
          fun h => rounding_mode.noConfusion h, fun h => rounding_mode.noConfusion h⟩
      · have h := operator_add_rounded_downward v1n v2n n₁ hv1_norm hv2_norm h_add h_n₁_ne
        rw [← hσ_def] at h
        obtain ⟨n', hn'_eq, hn'_val⟩ := h
        exact ⟨Fσ, by rw [hn'_val, h_floor_val n' hn'_eq]; norm_num,
          fun h => rounding_mode.noConfusion h, fun h => rounding_mode.noConfusion h,
          fun _ => by rw [if_neg (by exact Bool.noConfusion)],
          fun h => rounding_mode.noConfusion h⟩
      · have h := operator_add_rounded_upward v1n v2n n₁ hv1_norm hv2_norm h_add h_n₁_ne
        rw [← hσ_def] at h
        obtain ⟨n', hn'_eq, hn'_val⟩ := h
        exact ⟨Cσ, by rw [hn'_val, h_ceil_val n' hn'_eq]; norm_num,
          fun h => rounding_mode.noConfusion h, fun h => rounding_mode.noConfusion h,
          fun h => rounding_mode.noConfusion h,
          fun _ => by rw [if_neg (by exact Bool.noConfusion)]⟩
    · -- negative σ
      have hσ_neg_eq : -σ = A := by
        rw [hσ_signed, hn, if_pos rfl]
        ring
      have h_floor_val : ∀ n', Number.lower σ = some n' →
          n'.toRat = -((Cσ : ℚ) * 10 ^ E) := by
        intro n' hn'
        obtain ⟨n'', hn''_eq, hn''_val⟩ :=
          Number.lower_eq_floor_in_decade_neg σ E hE_lo hE_hi
            (by rw [hσ_neg_eq]; exact h_dec_lo) (by rw [hσ_neg_eq]; exact h_dec_hi)
        have h_eq : n' = n'' := Option.some.inj (hn'.symm.trans hn''_eq)
        rw [h_eq, hn''_val]
        have h_flo : ⌊σ / 10 ^ E⌋ = -Cσ := by
          rw [hCσ_def, ← hσ_neg_eq,
              show -σ / 10 ^ E = -(σ / 10 ^ E) from by ring, Int.ceil_neg]
          omega
        rw [h_flo]
        push_cast
        ring
      have h_ceil_val : ∀ n', Number.upper σ = some n' →
          n'.toRat = -((Fσ : ℚ) * 10 ^ E) := by
        intro n' hn'
        obtain ⟨n'', hn''_eq, hn''_val⟩ :=
          Number.upper_eq_ceil_in_decade_neg σ E hE_lo hE_hi
            (by rw [hσ_neg_eq]; exact h_dec_lo) (by rw [hσ_neg_eq]; exact h_dec_hi)
        have h_eq : n' = n'' := Option.some.inj (hn'.symm.trans hn''_eq)
        rw [h_eq, hn''_val]
        have h_cei : ⌈σ / 10 ^ E⌉ = -Fσ := by
          rw [hFσ_def, ← hσ_neg_eq,
              show -σ / 10 ^ E = -(σ / 10 ^ E) from by ring, Int.floor_neg]
          omega
        rw [h_cei]
        push_cast
        ring
      have hσ_lt : σ < 0 := by
        have : -σ = A := hσ_neg_eq
        linarith
      rcases mode with _ | _ | _ | _
      · have h := operator_add_rounded_to_nearest v1n v2n n₁ hv1_norm hv2_norm h_add
        rw [← hσ_def] at h
        rcases h with ⟨n', hn'_eq, hn'_val⟩ | ⟨n', hn'_eq, hn'_val⟩
        · exact ⟨Cσ, by rw [hn'_val, h_floor_val n' hn'_eq, if_pos rfl]; ring,
            fun _ => Or.inr rfl, fun h => rounding_mode.noConfusion h,
            fun h => rounding_mode.noConfusion h, fun h => rounding_mode.noConfusion h⟩
        · exact ⟨Fσ, by rw [hn'_val, h_ceil_val n' hn'_eq, if_pos rfl]; ring,
            fun _ => Or.inl rfl, fun h => rounding_mode.noConfusion h,
            fun h => rounding_mode.noConfusion h, fun h => rounding_mode.noConfusion h⟩
      · have h := operator_add_rounded_towards_zero v1n v2n n₁ hv1_norm hv2_norm h_add
        rw [← hσ_def] at h
        obtain ⟨n', hn'_eq, hn'_val⟩ := h
        rw [if_neg (not_le.mpr hσ_lt)] at hn'_eq
        exact ⟨Fσ, by rw [hn'_val, h_ceil_val n' hn'_eq, if_pos rfl]; ring,
          fun h => rounding_mode.noConfusion h, fun _ => rfl,
          fun h => rounding_mode.noConfusion h, fun h => rounding_mode.noConfusion h⟩
      · have h := operator_add_rounded_downward v1n v2n n₁ hv1_norm hv2_norm h_add h_n₁_ne
        rw [← hσ_def] at h
        obtain ⟨n', hn'_eq, hn'_val⟩ := h
        exact ⟨Cσ, by rw [hn'_val, h_floor_val n' hn'_eq, if_pos rfl]; ring,
          fun h => rounding_mode.noConfusion h, fun h => rounding_mode.noConfusion h,
          fun _ => by rw [if_pos rfl],
          fun h => rounding_mode.noConfusion h⟩
      · have h := operator_add_rounded_upward v1n v2n n₁ hv1_norm hv2_norm h_add h_n₁_ne
        rw [← hσ_def] at h
        obtain ⟨n', hn'_eq, hn'_val⟩ := h
        exact ⟨Fσ, by rw [hn'_val, h_ceil_val n' hn'_eq, if_pos rfl]; ring,
          fun h => rounding_mode.noConfusion h, fun h => rounding_mode.noConfusion h,
          fun h => rounding_mode.noConfusion h,
          fun _ => by rw [if_pos rfl]⟩
  obtain ⟨M₁, hM_val, hM_tn, hM_tz, hM_dn, hM_up⟩ := hM_spec
  -- Bounds on M₁ (uniform across modes).
  have hM_lo : (10 ^ 18 : ℤ) ≤ M₁ := by
    rcases mode with _ | _ | _ | _
    · rcases hM_tn rfl with h | h <;> omega
    · have := hM_tz rfl; omega
    · have := hM_dn rfl
      rcases hn : neg with _ | _ <;> rw [hn] at this <;> simp at this <;> omega
    · have := hM_up rfl
      rcases hn : neg with _ | _ <;> rw [hn] at this <;> simp at this <;> omega
  have hM_hi : M₁ ≤ 2 * 10 ^ 18 := by
    rcases mode with _ | _ | _ | _
    · rcases hM_tn rfl with h | h <;> omega
    · have := hM_tz rfl; omega
    · have := hM_dn rfl
      rcases hn : neg with _ | _ <;> rw [hn] at this <;> simp at this <;> omega
    · have := hM_up rfl
      rcases hn : neg with _ | _ <;> rw [hn] at this <;> simp at this <;> omega
  -- Pin n₁'s record.
  set M₁ℕ : ℕ := M₁.toNat with hM₁ℕ_def
  have hM₁ℕ_cast : (M₁ℕ : ℤ) = M₁ := Int.toNat_of_nonneg (by omega)
  have h_n₁_abs : |n₁.toRat| = (M₁ℕ : ℚ) * 10 ^ E := by
    rw [hM_val]
    have hM₁_pos : (0 : ℚ) < (M₁ : ℚ) := by
      have : (0 : ℤ) < M₁ := by omega
      exact_mod_cast this
    have hcast : ((M₁ℕ : ℕ) : ℚ) = (M₁ : ℚ) := by exact_mod_cast hM₁ℕ_cast
    rcases hn : neg with _ | _
    · simp only [Bool.false_eq_true, if_false, one_mul]
      rw [abs_of_pos (by positivity), hcast]
    · simp only [if_true]
      rw [show (-1 : ℚ) * (M₁ : ℚ) * 10 ^ E = -((M₁ : ℚ) * 10 ^ E) from by ring,
          abs_neg, abs_of_pos (by positivity), hcast]
  obtain ⟨h_n₁_mant, h_n₁_exp⟩ := Number.normalized_rep_of_abs n₁ h_n₁_norm M₁ℕ E
    (by omega) (by omega) h_n₁_abs
  have h_n₁_neg : n₁.negative_ = neg := by
    rcases hn : neg with _ | _
    · rcases hb : n₁.negative_ with _ | _
      · rfl
      · exfalso
        have h1 : n₁.toRat ≤ 0 := Number.toRat_nonpos_of_negative n₁ hb
        have h2 : n₁.toRat = (M₁ : ℚ) * 10 ^ E := by
          rw [hM_val, hn]
          norm_num
        have hM₁_pos : (0 : ℚ) < (M₁ : ℚ) := by
          have : (0 : ℤ) < M₁ := by omega
          exact_mod_cast this
        nlinarith
    · rcases hb : n₁.negative_ with _ | _
      · exfalso
        have h1 : 0 ≤ n₁.toRat := Number.toRat_nonneg_of_nonnegative n₁ hb
        have h2 : n₁.toRat = -((M₁ : ℚ) * 10 ^ E) := by
          rw [hM_val, hn, if_pos rfl]
          ring
        have hM₁_pos : (0 : ℚ) < (M₁ : ℚ) := by
          have : (0 : ℤ) < M₁ := by omega
          exact_mod_cast this
        nlinarith
      · rfl
  -- 16-digit renormalization of n₁.
  obtain ⟨m₁₆, h_ntr, h_m₁₆_hi, h_m₁₆_tn, h_m₁₆_tz, h_m₁₆_dn, h_m₁₆_up, h_m₁₆_exact⟩ :=
    normalizeToRange_16_per_mode n₁ mode
      (by rw [h_n₁_mant]; omega) (by rw [h_n₁_mant]; omega)
      (by rw [h_n₁_exp]; omega) (by rw [h_n₁_exp]; unfold maxExponent; omega)
  rw [h_n₁_exp, show E + 3 = s from by rw [hE_def]; ring] at h_ntr
  -- ofNumber: clamp passes at exponent s.
  have h_of_explicit : IOUAmount.ofNumber n₁ mode
      = .ok ⟨if n₁.negative_ then -m₁₆.toInt64 else m₁₆.toInt64, s⟩ := by
    unfold IOUAmount.ofNumber IOUAmount.fromNumber
    rw [h_ntr]
    simp only []
    rw [if_neg (by show ¬ s > cMaxOffset; unfold cMaxOffset; omega),
        if_neg (by show ¬ s < cMinOffset; unfold cMinOffset; omega)]
  have h_sumI : sumI = ⟨if n₁.negative_ then -m₁₆.toInt64 else m₁₆.toInt64, s⟩ :=
    Except.ok.inj (h_of.symm.trans h_of_explicit)
  -- Repack via ofIOUAmount.
  have h_m₁₆_lo : 10 ^ 15 ≤ m₁₆.toNat := by
    rcases mode with _ | _ | _ | _
    · rcases h_m₁₆_tn rfl with h | h <;> rw [h, h_n₁_mant] <;> omega
    · rw [h_m₁₆_tz rfl, h_n₁_mant]; omega
    · have := h_m₁₆_dn rfl; rw [this, h_n₁_mant]; omega
    · have := h_m₁₆_up rfl; rw [this, h_n₁_mant]; omega
  have h_m₁₆_fit : m₁₆.toNat < 2 ^ 63 := by omega
  have h_mant_toInt : (if n₁.negative_ then -m₁₆.toInt64 else m₁₆.toInt64).toInt
      = (if n₁.negative_ then -1 else 1) * (m₁₆.toNat : ℤ) :=
    signed_mantissa_toInt n₁.negative_ m₁₆ h_m₁₆_fit
  have h_pack_explicit : STAmount.ofIOUAmount
      ⟨if n₁.negative_ then -m₁₆.toInt64 else m₁₆.toInt64, s⟩ iss mode
      = .ok ⟨.issue iss, m₁₆, s, n₁.negative_⟩ := by
    have hr : IOUAmount.InRange16 ⟨if n₁.negative_ then -m₁₆.toInt64 else m₁₆.toInt64, s⟩ :=
      { mant_lo := by
          show 10 ^ 15 ≤ (if n₁.negative_ then -m₁₆.toInt64 else m₁₆.toInt64).toInt.natAbs
          rw [h_mant_toInt]
          rcases hb : n₁.negative_ with _ | _ <;> simp <;> omega
        mant_hi := by
          show (if n₁.negative_ then -m₁₆.toInt64 else m₁₆.toInt64).toInt.natAbs < 10 ^ 16
          rw [h_mant_toInt]
          rcases hb : n₁.negative_ with _ | _ <;> simp <;> omega
        exp_lo := h_s
        exp_hi := h_s_hi }
    have h_not_xrp' : (Asset.issue iss).isNative = false := h_not_xrp
    rw [STAmount.ofIOUAmount_canonical _ iss mode h_not_xrp' hr]
    congr 1
    have h_abs_eq : ((if n₁.negative_ then -m₁₆.toInt64 else m₁₆.toInt64).toInt.natAbs
        : ℕ).toUInt64 = m₁₆ := by
      rw [← UInt64.toNat_inj]
      have h1 : (if n₁.negative_ then -m₁₆.toInt64 else m₁₆.toInt64).toInt.natAbs
          = m₁₆.toNat :=
        signed_mantissa_natAbs n₁.negative_ m₁₆ h_m₁₆_fit
      rw [h1, UInt64.toNat_ofNat_of_lt' (by rw [uint64_size_val]; omega)]
    have h_dec : decide ((if n₁.negative_ then -m₁₆.toInt64 else m₁₆.toInt64) < 0)
        = n₁.negative_ :=
      signed_mantissa_decide_neg n₁.negative_ m₁₆ h_m₁₆_fit (by omega)
    rw [h_abs_eq, h_dec]
  have h_sum : sum = ⟨.issue iss, m₁₆, s, n₁.negative_⟩ := by
    rw [h_sumI] at h_pack
    exact Except.ok.inj (h_pack.symm.trans h_pack_explicit)
  -- Cast and composition prework for the k-characterization.
  have h_A_div : A / 10 ^ E = V / 10 ^ E + 10 ^ 18 := by
    rw [hA_def]
    field_simp
  have hFσ_split : Fσ = ⌊V / 10 ^ E⌋ + 10 ^ 18 := by
    rw [hFσ_def, h_A_div, show ((10 : ℚ) ^ 18) = ((10 ^ 18 : ℤ) : ℚ) from by push_cast; ring,
        Int.floor_add_intCast]
  have hCσ_split : Cσ = ⌈V / 10 ^ E⌉ + 10 ^ 18 := by
    rw [hCσ_def, h_A_div, show ((10 : ℚ) ^ 18) = ((10 ^ 18 : ℤ) : ℚ) from by push_cast; ring,
        Int.ceil_add_intCast]
  set Fe : ℤ := ⌊V / 10 ^ E⌋ with hFe_def
  set Ce : ℤ := ⌈V / 10 ^ E⌉ with hCe_def
  have hFe_nn : 0 ≤ Fe := by
    rw [hFe_def]
    apply Int.le_floor.mpr
    push_cast
    positivity
  have hCe_nn : 0 ≤ Ce := le_trans hFe_nn (Int.floor_le_ceil _)
  have h_div_eq : V / 10 ^ E / 1000 = V / 10 ^ s := by
    rw [h_pow_shift]
    field_simp
  have hF'_comp : ⌊V / 10 ^ s⌋ = ⌊((Fe : ℚ)) / 1000⌋ := by
    rw [← h_div_eq, hFe_def]
    exact (Int.floor_floor_div (V / 10 ^ E) 1000 (by norm_num)).symm
  have hC'_comp : ⌈V / 10 ^ s⌉ = ⌈((Ce : ℚ)) / 1000⌉ := by
    rw [← h_div_eq, hCe_def]
    exact (Int.ceil_ceil_div (V / 10 ^ E) 1000 (by norm_num)).symm
  have hFe_floor_cast : ((Fe.toNat / 1000 : ℕ) : ℤ) = ⌊V / 10 ^ s⌋ := by
    rw [nat_floorDiv_cast, hF'_comp]
    congr 2
    exact_mod_cast Int.toNat_of_nonneg hFe_nn
  have hCe_ceil_cast : ((Ce.toNat / 1000 + if Ce.toNat % 1000 ≠ 0 then 1 else 0 : ℕ) : ℤ)
      = ⌈V / 10 ^ s⌉ := by
    rw [nat_ceilDiv_cast, hC'_comp]
    congr 2
    exact_mod_cast Int.toNat_of_nonneg hCe_nn
  -- Non-integrality bridge: a nonzero last-3-digit remainder forces V/10^s ∉ ℤ.
  have h_not_int_of_Fe : Fe.toNat % 1000 ≠ 0 → ∀ t : ℤ, V / 10 ^ s ≠ (t : ℚ) := by
    intro hmod t ht
    apply hmod
    have h1 : V / 10 ^ E = (t : ℚ) * 1000 := by
      rw [← h_div_eq] at ht
      field_simp at ht ⊢
      linarith
    have h2 : Fe = t * 1000 := by
      rw [hFe_def, h1, show ((t : ℚ) * 1000) = ((t * 1000 : ℤ) : ℚ) from by push_cast; ring,
          Int.floor_intCast]
    omega
  have h_not_int_of_Ce : Ce.toNat % 1000 ≠ 0 → ∀ t : ℤ, V / 10 ^ s ≠ (t : ℚ) := by
    intro hmod t ht
    apply hmod
    have h1 : V / 10 ^ E = (t : ℚ) * 1000 := by
      rw [← h_div_eq] at ht
      field_simp at ht ⊢
      linarith
    have h2 : Ce = t * 1000 := by
      rw [hCe_def, h1, show ((t : ℚ) * 1000) = ((t * 1000 : ℤ) : ℚ) from by push_cast; ring,
          Int.ceil_intCast]
    omega
  have h_FC_succ : (∀ t : ℤ, V / 10 ^ s ≠ (t : ℚ)) →
      ⌈V / 10 ^ s⌉ = ⌊V / 10 ^ s⌋ + 1 := by
    intro h_ni
    have h1 : ⌈V / 10 ^ s⌉ ≤ ⌊V / 10 ^ s⌋ + 1 := Int.ceil_le_floor_add_one _
    have h2 : ⌊V / 10 ^ s⌋ < ⌈V / 10 ^ s⌉ := by
      rcases lt_or_eq_of_le (Int.floor_le_ceil (V / 10 ^ s)) with h | h
      · exact h
      · exfalso
        apply h_ni ⌊V / 10 ^ s⌋
        have h3 := Int.floor_le (V / 10 ^ s)
        have h4 := Int.le_ceil (V / 10 ^ s)
        rw [← h] at h4
        linarith
    omega
  have hM₁ℕ_mod_F : M₁ = Fσ → M₁ℕ % 1000 = Fe.toNat % 1000 := by
    intro h
    have : (M₁ℕ : ℤ) = Fe + 10 ^ 18 := by rw [hM₁ℕ_cast, h, hFσ_split]
    omega
  have hM₁ℕ_mod_C : M₁ = Cσ → M₁ℕ % 1000 = Ce.toNat % 1000 := by
    intro h
    have : (M₁ℕ : ℤ) = Ce + 10 ^ 18 := by rw [hM₁ℕ_cast, h, hCσ_split]
    omega
  have hM₁ℕ_div_F : M₁ = Fσ → M₁ℕ / 1000 = 10 ^ 15 + Fe.toNat / 1000 := by
    intro h
    have : (M₁ℕ : ℤ) = Fe + 10 ^ 18 := by rw [hM₁ℕ_cast, h, hFσ_split]
    omega
  have hM₁ℕ_div_C : M₁ = Cσ → M₁ℕ / 1000 = 10 ^ 15 + Ce.toNat / 1000 := by
    intro h
    have : (M₁ℕ : ℤ) = Ce + 10 ^ 18 := by rw [hM₁ℕ_cast, h, hCσ_split]
    omega
  have h_n₁_mantN : n₁.mantissa_.toNat = M₁ℕ := h_n₁_mant
  -- The k-characterizations, per mode.
  have hk_lo : 10 ^ 15 ≤ m₁₆.toNat := h_m₁₆_lo
  refine ⟨m₁₆.toNat - 10 ^ 15, ?_, ?_, ?_, ?_, ?_, ?_, ?_, ?_, ?_⟩
  · -- k ≤ 10^15: from the mode characterizations and Cσ ≤ 2·10^18.
    rcases mode with _ | _ | _ | _
    · rcases h_m₁₆_tn rfl with h | h <;> rw [h_n₁_mantN] at h
      · have := hM₁ℕ_div_F
        rcases hM_tn rfl with hM | hM
        · have h1 := hM₁ℕ_div_F hM
          have h2 := hFe_floor_cast
          have h3 : ⌊V / 10 ^ s⌋ ≤ ⌈V / 10 ^ s⌉ := Int.floor_le_ceil _
          have h4 : ⌈V / 10 ^ s⌉ ≤ 10 ^ 15 := by
            apply Int.ceil_le.mpr
            rw [div_le_iff₀ h_pow_s_pos]
            push_cast
            linarith
          omega
        · have h1 := hM₁ℕ_div_C hM
          have h2 := hCe_ceil_cast
          have h4 : ⌈V / 10 ^ s⌉ ≤ 10 ^ 15 := by
            apply Int.ceil_le.mpr
            rw [div_le_iff₀ h_pow_s_pos]
            push_cast
            linarith
          omega
      · rcases hM_tn rfl with hM | hM
        · have h1 := hM₁ℕ_div_F hM
          have h2 := hFe_floor_cast
          have h3 : ⌊V / 10 ^ s⌋ + 1 ≤ 10 ^ 15 := by
            have h4 : ⌊V / 10 ^ s⌋ < 10 ^ 15 := by
              apply Int.floor_lt.mpr
              rw [div_lt_iff₀ h_pow_s_pos]
              push_cast
              linarith
            omega
          omega
        · have h1 := hM₁ℕ_div_C hM
          have h2 := hCe_ceil_cast
          have h3 : ⌈V / 10 ^ s⌉ ≤ 10 ^ 15 := by
            apply Int.ceil_le.mpr
            rw [div_le_iff₀ h_pow_s_pos]
            push_cast
            linarith
          have h4 : Ce.toNat / 1000 ≤ 10 ^ 15 := by omega
          omega
    · rw [h_m₁₆_tz rfl, h_n₁_mantN, hM₁ℕ_div_F (hM_tz rfl)]
      have h2 := hFe_floor_cast
      have h4 : ⌊V / 10 ^ s⌋ < 10 ^ 15 := by
        apply Int.floor_lt.mpr
        rw [div_lt_iff₀ h_pow_s_pos]
        push_cast
        linarith
      omega
    · rw [h_m₁₆_dn rfl, h_n₁_mantN, h_n₁_neg]
      have h_dn := hM_dn rfl
      rcases hn : neg with _ | _ <;> rw [hn] at h_dn
      · simp only [Bool.false_eq_true, if_false] at h_dn
        rw [hM₁ℕ_div_F h_dn,
            if_neg (by intro ⟨h, _⟩; exact Bool.noConfusion h), Nat.add_zero]
        have h2 := hFe_floor_cast
        have h4 : ⌊V / 10 ^ s⌋ < 10 ^ 15 := by
          apply Int.floor_lt.mpr
          rw [div_lt_iff₀ h_pow_s_pos]
          push_cast
          linarith
        omega
      · simp only [if_true] at h_dn
        rw [hM₁ℕ_div_C h_dn]
        have h2 := hCe_ceil_cast
        have h3 : ⌈V / 10 ^ s⌉ ≤ 10 ^ 15 := by
          apply Int.ceil_le.mpr
          rw [div_le_iff₀ h_pow_s_pos]
          push_cast
          linarith
        by_cases hmod : M₁ℕ % 1000 ≠ 0
        · rw [if_pos ⟨rfl, hmod⟩]
          have h5 := hM₁ℕ_mod_C h_dn
          have h6 : Ce.toNat % 1000 ≠ 0 := by omega
          rw [if_pos h6] at h2
          omega
        · push_neg at hmod
          rw [if_neg (by intro ⟨_, h⟩; exact h hmod), Nat.add_zero]
          have h5 := hM₁ℕ_mod_C h_dn
          have h6 : Ce.toNat % 1000 = 0 := by omega
          rw [if_neg (by intro hh; exact hh h6)] at h2
          omega
    · rw [h_m₁₆_up rfl, h_n₁_mantN, h_n₁_neg]
      have h_up := hM_up rfl
      rcases hn : neg with _ | _ <;> rw [hn] at h_up
      · simp only [Bool.false_eq_true, if_false] at h_up
        rw [hM₁ℕ_div_C h_up]
        have h2 := hCe_ceil_cast
        have h3 : ⌈V / 10 ^ s⌉ ≤ 10 ^ 15 := by
          apply Int.ceil_le.mpr
          rw [div_le_iff₀ h_pow_s_pos]
          push_cast
          linarith
        by_cases hmod : M₁ℕ % 1000 ≠ 0
        · rw [if_pos ⟨rfl, hmod⟩]
          have h5 := hM₁ℕ_mod_C h_up
          have h6 : Ce.toNat % 1000 ≠ 0 := by omega
          rw [if_pos h6] at h2
          omega
        · push_neg at hmod
          rw [if_neg (by intro ⟨_, h⟩; exact h hmod), Nat.add_zero]
          have h5 := hM₁ℕ_mod_C h_up
          have h6 : Ce.toNat % 1000 = 0 := by omega
          rw [if_neg (by intro hh; exact hh h6)] at h2
          omega
      · simp only [if_true] at h_up
        rw [hM₁ℕ_div_F h_up,
            if_neg (by intro ⟨h, _⟩; exact Bool.noConfusion h), Nat.add_zero]
        have h2 := hFe_floor_cast
        have h4 : ⌊V / 10 ^ s⌋ < 10 ^ 15 := by
          apply Int.floor_lt.mpr
          rw [div_lt_iff₀ h_pow_s_pos]
          push_cast
          linarith
        omega
  · rw [h_sum, h_asset]
  · rw [h_sum]
    show m₁₆.toNat = 10 ^ 15 + (m₁₆.toNat - 10 ^ 15)
    omega
  · rw [h_sum]
  · rw [h_sum, h_n₁_neg, hneg_def]
  · -- to_nearest: membership.
    intro hmode
    subst hmode
    have h_m := h_m₁₆_tn rfl
    rw [h_n₁_mantN] at h_m
    rcases hM_tn rfl with hM | hM
    · rcases h_m with h | h
      · left
        rw [hM₁ℕ_div_F hM] at h
        have h2 := hFe_floor_cast
        omega
      · -- fired on the floor branch: the remainder is nonzero, so ceil = floor + 1.
        right
        have hmod : M₁ℕ % 1000 ≠ 0 := by
          intro h0
          have := h_m₁₆_exact (by rw [h_n₁_mantN]; exact h0)
          rw [h_n₁_mantN] at this
          omega
        have h5 := hM₁ℕ_mod_F hM
        have h6 : Fe.toNat % 1000 ≠ 0 := by omega
        have h7 := h_FC_succ (h_not_int_of_Fe h6)
        rw [hM₁ℕ_div_F hM] at h
        have h2 := hFe_floor_cast
        omega
    · rcases h_m with h | h
      · -- truncated on the ceil branch.
        rw [hM₁ℕ_div_C hM] at h
        have h2 := hCe_ceil_cast
        by_cases hmod : Ce.toNat % 1000 = 0
        · right
          rw [if_neg (by intro hh; exact hh hmod)] at h2
          omega
        · left
          have h7 := h_FC_succ (h_not_int_of_Ce hmod)
          rw [if_pos hmod] at h2
          omega
      · -- fired on the ceil branch: remainder nonzero.
        right
        have hmod : M₁ℕ % 1000 ≠ 0 := by
          intro h0
          have := h_m₁₆_exact (by rw [h_n₁_mantN]; exact h0)
          rw [h_n₁_mantN] at this
          omega
        have h5 := hM₁ℕ_mod_C hM
        have h6 : Ce.toNat % 1000 ≠ 0 := by omega
        rw [hM₁ℕ_div_C hM] at h
        have h2 := hCe_ceil_cast
        rw [if_pos h6] at h2
        omega
  · -- towards_zero.
    intro hmode
    subst hmode
    rw [h_m₁₆_tz rfl, h_n₁_mantN, hM₁ℕ_div_F (hM_tz rfl)]
    have h2 := hFe_floor_cast
    omega
  · -- downward.
    intro hmode
    subst hmode
    have h_m := h_m₁₆_dn rfl
    rw [h_n₁_mantN, h_n₁_neg] at h_m
    have h_dn := hM_dn rfl
    rcases hn : neg with _ | _ <;> rw [hn] at h_dn h_m
    · simp only [Bool.false_eq_true, if_false] at h_dn ⊢
      rw [if_neg (by intro ⟨h, _⟩; exact Bool.noConfusion h), Nat.add_zero,
          hM₁ℕ_div_F h_dn] at h_m
      have h2 := hFe_floor_cast
      omega
    · simp only [if_true] at h_dn ⊢
      rw [hM₁ℕ_div_C h_dn] at h_m
      have h2 := hCe_ceil_cast
      have h5 := hM₁ℕ_mod_C h_dn
      by_cases hmod : M₁ℕ % 1000 ≠ 0
      · rw [if_pos ⟨rfl, hmod⟩] at h_m
        have h6 : Ce.toNat % 1000 ≠ 0 := by omega
        rw [if_pos h6] at h2
        omega
      · push_neg at hmod
        rw [if_neg (by intro ⟨_, h⟩; exact h hmod), Nat.add_zero] at h_m
        have h6 : Ce.toNat % 1000 = 0 := by omega
        rw [if_neg (by intro hh; exact hh h6)] at h2
        omega
  · -- upward.
    intro hmode
    subst hmode
    have h_m := h_m₁₆_up rfl
    rw [h_n₁_mantN, h_n₁_neg] at h_m
    have h_up := hM_up rfl
    rcases hn : neg with _ | _ <;> rw [hn] at h_up h_m
    · simp only [Bool.false_eq_true, if_false] at h_up ⊢
      rw [hM₁ℕ_div_C h_up] at h_m
      have h2 := hCe_ceil_cast
      have h5 := hM₁ℕ_mod_C h_up
      by_cases hmod : M₁ℕ % 1000 ≠ 0
      · rw [if_pos ⟨rfl, hmod⟩] at h_m
        have h6 : Ce.toNat % 1000 ≠ 0 := by omega
        rw [if_pos h6] at h2
        omega
      · push_neg at hmod
        rw [if_neg (by intro ⟨_, h⟩; exact h hmod), Nat.add_zero] at h_m
        have h6 : Ce.toNat % 1000 = 0 := by omega
        rw [if_neg (by intro hh; exact hh h6)] at h2
        omega
    · simp only [if_true] at h_up ⊢
      rw [if_neg (by intro ⟨h, _⟩; exact Bool.noConfusion h), Nat.add_zero,
          hM₁ℕ_div_F h_up] at h_m
      have h2 := hFe_floor_cast
      omega

end XRPL.Model.Protocol
