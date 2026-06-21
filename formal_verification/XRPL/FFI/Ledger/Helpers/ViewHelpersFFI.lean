import XRPL.FFI.CommonFFI
import XRPL.Model.Ledger.View

set_option linter.style.longLine false
set_option linter.style.emptyLine false

namespace XRPL.FFI

open XRPL.Model.Protocol
open XRPL.Model.Ledger (Ledger doWithdraw)

@[export lean_do_withdraw]
def lean_do_withdraw (ledger : Ledger) (credentialIDs : Option STVector256)
    (senderAcct dstAcct sourceAcct : AccountID) (priorBalance : Int64)
    (amount : STAmount) (mode : UInt8) : Ledger × Except String TER :=
  match (doWithdraw credentialIDs senderAcct dstAcct sourceAcct ⟨priorBalance⟩ amount (decodeMode mode)).run ledger with
  | .ok (ter, ledger') => (ledger', .ok ter)
  | .error e => (ledger, .error e)

end XRPL.FFI
