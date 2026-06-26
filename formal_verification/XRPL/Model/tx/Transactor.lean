import XRPL.Model.Ledger.ApplyView
import XRPL.Model.Protocol.AccountID
import XRPL.Model.Protocol.Indexes
import XRPL.Model.Protocol.Rules
import XRPL.Model.Protocol.TER
import XRPL.Model.Protocol.XRPAmount
import XRPL.Model.tx.Tx


namespace XRPL.Model.tx

open XRPL.Model.Ledger
open XRPL.Model.Protocol

abbrev tfUniversalMask : UInt32 := 0x7FFFFFFF

-- The base Transactor. Methods with a `:=` body are base defaults.
class Transactor (T : Type) where
  baseTx : T → Tx
  checkExtraFeatures : T → Rules → Bool := fun _ _ => true
  getFlagsMask : T → Rules → UInt32 := fun _ _ => tfUniversalMask
  preflight : T → Rules → TER
  preclaim : T → ReadView TER
  doApply : T → ApplyView TER

-- First non-success result in a list, else `tesSUCCESS`
def firstError : List TER → TER
  | []      => .tesSUCCESS
  | t :: ts => if t.isTesSuccess then firstError ts else t

-- Subset of C++ `preflight0`+`preflight1`: txid, flags, account, fee.
-- Excluded checks: networkID, delegate, signingKey, ticket, batch.
def preflightBase (tx : Tx) (_rules : Rules) (flagMask : UInt32) : TER :=
  if tx.txID == 0 then .temINVALID
  else if (tx.flags &&& flagMask) != 0 then .temINVALID_FLAG
  else if tx.account == xrpAccount then .temBAD_SRC_ACCOUNT
  else if tx.fee.signum < 0 || tx.fee.value.toInt > (kMaxNativeN : Int) then .temBAD_FEE
  else .tesSUCCESS

def invokePreflight {T : Type} [Transactor T] (tx : T) (rules : Rules) : TER :=
  if !Transactor.checkExtraFeatures tx rules then
    .temDISABLED
  else
    let base := Transactor.baseTx tx
    let flagMask := Transactor.getFlagsMask tx rules
    firstError
      [ preflightBase base rules flagMask
      , Transactor.preflight tx rules ]

-- Subset of C++ `Transactor::apply()` before `doApply`: fee deduction, sequence bump.
-- Excluded: ticket, accountTxnID, feePayer.
def applyBase (tx : Tx) : ApplyView TER := do
  match ← ApplyView.peek (Keylet.account tx.account) with
  | some (.accountRoot ar) =>
      let newDrops : Int := ar.balance.signedDrops - tx.fee.value.toInt
      let ar := { ar with
        sequence := ar.sequence + 1
        balance := STAmount.ofNativeInt64 newDrops.toInt64 }
      ApplyView.update (.accountRoot ar)
      return .tesSUCCESS
  | _ => return .terNO_ACCOUNT

-- Protocol invariants
def checkInvariantsStub (_ledger _ledger' : Ledger) (_tx : Tx) (result : TER) : TER := result

-- Discard all tx effects, then charge the fee and consume the sequence
def reset (tx : Tx) (original : Ledger) : ApplyView TER := do
  set original
  match ← ApplyView.peek (Keylet.account tx.account) with
  | some (.accountRoot ar) =>
      let balance := ar.balance.signedDrops
      let fee := min balance tx.fee.value.toInt
      let ar := { ar with
        sequence := ar.sequence + 1
        balance := STAmount.ofNativeInt64 (balance - fee).toInt64 }
      ApplyView.update (.accountRoot ar)
      return .tesSUCCESS
  | _ => return .tefINTERNAL

-- `Transactor::operator()`: preflight, preclaim, apply (base + doApply)
def runTransactor {T : Type} [Transactor T] (tx : T) (rules : Rules) : ApplyView TER := do
  let preflightResult := invokePreflight tx rules
  if !preflightResult.isTesSuccess then return preflightResult
  let ledger ← get
  let preclaimResult ← ApplyView.ofReadView (Transactor.preclaim tx)
  let result ← do
    if preclaimResult.isTesSuccess then
      let baseResult ← applyBase (Transactor.baseTx tx)
      if baseResult.isTesSuccess then Transactor.doApply tx else pure baseResult
    else
      pure preclaimResult
  if result.isTesSuccess then
    let ledger' ← get
    return checkInvariantsStub ledger ledger' (Transactor.baseTx tx) result
  else if result.isTec then
    let resetResult ← reset (Transactor.baseTx tx) ledger
    return if resetResult.isTesSuccess then result else resetResult
  else
    set ledger
    return result

end XRPL.Model.tx
