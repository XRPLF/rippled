import XRPL.Model.Basics.BaseUInt
import XRPL.Model.Basics.Blob
import XRPL.Model.Protocol.AccountID


namespace XRPL.Model.Protocol

structure Credential where
  key : UInt256 := 0
  flags : UInt32 := 0
  subject : AccountID := xrpAccount
  issuer : AccountID := xrpAccount
  credentialType : Blob := []
  expiration : Option UInt32 := none
  uri : Option Blob := none
  issuerNode : UInt64 := 0
  subjectNode : Option UInt64 := none
  previousTxnID : UInt256 := 0
  previousTxnLgrSeq : UInt32 := 0
  deriving Repr

def Credential.empty : Credential := {}

def Credential.isFlag (sle : Credential) (f : UInt32) : Bool := (sle.flags &&& f) == f

end XRPL.Model.Protocol
