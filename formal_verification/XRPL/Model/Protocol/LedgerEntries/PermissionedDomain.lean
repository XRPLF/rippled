import XRPL.Model.Basics.BaseUInt
import XRPL.Model.Protocol.AcceptedCredential
import XRPL.Model.Protocol.AccountID

set_option linter.style.longLine false
set_option linter.style.emptyLine false

namespace XRPL.Model.Protocol

structure PermissionedDomain where
  key : UInt256 := 0
  flags : UInt32 := 0
  owner : AccountID := xrpAccount
  sequence : UInt32 := 0
  acceptedCredentials : List AcceptedCredential := []
  ownerNode : UInt64 := 0
  previousTxnID : UInt256 := 0
  previousTxnLgrSeq : UInt32 := 0
  deriving Repr

def PermissionedDomain.empty : PermissionedDomain := {}

def PermissionedDomain.isFlag (sle : PermissionedDomain) (f : UInt32) : Bool := (sle.flags &&& f) == f

end XRPL.Model.Protocol
