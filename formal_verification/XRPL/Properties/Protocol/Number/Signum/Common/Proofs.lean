import XRPL.Properties.Protocol.Number.Common.ToRatLemmas


namespace XRPL.Model.Protocol

/-- A normalized number is nonzero exactly when its mantissa is nonzero. The
only normalized value with `mantissa_ = 0` is the canonical `Number.zero`
(positive), so a negative normalized number always has a nonzero mantissa. -/
lemma Number.mantissa_ne_zero_of_negative (n : Number) (hn : n.isNormalized)
    (hneg : n.negative_ = true) : n.mantissa_ ≠ 0 := by
  rcases hn with heq | ⟨hlo, _⟩
  · rw [heq] at hneg; exact absurd hneg (by decide)
  · intro hm; rw [hm] at hlo; exact absurd hlo (by decide)


theorem signum_eq_proof (n : Number) (hn : n.isNormalized) :
    n.signum = if 0 < n.toRat then 1 else if n.toRat < 0 then -1 else 0 := by
  unfold Number.signum
  by_cases hneg : n.negative_
  · rw [if_pos hneg]
    have hmant : n.mantissa_ ≠ 0 := Number.mantissa_ne_zero_of_negative n hn hneg
    have hne : n.toRat ≠ 0 := fun h => hmant (Number.toRat_eq_zero_iff.mp h)
    have hlt : n.toRat < 0 := lt_of_le_of_ne (Number.toRat_nonpos_of_negative n hneg) hne
    rw [if_neg (by linarith), if_pos hlt]
  · rw [if_neg hneg]
    by_cases hmant : n.mantissa_ == 0
    · rw [if_neg (by simpa using hmant)]
      have hz : n.toRat = 0 := Number.toRat_eq_zero_iff.mpr (by simpa using hmant)
      rw [if_neg (by rw [hz]; norm_num), if_neg (by rw [hz]; norm_num)]
    · rw [if_pos (by simpa using hmant)]
      have hmant' : n.mantissa_ ≠ 0 := by simpa using hmant
      have hne : n.toRat ≠ 0 := fun h => hmant' (Number.toRat_eq_zero_iff.mp h)
      have hpos : 0 < n.toRat :=
        lt_of_le_of_ne (Number.toRat_nonneg_of_nonnegative n (by simpa using hneg)) (Ne.symm hne)
      rw [if_pos hpos]

theorem signum_eq_one_iff_proof (n : Number) (hn : n.isNormalized) :
    n.signum = 1 ↔ 0 < n.toRat := by
  rw [signum_eq_proof n hn]; split_ifs with h1 h2 <;> simp_all

theorem signum_eq_neg_one_iff_proof (n : Number) (hn : n.isNormalized) :
    n.signum = -1 ↔ n.toRat < 0 := by
  rw [signum_eq_proof n hn]
  split_ifs with h1 h2 <;> simp_all
  all_goals linarith

theorem signum_eq_zero_iff_proof (n : Number) (hn : n.isNormalized) :
    n.signum = 0 ↔ n.toRat = 0 := by
  rw [signum_eq_proof n hn]
  split_ifs with h1 h2 <;> simp_all
  all_goals linarith

end XRPL.Model.Protocol
