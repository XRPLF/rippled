import Mathlib.Tactic

import XRPL.Model.Protocol.Number

/-!
The `largeRange` mantissa bounds as explicit naturals.

Kept in a leaf file of their own so that any proof file can import the
ground-truth values alone, without the lemmas built on top of them.
-/

namespace XRPL.Model.Protocol

/-- The lower bound of `largeRange` as a natural number: `10^18`. -/
lemma largeRange_min_val : largeRange.min.toNat = 1000000000000000000 := by decide

/-- The upper bound of `largeRange` as a natural number: `10^19 - 1`. -/
lemma largeRange_max_val : largeRange.max.toNat = 9999999999999999999 := by decide

end XRPL.Model.Protocol
