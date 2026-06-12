import XRPL.Properties.Protocol.Number.Common.Notation
import XRPL.Properties.Protocol.Number.Common.Helpers
import XRPL.Properties.Protocol.Number.Rounding.DoRoundUp

set_option linter.style.longLine false
set_option linter.style.emptyLine false

namespace XRPL.Model.Protocol

/-- Under `operator_mul`'s hypotheses with `.towards_zero`, the result is normalized. -/
theorem operator_mul_result_isNormalized_towards_zero (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (hok : Number.operator_mul x y .towards_zero = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    result.isNormalized := by
  have hx_bounds := hx.mantissaBounds hx_mant_ne
  have hy_bounds := hy.mantissaBounds hy_mant_ne
  have hx_mant_pos : 0 < x.mantissa_.toNat := by
    have : largeRange.min.toNat ≤ x.mantissa_.toNat := UInt64.le_iff_toNat_le.mp hx_bounds.1
    rw [largeRange_min_val] at this; omega
  have hy_mant_pos : 0 < y.mantissa_.toNat := by
    have : largeRange.min.toNat ≤ y.mantissa_.toNat := UInt64.le_iff_toNat_le.mp hy_bounds.1
    rw [largeRange_min_val] at this; omega
  have hx_ne_zero : ¬ x.operator_eq Number.zero := by
    intro h
    unfold Number.operator_eq Number.zero at h
    simp only [Bool.and_eq_true, beq_iff_eq] at h
    obtain ⟨⟨_, hmeq⟩, _⟩ := h
    have : x.mantissa_.toNat = 0 := by rw [hmeq]; rfl
    omega
  have hy_ne_zero : ¬ y.operator_eq Number.zero := by
    intro h
    unfold Number.operator_eq Number.zero at h
    simp only [Bool.and_eq_true, beq_iff_eq] at h
    obtain ⟨⟨_, hmeq⟩, _⟩ := h
    have : y.mantissa_.toNat = 0 := by rw [hmeq]; rfl
    omega
  unfold Number.operator_mul at hok
  simp only [hx_ne_zero, hy_ne_zero, Bool.false_eq_true, if_false] at hok
  set zn : Bool := x.negative_ != y.negative_
  set zm128 : UInt128 := toUInt128 x.mantissa_ * toUInt128 y.mantissa_
  set ze : Int := x.exponent_ + y.exponent_
  set g0 : Guard := if zn then Guard.new.set_negative else Guard.new
  set sd_result : UInt64 × Int × Guard := scaleDown128 zm128 ze g0 with hsd_def
  set zm : UInt64 := sd_result.1 with hzm_def
  set ze' : Int := sd_result.2.1 with hze'_def
  set g : Guard := sd_result.2.2 with hg_def
  have hg0_rep : represents g0 0 := by
    by_cases hzn : zn
    · change represents (if zn then Guard.new.set_negative else Guard.new) 0
      rw [if_pos hzn]
      obtain ⟨x_rep, hx_nn, hx_lt, hf_eq, hxbit, hall⟩ := represents_new
      refine ⟨x_rep, hx_nn, hx_lt, ?_, ?_, ?_⟩
      · have : Guard.new.set_negative.digits_ = Guard.new.digits_ := rfl
        change (0 : ℚ) = _
        rw [this]; exact hf_eq
      · have hxbit_eq : Guard.new.set_negative.xbit_ = Guard.new.xbit_ := rfl
        rw [hxbit_eq]; exact hxbit
      · have : Guard.new.set_negative.digits_ = Guard.new.digits_ := rfl
        rw [this]; exact hall
    · change represents (if zn then Guard.new.set_negative else Guard.new) 0
      rw [if_neg hzn]; exact represents_new
  have h_sd_correct := scaleDown128_correct zm128 ze g0 0 hg0_rep
  simp only at h_sd_correct
  rw [← hsd_def] at h_sd_correct
  have h_sd_split : sd_result = (zm, ze', g) := by rw [hzm_def, hze'_def, hg_def]
  rw [h_sd_split] at h_sd_correct
  simp only at h_sd_correct
  obtain ⟨k, hk_eq, hzm_le_maxRep, hzm128_decomp, _hg_rep, _hfloor⟩ := h_sd_correct
  have hx_mant_bound : x.mantissa_.toNat < 10 ^ 19 := by
    have := UInt64.le_iff_toNat_le.mp hx_bounds.2
    rw [largeRange_max_val] at this; omega
  have hy_mant_bound : y.mantissa_.toNat < 10 ^ 19 := by
    have := UInt64.le_iff_toNat_le.mp hy_bounds.2
    rw [largeRange_max_val] at this; omega
  have h_prod_fit : x.mantissa_.toNat * y.mantissa_.toNat < 2 ^ 128 := by
    calc x.mantissa_.toNat * y.mantissa_.toNat
        < 10 ^ 19 * 10 ^ 19 := Nat.mul_lt_mul'' hx_mant_bound hy_mant_bound
      _ = 10 ^ 38 := by norm_num
      _ < 2 ^ 128 := by norm_num
  have hzm128_toNat : zm128.toNat = x.mantissa_.toNat * y.mantissa_.toNat :=
    uint128_of_uint64_mul_toNat _ _ h_prod_fit
  have hx_min : (10 : ℕ) ^ 18 ≤ x.mantissa_.toNat := by
    have := UInt64.le_iff_toNat_le.mp hx_bounds.1
    rw [largeRange_min_val] at this; exact this
  have hy_min : (10 : ℕ) ^ 18 ≤ y.mantissa_.toNat := by
    have := UInt64.le_iff_toNat_le.mp hy_bounds.1
    rw [largeRange_min_val] at this; exact this
  have hzm128_ge : (10 : ℕ) ^ 36 ≤ zm128.toNat := by
    rw [hzm128_toNat]
    calc (10 : ℕ) ^ 36 = 10 ^ 18 * 10 ^ 18 := by ring
      _ ≤ x.mantissa_.toNat * y.mantissa_.toNat := Nat.mul_le_mul hx_min hy_min
  have hmaxRep_lt : maxRep.toNat < zm128.toNat := by
    have h_mr_val : maxRep.toNat = maxRepNat := maxRep_val
    rw [h_mr_val]
    calc (maxRepNat : ℕ) < 10 ^ 36 := by norm_num
      _ ≤ zm128.toNat := hzm128_ge
  have h_zm_lb := scaleDown128_lower_bound zm128 ze g0 hmaxRep_lt
  rw [← hsd_def] at h_zm_lb
  rw [h_sd_split] at h_zm_lb
  simp only at h_zm_lb
  have hzm_ge : zm.toNat ≥ mantissaFloor := by
    have : (maxRep.toNat + 1) / 10 = mantissaFloor := by rw [maxRep_val]
    omega
  have h_rup_exists : ∃ res : RoundResult,
      g.doRoundUp zn zm ze' largeRange.min largeRange.max .towards_zero "Number::multiplication overflow" = .ok res := by
    match hg : g.doRoundUp zn zm ze' largeRange.min largeRange.max .towards_zero "Number::multiplication overflow" with
    | .error e => simp only [hg, reduceCtorEq] at hok
    | .ok r => exact ⟨r, rfl⟩
  obtain ⟨res, h_rup⟩ := h_rup_exists
  simp only [h_rup] at hok
  have hres_mant_ne : res.mantissa_ ≠ 0 :=
    Number.normalize_mantissa_ne_zero_of_result hok hresult
  have h_inv := doRoundUp_output_invariants_towards_zero g zn zm ze' hzm_ge hzm_le_maxRep "Number::multiplication overflow" res h_rup hres_mant_ne
  obtain ⟨h_res_min, h_res_max, h_res_exp, h_res_mod⟩ := h_inv
  have h_result_eq_res : result = res.toNumber :=
    Number.normalize_eq_of_invariants_towards_zero h_res_min h_res_max h_res_exp h_res_mod hok
  rw [h_result_eq_res]
  right
  refine ⟨?_, ?_, ?_, h_res_exp, ?_⟩
  · change largeRange.min ≤ res.mantissa_
    rw [UInt64.le_iff_toNat_le]; exact h_res_min
  · change res.mantissa_ ≤ largeRange.max
    rw [UInt64.le_iff_toNat_le]; exact h_res_max
  · by_cases h_cusp : res.mantissa_.toNat ≤ maxRep.toNat
    · left
      change res.mantissa_ ≤ maxRep
      rw [UInt64.le_iff_toNat_le]; exact h_cusp
    · push_neg at h_cusp
      right
      change res.mantissa_.toNat % 10 = 0
      exact h_res_mod h_cusp
  · show res.toNumber.exponent_ ≤ maxExponent
    exact Number.normalize_exp_bound h_res_min h_res_max h_res_exp hok

/-- Under `operator_mul`'s hypotheses with `.towards_zero`, max-exponent implies mantissa in range. -/
theorem operator_mul_no_overflow_mantissa_towards_zero (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (hok : Number.operator_mul x y .towards_zero = .ok result)
    (hresult : result.mantissa_ ≠ 0)
    (h_exp_ge : result.exponent_ ≥ maxExponent) :
    result.mantissa_ ≤ maxRep := by
  have hx_bounds := hx.mantissaBounds hx_mant_ne
  have hy_bounds := hy.mantissaBounds hy_mant_ne
  have hx_mant_pos : 0 < x.mantissa_.toNat := by
    have : largeRange.min.toNat ≤ x.mantissa_.toNat := UInt64.le_iff_toNat_le.mp hx_bounds.1
    rw [largeRange_min_val] at this; omega
  have hy_mant_pos : 0 < y.mantissa_.toNat := by
    have : largeRange.min.toNat ≤ y.mantissa_.toNat := UInt64.le_iff_toNat_le.mp hy_bounds.1
    rw [largeRange_min_val] at this; omega
  have hx_ne_zero : ¬ x.operator_eq Number.zero := by
    intro h
    unfold Number.operator_eq Number.zero at h
    simp only [Bool.and_eq_true, beq_iff_eq] at h
    obtain ⟨⟨_, hmeq⟩, _⟩ := h
    have : x.mantissa_.toNat = 0 := by rw [hmeq]; rfl
    omega
  have hy_ne_zero : ¬ y.operator_eq Number.zero := by
    intro h
    unfold Number.operator_eq Number.zero at h
    simp only [Bool.and_eq_true, beq_iff_eq] at h
    obtain ⟨⟨_, hmeq⟩, _⟩ := h
    have : y.mantissa_.toNat = 0 := by rw [hmeq]; rfl
    omega
  unfold Number.operator_mul at hok
  simp only [hx_ne_zero, hy_ne_zero, Bool.false_eq_true, if_false] at hok
  set zn : Bool := x.negative_ != y.negative_
  set zm128 : UInt128 := toUInt128 x.mantissa_ * toUInt128 y.mantissa_
  set ze : Int := x.exponent_ + y.exponent_
  set g0 : Guard := if zn then Guard.new.set_negative else Guard.new
  set sd_result : UInt64 × Int × Guard := scaleDown128 zm128 ze g0 with hsd_def
  set zm : UInt64 := sd_result.1 with hzm_def
  set ze' : Int := sd_result.2.1 with hze'_def
  set g : Guard := sd_result.2.2 with hg_def
  have hg0_rep : represents g0 0 := by
    by_cases hzn : zn
    · change represents (if zn then Guard.new.set_negative else Guard.new) 0
      rw [if_pos hzn]
      obtain ⟨x_rep, hx_nn, hx_lt, hf_eq, hxbit, hall⟩ := represents_new
      refine ⟨x_rep, hx_nn, hx_lt, ?_, ?_, ?_⟩
      · have : Guard.new.set_negative.digits_ = Guard.new.digits_ := rfl
        change (0 : ℚ) = _
        rw [this]; exact hf_eq
      · have hxbit_eq : Guard.new.set_negative.xbit_ = Guard.new.xbit_ := rfl
        rw [hxbit_eq]; exact hxbit
      · have : Guard.new.set_negative.digits_ = Guard.new.digits_ := rfl
        rw [this]; exact hall
    · change represents (if zn then Guard.new.set_negative else Guard.new) 0
      rw [if_neg hzn]; exact represents_new
  have h_sd_correct := scaleDown128_correct zm128 ze g0 0 hg0_rep
  simp only at h_sd_correct
  rw [← hsd_def] at h_sd_correct
  have h_sd_split : sd_result = (zm, ze', g) := by rw [hzm_def, hze'_def, hg_def]
  rw [h_sd_split] at h_sd_correct
  simp only at h_sd_correct
  obtain ⟨k, hk_eq, hzm_le_maxRep, hzm128_decomp, _hg_rep, _hfloor⟩ := h_sd_correct
  have hx_mant_bound : x.mantissa_.toNat < 10 ^ 19 := by
    have := UInt64.le_iff_toNat_le.mp hx_bounds.2
    rw [largeRange_max_val] at this; omega
  have hy_mant_bound : y.mantissa_.toNat < 10 ^ 19 := by
    have := UInt64.le_iff_toNat_le.mp hy_bounds.2
    rw [largeRange_max_val] at this; omega
  have h_prod_fit : x.mantissa_.toNat * y.mantissa_.toNat < 2 ^ 128 := by
    calc x.mantissa_.toNat * y.mantissa_.toNat
        < 10 ^ 19 * 10 ^ 19 := Nat.mul_lt_mul'' hx_mant_bound hy_mant_bound
      _ = 10 ^ 38 := by norm_num
      _ < 2 ^ 128 := by norm_num
  have hzm128_toNat : zm128.toNat = x.mantissa_.toNat * y.mantissa_.toNat :=
    uint128_of_uint64_mul_toNat _ _ h_prod_fit
  have hx_min : (10 : ℕ) ^ 18 ≤ x.mantissa_.toNat := by
    have := UInt64.le_iff_toNat_le.mp hx_bounds.1
    rw [largeRange_min_val] at this; exact this
  have hy_min : (10 : ℕ) ^ 18 ≤ y.mantissa_.toNat := by
    have := UInt64.le_iff_toNat_le.mp hy_bounds.1
    rw [largeRange_min_val] at this; exact this
  have hzm128_ge : (10 : ℕ) ^ 36 ≤ zm128.toNat := by
    rw [hzm128_toNat]
    calc (10 : ℕ) ^ 36 = 10 ^ 18 * 10 ^ 18 := by ring
      _ ≤ x.mantissa_.toNat * y.mantissa_.toNat := Nat.mul_le_mul hx_min hy_min
  have hmaxRep_lt : maxRep.toNat < zm128.toNat := by
    have h_mr_val : maxRep.toNat = maxRepNat := maxRep_val
    rw [h_mr_val]
    calc (maxRepNat : ℕ) < 10 ^ 36 := by norm_num
      _ ≤ zm128.toNat := hzm128_ge
  have h_zm_lb := scaleDown128_lower_bound zm128 ze g0 hmaxRep_lt
  rw [← hsd_def] at h_zm_lb
  rw [h_sd_split] at h_zm_lb
  simp only at h_zm_lb
  have hzm_ge : zm.toNat ≥ mantissaFloor := by
    have : (maxRep.toNat + 1) / 10 = mantissaFloor := by rw [maxRep_val]
    omega
  have h_rup_exists : ∃ res : RoundResult,
      g.doRoundUp zn zm ze' largeRange.min largeRange.max .towards_zero "Number::multiplication overflow" = .ok res := by
    match hg : g.doRoundUp zn zm ze' largeRange.min largeRange.max .towards_zero "Number::multiplication overflow" with
    | .error e => simp only [hg, reduceCtorEq] at hok
    | .ok r => exact ⟨r, rfl⟩
  obtain ⟨res, h_rup⟩ := h_rup_exists
  simp only [h_rup] at hok
  have hres_mant_ne : res.mantissa_ ≠ 0 :=
    Number.normalize_mantissa_ne_zero_of_result hok hresult
  have h_inv := doRoundUp_output_invariants_towards_zero g zn zm ze' hzm_ge hzm_le_maxRep "Number::multiplication overflow" res h_rup hres_mant_ne
  obtain ⟨h_res_min, h_res_max, h_res_exp, h_res_mod⟩ := h_inv
  have h_result_eq_res : result = res.toNumber :=
    Number.normalize_eq_of_invariants_towards_zero h_res_min h_res_max h_res_exp h_res_mod hok
  by_contra h_not
  rw [h_result_eq_res] at h_not h_exp_ge
  have h_gt : res.toNumber.mantissa_.toNat > maxRep.toNat :=
    Nat.lt_of_not_le (mt UInt64.le_iff_toNat_le.mpr h_not)
  exact absurd ⟨h_gt, h_exp_ge⟩
    (Number.normalize_no_cusp_overflow h_res_min h_res_max h_res_exp hok)

/-! ## `scaleDown128` sbit preservation -/

private lemma scaleDown128_sbit_preserved_tz
    (M : UInt128) (e : Int) (g0 : Guard) :
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

/-! ## Algorithmic decomposition for `.towards_zero`-mode `operator_mul`. -/

theorem operator_mul_algorithmic_facts_towards_zero (x y result : Number)
    (hx : x.isNormalized) (hy : y.isNormalized)
    (hx_mant_ne : x.mantissa_ ≠ 0) (hy_mant_ne : y.mantissa_ ≠ 0)
    (hok : Number.operator_mul x y .towards_zero = .ok result)
    (hresult : result.mantissa_ ≠ 0) :
    ∃ (zm : UInt64) (ze' : Int) (f : ℚ) (g : Guard) (res_pos : RoundResult),
      mantissaFloor ≤ zm.toNat ∧
      zm.toNat ≤ maxRep.toNat ∧
      0 ≤ f ∧ f < 1 ∧
      |x.toRat * y.toRat| = ((zm.toNat : ℚ) + f) * 10 ^ ze' ∧
      g.doRoundUp false zm ze' largeRange.min largeRange.max .towards_zero "Number::multiplication overflow" = .ok res_pos ∧
      |result.toRat| = (res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_ ∧
      res_pos.mantissa_ ≠ 0 ∧
      represents g f ∧
      result.negative_ = (x.negative_ != y.negative_) := by
  have hx_bounds := hx.mantissaBounds hx_mant_ne
  have hy_bounds := hy.mantissaBounds hy_mant_ne
  have hx_mant_pos : 0 < x.mantissa_.toNat := by
    have : largeRange.min.toNat ≤ x.mantissa_.toNat := UInt64.le_iff_toNat_le.mp hx_bounds.1
    rw [largeRange_min_val] at this; omega
  have hy_mant_pos : 0 < y.mantissa_.toNat := by
    have : largeRange.min.toNat ≤ y.mantissa_.toNat := UInt64.le_iff_toNat_le.mp hy_bounds.1
    rw [largeRange_min_val] at this; omega
  have hx_ne_zero : ¬ x.operator_eq Number.zero := by
    intro h
    unfold Number.operator_eq Number.zero at h
    simp only [Bool.and_eq_true, beq_iff_eq] at h
    obtain ⟨⟨_, hmeq⟩, _⟩ := h
    have : x.mantissa_.toNat = 0 := by rw [hmeq]; rfl
    omega
  have hy_ne_zero : ¬ y.operator_eq Number.zero := by
    intro h
    unfold Number.operator_eq Number.zero at h
    simp only [Bool.and_eq_true, beq_iff_eq] at h
    obtain ⟨⟨_, hmeq⟩, _⟩ := h
    have : y.mantissa_.toNat = 0 := by rw [hmeq]; rfl
    omega
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
    rw [hg0_def]
    by_cases hzn : zn
    · rw [if_pos hzn]
      obtain ⟨x_rep, hx_nn, hx_lt, hf_eq, hxbit, hall⟩ := represents_new
      refine ⟨x_rep, hx_nn, hx_lt, ?_, ?_, ?_⟩
      · show (0 : ℚ) = _
        have : Guard.new.set_negative.digits_ = Guard.new.digits_ := rfl
        rw [this]; exact hf_eq
      · have hxbit_eq : Guard.new.set_negative.xbit_ = Guard.new.xbit_ := rfl
        rw [hxbit_eq]; exact hxbit
      · have : Guard.new.set_negative.digits_ = Guard.new.digits_ := rfl
        rw [this]; exact hall
    · rw [if_neg hzn]; exact represents_new
  have h_sd_correct := scaleDown128_correct zm128 ze g0 0 hg0_rep
  simp only at h_sd_correct
  rw [← hsd_def] at h_sd_correct
  have h_sd_split : sd_result = (zm, ze', g) := by rw [hzm_def, hze'_def, hg_def]
  rw [h_sd_split] at h_sd_correct
  simp only at h_sd_correct
  obtain ⟨k, hk_eq, hzm_le_maxRep, hzm128_decomp, hg_rep, _hfloor⟩ := h_sd_correct
  have hx_mant_bound : x.mantissa_.toNat < 10 ^ 19 := by
    have := UInt64.le_iff_toNat_le.mp hx_bounds.2
    rw [largeRange_max_val] at this; omega
  have hy_mant_bound : y.mantissa_.toNat < 10 ^ 19 := by
    have := UInt64.le_iff_toNat_le.mp hy_bounds.2
    rw [largeRange_max_val] at this; omega
  have h_prod_fit : x.mantissa_.toNat * y.mantissa_.toNat < 2 ^ 128 := by
    calc x.mantissa_.toNat * y.mantissa_.toNat
        < 10 ^ 19 * 10 ^ 19 := Nat.mul_lt_mul'' hx_mant_bound hy_mant_bound
      _ = 10 ^ 38 := by norm_num
      _ < 2 ^ 128 := by norm_num
  have hzm128_toNat : zm128.toNat = x.mantissa_.toNat * y.mantissa_.toNat := by
    rw [hzm128_def]
    exact uint128_of_uint64_mul_toNat _ _ h_prod_fit
  have habs_xy_eq : |x.toRat * y.toRat|
      = (x.mantissa_.toNat : ℚ) * (y.mantissa_.toNat : ℚ) * 10 ^ ze := by
    rw [abs_mul, abs_toRat_eq, abs_toRat_eq, hze_def]
    rw [show x.exponent_ + y.exponent_ = x.exponent_ + y.exponent_ from rfl]
    rw [zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0)]
    ring
  set f : ℚ := ((zm128.toNat % 10 ^ k : ℕ) : ℚ) / 10 ^ k with hf_def
  have h10k_pos : (0 : ℚ) < 10 ^ k := by positivity
  have h10k_ne : (10 : ℚ) ^ k ≠ 0 := ne_of_gt h10k_pos
  have hf_rep : represents g f := by
    have : (0 + ((zm128.toNat % 10 ^ k : ℕ) : ℚ)) / 10 ^ k = f := by
      rw [hf_def]; ring
    rw [← this]; exact hg_rep
  have habs_xy_eq2 : |x.toRat * y.toRat| = ((zm.toNat : ℚ) + f) * 10 ^ ze' := by
    rw [habs_xy_eq, hk_eq]
    have hzm128_q : ((x.mantissa_.toNat : ℚ) * (y.mantissa_.toNat : ℚ))
        = (zm.toNat : ℚ) * 10 ^ k + ((zm128.toNat % 10 ^ k : ℕ) : ℚ) := by
      have hq : ((zm128.toNat : ℕ) : ℚ)
          = ((zm.toNat * 10 ^ k + zm128.toNat % 10 ^ k : ℕ) : ℚ) := by
        exact_mod_cast hzm128_decomp
      have hcast : (zm128.toNat : ℚ) = (x.mantissa_.toNat : ℚ) * (y.mantissa_.toNat : ℚ) := by
        have := hzm128_toNat
        exact_mod_cast this
      rw [← hcast]; rw [hq]; push_cast; ring
    rw [hzm128_q, hf_def]
    have h_zpow_add : (10 : ℚ) ^ (ze + (k : ℤ)) = 10 ^ ze * 10 ^ k := by
      rw [zpow_add₀ (by norm_num : (10 : ℚ) ≠ 0), zpow_natCast]
    rw [h_zpow_add]
    field_simp
  have hf_nn : 0 ≤ f := by
    rw [hf_def]
    apply div_nonneg
    · exact_mod_cast Nat.zero_le _
    · exact le_of_lt h10k_pos
  have hf_lt : f < 1 := by
    rw [hf_def]
    rw [div_lt_one h10k_pos]
    have h_mod_lt : zm128.toNat % 10 ^ k < 10 ^ k := Nat.mod_lt _ (Nat.pos_of_ne_zero (by
      intro h
      have : (10 : ℕ) ^ k > 0 := by positivity
      omega))
    exact_mod_cast h_mod_lt
  have hx_min : (10 : ℕ) ^ 18 ≤ x.mantissa_.toNat := by
    have := UInt64.le_iff_toNat_le.mp hx_bounds.1
    rw [largeRange_min_val] at this; exact this
  have hy_min : (10 : ℕ) ^ 18 ≤ y.mantissa_.toNat := by
    have := UInt64.le_iff_toNat_le.mp hy_bounds.1
    rw [largeRange_min_val] at this; exact this
  have hzm128_ge : (10 : ℕ) ^ 36 ≤ zm128.toNat := by
    rw [hzm128_toNat]
    calc (10 : ℕ) ^ 36 = 10 ^ 18 * 10 ^ 18 := by ring
      _ ≤ x.mantissa_.toNat * y.mantissa_.toNat := Nat.mul_le_mul hx_min hy_min
  have hmaxRep_lt : maxRep.toNat < zm128.toNat := by
    have h_mr_val : maxRep.toNat = maxRepNat := maxRep_val
    rw [h_mr_val]
    calc (maxRepNat : ℕ) < 10 ^ 36 := by norm_num
      _ ≤ zm128.toNat := hzm128_ge
  have h_zm_lb := scaleDown128_lower_bound zm128 ze g0 hmaxRep_lt
  rw [← hsd_def] at h_zm_lb
  rw [h_sd_split] at h_zm_lb
  simp only at h_zm_lb
  have hzm_ge : zm.toNat ≥ mantissaFloor := by
    have : (maxRep.toNat + 1) / 10 = mantissaFloor := by rw [maxRep_val]
    omega
  have h_rup_exists : ∃ res : RoundResult,
      g.doRoundUp zn zm ze' largeRange.min largeRange.max .towards_zero "Number::multiplication overflow" = .ok res := by
    match hg : g.doRoundUp zn zm ze' largeRange.min largeRange.max .towards_zero "Number::multiplication overflow" with
    | .error e => simp only [hg, reduceCtorEq] at hok
    | .ok r => exact ⟨r, rfl⟩
  obtain ⟨res, h_rup⟩ := h_rup_exists
  simp only [h_rup] at hok
  have hres_mant_ne : res.mantissa_ ≠ 0 :=
    Number.normalize_mantissa_ne_zero_of_result hok hresult
  have h_inv := doRoundUp_output_invariants_towards_zero g zn zm ze' hzm_ge hzm_le_maxRep "Number::multiplication overflow" res h_rup hres_mant_ne
  obtain ⟨h_res_min, h_res_max, h_res_exp, h_res_mod⟩ := h_inv
  set res_pos : RoundResult := { negative_ := false, mantissa_ := res.mantissa_, exponent_ := res.exponent_ }
  have hres_pos_mant_ne : res_pos.mantissa_ ≠ 0 := hres_mant_ne
  have h_rup_pos : g.doRoundUp false zm ze' largeRange.min largeRange.max .towards_zero "Number::multiplication overflow" = .ok res_pos :=
    doRoundUp_false_from_ok g zn zm ze' .towards_zero "Number::multiplication overflow" res h_rup
  have h_result_eq_res : result = res.toNumber :=
    Number.normalize_eq_of_invariants_towards_zero h_res_min h_res_max h_res_exp h_res_mod hok
  have h_result_abs : |result.toRat|
      = (res_pos.mantissa_.toNat : ℚ) * 10 ^ res_pos.exponent_ := by
    rw [h_result_eq_res, abs_toRat_eq res.toNumber]; rfl
  have h_res_neg_eq_zn : result.negative_ = zn := by
    rw [h_result_eq_res]
    exact doRoundUp_negative_of_mant_ne g zn zm ze' _ _ _ "Number::multiplication overflow" res h_rup hres_mant_ne
  refine ⟨zm, ze', f, g, res_pos, hzm_ge, hzm_le_maxRep, hf_nn, hf_lt,
    habs_xy_eq2, h_rup_pos, h_result_abs, hres_pos_mant_ne, hf_rep, ?_⟩
  rw [h_res_neg_eq_zn]


end XRPL.Model.Protocol
