import XRPL.Model.Basics.BaseUInt
import XRPL.Model.Basics.Blob
import XRPL.Model.Protocol.Number
import XRPL.Model.Protocol.STAmount
import XRPL.Model.Protocol.STTakesAsset


namespace XRPL.Model.Protocol

structure Vault where
  key : UInt256 := 0
  flags : UInt32 := 0
  previousTxnID : UInt256 := 0
  previousTxnLgrSeq : UInt32 := 0
  sequence : UInt32 := 0
  ownerNode : UInt64 := 0
  owner : AccountID := xrpAccount
  pseudoID : AccountID := xrpAccount
  data : Option Blob := none
  asset : Asset := xrpAsset
  assetsTotal : Option STNumber := none
  assetsAvailable : Option STNumber := none
  assetsMaximum : Option STNumber := none
  lossUnrealized : Option STNumber := none
  shareMPTID : MPTID := ⟨0⟩
  withdrawalPolicy : UInt8 := 0
  scale : UInt8 := 0

def Vault.empty : Vault := {}

def Vault.associateAsset (v : Vault) (asset : Asset) (mode : rounding_mode)
    : Except String Vault := do
  let assetsTotal ← associateNumberField asset mode v.assetsTotal
  let assetsAvailable ← associateNumberField asset mode v.assetsAvailable
  let assetsMaximum ← associateNumberField asset mode v.assetsMaximum
  let lossUnrealized ← associateNumberField asset mode v.lossUnrealized
  return { v with assetsTotal, assetsAvailable, assetsMaximum, lossUnrealized }

end XRPL.Model.Protocol
