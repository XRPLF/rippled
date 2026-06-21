import XRPL.Model.Ledger.Ledger


namespace XRPL.Model.Ledger

open XRPL.Model.Protocol

abbrev ReadView := ReaderT Ledger (Except String)

def ReadView.read (k : Keylet) : ReadView (Option LedgerEntry) := do
  return (← readThe Ledger).read k

-- closed ledger model, so always false
def ReadView.openStub : ReadView Bool := pure false

end XRPL.Model.Ledger
