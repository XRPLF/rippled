import XRPL.Model.Ledger.ApplyView
import XRPL.Model.Protocol.AccountID
import XRPL.Model.Protocol.Asset
import XRPL.Model.Protocol.LedgerEntries.Vault
import XRPL.Model.Protocol.STAmount
import XRPL.Model.Protocol.TER
import XRPL.Model.tx.Transactor


namespace XRPL.Model.Protocol

open XRPL.Model.Ledger
open XRPL.Model.Protocol (TER)
open XRPL.Model.tx

structure VaultDepositTx where
  txID : String
  account : AccountID
  vaultID : UInt256
  amount  : STAmount

instance : Tx VaultDepositTx where
  txID := (·.txID)
  account := (·.account)

structure VaultDepositTr where

def VaultDepositTr.PreflightAny (α : Type) [Tx α] (ctx : PreflightContext α) : TER := Id.run do
  if Tx.txID ctx.tx = "" then
    return TER.temMALFORMED
  return TER.tesSUCCESS

def VaultDepositTr.Preflight (ctx : PreflightContext VaultDepositTx) : TER := Id.run do
  if ctx.tx.vaultID = 0 then
    return TER.temMALFORMED
  if ctx.tx.amount.negative then
    return TER.temBAD_AMOUNT
  return TER.tesSUCCESS

def VaultDepositTx.preclaimCheck (tx : VaultDepositTx) (vault : Vault) : TER :=
  if !Asset.equiv tx.amount.mAsset vault.asset then
    TER.tecWRONG_ASSET
  else
    TER.tesSUCCESS

def VaultDepositTr.Preclaim (ctx : PreclaimContext VaultDepositTx) : ReadView TER := do
  let some (.vault vault) ← ReadView.read ⟨.vault, ctx.tx.vaultID⟩
    | return TER.tecNO_ENTRY
  return ctx.tx.preclaimCheck vault

end XRPL.Model.Protocol
