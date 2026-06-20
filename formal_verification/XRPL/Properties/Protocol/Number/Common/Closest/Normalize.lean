import XRPL.Properties.Protocol.Number.Common.Notation
import XRPL.Properties.Protocol.Number.Common.Closest.Defs


namespace XRPL.Model.Protocol

/-! ## Helper lemmas for m_real bounds -/

/-- For positive `q`, `m_real = q * 10^(18 - Int.log 10 q)` lies in `[10^18, 10^19)`. -/
lemma m_real_bounds (q : ℚ) (hq : 0 < q) :
    let e_q : ℤ := Int.log 10 q - mantissaLog
    let m_real : ℚ := q * (10 : ℚ)^(-e_q)
    (10 : ℚ)^(18 : ℕ) ≤ m_real ∧ m_real < (10 : ℚ)^(19 : ℕ) := by
  simp only
  set L := Int.log 10 q with hL_def
  have h_log_le : (10 : ℚ) ^ L ≤ q := Int.zpow_log_le_self (by norm_num) hq
  have h_log_lt : q < (10 : ℚ) ^ (L + 1) := Int.lt_zpow_succ_log_self (by norm_num) q
  have h_pow_pos : (0 : ℚ) < (10 : ℚ) ^ (18 - L) := zpow_pos (by norm_num) _
  refine ⟨?_, ?_⟩
  · have h_eq : (10 : ℚ) ^ (-(L - mantissaLog)) = (10 : ℚ) ^ (18 - L) := by
      congr 1; ring
    rw [h_eq]
    have h_pow_eq : (10 : ℚ) ^ (18 : ℕ) = (10 : ℚ) ^ L * (10 : ℚ) ^ (18 - L) := by
      rw [← zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0)]
      have : (L + (18 - L) : ℤ) = (18 : ℕ) := by push_cast; ring
      rw [this, zpow_natCast]
    rw [h_pow_eq]
    exact mul_le_mul_of_nonneg_right h_log_le (le_of_lt h_pow_pos)
  · have h_eq : (10 : ℚ) ^ (-(L - mantissaLog)) = (10 : ℚ) ^ (18 - L) := by
      congr 1; ring
    rw [h_eq]
    have h_pow_eq : (10 : ℚ) ^ (19 : ℕ) = (10 : ℚ) ^ (L + 1) * (10 : ℚ) ^ (18 - L) := by
      rw [← zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0)]
      have : ((L + 1) + (18 - L) : ℤ) = (19 : ℕ) := by push_cast; ring
      rw [this, zpow_natCast]
    rw [h_pow_eq]
    exact mul_lt_mul_of_pos_right h_log_lt h_pow_pos

/-- For positive `q`, `m_ceil_nat := ⌈m_real⌉₊` satisfies `10^18 ≤ m_ceil_nat ≤ 10^19`. -/
lemma m_ceil_nat_bounds (q : ℚ) (hq : 0 < q) :
    let e_q : ℤ := Int.log 10 q - mantissaLog
    let m_real : ℚ := q * (10 : ℚ)^(-e_q)
    let m_ceil_nat : ℕ := ⌈m_real⌉₊
    10^18 ≤ m_ceil_nat ∧ m_ceil_nat ≤ 10^19 := by
  simp only
  obtain ⟨h_lo, h_hi⟩ := m_real_bounds q hq
  refine ⟨?_, ?_⟩
  · have h_le : ((10^18 : ℕ) : ℚ) ≤ q * (10 : ℚ)^(-(Int.log 10 q - mantissaLog)) := by
      have := h_lo
      push_cast
      convert this using 1
    have h_ge_m : q * (10 : ℚ)^(-(Int.log 10 q - mantissaLog))
        ≤ (⌈q * (10 : ℚ)^(-(Int.log 10 q - mantissaLog))⌉₊ : ℚ) := Nat.le_ceil _
    have : ((10^18 : ℕ) : ℚ) ≤ (⌈q * (10 : ℚ)^(-(Int.log 10 q - mantissaLog))⌉₊ : ℚ) :=
      le_trans h_le h_ge_m
    exact_mod_cast this
  · have h_le : q * (10 : ℚ)^(-(Int.log 10 q - mantissaLog)) ≤ ((10^19 : ℕ) : ℚ) := by
      have := le_of_lt h_hi
      push_cast
      convert this using 1
    exact Nat.ceil_le.mpr h_le

/-! ## Normalization and sign-flip lemmas -/

/-- Sign-flip lemma: flipping `negative_` negates `toRat`. -/
private lemma Number.toRat_flip (n : Number) :
    ({n with negative_ := !n.negative_} : Number).toRat = -n.toRat := by
  set m := ({n with negative_ := !n.negative_} : Number) with hm_def
  have h_abs_eq : |m.toRat| = |n.toRat| := by
    rw [abs_toRat_eq m, abs_toRat_eq n]
  cases hn : n.negative_
  · have hm_neg : m.negative_ = true := by
      rw [hm_def]; change (!n.negative_ : Bool) = true; rw [hn]; rfl
    have hm_le : m.toRat ≤ 0 := Number.toRat_nonpos_of_negative m hm_neg
    have hn_nn : 0 ≤ n.toRat := Number.toRat_nonneg_of_nonnegative n hn
    have h_eq : -m.toRat = n.toRat := by
      rw [← abs_of_nonpos hm_le, ← abs_of_nonneg hn_nn]
      exact h_abs_eq
    linarith
  · have hm_neg : m.negative_ = false := by
      rw [hm_def]; change (!n.negative_ : Bool) = false; rw [hn]; rfl
    have hm_nn : 0 ≤ m.toRat := Number.toRat_nonneg_of_nonnegative m hm_neg
    have hn_le : n.toRat ≤ 0 := Number.toRat_nonpos_of_negative n hn
    have h_eq : m.toRat = -n.toRat := by
      have h1 : m.toRat = |n.toRat| := by rw [← abs_of_nonneg hm_nn]; exact h_abs_eq
      have h2 : |n.toRat| = -n.toRat := abs_of_nonpos hn_le
      linarith
    exact h_eq

/-- Specialization: flipping `negative_ := true` from a non-negative Number. -/
lemma Number.toRat_set_neg_true_of_nn (n : Number) (h : n.negative_ = false) :
    ({n with negative_ := true} : Number).toRat = -n.toRat := by
  have := Number.toRat_flip n
  rw [h] at this; simpa using this

/-- Specialization: flipping `negative_ := false` from a negative Number. -/
lemma Number.toRat_set_neg_false_of_neg (n : Number) (h : n.negative_ = true) :
    ({n with negative_ := false} : Number).toRat = -n.toRat := by
  have := Number.toRat_flip n
  rw [h] at this; simpa using this

/-- Characterization of `Number.lower q` for negative q: it sign-flips `Number.upper (-q)`. -/
lemma Number.lower_neg_eq (q : ℚ) (hq : q < 0) :
    Number.lower q = (upperPosAux (-q)).map (fun n => { n with negative_ := true }) := by
  unfold Number.lower
  have hq_ne : q ≠ 0 := ne_of_lt hq
  rw [if_neg hq_ne, if_pos hq]

/-- Characterization of `Number.upper q` for negative q: it sign-flips `Number.lower (-q)`,
with a `Number.zero` short-circuit. -/
lemma Number.upper_neg_eq (q : ℚ) (hq : q < 0) :
    Number.upper q = (lowerPosAux (-q)).map (fun n =>
      if n = Number.zero then Number.zero else { n with negative_ := true }) := by
  unfold Number.upper
  have hq_ne : q ≠ 0 := ne_of_lt hq
  rw [if_neg hq_ne, if_pos hq]

/-- Helper: given m, e, and validity props, the Number `⟨false, ⟨m, h⟩, e⟩` is normalized. -/
private lemma isNormalized_anonymous
    (m : ℕ) (h_m64 : m < 2 ^ 64) (neg : Bool) (e : Int)
    (h_min : largeRange.min.toNat ≤ m)
    (h_max : m ≤ largeRange.max.toNat)
    (h_valid : m ≤ maxRep.toNat ∨ m % 10 = 0)
    (h_emin : minExponent ≤ e) (h_emax : e ≤ maxExponent) :
    Number.isNormalized
      { negative_ := neg
        mantissa_ := { toBitVec := { toFin := ⟨m, h_m64⟩ } }
        exponent_ := e } := by
  right
  -- The mantissa as UInt64 has toNat = m (since m < 2^64).
  have h_toNat : ((⟨⟨⟨m, h_m64⟩⟩⟩ : UInt64)).toNat = m := rfl
  refine ⟨?_, ?_, ?_, h_emin, h_emax⟩
  · change largeRange.min ≤ (⟨⟨⟨m, h_m64⟩⟩⟩ : UInt64)
    rw [UInt64.le_iff_toNat_le, h_toNat]
    exact h_min
  · change (⟨⟨⟨m, h_m64⟩⟩⟩ : UInt64) ≤ largeRange.max
    rw [UInt64.le_iff_toNat_le, h_toNat]
    exact h_max
  · rcases h_valid with hv | hv
    · left
      change (⟨⟨⟨m, h_m64⟩⟩⟩ : UInt64) ≤ maxRep
      rw [UInt64.le_iff_toNat_le, h_toNat]
      exact hv
    · right
      change ((⟨⟨⟨m, h_m64⟩⟩⟩ : UInt64).toNat) % 10 = 0
      rw [h_toNat]
      exact hv

/-- Helper: result is normalized for the `largeRange.min, minExponent` corner. -/
private lemma isNormalized_largeRange_min_at_minExp :
    (⟨false, largeRange.min, minExponent⟩ : Number).isNormalized := by
  right
  refine ⟨?_, ?_, ?_, ?_, ?_⟩
  · change largeRange.min ≤ largeRange.min
    rw [UInt64.le_iff_toNat_le]
  · change largeRange.min ≤ largeRange.max
    rw [UInt64.le_iff_toNat_le]
    have hmin_eq : largeRange.min.toNat = 1000000000000000000 := largeRange_min_val
    have hmax_eq : largeRange.max.toNat = 9999999999999999999 := largeRange_max_val
    omega
  · right
    change largeRange.min.toNat % 10 = 0
    decide
  · change minExponent ≤ minExponent; rfl
  · change minExponent ≤ maxExponent; decide

/-- Helper: for q > 0, the result of upperPosAux is normalized. -/
lemma upperPosAux_isNormalized (q : ℚ) (hq : 0 < q) (n : Number)
    (h : upperPosAux q = some n) : n.isNormalized := by
  have h_ceil_bounds := m_ceil_nat_bounds q hq
  unfold upperPosAux at h
  simp only at h
  set m_ceil_nat : ℕ := ⌈q * (10 : ℚ)^(-(Int.log 10 q - mantissaLog))⌉₊ with hmceil_def
  rcases h_bump_eq : bumpToValidMantissa m_ceil_nat with _ | m
  · rw [h_bump_eq] at h
    simp only at h
    split_ifs at h with hexp hcorner
    · have hn_eq : n = ⟨false, largeRange.min, Int.log 10 q - mantissaLog + 1⟩ :=
        (Option.some.inj h).symm
      rw [hn_eq]
      right
      refine ⟨?_, ?_, ?_, hexp.1, hexp.2⟩
      · change largeRange.min ≤ largeRange.min
        rw [UInt64.le_iff_toNat_le]
      · change largeRange.min ≤ largeRange.max
        rw [UInt64.le_iff_toNat_le]
        have hmin_eq : largeRange.min.toNat = 1000000000000000000 := largeRange_min_val
        have hmax_eq : largeRange.max.toNat = 9999999999999999999 := largeRange_max_val
        omega
      · right
        change largeRange.min.toNat % 10 = 0
        decide
    · have hn_eq : n = ⟨false, largeRange.min, minExponent⟩ := (Option.some.inj h).symm
      rw [hn_eq]
      exact isNormalized_largeRange_min_at_minExp
  · rw [h_bump_eq] at h
    simp only at h
    have h_m_valid : m ≤ maxRep.toNat ∨ m % 10 = 0 :=
      bumpToValidMantissa_valid m_ceil_nat m h_bump_eq
    have h_m_lt : m < 10 ^ 19 := bumpToValidMantissa_lt_pow19 m_ceil_nat m h_bump_eq
    have h_m_ge : m_ceil_nat ≤ m := bumpToValidMantissa_ge m_ceil_nat m h_bump_eq
    split_ifs at h with hexp hcorner
    · have hn_eq : n = ⟨false, ⟨⟨⟨m, hexp.2.2⟩⟩⟩, Int.log 10 q - mantissaLog⟩ :=
        (Option.some.inj h).symm
      rw [hn_eq]
      have hmin_eq : largeRange.min.toNat = 1000000000000000000 := largeRange_min_val
      have hmax_eq : largeRange.max.toNat = 9999999999999999999 := largeRange_max_val
      have h_min : largeRange.min.toNat ≤ m := by omega
      have h_max : m ≤ largeRange.max.toNat := by omega
      exact isNormalized_anonymous m hexp.2.2 false (Int.log 10 q - mantissaLog)
        h_min h_max h_m_valid hexp.1 hexp.2.1
    · have hn_eq : n = ⟨false, largeRange.min, minExponent⟩ := (Option.some.inj h).symm
      rw [hn_eq]
      exact isNormalized_largeRange_min_at_minExp

/-- Helper: for q > 0, the result of lowerPosAux is normalized. -/
lemma lowerPosAux_isNormalized (q : ℚ) (hq : 0 < q) (n : Number)
    (h : lowerPosAux q = some n) : n.isNormalized := by
  have h_real_bounds := m_real_bounds q hq
  unfold lowerPosAux at h
  simp only at h
  set m_floor_nat : ℕ := ⌊q * (10 : ℚ)^(-(Int.log 10 q - mantissaLog))⌋₊
  have h_floor_ge : (10^18 : ℕ) ≤ m_floor_nat := by
    have h_real_ge : (10 : ℚ) ^ (18 : ℕ) ≤ q * (10 : ℚ)^(-(Int.log 10 q - mantissaLog)) :=
      h_real_bounds.1
    have : ((10^18 : ℕ) : ℚ) ≤ q * (10 : ℚ)^(-(Int.log 10 q - mantissaLog)) := by
      push_cast; exact h_real_ge
    exact Nat.le_floor this
  have h_floor_lt : m_floor_nat < 10^19 := by
    have h_real_lt : q * (10 : ℚ)^(-(Int.log 10 q - mantissaLog)) < (10 : ℚ) ^ (19 : ℕ) :=
      h_real_bounds.2
    have h_real_nn : (0 : ℚ) ≤ q * (10 : ℚ)^(-(Int.log 10 q - mantissaLog)) := by
      have : (0 : ℚ) < (10 : ℚ) ^ (18 : ℕ) := by positivity
      linarith [h_real_bounds.1]
    have h_floor_le : (m_floor_nat : ℚ) ≤ q * (10 : ℚ)^(-(Int.log 10 q - mantissaLog)) :=
      Nat.floor_le h_real_nn
    have : (m_floor_nat : ℚ) < ((10^19 : ℕ) : ℚ) := by
      push_cast; linarith
    exact_mod_cast this
  set m := truncToValidMantissa m_floor_nat
  have h_m_le_floor : m ≤ m_floor_nat := truncToValidMantissa_le m_floor_nat
  have h_m_valid := truncToValidMantissa_valid m_floor_nat
  have h_m_lt : m < 10^19 := by omega
  split at h
  · rename_i hpos
    split at h
    · rename_i hexp
      have hn_eq : n = ⟨false, ⟨⟨⟨m, hpos.2⟩⟩⟩, Int.log 10 q - mantissaLog⟩ :=
        (Option.some.inj h).symm
      rw [hn_eq]
      have hmin_eq : largeRange.min.toNat = 1000000000000000000 := largeRange_min_val
      have hmax_eq : largeRange.max.toNat = 9999999999999999999 := largeRange_max_val
      have h_min : largeRange.min.toNat ≤ m := hpos.1
      have h_max : m ≤ largeRange.max.toNat := by omega
      exact isNormalized_anonymous m hpos.2 false (Int.log 10 q - mantissaLog)
        h_min h_max h_m_valid hexp.1 hexp.2
    · split at h
      · have hn_eq : n = Number.zero := (Option.some.inj h).symm
        rw [hn_eq]; left; rfl
      · exact absurd h (by simp)
  · rename_i _hpos_neg
    split at h
    · rename_i hexp
      have hn_eq : n = ⟨false, ⟨maxMul10Witness, by decide⟩, Int.log 10 q - mantissaLog - 1⟩ :=
        (Option.some.inj h).symm
      rw [hn_eq]
      have hmin_eq : largeRange.min.toNat = 1000000000000000000 := largeRange_min_val
      have hmax_eq : largeRange.max.toNat = 9999999999999999999 := largeRange_max_val
      have hmaxRep_eq : maxRep.toNat = maxRepNat := maxRep_val
      have h_m64 : maxMul10Witness < 2 ^ 64 := by decide
      have h_min : largeRange.min.toNat ≤ maxMul10Witness := by omega
      have h_max : maxMul10Witness ≤ largeRange.max.toNat := by omega
      have h_valid : maxMul10Witness ≤ maxRep.toNat ∨ maxMul10Witness % 10 = 0 := by
        right; decide
      have h_e_eq : (Int.log 10 q - mantissaLog - 1 : ℤ) = (Int.log 10 q - mantissaLog) - 1 := by ring
      exact isNormalized_anonymous maxMul10Witness h_m64 false (Int.log 10 q - mantissaLog - 1)
        h_min h_max h_valid hexp.1 hexp.2
    · split at h
      · have hn_eq : n = Number.zero := (Option.some.inj h).symm
        rw [hn_eq]; left; rfl
      · exact absurd h (by simp)

/-- For q > 0, lowerPosAux returns `Number.zero` or a Number with full bounds. -/
lemma lowerPosAux_isNormalized_right (q : ℚ) (hq : 0 < q) (n : Number)
    (h : lowerPosAux q = some n) :
    n = Number.zero ∨
    (largeRange.min ≤ n.mantissa_ ∧ n.mantissa_ ≤ largeRange.max ∧
     (n.mantissa_ ≤ maxRep ∨ n.mantissa_.toNat % 10 = 0) ∧
     minExponent ≤ n.exponent_ ∧ n.exponent_ ≤ maxExponent) := by
  have h_real_bounds := m_real_bounds q hq
  unfold lowerPosAux at h
  simp only at h
  set m_floor_nat : ℕ := ⌊q * (10 : ℚ)^(-(Int.log 10 q - mantissaLog))⌋₊
  have h_floor_ge : (10^18 : ℕ) ≤ m_floor_nat := by
    have h_real_ge : (10 : ℚ) ^ (18 : ℕ) ≤ q * (10 : ℚ)^(-(Int.log 10 q - mantissaLog)) :=
      h_real_bounds.1
    have : ((10^18 : ℕ) : ℚ) ≤ q * (10 : ℚ)^(-(Int.log 10 q - mantissaLog)) := by
      push_cast; exact h_real_ge
    exact Nat.le_floor this
  have h_floor_lt : m_floor_nat < 10^19 := by
    have h_real_lt : q * (10 : ℚ)^(-(Int.log 10 q - mantissaLog)) < (10 : ℚ) ^ (19 : ℕ) :=
      h_real_bounds.2
    have h_real_nn : (0 : ℚ) ≤ q * (10 : ℚ)^(-(Int.log 10 q - mantissaLog)) := by
      have : (0 : ℚ) < (10 : ℚ) ^ (18 : ℕ) := by positivity
      linarith [h_real_bounds.1]
    have h_floor_le : (m_floor_nat : ℚ) ≤ q * (10 : ℚ)^(-(Int.log 10 q - mantissaLog)) :=
      Nat.floor_le h_real_nn
    have : (m_floor_nat : ℚ) < ((10^19 : ℕ) : ℚ) := by
      push_cast; linarith
    exact_mod_cast this
  set m := truncToValidMantissa m_floor_nat
  have h_m_le_floor : m ≤ m_floor_nat := truncToValidMantissa_le m_floor_nat
  have h_m_valid := truncToValidMantissa_valid m_floor_nat
  have h_m_lt : m < 10^19 := by omega
  split at h
  · rename_i hpos
    split at h
    · rename_i hexp
      have hn_eq : n = ⟨false, ⟨⟨⟨m, hpos.2⟩⟩⟩, Int.log 10 q - mantissaLog⟩ :=
        (Option.some.inj h).symm
      right
      rw [hn_eq]
      have hmin_eq : largeRange.min.toNat = 1000000000000000000 := largeRange_min_val
      have hmax_eq : largeRange.max.toNat = 9999999999999999999 := largeRange_max_val
      have h_min : largeRange.min.toNat ≤ m := hpos.1
      have h_max : m ≤ largeRange.max.toNat := by omega
      have h_toNat : ((⟨⟨⟨m, hpos.2⟩⟩⟩ : UInt64)).toNat = m := rfl
      refine ⟨?_, ?_, ?_, hexp.1, hexp.2⟩
      · change largeRange.min ≤ (⟨⟨⟨m, hpos.2⟩⟩⟩ : UInt64)
        rw [UInt64.le_iff_toNat_le, h_toNat]; exact h_min
      · change (⟨⟨⟨m, hpos.2⟩⟩⟩ : UInt64) ≤ largeRange.max
        rw [UInt64.le_iff_toNat_le, h_toNat]; exact h_max
      · rcases h_m_valid with hv | hv
        · left
          change (⟨⟨⟨m, hpos.2⟩⟩⟩ : UInt64) ≤ maxRep
          rw [UInt64.le_iff_toNat_le, h_toNat]; exact hv
        · right
          change ((⟨⟨⟨m, hpos.2⟩⟩⟩ : UInt64).toNat) % 10 = 0
          rw [h_toNat]; exact hv
    · split at h
      · left; exact (Option.some.inj h).symm
      · exact absurd h (by simp)
  · rename_i _hpos_neg
    split at h
    · rename_i hexp
      have hn_eq : n = ⟨false, ⟨maxMul10Witness, by decide⟩, Int.log 10 q - mantissaLog - 1⟩ :=
        (Option.some.inj h).symm
      right
      rw [hn_eq]
      have hmin_eq : largeRange.min.toNat = 1000000000000000000 := largeRange_min_val
      have hmax_eq : largeRange.max.toNat = 9999999999999999999 := largeRange_max_val
      have hmaxRep_eq : maxRep.toNat = maxRepNat := maxRep_val
      refine ⟨?_, ?_, ?_, hexp.1, hexp.2⟩
      · change largeRange.min ≤ (⟨maxMul10Witness, by decide⟩ : UInt64)
        rw [UInt64.le_iff_toNat_le]
        have h_toNat : ((⟨maxMul10Witness, by decide⟩ : UInt64)).toNat = maxMul10Witness := rfl
        rw [h_toNat]; omega
      · change (⟨maxMul10Witness, by decide⟩ : UInt64) ≤ largeRange.max
        rw [UInt64.le_iff_toNat_le]
        have h_toNat : ((⟨maxMul10Witness, by decide⟩ : UInt64)).toNat = maxMul10Witness := rfl
        rw [h_toNat]; omega
      · right
        change ((⟨maxMul10Witness, by decide⟩ : UInt64).toNat) % 10 = 0
        decide
    · split at h
      · left; exact (Option.some.inj h).symm
      · exact absurd h (by simp)

/-- For q > 0, upperPosAux always returns a Number with full bounds (never `Number.zero`). -/
lemma upperPosAux_isNormalized_right (q : ℚ) (hq : 0 < q) (n : Number)
    (h : upperPosAux q = some n) :
    largeRange.min ≤ n.mantissa_ ∧ n.mantissa_ ≤ largeRange.max ∧
    (n.mantissa_ ≤ maxRep ∨ n.mantissa_.toNat % 10 = 0) ∧
    minExponent ≤ n.exponent_ ∧ n.exponent_ ≤ maxExponent := by
  have h_largeRange_min_bounds : ∀ (e : ℤ), minExponent ≤ e → e ≤ maxExponent →
      largeRange.min ≤ (⟨false, largeRange.min, e⟩ : Number).mantissa_ ∧
      (⟨false, largeRange.min, e⟩ : Number).mantissa_ ≤ largeRange.max ∧
      ((⟨false, largeRange.min, e⟩ : Number).mantissa_ ≤ maxRep ∨
        (⟨false, largeRange.min, e⟩ : Number).mantissa_.toNat % 10 = 0) ∧
      minExponent ≤ (⟨false, largeRange.min, e⟩ : Number).exponent_ ∧
      (⟨false, largeRange.min, e⟩ : Number).exponent_ ≤ maxExponent := by
    intro e hemin hemax
    refine ⟨?_, ?_, ?_, hemin, hemax⟩
    · change largeRange.min ≤ largeRange.min
      rw [UInt64.le_iff_toNat_le]
    · change largeRange.min ≤ largeRange.max
      rw [UInt64.le_iff_toNat_le]
      have hmin_eq : largeRange.min.toNat = 1000000000000000000 := largeRange_min_val
      have hmax_eq : largeRange.max.toNat = 9999999999999999999 := largeRange_max_val
      omega
    · right
      change largeRange.min.toNat % 10 = 0
      decide
  have h_ceil_bounds := m_ceil_nat_bounds q hq
  unfold upperPosAux at h
  simp only at h
  set m_ceil_nat : ℕ := ⌈q * (10 : ℚ)^(-(Int.log 10 q - mantissaLog))⌉₊ with hmceil_def
  rcases h_bump_eq : bumpToValidMantissa m_ceil_nat with _ | m
  · rw [h_bump_eq] at h
    simp only at h
    split_ifs at h with hexp hcorner
    · have hn_eq : n = ⟨false, largeRange.min, Int.log 10 q - mantissaLog + 1⟩ :=
        (Option.some.inj h).symm
      rw [hn_eq]
      exact h_largeRange_min_bounds (Int.log 10 q - mantissaLog + 1) hexp.1 hexp.2
    · have hn_eq : n = ⟨false, largeRange.min, minExponent⟩ := (Option.some.inj h).symm
      rw [hn_eq]
      have : minExponent ≤ maxExponent := by decide
      exact h_largeRange_min_bounds minExponent (le_refl _) this
  · rw [h_bump_eq] at h
    simp only at h
    have h_m_valid : m ≤ maxRep.toNat ∨ m % 10 = 0 :=
      bumpToValidMantissa_valid m_ceil_nat m h_bump_eq
    have h_m_lt : m < 10 ^ 19 := bumpToValidMantissa_lt_pow19 m_ceil_nat m h_bump_eq
    have h_m_ge : m_ceil_nat ≤ m := bumpToValidMantissa_ge m_ceil_nat m h_bump_eq
    split_ifs at h with hexp hcorner
    · have hn_eq : n = ⟨false, ⟨⟨⟨m, hexp.2.2⟩⟩⟩, Int.log 10 q - mantissaLog⟩ :=
        (Option.some.inj h).symm
      rw [hn_eq]
      have hmin_eq : largeRange.min.toNat = 1000000000000000000 := largeRange_min_val
      have hmax_eq : largeRange.max.toNat = 9999999999999999999 := largeRange_max_val
      have h_min : largeRange.min.toNat ≤ m := by omega
      have h_max : m ≤ largeRange.max.toNat := by omega
      have h_toNat : ((⟨⟨⟨m, hexp.2.2⟩⟩⟩ : UInt64)).toNat = m := rfl
      refine ⟨?_, ?_, ?_, hexp.1, hexp.2.1⟩
      · change largeRange.min ≤ (⟨⟨⟨m, hexp.2.2⟩⟩⟩ : UInt64)
        rw [UInt64.le_iff_toNat_le, h_toNat]; exact h_min
      · change (⟨⟨⟨m, hexp.2.2⟩⟩⟩ : UInt64) ≤ largeRange.max
        rw [UInt64.le_iff_toNat_le, h_toNat]; exact h_max
      · rcases h_m_valid with hv | hv
        · left
          change (⟨⟨⟨m, hexp.2.2⟩⟩⟩ : UInt64) ≤ maxRep
          rw [UInt64.le_iff_toNat_le, h_toNat]; exact hv
        · right
          change ((⟨⟨⟨m, hexp.2.2⟩⟩⟩ : UInt64).toNat) % 10 = 0
          rw [h_toNat]; exact hv
    · have hn_eq : n = ⟨false, largeRange.min, minExponent⟩ := (Option.some.inj h).symm
      rw [hn_eq]
      have : minExponent ≤ maxExponent := by decide
      exact h_largeRange_min_bounds minExponent (le_refl _) this

/-- `Number.upper q` returns a normalized Number when defined. -/
lemma Number.upper_isNormalized (q : ℚ) (n : Number) (h : Number.upper q = some n) :
    n.isNormalized := by
  unfold Number.upper at h
  split_ifs at h with hq0 hqneg
  · have : n = Number.zero := (Option.some.inj h).symm
    left; exact this
  · rw [Option.map_eq_some_iff] at h
    obtain ⟨n', hn', heq⟩ := h
    have hnq : 0 < -q := by linarith
    have h_n'_right := lowerPosAux_isNormalized_right (-q) hnq n' hn'
    rw [← heq]
    rcases h_n'_right with h_zero | h_bounds
    · rw [h_zero]
      simp only
      left; rfl
    · have h_n'_ne_zero : n' ≠ Number.zero := by
        intro h_eq
        rw [h_eq] at h_bounds
        have h_min : largeRange.min ≤ Number.zero.mantissa_ := h_bounds.1
        rw [UInt64.le_iff_toNat_le] at h_min
        have : largeRange.min.toNat = 10^18 := by decide
        change largeRange.min.toNat ≤ (0 : UInt64).toNat at h_min
        rw [this] at h_min
        simp at h_min
      simp only [if_neg h_n'_ne_zero]
      right
      exact h_bounds
  · have hq_pos : 0 < q := by
      rcases lt_trichotomy q 0 with h1 | h1 | h1
      · exact absurd h1 hqneg
      · exact absurd h1 hq0
      · exact h1
    exact upperPosAux_isNormalized q hq_pos n h

end XRPL.Model.Protocol
