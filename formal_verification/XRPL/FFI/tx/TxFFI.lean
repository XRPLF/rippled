import XRPL.FFI.CommonFFI
import XRPL.Model.tx.ConcreteTx

set_option linter.style.longLine false

namespace XRPL.FFI

open XRPL.Model.Protocol
open XRPL.Model.Ledger (Ledger)
open XRPL.Model.tx

-- The single process-transaction FFI export (for all transactors).
@[export lean_process_tx]
def lean_process_tx (ledger : Ledger) (txn : ConcreteTx) (rules : Rules)
    : Ledger × Except String TER :=
  match (processTx txn rules).run ledger with
  | .ok (ter, ledger') => (ledger', .ok ter)
  | .error e => (ledger, .error e)

end XRPL.FFI
