import XRPL.Model.Basics.BaseUInt
import XRPL.Model.Protocol.AcceptedCredential
import XRPL.Model.Protocol.AccountID
import XRPL.Model.Protocol.LedgerEntries.Credential


namespace XRPL.Model.Protocol

structure DepositPreauth where
  key : UInt256 := 0
  flags : UInt32 := 0
  account : AccountID := xrpAccount
  authorize : Option AccountID := none
  ownerNode : UInt64 := 0
  previousTxnID : UInt256 := 0
  previousTxnLgrSeq : UInt32 := 0
  authorizeCredentials : Option (List AcceptedCredential) := none
  deriving Repr

def DepositPreauth.empty : DepositPreauth := {}

-- Two `keylet::depositPreauth` overloads.
inductive DepositPreauthKey where
  | byAccount (owner authorized : AccountID)
  | byCredentials (owner : AccountID) (authCreds : List AcceptedCredential)
  deriving DecidableEq, Hashable

end XRPL.Model.Protocol
