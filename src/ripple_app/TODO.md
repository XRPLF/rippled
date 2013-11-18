# ripple_app

## Peer.cpp

- Move magic number constants into Tuning.h / Peer constants enum 

- Wrap lines to 80 columns

- Use Journal

- Pass Journal in the ctor argument list

## Peers.cpp

## LedgerMaster.cpp

- Change getLedgerByHash() to not use "all bits zero" to mean
  "return the current ledger"

- replace uint32 with LedgerIndex and choose appropriate names

## Beast

- Change Stoppable to not require a constructor with parameters
