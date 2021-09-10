## Introduction

Side chain federators work by controlling an account on the main chain and an
account on the side chain. The account on the side chain is the root account.
The account on the main chain is specified in the configuration file (See
[configFile.md](docs/sidechain/configFile.md) for the new configuration file
stanzas).

The test scripts will set up these accounts for you when running a test network
on your local machine (see the functions `setup_mainchain` and `setup_sidechain`
in the sidechain.py module). This document describes what's needed to set up
these accounts if not using the scripts.

## Transactions

* `SignerListSet`: Since the federators will jointly control these accounts, a
  `SignerListSet` transaction must be sent to both the main chain account and
  the side chain account. The signer list should consist of the federator's
  public signing keys and should match the keys specified in the config file.
  The quorum should be set to 80% of the federators on the list (i.e. for five
  federators, set this to 4).
  
The federators use tickets to handle unusual situations. For example, if the
federators fall too far behind they will disallow new cross chain transactions
until they catch up. Three tickets are needed, and three transactions are needed
to create the tickets (since they use the source tag as a way to set the purpose
for the ticket).
  
* `Ticket`: Sent a `Ticket` transaction with the source tag of `1` to both the
  main chain account and side chain account.

* `Ticket`: Sent a `Ticket` transaction with the source tag of `2` to both the
  main chain account and side chain account.

* `Ticket`: Sent a `Ticket` transaction with the source tag of `3` to both the
  main chain account and side chain account.

* `TrustSet` if the cross chain transactions involve issued assets (IOUs), set
  up the trust lines by sending a `TrustSet` transaction to the appropriate
  accounts. If the cross chain transactions only involve XRP, this is not
  needed.
  
* `AccountSet`: Disable the master key with an `AccountSet` transactions. This
  ensures that nothing except the federators (as a group) control these
  accounts. Send this transaction to both the main chain account and side chain
  account.

*Important*: The `AccountSet` transaction that disables the master key *must* be
the last transaction. The federator's initialization code uses this to
distinguish transactions that are part of setup and other transactions.
