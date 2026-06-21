import Mathlib.Tactic
import XRPL.Model.Protocol.MPTAmount

namespace XRPL.Model.Protocol

/-- A well-formed `MPTAmount`: its magnitude fits the protocol cap `maxMPTokenAmount`. -/
structure MPTAmount.Valid (a : MPTAmount) : Prop where
  in_range : a.value_.toInt.natAbs ≤ maxMPTokenAmount

end XRPL.Model.Protocol
