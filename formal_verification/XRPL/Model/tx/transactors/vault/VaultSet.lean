import XRPL.Model.tx.Transactor
import XRPL.Model.Protocol.LedgerEntries.Vault
import XRPL.Model.Protocol.LedgerEntries.MPTokenIssuance
import XRPL.Model.Protocol.LedgerEntries.PermissionedDomain
import XRPL.Model.Protocol.STNumber
import XRPL.Model.Protocol.LedgerFormats
import XRPL.Model.Basics.Blob


namespace XRPL.Model.tx

open XRPL.Model.Ledger
open XRPL.Model.Protocol

abbrev kMaxDataPayloadLength : Nat := 256

structure VaultSetTx extends Tx where
  vaultID : UInt256 := 0
  data : Option Blob := none
  assetsMaximum : Option STNumber := none
  domainID : Option UInt256 := none

def vaultSetCheckExtraFeatures (tx : VaultSetTx) (r : Rules) : Bool :=
  r.enabled .singleAssetVault && (tx.domainID.isNone || r.enabled .permissionedDomains)

def vaultSetPreflight (tx : VaultSetTx) (_rules : Rules) : TER := Id.run do
  if tx.vaultID == 0 then
    return .temMALFORMED
  if let some d := tx.data then
    if d.isEmpty || d.length > kMaxDataPayloadLength then
      return .temMALFORMED
  if let some am := tx.assetsMaximum then
    if am.value.signum < 0 then
      return .temMALFORMED
  if tx.domainID.isNone && tx.assetsMaximum.isNone && tx.data.isNone then
    return .temMALFORMED
  return .tesSUCCESS

def vaultSetPreclaim (tx : VaultSetTx) : ReadView TER := do
  let some (.vault vault) ← ReadView.read ⟨.vault, tx.vaultID⟩
    | return .tecNO_ENTRY
  if tx.account != vault.owner then
    return .tecNO_PERMISSION
  let some (.mptokenIssuance issuance) ← ReadView.read (Keylet.mptIssuance vault.shareMPTID)
    | return .tefINTERNAL
  match tx.domainID with
  | some domain =>
      if (vault.flags &&& lsfVaultPrivate) != lsfVaultPrivate then
        return .tecNO_PERMISSION
      if domain != 0 then
        let some (.permissionedDomain _) ← ReadView.read (Keylet.permissionedDomain domain)
          | return .tecOBJECT_NOT_FOUND
      if !issuance.isFlag lsfMPTRequireAuth then
        return .tefINTERNAL
      return .tesSUCCESS
  | none => return .tesSUCCESS

def vaultSetDoApply (tx : VaultSetTx) : ApplyView TER := do
  let some (.vault vault0) ← ApplyView.peek ⟨.vault, tx.vaultID⟩
    | return .tefINTERNAL
  let vaultAsset := vault0.asset
  let some (.mptokenIssuance issuance0) ← ApplyView.peek (Keylet.mptIssuance vault0.shareMPTID)
    | return .tefINTERNAL
  let mut vault := vault0
  if let some d := tx.data then
    vault := { vault with data := some d }
  if let some am := tx.assetsMaximum then
    if !(am.value.operator_eq Number.zero) then
      match vault.assetsTotal with
      | some total => if am.value.operator_lt total.value then return .tecLIMIT_EXCEEDED
      | none => pure ()
    vault := { vault with assetsMaximum := some am }
  if let some domainId := tx.domainID then
    let issuance :=
      if domainId != 0 then { issuance0 with domainID := some domainId }
      else { issuance0 with domainID := none }
    ApplyView.update (.mptokenIssuance issuance)
  match vault.associateAsset vaultAsset .to_nearest with
  | .error _ => return .tefINTERNAL
  | .ok finalVault =>
      ApplyView.update (.vault finalVault)
      return .tesSUCCESS

-- VaultSet's tx-specific valid flags (TxFlags.h: tfVaultDepositBlock/Unblock).
def tfVaultDepositBlock : UInt32 := 0x00010000
def tfVaultDepositUnblock : UInt32 := 0x00020000

-- Allow VaultSet's own flags
def vaultSetFlagsMask (_ : VaultSetTx) (_ : Rules) : UInt32 :=
  tfUniversalMask &&& ~~~(tfVaultDepositBlock ||| tfVaultDepositUnblock)

instance : Transactor VaultSetTx where
  baseTx := VaultSetTx.toTx
  checkExtraFeatures := vaultSetCheckExtraFeatures
  getFlagsMask := vaultSetFlagsMask
  preflight := vaultSetPreflight
  preclaim := vaultSetPreclaim
  doApply := vaultSetDoApply

end XRPL.Model.tx
