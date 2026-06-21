import XRPL.Model.Basics.BaseUInt
import XRPL.Model.Basics.Blob
import XRPL.Model.Protocol.AccountID
import XRPL.Model.Protocol.Number
import XRPL.Model.Protocol.STAmount


namespace XRPL.Model.Protocol

structure AccountRoot where
  key : UInt256 := 0
  flags : UInt32 := 0
  account : AccountID := xrpAccount
  sequence : UInt32 := 0
  balance : STAmount := STAmount.ofNativeInt64 0
  ownerCount : UInt32 := 0
  previousTxnID : UInt256 := 0
  previousTxnLgrSeq : UInt32 := 0
  accountTxnID : Option UInt256 := none
  regularKey : Option AccountID := none
  emailHash : Option UInt128 := none
  walletLocator : Option UInt256 := none
  walletSize : Option UInt32 := none
  messageKey : Option Blob := none
  transferRate : Option UInt32 := none
  domain : Option Blob := none
  tickSize : Option UInt8 := none
  ticketCount : Option UInt32 := none
  nftokenMinter : Option AccountID := none
  mintedNFTokens : UInt32 := 0
  burnedNFTokens : UInt32 := 0
  firstNFTokenSequence : Option UInt32 := none
  ammID : Option UInt256 := none
  vaultID : Option UInt256 := none
  loanBrokerID : Option UInt256 := none
  deriving Repr

def AccountRoot.empty : AccountRoot := {}

def AccountRoot.isFlag (sle : AccountRoot) (f : UInt32) : Bool := (sle.flags &&& f) == f

-- Pseudo-account designator fields (AccountRoot fields flagged kSmdPseudoAccount
-- in rippled)
inductive PseudoField where
  | vaultID
  | loanBrokerID
  deriving DecidableEq, Repr, BEq

def AccountRoot.isPseudoAccount (sle : AccountRoot) : Bool :=
  sle.vaultID.isSome || sle.loanBrokerID.isSome || sle.ammID.isSome

end XRPL.Model.Protocol
