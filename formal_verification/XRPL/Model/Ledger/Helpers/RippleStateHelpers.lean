import XRPL.Model.Ledger.ApplyView
import XRPL.Model.Ledger.Helpers.AccountRootHelpers
import XRPL.Model.Protocol.Indexes
import XRPL.Model.Protocol.LedgerFormats


namespace XRPL.Model.Ledger.Helpers

open XRPL.Model.Protocol
open XRPL.Model.Ledger

def isFrozenIssue (account : AccountID) (issue : Issue) : ReadView Bool := do
  if issue.currency.isXRP then
    return false
  let issuer := issue.account
  let globalFrozen := match ← ReadView.read (Keylet.account issuer) with
    | some (.accountRoot sle) => sle.isFlag lsfGlobalFreeze
    | _ => false
  if globalFrozen then
    return true
  if issuer != account then
    let ledger ← read
    let freezeFlag := if AccountID.lt account issuer then lsfHighFreeze else lsfLowFreeze
    match ledger.read (Keylet.line account issuer issue.currency) with
    | some (.rippleState line) => return line.isFlag freezeFlag
    | _ => return false
  return false

def isIndividualFrozenIssue (account : AccountID) (issue : Issue) : ReadView Bool := do
  if issue.currency.isXRP then
    return false
  let issuer := issue.account
  if issuer == account then
    return false
  let ledger ← read
  let freezeFlag := if AccountID.lt account issuer then lsfHighFreeze else lsfLowFreeze
  match ledger.read (Keylet.line account issuer issue.currency) with
  | some (.rippleState line) => return line.isFlag freezeFlag
  | _ => return false

def isDeepFrozenIssue (account : AccountID) (issue : Issue) : ReadView Bool := do
  if issue.currency.isXRP then
    return false
  let issuer := issue.account
  if issuer == account then
    return false
  let ledger ← read
  match ledger.read (Keylet.line account issuer issue.currency) with
  | some (.rippleState line) => return line.isFlag lsfHighDeepFreeze || line.isFlag lsfLowDeepFreeze
  | _ => return false

private def rippleDisabled (issue : Issue) (issuerId : AccountID) (sleIssuer : AccountRoot)
    (account : AccountID) : ReadView Bool := do
  let ledger ← read
  match ledger.read (Keylet.line account issuerId issue.currency) with
  | some (.rippleState line) =>
    return line.isFlag (if AccountID.lt account issuerId then lsfHighNoRipple else lsfLowNoRipple)
  | _ => return !sleIssuer.isFlag lsfDefaultRipple

def canTransferIssue (issue : Issue) (from_ to_ : AccountID) : ReadView TER := do
  if issue.native then
    return .tesSUCCESS
  let issuerId := issue.getIssuer
  if issuerId == from_ || issuerId == to_ then
    return .tesSUCCESS
  let some (.accountRoot sleIssuer) ← ReadView.read (Keylet.account issuerId)
    | return .tefINTERNAL
  let fromDisabled ← rippleDisabled issue issuerId sleIssuer from_
  let toDisabled   ← rippleDisabled issue issuerId sleIssuer to_
  if fromDisabled && toDisabled then
    return .terNO_RIPPLE
  return .tesSUCCESS

def creditLimit (account issuer : AccountID) (currency : Currency) : ReadView STAmount := do
  let result := STAmount.ofAsset (.issue ⟨currency, account⟩)
  let view ← read
  match view.read (Keylet.line account issuer currency) with
  | some (.rippleState line) =>
    let limit := if AccountID.lt account issuer then line.lowLimit else line.highLimit
    return { limit with mAsset := .issue ⟨currency, account⟩ }
  | _ => return result

def creditBalance (account issuer : AccountID) (currency : Currency) : ReadView STAmount := do
  let result := STAmount.ofAsset (.issue ⟨currency, account⟩)
  let view ← read
  match view.read (Keylet.line account issuer currency) with
  | some (.rippleState line) =>
    let bal := if AccountID.lt account issuer then line.balance.operator_neg else line.balance
    return { bal with mAsset := .issue ⟨currency, account⟩ }
  | _ => return result

def trustCreate (bSrcHigh : Bool) (uSrcAccountID uDstAccountID : AccountID) (uIndex : UInt256)
    (sleAccount : Option AccountRoot) (bAuth bNoRipple bFreeze bDeepFreeze : Bool)
    (saBalance saLimit : STAmount) (uQualityIn uQualityOut : UInt32) : ApplyView TER := do
  let uLowAccountID := if bSrcHigh then uDstAccountID else uSrcAccountID
  let uHighAccountID := if bSrcHigh then uSrcAccountID else uDstAccountID
  if uLowAccountID == uHighAccountID then
    return .tecINTERNAL
  ApplyView.insert (.rippleState { key := uIndex })
  let some lowNode ← ApplyView.dirInsertStub uLowAccountID uIndex
    | return .tecDIR_FULL
  let some highNode ← ApplyView.dirInsertStub uHighAccountID uIndex
    | return .tecDIR_FULL
  let bSetDst := saLimit.getIssuer == uDstAccountID
  let bSetHigh := Bool.xor bSrcHigh bSetDst
  let some sleAccount := sleAccount
    | return .tefINTERNAL
  let peerId := if bSetHigh then uLowAccountID else uHighAccountID
  let some (.accountRoot slePeer) ← ApplyView.peek (Keylet.account peerId)
    | return .tecNO_TARGET
  match saBalance.asset.getIssue with
  | .error e => throw e
  | .ok balanceIssue =>
    let peerForLimit := if bSetDst then uSrcAccountID else uDstAccountID
    let otherLimit : STAmount :=
      STAmount.ofAsset (.issue ⟨balanceIssue.currency, peerForLimit⟩)
    let mut uFlags : UInt32 := if bSetHigh then lsfHighReserve else lsfLowReserve
    if bAuth then
      uFlags := uFlags ||| (if bSetHigh then lsfHighAuth else lsfLowAuth)
    if bNoRipple then
      uFlags := uFlags ||| (if bSetHigh then lsfHighNoRipple else lsfLowNoRipple)
    if bFreeze then
      uFlags := uFlags ||| (if bSetHigh then lsfHighFreeze else lsfLowFreeze)
    if bDeepFreeze then
      uFlags := uFlags ||| (if bSetHigh then lsfHighDeepFreeze else lsfLowDeepFreeze)
    if !slePeer.isFlag lsfDefaultRipple then
      uFlags := uFlags ||| (if bSetHigh then lsfLowNoRipple else lsfHighNoRipple)
    let rs : RippleState :=
      { key := uIndex
        previousTxnID := 0
        previousTxnLgrSeq := 0
        lowNode := some lowNode
        highNode := some highNode
        lowLimit := if bSetHigh then otherLimit else saLimit
        highLimit := if bSetHigh then saLimit else otherLimit
        lowQualityIn := if !bSetHigh && uQualityIn != 0 then some uQualityIn else none
        lowQualityOut := if !bSetHigh && uQualityOut != 0 then some uQualityOut else none
        highQualityIn := if bSetHigh && uQualityIn != 0 then some uQualityIn else none
        highQualityOut := if bSetHigh && uQualityOut != 0 then some uQualityOut else none
        balance := if bSetHigh then saBalance.operator_neg else saBalance
        flags := uFlags }
    ApplyView.update (.rippleState rs)
    adjustOwnerCount (some sleAccount) 1
    creditHookIOUStub uSrcAccountID uDstAccountID saBalance saBalance.zeroed
    return .tesSUCCESS

def trustDelete (sleRippleState : RippleState) (uLowAccountID uHighAccountID : AccountID)
    : ApplyView TER := do
  let uLowNode := sleRippleState.lowNode.getD 0
  let uHighNode := sleRippleState.highNode.getD 0
  if !(← ApplyView.dirRemoveStub uLowAccountID uLowNode sleRippleState.key false) then
    return .tefBAD_LEDGER
  if !(← ApplyView.dirRemoveStub uHighAccountID uHighNode sleRippleState.key false) then
    return .tefBAD_LEDGER
  ApplyView.erase ⟨.rippleState, sleRippleState.key⟩
  return .tesSUCCESS

def addEmptyHoldingIssue (accountID : AccountID) (priorBalance : XRPAmount)
    (issue : Issue) : ApplyView TER := do
  if issue.native || accountID == issue.getIssuer then
    return .tesSUCCESS
  let issuerId := issue.getIssuer
  let currency := issue.currency
  if ← ApplyView.ofReadView (isGlobalFrozenAccount issuerId) then
    return .tecFROZEN
  let srcId := issuerId
  let dstId := accountID
  let high := AccountID.lt dstId srcId
  let lineKeylet := Keylet.line srcId dstId currency
  let some (.accountRoot sleSrc) ← ApplyView.peek (Keylet.account srcId)
    | return .tefINTERNAL
  let some (.accountRoot sleDst) ← ApplyView.peek (Keylet.account dstId)
    | return .tefINTERNAL
  if !sleSrc.isFlag lsfDefaultRipple then
    return .tecINTERNAL
  let sb ← get
  if (sb.read lineKeylet).isSome then
    return .tecDUPLICATE
  let ownerCount := sleDst.ownerCount
  if priorBalance.operator_lt (sb.fees.accountReserve (ownerCount + 1)) then
    return .tecNO_LINE_INSUF_RESERVE
  trustCreate high srcId dstId lineKeylet.key (some sleDst) false true false false
    (STAmount.ofAsset (.issue ⟨currency, noAccount⟩))
    (STAmount.ofAsset (.issue ⟨currency, dstId⟩)) 0 0

def removeEmptyHoldingIssue (accountID : AccountID) (issue : Issue) : ApplyView TER := do
  if issue.native then
    let some (.accountRoot sle) ← ApplyView.peek (Keylet.account accountID)
      | return .tecINTERNAL
    match sle.balance.xrp with
    | .error e => throw e
    | .ok balance =>
      if balance.operator_ne_int 0 then
        return .tecHAS_OBLIGATIONS
      return .tesSUCCESS
  let accountIsIssuer := accountID == issue.account
  let sb ← get
  match sb.read (Keylet.line accountID issue.account issue.currency) with
  | some (.rippleState line) =>
    if !accountIsIssuer then
      match line.balance.iou rounding_mode.to_nearest with
      | .error e => throw e
      | .ok iouBalance =>
        -- C++ `iou() != beast::kZero` lowers to `signum() != 0` via beast::Zero.
        if iouBalance.signum != 0 then
          return .tecHAS_OBLIGATIONS
    let mut line := line
    if line.isFlag lsfLowReserve then
      let lowIssuer := line.lowLimit.getIssuer
      let some (.accountRoot sleLowAccount) ← ApplyView.peek (Keylet.account lowIssuer)
        | return .tecINTERNAL
      adjustOwnerCount (some sleLowAccount) (-1)
      line := line.clearFlag lsfLowReserve
    if line.isFlag lsfHighReserve then
      let highIssuer := line.highLimit.getIssuer
      let some (.accountRoot sleHighAccount) ← ApplyView.peek (Keylet.account highIssuer)
        | return .tecINTERNAL
      adjustOwnerCount (some sleHighAccount) (-1)
      line := line.clearFlag lsfHighReserve
    trustDelete line line.lowLimit.getIssuer line.highLimit.getIssuer
  | _ => return (if accountIsIssuer then .tesSUCCESS else .tecOBJECT_NOT_FOUND)

end XRPL.Model.Ledger.Helpers
