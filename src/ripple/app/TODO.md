# ripple_app

## LedgerMaster.cpp

- Change getLedgerByHash() to not use "all bits zero" to mean
  "return the current ledger"

- replace uint32 with LedgerIndex and choose appropriate names

## IHashRouter.h

- Rename to HashRouter.h

## HashRouter.cpp

- Rename HashRouter to HashRouterImp

- Inline functions

- Comment appropriately

- Determine the semantics of the uint256, replace it with an appropriate
  typedef like RipplePublicKey or whatever is appropriate.

- Provide good symbolic names for the config tunables.

## Beast

- Change Stoppable to not require a constructor with parameters
