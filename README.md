# `rippled` - The XRP Ledger Server

The XRP Ledger is a decentralized cryptographic ledger powered by a network of peer-to-peer servers. The XRP Ledger uses a novel Byzantine Fault Tolerant consensus algorithm to settle and record transactions in a secure distributed database without a central operator. The server software that powers the XRP Ledger is called `rippled` and is available in this repository under the permissive [ISC open-source license](LICENSE). The XRP Ledger is the home of XRP, a digital asset designed to bridge the many different currencies in use worldwide.

[Ripple](https://ripple.com/) stewards the development of the XRP Ledger, and advances XRP as a key contribution to the Internet of Value: a world in which money moves the way information does today.


## What is XRP?

XRP is the digital asset native to the XRP Ledger. XRP was originally created in 2012 by Arthur Britto, Jed McCaleb and David Schwartz. XRP is public, counterparty-less and traded on the open-market, available for anyone to access. 100 billion units of XRP were generated in 2012 at conception.


## Key Features

- **[Censorship-Resistant Transaction Processing][]:** No single party decides which XRP transactions succeed or fail, and no one can "roll back" a transaction after it completes. As long as those who choose to participate in the network keep it healthy, they can send and receive XRP in seconds.
- **[Fast, Efficient Consensus Algorithm][]:** The XRP Ledger's consensus algorithm settles transactions in 4 to 5 seconds, processing at a throughput of up to 1500 transactions per second. These properties put XRP at least an order of magnitude ahead of other top digital assets.
- **[Finite XRP Supply][]:** When the XRP Ledger began, 100 billion XRP were created, and no more XRP will ever be created. (Each XRP is subdivisible down to 6 decimal places, for a grand total of 100 quintillion _drops_ of XRP.) The available supply of XRP decreases slowly over time as small amounts are destroyed to pay transaction costs.
- **[Responsible Software Governance][]:** A team of full-time, world-class developers at Ripple maintain and continually improve the XRP Ledger's underlying software with contributions from the open-source community. Ripple acts as a steward for the technology and an advocate for its interests, and builds constructive relationships with governments and financial institutions worldwide.
- **[Secure, Adaptable Cryptography][]:** The XRP Ledger relies on industry standard digital signature systems like ECDSA (the same scheme used by Bitcoin) but also supports modern, efficient algorithms like Ed25519. The extensible nature of the XRP Ledger's software makes it possible to add and disable algorithms as the state of the art in cryptography advances.
- **[Modern Features for Smart Contracts][]:** Features like Escrow, Checks, and Payment Channels support cutting-edge financial applications including the [Interledger Protocol](https://interledger.org/). This toolbox of advanced features comes with safety features like a process for amending the network and separate checks against invariant constraints.
- **[On-Ledger Decentralized Exchange][]:** In addition to all the features that make XRP useful on its own, the XRP Ledger also has a fully-functional accounting system for tracking and trading obligations denominated in any way users want, and an exchange built into the protocol. The XRP Ledger can settle long, cross-currency payment paths and exchanges of multiple currencies in atomic transactions, bridging gaps of trust with XRP.

[Censorship-Resistant Transaction Processing]: https://developers.ripple.com/xrp-ledger-overview.html#censorship-resistant-transaction-processing
[Fast, Efficient Consensus Algorithm]: https://developers.ripple.com/xrp-ledger-overview.html#fast-efficient-consensus-algorithm
[Finite XRP Supply]: https://developers.ripple.com/xrp-ledger-overview.html#finite-xrp-supply
[Responsible Software Governance]: https://developers.ripple.com/xrp-ledger-overview.html#responsible-software-governance
[Secure, Adaptable Cryptography]: https://developers.ripple.com/xrp-ledger-overview.html#secure-adaptable-cryptography
[Modern Features for Smart Contracts]: https://developers.ripple.com/xrp-ledger-overview.html#modern-features-for-smart-contracts
[On-Ledger Decentralized Exchange]: https://developers.ripple.com/xrp-ledger-overview.html#on-ledger-decentralized-exchange


## Source Code
[![travis-ci.org: Build Status](https://travis-ci.org/ripple/rippled.png?branch=develop)](https://travis-ci.org/ripple/rippled)
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

* [XRP Ledger Dev Portal](https://developers.ripple.com/)
* [XRP News](https://ripple.com/category/xrp/)
* [Setup and Installation](https://developers.ripple.com/install-rippled.html)

To learn about how Ripple is transforming global payments, visit
<https://ripple.com/contact/>.

---

Copyright © 2018, Ripple Labs. All rights reserved.
