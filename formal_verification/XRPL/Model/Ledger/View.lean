import XRPL.Model.Ledger.ApplyView
import XRPL.Model.Ledger.Helpers.AccountRootHelpers
import XRPL.Model.Ledger.Helpers.CredentialHelpers
import XRPL.Model.Ledger.Helpers.RippleStateHelpers
import XRPL.Model.Ledger.Helpers.TokenHelpers
import XRPL.Model.Protocol.LedgerFormats


namespace XRPL.Model.Ledger

open XRPL.Model.Protocol
open XRPL.Model.Ledger.Helpers

def hasExpired (exp : Option UInt32) : ReadView Bool := do
  let sb ← readThe Ledger
  return match exp with
    | none => false
    | some e => sb.parentCloseTime ≥ e

def withdrawToDestExceedsLimit (from_ to_ : AccountID) (amount : STAmount) : ReadView TER := do
  let issuer := amount.getIssuer
  if from_ == to_ || to_ == issuer || issuer.isXRP then
    return .tesSUCCESS
  match amount.asset with
  | .mptIssue _ => return .tesSUCCESS
  | .issue issue =>
    let owed ← creditBalance to_ issuer issue.currency
    if owed.signum ≤ 0 then
      let limit ← creditLimit to_ issuer issue.currency
      let negOwedGeLimit ← match STAmount.operator_ge owed.operator_neg limit with
        | .error e => throw e
        | .ok b => pure b
      let sum ← match STAmount.operator_add limit owed rounding_mode.to_nearest with
        | .error e => throw e
        | .ok s => pure s
      let amountGtSum ← match STAmount.operator_gt amount sum with
        | .error e => throw e
        | .ok b => pure b
      if negOwedGeLimit || amountGtSum then
        return .tecNO_LINE
    return .tesSUCCESS

def canWithdrawToSle (from_ to_ : AccountID) (toSle : Option AccountRoot) (amount : STAmount)
    (hasDestinationTag : Bool) : ReadView TER := do
  let ret := checkDestinationAndTag toSle hasDestinationTag
  if ret.operator_bool then
    return ret
  if from_ == to_ then
    return .tesSUCCESS
  let some toSle := toSle
    | return .tecNO_DST
  if toSle.isFlag lsfDepositAuth then
    let view ← read
    if (view.read (Keylet.depositPreauthAccount to_ from_)).isNone then
      return .tecNO_PERMISSION
  withdrawToDestExceedsLimit from_ to_ amount

def canWithdrawFromTo (from_ to_ : AccountID) (amount : STAmount) (hasDestinationTag : Bool) : ReadView TER := do
  let toSle := match ← ReadView.read (Keylet.account to_) with
    | some (.accountRoot sle) => some sle
    | _ => none
  canWithdrawToSle from_ to_ toSle amount hasDestinationTag

def canWithdrawTx (from_ : AccountID) (to_ : Option AccountID) (amount : STAmount)
    (destinationTag : Option UInt32) : ReadView TER := do
  let to_ := to_.getD from_
  canWithdrawFromTo from_ to_ amount destinationTag.isSome

def doWithdraw (credentialIDs : Option STVector256) (senderAcct dstAcct sourceAcct : AccountID)
    (priorBalance : XRPAmount) (amount : STAmount) (mode : rounding_mode) : ApplyView TER := do
  -- create trust line or MPToken for the receiving account
  if dstAcct == senderAcct then
    let ter ← addEmptyHoldingAsset senderAcct priorBalance amount.asset
    if !ter.isTesSuccess && ter != .tecDUPLICATE then
      return ter
  else
    let dstSle := (← ApplyView.peek (Keylet.account dstAcct)).bind (·.asAccountRoot)
    let err ← verifyDepositPreauth credentialIDs senderAcct dstAcct dstSle
    if err.operator_bool then
      return err
  let balance ← ApplyView.ofReadView
    (accountHolds sourceAcct amount.asset .fhIGNORE_FREEZE .ahIGNORE_AUTH mode)
  let isLt ← match balance.operator_lt amount with
    | .error e => throw e
    | .ok b => pure b
  if isLt then
    return .tefINTERNAL
  accountSend sourceAcct dstAcct amount mode .yes

end XRPL.Model.Ledger
