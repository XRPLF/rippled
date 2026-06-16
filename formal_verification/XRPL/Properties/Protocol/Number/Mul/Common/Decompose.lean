import XRPL.Properties.Protocol.Number.Common.Notation
import XRPL.Properties.Protocol.Number.Common.Helpers
import XRPL.Properties.Protocol.Number.Common.Rounding.DoRoundUp

set_option linter.style.longLine false
set_option linter.style.emptyLine false

namespace XRPL.Model.Protocol

structure MulDecomp (x y result : Number) (mode : rounding_mode)
    (zn : Bool) (zm : UInt64) (ze' : Int) (f : ℚ) (g : Guard) (res : RoundResult) : Prop where
  neg_eq : zn = (x.negative_ != y.negative_)
  zm_ge_floorSucc : mantissaFloorSucc ≤ zm.toNat
  zm_le_maxRepUp : zm.toNat ≤ maxRepUp.toNat
  f_nonneg : 0 ≤ f
  f_lt_one : f < 1
  floor_cusp : zm.toNat = mantissaFloor → (8 : ℚ) / 10 ≤ f
  value_eq : |x.toRat * y.toRat| = ((zm.toNat : ℚ) + f) * 10 ^ ze'
  represents_f : represents g f
  sbit_eq : g.sbit_ = (x.negative_ != y.negative_)
  rounds : g.doRoundUp zn zm ze' largeRange.min largeRange.max mode
    "Number::multiplication overflow" = .ok res
  normalizes : res.toNumber.normalize largeRange.min largeRange.max mode = .ok result

theorem operator_mul_decompose (x y result : Number) (mode : rounding_mode)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (hok : Number.operator_mul x y mode = .ok result) :
    ∃ (zn : Bool) (zm : UInt64) (ze' : Int) (f : ℚ) (g : Guard) (res : RoundResult),
      MulDecomp x y result mode zn zm ze' f g res := by
  have hx_bounds := hx.mantissaBounds hx_mant_ne
  have hy_bounds := hy.mantissaBounds hy_mant_ne
  have hx_mant_pos := mantissa_toNat_pos_of_bounds hx_bounds
  have hy_mant_pos := mantissa_toNat_pos_of_bounds hy_bounds
  have hx_ne_zero := Number.not_operator_eq_zero_of_mantissa_ne hx_mant_ne
  have hy_ne_zero := Number.not_operator_eq_zero_of_mantissa_ne hy_mant_ne
  unfold Number.operator_mul at hok
  simp only [hx_ne_zero, hy_ne_zero, Bool.false_eq_true, if_false] at hok
  set zn : Bool := x.negative_ != y.negative_ with hzn_def
  set zm128 : UInt128 := toUInt128 x.mantissa_ * toUInt128 y.mantissa_ with hzm128_def
  set ze : Int := x.exponent_ + y.exponent_ with hze_def
  set g0 : Guard := if zn then Guard.new.set_negative else Guard.new with hg0_def
  set sd_result : UInt64 × Int × Guard := scaleDown128 zm128 ze g0 with hsd_def
  set zm : UInt64 := sd_result.1 with hzm_def
  set ze' : Int := sd_result.2.1 with hze'_def
  set g : Guard := sd_result.2.2 with hg_def
  have hg0_rep : represents g0 0 := by
    change represents (if zn then Guard.new.set_negative else Guard.new) 0
    exact g0_represents_zero zn
  have h_sd_correct := scaleDown128_correct zm128 ze g0 0 hg0_rep
  simp only at h_sd_correct
  rw [← hsd_def] at h_sd_correct
  have h_sd_split : sd_result = (zm, ze', g) := by rw [hzm_def, hze'_def, hg_def]
  rw [h_sd_split] at h_sd_correct
  simp only at h_sd_correct
  obtain ⟨k, hk_eq, hzm_le_maxRep, hzm128_decomp, hg_rep, hfloor⟩ := h_sd_correct
  have hx_mant_bound : x.mantissa_.toNat < 10 ^ 19 := (mantissaBounds_nat_of hx_bounds).2
  have hy_mant_bound : y.mantissa_.toNat < 10 ^ 19 := (mantissaBounds_nat_of hy_bounds).2
  have h_prod_fit : x.mantissa_.toNat * y.mantissa_.toNat < 2 ^ 128 :=
    nat_mul_lt_two_pow_128 hx_mant_bound hy_mant_bound
  have hzm128_toNat : zm128.toNat = x.mantissa_.toNat * y.mantissa_.toNat := by
    rw [hzm128_def]
    exact uint128_of_uint64_mul_toNat _ _ h_prod_fit
  have habs_xy_eq : |x.toRat * y.toRat|
      = (x.mantissa_.toNat : ℚ) * (y.mantissa_.toNat : ℚ) * 10 ^ ze := by
    rw [abs_mul, abs_toRat_eq, abs_toRat_eq, hze_def]
    rw [zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0)]
    ring
  set f : ℚ := ((zm128.toNat % 10 ^ k : ℕ) : ℚ) / 10 ^ k with hf_def
  have h10k_pos : (0 : ℚ) < 10 ^ k := by positivity
  have hf_rep : represents g f := by
    have : (0 + ((zm128.toNat % 10 ^ k : ℕ) : ℚ)) / 10 ^ k = f := by rw [hf_def]; ring
    rw [← this]; exact hg_rep
  have habs_xy_eq2 : |x.toRat * y.toRat| = ((zm.toNat : ℚ) + f) * 10 ^ ze' := by
    rw [habs_xy_eq, hk_eq]
    have hzm128_q : ((x.mantissa_.toNat : ℚ) * (y.mantissa_.toNat : ℚ))
        = (zm.toNat : ℚ) * 10 ^ k + ((zm128.toNat % 10 ^ k : ℕ) : ℚ) := by
      have hq : ((zm128.toNat : ℕ) : ℚ)
          = ((zm.toNat * 10 ^ k + zm128.toNat % 10 ^ k : ℕ) : ℚ) := by
        exact_mod_cast hzm128_decomp
      have hcast : (zm128.toNat : ℚ) = (x.mantissa_.toNat : ℚ) * (y.mantissa_.toNat : ℚ) := by
        have := hzm128_toNat; exact_mod_cast this
      rw [← hcast]; rw [hq]; push_cast; ring
    rw [hzm128_q, hf_def]
    have h_zpow_add : (10 : ℚ) ^ (ze + (k : ℤ)) = 10 ^ ze * 10 ^ k := by
      rw [zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0), zpow_natCast]
    rw [h_zpow_add]; field_simp
  have hf_nn : 0 ≤ f := by
    rw [hf_def]; exact div_nonneg (by exact_mod_cast Nat.zero_le _) (le_of_lt h10k_pos)
  have hf_lt : f < 1 := by
    rw [hf_def, div_lt_one h10k_pos]
    have h_mod_lt : zm128.toNat % 10 ^ k < 10 ^ k := Nat.mod_lt _ (by positivity)
    exact_mod_cast h_mod_lt
  have hx_min : (10 : ℕ) ^ 18 ≤ x.mantissa_.toNat := (mantissaBounds_nat_of hx_bounds).1
  have hy_min : (10 : ℕ) ^ 18 ≤ y.mantissa_.toNat := (mantissaBounds_nat_of hy_bounds).1
  have hzm128_ge : (10 : ℕ) ^ 36 ≤ zm128.toNat := by
    rw [hzm128_toNat]
    calc (10 : ℕ) ^ 36 = 10 ^ 18 * 10 ^ 18 := by ring
      _ ≤ x.mantissa_.toNat * y.mantissa_.toNat := Nat.mul_le_mul hx_min hy_min
  have hmaxRep_lt : maxRepUp.toNat < zm128.toNat := by
    rw [show maxRepUp.toNat = maxRepUpNat from rfl]
    calc (9223372036854775810 : ℕ) < 10 ^ 36 := by norm_num
      _ ≤ zm128.toNat := hzm128_ge
  have h_zm_lb := scaleDown128_lower_bound zm128 ze g0 hmaxRep_lt
  rw [← hsd_def] at h_zm_lb
  rw [h_sd_split] at h_zm_lb
  simp only at h_zm_lb
  have hzm_succ : mantissaFloorSucc ≤ zm.toNat := by
    have : (maxRepUp.toNat + 1) / 10 = mantissaFloorSucc := by
      rw [show maxRepUp.toNat = maxRepUpNat from rfl]
    omega
  have h_floor_constraint : zm.toNat = mantissaFloor → (8 : ℚ) / 10 ≤ f := by
    intro h_zm_floor; exfalso
    have : (maxRepUp.toNat + 1) / 10 = mantissaFloorSucc := by
      rw [show maxRepUp.toNat = maxRepUpNat from rfl]
    omega
  have h_g_sbit : g.sbit_ = zn := by
    have h_g_eq : g = (scaleDown128 zm128 ze g0).2.2 := by rw [hg_def, hsd_def]
    rw [h_g_eq, scaleDown128_sbit_preserved, hg0_def]
    by_cases hzn : zn
    · rw [if_pos hzn]; exact hzn.symm
    · rw [if_neg hzn]; exact (Bool.not_eq_true _ |>.mp hzn).symm
  have h_rup_exists : ∃ res : RoundResult,
      g.doRoundUp zn zm ze' largeRange.min largeRange.max mode "Number::multiplication overflow" = .ok res := by
    match hg : g.doRoundUp zn zm ze' largeRange.min largeRange.max mode "Number::multiplication overflow" with
    | .error e => simp only [hg, reduceCtorEq] at hok
    | .ok r => exact ⟨r, rfl⟩
  obtain ⟨res, h_rup⟩ := h_rup_exists
  simp only [h_rup] at hok
  exact ⟨zn, zm, ze', f, g, res,
    ⟨hzn_def, hzm_succ, hzm_le_maxRep, hf_nn, hf_lt,
     h_floor_constraint, habs_xy_eq2, hf_rep, h_g_sbit, h_rup, hok⟩⟩

structure MulFactsSpec (x y result : Number) (mode : rounding_mode)
    (zm : UInt64) (ze' : Int) (f : ℚ) (g : Guard) (res_pos : RoundResult) : Prop where
  zm_ge_floor : mantissaFloor ≤ zm.toNat
  zm_le_maxRepUp : zm.toNat ≤ maxRepUp.toNat
  f_nonneg : 0 ≤ f
  f_lt_one : f < 1
  floor_cusp : zm.toNat = mantissaFloor → (8 : ℚ) / 10 ≤ f
  value_eq : |x.toRat * y.toRat| = ((zm.toNat : ℚ) + f) * 10 ^ ze'
  rounds : g.doRoundUp false zm ze' largeRange.min largeRange.max mode
    "Number::multiplication overflow" = .ok res_pos
  result_abs : |result.toRat| = (res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_
  res_mant_ne : res_pos.mantissa_ ≠ 0
  represents_f : represents g f
  result_neg : result.negative_ = (x.negative_ != y.negative_)
  sbit : g.sbit_ = (x.negative_ != y.negative_)
  zm_succ : mantissaFloorSucc ≤ zm.toNat

structure MulFactsToNearest (x y result : Number) (mode : rounding_mode)
    (zm : UInt64) (ze' : Int) (f : ℚ) (g : Guard) (res_pos : RoundResult) : Prop where
  zm_ge_floor : mantissaFloor ≤ zm.toNat
  zm_le_maxRepUp : zm.toNat ≤ maxRepUp.toNat
  f_nonneg : 0 ≤ f
  f_lt_one : f < 1
  floor_cusp : zm.toNat = mantissaFloor → (8 : ℚ) / 10 ≤ f
  value_eq : |x.toRat * y.toRat| = ((zm.toNat : ℚ) + f) * 10 ^ ze'
  rounds : g.doRoundUp false zm ze' largeRange.min largeRange.max mode
    "Number::multiplication overflow" = .ok res_pos
  result_abs : |result.toRat| = (res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_
  res_mant_ne : res_pos.mantissa_ ≠ 0
  represents_f : represents g f
  result_neg : result.negative_ = (x.negative_ != y.negative_)

structure MulFactsTowardsZero (x y result : Number) (mode : rounding_mode)
    (zm : UInt64) (ze' : Int) (f : ℚ) (g : Guard) (res_pos : RoundResult) : Prop where
  zm_ge_floor : mantissaFloor ≤ zm.toNat
  zm_le_maxRepUp : zm.toNat ≤ maxRepUp.toNat
  f_nonneg : 0 ≤ f
  f_lt_one : f < 1
  value_eq : |x.toRat * y.toRat| = ((zm.toNat : ℚ) + f) * 10 ^ ze'
  rounds : g.doRoundUp false zm ze' largeRange.min largeRange.max mode
    "Number::multiplication overflow" = .ok res_pos
  result_abs : |result.toRat| = (res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_
  res_mant_ne : res_pos.mantissa_ ≠ 0
  represents_f : represents g f
  result_neg : result.negative_ = (x.negative_ != y.negative_)
  zm_succ : mantissaFloorSucc ≤ zm.toNat

theorem operator_mul_result_isNormalized (x y result : Number) (mode : rounding_mode)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (hok : Number.operator_mul x y mode = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    result.isNormalized := by
  obtain ⟨zn, zm, ze', f, g, res, d⟩ :=
    operator_mul_decompose x y result mode hx hy hx_mant_ne hy_mant_ne hok
  have hzm_ge : zm.toNat ≥ mantissaFloor := by have := d.zm_ge_floorSucc; omega
  have hres_mant_ne : res.mantissa_ ≠ 0 :=
    Number.normalize_mantissa_ne_zero_of_result d.normalizes hresult
  obtain ⟨h_res_min, h_res_max, h_res_exp, h_res_mod⟩ :=
    doRoundUp_output_invariants_upTo_maxRepUp_anyMode g zn zm ze' mode hzm_ge d.zm_le_maxRepUp
      "Number::multiplication overflow" res d.rounds hres_mant_ne
  exact Number.normalize_isNormalized_of_invariants h_res_min h_res_max h_res_exp h_res_mod d.normalizes

theorem operator_mul_no_overflow_mantissa (x y result : Number) (mode : rounding_mode)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (hok : Number.operator_mul x y mode = .ok result)
    (hresult : result.mantissa_ ≠ 0)
    (h_exp_ge : result.exponent_ ≥ maxExponent) :
    result.mantissa_ ≤ maxRepUp := by
  obtain ⟨zn, zm, ze', f, g, res, hzn, hzm_succ, hzm_le_maxRep, hf_nn, hf_lt, hfloor,
      habs, hf_rep, hsbit, h_rup, hok'⟩ :=
    operator_mul_decompose x y result mode hx hy hx_mant_ne hy_mant_ne hok
  have hzm_ge : zm.toNat ≥ mantissaFloor := by omega
  have hres_mant_ne : res.mantissa_ ≠ 0 :=
    Number.normalize_mantissa_ne_zero_of_result hok' hresult
  obtain ⟨h_res_min, h_res_max, h_res_exp, h_res_mod⟩ :=
    doRoundUp_output_invariants_upTo_maxRepUp_anyMode g zn zm ze' mode hzm_ge hzm_le_maxRep "Number::multiplication overflow" res h_rup hres_mant_ne
  exact Number.normalize_mantissa_le_maxRepUp_of_invariants h_res_min h_res_max h_res_exp h_res_mod hok' h_exp_ge

/-- `operator_mul` short-circuits on a zero operand (returning that operand), so a
nonzero result forces both operands' mantissas to be nonzero. Lets the public
`operator_mul_rounds_<mode>` theorems drop their `hx_mant_ne`/`hy_mant_ne` hypotheses. -/
theorem operator_mul_operands_ne_zero {x y result : Number} {mode : rounding_mode}
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hok : Number.operator_mul x y mode = .ok result) (hresult : result.mantissa_ ≠ 0) :
    x.mantissa_ ≠ 0 ∧ y.mantissa_ ≠ 0 := by
  refine ⟨fun hxz => ?_, fun hyz => ?_⟩
  · have hx_zero : x = Number.zero := Number.eq_zero_of_mantissa_zero x hx hxz
    unfold Number.operator_mul at hok
    rw [if_pos (show x.operator_eq Number.zero = true from by rw [hx_zero]; decide)] at hok
    have h_result : result = x :=
      (Except.ok.inj (show (Except.ok x : Except String Number) = .ok result from hok)).symm
    apply hresult
    rw [h_result, hx_zero]
    rfl
  · have hy_zero : y = Number.zero := Number.eq_zero_of_mantissa_zero y hy hyz
    by_cases hxg : x.operator_eq Number.zero = true
    · unfold Number.operator_mul at hok
      rw [if_pos hxg] at hok
      have h_result : result = x :=
        (Except.ok.inj (show (Except.ok x : Except String Number) = .ok result from hok)).symm
      apply hresult
      rw [h_result]
      exact Number.mantissa_eq_zero_of_operator_eq_zero hxg
    · unfold Number.operator_mul at hok
      rw [if_neg hxg,
          if_pos (show y.operator_eq Number.zero = true from by rw [hy_zero]; decide)] at hok
      have h_result : result = y :=
        (Except.ok.inj (show (Except.ok y : Except String Number) = .ok result from hok)).symm
      apply hresult
      rw [h_result, hy_zero]
      rfl

end XRPL.Model.Protocol
