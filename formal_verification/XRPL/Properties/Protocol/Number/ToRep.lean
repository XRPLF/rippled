import XRPL.Properties.Protocol.Number.Accessors
import XRPL.Properties.Protocol.Number.Common.Int64Lemmas
import Mathlib.Tactic

set_option linter.style.longLine false
set_option linter.style.emptyLine false

namespace XRPL.Model.Protocol

/-! # Helper invariants for `Number.to_rep`

The guard-cleanliness, sign/magnitude, and `grow`/`shift` invariant lemmas
underlying the headline `to_rep_within_one` and `to_rep_exact_of_exponent_nonneg`
correctness results. -/

/-! ## Guard "cleanliness": no nonzero digit dropped ⟹ no round-up

`Guard.round` only triggers a round-up when `digits_ > 0` or `xbit_` is set, both
of which require some nonzero low digit to have been dropped. When the magnitude
divides evenly by `10^(-offset)`, every dropped digit is `0`, the guard stays
"clean" (`digits_ = 0`, `xbit_ = false`), and the round is `-1`. -/

/-- A clean guard rounds to `-1` (never rounds up), regardless of mode/`sbit_`. -/
lemma clean_guard_round (g : Guard) (h1 : g.digits_ = 0) (h2 : g.xbit_ = false)
    (mode : rounding_mode) : g.round mode = -1 := by
  unfold Guard.round
  cases mode <;> simp_all

/-- Pushing the digit `0` onto a clean guard keeps it clean. -/
lemma push_zero_clean (g : Guard) (h1 : g.digits_ = 0) (h2 : g.xbit_ = false) :
    (g.push 0).digits_ = 0 ∧ (g.push 0).xbit_ = false := by
  refine ⟨?_, ?_⟩
  · change ((g.digits_ >>> 4) ||| ((0 : UInt64) &&& 0xF) <<< 60) = 0
    rw [h1]; decide
  · change (g.xbit_ || ((g.digits_ &&& 0xF) != 0)) = false
    rw [h1, h2]; decide

/-- If the magnitude divides evenly by `10^(-offset)`, `shift` leaves the guard
clean: every dropped digit is `0`. -/
lemma shift_snd_clean (D : Int64) (off : Int) (g : Guard) (h0 : 0 ≤ D.toInt)
    (hg1 : g.digits_ = 0) (hg2 : g.xbit_ = false)
    (hdvd : D.toInt % 10 ^ (-off).toNat = 0) :
    (Number.to_rep.shift D off g).2.digits_ = 0 ∧ (Number.to_rep.shift D off g).2.xbit_ = false := by
  induction D, off, g using Number.to_rep.shift.induct with
  | case1 drops offset gg hneg ih =>
    rw [Number.to_rep.shift, if_pos hneg]
    -- dropped digit is 0
    have hk : (-offset).toNat = (-(offset + 1)).toNat + 1 := by omega
    rw [hk, pow_succ'] at hdvd
    have hdvd_full : (10 * 10 ^ (-(offset + 1)).toNat : ℤ) ∣ drops.toInt :=
      Int.dvd_of_emod_eq_zero hdvd
    have hdvd10 : drops.toInt % 10 = 0 := by
      apply Int.emod_eq_zero_of_dvd
      exact dvd_trans ⟨10 ^ (-(offset + 1)).toNat, by ring⟩ hdvd_full
    have hdig0 : (drops % 10).toUInt64 = 0 := by
      have : (drops % 10).toInt = 0 := by rw [toInt_mod_ten_of_nonneg drops h0, hdvd10]
      have hz : drops % 10 = 0 := Int64.toInt_inj.mp (by rw [this]; decide)
      rw [hz]; rfl
    have hdiv_nn : 0 ≤ (drops / 10).toInt := by
      rw [toInt_div_ten_of_nonneg drops h0]; exact Int.ediv_nonneg h0 (by norm_num)
    have hdiv_dvd : (drops / 10).toInt % 10 ^ (-(offset + 1)).toNat = 0 := by
      rw [toInt_div_ten_of_nonneg drops h0]
      obtain ⟨c, hc⟩ := hdvd_full
      rw [hc]
      rw [show (10 * 10 ^ (-(offset + 1)).toNat * c) / 10 = 10 ^ (-(offset + 1)).toNat * c from by
        rw [mul_assoc, Int.mul_ediv_cancel_left _ (by norm_num)]]
      exact Int.mul_emod_right _ _
    rcases push_zero_clean gg hg1 hg2 with ⟨hpc1, hpc2⟩
    nth_rewrite 1 [← hdig0] at hpc1
    nth_rewrite 1 [← hdig0] at hpc2
    exact ih hdiv_nn hpc1 hpc2 hdiv_dvd
  | case2 drops offset gg hnneg =>
    rw [Number.to_rep.shift, if_neg hnneg]
    exact ⟨hg1, hg2⟩

/-! ## Sign and magnitude of the external mantissa -/

/-- The external mantissa is nonpositive when negative, nonnegative otherwise. -/
lemma mantissa_sign (n : Number) :
    (n.negative_ = true → n.mantissa.toInt ≤ 0) ∧
    (n.negative_ = false → 0 ≤ n.mantissa.toInt) := by
  rw [Number.mantissa_toInt n]
  refine ⟨fun h => ?_, fun h => ?_⟩
  · rw [if_pos h]
    apply mul_nonpos_of_nonpos_of_nonneg (by norm_num)
    split <;> positivity
  · rw [if_neg (by rw [h]; decide), one_mul]
    split <;> positivity

/-- The `Int64` magnitude formed at the start of `to_rep`
(`if n.negative_ then -drops else drops`) has integer value `|M|`. -/
lemma magnitude_toInt (n : Number) (hn : n.isNormalized) :
    (if n.negative_ then -n.mantissa else n.mantissa).toInt = n.mantissa.toInt.natAbs := by
  have hbound : n.mantissa.toInt.natAbs ≤ maxRep.toNat := mantissa_natAbs_le_maxRep n hn
  have hlt : (n.mantissa.toInt.natAbs : ℤ) < 2 ^ 63 := by rw [maxRep_val] at hbound; omega
  rcases mantissa_sign n with ⟨hsneg, hsnn⟩
  by_cases hneg : n.negative_
  · rw [if_pos hneg, Int64.toInt_neg, Int.bmod_eq_iff (by norm_num)]
    have hnp : n.mantissa.toInt ≤ 0 := hsneg hneg
    rw [Int.ofNat_natAbs_of_nonpos hnp] at hlt ⊢
    constructor <;> [omega; (push_cast; omega)]
  · rw [if_neg hneg]
    have hnn : 0 ≤ n.mantissa.toInt := hsnn (by simpa using hneg)
    rw [Int.natAbs_of_nonneg hnn]

/-! ## `grow` (positive offset): exact integer scaling

When `offset ≥ 0`, `grow` multiplies the magnitude by `10^offset` exactly (the
overflow guard `drops ≤ maxRep/10` keeps each `*10` step inside the `Int64`
range), or signals overflow. We prove the success case is exact. -/

lemma grow_ok_eq (D D' : Int64) (off : Int) (h0 : 0 ≤ D.toInt)
    (hok : Number.to_rep.grow D off = .ok D') :
    (D'.toInt : ℤ) = D.toInt * 10 ^ off.toNat := by
  induction D, off using Number.to_rep.grow.induct with
  | case1 drops offset hpos hover =>
    rw [Number.to_rep.grow, if_pos hpos, if_pos hover] at hok
    exact absurd hok (by simp)
  | case2 drops offset hpos hover ih =>
    rw [Number.to_rep.grow, if_pos hpos, if_neg hover] at hok
    have hguard : drops.toInt ≤ (maxRep.toNat : ℤ) / 10 := by
      have hle : drops ≤ maxRep.toInt64 / 10 := Int64.not_lt.mp hover
      have hb : (maxRep.toInt64 / 10).toInt = (maxRep.toNat : ℤ) / 10 := by decide
      have : drops.toInt ≤ (maxRep.toInt64 / 10).toInt := Int64.le_iff_toInt_le.mp hle
      rw [hb] at this; exact this
    have hmul := toInt_mul_ten_of_le drops h0 hguard
    have h0' : 0 ≤ (drops * 10).toInt := by rw [hmul]; positivity
    have ihr := ih h0' hok
    rw [ihr, hmul]
    have hoff : offset.toNat = (offset - 1).toNat + 1 := by omega
    rw [hoff, pow_succ]
    ring
  | case3 drops offset hnpos =>
    rw [Number.to_rep.grow, if_neg hnpos] at hok
    have hd : drops = D' := by injection hok
    have hoff : offset.toNat = 0 := by omega
    rw [← hd, hoff, pow_zero, mul_one]

/-! ## `shift` (negative offset): floor division

When `offset < 0`, `shift` repeatedly divides the magnitude by `10`, dropping the
low digit into the `Guard`. The resulting magnitude is the floor `⌊|value|⌋`,
i.e. `D / 10^(-offset)` (`Int` floor division on the non-negative magnitude). -/

lemma shift_fst_eq (D : Int64) (off : Int) (g : Guard) (h0 : 0 ≤ D.toInt) :
    ((Number.to_rep.shift D off g).fst).toInt = D.toInt / 10 ^ (-off).toNat := by
  induction D, off, g using Number.to_rep.shift.induct with
  | case1 drops offset gg hneg ih =>
    rw [Number.to_rep.shift, if_pos hneg]
    have hdiv : (drops / 10).toInt = drops.toInt / 10 := toInt_div_ten_of_nonneg drops h0
    have h0' : 0 ≤ (drops / 10).toInt := by
      rw [hdiv]; exact Int.ediv_nonneg h0 (by norm_num)
    have ihr := ih h0'
    rw [ihr, hdiv]
    have hk : (-offset).toNat = (-(offset + 1)).toNat + 1 := by omega
    rw [hk, pow_succ', Int.ediv_ediv_of_nonneg (by norm_num)]
  | case2 drops offset gg hnneg =>
    rw [Number.to_rep.shift, if_neg hnneg]
    have hk : (-offset).toNat = 0 := by omega
    rw [hk, pow_zero, Int.ediv_one]

/-- The starting guard (empty, possibly with only the sign bit set) never rounds
up: `digits_ = 0` and `xbit_ = false`. -/
lemma start_guard_round (mode : rounding_mode) (neg : Bool) :
    (if neg then Guard.new.set_negative else Guard.new).round mode = -1 := by
  cases neg <;>
    (unfold Guard.round Guard.new Guard.set_negative; cases mode <;> simp_all)


/-! # Correctness of `Number.to_rep`

`Number.to_rep n mode` converts a normalized `Number` to a signed `Int64`,
rounding to an integer (according to `mode`) and signalling overflow with an
`Except` error. The headline result `to_rep_within_one` states that whenever it
succeeds, the returned integer is within strictly `1` of the exact rational
value `n.toRat`. -/

theorem to_rep_within_one (n : Number) (mode : rounding_mode) (r : Int64)
    (hn : n.isNormalized)
    (hok : n.to_rep mode = .ok r) :
    |(r.toInt : ℚ) - n.toRat| < 1 := by
  unfold Number.to_rep at hok
  simp only at hok
  by_cases hz : (n.mantissa == 0) = true
  · -- zero magnitude: result is 0 and value is 0
    rw [if_pos hz] at hok
    have hr : r = 0 := by injection hok with h; exact h.symm
    have hmant : n.mantissa.toInt = 0 := by
      rw [beq_iff_eq] at hz
      rw [hz]; decide
    have htr : n.toRat = 0 := by
      rw [← mantissa_mul_exponent_eq_toRat n hn, hmant]; norm_num
    rw [hr, htr]
    have : (0 : Int64).toInt = 0 := by decide
    rw [this]; norm_num
  · rw [if_neg hz] at hok
    -- magnitude facts
    have hmant_ne : n.mantissa.toInt ≠ 0 := by
      intro hc
      apply hz
      rw [beq_iff_eq]
      have : n.mantissa = 0 := by
        have := hc; exact Int64.toInt_inj.mp (by rw [hc]; decide)
      exact this
    have hmagM_pos : 0 < n.mantissa.toInt.natAbs := Int.natAbs_pos.mpr hmant_ne
    have hmagM_le : n.mantissa.toInt.natAbs ≤ maxRep.toNat := mantissa_natAbs_le_maxRep n hn
    have hmag : (if n.negative_ then -n.mantissa else n.mantissa).toInt
        = (n.mantissa.toInt.natAbs : ℤ) := magnitude_toInt n hn
    have hD0_nonneg : 0 ≤ (if n.negative_ then -n.mantissa else n.mantissa).toInt := by
      rw [hmag]; positivity
    by_cases hexp : n.exponent < 0
    · -- offset < 0: shift floor + guard round-up by at most 1
      have hge : ¬ n.exponent ≥ 0 := by omega
      rw [if_pos hexp, if_neg hge] at hok
      simp only at hok
      set D0 : Int64 := if n.negative_ then -n.mantissa else n.mantissa with hD0def
      set g0 : Guard := if n.negative_ then Guard.new.set_negative else Guard.new with hg0def
      set sp := Number.to_rep.shift D0 n.exponent g0 with hspdef
      -- Df = floor magnitude
      have hDf := shift_fst_eq D0 n.exponent g0 hD0_nonneg
      rw [hmag] at hDf
      set k : ℕ := (-n.exponent).toNat with hkdef
      rw [← hspdef] at hDf
      -- magnitude of the truth value, as a rational
      set magM : ℕ := (Int64.toInt n.mantissa).natAbs with hmagMdef
      have hpowk_pos : (0 : ℚ) < (10 : ℚ) ^ k := by positivity
      -- |n.toRat| = magM / 10^k
      have hexp_pow : (10 : ℚ) ^ n.exponent = ((10 : ℚ) ^ k)⁻¹ := by
        rw [show n.exponent = -(k : ℤ) from by rw [hkdef]; omega, zpow_neg, zpow_natCast]
      have habs : |n.toRat| = (magM : ℚ) / (10 : ℚ) ^ k := by
        rw [← mantissa_mul_exponent_eq_toRat n hn, abs_mul, abs_zpow]
        rw [show |(10 : ℚ)| = 10 from by norm_num, hexp_pow]
        rw [show |(n.mantissa.toInt : ℚ)| = (magM : ℚ) from by
          rw [hmagMdef, Nat.cast_natAbs]; push_cast; rfl]
        rw [div_eq_mul_inv]
      -- floor bounds (integer): q*10^k ≤ magM < (q+1)*10^k
      have hpowk_ne : (10 : ℤ) ^ k ≠ 0 := by positivity
      have hint_lb : (sp.1.toInt) * 10 ^ k ≤ (magM : ℤ) := by
        rw [hDf]
        exact Int.ediv_mul_le (magM : ℤ) hpowk_ne
      have hint_ub : (magM : ℤ) < (sp.1.toInt + 1) * 10 ^ k := by
        rw [hDf]
        have h1 := Int.emod_lt_of_pos (magM : ℤ) (show (0 : ℤ) < (10 : ℤ) ^ k by positivity)
        have h2 := Int.mul_ediv_add_emod (magM : ℤ) ((10 : ℤ) ^ k)
        have h3 := Int.emod_nonneg (magM : ℤ) hpowk_ne
        nlinarith
      -- floor bounds (rational)
      have hfloor_lb : (sp.1.toInt : ℚ) ≤ (magM : ℚ) / (10 : ℚ) ^ k := by
        rw [le_div_iff₀ hpowk_pos]
        have := (Int.cast_le (R := ℚ)).mpr hint_lb
        push_cast at this; linarith
      have hfloor_ub : (magM : ℚ) / (10 : ℚ) ^ k < (sp.1.toInt : ℚ) + 1 := by
        rw [div_lt_iff₀ hpowk_pos]
        have := (Int.cast_lt (R := ℚ)).mpr hint_ub
        push_cast at this; linarith
      -- bracket: 0 ≤ |n.toRat| - Df.toInt < 1
      have hbra_lo : (0 : ℚ) ≤ |n.toRat| - (sp.1.toInt : ℚ) := by rw [habs]; linarith
      have hbra_hi : |n.toRat| - (sp.1.toInt : ℚ) < 1 := by rw [habs]; linarith
      -- n.toRat = sign * |n.toRat|
      have hsign_toRat : n.toRat = (if n.negative_ then -1 else 1) * |n.toRat| := by
        by_cases hneg : n.negative_
        · rw [if_pos hneg, abs_of_nonpos (Number.toRat_nonpos_of_negative n hneg)]; ring
        · rw [if_neg hneg, abs_of_nonneg (Number.toRat_nonneg_of_nonnegative n (by simpa using hneg))]; ring
      have hDf_nn : 0 ≤ sp.1.toInt := by rw [hDf]; exact Int.ediv_nonneg (by positivity) (by positivity)
      -- key reduction: |r.toInt - toRat| = |mg - |toRat|| where mg is the magnitude
      have hkey : ∀ (mg : Int64), 0 ≤ mg.toInt →
          r = (if n.negative_ then -mg else mg) →
          |(r.toInt : ℚ) - n.toRat| = abs ((mg.toInt : ℚ) - |n.toRat|) := by
        intro mg hmg_nn hrmg
        set s : ℚ := if n.negative_ then (-1 : ℚ) else 1 with hsdef
        have hrInt : (r.toInt : ℚ) = s * (mg.toInt : ℚ) := by
          rw [hrmg, hsdef]
          by_cases hneg : n.negative_
          · rw [if_pos hneg, if_pos hneg, toInt_neg_of_nonneg _ hmg_nn]; push_cast; ring
          · rw [if_neg hneg, if_neg hneg, one_mul]
        have hdiff : (r.toInt : ℚ) - n.toRat = s * ((mg.toInt : ℚ) - |n.toRat|) := by
          rw [hrInt]; nth_rewrite 1 [hsign_toRat]; ring
        have habs_s : |s| = 1 := by rw [hsdef]; by_cases hneg : n.negative_ <;>
          [rw [if_pos hneg]; rw [if_neg hneg]] <;> norm_num
        rw [hdiff, abs_mul, habs_s, one_mul]
      -- case on the round-up boolean
      by_cases hb : (sp.2.round mode == 1 || sp.2.round mode == 0 && sp.1 % 2 == 1) = true
      · -- round up: r = ±(Df + 1)
        rw [if_pos hb] at hok
        by_cases hovf : sp.1 ≥ maxRep.toInt64
        · rw [if_pos hovf] at hok; exact absurd hok (by simp)
        · rw [if_neg hovf] at hok
          have hr : r = (if n.negative_ then -(sp.1 + 1) else sp.1 + 1) := by
            injection hok with h; exact h.symm
          -- Df + 1 is nonneg and exact
          have hovf' : sp.1.toInt < maxRep.toInt64.toInt :=
            (Int64.lt_iff_toInt_lt).mp (Int64.not_le.mp hovf)
          clear hovf
          have hmaxval : maxRep.toInt64.toInt = (maxRep.toNat : ℤ) := by decide
          have hadd : (sp.1 + 1).toInt = sp.1.toInt + 1 := by
            rw [Int64.toInt_add, int64_one_toInt, Int.bmod_eq_iff (by norm_num)]
            rw [hmaxval] at hovf'
            refine ⟨?_, ?_⟩ <;> push_cast <;> [omega; (rw [maxRep_val] at hovf'; omega)]
          have hmg_nn : 0 ≤ (sp.1 + 1).toInt := by rw [hadd]; omega
          -- round-up implies a nonzero digit was dropped: magM % 10^k ≠ 0
          have hg0_clean : g0.digits_ = 0 ∧ g0.xbit_ = false := by
            rw [hg0def]; by_cases hneg : n.negative_ <;> [rw [if_pos hneg]; rw [if_neg hneg]] <;>
              exact ⟨rfl, rfl⟩
          have hdvd_ne : (magM : ℤ) % 10 ^ k ≠ 0 := by
            intro hdvd
            have hDeq : D0.toInt % 10 ^ (-n.exponent).toNat = 0 := by
              rw [hmag, ← hkdef]; exact hdvd
            have hclean := shift_snd_clean D0 n.exponent g0 hD0_nonneg hg0_clean.1 hg0_clean.2 hDeq
            rw [← hspdef] at hclean
            have hround := clean_guard_round sp.2 hclean.1 hclean.2 mode
            rw [hround] at hb
            exact absurd hb (by simp)
          have hstrict : (sp.1.toInt : ℚ) < |n.toRat| := by
            rw [habs, lt_div_iff₀ hpowk_pos]
            have hlt : (sp.1.toInt) * 10 ^ k < (magM : ℤ) := by
              rcases lt_or_eq_of_le hint_lb with h | h
              · exact h
              · exfalso; apply hdvd_ne; rw [← h, Int.mul_emod_left]
            have := (Int.cast_lt (R := ℚ)).mpr hlt
            push_cast at this; linarith
          rw [hkey (sp.1 + 1) hmg_nn hr, hadd]
          push_cast
          rw [abs_lt]
          constructor <;> linarith
      · -- no round up: r = ±Df
        rw [if_neg hb] at hok
        have hr : r = (if n.negative_ then -sp.1 else sp.1) := by injection hok with h; exact h.symm
        rw [hkey sp.1 hDf_nn hr]
        rw [abs_lt]
        constructor <;> linarith
    · -- offset ≥ 0: grow is exact; guard empty so no round-up
      have hexp' : n.exponent ≥ 0 := not_lt.mp hexp
      rw [if_neg hexp, if_pos hexp'] at hok
      simp only [start_guard_round] at hok
      -- guard rounds to -1 everywhere, so the round-up condition is false
      cases hgrow : Number.to_rep.grow (if n.negative_ then -n.mantissa else n.mantissa) n.exponent with
      | error e => rw [hgrow] at hok; exact absurd hok (by simp)
      | ok drops =>
        rw [hgrow] at hok
        simp only at hok
        rw [if_neg (show ¬ ((-1 : Int) == 1 || (-1 : Int) == 0 && drops % 2 == 1) = true from by
          simp)] at hok
        have hr : r = (if n.negative_ then -drops else drops) := by injection hok with h; exact h.symm
        -- exact: drops.toInt = magM * 10^exp
        have hdrops := grow_ok_eq _ drops n.exponent hD0_nonneg hgrow
        rw [hmag] at hdrops
        -- r.toInt = M * 10^exp = n.toRat
        have hdrops_nn : 0 ≤ drops.toInt := by rw [hdrops]; positivity
        have hsign : n.mantissa.toInt
            = (if n.negative_ then -1 else 1) * (n.mantissa.toInt.natAbs : ℤ) := by
          rcases mantissa_sign n with ⟨hsn, hsp⟩
          by_cases hneg : n.negative_
          · rw [if_pos hneg, Int.ofNat_natAbs_of_nonpos (hsn hneg)]; ring
          · rw [if_neg hneg, Int.natAbs_of_nonneg (hsp (by simpa using hneg))]; ring
        have hrInt : (r.toInt : ℤ) = n.mantissa.toInt * 10 ^ n.exponent.toNat := by
          have hrsign : (r.toInt : ℤ) = (if n.negative_ then -1 else 1) * drops.toInt := by
            rw [hr]
            by_cases hneg : n.negative_
            · rw [if_pos hneg, toInt_neg_of_nonneg _ hdrops_nn, if_pos hneg]; ring
            · rw [if_neg hneg, if_neg hneg]; ring
          rw [hrsign, hdrops]
          conv_rhs => rw [hsign]
          ring
        have htoRat : n.toRat = (n.mantissa.toInt : ℚ) * (10 : ℚ) ^ n.exponent := by
          rw [mantissa_mul_exponent_eq_toRat n hn]
        have hpow : (10 : ℚ) ^ n.exponent = (10 : ℚ) ^ n.exponent.toNat := by
          rw [← zpow_natCast]; congr 1; omega
        rw [htoRat, hpow, hrInt]
        push_cast
        rw [sub_self, abs_zero]; norm_num

/-- When `0 ≤ n.exponent`, `to_rep` is exact: the magnitude is grown by an exact
power of ten with no digits dropped, so the returned integer equals the value. -/
theorem to_rep_exact_of_exponent_nonneg (n : Number) (mode : rounding_mode) (r : Int64)
    (hn : n.isNormalized) (hexp : 0 ≤ n.exponent)
    (hok : n.to_rep mode = .ok r) :
    (r.toInt : ℚ) = n.toRat := by
  unfold Number.to_rep at hok
  simp only at hok
  by_cases hz : (n.mantissa == 0) = true
  · rw [if_pos hz] at hok
    have hr : r = 0 := by injection hok with h; exact h.symm
    have hmant : n.mantissa.toInt = 0 := by rw [beq_iff_eq] at hz; rw [hz]; decide
    have htr : n.toRat = 0 := by rw [← mantissa_mul_exponent_eq_toRat n hn, hmant]; norm_num
    rw [hr, htr]; decide
  · rw [if_neg hz] at hok
    have hmant_ne : n.mantissa.toInt ≠ 0 := by
      intro hc; apply hz; rw [beq_iff_eq]
      exact Int64.toInt_inj.mp (by rw [hc]; decide)
    have hmag : (if n.negative_ then -n.mantissa else n.mantissa).toInt
        = (n.mantissa.toInt.natAbs : ℤ) := magnitude_toInt n hn
    have hD0_nonneg : 0 ≤ (if n.negative_ then -n.mantissa else n.mantissa).toInt := by
      rw [hmag]; positivity
    have hge : ¬ n.exponent < 0 := by omega
    rw [if_neg hge, if_pos hexp] at hok
    simp only [start_guard_round] at hok
    cases hgrow : Number.to_rep.grow (if n.negative_ then -n.mantissa else n.mantissa) n.exponent with
    | error e => rw [hgrow] at hok; exact absurd hok (by simp)
    | ok drops =>
      rw [hgrow] at hok
      simp only at hok
      rw [if_neg (show ¬ ((-1 : Int) == 1 || (-1 : Int) == 0 && drops % 2 == 1) = true from by
        simp)] at hok
      have hr : r = (if n.negative_ then -drops else drops) := by injection hok with h; exact h.symm
      have hdrops := grow_ok_eq _ drops n.exponent hD0_nonneg hgrow
      rw [hmag] at hdrops
      have hdrops_nn : 0 ≤ drops.toInt := by rw [hdrops]; positivity
      have hsign : n.mantissa.toInt
          = (if n.negative_ then -1 else 1) * (n.mantissa.toInt.natAbs : ℤ) := by
        rcases mantissa_sign n with ⟨hsn, hsp⟩
        by_cases hneg : n.negative_
        · rw [if_pos hneg, Int.ofNat_natAbs_of_nonpos (hsn hneg)]; ring
        · rw [if_neg hneg, Int.natAbs_of_nonneg (hsp (by simpa using hneg))]; ring
      have hrInt : (r.toInt : ℤ) = n.mantissa.toInt * 10 ^ n.exponent.toNat := by
        have hrsign : (r.toInt : ℤ) = (if n.negative_ then -1 else 1) * drops.toInt := by
          rw [hr]
          by_cases hneg : n.negative_
          · rw [if_pos hneg, toInt_neg_of_nonneg _ hdrops_nn, if_pos hneg]; ring
          · rw [if_neg hneg, if_neg hneg]; ring
        rw [hrsign, hdrops]; conv_rhs => rw [hsign]
        ring
      have htoRat : n.toRat = (n.mantissa.toInt : ℚ) * (10 : ℚ) ^ n.exponent := by
        rw [mantissa_mul_exponent_eq_toRat n hn]
      have hpow : (10 : ℚ) ^ n.exponent = (10 : ℚ) ^ n.exponent.toNat := by
        rw [← zpow_natCast]; congr 1; omega
      rw [htoRat, hpow, hrInt]; push_cast; ring

end XRPL.Model.Protocol
