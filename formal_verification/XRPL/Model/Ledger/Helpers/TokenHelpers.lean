import XRPL.Model.Ledger.ApplyView
import XRPL.Model.Ledger.Helpers.AccountRootHelpers
import XRPL.Model.Ledger.Helpers.CredentialHelpers
import XRPL.Model.Ledger.Helpers.MPTokenHelpers
import XRPL.Model.Ledger.Helpers.RippleStateHelpers
import XRPL.Model.Protocol.LedgerEntries.Vault
import XRPL.Model.Protocol.LedgerFormats
import XRPL.Model.Protocol.Rate
import XRPL.Model.Protocol.STAmount
import XRPL.Model.Protocol.TER
import XRPL.Model.Protocol.Protocol


namespace XRPL.Model.Ledger.Helpers

open XRPL.Model.Protocol
open XRPL.Model.Ledger

inductive FreezeHandling where | fhIGNORE_FREEZE | fhZERO_IF_FROZEN
  deriving DecidableEq, Repr, BEq
inductive AuthHandling where | ahIGNORE_AUTH | ahZERO_IF_UNAUTHORIZED
  deriving DecidableEq, Repr, BEq
inductive SpendableHandling where | shSIMPLE_BALANCE | shFULL_BALANCE
  deriving DecidableEq, Repr, BEq
inductive WaiveTransferFee where | yes | no
  deriving DecidableEq, Repr, BEq
inductive WaiveMPTCanTransfer where | no | yes
  deriving DecidableEq, Repr, BEq
inductive AuthType where | strongAuth | weakAuth | legacy
  deriving DecidableEq, Repr, BEq

def canAddHoldingIssue (issue : Issue) : ReadView TER := do
  if issue.native then
    return TER.tesSUCCESS
  match ← ReadView.read (Keylet.account issue.getIssuer) with
  | some (.accountRoot issuer) =>
    if !issuer.isFlag lsfDefaultRipple then
      return TER.terNO_RIPPLE
    return TER.tesSUCCESS
  | _ => return TER.terNO_ACCOUNT

def canAddHoldingAsset : Asset → ReadView TER
  | .issue i => canAddHoldingIssue i
  | .mptIssue m => canAddHoldingMPTIssue m

def addEmptyHoldingAsset (accountID : AccountID) (priorBalance : XRPAmount) : Asset → ApplyView TER
  | .issue i => addEmptyHoldingIssue accountID priorBalance i
  | .mptIssue m => addEmptyHoldingMPTIssue accountID priorBalance m

def removeEmptyHoldingAsset (accountID : AccountID) : Asset → ApplyView TER
  | .issue i => removeEmptyHoldingIssue accountID i
  | .mptIssue m => removeEmptyHoldingMPTIssue accountID m

def isGlobalFrozen (asset : Asset) : ReadView Bool :=
  match asset with
  | .issue issue => isGlobalFrozenAccount issue.getIssuer
  | .mptIssue mpt => isGlobalFrozenMPT mpt

def isIndividualFrozen (account : AccountID) (asset : Asset) : ReadView Bool :=
  match asset with
  | .issue issue => isIndividualFrozenIssue account issue
  | .mptIssue mpt => isIndividualFrozenMPT account mpt

mutual
def isVaultPseudoAccountFrozen (depth : Nat) (account : AccountID) (mptShare : MPTIssue) : ReadView Bool := do
  match _h : kMaxAssetCheckDepth - depth with
  | 0 => return true
  | _ + 1 =>
    let some (.mptokenIssuance issuance) ← ReadView.read (Keylet.mptIssuance mptShare.getMptID)
      | return false
    let issuer := issuance.issuer
    let some (.accountRoot issuerSle) ← ReadView.read (Keylet.account issuer)
      | return false
    let some vid := issuerSle.vaultID
      | return false
    let some (.vault vault) ← ReadView.read ⟨.vault, vid⟩
      | return false
    isAnyFrozen (depth + 1) [issuer, account] vault.asset
termination_by 2 * (kMaxAssetCheckDepth - depth)
decreasing_by omega

def isAnyFrozen (depth : Nat) (accounts : List AccountID) (asset : Asset) : ReadView Bool := do
  if (← isGlobalFrozen asset) then
    return true
  for account in accounts do
    if (← isIndividualFrozen account asset) then
      return true
  match asset with
  | .issue _ => return false
  | .mptIssue mpt =>
    for account in accounts do
      if (← isVaultPseudoAccountFrozen depth account mpt) then
        return true
    return false
termination_by 2 * (kMaxAssetCheckDepth - depth) + 1
decreasing_by omega
end

def isFrozenMPT (account : AccountID) (mpt : MPTIssue) : ReadView Bool := do
  let g ← isGlobalFrozenMPT mpt
  let i ← isIndividualFrozenMPT account mpt
  let v ← isVaultPseudoAccountFrozen 0 account mpt
  return g || i || v

def isFrozen (account : AccountID) (asset : Asset) : ReadView Bool :=
  match asset with
  | .issue issue => isFrozenIssue account issue
  | .mptIssue mpt => isFrozenMPT account mpt

def checkFrozenIssue (account : AccountID) (issue : Issue) : ReadView TER := do
  let frozen ← isFrozenIssue account issue
  if frozen then
    return .tecFROZEN
  return .tesSUCCESS

def checkFrozenMPT (account : AccountID) (mpt : MPTIssue) : ReadView TER := do
  let frozen ← isFrozenMPT account mpt
  if frozen then
    return .tecLOCKED
  return .tesSUCCESS

def checkFrozen (account : AccountID) (asset : Asset) : ReadView TER :=
  match asset with
  | .issue issue => checkFrozenIssue account issue
  | .mptIssue mpt => checkFrozenMPT account mpt

def checkDeepFrozenIssue (account : AccountID) (issue : Issue) : ReadView TER := do
  let frozen ← isDeepFrozenIssue account issue
  if frozen then
    return .tecFROZEN
  return .tesSUCCESS

def isDeepFrozen (account : AccountID) (asset : Asset) : ReadView Bool :=
  match asset with
  | .issue issue => isDeepFrozenIssue account issue
  | .mptIssue mpt => isFrozenMPT account mpt

def checkDeepFrozen (account : AccountID) (asset : Asset) : ReadView TER :=
  match asset with
  | .issue issue => checkDeepFrozenIssue account issue
  | .mptIssue mpt => checkFrozenMPT account mpt

def canTransferMPT (depth : Nat) (waive : WaiveMPTCanTransfer)
    (mptIssue : MPTIssue) (from_ to_ : AccountID) : ReadView TER := do
  let some (.mptokenIssuance issuance) ← ReadView.read (Keylet.mptIssuance mptIssue.getMptID)
    | return .tecOBJECT_NOT_FOUND
  let issuer := issuance.issuer
  if waive == .yes || from_ == issuer || to_ == issuer then
    return .tesSUCCESS
  if !issuance.isFlag lsfMPTCanTransfer then
    return .tecNO_AUTH
  -- fixCleanup3_2_0 enabled: vault shares inherit the underlying's transferability.
  match issuance.referenceHolding with
  | none => return .tesSUCCESS
  | some refKey =>
    match _h : kMaxAssetCheckDepth - depth with
    | 0 => return .tecINTERNAL
    | _ + 1 =>
      let some sleHolding := (← read).readUnchecked refKey
        | return .tefINTERNAL
      match assetOfHolding issuance sleHolding with
      | none            => return .tefINTERNAL
      | some underlying =>
        match underlying with
        | .issue i    => canTransferIssue i from_ to_
        | .mptIssue m => canTransferMPT (depth + 1) .no m from_ to_
termination_by kMaxAssetCheckDepth - depth
decreasing_by omega

def canTransfer (asset : Asset) (from_ to_ : AccountID)
    (waive : WaiveMPTCanTransfer := .no) : ReadView TER :=
  match asset with
  | .mptIssue m => canTransferMPT 0 waive m from_ to_
  | .issue i    => canTransferIssue i from_ to_

def requireAuthIssue (issue : Issue) (account : AccountID) (authType : AuthType) : ReadView TER := do
  if issue.isXRP || issue.account == account then
    return .tesSUCCESS
  let ledger ← read
  let trustLine := (ledger.read (Keylet.line account issue.account issue.currency)).bind (·.asRippleState)
  if trustLine.isNone && authType == .strongAuth then
    return .tecNO_LINE
  match ← ReadView.read (Keylet.account issue.account) with
  | some (.accountRoot issuerAccount) =>
    if !issuerAccount.isFlag lsfRequireAuth then
      return .tesSUCCESS
    match trustLine with
    | some line =>
      return (if line.isFlag (if AccountID.lt issue.account account then lsfLowAuth else lsfHighAuth)
              then .tesSUCCESS else .tecNO_AUTH)
    | none => return .tecNO_LINE
  | _ => return .tesSUCCESS

def requireAuthMPT (depth : Nat) (mptIssue : MPTIssue) (account : AccountID)
    (authType : AuthType) : ReadView TER := do
  let some (.mptokenIssuance sleIssuance) ← ReadView.read (Keylet.mptIssuance mptIssue.getMptID)
    | return .tecOBJECT_NOT_FOUND
  let mptIssuer := sleIssuance.issuer
  if mptIssuer == account then
    return .tesSUCCESS
  -- featureSingleAssetVault assumed enabled: a vault pseudo-account's shares
  -- inherit the underlying asset's authorization.
  match _h : kMaxAssetCheckDepth - depth with
  | 0 => return .tecINTERNAL
  | _ + 1 =>
    let some (.accountRoot sleIssuer) ← ReadView.read (Keylet.account mptIssuer)
      | return .tefINTERNAL
    match sleIssuer.vaultID with
    | some vid =>
      let some (.vault sleVault) ← ReadView.read ⟨.vault, vid⟩
        | return .tefINTERNAL
      let descent ← (match sleVault.asset with
        | .issue i    => requireAuthIssue i account authType
        | .mptIssue m => requireAuthMPT (depth + 1) m account authType)
      if descent.operator_bool then
        return descent
    | none => pure ()
    let sleToken := (← ReadView.read (Keylet.mptoken mptIssue.getMptID account)).bind (·.asMPToken)
    if sleToken.isNone && (authType == .strongAuth || authType == .legacy) then
      return .tecNO_AUTH
    match sleIssuance.domainID with
    | none => pure ()
    | some domainID =>
      let ter ← validDomain domainID account
      if !ter.operator_bool then
        return ter
      if sleToken.isNone then
        return ter
    if (← isPseudoAccount account) then
      return .tesSUCCESS
    match sleToken with
    | some t =>
      if sleIssuance.isFlag lsfMPTRequireAuth && !t.isFlag lsfMPTAuthorized then
        return .tecNO_AUTH
    | none =>
      if sleIssuance.isFlag lsfMPTRequireAuth then
        return .tecNO_AUTH
    return .tesSUCCESS
termination_by kMaxAssetCheckDepth - depth
decreasing_by omega

def requireAuth (asset : Asset) (account : AccountID) (authType : AuthType := .legacy) : ReadView TER :=
  match asset with
  | .issue i    => requireAuthIssue i account authType
  | .mptIssue m => requireAuthMPT 0 m account authType

private def getLineIfUsable (account : AccountID) (currency : Currency) (issuer : AccountID)
    (zeroIfFrozen : FreezeHandling) : ReadView (Option RippleState) := do
  let some (.rippleState sle) ← ReadView.read (Keylet.line account issuer currency)
    | return none
  if zeroIfFrozen == .fhZERO_IF_FROZEN then
    let issue : Issue := ⟨currency, issuer⟩
    if (← isFrozenIssue account issue) || (← isDeepFrozenIssue account issue) then
      return none
    -- fixFrozenLPTokenTransfer assumed enabled: issuer account must exist;
    -- the AMM/LPToken-freeze part of the check is out of scope (no sfAMMID in the model)
    let some (.accountRoot _) ← ReadView.read (Keylet.account issuer)
      | return none
    return some sle
  return some sle

private def getTrustLineBalance (sle : Option RippleState) (account : AccountID)
    (currency : Currency) (issuer : AccountID) (includeOppositeLimit : Bool)
    (mode : rounding_mode) : ReadView STAmount := do
  match sle with
  | none =>
    return balanceHookIOUStub account issuer (STAmount.clear (.issue ⟨currency, issuer⟩))
  | some line =>
    let accountHigh := AccountID.lt issuer account
    let oppositeField := if accountHigh then RippleStateAmountField.sfLowLimit else .sfHighLimit
    let amount := line.getFieldAmount .sfBalance
    let amount := if accountHigh then amount.operator_neg else amount
    let amount ← if includeOppositeLimit then
        match amount.operator_add (line.getFieldAmount oppositeField) mode with
        | .error e => throw e
        | .ok a => pure a
      else
        pure amount
    return balanceHookIOUStub account issuer { amount with mAsset := .issue ⟨currency, issuer⟩ }

def accountHoldsIssue (account : AccountID) (issue : Issue) (zeroIfFrozen : FreezeHandling)
    (mode : rounding_mode) (includeFullBalance : SpendableHandling := .shSIMPLE_BALANCE)
    : ReadView STAmount := do
  let currency := issue.currency
  let issuer := issue.getIssuer
  if currency.isXRP then
    -- XRP-only: canonicalize exact, mode inert
    match STAmount.ofXRPAmount (← xrpLiquid account 0) .to_nearest with
    | .error e => throw e
    | .ok s => return s
  let returnSpendable := includeFullBalance == .shFULL_BALANCE
  if returnSpendable && account == issuer then
    match STAmount.checked (.issue ⟨currency, issuer⟩) kMaxValue kMaxOffset false mode with
    | .error e => throw e
    | .ok s => return s
  let sle ← getLineIfUsable account currency issuer zeroIfFrozen
  getTrustLineBalance sle account currency issuer returnSpendable mode

def accountHoldsMPT (account : AccountID) (mptIssue : MPTIssue)
    (zeroIfFrozen : FreezeHandling) (zeroIfUnauthorized : AuthHandling)
    (includeFullBalance : SpendableHandling := .shSIMPLE_BALANCE) : ReadView STAmount := do
  let zero := STAmount.ofAsset (.mptIssue mptIssue)
  let issuer := mptIssue.getIssuer
  let returnSpendable := includeFullBalance == .shFULL_BALANCE
  if returnSpendable && account == issuer then
    let some (.mptokenIssuance issuance) ← ReadView.read (Keylet.mptIssuance mptIssue.getMptID)
      | return zero
    -- featureMPTokensV2 assumed enabled: result goes through the balance hook
    match balanceHookMPTStub issuer mptIssue (availableMPTAmount issuance) with
    | .error e => throw e
    | .ok s => return s
  let amount ← do
    let some (.mptoken sleMpt) ← ReadView.read (Keylet.mptoken mptIssue.getMptID account)
      | pure zero
    if zeroIfFrozen == .fhZERO_IF_FROZEN && (← isFrozenMPT account mptIssue) then
      pure zero
    else
      -- integral at offset 0: canonicalize exact, mode inert
      let amt ← match STAmount.checked (.mptIssue mptIssue) sleMpt.mptAmount 0 false .to_nearest with
        | .error e => throw e
        | .ok a => pure a
      -- featureSingleAssetVault assumed enabled: StrongAuth authorization check
      if zeroIfUnauthorized == .ahZERO_IF_UNAUTHORIZED then
        if !(← requireAuth (.mptIssue mptIssue) account .strongAuth).isTesSuccess then
          pure zero
        else pure amt
      else pure amt
  -- featureMPTokensV2 assumed enabled: result goes through the balance hook
  match amount.mpt with
  | .error e => throw e
  | .ok m =>
    match balanceHookMPTStub account mptIssue m.value_ with
    | .error e => throw e
    | .ok s => return s

def accountHolds (account : AccountID) (asset : Asset) (zeroIfFrozen : FreezeHandling)
    (zeroIfUnauthorized : AuthHandling) (mode : rounding_mode)
    (includeFullBalance : SpendableHandling := .shSIMPLE_BALANCE) : ReadView STAmount :=
  match asset with
  | .issue issue => accountHoldsIssue account issue zeroIfFrozen mode includeFullBalance
  | .mptIssue mpt => accountHoldsMPT account mpt zeroIfFrozen zeroIfUnauthorized includeFullBalance

private def directSendNoFeeIOU (uSenderID uReceiverID : AccountID) (saAmount : STAmount)
    (_bCheckIssuer : Bool) (mode : rounding_mode) : ApplyView TER := do
  let currency ← match saAmount.mAsset with
    | .issue i => pure i.currency
    | _ => throw "directSendNoFeeIOU: amount is not an Issue"
  let bSenderHigh := AccountID.lt uReceiverID uSenderID
  let index := Keylet.line uSenderID uReceiverID currency
  match ← ApplyView.peek index with
  | some (.rippleState sleRippleState) =>
    let saBalance0 := sleRippleState.getFieldAmount .sfBalance
    let saBalance := if bSenderHigh then saBalance0.operator_neg else saBalance0
    creditHookIOUStub uSenderID uReceiverID saAmount saBalance
    let saBefore := saBalance
    let saBalance ← match saBalance.operator_sub saAmount mode with
      | .error e => throw e
      | .ok b => pure b
    let senderReserveFlag := if bSenderHigh then lsfHighReserve else lsfLowReserve
    let senderNoRippleFlag := if bSenderHigh then lsfHighNoRipple else lsfLowNoRipple
    let senderFreezeFlag := if bSenderHigh then lsfHighFreeze else lsfLowFreeze
    let receiverReserveFlag := if bSenderHigh then lsfLowReserve else lsfHighReserve
    let mut sleLine := sleRippleState
    let mut bDelete := false
    -- sender crossed from positive to ≤ 0 and the line carries no other state:
    -- clear the sender's reserve, maybe delete the line
    if saBefore.signum > 0 && saBalance.signum ≤ 0
        && sleLine.isFlag senderReserveFlag
        && !sleLine.isFlag senderFreezeFlag
        && (sleLine.getFieldAmount (if bSenderHigh then .sfHighLimit else .sfLowLimit)).isZero
        && (if bSenderHigh then sleLine.highQualityIn else sleLine.lowQualityIn).getD 0 == 0
        && (if bSenderHigh then sleLine.highQualityOut else sleLine.lowQualityOut).getD 0 == 0 then
      let some (.accountRoot senderAcct) ← ApplyView.peek (Keylet.account uSenderID)
        | return .tefINTERNAL
      if sleLine.isFlag senderNoRippleFlag != senderAcct.isFlag lsfDefaultRipple then
        adjustOwnerCount (some senderAcct) (-1)
        sleLine := sleLine.clearFlag senderReserveFlag
        bDelete := saBalance.isZero && !sleLine.isFlag receiverReserveFlag
    let saBalanceStored := if bSenderHigh then saBalance.operator_neg else saBalance
    sleLine := { sleLine with balance := saBalanceStored }
    if bDelete then
      trustDelete sleLine (if bSenderHigh then uReceiverID else uSenderID)
                          (if bSenderHigh then uSenderID else uReceiverID)
    else
      ApplyView.update (.rippleState sleLine)
      return .tesSUCCESS
  | _ =>
    let saReceiverLimit := STAmount.ofAsset (.issue ⟨currency, uReceiverID⟩)
    let saBalance := { saAmount with mAsset := .issue ⟨currency, noAccount⟩ }
    let some (.accountRoot sleAccount) ← ApplyView.peek (Keylet.account uReceiverID)
      | return .tefINTERNAL
    let noRipple := !sleAccount.isFlag lsfDefaultRipple
    trustCreate bSenderHigh uSenderID uReceiverID index.key sleAccount false noRipple false false
      saBalance saReceiverLimit 0 0

-- C++ returns saActual (sender cost incl. transfer fee) via out-param; all callers in
-- our scope discard it, so it stays internal
private def directSendNoLimitIOU (uSenderID uReceiverID : AccountID) (saAmount : STAmount)
    (mode : rounding_mode) (waiveFee : WaiveTransferFee) : ApplyView TER := do
  let issuer := saAmount.getIssuer
  if uSenderID == issuer || uReceiverID == issuer || issuer == noAccount then
    -- direct send: redeeming IOUs and/or sending own IOUs
    directSendNoFeeIOU uSenderID uReceiverID saAmount false mode
  else
    -- sending third-party IOUs: transit through the issuer
    let saActual ← if waiveFee == .yes then pure saAmount
      else do
        let rate ← ApplyView.ofReadView (transferRate issuer)
        match multiplyRate saAmount rate mode with
        | .error e => throw e
        | .ok a => pure a
    let ter ← directSendNoFeeIOU issuer uReceiverID saAmount true mode
    if !ter.isTesSuccess then
      return ter
    directSendNoFeeIOU uSenderID issuer saActual true mode

private def accountSendIOU (uSenderID uReceiverID : AccountID) (saAmount : STAmount)
    (mode : rounding_mode) (waiveFee : WaiveTransferFee) : ApplyView TER := do
  if saAmount.signum < 0 || saAmount.holdsMPTIssue then
    return .tecINTERNAL
  if saAmount.isZero || uSenderID == uReceiverID then
    return .tesSUCCESS
  if !saAmount.native then
    directSendNoLimitIOU uSenderID uReceiverID saAmount mode waiveFee
  else
    -- XRP: pure balance adjustment; sender or receiver may be absent (pathfinding)
    let sender ← if uSenderID != xrpAccount then
        pure ((← ApplyView.peek (Keylet.account uSenderID)).bind (·.asAccountRoot))
      else pure none
    let receiver ← if uReceiverID != xrpAccount then
        pure ((← ApplyView.peek (Keylet.account uReceiverID)).bind (·.asAccountRoot))
      else pure none
    let mut terResult : TER := .tesSUCCESS
    match sender with
    | some s =>
      let isLt ← match s.balance.operator_lt saAmount with
        | .error e => throw e
        | .ok b => pure b
      if isLt then
        terResult := if (← ApplyView.openStub) then .telFAILED_PROCESSING else .tecFAILED_PROCESSING
      else
        let sndBal := s.balance
        creditHookIOUStub uSenderID xrpAccount saAmount sndBal
        let newBal ← match sndBal.operator_sub saAmount mode with
          | .error e => throw e
          | .ok b => pure b
        ApplyView.update (.accountRoot { s with balance := newBal })
    | none => pure ()
    if terResult.isTesSuccess then
      match receiver with
      | some r =>
        let rcvBal := r.balance
        let newBal ← match rcvBal.operator_add saAmount mode with
          | .error e => throw e
          | .ok b => pure b
        creditHookIOUStub xrpAccount uReceiverID saAmount rcvBal.operator_neg
        ApplyView.update (.accountRoot { r with balance := newBal })
      | none => pure ()
    return terResult

private def directSendNoFeeMPT (uSenderID uReceiverID : AccountID) (saAmount : STAmount)
    : ApplyView TER := do
  -- MPT authorization is not checked here - it must have been checked earlier
  let mptIssue ← match saAmount.mAsset with
    | .mptIssue m => pure m
    | _ => throw "directSendNoFeeMPT: amount is not an MPT"
  let issuer := saAmount.getIssuer
  let some (.mptokenIssuance sleIssuance) ← ApplyView.peek (Keylet.mptIssuance mptIssue.getMptID)
    | return .tecOBJECT_NOT_FOUND
  let maxAmount := maxMPTAmount sleIssuance
  let outstanding := sleIssuance.outstandingAmount
  let available := availableMPTAmount sleIssuance
  let amt ← match saAmount.mpt with
    | .error e => throw e
    | .ok m => pure m.value_
  if uSenderID == issuer then
    -- featureMPTokensV2 assumed enabled: minting may not overflow MaximumAmount
    if isMPTOverflow amt outstanding maxAmount .yes then
      return .tecPATH_DRY
    ApplyView.update (.mptokenIssuance
      { sleIssuance with outstandingAmount := outstanding + amt.toUInt64 })
  else
    match ← ApplyView.peek (Keylet.mptoken mptIssue.getMptID uSenderID) with
    | some (.mptoken sle) =>
      let senderBalance := sle.mptAmount
      if senderBalance < amt.toUInt64 then
        return .tecINSUFFICIENT_FUNDS
      creditHookMPTStub uSenderID uReceiverID saAmount senderBalance available
      ApplyView.update (.mptoken { sle with mptAmount := senderBalance - amt.toUInt64 })
    | _ => return .tecNO_AUTH
  if uReceiverID == issuer then
    if outstanding ≥ amt.toUInt64 then
      ApplyView.update (.mptokenIssuance
        { sleIssuance with outstandingAmount := outstanding - amt.toUInt64 })
    else
      return .tecINTERNAL
  else
    match ← ApplyView.peek (Keylet.mptoken mptIssue.getMptID uReceiverID) with
    | some (.mptoken sle) =>
      -- featureMPTokensV2 assumed enabled: receiver balance may not overflow UInt64
      if sle.mptAmount > maxUInt64 - amt.toUInt64 then
        return .tecINTERNAL
      creditHookMPTStub uSenderID uReceiverID saAmount sle.mptAmount available
      ApplyView.update (.mptoken { sle with mptAmount := sle.mptAmount + amt.toUInt64 })
    | _ => return .tecNO_AUTH
  return .tesSUCCESS

-- saActual out-param internalized, as in directSendNoLimitIOU
private def directSendNoLimitMPT (uSenderID uReceiverID : AccountID) (saAmount : STAmount)
    (mode : rounding_mode) (waiveFee : WaiveTransferFee) (allowOverflow : AllowMPTOverflow)
    : ApplyView TER := do
  let mptIssue ← match saAmount.mAsset with
    | .mptIssue m => pure m
    | _ => throw "directSendNoLimitMPT: amount is not an MPT"
  let issuer := saAmount.getIssuer
  let some (.mptokenIssuance sle) ← ApplyView.peek (Keylet.mptIssuance mptIssue.getMptID)
    | return .tecOBJECT_NOT_FOUND
  if uSenderID == issuer || uReceiverID == issuer then
    if uSenderID == issuer then
      let sendAmount ← match saAmount.mpt with
        | .error e => throw e
        | .ok m => pure m.value_
      -- featureMPTokensV2 assumed enabled: allowOverflow passes through unchanged
      if isMPTOverflow sendAmount sle.outstandingAmount (maxMPTAmount sle) allowOverflow then
        return .tecPATH_DRY
    -- direct send: redeeming MPTs and/or sending own MPTs
    directSendNoFeeMPT uSenderID uReceiverID saAmount
  else
    -- sending third-party MPTs: transit through the issuer
    let saActual ← if waiveFee == .yes then pure saAmount
      else do
        let rate ← ApplyView.ofReadView (transferRateMPT mptIssue.getMptID)
        match multiplyRate saAmount rate mode with
        | .error e => throw e
        | .ok a => pure a
    let ter ← directSendNoFeeMPT issuer uReceiverID saAmount
    if !ter.isTesSuccess then
      return ter
    directSendNoFeeMPT uSenderID issuer saActual

private def accountSendMPT (uSenderID uReceiverID : AccountID) (saAmount : STAmount)
    (mode : rounding_mode) (waiveFee : WaiveTransferFee) (allowOverflow : AllowMPTOverflow)
    : ApplyView TER := do
  if saAmount.isZero || uSenderID == uReceiverID then
    return .tesSUCCESS
  directSendNoLimitMPT uSenderID uReceiverID saAmount mode waiveFee allowOverflow

def accountSend (uSenderID uReceiverID : AccountID) (saAmount : STAmount) (mode : rounding_mode)
    (waiveFee : WaiveTransferFee := .no) (allowOverflow : AllowMPTOverflow := .no)
    : ApplyView TER :=
  match saAmount.mAsset with
  | .issue _ => accountSendIOU uSenderID uReceiverID saAmount mode waiveFee
  | .mptIssue _ => accountSendMPT uSenderID uReceiverID saAmount mode waiveFee allowOverflow

abbrev MultiplePaymentDestinations := List (AccountID × Number)

-- C++ accumulates `actual` (total sender cost) via out-param; internalized as before
private def directSendNoLimitMultiIOU (senderID : AccountID) (issue : Issue)
    (receivers : MultiplePaymentDestinations) (mode : rounding_mode)
    (waiveFee : WaiveTransferFee) : ApplyView TER := do
  let issuer := issue.getIssuer
  let mut takeFromSender := STAmount.ofAsset (.issue issue)
  for r in receivers do
    let receiverID := r.1
    let amount ← match STAmount.ofNumber (.issue issue) r.2 mode with
      | .error e => throw e
      | .ok a => pure a
    if amount.isZero || senderID == receiverID then
      continue
    if senderID == issuer || receiverID == issuer || issuer == noAccount then
      -- direct send: redeeming IOUs and/or sending own IOUs
      let ter ← directSendNoFeeIOU senderID receiverID amount false mode
      if !ter.isTesSuccess then
        return ter
      continue
    -- sending third-party IOUs: transit through the issuer
    let actualSend ← if waiveFee == .yes then pure amount
      else do
        let rate ← ApplyView.ofReadView (transferRate issuer)
        match multiplyRate amount rate mode with
        | .error e => throw e
        | .ok a => pure a
    takeFromSender ← match takeFromSender.operator_add actualSend mode with
      | .error e => throw e
      | .ok s => pure s
    let ter ← directSendNoFeeIOU issuer receiverID amount true mode
    if !ter.isTesSuccess then
      return ter
  if senderID != issuer && !takeFromSender.isZero then
    directSendNoFeeIOU senderID issuer takeFromSender true mode
  else
    return .tesSUCCESS

private def accountSendMultiIOU (senderID : AccountID) (issue : Issue)
    (receivers : MultiplePaymentDestinations) (mode : rounding_mode)
    (waiveFee : WaiveTransferFee) : ApplyView TER := do
  if !issue.native then
    directSendNoLimitMultiIOU senderID issue receivers mode waiveFee
  else
    -- XRP: credit each receiver, debit the sender once after the loop
    let sender ← if senderID != xrpAccount then
        pure ((← ApplyView.peek (Keylet.account senderID)).bind (·.asAccountRoot))
      else pure none
    let mut takeFromSender := STAmount.ofAsset (.issue issue)
    for r in receivers do
      let receiverID := r.1
      let amount ← match STAmount.ofNumber (.issue issue) r.2 mode with
        | .error e => throw e
        | .ok a => pure a
      if amount.signum < 0 then
        return .tecINTERNAL
      if amount.isZero || senderID == receiverID then
        continue
      let receiver ← if receiverID != xrpAccount then
          pure ((← ApplyView.peek (Keylet.account receiverID)).bind (·.asAccountRoot))
        else pure none
      match receiver with
      | some rcv =>
        let rcvBal := rcv.balance
        let newBal ← match rcvBal.operator_add amount mode with
          | .error e => throw e
          | .ok b => pure b
        creditHookIOUStub xrpAccount receiverID amount rcvBal.operator_neg
        ApplyView.update (.accountRoot { rcv with balance := newBal })
        takeFromSender ← match takeFromSender.operator_add amount mode with
          | .error e => throw e
          | .ok s => pure s
      | none => pure ()
    match sender with
    | some s =>
      let isLt ← match s.balance.operator_lt takeFromSender with
        | .error e => throw e
        | .ok b => pure b
      if isLt then
        return .tecFAILED_PROCESSING
      let sndBal := s.balance
      creditHookIOUStub senderID xrpAccount takeFromSender sndBal
      let newBal ← match sndBal.operator_sub takeFromSender mode with
        | .error e => throw e
        | .ok b => pure b
      ApplyView.update (.accountRoot { s with balance := newBal })
    | none => pure ()
    return .tesSUCCESS

-- `actual` out-param internalized, as in the single-destination senders
private def directSendNoLimitMultiMPT (senderID : AccountID) (mptIssue : MPTIssue)
    (receivers : MultiplePaymentDestinations) (mode : rounding_mode)
    (waiveFee : WaiveTransferFee) : ApplyView TER := do
  let issuer := mptIssue.getIssuer
  let some (.mptokenIssuance sle) ← ApplyView.peek (Keylet.mptIssuance mptIssue.getMptID)
    | return .tecOBJECT_NOT_FOUND
  -- issuer-as-sender MaximumAmount tracking in exact UInt64 arithmetic (Number's
  -- ~16-digit mantissa loses precision near kMaxMpTokenAmount); outstanding is a
  -- one-time snapshot, not re-read per iteration
  let mut totalSendAmount : UInt64 := 0
  let maximumAmount : UInt64 := sle.maximumAmount.getD maxMPTokenAmount.toUInt64
  let outstandingAmount := sle.outstandingAmount
  let mut takeFromSender := STAmount.ofAsset (.mptIssue mptIssue)
  for r in receivers do
    let receiverID := r.1
    let amount ← match STAmount.ofNumber (.mptIssue mptIssue) r.2 mode with
      | .error e => throw e
      | .ok a => pure a
    if amount.signum < 0 then
      return .tecINTERNAL
    if amount.isZero || senderID == receiverID then
      continue
    if senderID == issuer || receiverID == issuer then
      if senderID == issuer then
        let sendAmount ← match amount.mpt with
          | .error e => throw e
          | .ok m => pure m.value_.toUInt64
        -- fixCleanup3_1_3 assumed enabled: aggregate MaximumAmount check.
        -- Condition order is critical — each guards the next subtraction
        -- against unsigned underflow.
        let exceedsMaximumAmount :=
          sendAmount > maximumAmount ||
          totalSendAmount > maximumAmount - sendAmount ||
          outstandingAmount > maximumAmount - sendAmount - totalSendAmount
        if exceedsMaximumAmount then
          return .tecPATH_DRY
        totalSendAmount := totalSendAmount + sendAmount
      -- direct send: redeeming MPTs and/or sending own MPTs
      let ter ← directSendNoFeeMPT senderID receiverID amount
      if !ter.isTesSuccess then
        return ter
      continue
    -- sending third-party MPTs: transit through the issuer
    let actualSend ← if waiveFee == .yes then pure amount
      else do
        let rate ← ApplyView.ofReadView (transferRateMPT mptIssue.getMptID)
        match multiplyRate amount rate mode with
        | .error e => throw e
        | .ok a => pure a
    takeFromSender ← match takeFromSender.operator_add actualSend mode with
      | .error e => throw e
      | .ok s => pure s
    let ter ← directSendNoFeeMPT issuer receiverID amount
    if !ter.isTesSuccess then
      return ter
  if senderID != issuer && !takeFromSender.isZero then
    directSendNoFeeMPT senderID issuer takeFromSender
  else
    return .tesSUCCESS

private def accountSendMultiMPT (senderID : AccountID) (mptIssue : MPTIssue)
    (receivers : MultiplePaymentDestinations) (mode : rounding_mode)
    (waiveFee : WaiveTransferFee) : ApplyView TER :=
  directSendNoLimitMultiMPT senderID mptIssue receivers mode waiveFee

def accountSendMulti (senderID : AccountID) (asset : Asset)
    (receivers : MultiplePaymentDestinations) (mode : rounding_mode)
    (waiveFee : WaiveTransferFee := .no) : ApplyView TER :=
  match asset with
  | .issue issue => accountSendMultiIOU senderID issue receivers mode waiveFee
  | .mptIssue mpt => accountSendMultiMPT senderID mpt receivers mode waiveFee

end XRPL.Model.Ledger.Helpers
