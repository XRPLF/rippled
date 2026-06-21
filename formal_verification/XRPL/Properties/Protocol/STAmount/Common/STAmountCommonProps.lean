import Mathlib.Tactic

import XRPL.Model.Protocol.STAmount
import XRPL.Properties.Protocol.MPTAmount.Common.Defs


namespace XRPL.Model.Protocol

-- Post-condition of `STAmount::canonicalize` on the MPT branch:
-- `mOffset = 0`, `mValue ≤ maxMPTokenAmount`, and zero is sign-cleared.
structure STAmount.MPTCanonical (s : STAmount) : Prop where
  offset_zero : s.mOffset = 0
  value_in_range : s.mValue.toNat ≤ maxMPTokenAmount
  zero_sign_cleared : s.mValue = 0 → s.mIsNegative = false

end XRPL.Model.Protocol
