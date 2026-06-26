import XRPL.Model.Basics.BaseUInt
import XRPL.Model.Protocol.AccountID
import XRPL.Model.Protocol.XRPAmount


namespace XRPL.Model.tx

open XRPL.Model.Protocol

-- common fields present on every transaction
structure Tx where
  txID : UInt256 := 0
  account : AccountID := xrpAccount
  fee : XRPAmount := XRPAmount.zero
  sequence : UInt32 := 0
  flags : UInt32 := 0

end XRPL.Model.tx
