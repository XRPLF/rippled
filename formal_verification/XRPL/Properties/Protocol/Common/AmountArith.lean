import Mathlib.Tactic
import XRPL.Model.Protocol.Number

/-! # Shared `Int64`/`ℤ` arithmetic helpers for the integer amount types

`XRPAmount` and `MPTAmount` are both single signed `Int64` wrappers with identical
raw arithmetic and an identical `mulRatio` (truncated divide, `±1` rounding nudge,
overflow error, underflow saturation). These generic `Int64`/`ℤ` facts are shared by both proof trees: the
no-overflow `bmod` collapse, the `Int.toInt64` round-trip, and the floor/ceil
brackets behind `mulRatio` rounding. -/

namespace XRPL.Model.Protocol.AmountArith

/-- An `Int64`-range integer is its own balanced residue mod `2⁶⁴`. The no-overflow
collapse of `Int64.toInt_{add,sub,neg,mul}`. -/
lemma toInt_bmod_self {n : ℤ}
    (h_lo : Int64.minValue.toInt ≤ n) (h_hi : n ≤ Int64.maxValue.toInt) :
    n.bmod (2 ^ 64) = n := by
  have hmin : Int64.minValue.toInt = (-9223372036854775808 : ℤ) := by decide
  have hmax : Int64.maxValue.toInt = (9223372036854775807 : ℤ) := by decide
  rw [hmin] at h_lo
  rw [hmax] at h_hi
  rw [Int.bmod_eq_iff (by norm_num)]
  refine ⟨by norm_num; omega, by norm_num; omega, by simp⟩

/-- An `Int64`-range integer round-trips through `Int.toInt64`. -/
lemma toInt_toInt64_self {n : ℤ}
    (h_lo : Int64.minValue.toInt ≤ n) (h_hi : n ≤ Int64.maxValue.toInt) :
    (n.toInt64).toInt = n := by
  rw [show n.toInt64 = Int64.ofInt n from rfl, Int64.toInt_ofInt,
      show Int64.size = 2 ^ 64 from rfl, toInt_bmod_self h_lo h_hi]

/-- `tmod` of a nonpositive dividend is nonpositive. -/
lemma tmod_nonpos {m : ℤ} (d : ℤ) (h : m ≤ 0) : m.tmod d ≤ 0 := by
  have hneg : (-m).tmod d = -(m.tmod d) := Int.neg_tmod m d
  have h2 : 0 ≤ (-m).tmod d := Int.tmod_nonneg d (by omega)
  omega

/-- The rational floor bracket from the integer floor bracket (`d > 0`). -/
lemma rat_floor_bracket {m d r : ℤ} (hd : 0 < d)
    (h1 : r * d ≤ m) (h2 : m < (r + 1) * d) :
    (r : ℚ) ≤ (m : ℚ) / (d : ℚ) ∧ (m : ℚ) / (d : ℚ) < (r : ℚ) + 1 := by
  have hdQ : (0 : ℚ) < (d : ℚ) := by exact_mod_cast hd
  refine ⟨?_, ?_⟩
  · rw [le_div_iff₀ hdQ]; exact_mod_cast h1
  · rw [div_lt_iff₀ hdQ]; exact_mod_cast h2

/-- The rational ceil bracket from the integer ceil bracket (`d > 0`). -/
lemma rat_ceil_bracket {m d r : ℤ} (hd : 0 < d)
    (h1 : m ≤ r * d) (h2 : (r - 1) * d < m) :
    (m : ℚ) / (d : ℚ) ≤ (r : ℚ) ∧ (r : ℚ) - 1 < (m : ℚ) / (d : ℚ) := by
  have hdQ : (0 : ℚ) < (d : ℚ) := by exact_mod_cast hd
  refine ⟨?_, ?_⟩
  · rw [div_le_iff₀ hdQ]; exact_mod_cast h1
  · rw [lt_div_iff₀ hdQ]; exact_mod_cast h2

/-- **Integer bracket for the `mulRatio` rounding choice.**
With `d > 0`, the rounded quotient `r` brackets the exact ratio `m / d`:
floor when `roundUp = false`, ceil when `roundUp = true`. `neg` is the truncation
sign flag, which the caller links to the sign of `m`. -/
lemma mulRatio_int_bracket (m d r : ℤ) (hd : 0 < d) (roundUp neg : Bool)
    (hneg_lo : m < 0 → neg = true) (hneg_hi : 0 < m → neg = false)
    (hr : r = if m.tmod d ≠ 0 then
                if (!neg && roundUp) = true then m.tdiv d + 1
                else if (neg && !roundUp) = true then m.tdiv d - 1 else m.tdiv d
              else m.tdiv d) :
    (if roundUp then m ≤ r * d ∧ (r - 1) * d < m
     else r * d ≤ m ∧ m < (r + 1) * d) := by
  set q := m.tdiv d with hq
  have heucl : d * q + m.tmod d = m := Int.mul_tdiv_add_tmod m d
  have hqd : d * q = m - m.tmod d := by linarith [heucl]
  by_cases hs0 : m.tmod d = 0
  · -- exact: r = q
    have hrq : r = q := by rw [hr, if_neg (not_not.mpr hs0)]
    rw [hs0] at hqd
    cases roundUp <;> simp only [hrq, Bool.false_eq_true, if_false, if_true] <;>
      exact ⟨by nlinarith [hqd, hd], by nlinarith [hqd, hd]⟩
  · -- inexact: m ≠ 0
    have hlt : m.tmod d < d := Int.tmod_lt_of_pos m hd
    have hgt : -d < m.tmod d := Int.lt_tmod_of_pos m hd
    have hm0 : m ≠ 0 := fun h => hs0 (by rw [h]; exact Int.zero_tmod d)
    rcases lt_or_gt_of_ne hm0 with hmneg | hmpos
    · -- m < 0  ⇒  neg = true, tmod < 0
      have hsle : m.tmod d ≤ 0 := tmod_nonpos d hmneg.le
      have hslt : m.tmod d < 0 := lt_of_le_of_ne hsle hs0
      have hnegT : neg = true := hneg_lo hmneg
      subst hnegT
      cases roundUp
      · -- roundUp = false ⇒ r = q - 1
        have hrq : r = q - 1 := by rw [hr, if_pos hs0]; simp
        simp only [Bool.false_eq_true, if_false, hrq]
        exact ⟨by nlinarith [hqd, hslt, hgt], by nlinarith [hqd, hslt, hgt]⟩
      · -- roundUp = true ⇒ r = q
        have hrq : r = q := by rw [hr, if_pos hs0]; simp
        simp only [if_true, hrq]
        exact ⟨by nlinarith [hqd, hslt, hgt], by nlinarith [hqd, hslt, hgt]⟩
    · -- m > 0  ⇒  neg = false, tmod > 0
      have hsge : 0 ≤ m.tmod d := Int.tmod_nonneg d hmpos.le
      have hsgt : 0 < m.tmod d := lt_of_le_of_ne hsge (Ne.symm hs0)
      have hnegF : neg = false := hneg_hi hmpos
      subst hnegF
      cases roundUp
      · -- roundUp = false ⇒ r = q
        have hrq : r = q := by rw [hr, if_pos hs0]; simp
        simp only [Bool.false_eq_true, if_false, hrq]
        exact ⟨by nlinarith [hqd, hsgt, hlt], by nlinarith [hqd, hsgt, hlt]⟩
      · -- roundUp = true ⇒ r = q + 1
        have hrq : r = q + 1 := by rw [hr, if_pos hs0]; simp
        simp only [if_true, hrq]
        exact ⟨by nlinarith [hqd, hsgt, hlt], by nlinarith [hqd, hsgt, hlt]⟩

end XRPL.Model.Protocol.AmountArith
