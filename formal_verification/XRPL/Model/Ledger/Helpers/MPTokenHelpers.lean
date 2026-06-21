import XRPL.Model.Ledger.ApplyView
import XRPL.Model.Ledger.Helpers.AccountRootHelpers
import XRPL.Model.Protocol.Indexes
import XRPL.Model.Protocol.LedgerFormats
import XRPL.Model.Protocol.Rate
import XRPL.Model.Protocol.TxFlags
import XRPL.Model.Protocol.XRPAmount


namespace XRPL.Model.Ledger.Helpers

open XRPL.Model.Protocol
open XRPL.Model.Ledger

def canAddHoldingMPTIssue (mptIssue : MPTIssue) : ReadView TER := do
  match ← ReadView.read (Keylet.mptIssuance mptIssue.getMptID) with
  | some (.mptokenIssuance issuance) =>
    if !issuance.isFlag lsfMPTCanTransfer then
      return .tecNO_AUTH
    return .tesSUCCESS
  | _ => return .tecOBJECT_NOT_FOUND

def isGlobalFrozenMPT (mpt : MPTIssue) : ReadView Bool := do
  return match ← ReadView.read (Keylet.mptIssuance mpt.getMptID) with
    | some (.mptokenIssuance sle) => sle.isFlag lsfMPTLocked
    | _ => false

def isIndividualFrozenMPT (account : AccountID) (mpt : MPTIssue) : ReadView Bool := do
  let ledger ← read
  return match ledger.read (Keylet.mptoken mpt.getMptID account) with
    | some (.mptoken sle) => sle.isFlag lsfMPTLocked
    | _ => false

def maxMPTAmount (sleIssuance : MPTokenIssuance) : Int64 :=
  (sleIssuance.maximumAmount.getD maxMPTokenAmount.toUInt64).toInt64

def availableMPTAmount (sleIssuance : MPTokenIssuance) : Int64 :=
  maxMPTAmount sleIssuance - sleIssuance.outstandingAmount.toInt64

-- C++ declares AllowMPTOverflow in TokenHelpers.h; placed here to avoid an import cycle
inductive AllowMPTOverflow where | no | yes
  deriving DecidableEq, Repr, BEq

def isMPTOverflow (sendAmount : Int64) (outstandingAmount : UInt64) (maximumAmount : Int64)
    (allowOverflow : AllowMPTOverflow) : Bool :=
  let limit : UInt64 := if allowOverflow == .yes then maxUInt64 else maximumAmount.toUInt64
  sendAmount > maximumAmount || outstandingAmount > limit - sendAmount.toUInt64

def transferRateMPT (issuanceID : MPTID) : ReadView Rate := do
  match ← ReadView.read (Keylet.mptIssuance issuanceID) with
  | some (.mptokenIssuance sle) =>
    -- sfTransferFee is soeDEFAULT (absent ↔ 0), and fee 0 yields exactly the parity rate
    return ⟨1000000000 + 10000 * sle.transferFee.toUInt32⟩
  | _ => return kParityRate

-- Underlying asset of a vault pseudo-account's holding (an MPToken or a RippleState).
def assetOfHolding (issuance : MPTokenIssuance) (holding : LedgerEntry) : Option Asset :=
  match holding with
  | .mptoken m => some (.mptIssue { mptID := m.mptokenIssuanceID })
  | .rippleState rs =>
    match rs.lowLimit.mAsset with
    | .issue li =>
      let vaultPseudo := issuance.issuer
      let iouIssuer := if rs.lowLimit.getIssuer != vaultPseudo
                       then rs.lowLimit.getIssuer else rs.highLimit.getIssuer
      some (.issue ⟨li.currency, iouIssuer⟩)
    | _ => none
  | _ => none

def authorizeMPToken (priorBalance : XRPAmount) (mptIssuanceID : MPTID)
    (accountID : AccountID) (flags : UInt32) (holderID : Option AccountID) : ApplyView TER := do
  let issuanceKeylet := Keylet.mptIssuance mptIssuanceID
  let some (.accountRoot sleAcct) ← ApplyView.peek (Keylet.account accountID)
    | return .tecINTERNAL
  match holderID with
  | none =>
    let tokenKeylet := Keylet.mptoken mptIssuanceID accountID
    if flags &&& tfMPTUnauthorize != 0 then
      let some (.mptoken sleMpt) ← ApplyView.peek tokenKeylet
        | return .tecINTERNAL
      if sleMpt.mptAmount != 0 ∨ sleMpt.lockedAmount.getD 0 != 0 then
        return .tecINTERNAL
      if !(← ApplyView.dirRemoveStub accountID sleMpt.ownerNode sleMpt.key false) then
        return .tecINTERNAL
      adjustOwnerCount (some sleAcct) (-1)
      ApplyView.erase tokenKeylet
      return .tesSUCCESS
    let uOwnerCount := sleAcct.ownerCount
    let sb ← get
    let reserveCreate : XRPAmount :=
      if uOwnerCount < 2 then XRPAmount.zero
      else sb.fees.accountReserve (uOwnerCount + 1)
    if priorBalance.operator_lt reserveCreate then
      return .tecINSUFFICIENT_RESERVE
    let some (.mptokenIssuance mpt) ← ApplyView.peek issuanceKeylet
      | return .tecINTERNAL
    if mpt.issuer == accountID then
      return .tecINTERNAL
    let mptoken : MPToken :=
      { key := tokenKeylet.key, flags := 0, account := accountID,
        mptokenIssuanceID := mptIssuanceID, mptAmount := 0, lockedAmount := none, ownerNode := 0,
        previousTxnID := 0, previousTxnLgrSeq := 0 }
    let ter ← ApplyView.dirLinkStub accountID mptoken
    if ter.operator_bool then
      return ter
    ApplyView.insert (.mptoken mptoken)
    adjustOwnerCount (some sleAcct) 1
    return .tesSUCCESS
  | some holderID =>
    let some (.mptokenIssuance sleMptIssuance) ← ApplyView.peek issuanceKeylet
      | return .tecINTERNAL
    if accountID != sleMptIssuance.issuer then
      return .tecINTERNAL
    let some (.mptoken sleMpt) ← ApplyView.peek (Keylet.mptoken mptIssuanceID holderID)
      | return .tecINTERNAL
    let flagsIn := sleMpt.flags
    let flagsOut := if flags &&& tfMPTUnauthorize != 0 then
        flagsIn &&& ~~~lsfMPTAuthorized
      else
        flagsIn ||| lsfMPTAuthorized
    ApplyView.update (.mptoken { sleMpt with flags := flagsOut })
    return .tesSUCCESS

def addEmptyHoldingMPTIssue (accountID : AccountID) (priorBalance : XRPAmount)
    (mptIssue : MPTIssue) : ApplyView TER := do
  let mptID := mptIssue.getMptID
  let some (.mptokenIssuance sleMpt) ← ApplyView.peek (Keylet.mptIssuance mptID)
    | return .tefINTERNAL
  if sleMpt.isFlag lsfMPTLocked then
    return .tefINTERNAL
  if (← ApplyView.peek (Keylet.mptoken mptID accountID)).isSome then
    return .tecDUPLICATE
  if accountID == mptIssue.getIssuer then
    return .tesSUCCESS
  authorizeMPToken priorBalance mptID accountID 0 none

def removeEmptyHoldingMPTIssue (accountID : AccountID) (mptIssue : MPTIssue) : ApplyView TER := do
  let accountIsIssuer := accountID == mptIssue.getIssuer
  let mptID := mptIssue.getMptID
  let some (.mptoken mptoken) ← ApplyView.peek (Keylet.mptoken mptID accountID)
    | return (if accountIsIssuer then .tesSUCCESS else .tecOBJECT_NOT_FOUND)
  if mptoken.mptAmount != 0 || mptoken.lockedAmount.getD 0 != 0 then
    return .tecHAS_OBLIGATIONS
  authorizeMPToken XRPAmount.zero mptID accountID tfMPTUnauthorize none

end XRPL.Model.Ledger.Helpers
