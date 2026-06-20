import XRPL.Properties.Protocol.Number.Common.Defs


namespace XRPL.Model.Protocol

/-! ## Helper lemmas for `operator_mul_rounded_to_nearest` -/

/-- If `n` is the maximal normalized Number with `n.toRat ≤ q`, then
`n.toRat = n_lo.toRat` where `n_lo = Number.lower q`. -/
lemma Number.toRat_eq_lower_of_max (q : ℚ) (n n_lo : Number)
    (h_lower : Number.lower q = some n_lo)
    (h_norm : n.isNormalized) (h_le : n.toRat ≤ q)
    (h_max : ∀ m : Number, m.isNormalized → m.toRat ≤ q → m.toRat ≤ n.toRat) :
    n.toRat = n_lo.toRat := by
  have h_n_lo_norm : n_lo.isNormalized := Number.lower_isNormalized q n_lo h_lower
  have h_n_lo_le : n_lo.toRat ≤ q := Number.lower_le q n_lo h_lower
  have h1 : n.toRat ≤ n_lo.toRat := Number.lower_tight q n_lo h_lower n h_norm h_le
  have h2 : n_lo.toRat ≤ n.toRat := h_max n_lo h_n_lo_norm h_n_lo_le
  linarith

/-- If `n` is the minimal normalized Number with `q ≤ n.toRat`, then
`n.toRat = n_up.toRat` where `n_up = Number.upper q`. -/
lemma Number.toRat_eq_upper_of_min (q : ℚ) (n n_up : Number)
    (h_upper : Number.upper q = some n_up)
    (h_norm : n.isNormalized) (h_ge : q ≤ n.toRat)
    (h_min : ∀ m : Number, m.isNormalized → q ≤ m.toRat → n.toRat ≤ m.toRat) :
    n.toRat = n_up.toRat := by
  have h_n_up_norm : n_up.isNormalized := Number.upper_isNormalized q n_up h_upper
  have h_n_up_ge : q ≤ n_up.toRat := Number.le_upper q n_up h_upper
  have h1 : n_up.toRat ≤ n.toRat := Number.upper_tight q n_up h_upper n h_norm h_ge
  have h2 : n.toRat ≤ n_up.toRat := h_min n_up h_n_up_norm h_n_up_ge
  linarith

/-! ## Nonzero product -/

/-- The product `x.toRat * y.toRat` is nonzero when both mantissas are nonzero
and the inputs are normalized. -/
lemma toRat_mul_ne_zero_of_normalized (x y : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0) :
    x.toRat * y.toRat ≠ 0 := by
  have hx_bounds := hx.mantissaBounds hx_mant_ne
  have hy_bounds := hy.mantissaBounds hy_mant_ne
  have habsx : |x.toRat| > 0 := by
    rw [abs_toRat_eq]
    have hx_min : (10 : ℕ) ^ 18 ≤ x.mantissa_.toNat := (mantissaBounds_nat_of hx_bounds).1
    have h_mant_pos : (0 : ℚ) < (x.mantissa_.toNat : ℚ) := by
      have : (0 : ℕ) < x.mantissa_.toNat := by
        have : (10 : ℕ) ^ 18 > 0 := by norm_num
        omega
      exact_mod_cast this
    have h_pow_pos : (0 : ℚ) < (10 : ℚ) ^ x.exponent_ := zpow_pos (by norm_num) _
    positivity
  have habsy : |y.toRat| > 0 := by
    rw [abs_toRat_eq]
    have hy_min : (10 : ℕ) ^ 18 ≤ y.mantissa_.toNat := (mantissaBounds_nat_of hy_bounds).1
    have h_mant_pos : (0 : ℚ) < (y.mantissa_.toNat : ℚ) := by
      have : (0 : ℕ) < y.mantissa_.toNat := by
        have : (10 : ℕ) ^ 18 > 0 := by norm_num
        omega
      exact_mod_cast this
    have h_pow_pos : (0 : ℚ) < (10 : ℚ) ^ y.exponent_ := zpow_pos (by norm_num) _
    positivity
  intro h
  rw [mul_eq_zero] at h
  rcases h with hx0 | hy0
  · rw [hx0] at habsx; simp at habsx
  · rw [hy0] at habsy; simp at habsy

/-- `scaleDown128` preserves the guard's sign bit (each digit-push keeps `sbit_`).
Shared by the directed-mode `Mul` algorithmic-facts files. -/
lemma scaleDown128_sbit_preserved (M : UInt128) (e : Int) (g0 : Guard) :
    (scaleDown128 M e g0).2.2.sbit_ = g0.sbit_ := by
  induction M, e, g0 using scaleDown128.induct with
  | case1 M e g0 hcond _d IH =>
    have hunfold : scaleDown128 M e g0
        = scaleDown128 (M / 10) (e + 1) (g0.push (toUInt64 (M % 10))) := by
      conv_lhs => rw [scaleDown128]
      simp [hcond]
    rw [hunfold]
    have h_push_sbit : (g0.push (toUInt64 (M % 10))).sbit_ = g0.sbit_ := rfl
    rw [IH, h_push_sbit]
  | case2 M e g0 hcond =>
    unfold scaleDown128
    simp [hcond]

end XRPL.Model.Protocol
