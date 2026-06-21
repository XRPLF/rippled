import Mathlib.Tactic
import XRPL.Model.Protocol.MPTAmount
import XRPL.Properties.Protocol.Common.AmountArith

/-! # Proof body for the `MPTAmount.mulRatio` correctness headline. -/

namespace XRPL.Model.Protocol

/-- **`mulRatio` rounds correctly (floor for `roundUp = false`, ceil for `roundUp = true`).**
On a successful (`.ok`) result the value is either saturated to `Int64.minValue`
(negative underflow) or the correctly-rounded quotient of `amt * num / den`. -/
theorem MPTAmount.mulRatio_rounds_proof (amt : MPTAmount) (num den : UInt32) (roundUp : Bool)
    (result : MPTAmount)
    (hok : MPTAmount.mulRatio amt num den roundUp = .ok result) :
    result.value_ = Int64.minValue ∨
      (if roundUp
       then amt.toRat * (num.toNat : ℚ) / (den.toNat : ℚ) ≤ result.toRat ∧
            result.toRat - 1 < amt.toRat * (num.toNat : ℚ) / (den.toNat : ℚ)
       else result.toRat ≤ amt.toRat * (num.toNat : ℚ) / (den.toNat : ℚ) ∧
            amt.toRat * (num.toNat : ℚ) / (den.toNat : ℚ) < result.toRat + 1) := by
  -- `den = 0` makes `mulRatio` error, contradicting `hok`; the real work is `den ≠ 0`.
  by_cases hden : den = 0
  · rw [show MPTAmount.mulRatio amt num den roundUp = Except.error "division by zero" from by
          unfold MPTAmount.mulRatio; rw [if_pos (by simpa using hden)]] at hok
    exact absurd hok (by simp)
  set m : ℤ := amt.value_.toInt * (num.toNat : ℤ) with hm_def
  set d : ℤ := (den.toNat : ℤ) with hd_def
  set R : ℤ := if m.tmod d ≠ 0 then
                 if (!decide (amt.value_ < 0) && roundUp) = true then m.tdiv d + 1
                 else if (decide (amt.value_ < 0) && !roundUp) = true then m.tdiv d - 1 else m.tdiv d
               else m.tdiv d with hR_def
  -- `d > 0`
  have hdnat : den.toNat ≠ 0 := by
    intro h; exact hden (by simpa using (UInt32.toNat_inj.mp (by simpa using h)))
  have hd : (0 : ℤ) < d := by rw [hd_def]; exact_mod_cast Nat.pos_of_ne_zero hdnat
  -- reduce the function call to a clean three-way if on `R`
  have key : MPTAmount.mulRatio amt num den roundUp =
      (if R > Int64.maxValue.toInt then Except.error "MPT mulRatio overflow"
       else if R < Int64.minValue.toInt then Except.ok ⟨Int64.minValue⟩
       else Except.ok ⟨R.toInt64⟩) := by
    unfold MPTAmount.mulRatio; rw [if_neg (by simpa using hden)]
  rw [key] at hok
  split_ifs at hok with hMax hMin
  · -- saturation (the overflow `error = ok` case is discharged by `split_ifs`)
    left; exact (congrArg MPTAmount.value_ (Except.ok.inj hok)).symm
  · -- normal: result = ⟨R.toInt64⟩
    right
    have hMax' : R ≤ Int64.maxValue.toInt := not_lt.mp hMax
    have hMin' : Int64.minValue.toInt ≤ R := not_lt.mp hMin
    have hresult : result = ⟨R.toInt64⟩ := (Except.ok.inj hok).symm
    have hround : (R.toInt64).toInt = R := AmountArith.toInt_toInt64_self hMin' hMax'
    have hrR : result.toRat = (R : ℚ) := by
      rw [hresult]; unfold MPTAmount.toRat; rw [hround]
    have htruth : amt.toRat * (num.toNat : ℚ) / (den.toNat : ℚ) = (m : ℚ) / (d : ℚ) := by
      rw [hm_def, hd_def]; unfold MPTAmount.toRat; push_cast; ring
    -- sign bridge: relate the truncation flag `amt.value_ < 0` to the sign of `m`
    have hz : ((0 : Int64)).toInt = 0 := by decide
    have hlt0 : (amt.value_ < 0) ↔ amt.value_.toInt < 0 := by
      rw [Int64.lt_iff_toInt_lt, hz]
    have hN : (0 : ℤ) ≤ (num.toNat : ℤ) := Int.natCast_nonneg _
    have hneg_lo : m < 0 → decide (amt.value_ < 0) = true := by
      intro hmlt
      have hD : amt.value_.toInt < 0 := by rw [hm_def] at hmlt; nlinarith [hmlt, hN]
      exact decide_eq_true (hlt0.mpr hD)
    have hneg_hi : 0 < m → decide (amt.value_ < 0) = false := by
      intro hmgt
      have hD : 0 < amt.value_.toInt := by rw [hm_def] at hmgt; nlinarith [hmgt, hN]
      exact decide_eq_false (by rw [hlt0]; omega)
    have hbrk := AmountArith.mulRatio_int_bracket m d R hd roundUp (decide (amt.value_ < 0))
      hneg_lo hneg_hi rfl
    rw [hrR, htruth]
    cases roundUp
    · simp only [Bool.false_eq_true, if_false] at hbrk ⊢
      exact AmountArith.rat_floor_bracket hd hbrk.1 hbrk.2
    · simp only [if_true] at hbrk ⊢
      exact AmountArith.rat_ceil_bracket hd hbrk.1 hbrk.2

end XRPL.Model.Protocol
