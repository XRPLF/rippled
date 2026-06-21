import XRPL.Model.Ledger.ApplyView
import XRPL.Model.Ledger.Helpers.AccountRootHelpers
import XRPL.Model.Protocol.LedgerFormats
import XRPL.Model.Protocol.STVector256


namespace XRPL.Model.Ledger.Helpers

open XRPL.Model.Protocol
open XRPL.Model.Ledger

-- now > expiration (value_or UINT32_MAX), where now is parentCloseTime.
def checkExpired (sleCredential : Credential) (closed : NetClock.TimePoint) : Bool :=
  closed > sleCredential.expiration.getD maxUInt32

def validDomain (domainID : UInt256) (subject : AccountID) : ReadView TER := do
  let some (.permissionedDomain slePD) ← ReadView.read (Keylet.permissionedDomain domainID)
    | return .tecOBJECT_NOT_FOUND
  let ledger ← read
  let closeTime := ledger.parentCloseTime
  let mut foundExpired := false
  for ac in slePD.acceptedCredentials do
    match ledger.read (Keylet.credential subject ac.issuer ac.credentialType) with
    | some (.credential cred) =>
      if checkExpired cred closeTime then
        foundExpired := true
      else if cred.isFlag lsfAccepted then
        return .tesSUCCESS
    | _ => pure ()
  return (if foundExpired then .tecEXPIRED else .tecNO_AUTH)

private def delSLE (sleCredential : Credential) (account : AccountID) (page : UInt64) (isOwner : Bool) : ApplyView TER := do
  let some (.accountRoot sleAccount) ← ApplyView.peek (Keylet.account account)
    | return .tecINTERNAL
  if !(← ApplyView.dirRemoveStub account page sleCredential.key false) then
    return .tefBAD_LEDGER
  if isOwner then
    adjustOwnerCount (some sleAccount) (-1)
  return .tesSUCCESS

def deleteSLE (sleCredential : Option Credential) : ApplyView TER := do
  let some sleCredential := sleCredential
    | return .tecNO_ENTRY
  let issuer := sleCredential.issuer
  let subject := sleCredential.subject
  let accepted := sleCredential.isFlag lsfAccepted
  let err ← delSLE sleCredential issuer sleCredential.issuerNode (!accepted || subject == issuer)
  if !err.isTesSuccess then
    return err
  if subject != issuer then
    let err2 ← delSLE sleCredential subject (sleCredential.subjectNode.getD 0) accepted
    if !err2.isTesSuccess then
      return err2
  ApplyView.erase ⟨.credential, sleCredential.key⟩
  return .tesSUCCESS

private def removeExpired (credIDs : STVector256) : ApplyView (Except TER Bool) := do
  let closeTime := (← get).parentCloseTime
  let mut foundExpired := false
  for credID in credIDs do
    match ← ApplyView.peek ⟨.credential, credID⟩ with
    | some (.credential sleCred) =>
      if checkExpired sleCred closeTime then
        let err ← deleteSLE (some sleCred)
        if !err.isTesSuccess then
          return .error err
        foundExpired := true
    | _ => pure ()
  return .ok foundExpired

def authorizedDepositPreauth (credIDs : STVector256) (dst : AccountID) : ReadView TER := do
  let mut creds : List AcceptedCredential := []
  for credID in credIDs do
    match ← ReadView.read ⟨.credential, credID⟩ with
    | some (.credential cred) =>
      let ac : AcceptedCredential := { issuer := cred.issuer, credentialType := cred.credentialType }
      if creds.contains ac then
        return .tefINTERNAL
      creds := creds ++ [ac]
    | _ => return .tefINTERNAL
  let view ← read
  -- Keylet.depositPreauthCreds dedups + sorts, so the collected order doesn't matter.
  let preauth := Keylet.depositPreauthCreds dst creds
  if (view.read preauth).isNone then
    return .tecNO_PERMISSION
  return .tesSUCCESS

def verifyDepositPreauth (credentialIDs : Option STVector256) (src dst : AccountID)
    (sleDst : Option AccountRoot) : ApplyView TER := do
  let credentialsPresent := credentialIDs.isSome
  if credentialsPresent then
    match ← removeExpired (credentialIDs.getD []) with
    | .error ter => return ter
    | .ok foundExpired =>
      if foundExpired then
        return .tecEXPIRED
  let depositAuth := match sleDst with
    | some d => d.isFlag lsfDepositAuth
    | none => false
  if depositAuth && src != dst then
    let sb ← get
    if (sb.read (Keylet.depositPreauthAccount dst src)).isNone then
      if credentialsPresent then
        return ← ApplyView.ofReadView (authorizedDepositPreauth (credentialIDs.getD []) dst)
      else
        return .tecNO_PERMISSION
  return .tesSUCCESS

end XRPL.Model.Ledger.Helpers
