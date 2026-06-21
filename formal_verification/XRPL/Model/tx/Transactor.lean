import XRPL.Model.Ledger.ApplyView
import XRPL.Model.Protocol.LedgerEntries.Vault
import XRPL.Model.Protocol.STAmount
import XRPL.Model.Protocol.TER

-- import XRPL.Protocol.STTx
import XRPL.Model.Protocol.Asset
import XRPL.Model.Protocol.AccountID


namespace XRPL.Model.tx

open XRPL.Model.Ledger
open XRPL.Model.Protocol

class Tx (α : Type) where
  txID : α → String
  account : α → AccountID

structure PreflightContext (α : Type) [Tx α] where
  tx : α

structure PreclaimContext (α : Type) [Tx α] where
  tx : α

end XRPL.Model.tx
