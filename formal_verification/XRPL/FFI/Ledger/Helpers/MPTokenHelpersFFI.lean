import XRPL.Model.Ledger.Helpers.MPTokenHelpers

set_option linter.style.longLine false
set_option linter.style.emptyLine false

namespace XRPL.FFI

open XRPL.Model.Protocol (MPTID AccountID TER)
open XRPL.Model.Ledger (Ledger)
open XRPL.Model.Ledger.Helpers (authorizeMPToken)

@[export lean_authorize_mptoken]
def lean_authorize_mptoken (ledger : Ledger) (priorBalance : Int64) (mptIssuanceID : MPTID)
    (accountID : AccountID) (flags : UInt32) (holderID : Option AccountID) : Ledger × Except String TER :=
  match (authorizeMPToken ⟨priorBalance⟩ mptIssuanceID accountID flags holderID).run ledger with
  | .ok (ter, ledger') => (ledger', .ok ter)
  | .error e => (ledger, .error e)

end XRPL.FFI
