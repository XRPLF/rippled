# RCL Consensus

This directory holds the types and classes needed
to connect the generic consensus algorithm to the
xrpld-specific instance of consensus.

- `RCLCxTx` adapts a `SHAMapItem` transaction.
- `RCLCxTxSet` adapts a `SHAMap` to represent a set of transactions.
- `RCLCxLedger` adapts a `Ledger`.
- `RCLConsensus` is implements the requirements of the generic
  `Consensus` class by connecting to the rest of the `xrpld`
  application.
