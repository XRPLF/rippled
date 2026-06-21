import XRPL.Model.Ledger.Helpers.RippleStateHelpers

set_option linter.style.longLine false
set_option linter.style.emptyLine false

namespace XRPL.FFI

open XRPL.Model.Protocol
open XRPL.Model.Ledger (Ledger)
open XRPL.Model.Ledger.Helpers (trustCreate trustDelete creditLimit creditBalance)

@[export lean_trust_create]
def lean_trust_create (ledger : Ledger) (bSrcHigh : UInt8) (uSrcAccountID uDstAccountID : AccountID)
    (uIndex : UInt256) (sleAccount : Option AccountRoot) (bAuth bNoRipple bFreeze bDeepFreeze : UInt8)
    (saBalance saLimit : STAmount) (uQualityIn uQualityOut : UInt32) : Ledger × Except String TER :=
  match (trustCreate (bSrcHigh != 0) uSrcAccountID uDstAccountID uIndex sleAccount
      (bAuth != 0) (bNoRipple != 0) (bFreeze != 0) (bDeepFreeze != 0)
      saBalance saLimit uQualityIn uQualityOut).run ledger with
  | .ok (ter, ledger') => (ledger', .ok ter)
  | .error e => (ledger, .error e)

@[export lean_trust_delete]
def lean_trust_delete (ledger : Ledger) (sleRippleState : RippleState)
    (uLowAccountID uHighAccountID : AccountID) : Ledger × Except String TER :=
  match (trustDelete sleRippleState uLowAccountID uHighAccountID).run ledger with
  | .ok (ter, ledger') => (ledger', .ok ter)
  | .error e => (ledger, .error e)

@[export lean_credit_limit]
def lean_credit_limit (ledger : Ledger) (account issuer : AccountID) (currency : Currency)
    : Ledger × Except String STAmount :=
  (ledger, (creditLimit account issuer currency).run ledger)

@[export lean_credit_balance]
def lean_credit_balance (ledger : Ledger) (account issuer : AccountID) (currency : Currency)
    : Ledger × Except String STAmount :=
  (ledger, (creditBalance account issuer currency).run ledger)

end XRPL.FFI
