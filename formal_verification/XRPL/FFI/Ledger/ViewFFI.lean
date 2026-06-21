import XRPL.Model.Ledger.View

set_option linter.style.longLine false
set_option linter.style.emptyLine false

namespace XRPL.FFI

open XRPL.Model.Protocol (AccountRoot TER AccountID STAmount)
open XRPL.Model.Ledger (Ledger hasExpired canWithdrawToSle canWithdrawFromTo canWithdrawTx)

@[export lean_has_expired]
def lean_has_expired (ledger : Ledger) (exp : Option UInt32) : Ledger × Except String Bool :=
  (ledger, (hasExpired exp).run ledger)

@[export lean_can_withdraw_to_sle]
def lean_can_withdraw_to_sle (ledger : Ledger) (from_ to_ : AccountID) (toSle : Option AccountRoot)
    (amount : STAmount) (hasDestinationTag : UInt8) : Ledger × Except String TER :=
  (ledger, (canWithdrawToSle from_ to_ toSle amount (hasDestinationTag != 0)).run ledger)

@[export lean_can_withdraw_from_to]
def lean_can_withdraw_from_to (ledger : Ledger) (from_ to_ : AccountID) (amount : STAmount)
    (hasDestinationTag : UInt8) : Ledger × Except String TER :=
  (ledger, (canWithdrawFromTo from_ to_ amount (hasDestinationTag != 0)).run ledger)

@[export lean_can_withdraw_tx]
def lean_can_withdraw_tx (ledger : Ledger) (from_ : AccountID) (to_ : Option AccountID)
    (amount : STAmount) (destinationTag : Option UInt32) : Ledger × Except String TER :=
  (ledger, (canWithdrawTx from_ to_ amount destinationTag).run ledger)

end XRPL.FFI
