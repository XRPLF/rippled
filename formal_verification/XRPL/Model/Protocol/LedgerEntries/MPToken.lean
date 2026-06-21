import XRPL.Model.Basics.BaseUInt
import XRPL.Model.Protocol.AccountID
import XRPL.Model.Protocol.UintTypes


namespace XRPL.Model.Protocol

structure MPToken where
  key : UInt256 := 0
  flags : UInt32 := 0
  account : AccountID := xrpAccount
  mptokenIssuanceID : MPTID := ⟨0⟩
  mptAmount : UInt64 := 0
  lockedAmount : Option UInt64 := none
  ownerNode : UInt64 := 0
  previousTxnID : UInt256 := 0
  previousTxnLgrSeq : UInt32 := 0
  deriving Repr

def MPToken.empty : MPToken := {}

def MPToken.isFlag (sle : MPToken) (f : UInt32) : Bool := (sle.flags &&& f) == f

end XRPL.Model.Protocol
