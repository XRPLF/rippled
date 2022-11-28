# The XRP Ledger

The [XRP Ledger](https://xrpl.org/) is a decentralized cryptographic ledger powered by a network of peer-to-peer nodes. The XRP Ledger uses a novel Byzantine Fault Tolerant consensus algorithm to settle and record transactions in a secure distributed database without a central operator.

## XRP
[XRP](https://xrpl.org/xrp.html) is a public, counterparty-free asset native to the XRP Ledger, and is designed to bridge the many different currencies in use worldwide. XRP is traded on the open-market and is available for anyone to access. The XRP Ledger was created in 2012 with a finite supply of 100 billion units of XRP. Its creators gifted 80 billion XRP to a company, now called [Ripple](https://ripple.com/), to develop the XRP Ledger and its ecosystem. Ripple uses XRP to help build the Internet of Value, ushering in a world in which money moves as fast and efficiently as information does today.

## rippled
The server software that powers the XRP Ledger is called `rippled` and is available in this repository under the permissive [ISC open-source license](LICENSE.md). The `rippled` server software is written primarily in C++ and runs on a variety of platforms. The `rippled` server software can run in several modes depending on its [configuration](https://xrpl.org/rippled-server-modes.html).  

### Build from Source

* [Linux](Builds/linux/README.md)
* [Mac](Builds/macos/README.md) (Not recommended for production)
* [Windows](Builds/VisualStudio2017/README.md) (Not recommended for production)

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
* [Source Documentation (Doxygen)](https://xrplf.github.io/rippled/)
* [Mailing List for Release Announcements](https://groups.google.com/g/ripple-server)
* [Learn more about the XRP Ledger (YouTube)](https://www.youtube.com/playlist?list=PLJQ55Tj1hIVZtJ_JdTvSum2qMTsedWkNi)
