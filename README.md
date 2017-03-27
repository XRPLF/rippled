![Ripple](/images/ripple.png)

# What is Ripple?
Ripple is a network of computers which use the Ripple consensus algorithm to atomically settle and record
transactions on a secure distributed database, the Ripple Consensus Ledger
(RCL). Because of its distributed nature, the RCL offers transaction immutability
without a central operator. The RCL contains a built-in currency exchange and its
path-finding algorithm finds competitive exchange rates across order books
and currency pairs.

### Key Features
- **Distributed**
  - Direct account-to-account settlement with no central operator
  - Decentralized global market for competitive FX
- **Secure**
  - Transactions are cryptographically signed using ECDSA or Ed25519
  - Multi-signing capabilities
- **Scalable**
  - Capacity to process the world’s cross-border payments volume
  - Easy access to liquidity through a competitive FX marketplace

## Cross-border payments
Ripple enables banks to settle cross-border payments in real-time, with
end-to-end transparency, and at lower costs. Banks can provide liquidity
for FX themselves or source it from third parties.

As Ripple adoption grows, so do the number of currencies and counterparties.
Liquidity providers need to maintain accounts with each counterparty for
each currency – a capital- and time-intensive endeavor that spreads liquidity
thin. Further, some transactions, such as exotic currency trades, will require
multiple trading parties, who each layer costs to the transaction. Thin
liquidity and many intermediary trading parties make competitive pricing
challenging.

![Flow - Direct](images/flow1.png)

### XRP as a Bridge Currency
Ripple can bridge even exotic currency pairs directly through XRP. Similar to
USD in today’s currency market, XRP allows liquidity providers to focus on
offering competitive FX rates on fewer pairs and adding depth to order books.
Unlike USD, trading through XRP does not require bank accounts, service fees,
counterparty risk, or additional operational costs. By using XRP, liquidity
providers can specialize in certain currency corridors, reduce operational
costs, and ultimately, offer more competitive FX pricing.

![Flow - Bridged over XRP](images/flow2.png)

# rippled - Ripple server
`rippled` is the reference server implementation of the Ripple
protocol. To learn more about how to build and run a `rippled`
server, visit https://ripple.com/build/rippled-setup/

[![travis-ci.org: Build Status](https://travis-ci.org/ripple/rippled.png?branch=develop)](https://travis-ci.org/ripple/rippled)
[![codecov.io: Code Coverage](https://codecov.io/gh/ripple/rippled/branch/develop/graph/badge.svg)](https://codecov.io/gh/ripple/rippled)

### License
`rippled` is open source and permissively licensed under the
ISC license. See the LICENSE file for more details.

#### Repository Contents

| Folder  | Contents |
|---------|----------|
| ./bin   | Scripts and data files for Ripple integrators. |
| ./build | Intermediate and final build outputs.          |
| ./Builds| Platform or IDE-specific project files.        |
| ./doc   | Documentation and example configuration files. |
| ./src   | Source code.                                   |

Some of the directories under `src` are external repositories inlined via
git-subtree. See the corresponding README for more details.

##For more information:

* [Ripple Knowledge Center](https://ripple.com/learn/)
* [Ripple Developer Center](https://ripple.com/build/)
* [Ripple Whitepapers & Reports](https://ripple.com/whitepapers-reports/)
  * [Ripple Consensus Whitepaper](https://ripple.com/consensus-whitepaper/)
  * [Ripple Solutions Guide](https://ripple.com/files/ripple_solutions_guide.pdf)

To learn about how Ripple is transforming global payments visit
[https://ripple.com/contact/](https://ripple.com/contact/)

- - -

Copyright © 2015, Ripple Labs. All rights reserved.

Portions of this document, including but not limited to the Ripple logo,
images and image templates are the property of Ripple Labs and cannot be
copied or used without permission.
