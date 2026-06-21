import XRPL.Properties.Protocol.Number.Common.Int64Lemmas


namespace XRPL.Model.Protocol

/-- `maxRep.toNat = 2^63 - 1 < 2^63`  the internal mantissa always fits in
the positive `Int64` range when below the `maxRep` cusp. -/
private lemma maxRep_lt_2pow63 : maxRep.toNat < 2 ^ 63 := by rw [maxRep_val]; norm_num

/-- The external mantissa view as an integer:
`n.mantissa.toInt = sign · (post-`/10`-or-not internal magnitude)`. -/
lemma Number.mantissa_toInt (n : Number) :
    n.mantissa.toInt =
      (if n.negative_ then -1 else 1) *
        (if n.mantissa_ > maxRep then ((n.mantissa_.toNat / 10 : ℕ) : ℤ) else (n.mantissa_.toNat : ℤ)) := by
  unfold Number.mantissa
  by_cases hcusp : n.mantissa_ > maxRep
  · rw [if_pos hcusp, if_pos hcusp]
    have hdiv_nat : (n.mantissa_ / 10).toNat = n.mantissa_.toNat / 10 := by
      rw [UInt64.toNat_div]; rfl
    have hfit : (n.mantissa_ / 10).toNat < 2 ^ 63 := by
      rw [hdiv_nat]
      have hlt : n.mantissa_.toNat < 2 ^ 64 := n.mantissa_.toNat_lt
      omega
    by_cases hneg : n.negative_
    · rw [if_pos hneg, if_pos hneg]
      rw [UInt64.neg_toInt64_toInt_of_lt _ hfit, hdiv_nat]; push_cast; ring
    · rw [if_neg hneg, if_neg hneg]
      rw [UInt64.toInt64_toInt_of_lt _ hfit, hdiv_nat]; push_cast; ring
  · rw [if_neg hcusp, if_neg hcusp]
    have hle : n.mantissa_.toNat ≤ maxRep.toNat :=
      le_of_not_gt (fun hc => hcusp (UInt64.lt_iff_toNat_lt.mpr hc))
    have hfit : n.mantissa_.toNat < 2 ^ 63 := lt_of_le_of_lt hle maxRep_lt_2pow63
    by_cases hneg : n.negative_
    · rw [if_pos hneg, if_pos hneg]
      rw [UInt64.neg_toInt64_toInt_of_lt _ hfit]; ring
    · rw [if_neg hneg, if_neg hneg]
      rw [UInt64.toInt64_toInt_of_lt _ hfit]; ring

theorem mantissa_mul_exponent_eq_toRat_proof (n : Number) (hn : n.isNormalized) :
    (n.mantissa.toInt : ℚ) * (10 : ℚ) ^ n.exponent = n.toRat := by
  rw [Number.mantissa_toInt n]
  unfold Number.exponent
  by_cases hcusp : n.mantissa_ > maxRep
  · rw [if_pos hcusp, if_pos hcusp]
    -- cusp clause: mantissa_.toNat % 10 = 0, so (m/10)*10 = m
    have hcusp_nat : maxRep.toNat < n.mantissa_.toNat := UInt64.lt_iff_toNat_lt.mp hcusp
    have hmod : n.mantissa_.toNat % 10 = 0 := by
      rcases hn with hz | ⟨_, _, hcuspclause, _, _⟩
      · exfalso; apply absurd hcusp; rw [hz]; decide
      · rcases hcuspclause with hle | hmod
        · exact absurd (UInt64.le_iff_toNat_le.mp hle) (Nat.not_le.mpr hcusp_nat)
        · exact hmod
    have hdiv : (n.mantissa_.toNat / 10) * 10 = n.mantissa_.toNat :=
      Nat.div_mul_cancel (Nat.dvd_of_mod_eq_zero hmod)
    have hdivQ : (↑(n.mantissa_.toNat / 10) : ℚ) * 10 = (n.mantissa_.toNat : ℚ) := by
      have := congrArg (Nat.cast : ℕ → ℚ) hdiv; push_cast at this ⊢; linarith
    have hzpow : (10 : ℚ) ^ (n.exponent_ + 1) = (10 : ℚ) ^ n.exponent_ * 10 :=
      zpow_add_one₀ (by norm_num) _
    by_cases hneg : n.negative_ = false
    · rw [hneg, if_neg (by decide), Number.toRat_of_nonneg n hneg, hzpow, Int.cast_mul,
          Int.cast_one, Int.cast_natCast]
      rw [show (1 : ℚ) * (↑(n.mantissa_.toNat / 10) : ℚ) * ((10 : ℚ) ^ n.exponent_ * 10)
            = (↑(n.mantissa_.toNat / 10) : ℚ) * 10 * (10 : ℚ) ^ n.exponent_ from by ring,
          hdivQ]
    · rw [Bool.not_eq_false] at hneg
      rw [hneg, if_pos (by decide), Number.toRat_of_neg n hneg, hzpow, Int.cast_mul, Int.cast_neg,
          Int.cast_one, Int.cast_natCast]
      rw [show (-1 : ℚ) * (↑(n.mantissa_.toNat / 10) : ℚ) * ((10 : ℚ) ^ n.exponent_ * 10)
            = -((↑(n.mantissa_.toNat / 10) : ℚ) * 10 * (10 : ℚ) ^ n.exponent_) from by ring,
          hdivQ]
  · rw [if_neg hcusp, if_neg hcusp]
    by_cases hneg : n.negative_ = false
    · rw [hneg, if_neg (by decide), Number.toRat_of_nonneg n hneg]
      push_cast; ring
    · rw [Bool.not_eq_false] at hneg
      rw [hneg, if_pos (by decide), Number.toRat_of_neg n hneg]
      push_cast; ring

theorem mantissa_natAbs_le_maxRep_proof (n : Number) (_hn : n.isNormalized) :
    (n.mantissa).toInt.natAbs ≤ maxRep.toNat := by
  rw [Number.mantissa_toInt n, Int.natAbs_mul]
  have hsign : (if n.negative_ then (-1 : ℤ) else 1).natAbs = 1 := by
    cases n.negative_ <;> decide
  rw [hsign, one_mul]
  by_cases hcusp : n.mantissa_ > maxRep
  · rw [if_pos hcusp, Int.natAbs_natCast]
    have hlt : n.mantissa_.toNat < 2 ^ 64 := n.mantissa_.toNat_lt
    rw [maxRep_val]; omega
  · rw [if_neg hcusp, Int.natAbs_natCast]
    exact le_of_not_gt (fun hc => hcusp (UInt64.lt_iff_toNat_lt.mpr hc))

end XRPL.Model.Protocol
