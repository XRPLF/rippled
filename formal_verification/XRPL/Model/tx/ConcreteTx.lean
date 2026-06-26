import XRPL.Model.tx.transactors.vault.VaultSet


namespace XRPL.Model.tx

open XRPL.Model.Ledger
open XRPL.Model.Protocol

inductive ConcreteTx where
  | vaultSet (tx : VaultSetTx)

def processTx (txn : ConcreteTx) (rules : Rules) : ApplyView TER :=
  match txn with
  | .vaultSet tx => runTransactor tx rules

end XRPL.Model.tx
