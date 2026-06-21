import XRPL.Model.Basics.BaseUInt
import XRPL.Model.Basics.Blob
import XRPL.Model.Protocol.STTakesAsset


namespace XRPL.Model.Protocol

structure LoanBroker where
  key : UInt256 := 0
  flags : UInt32 := 0
  previousTxnID : UInt256 := 0
  previousTxnLgrSeq : UInt32 := 0
  sequence : UInt32 := 0
  ownerNode : UInt64 := 0
  vaultNode : UInt64 := 0
  vaultID : UInt256 := 0
  account : AccountID := xrpAccount
  owner : AccountID := xrpAccount
  loanSequence : UInt32 := 0
  data : Blob := []
  managementFeeRate : UInt16 := 0
  ownerCount : UInt32 := 0
  debtTotal : Option STNumber := none
  debtMaximum : Option STNumber := none
  coverAvailable : Option STNumber := none
  coverRateMinimum : UInt32 := 0
  coverRateLiquidation : UInt32 := 0

def LoanBroker.empty : LoanBroker := {}

def LoanBroker.associateAsset (b : LoanBroker) (asset : Asset) (mode : rounding_mode)
    : Except String LoanBroker := do
  let debtTotal ← associateNumberField asset mode b.debtTotal
  let debtMaximum ← associateNumberField asset mode b.debtMaximum
  let coverAvailable ← associateNumberField asset mode b.coverAvailable
  return { b with debtTotal, debtMaximum, coverAvailable }

end XRPL.Model.Protocol
