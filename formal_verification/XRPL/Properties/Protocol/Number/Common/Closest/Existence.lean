import XRPL.Properties.Protocol.Number.Common.Notation
import XRPL.Properties.Protocol.Number.Common.Closest.Tightness


namespace XRPL.Model.Protocol


/-! ## Existence of `Number.lower` and `Number.upper` (positive case)

These lemmas show `Number.lower q` and `Number.upper q` are defined when
`q > 0` is bracketed by normalized witnesses constraining `e_q` to
`[minExponent, maxExponent]`. -/

/-- `Number.lower q` succeeds given positive normalized lower and upper witnesses. -/
lemma Number.lower_some_of_pos_witnesses (q : ℚ) (hq : 0 < q)
    (n_lo : Number) (h_lo_norm : n_lo.isNormalized)
    (h_lo_pos : 0 < n_lo.toRat) (h_lo_le : n_lo.toRat ≤ q)
    (n_hi : Number) (h_hi_norm : n_hi.isNormalized)
    (h_hi_ge : q ≤ n_hi.toRat) :
    ∃ n, Number.lower q = some n := by
  have hq_ne : q ≠ 0 := ne_of_gt hq
  have hq_not_neg : ¬ (q < 0) := not_lt.mpr (le_of_lt hq)
  unfold Number.lower
  rw [if_neg hq_ne, if_neg hq_not_neg]
  have h_lo_neg : n_lo.negative_ = false := by
    by_contra hh
    have hh' : n_lo.negative_ = true := by
      cases h : n_lo.negative_
      · exact absurd h hh
      · rfl
    have : n_lo.toRat ≤ 0 := Number.toRat_nonpos_of_negative n_lo hh'
    linarith
  have h_lo_zero_ne : n_lo ≠ Number.zero := by
    intro h_eq
    have h_zero : n_lo.toRat = 0 := by rw [h_eq, Number.toRat_zero]
    linarith
  have h_e_lo : n_lo.exponent_ ≤ Int.log 10 q - mantissaLog :=
    exponent_le_of_toRat_le_pos q hq n_lo h_lo_norm h_lo_neg h_lo_zero_ne h_lo_le
  have h_e_hi : Int.log 10 q - mantissaLog ≤ n_hi.exponent_ :=
    exponent_ge_of_toRat_ge q hq n_hi h_hi_norm h_hi_ge
  rcases h_lo_norm with hz | ⟨_, _, _, h_lo_emin, _⟩
  · exact absurd hz h_lo_zero_ne
  have h_hi_zero_ne : n_hi ≠ Number.zero := by
    intro h_eq
    have h_zero : n_hi.toRat = 0 := by rw [h_eq, Number.toRat_zero]
    linarith
  rcases h_hi_norm with hz | ⟨_, _, _, _, h_hi_emax⟩
  · exact absurd hz h_hi_zero_ne
  have h_e_q_min : minExponent ≤ Int.log 10 q - mantissaLog := le_trans h_lo_emin h_e_lo
  have h_e_q_max : Int.log 10 q - mantissaLog ≤ maxExponent := le_trans h_e_hi h_hi_emax
  have h_real_bounds := m_real_bounds q hq
  unfold lowerPosAux
  simp only
  set e_q : ℤ := Int.log 10 q - mantissaLog with he_q_def
  set m_real : ℚ := q * (10 : ℚ)^(-e_q) with hm_real_def
  set m_floor_nat : ℕ := ⌊m_real⌋₊ with hm_floor_def
  have h_real_nn : (0 : ℚ) ≤ m_real := by
    have : (0 : ℚ) < (10 : ℚ) ^ (18 : ℕ) := by positivity
    linarith [h_real_bounds.1]
  have h_floor_ge : (10^18 : ℕ) ≤ m_floor_nat := by
    have : ((10^18 : ℕ) : ℚ) ≤ m_real := by push_cast; exact h_real_bounds.1
    exact Nat.le_floor this
  have h_floor_lt : m_floor_nat < 10^19 := by
    have h_floor_le : (m_floor_nat : ℚ) ≤ m_real := Nat.floor_le h_real_nn
    have : (m_floor_nat : ℚ) < ((10^19 : ℕ) : ℚ) := by
      push_cast; linarith [h_real_bounds.2]
    exact_mod_cast this
  set m_tr := truncToValidMantissa m_floor_nat with hm_tr_def
  have h_maxR : maxRep.toNat = maxRepNat := maxRep_val
  have h_cusp_v : cuspMin = maxRepCuspTarget := rfl
  have h_min_eq : largeRange.min.toNat = 10^18 := by decide
  have h_pow18_v : (10 : ℕ)^18 = 1000000000000000000 := by decide
  have h_pow19_v : (10 : ℕ)^19 = tenPow19 := by decide
  have h_pow64 : (2 : ℕ)^64 = twoPow64 := by decide
  have h_tr_ge_pow18 : (10^18 : ℕ) ≤ m_tr := by
    by_cases h1 : m_floor_nat ≤ maxRep.toNat
    · have h_eq : truncToValidMantissa m_floor_nat = m_floor_nat := by
        unfold truncToValidMantissa; rw [if_pos h1]
      rw [hm_tr_def, h_eq]; exact h_floor_ge
    · push_neg at h1
      by_cases h2 : m_floor_nat < cuspMin
      · have h_eq : truncToValidMantissa m_floor_nat = maxRep.toNat := by
          unfold truncToValidMantissa
          rw [if_neg (by omega), if_pos h2]
        rw [hm_tr_def, h_eq]; omega
      · push_neg at h2
        have h_eq : truncToValidMantissa m_floor_nat = (m_floor_nat / 10) * 10 := by
          unfold truncToValidMantissa
          rw [if_neg (by omega), if_neg (by omega)]
        rw [hm_tr_def, h_eq]
        have h_div_ge : cuspMin / 10 ≤ m_floor_nat / 10 := Nat.div_le_div_right h2
        have h_cusp_div_v : cuspMin / 10 = mantissaFloorSucc := cuspMin_div_ten
        have h_lower : mantissaFloorSucc ≤ m_floor_nat / 10 := by omega
        have h_eq2 : mantissaFloorSucc * 10 = maxRepCuspTarget := by decide
        have h_mul : maxRepCuspTarget ≤ m_floor_nat / 10 * 10 := by
          calc maxRepCuspTarget = mantissaFloorSucc * 10 := h_eq2.symm
            _ ≤ m_floor_nat / 10 * 10 := Nat.mul_le_mul_right 10 h_lower
        omega
  have h_tr_le_floor : m_tr ≤ m_floor_nat := truncToValidMantissa_le m_floor_nat
  have h_tr_lt_pow19 : m_tr < 10^19 := by omega
  have h_tr_lt_64 : m_tr < 2^64 := by omega
  have h_tr_ge_min : largeRange.min.toNat ≤ m_tr := by rw [h_min_eq]; exact h_tr_ge_pow18
  rw [dif_pos ⟨h_tr_ge_min, h_tr_lt_64⟩]
  rw [if_pos ⟨h_e_q_min, h_e_q_max⟩]
  exact ⟨_, rfl⟩

lemma Number.upper_some_of_pos_witnesses (q : ℚ) (hq : 0 < q)
    (n_lo : Number) (h_lo_norm : n_lo.isNormalized)
    (h_lo_pos : 0 < n_lo.toRat) (h_lo_le : n_lo.toRat ≤ q)
    (n_hi : Number) (h_hi_norm : n_hi.isNormalized)
    (h_hi_ge : q ≤ n_hi.toRat) :
    ∃ n, Number.upper q = some n := by
  have hq_ne : q ≠ 0 := ne_of_gt hq
  have hq_not_neg : ¬ (q < 0) := not_lt.mpr (le_of_lt hq)
  unfold Number.upper
  rw [if_neg hq_ne, if_neg hq_not_neg]
  have h_lo_neg : n_lo.negative_ = false := by
    by_contra hh
    have hh' : n_lo.negative_ = true := by
      cases h : n_lo.negative_
      · exact absurd h hh
      · rfl
    have : n_lo.toRat ≤ 0 := Number.toRat_nonpos_of_negative n_lo hh'
    linarith
  have h_lo_zero_ne : n_lo ≠ Number.zero := by
    intro h_eq
    have h_zero : n_lo.toRat = 0 := by rw [h_eq, Number.toRat_zero]
    linarith
  have h_hi_zero_ne : n_hi ≠ Number.zero := by
    intro h_eq
    have h_zero : n_hi.toRat = 0 := by rw [h_eq, Number.toRat_zero]
    linarith
  have h_hi_neg : n_hi.negative_ = false := by
    by_contra hh
    have hh' : n_hi.negative_ = true := by
      cases h : n_hi.negative_
      · exact absurd h hh
      · rfl
    have : n_hi.toRat ≤ 0 := Number.toRat_nonpos_of_negative n_hi hh'
    linarith
  have h_e_lo : n_lo.exponent_ ≤ Int.log 10 q - mantissaLog :=
    exponent_le_of_toRat_le_pos q hq n_lo h_lo_norm h_lo_neg h_lo_zero_ne h_lo_le
  have h_e_hi : Int.log 10 q - mantissaLog ≤ n_hi.exponent_ :=
    exponent_ge_of_toRat_ge q hq n_hi h_hi_norm h_hi_ge
  rcases h_lo_norm with hz | ⟨_, _, _, h_lo_emin, _⟩
  · exact absurd hz h_lo_zero_ne
  rcases h_hi_norm with hz | ⟨h_hi_min, h_hi_max, h_hi_v, _, h_hi_emax⟩
  · exact absurd hz h_hi_zero_ne
  have h_e_q_min : minExponent ≤ Int.log 10 q - mantissaLog := le_trans h_lo_emin h_e_lo
  have h_e_q_max : Int.log 10 q - mantissaLog ≤ maxExponent := le_trans h_e_hi h_hi_emax
  have h_hi_min_nat : (10^18 : ℕ) ≤ n_hi.mantissa_.toNat := by
    have h := h_hi_min
    rw [UInt64.le_iff_toNat_le] at h
    have hmin_eq : largeRange.min.toNat = 10^18 := by decide
    omega
  have h_hi_max_nat : n_hi.mantissa_.toNat ≤ (10^19 - 1 : ℕ) := by
    have h := h_hi_max
    rw [UInt64.le_iff_toNat_le] at h
    have hmax_eq : largeRange.max.toNat = 10^19 - 1 := by decide
    omega
  have h_hi_lt : n_hi.mantissa_.toNat < 10^19 := by omega
  have h_hi_valid : n_hi.mantissa_.toNat ≤ maxRep.toNat ∨ n_hi.mantissa_.toNat % 10 = 0 := by
    rcases h_hi_v with hv | hv
    · left; rw [← UInt64.le_iff_toNat_le]; exact hv
    · right; exact hv
  have h_real_bounds := m_real_bounds q hq
  have h_ceil_bounds := m_ceil_nat_bounds q hq
  unfold upperPosAux
  simp only
  set e_q : ℤ := Int.log 10 q - mantissaLog with he_q_def
  set m_real : ℚ := q * (10 : ℚ)^(-e_q) with hm_real_def
  set m_ceil_nat : ℕ := ⌈m_real⌉₊ with hm_ceil_def
  have h_pow_pos_eq : (0 : ℚ) < (10 : ℚ) ^ e_q := zpow_pos (by norm_num) _
  have h_q_eq : q = m_real * (10 : ℚ) ^ e_q := by
    rw [hm_real_def, mul_assoc, ← zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0)]
    rw [show (-e_q + e_q : ℤ) = 0 from by ring]; simp
  rcases h_bump_eq : bumpToValidMantissa m_ceil_nat with _ | m_b
  · have h_ceil_lower : m_ceil_nat ≥ 9999999999999999991 := by
      unfold bumpToValidMantissa at h_bump_eq
      by_cases hb1 : m_ceil_nat ≤ maxRep.toNat
      · rw [if_pos hb1] at h_bump_eq; exact absurd h_bump_eq (by simp)
      rw [if_neg hb1] at h_bump_eq
      by_cases hb2 : m_ceil_nat < cuspMin
      · rw [if_pos hb2] at h_bump_eq; exact absurd h_bump_eq (by simp)
      rw [if_neg hb2] at h_bump_eq
      simp only at h_bump_eq
      by_cases hb3 : ((m_ceil_nat + 9) / 10) * 10 < 10^19
      · rw [if_pos hb3] at h_bump_eq; exact absurd h_bump_eq (by simp)
      push_neg at hb3
      have h_div_eq : 10 * ((m_ceil_nat + 9) / 10) + (m_ceil_nat + 9) % 10 = m_ceil_nat + 9 :=
        Nat.div_add_mod (m_ceil_nat + 9) 10
      have h_mod_lt : (m_ceil_nat + 9) % 10 < 10 := Nat.mod_lt _ (by omega)
      omega
    have h_e_hi_strict : e_q + 1 ≤ n_hi.exponent_ := by
      by_contra h_not
      push_neg at h_not
      have h_e_hi_eq : n_hi.exponent_ = e_q := by omega
      have h_hi_toRat : n_hi.toRat = (n_hi.mantissa_.toNat : ℚ) * (10 : ℚ) ^ n_hi.exponent_ :=
        Number.toRat_of_nonneg n_hi h_hi_neg
      have h_hi_toRat' : (n_hi.mantissa_.toNat : ℚ) * (10 : ℚ) ^ e_q ≥ q := by
        rw [← h_e_hi_eq, ← h_hi_toRat]; exact h_hi_ge
      rw [h_q_eq] at h_hi_toRat'
      have h_mant_ge_real : m_real ≤ (n_hi.mantissa_.toNat : ℚ) :=
        le_of_mul_le_mul_right h_hi_toRat' h_pow_pos_eq
      have h_mant_ge_ceil : m_ceil_nat ≤ n_hi.mantissa_.toNat := Nat.ceil_le.mpr h_mant_ge_real
      have h_mant_gt_maxR : n_hi.mantissa_.toNat > maxRep.toNat := by
        have h_maxR : maxRep.toNat = maxRepNat := maxRep_val
        omega
      have h_mant_mod : n_hi.mantissa_.toNat % 10 = 0 := by
        rcases h_hi_valid with hv | hv
        · omega
        · exact hv
      omega
    have h_inner : minExponent ≤ e_q + 1 ∧ e_q + 1 ≤ maxExponent := by
      refine ⟨?_, ?_⟩
      · linarith
      · linarith
    refine ⟨(⟨false, largeRange.min, e_q + 1⟩ : Number), ?_⟩
    change (if h_exp : minExponent ≤ e_q + 1 ∧ e_q + 1 ≤ maxExponent then
            some (⟨false, largeRange.min, e_q + 1⟩ : Number)
          else if e_q + 1 < minExponent then
            some (⟨false, largeRange.min, minExponent⟩ : Number)
          else none) = _
    rw [dif_pos h_inner]
  · have h_m_b_lt : m_b < 10^19 := bumpToValidMantissa_lt_pow19 m_ceil_nat m_b h_bump_eq
    have h_pow19_v : (10 : ℕ)^19 = tenPow19 := by decide
    have h_pow64 : (2 : ℕ)^64 = twoPow64 := by decide
    have h_m_b_lt_64 : m_b < 2^64 := by omega
    have h_inner : minExponent ≤ e_q ∧ e_q ≤ maxExponent ∧ m_b < 2^64 :=
      ⟨h_e_q_min, h_e_q_max, h_m_b_lt_64⟩
    refine ⟨(⟨false, ⟨m_b, h_m_b_lt_64⟩, e_q⟩ : Number), ?_⟩
    change (if h_exp : minExponent ≤ e_q ∧ e_q ≤ maxExponent ∧ m_b < 2^64 then
            some (⟨false, ⟨m_b, h_exp.2.2⟩, e_q⟩ : Number)
          else if e_q < minExponent then
            some (⟨false, largeRange.min, minExponent⟩ : Number)
          else none) = _
    rw [dif_pos h_inner]

/-- `lowerPosAux` is total below the lattice top: for any positive `q` below
`10^19 · 10^maxExponent`, it returns `some`. Unlike
`Number.lower_some_of_pos_witnesses` this needs no upper witness — values in
`(maxMul10Witness · 10^maxExponent, 10^19 · 10^maxExponent)` have no
representable above them, yet their `lower` is still defined. -/
lemma lowerPosAux_isSome_of_lt_top (q : ℚ) (hq : 0 < q)
    (h_top : q < 10 ^ 19 * (10 : ℚ) ^ (maxExponent : ℤ)) :
    ∃ n, lowerPosAux q = some n := by
  have h_log_le : Int.log 10 q ≤ maxExponent + 18 := by
    by_contra h_not
    push_neg at h_not
    have h_log_ge : maxExponent + 19 ≤ Int.log 10 q := by omega
    have h_pow_mono : (10 : ℚ) ^ (maxExponent + 19) ≤ (10 : ℚ) ^ (Int.log 10 q) :=
      zpow_le_zpow_right₀ (by norm_num) h_log_ge
    have h_log_self : (10 : ℚ) ^ (Int.log 10 q) ≤ q :=
      Int.zpow_log_le_self (by norm_num : (1 : ℕ) < 10) hq
    have h_pow_form : (10 : ℚ) ^ 19 * (10 : ℚ) ^ (maxExponent : ℤ)
        = (10 : ℚ) ^ (maxExponent + 19 : ℤ) := by
      rw [show (maxExponent + 19 : ℤ) = (19 : ℕ) + maxExponent from by push_cast; ring,
          zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0), zpow_natCast]
    rw [h_pow_form] at h_top
    linarith [le_trans h_pow_mono h_log_self]
  have h_e_q_le : Int.log 10 q - mantissaLog ≤ maxExponent := by omega
  unfold lowerPosAux
  simp only []
  split_ifs with h1 h2 h3 h4 h5 <;> first
    | exact ⟨_, rfl⟩
    | (exfalso; omega)

end XRPL.Model.Protocol
