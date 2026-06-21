import XRPL.Model.Basics.BaseUInt
import XRPL.Model.Basics.Blob
import XRPL.Model.Protocol.AccountID
import XRPL.Model.Protocol.UintTypes


namespace XRPL.Model.Protocol

structure MPTokenIssuance where
  key : UInt256 := 0
  flags : UInt32 := 0
  issuer : AccountID := xrpAccount
  sequence : UInt32 := 0
  transferFee : UInt16 := 0
  ownerNode : UInt64 := 0
  assetScale : UInt8 := 0
  maximumAmount : Option UInt64 := none
  outstandingAmount : UInt64 := 0
  lockedAmount : Option UInt64 := none
  mptokenMetadata : Option Blob := none
  previousTxnID : UInt256 := 0
  previousTxnLgrSeq : UInt32 := 0
  domainID : Option UInt256 := none
  mutableFlags : UInt32 := 0
  referenceHolding : Option UInt256 := none
  deriving Repr

def MPTokenIssuance.empty : MPTokenIssuance := {}

def MPTokenIssuance.isFlag (i : MPTokenIssuance) (f : UInt32) : Bool := (i.flags &&& f) == f

-- The MPTID is the 192-bit (sequence ++ issuer), matching keylet/MPTID layout
def MPTokenIssuance.mptID (i : MPTokenIssuance) : MPTID :=
  ⟨BitVec.ofNat 192 (i.sequence.toNat * 2 ^ 160 + i.issuer.val.toNat)⟩

end XRPL.Model.Protocol
