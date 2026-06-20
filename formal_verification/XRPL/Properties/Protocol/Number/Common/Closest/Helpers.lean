import XRPL.Properties.Protocol.Number.Common.Notation
import Mathlib.Tactic

import XRPL.Model.Protocol.Number
import XRPL.Properties.Protocol.Number.Common.Constants
import XRPL.Properties.Protocol.Number.Common.ToRatLemmas


namespace XRPL.Model.Protocol

/-! # Discrete-grid primitives for representable Numbers

For any rational `q`, define:
- `Number.upper q` — the smallest representable Number with `n.toRat ≥ q`, if it exists
- `Number.lower q` — the largest representable Number with `n.toRat ≤ q`, if it exists

These are the foundation for interval arithmetic over Numbers and for the
`_rounded` theorems that state operator correctness in discrete terms.

The set of valid (normalized) Number mantissas in `largeRange` is asymmetric
near the cusp:
- `[10^18, maxRep]`: every integer is valid (spacing 1)
- `(maxRep, 2^63 + 2)`: NO valid mantissas (the cusp gap)
- `2^63 + 2 = maxRepCuspTarget`: valid (smallest above maxRep)
- Above: only multiples of 10 (spacing 10)
- Up to `10^19 - 10`: valid

Internal helpers `bumpToValidMantissa` (round up) and `truncToValidMantissa`
(round down) encapsulate the cusp logic. The top-level `Number.upper` and
`Number.lower` use them along with exponent normalization via `Int.log 10`. -/

/-- Smallest mantissa value strictly greater than `maxRep` that satisfies the
divisibility-by-10 invariant. Equals `2^63 + 2 = maxRepCuspTarget`. -/
def cuspMin : ℕ := maxRepCuspTarget

lemma cuspMin_div_ten : cuspMin / 10 = mantissaFloorSucc := by decide

lemma cuspMin_mod_ten : cuspMin % 10 = 0 := by decide

lemma cuspMin_lt_pow19 : cuspMin < 10^19 := by decide

/-! ## Cusp helpers — round a candidate mantissa up/down to a valid value -/

/-- Round a candidate mantissa up to the next valid mantissa value, respecting
the cusp's divisibility-by-10 invariant.

Returns `none` if the result would exceed `10^19 - 10`; the caller must bump
the exponent in that case.

Three cases:
1. `m ≤ maxRep`: already valid (integer grid is dense below the cusp)
2. `maxRep < m < 2^63 + 2`: in the cusp gap; bump up to `2^63 + 2`
3. `m ≥ 2^63 + 2`: round up to next multiple of 10 -/
def bumpToValidMantissa (m : ℕ) : Option ℕ :=
  if m ≤ maxRep.toNat then
    some m
  else if m < cuspMin then
    some cuspMin
  else
    let rounded := ((m + 9) / 10) * 10
    if rounded < 10^19 then some rounded else none

/-- Round a candidate mantissa down to the previous valid mantissa value.

Three cases:
1. `m ≤ maxRep`: already valid
2. `maxRep < m < 2^63 + 2`: in the cusp gap; fall back to `maxRep`
3. `m ≥ 2^63 + 2`: round down to nearest multiple of 10 -/
def truncToValidMantissa (m : ℕ) : ℕ :=
  if m ≤ maxRep.toNat then
    m
  else if m < cuspMin then
    maxRep.toNat
  else
    (m / 10) * 10

/-! ## Properties of the cusp helpers -/

/-- `bumpToValidMantissa` is monotone-up: output ≥ input. -/
lemma bumpToValidMantissa_ge (m : ℕ) (m_out : ℕ)
    (h : bumpToValidMantissa m = some m_out) :
    m ≤ m_out := by
  unfold bumpToValidMantissa at h
  split_ifs at h with h1 h2
  · exact le_of_eq (Option.some.inj h)
  · have h_eq : m_out = cuspMin := (Option.some.inj h).symm
    rw [h_eq]; omega
  · by_cases hr : ((m + 9) / 10) * 10 < 10^19
    · rw [if_pos hr] at h
      have h_eq : m_out = ((m + 9) / 10) * 10 := (Option.some.inj h).symm
      rw [h_eq]
      have h_div : 10 * ((m + 9) / 10) + (m + 9) % 10 = m + 9 := Nat.div_add_mod (m + 9) 10
      have h_mod : (m + 9) % 10 < 10 := Nat.mod_lt _ (by omega)
      omega
    · rw [if_neg hr] at h
      exact absurd h (by simp)

/-- `truncToValidMantissa` is monotone-down: output ≤ input. -/
lemma truncToValidMantissa_le (m : ℕ) : truncToValidMantissa m ≤ m := by
  change (if m ≤ maxRep.toNat then m
        else if m < cuspMin then maxRep.toNat
        else (m / 10) * 10) ≤ m
  split_ifs with h1 h2
  · exact Nat.le_refl m
  · omega
  · exact Nat.div_mul_le_self m 10

/-- `bumpToValidMantissa`'s output is in the valid set (≤ maxRep or multiple of 10). -/
lemma bumpToValidMantissa_valid (m : ℕ) (m_out : ℕ)
    (h : bumpToValidMantissa m = some m_out) :
    m_out ≤ maxRep.toNat ∨ m_out % 10 = 0 := by
  unfold bumpToValidMantissa at h
  split_ifs at h with h1 h2
  · left
    have h_eq : m_out = m := (Option.some.inj h).symm
    rw [h_eq]; exact h1
  · right
    have h_eq : m_out = cuspMin := (Option.some.inj h).symm
    rw [h_eq]; exact cuspMin_mod_ten
  · by_cases hr : ((m + 9) / 10) * 10 < 10^19
    · rw [if_pos hr] at h
      right
      have h_eq : m_out = ((m + 9) / 10) * 10 := (Option.some.inj h).symm
      rw [h_eq]
      exact Nat.mul_mod_left _ _
    · rw [if_neg hr] at h
      exact absurd h (by simp)

/-- `bumpToValidMantissa`'s output is below `10^19`. -/
lemma bumpToValidMantissa_lt_pow19 (m : ℕ) (m_out : ℕ)
    (h : bumpToValidMantissa m = some m_out) :
    m_out < 10^19 := by
  unfold bumpToValidMantissa at h
  split_ifs at h with h1 h2
  · have h_eq : m_out = m := (Option.some.inj h).symm
    rw [h_eq]
    have hmax : maxRep.toNat = maxRepNat := maxRep_val
    have : m ≤ maxRep.toNat := h1
    omega
  · have h_eq : m_out = cuspMin := (Option.some.inj h).symm
    rw [h_eq]; exact cuspMin_lt_pow19
  · by_cases hr : ((m + 9) / 10) * 10 < 10^19
    · rw [if_pos hr] at h
      have h_eq : m_out = ((m + 9) / 10) * 10 := (Option.some.inj h).symm
      rw [h_eq]; exact hr
    · rw [if_neg hr] at h
      exact absurd h (by simp)

/-- `truncToValidMantissa`'s output is in the valid set. -/
lemma truncToValidMantissa_valid (m : ℕ) :
    truncToValidMantissa m ≤ maxRep.toNat ∨ truncToValidMantissa m % 10 = 0 := by
  unfold truncToValidMantissa
  split_ifs with h1 h2
  · left; exact h1
  · left; exact le_refl _
  · right; exact Nat.mul_mod_left _ _

/-- `bumpToValidMantissa` produces the SMALLEST valid mantissa ≥ input. -/
lemma bumpToValidMantissa_tight (m : ℕ) (m_out : ℕ)
    (h : bumpToValidMantissa m = some m_out)
    (k : ℕ) (h_k_valid : k ≤ maxRep.toNat ∨ (maxRep.toNat < k ∧ k % 10 = 0 ∧ k < 10 ^ 19))
    (h_k_ge : m ≤ k) :
    m_out ≤ k := by
  unfold bumpToValidMantissa at h
  split_ifs at h with h1 h2
  · have h_eq : m_out = m := (Option.some.inj h).symm
    rw [h_eq]; exact h_k_ge
  · have h_eq : m_out = cuspMin := (Option.some.inj h).symm
    rw [h_eq]
    rcases h_k_valid with hk_le | ⟨hk_gt, hk_mod, _⟩
    · omega
    · have hmax : maxRep.toNat = maxRepNat := maxRep_val
      have hcusp : cuspMin = maxRepCuspTarget := rfl
      omega
  · by_cases hr : ((m + 9) / 10) * 10 < 10^19
    · rw [if_pos hr] at h
      have h_eq : m_out = ((m + 9) / 10) * 10 := (Option.some.inj h).symm
      rw [h_eq]
      rcases h_k_valid with hk_le | ⟨_, hk_mod, _⟩
      · have hmax : maxRep.toNat = maxRepNat := maxRep_val
        have hcusp : cuspMin = maxRepCuspTarget := rfl
        omega
      · have h_km : k = (k / 10) * 10 := by
          have := Nat.div_add_mod k 10
          omega
        have h_div_le : (m + 9) / 10 ≤ k / 10 := by
          have h_le : (m + 9) / 10 ≤ (k + 9) / 10 := Nat.div_le_div_right (by omega)
          have hk_div : 10 * (k / 10) + k % 10 = k := Nat.div_add_mod k 10
          have hk9_div : 10 * ((k + 9) / 10) + (k + 9) % 10 = k + 9 := Nat.div_add_mod (k + 9) 10
          have hk9_mod : (k + 9) % 10 < 10 := Nat.mod_lt _ (by omega)
          omega
        calc (m + 9) / 10 * 10 ≤ (k / 10) * 10 := by
              exact Nat.mul_le_mul_right 10 h_div_le
          _ = k := h_km.symm
    · rw [if_neg hr] at h
      exact absurd h (by simp)

/-- `truncToValidMantissa` produces the LARGEST valid mantissa ≤ input. -/
lemma truncToValidMantissa_tight (m : ℕ)
    (k : ℕ) (h_k_valid : k ≤ maxRep.toNat ∨ (maxRep.toNat < k ∧ k % 10 = 0 ∧ k < 10 ^ 19))
    (h_k_le : k ≤ m) :
    k ≤ truncToValidMantissa m := by
  unfold truncToValidMantissa
  split_ifs with h1 h2
  · exact h_k_le
  · -- cuspMin is the smallest multiple of 10 above maxRep, so no valid k fits in the gap
    rcases h_k_valid with hk_le | ⟨hk_gt, hk_mod, _⟩
    · exact hk_le
    · exfalso
      have hmax : maxRep.toNat = maxRepNat := maxRep_val
      have hcusp : cuspMin = maxRepCuspTarget := rfl
      have hk_div : 10 * (k / 10) + k % 10 = k := Nat.div_add_mod k 10
      omega
  · have hmax : maxRep.toNat = maxRepNat := maxRep_val
    have hcusp : cuspMin = maxRepCuspTarget := rfl
    have h_m_div : 10 * (m / 10) + m % 10 = m := Nat.div_add_mod m 10
    have h_m_mod : m % 10 < 10 := Nat.mod_lt _ (by omega)
    have h_m_div_ge : m / 10 ≥ mantissaFloorSucc := by omega
    rcases h_k_valid with hk_le | ⟨hk_gt, hk_mod, _⟩
    · have h_lower : (m / 10) * 10 ≥ maxRepCuspTarget := by
        have := Nat.mul_le_mul_right 10 h_m_div_ge
        omega
      omega
    · have hk_div : 10 * (k / 10) + k % 10 = k := Nat.div_add_mod k 10
      have h_div_le : k / 10 ≤ m / 10 := Nat.div_le_div_right h_k_le
      have : k = 10 * (k / 10) := by omega
      calc k = 10 * (k / 10) := this
        _ = (k / 10) * 10 := by ring
        _ ≤ (m / 10) * 10 := Nat.mul_le_mul_right 10 h_div_le

end XRPL.Model.Protocol
