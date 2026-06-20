import XRPL.Properties.Protocol.Number.Common.Notation
import XRPL.Properties.Protocol.Number.Common.Closest.Normalize


namespace XRPL.Model.Protocol

/-- For q > 0, lowerPosAux output has toRat ≤ q. -/
lemma lowerPosAux_le (q : ℚ) (hq : 0 < q) (n : Number)
    (h : lowerPosAux q = some n) : n.toRat ≤ q := by
  have h_real_bounds := m_real_bounds q hq
  unfold lowerPosAux at h
  simp only at h
  set e_q : ℤ := Int.log 10 q - mantissaLog with he_q_def
  set m_real : ℚ := q * (10 : ℚ)^(-e_q) with hm_real_def
  set m_floor_nat : ℕ := ⌊m_real⌋₊ with hm_floor_def
  have h_pow_pos : (0 : ℚ) < (10 : ℚ) ^ e_q := zpow_pos (by norm_num) _
  have h_real_nn : (0 : ℚ) ≤ m_real := by
    have : (0 : ℚ) < (10 : ℚ) ^ (18 : ℕ) := by positivity
    linarith [h_real_bounds.1]
  have h_floor_ge : (10 ^ 18 : ℕ) ≤ m_floor_nat := by
    have : ((10^18 : ℕ) : ℚ) ≤ m_real := by push_cast; exact h_real_bounds.1
    exact Nat.le_floor this
  have h_floor_lt : m_floor_nat < 10 ^ 19 := by
    have h_floor_le : (m_floor_nat : ℚ) ≤ m_real := Nat.floor_le h_real_nn
    have : (m_floor_nat : ℚ) < ((10^19 : ℕ) : ℚ) := by
      push_cast; linarith [h_real_bounds.2]
    exact_mod_cast this
  set m := truncToValidMantissa m_floor_nat with hm_def
  have h_m_le_floor : m ≤ m_floor_nat := truncToValidMantissa_le m_floor_nat
  have h_q_eq : q = m_real * (10 : ℚ) ^ e_q := by
    rw [hm_real_def, mul_assoc, ← zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0)]
    rw [show (-e_q + e_q : ℤ) = 0 from by ring]; simp
  split at h
  · rename_i hpos
    split at h
    · rename_i hexp
      have hn_eq : n = ⟨false, ⟨⟨⟨m, hpos.2⟩⟩⟩, e_q⟩ := (Option.some.inj h).symm
      rw [hn_eq]
      have h_nn : (⟨false, ⟨⟨⟨m, hpos.2⟩⟩⟩, e_q⟩ : Number).negative_ = false := rfl
      rw [Number.toRat_of_nonneg _ h_nn]
      change ((⟨⟨⟨m, hpos.2⟩⟩⟩ : UInt64).toNat : ℚ) * (10 : ℚ) ^ e_q ≤ q
      have h_toNat : ((⟨⟨⟨m, hpos.2⟩⟩⟩ : UInt64)).toNat = m := rfl
      rw [h_toNat]
      rw [h_q_eq]
      apply mul_le_mul_of_nonneg_right _ (le_of_lt h_pow_pos)
      have h_floor_le : (m_floor_nat : ℚ) ≤ m_real := Nat.floor_le h_real_nn
      have : (m : ℚ) ≤ (m_floor_nat : ℚ) := by exact_mod_cast h_m_le_floor
      linarith
    · split at h
      · have hn_eq : n = Number.zero := (Option.some.inj h).symm
        rw [hn_eq, Number.toRat_zero]; linarith
      · exact absurd h (by simp)
  · rename_i _hpos_neg
    split at h
    · rename_i hexp
      have hn_eq : n = ⟨false, ⟨maxMul10Witness, by decide⟩, e_q - 1⟩ :=
        (Option.some.inj h).symm
      rw [hn_eq]
      have h_nn : (⟨false, ⟨maxMul10Witness, by decide⟩, e_q - 1⟩ : Number).negative_ = false :=
        rfl
      rw [Number.toRat_of_nonneg _ h_nn]
      change ((⟨maxMul10Witness, by decide⟩ : UInt64).toNat : ℚ) * (10 : ℚ) ^ (e_q - 1) ≤ q
      have h_toNat : ((⟨maxMul10Witness, by decide⟩ : UInt64)).toNat = maxMul10Witness := rfl
      rw [h_toNat]
      have h_lt_pow : (maxMul10Witness : ℚ) < (10 : ℚ) ^ (19 : ℕ) := by norm_num
      have h_pow_pos_em1 : (0 : ℚ) < (10 : ℚ) ^ (e_q - 1) := zpow_pos (by norm_num) _
      have h_pow_eq : (10 : ℚ) ^ (19 : ℕ) * (10 : ℚ) ^ (e_q - 1) = (10 : ℚ) ^ (Int.log 10 q) := by
        rw [← zpow_natCast (10 : ℚ) 19, ← zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0)]
        congr 1; rw [he_q_def]; push_cast; ring
      have h_log_le : (10 : ℚ) ^ Int.log 10 q ≤ q := Int.zpow_log_le_self (by norm_num) hq
      have h_intermediate : (maxMul10Witness : ℚ) * (10 : ℚ) ^ (e_q - 1)
          ≤ (10 : ℚ) ^ (19 : ℕ) * (10 : ℚ) ^ (e_q - 1) := by
        apply mul_le_mul_of_nonneg_right (le_of_lt h_lt_pow) (le_of_lt h_pow_pos_em1)
      have h_cast : ((maxMul10Witness : ℕ) : ℚ) = (maxMul10Witness : ℚ) := by norm_cast
      rw [h_cast]
      linarith
    · split at h
      · have hn_eq : n = Number.zero := (Option.some.inj h).symm
        rw [hn_eq, Number.toRat_zero]; linarith
      · exact absurd h (by simp)

/-- For q > 0, upperPosAux output has toRat ≥ q. -/
lemma upperPosAux_ge (q : ℚ) (hq : 0 < q) (n : Number)
    (h : upperPosAux q = some n) : q ≤ n.toRat := by
  have h_ceil_bounds := m_ceil_nat_bounds q hq
  have h_real_bounds := m_real_bounds q hq
  unfold upperPosAux at h
  simp only at h
  set e_q : ℤ := Int.log 10 q - mantissaLog with he_q_def
  set m_real : ℚ := q * (10 : ℚ)^(-e_q) with hm_real_def
  set m_ceil_nat : ℕ := ⌈m_real⌉₊ with hm_ceil_def
  have h_pow_pos : (0 : ℚ) < (10 : ℚ) ^ e_q := zpow_pos (by norm_num) _
  have h_corner : ∀ (h_corner_cond : e_q < minExponent),
      q ≤ (⟨false, largeRange.min, minExponent⟩ : Number).toRat := by
    intro h_corner_cond
    have h_nn : (⟨false, largeRange.min, minExponent⟩ : Number).negative_ = false := rfl
    rw [Number.toRat_of_nonneg _ h_nn]
    change q ≤ (largeRange.min.toNat : ℚ) * (10 : ℚ) ^ minExponent
    have hmin : (largeRange.min.toNat : ℚ) = (10 : ℚ) ^ (18 : ℕ) := by
      have : largeRange.min.toNat = 10 ^ 18 := by decide
      rw [this]; push_cast; ring
    rw [hmin]
    have h_zpow_eq : (10 : ℚ) ^ (18 : ℕ) * (10 : ℚ) ^ (minExponent : ℤ) = (10 : ℚ) ^ ((minExponent : ℤ) + 18) := by
      rw [← zpow_natCast (10 : ℚ) 18, ← zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0)]
      ring_nf
    rw [h_zpow_eq]
    have h_log_succ : Int.log 10 q + 1 ≤ minExponent + 18 := by
      have : Int.log 10 q - mantissaLog < minExponent := by rw [he_q_def] at h_corner_cond; exact h_corner_cond
      linarith
    have h_pow_mono : (10 : ℚ) ^ (Int.log 10 q + 1) ≤ (10 : ℚ) ^ ((minExponent : ℤ) + 18) :=
      zpow_le_zpow_right₀ (by norm_num) h_log_succ
    have h_q_lt : q < (10 : ℚ) ^ (Int.log 10 q + 1) := Int.lt_zpow_succ_log_self (by norm_num) q
    linarith
  rcases h_bump_eq : bumpToValidMantissa m_ceil_nat with _ | m
  · rw [h_bump_eq] at h
    simp only at h
    split_ifs at h with hexp hcorner
    · have hn_eq : n = ⟨false, largeRange.min, e_q + 1⟩ :=
        (Option.some.inj h).symm
      rw [hn_eq]
      have h_nn : (⟨false, largeRange.min, e_q + 1⟩ : Number).negative_ = false := rfl
      rw [Number.toRat_of_nonneg _ h_nn]
      change q ≤ (largeRange.min.toNat : ℚ) * (10 : ℚ) ^ (e_q + 1)
      have hmin : (largeRange.min.toNat : ℚ) = (10 : ℚ) ^ (18 : ℕ) := by
        have : largeRange.min.toNat = 10 ^ 18 := by decide
        rw [this]; push_cast; ring
      rw [hmin]
      have h_zpow_eq : (10 : ℚ) ^ (18 : ℕ) * (10 : ℚ) ^ (e_q + 1) = (10 : ℚ) ^ (e_q + 19) := by
        rw [← zpow_natCast (10 : ℚ) 18, ← zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0)]
        congr 1; push_cast; ring
      rw [h_zpow_eq]
      have h_eq2 : e_q + 19 = Int.log 10 q + 1 := by rw [he_q_def]; ring
      rw [h_eq2]
      exact le_of_lt (Int.lt_zpow_succ_log_self (by norm_num) q)
    · have hn_eq : n = ⟨false, largeRange.min, minExponent⟩ := (Option.some.inj h).symm
      rw [hn_eq]
      have h_corner_cond : e_q < minExponent := by linarith
      exact h_corner h_corner_cond
  · rw [h_bump_eq] at h
    simp only at h
    have h_m_lt : m < 10 ^ 19 := bumpToValidMantissa_lt_pow19 m_ceil_nat m h_bump_eq
    have h_m_ge : m_ceil_nat ≤ m := bumpToValidMantissa_ge m_ceil_nat m h_bump_eq
    split_ifs at h with hexp hcorner
    · have hn_eq : n = ⟨false, ⟨⟨⟨m, hexp.2.2⟩⟩⟩, e_q⟩ :=
        (Option.some.inj h).symm
      rw [hn_eq]
      have h_nn : (⟨false, ⟨⟨⟨m, hexp.2.2⟩⟩⟩, e_q⟩ : Number).negative_ = false := rfl
      rw [Number.toRat_of_nonneg _ h_nn]
      change q ≤ ((⟨⟨⟨m, hexp.2.2⟩⟩⟩ : UInt64).toNat : ℚ) * (10 : ℚ) ^ e_q
      have h_toNat : ((⟨⟨⟨m, hexp.2.2⟩⟩⟩ : UInt64)).toNat = m := rfl
      rw [h_toNat]
      have h_q_eq : q = m_real * (10 : ℚ) ^ e_q := by
        rw [hm_real_def]
        rw [mul_assoc, ← zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0)]
        rw [show (-e_q + e_q : ℤ) = 0 from by ring]
        simp
      rw [h_q_eq]
      apply mul_le_mul_of_nonneg_right _ (le_of_lt h_pow_pos)
      have h_m_real_le_ceil : m_real ≤ (m_ceil_nat : ℚ) := Nat.le_ceil _
      have h_ceil_le_m : (m_ceil_nat : ℚ) ≤ (m : ℚ) := by exact_mod_cast h_m_ge
      linarith
    · have hn_eq : n = ⟨false, largeRange.min, minExponent⟩ := (Option.some.inj h).symm
      rw [hn_eq]
      exact h_corner hcorner

/-- `Number.upper q` is at least `q`. -/
lemma Number.le_upper (q : ℚ) (n : Number) (h : Number.upper q = some n) :
    q ≤ n.toRat := by
  unfold Number.upper at h
  split_ifs at h with hq0 hqneg
  · have : n = Number.zero := (Option.some.inj h).symm
    rw [this, hq0, Number.toRat_zero]
  · rw [Option.map_eq_some_iff] at h
    obtain ⟨n', hn', heq⟩ := h
    have hnq : 0 < -q := by linarith
    have h_n'_le : n'.toRat ≤ -q := lowerPosAux_le (-q) hnq n' hn'
    rw [← heq]
    by_cases h_n'_zero : n' = Number.zero
    · rw [h_n'_zero]
      simp only [if_true, Number.toRat_zero]
      linarith
    · simp only [if_neg h_n'_zero]
      have h_n_neg : ({ n' with negative_ := true } : Number).negative_ = true := rfl
      rw [Number.toRat_of_neg _ h_n_neg]
      change q ≤ -((n'.mantissa_.toNat : ℚ) * (10 : ℚ) ^ n'.exponent_)
      -- lowerPosAux always produces negative_ = false
      have h_n'_neg : n'.negative_ = false := by
        unfold lowerPosAux at hn'
        simp only at hn'
        split at hn'
        · split at hn'
          · exact congrArg (·.negative_) (Option.some.inj hn').symm
          · split at hn'
            · exact congrArg (·.negative_) (Option.some.inj hn').symm
            · exact absurd hn' (by simp)
        · split at hn'
          · exact congrArg (·.negative_) (Option.some.inj hn').symm
          · split at hn'
            · exact congrArg (·.negative_) (Option.some.inj hn').symm
            · exact absurd hn' (by simp)
      rw [Number.toRat_of_nonneg _ h_n'_neg] at h_n'_le
      linarith
  · have hq_pos : 0 < q := by
      rcases lt_trichotomy q 0 with h1 | h1 | h1
      · exact absurd h1 hqneg
      · exact absurd h1 hq0
      · exact h1
    exact upperPosAux_ge q hq_pos n h

end XRPL.Model.Protocol
