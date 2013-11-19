# ripple_app

## Ledger.cpp

- Move all inlines into the .cpp, make the interface abstract

- Move static database functions into a real class, perhaps LedgerMaster

## LedgerMaster.cpp

- Change getLedgerByHash() to not use "all bits zero" to mean
  "return the current ledger"

- replace uint32 with LedgerIndex and choose appropriate names

## Beast

- Change Stoppable to not require a constructor with parameters
