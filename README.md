# XRP Ledger Side chain Branch

## Warning

This is not the main branch of the XRP Ledger. This branch supports side chains
on the XRP ledger and integrates an implementation of side chain federators.
This is a developers prelease and it should not be used for production or to
transfer real value. Consider this "alpha" quality software. There will be bugs.
See "Status" for a fuller description.

The latest production code for the XRP ledger is on the "master" branch.

Until this branch is merged with the mainline branch, it will periodically be
rebased on that branch and force pushed to github.

## What are side chains?

Side chains are independent ledgers. They can have their own transaction types,
their own UNL list (or even their own consensus algorithm), and their own set of
other unique features (like a smart contracts layer). What's special about them
is there is a way to transfer assets from the XRP ledger to the side chain, and
a way to return those assets back from the side chain to the XRP ledger. Both
XRP and issued assets may be exchanged.

The motivation for creating a side chain is to implement an idea that may not be
a great fit for the main XRP ledger, or may take a long time before such a
feature is adopted by the main XRP ledger. The advantage of a side chain over a
brand new ledger is it allows the side chain to immediate use tokens with real
monetary value.

This implementation is meant to support side chains that are similar to the XRP
ledger and use the XRP ledger as the main chain. The idea is to develop a new
side chain, first this code will be forked and the new features specific to the
new chain will be implemented.

## Status

All the functionality needed to build side chains should be complete. However,
it has not been well tested or polished.

In particular, all of the following are built:

* Cross chain transactions for both XRP and Issued Assets
* Refunds if transactions fail
* Allowing federators to rejoin a network
* Detecting and handling when federators fall too far behind in processing
  transactions
* A python library to easily create configuration files for testing side chains
  and spin up side chains on a local machine
* Python scripts to test side chains
* An interactive shell to explore side chain functionality

The biggest missing pieces are:

* Testing: While the functionality is there, it has just begun to be tested.
  There will be bugs. Even horrible and embarrassing bugs. Of course, this will
  improve as testing progresses.

* Tooling: There is a python library and an interactive shell that was built to
  help development. However, these tools are geared to run a test network on a
  local machine. They are not geared to new users or to production systems.
  Better tooling is coming.

* Documentation: There is documentation that describes the technical details of
  how side chains works, how to run the python scripts to set up side chains on
  the local machine, and the changes to the configuration files. However, like
  the tooling, this is not geared to new users or production systems. Better
  documentation is coming. In particular, good documentation for how to set up a
  production side chain - or even a test net that doesn't run on a local
  machine - needs to be written.

## Getting Started

See the instructions [here](docs/sidechain/GettingStarted.md) for how to
run an interactive shell that will spin up a set of federators on your local
machine and allow you to transfer assets between the main chain and a side
chain.

After setting things up and completing a cross chain transaction with the
"getting started" script above, it may to useful to browse some other
documentation:

* [This](bin/sidechain/python/README.md) document describes the scripts and
  python modules used to test and explore side chains on your local machine.
  
* [This](docs/sidechain/configFile.md) document describes the new stanzas in the
  config file needed for side chains.
  
* [This](docs/sidechain/federatorAccountSetup.md) document describes how to set
  up the federator accounts if not using the python scripts.

* [This](docs/sidechain/design.md) document describes the low-level details for
  how side chains work.

# The XRP Ledger

The [XRP Ledger](https://xrpl.org/) is a decentralized cryptographic ledger powered by a network of peer-to-peer servers. The XRP Ledger uses a novel Byzantine Fault Tolerant consensus algorithm to settle and record transactions in a secure distributed database without a central operator.

## XRP
[XRP](https://xrpl.org/xrp.html) is a public, counterparty-free asset native to the XRP Ledger, and is designed to bridge the many different currencies in use worldwide. XRP is traded on the open-market and is available for anyone to access. The XRP Ledger was created in 2012 with a finite supply of 100 billion units of XRP. Its creators gifted 80 billion XRP to a company, now called [Ripple](https://ripple.com/), to develop the XRP Ledger and its ecosystem. Ripple uses XRP to help build the Internet of Value, ushering in a world in which money moves as fast and efficiently as information does today.

## rippled
The server software that powers the XRP Ledger is called `rippled` and is available in this repository under the permissive [ISC open-source license](LICENSE). The `rippled` server is written primarily in C++ and runs on a variety of platforms.

### Build from Source

* [Linux](Builds/linux/README.md)
* [Mac](Builds/macos/README.md)
* [Windows](Builds/VisualStudio2017/README.md)

## Key Features of the XRP Ledger

- **[Censorship-Resistant Transaction Processing][]:** No single party decides which transactions succeed or fail, and no one can "roll back" a transaction after it completes. As long as those who choose to participate in the network keep it healthy, they can settle transactions in seconds.
- **[Fast, Efficient Consensus Algorithm][]:** The XRP Ledger's consensus algorithm settles transactions in 4 to 5 seconds, processing at a throughput of up to 1500 transactions per second. These properties put XRP at least an order of magnitude ahead of other top digital assets.
- **[Finite XRP Supply][]:** When the XRP Ledger began, 100 billion XRP were created, and no more XRP will ever be created. The available supply of XRP decreases slowly over time as small amounts are destroyed to pay transaction costs.
- **[Responsible Software Governance][]:** A team of full-time, world-class developers at Ripple maintain and continually improve the XRP Ledger's underlying software with contributions from the open-source community. Ripple acts as a steward for the technology and an advocate for its interests, and builds constructive relationships with governments and financial institutions worldwide.
- **[Secure, Adaptable Cryptography][]:** The XRP Ledger relies on industry standard digital signature systems like ECDSA (the same scheme used by Bitcoin) but also supports modern, efficient algorithms like Ed25519. The extensible nature of the XRP Ledger's software makes it possible to add and disable algorithms as the state of the art in cryptography advances.
- **[Modern Features for Smart Contracts][]:** Features like Escrow, Checks, and Payment Channels support cutting-edge financial applications including the [Interledger Protocol](https://interledger.org/). This toolbox of advanced features comes with safety features like a process for amending the network and separate checks against invariant constraints.
- **[On-Ledger Decentralized Exchange][]:** In addition to all the features that make XRP useful on its own, the XRP Ledger also has a fully-functional accounting system for tracking and trading obligations denominated in any way users want, and an exchange built into the protocol. The XRP Ledger can settle long, cross-currency payment paths and exchanges of multiple currencies in atomic transactions, bridging gaps of trust with XRP.

[Censorship-Resistant Transaction Processing]: https://xrpl.org/xrp-ledger-overview.html#censorship-resistant-transaction-processing
[Fast, Efficient Consensus Algorithm]: https://xrpl.org/xrp-ledger-overview.html#fast-efficient-consensus-algorithm
[Finite XRP Supply]: https://xrpl.org/xrp-ledger-overview.html#finite-xrp-supply
[Responsible Software Governance]: https://xrpl.org/xrp-ledger-overview.html#responsible-software-governance
[Secure, Adaptable Cryptography]: https://xrpl.org/xrp-ledger-overview.html#secure-adaptable-cryptography
[Modern Features for Smart Contracts]: https://xrpl.org/xrp-ledger-overview.html#modern-features-for-smart-contracts
[On-Ledger Decentralized Exchange]: https://xrpl.org/xrp-ledger-overview.html#on-ledger-decentralized-exchange


## Source Code
[![travis-ci.com: Build Status](https://travis-ci.com/ripple/rippled.svg?branch=develop)](https://travis-ci.com/ripple/rippled)
[![codecov.io: Code Coverage](https://codecov.io/gh/ripple/rippled/branch/develop/graph/badge.svg)](https://codecov.io/gh/ripple/rippled)

### Repository Contents

| Folder     | Contents                                         |
|:-----------|:-------------------------------------------------|
| `./bin`    | Scripts and data files for Ripple integrators.   |
| `./Builds` | Platform-specific guides for building `rippled`. |
| `./docs`   | Source documentation files and doxygen config.   |
| `./cfg`    | Example configuration files.                     |
| `./src`    | Source code.                                     |

Some of the directories under `src` are external repositories included using
git-subtree. See those directories' README files for more details.


## See Also

* [XRP Ledger Dev Portal](https://xrpl.org/)
* [Setup and Installation](https://xrpl.org/install-rippled.html)
* [Source Documentation (Doxygen)](https://ripple.github.io/rippled)
