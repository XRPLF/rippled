import Mathlib.Tactic

import XRPL.Model.Protocol.Number


namespace XRPL.Model.Protocol

/-- `largeRange.min.toNat = 10^18`. -/
lemma largeRange_min_val : largeRange.min.toNat = 1000000000000000000 := by decide

/-- `largeRange.max.toNat = 10^19 - 1`. -/
lemma largeRange_max_val : largeRange.max.toNat = 9999999999999999999 := by decide

end XRPL.Model.Protocol
