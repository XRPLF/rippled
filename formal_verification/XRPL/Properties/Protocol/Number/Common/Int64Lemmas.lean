import XRPL.Properties.Protocol.Number.Rounding.DoRoundUp
import Mathlib.Tactic

set_option linter.style.longLine false
set_option linter.style.emptyLine false

namespace XRPL.Model.Protocol

/-! # Int64 ↔ ℤ bridges

`Int64` arithmetic is defined modulo `2^64` (`bmod`). The overflow guards in
`Number.to_rep` / `Number.from_rep` keep every intermediate magnitude below
`2^63`, so in our setting these operations agree with the corresponding integer
operations. -/

/-- Int64←UInt64 cast bridge: when a `UInt64` value fits in the non-negative
`Int64` range (`< 2^63`), `UInt64.toInt64` preserves the value. -/
lemma UInt64.toInt64_toInt_of_lt (u : UInt64) (h : u.toNat < 2 ^ 63) :
    u.toInt64.toInt = (u.toNat : ℤ) := by
  unfold Int64.toInt UInt64.toInt64
  change u.toBitVec.toInt = (u.toNat : ℤ)
  have hb : u.toBitVec.toNat = u.toNat := rfl
  rw [BitVec.toInt_eq_toNat_of_lt (by rw [hb]; omega), hb]

/-- Negated Int64←UInt64 cast bridge: when a `UInt64` value fits in the
non-negative `Int64` range (`< 2^63`), negating its `Int64` view negates its
integer value (the `bmod` wraparound is the identity here). -/
lemma UInt64.neg_toInt64_toInt_of_lt (u : UInt64) (h : u.toNat < 2 ^ 63) :
    (-u.toInt64).toInt = -(u.toNat : ℤ) := by
  rw [Int64.toInt_neg, UInt64.toInt64_toInt_of_lt u h, Int.bmod_eq_iff (by norm_num)]
  omega

/-- `10` as an `Int64` has integer value `10`. -/
lemma int64_ten_toInt : (10 : Int64).toInt = 10 := by decide

/-- `1` as an `Int64` has integer value `1`. -/
lemma int64_one_toInt : (1 : Int64).toInt = 1 := by decide

/-- Multiplying by `10` is exact while the value stays in the `Int64` range. -/
lemma toInt_mul_ten_of_le (a : Int64) (h0 : 0 ≤ a.toInt)
    (hle : a.toInt ≤ maxRep.toNat / 10) :
    (a * 10).toInt = a.toInt * 10 := by
  have hmax : (maxRep.toNat : ℤ) / 10 * 10 < 2 ^ 63 := by rw [maxRep_val]; norm_num
  have h0' : (0 : ℤ) ≤ a.toInt * 10 := by positivity
  rw [Int64.toInt_mul, int64_ten_toInt, Int.bmod_eq_iff (by norm_num)]
  have hub : a.toInt * 10 ≤ (maxRep.toNat : ℤ) / 10 * 10 := by
    have : a.toInt ≤ (maxRep.toNat : ℤ) / 10 := by exact_mod_cast hle
    nlinarith
  push_cast
  omega

/-- Negating a non-negative `Int64` is exact (the value is in `[0, 2^63)`, so its
negation is in `(-2^63, 0]`, within range). -/
lemma toInt_neg_of_nonneg (a : Int64) (h0 : 0 ≤ a.toInt) :
    (-a).toInt = -a.toInt := by
  have ha2 := Int64.toInt_lt a
  rw [Int64.toInt_neg, Int.bmod_eq_iff (by norm_num)]
  refine ⟨?_, ?_⟩ <;> push_cast <;> omega

/-- Dividing a non-negative `Int64` by `10` is exact (floor division). -/
lemma toInt_div_ten_of_nonneg (a : Int64) (h0 : 0 ≤ a.toInt) :
    (a / 10).toInt = a.toInt / 10 := by
  have ha2 := Int64.toInt_lt a
  have hdiv_nonneg : 0 ≤ a.toInt / 10 := Int.ediv_nonneg h0 (by norm_num)
  have hdiv_le : a.toInt / 10 ≤ a.toInt := Int.ediv_le_self _ h0
  rw [Int64.toInt_div, int64_ten_toInt, Int.bmod_eq_iff (by norm_num)]
  rw [Int.tdiv_eq_ediv_of_nonneg h0]
  omega

/-- `Int64`-mod by `10` of a non-negative value is exact. -/
lemma toInt_mod_ten_of_nonneg (a : Int64) (h0 : 0 ≤ a.toInt) :
    (a % 10).toInt = a.toInt % 10 := by
  rw [Int64.toInt_mod, int64_ten_toInt, Int.tmod_eq_emod_of_nonneg h0]

end XRPL.Model.Protocol
