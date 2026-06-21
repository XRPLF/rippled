import XRPL.Model.Ledger.ApplyView
import XRPL.Model.Protocol.LedgerFormats
import XRPL.Model.Protocol.Rate


namespace XRPL.Model.Ledger.Helpers

open XRPL.Model.Protocol
open XRPL.Model.Ledger

def confineOwnerCount (current : UInt32) (adjustment : Int32) : UInt32 :=
  let adjusted := current + adjustment.toUInt32
  if adjustment > 0 then
    if adjusted < current then maxUInt32 else adjusted
  else
    if adjusted > current then 0 else adjusted

def adjustOwnerCount (sle : Option AccountRoot) (amount : Int32) : ApplyView Unit := do
  let some sle := sle | return
  let current := sle.ownerCount
  let adjusted := confineOwnerCount current amount
  adjustOwnerCountHookStub sle.account current adjusted
  ApplyView.update (.accountRoot { sle with ownerCount := adjusted })


-- One pseudo-account-id attempt, delegated to rippled that returns RipeshaHasher digest
@[extern "cpp_pseudo_account_address_hash"]
opaque pseudoAccountAddressHash (i : UInt16) (parentHash pseudoOwnerKey : @& ByteArray) : ByteArray

def pseudoAccountAddress (pseudoOwnerKey : UInt256) : ReadView AccountID := do
  let view ← read
  let parentHashBytes := serUInt256 view.header.parentHash
  let ownerKeyBytes := serUInt256 pseudoOwnerKey
  for i in [0:256] do
    let ret := AccountID.fromRaw (pseudoAccountAddressHash i.toUInt16 parentHashBytes ownerKeyBytes)
    if (← ReadView.read (Keylet.account ret)).isNone then
      return ret
  return xrpAccount

-- featureSingleAssetVault / featureLendingProtocol assumed enabled: pseudo-accounts
-- always get sequence 0
def createPseudoAccount (pseudoOwnerKey : UInt256) (ownerField : PseudoField)
    : ApplyView (Except TER AccountRoot) := do
  let accountId ← ApplyView.ofReadView (pseudoAccountAddress pseudoOwnerKey)
  if accountId == xrpAccount then
    return .error .tecDUPLICATE
  let account : AccountRoot :=
    { key := (Keylet.account accountId).key
    , account := accountId
    , balance := STAmount.ofNativeInt64 0
    , sequence := 0
    , flags := lsfDisableMaster ||| lsfDefaultRipple ||| lsfDepositAuth
    , vaultID := if ownerField == .vaultID then some pseudoOwnerKey else none
    , loanBrokerID := if ownerField == .loanBrokerID then some pseudoOwnerKey else none }
  ApplyView.insert (.accountRoot account)
  return .ok account

def isGlobalFrozenAccount (issuer : AccountID) : ReadView Bool := do
  if issuer.isXRP then
    return false
  return match ← ReadView.read (Keylet.account issuer) with
    | some (.accountRoot a) => a.isFlag lsfGlobalFreeze
    | _ => false

def checkDestinationAndTag (toSle : Option AccountRoot) (hasDestinationTag : Bool) : TER :=
  match toSle with
  | none => .tecNO_DST
  | some sle =>
    if sle.isFlag lsfRequireDestTag && !hasDestinationTag then
      .tecDST_TAG_NEEDED
    else
      .tesSUCCESS

def isPseudoAccount (accountId : AccountID) : ReadView Bool := do
  match ← ReadView.read (Keylet.account accountId) with
  | some (.accountRoot account) => return account.isPseudoAccount
  | _ => return false

def xrpLiquid (id : AccountID) (ownerCountAdj : Int32) : ReadView XRPAmount := do
  match ← ReadView.read (Keylet.account id) with
  | some (.accountRoot account) =>
    let view ← read
    let ownerCount := confineOwnerCount (ownerCountHookStub id account.ownerCount) ownerCountAdj
    let reserve := if account.isPseudoAccount then XRPAmount.zero
                   else view.fees.accountReserve ownerCount
    let balance := balanceHookIOUStub id xrpAccount account.balance
    let reserveAmt ← match STAmount.ofXRPAmount reserve rounding_mode.to_nearest with
      | .error e => throw e
      | .ok s => pure s
    let isLt ← match balance.operator_lt reserveAmt with
      | .error e => throw e
      | .ok b => pure b
    let amount ← if isLt then pure balance.zeroed
      else match balance.operator_sub reserveAmt rounding_mode.to_nearest with
        | .error e => throw e
        | .ok a => pure a
    match amount.xrp with
    | .error e => throw e
    | .ok a => return a
  | _ => return XRPAmount.zero

def transferRate (issuer : AccountID) : ReadView Rate := do
  match ← ReadView.read (Keylet.account issuer) with
  | some (.accountRoot sle) => return ⟨sle.transferRate.getD kParityRate.value⟩
  | _ => return kParityRate

end XRPL.Model.Ledger.Helpers
